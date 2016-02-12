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

#include "proto/query.pb-c.h"

#include "lib/common.h"
#include "lib/server.h"

static struct sd_keys keys;

static int negotiate_encryption(struct sd_channel *channel)
{
    uint8_t nonce[crypto_box_NONCEBYTES];
    struct sd_keys_public remote_keys;
    QueryResponse response = QUERY_RESPONSE__INIT;
    QueryMessage *query;
    Envelope *env;

    if (sd_channel_receive_protobuf(channel,
            (ProtobufCMessageDescriptor *) &envelope__descriptor,
            (ProtobufCMessage **) &env) < 0) {
        puts("Failed receiving protobuf");
        return -1;
    }

    if (unpack_signed_protobuf(&query_message__descriptor,
                (ProtobufCMessage **) &query, env, &keys) < 0) {
        puts("Failed unpacking protobuf");
        return -1;
    }
    if (sd_keys_public_from_bin(&remote_keys, env->pk.data, env->pk.len) < 0 ) {
        puts("Could not extract remote keys");
        return -1;
    }
    query_message__free_unpacked(query, NULL);
    envelope__free_unpacked(env, NULL);

    /* TODO: use correct nonce */
    randombytes_buf(nonce, sizeof(nonce));
    response.nonce.data = nonce;
    response.nonce.len = sizeof(nonce);

    if (pack_signed_protobuf(&env, (ProtobufCMessage *) &response,
                &keys, &remote_keys) < 0) {
        puts("Could not pack query");
        return -1;
    }
    if (sd_channel_write_protobuf(channel, (ProtobufCMessage *) env) < 0) {
        puts("Could not send query");
        return -1;
    }
    envelope__free_unpacked(env, NULL);

    sd_channel_set_crypto_encrypt(channel, &keys, &remote_keys, nonce, query->nonce.data);

    return 0;
}

static int handle_connect(struct sd_channel *channel)
{
    if (negotiate_encryption(channel) < 0) {
        puts("Unable to negotiate encryption");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *config, *port;
    struct sd_channel channel;
    struct sd_server server;

    if (argc != 3) {
        printf("USAGE: %s <CONFIG> <PORT>\n", argv[0]);
        return -1;
    }

    config = argv[1];
    port = argv[2];

    if (sodium_init() < 0) {
        puts("Could not init libsodium");
        return -1;
    }

    if (sd_keys_from_config_file(&keys, config) < 0) {
        puts("Could not parse config");
        return -1;
    }

    if (sd_server_init(&server, NULL, port, SD_CHANNEL_TYPE_TCP) < 0) {
        puts("Could not set up server");
        return -1;
    }

    if (sd_server_listen(&server) < 0) {
        puts("Could not start listening");
        return -1;
    }

    if (sd_server_accept(&server, &channel) < 0) {
        puts("Could not accept connection");
        return -1;
    }

    handle_connect(&channel);

    sd_server_close(&server);

    return 0;
}
