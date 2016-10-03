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
#include <sodium.h>

#include "capone/common.h"
#include "capone/log.h"

#include "capone/crypto/hash.h"
#include "capone/crypto/symmetric.h"

int cpn_symmetric_key_generate(struct cpn_symmetric_key *out)
{
    randombytes(out->data, sizeof(out->data));
    return 0;
}

int cpn_symmetric_key_from_hex(struct cpn_symmetric_key *out, const char *hex)
{
    if (hex == NULL) {
        cpn_log(LOG_LEVEL_ERROR, "Error parsing nonexistent symmetric key");
        return -1;
    }

    if (parse_hex(out->data, sizeof(out->data), hex, strlen(hex)) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Error parsing invalid symmetric key");
        return -1;
    }
    return 0;
}

int cpn_symmetric_key_from_bin(struct cpn_symmetric_key *out, const uint8_t *key, size_t keylen)
{
    if (keylen != crypto_secretbox_KEYBYTES) {
        cpn_log(LOG_LEVEL_ERROR, "Passed in buffer does not match required symmetric key length");
        return -1;
    }

    memcpy(out->data, key, sizeof(out->data));

    return 0;
}

int cpn_symmetric_key_from_scalarmult(struct cpn_symmetric_key *out,
        const struct cpn_asymmetric_keys *keys, const struct cpn_asymmetric_pk *pk, bool localfirst)
{
    uint8_t scalarmult[crypto_scalarmult_BYTES];
    struct cpn_hash_state hash;
    int err = 0;

    if (crypto_scalarmult(scalarmult, keys->sk.data, pk->data) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Unable to perform scalarmultiplication");
        return -1;
    }

    err |= cpn_hash_init(&hash, sizeof(out->data));

    err |= cpn_hash_update(&hash, scalarmult, sizeof(scalarmult));
    if (localfirst) {
        err |= cpn_hash_update(&hash, keys->pk.data, sizeof(keys->pk.data));
        err |= cpn_hash_update(&hash, pk->data, sizeof(pk->data));
    } else {
        err |= cpn_hash_update(&hash, pk->data, sizeof(pk->data));
        err |= cpn_hash_update(&hash, keys->pk.data, sizeof(keys->pk.data));
    }

    err |= cpn_hash_final(out->data, &hash);

    if (err) {
        cpn_log(LOG_LEVEL_ERROR, "Unable to calculate h(q || pk1 || pk2)");
        return -1;
    }

    return 0;
}

int cpn_symmetric_key_hex_from_bin(struct cpn_symmetric_key_hex *out, const uint8_t *data, size_t datalen)
{
    struct cpn_symmetric_key key;

    if (cpn_symmetric_key_from_bin(&key, data, datalen) < 0)
        return -1;

    cpn_symmetric_key_hex_from_key(out, &key);
    return 0;
}

void cpn_symmetric_key_hex_from_key(struct cpn_symmetric_key_hex *out, const struct cpn_symmetric_key *key)
{
    sodium_bin2hex(out->data, sizeof(out->data), key->data, sizeof(key->data));
}

int cpn_symmetric_key_encrypt(uint8_t *out, const struct cpn_symmetric_key *key,
        const struct cpn_symmetric_key_nonce *nonce, uint8_t *data, size_t datalen)
{
    return crypto_secretbox_easy(out, data, datalen, nonce->data, key->data);
}

int cpn_symmetric_key_decrypt(uint8_t *out, const struct cpn_symmetric_key *key,
        const struct cpn_symmetric_key_nonce *nonce, uint8_t *data, size_t datalen)
{
    return crypto_secretbox_open_easy(out, data, datalen, nonce->data, key->data);
}

void cpn_symmetric_key_nonce_increment(struct cpn_symmetric_key_nonce *nonce, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        sodium_increment(nonce->data, sizeof(nonce->data));
}
