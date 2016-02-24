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

#include "lib/cfg.h"
#include "lib/log.h"

#include "keys.h"

int sd_key_pair_from_config_file(struct sd_key_pair *out, const char *file)
{
    uint8_t sign_pk[crypto_sign_ed25519_PUBLICKEYBYTES];
    uint8_t sign_sk[crypto_sign_ed25519_SECRETKEYBYTES];
    uint8_t box_pk[crypto_scalarmult_curve25519_BYTES];
    uint8_t box_sk[crypto_scalarmult_curve25519_BYTES];
    struct cfg cfg;
    char *value;

    if (cfg_parse(&cfg, file) < 0) {
        return -1;
    }

    value = cfg_get_str_value(&cfg, "core", "public_key");
    if (value == NULL) {
        puts("Could not retrieve public key from config");
        goto out_err;
    }
    if (sodium_hex2bin(sign_pk, sizeof(sign_pk), value, strlen(value), NULL, NULL, NULL) < 0) {
        puts("Could not decode public key");
        goto out_err;
    }
    free(value);

    value = cfg_get_str_value(&cfg, "core", "secret_key");
    if (value == NULL) {
        puts("Could not retrieve secret key from config");
        goto out_err;
    }
    if (sodium_hex2bin(sign_sk, sizeof(sign_sk), value, strlen(value), NULL, NULL, NULL)) {
        puts("Could not decode public key");
        goto out_err;
    }
    free(value);
    value = NULL;

    if (crypto_sign_ed25519_pk_to_curve25519(box_pk, sign_pk) < 0) {
        puts("Could not convert public key to curve52219");
        goto out_err;
    }
    if (crypto_sign_ed25519_sk_to_curve25519(box_sk, sign_sk) < 0) {
        puts("Could not convert public key to curve52219");
        goto out_err;
    }

    memcpy(out->pk.sign, sign_pk, sizeof(sign_pk));
    memcpy(out->sk.sign, sign_sk, sizeof(sign_sk));
    memcpy(out->pk.box, box_pk, sizeof(box_pk));
    memcpy(out->sk.box, box_sk, sizeof(box_sk));

    cfg_free(&cfg);

    return 0;

out_err:
    free(value);
    cfg_free(&cfg);

    return -1;
}

int sd_key_public_from_hex(struct sd_key_public *out, const char *hex)
{
    int len;
    uint8_t sign_pk[crypto_sign_ed25519_PUBLICKEYBYTES],
        box_pk[crypto_scalarmult_curve25519_BYTES];

    len = strlen(hex);
    if (len != 2 * crypto_sign_PUBLICKEYBYTES) {
        sd_log(LOG_LEVEL_ERROR, "Passed in buffer does not match required public key length");
        return -1;
    }

    if (sodium_hex2bin(sign_pk, sizeof(sign_pk), hex, len, NULL, NULL, NULL) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not decode hex");
        return -1;
    }

    if (crypto_sign_ed25519_pk_to_curve25519(box_pk, sign_pk) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not convert public key to curve52219");
        return -1;
    }

    memcpy(out->box, box_pk, sizeof(out->box));
    memcpy(out->sign, sign_pk, sizeof(out->sign));

    return 0;
}

int sd_key_public_from_bin(struct sd_key_public *out, uint8_t *data, size_t len)
{
    uint8_t box_pk[crypto_scalarmult_curve25519_BYTES];

    if (len != crypto_sign_PUBLICKEYBYTES) {
        sd_log(LOG_LEVEL_ERROR, "Passed in buffer does not match required public key length");
        return -1;
    }

    if (crypto_sign_ed25519_pk_to_curve25519(box_pk, data) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not convert public key to curve52219");
        return -1;
    }

    memcpy(out->box, box_pk, sizeof(out->box));
    memcpy(out->sign, data, len);

    return 0;
}

int sd_key_symmetric_from_hex(struct sd_key_symmetric *out, const char *hex)
{
    int len;
    uint8_t key[crypto_secretbox_KEYBYTES];

    len = strlen(hex);
    if (len != 2 * crypto_secretbox_KEYBYTES) {
        sd_log(LOG_LEVEL_ERROR, "Passed in buffer does not match required symmetric key length");
        return -1;
    }

    if (sodium_hex2bin(key, sizeof(key), hex, len, NULL, NULL, NULL) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not decode hex");
        return -1;
    }

    memcpy(out->key, key, sizeof(out->key));

    return 0;

}
