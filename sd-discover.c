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

#include "lib/common.h"
#include "lib/cfg.h"
#include "lib/log.h"
#include "lib/proto.h"
#include "lib/server.h"

#include "proto/discovery.pb-c.h"

#define LISTEN_PORT 6668

static struct sd_sign_key_pair local_keys;

static void probe(void *payload)
{
    DiscoverMessage msg = DISCOVER_MESSAGE__INIT;
    struct sd_channel channel;

    UNUSED(payload);

    msg.version = VERSION;
    msg.port = LISTEN_PORT;
    msg.sign_key.data = local_keys.pk.data;
    msg.sign_key.len = sizeof(local_keys.pk.data);

    if (sd_channel_init_from_host(&channel, "224.0.0.1", "6667", SD_CHANNEL_TYPE_UDP) < 0) {
        puts("Unable to initialize channel");
        goto out;
    }

    while (true) {
        if (sd_channel_write_protobuf(&channel, &msg.base) < 0) {
            puts("Unable to write protobuf");
            goto out;
        }

        sd_log(LOG_LEVEL_DEBUG, "Sent probe message");

        sleep(5);
    }

out:
    sd_channel_close(&channel);
}

static void handle_announce()
{
    struct sd_sign_key_hex remote_key;
    struct sd_server server;
    struct sd_channel channel;
    AnnounceMessage *announce = NULL;
    unsigned i;

    if (sd_server_init(&server, NULL, "6668", SD_CHANNEL_TYPE_UDP) < 0) {
        puts("Unable to init listening channel");
        goto out;
    }

    if (sd_server_accept(&server, &channel) < 0) {
        puts("Unable to accept connection");
        goto out;
    }

    if (sd_channel_receive_protobuf(&channel,
                (ProtobufCMessageDescriptor *) &announce_message__descriptor,
                (ProtobufCMessage **) &announce) < 0) {
        puts("Unable to receive protobuf");
        goto out;
    }

    if (sd_sign_key_hex_from_bin(&remote_key,
                announce->sign_key.data, announce->sign_key.len) < 0)
    {
        puts("Unable to retrieve remote sign key");
        goto out;
    }

    printf("%s (v%s)\n", remote_key.data, announce->version);

    for (i = 0; i < announce->n_services; i++) {
        AnnounceMessage__Service *service = announce->services[i];

        printf("\t%s -> %s (%s)\n", service->port, service->name, service->type);
    }

out:
    announce_message__free_unpacked(announce, NULL);
    sd_channel_close(&channel);
}

int main(int argc, char *argv[])
{
    int pid;

    if (sodium_init() < 0) {
        return -1;
    }

    if (argc != 2) {
        printf("USAGE: %s <CONFIG>\n", argv[0]);
        return -1;
    }

    if (sd_sign_key_pair_from_config_file(&local_keys, argv[1]) < 0) {
        puts("Could not parse config");
        return -1;
    }

    pid = spawn(probe, NULL);
    handle_announce();

    kill(pid, SIGTERM);
    waitpid(-1, NULL, 0);

    return 0;
}
