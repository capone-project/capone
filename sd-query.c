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

#include <string.h>
#include <stdio.h>
#include <sodium.h>

#include "proto/query.pb-c.h"

#include "lib/channel.h"
#include "lib/common.h"

static struct sd_channel channel;
static struct sd_keys keys;
static struct sd_keys_public remote_keys;

int query(void)
{
    uint8_t nonce[crypto_box_NONCEBYTES];
    QueryMessage query = QUERY_MESSAGE__INIT;
    QueryResponse *response;
    Envelope *env;

    /* TODO: use correct nonce */
    randombytes_buf(nonce, sizeof(nonce));
    query.nonce.data = nonce;
    query.nonce.len = sizeof(nonce);

    if (pack_signed_protobuf(&env, (ProtobufCMessage *) &query,
                &keys, &remote_keys) < 0) {
        puts("Could not pack query");
        return -1;
    }
    if (sd_channel_write_protobuf(&channel, (ProtobufCMessage *) env) < 0) {
        puts("Could not send query");
        return -1;
    }
    envelope__free_unpacked(env, NULL);

    if (sd_channel_receive_protobuf(&channel,
            (ProtobufCMessageDescriptor *) &envelope__descriptor,
            (ProtobufCMessage **) &env) < 0) {
        puts("Failed receiving query response");
        return -1;
    }
    if (unpack_signed_protobuf(&query_response__descriptor,
                (ProtobufCMessage **) &response, env, &keys) < 0) {
        puts("Failed unpacking protobuf");
        return -1;
    }

    puts("Exchanged nonces");

    return 0;
}

int main(int argc, char *argv[])
{
    const char *config, *key, *host, *port;

    if (argc != 5) {
        printf("USAGE: %s <CONFIG> <KEY> <HOST> <PORT>\n", argv[0]);
        return -1;
    }

    config = argv[1];
    key = argv[2];
    host = argv[3];
    port = argv[4];

    if (sodium_init() < 0) {
        puts("Could not init libsodium");
        return -1;
    }

    if (sd_keys_from_config_file(&keys, config) < 0) {
        puts("Could not parse config");
        return -1;
    }

    if (sd_keys_public_from_hex(&remote_keys, key) < 0) {
        puts("Could not parse remote public key");
        return -1;
    }

    if (sd_channel_init_from_host(&channel, host, port, SD_CHANNEL_TYPE_TCP) < 0) {
        puts("Could not initialize channel");
        return -1;
    }

    if (sd_channel_connect(&channel) < 0) {
        puts("Could not connect to server");
        return -1;
    }

    if (query() < 0) {
        puts("Could not query server");
        return -1;
    }

    sd_channel_close(&channel);

    return 0;
}
