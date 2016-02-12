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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib/cfg.h"
#include "lib/log.h"
#include "lib/keys.h"

#include "common.h"

int spawn(thread_fn fn, void *payload)
{
    int pid = fork();

    if (pid == 0) {
        /* child */
        fn(payload);
        exit(0);
    } else if (pid > 0) {
        /* parent */
        return pid;
    } else {
        printf("Could not spawn function: %s\n", strerror(errno));
        return -1;
    }
}

int pack_signed_protobuf(Envelope **out, const ProtobufCMessage *msg,
        const struct sd_keys *keys, const struct sd_keys_public *remote_key)
{
    Envelope *env;
    uint8_t mac[crypto_sign_BYTES];
    uint8_t *buf;
    int len;

    *out = NULL;

    len = protobuf_c_message_get_packed_size(msg);
    buf = malloc(len);
    protobuf_c_message_pack(msg, buf);

    env = malloc(sizeof(Envelope));
    envelope__init(env);

    env->data.data = buf;
    env->data.len = len;

    if (crypto_sign_detached(mac, NULL, buf, len, keys->sk.sign) != 0) {
        sd_log(LOG_LEVEL_ERROR, "Unable to sign protobuf");
        return -1;
    }
    env->mac.data = malloc(crypto_sign_BYTES);
    memcpy(env->mac.data, mac, crypto_sign_BYTES);
    env->mac.len = crypto_sign_BYTES;

    if (remote_key) {
        buf = realloc(buf, len + sizeof(crypto_box_SEALBYTES));

        if (crypto_box_seal(buf, buf, len, remote_key->box) < 0) {
            sd_log(LOG_LEVEL_ERROR, "Unable to encrypt protobuf");
            return -1;
        }

        len = len + crypto_box_SEALBYTES;
        env->encrypted = 1;
    } else {
        env->encrypted = 0;
    }
    env->pk.data = malloc(sizeof(keys->pk.sign));
    memcpy(env->pk.data, keys->pk.sign, sizeof(keys->pk.sign));
    env->pk.len = sizeof(keys->pk.sign);

    *out = env;

    return 0;
}

int unpack_signed_protobuf(const ProtobufCMessageDescriptor *descr,
        ProtobufCMessage **out, const Envelope *env, const struct sd_keys *keys)
{
    ProtobufCMessage *msg;

    *out = NULL;

    if (env->mac.len != crypto_sign_BYTES) {
        sd_log(LOG_LEVEL_ERROR, "Invalid MAC length");
        return -1;
    }
    if (env->pk.len != crypto_sign_ed25519_PUBLICKEYBYTES) {
        sd_log(LOG_LEVEL_ERROR, "Invalid public key length");
        return -1;
    }

    if (env->encrypted) {
        if (crypto_box_seal_open(env->data.data, env->data.data, env->data.len,
                env->mac.data, keys->sk.box) < 0) {
            sd_log(LOG_LEVEL_ERROR, "Unable to decrypt protobuf");
            return -1;
        }
    }

    if (crypto_sign_verify_detached(env->mac.data,
                env->data.data, env->data.len, env->pk.data) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Unable to verify signed protobuf");
        return -1;
    }

    msg = protobuf_c_message_unpack(descr, NULL,
            env->data.len, env->data.data);
    if (msg == NULL) {
        sd_log(LOG_LEVEL_ERROR, "Unable to unpack signed protobuf");
        return -1;
    }

    *out = msg;

    return 0;
}
