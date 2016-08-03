/*
 * Copyright (C) 2016 Patrick Steinhardt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "capone/cmdparse.h"
#include "capone/common.h"
#include "capone/cfg.h"
#include "capone/log.h"
#include "capone/proto.h"
#include "capone/server.h"

#include "capone/proto/discovery.pb-c.h"

#define LISTEN_PORT 6668

static struct cpn_sign_key_pair local_keys;

static struct known_keys {
    struct cpn_sign_key_public key;
    struct known_keys *next;
} *known_keys;

static int send_discover(struct cpn_channel *channel)
{
    DiscoverMessage msg = DISCOVER_MESSAGE__INIT;
    struct known_keys *it;
    size_t i, keys;
    int err;

    msg.version = VERSION;
    msg.port = LISTEN_PORT;

    for (keys = 0, it = known_keys; it && keys < 50; it = it->next, keys++);

    msg.n_known_keys = keys;
    if (keys > 0) {
        msg.known_keys = calloc(keys, sizeof(ProtobufCBinaryData));

        for (i = 0, it = known_keys; i < keys; i++, it = it->next) {
            msg.known_keys[i].len = sizeof(struct cpn_sign_key_public);
            msg.known_keys[i].data = malloc(sizeof(struct cpn_sign_key_public));
            memcpy(msg.known_keys[i].data, &it->key.data, sizeof(struct cpn_sign_key_public));
        }
    } else {
        msg.known_keys = NULL;
    }

    err = cpn_channel_write_protobuf(channel, &msg.base);

    for (i = 0; i < keys; i++) {
        free(msg.known_keys[i].data);
    }
    free(msg.known_keys);

    if (err)
        cpn_log(LOG_LEVEL_ERROR, "Unable to send discover: %s", strerror(errno));

    return err;
}

static void *probe(void *payload)
{
    struct cpn_channel channel;

    UNUSED(payload);

    if (cpn_channel_init_from_host(&channel, "224.0.0.1", "6667", CPN_CHANNEL_TYPE_UDP) < 0) {
        puts("Unable to initialize channel");
        goto out;
    }

    while (true) {
        if (send_discover(&channel) < 0) {
            puts("Unable to write protobuf");
            goto out;
        }

        cpn_log(LOG_LEVEL_DEBUG, "Sent probe message");

        sleep(5);
    }

out:
    cpn_channel_close(&channel);

    return NULL;
}

static int handle_announce(struct cpn_channel *channel)
{
    struct known_keys *it;
    struct cpn_sign_key_hex remote_key;
    AnnounceMessage *announce = NULL;
    unsigned i = 0;
    int err = -1;

    if (cpn_channel_receive_protobuf(channel,
                (ProtobufCMessageDescriptor *) &announce_message__descriptor,
                (ProtobufCMessage **) &announce) < 0) {
        puts("Unable to receive protobuf");
        goto out;
    }

    if (cpn_sign_key_hex_from_bin(&remote_key,
                announce->sign_key.data, announce->sign_key.len) < 0)
    {
        puts("Unable to retrieve remote sign key");
        goto out;
    }

    for (it = known_keys; it; it = it->next) {
        if (!memcmp(it->key.data, announce->sign_key.data,
                    sizeof(struct cpn_sign_key_public)))
        {
            cpn_log(LOG_LEVEL_DEBUG, "Ignoring known key %s", remote_key.data);
            err = 0;
            goto out;
        }
    }

    it = malloc(sizeof(struct known_keys));
    it->next = known_keys;
    memcpy(&it->key, announce->sign_key.data, sizeof(struct cpn_sign_key_public));
    known_keys = it;

    printf("%s - %s (v%s)\n", announce->name, remote_key.data, announce->version);

    for (i = 0; i < announce->n_services; i++) {
        AnnounceMessage__Service *service = announce->services[i];

        printf("\t%s -> %s (%s)\n", service->port, service->name, service->category);
    }

    err = 0;

out:
    if (announce)
        announce_message__free_unpacked(announce, NULL);

    return err;
}

static void undirected_discovery()
{
    struct cpn_server server;
    struct cpn_channel channel;
    struct cpn_thread t;

    channel.fd = -1;

    cpn_spawn(&t, probe, NULL);

    if (cpn_server_init(&server, NULL, "6668", CPN_CHANNEL_TYPE_UDP) < 0) {
        puts("Unable to init listening channel");
        goto out;
    }

    if (cpn_server_enable_broadcast(&server) < 0) {
        puts("Unable to enable broadcasting");
        goto out;
    }

    if (cpn_server_accept(&server, &channel) < 0) {
        puts("Unable to accept connection");
        goto out;
    }

    while (true) {
        if (handle_announce(&channel) < 0) {
            puts("Unable to handle announce");
            goto out;
        }
    }

out:
    cpn_kill(&t);
}

static void directed_discovery(const struct cpn_sign_key_public *remote_key,
        const char *host, const char *port)
{
    struct cpn_channel channel;

    if (cpn_channel_init_from_host(&channel, host, port, CPN_CHANNEL_TYPE_TCP) < 0) {
        puts("Unable to initiate channel");
        goto out;
    }

    if (cpn_channel_connect(&channel) < 0) {
        puts("Unable to connect");
        goto out;
    }

    if (cpn_proto_initiate_encryption(&channel, &local_keys, remote_key) < 0) {
        puts("Unable to initiate encryption");
        goto out;
    }

    if (send_discover(&channel) < 0) {
        puts("Unable to send directed discover");
        goto out;
    }

    if (handle_announce(&channel) < 0) {
        puts("Unable to handle announce");
        goto out;
    }

out:
    cpn_channel_close(&channel);
}

int main(int argc, const char *argv[])
{
    static struct cpn_cmdparse_opt directed_opts[] = {
        CPN_CMDPARSE_OPT_SIGKEY(0, "--remote-key",
                "Public signature key of the host to query", "KEY", false),
        CPN_CMDPARSE_OPT_STRING(0, "--remote-host",
                "Network address of the host to query", "ADDRESS", false),
        CPN_CMDPARSE_OPT_STRING(0, "--remote-port",
                "Port of the host to query", "PORT", false),
        CPN_CMDPARSE_OPT_END
    };
    struct cpn_cmdparse_opt opts[] = {
        CPN_CMDPARSE_OPT_STRING('c', "--config", "Configuration file", "FILE", false),
        CPN_CMDPARSE_OPT_ACTION("broadcast", NULL, NULL),
        CPN_CMDPARSE_OPT_ACTION("direct", NULL, directed_opts),
        CPN_CMDPARSE_OPT_END
    };

    if (cpn_cmdparse_parse_cmd(opts, argc, argv) < 0)
        return -1;

    if (sodium_init() < 0)
        return -1;

    if (opts[1].set) {
        undirected_discovery();
    } else if (opts[2].set) {
        directed_discovery(&directed_opts[0].value.sigkey,
                directed_opts[1].value.string,
                directed_opts[1].value.string);
    } else {
        puts("No action specified");
    }

    return 0;
}
