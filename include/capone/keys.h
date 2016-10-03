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

/**
 * \defgroup cpn-keys Key Management
 * \ingroup cpn-lib
 *
 * @brief Module for key handling
 *
 * This module provides several structs for keys and functions
 * used to read data into these structs. Like this, we can
 * provide opaque handling of the different structures required
 * for different kind of keys.
 *
 * The module provides three kind of keys:
 *  - Signing key pairs provide keys used for signing and
 *    verifying data. They are usually used to represent an
 *    entity and are thus long-term keys.
 *  - Encrypt keys pairs are used for encrypting and decrypting
 *    data with an asymmetric key pair. Signature keys cannot be
 *    used for this use case.
 *  - Symmetric keys are used for encrypting data with a single
 *    key shared between participating parties.
 *
 * @{
 */

#ifndef CPN_LIB_KEYS_H
#define CPN_LIB_KEYS_H

#include <sodium.h>

#include "capone/cfg.h"
#include "capone/proto/core.pb-c.h"

/** @brief Secret encryption key used to decrypt data */
struct cpn_encrypt_key_secret {
    uint8_t data[crypto_box_SECRETKEYBYTES];
};
/** @brief Public encryption key used to encrypt data */
struct cpn_encrypt_key_public {
    uint8_t data[crypto_box_PUBLICKEYBYTES];
};
/** @brief Encryption key pair */
struct cpn_encrypt_key_pair {
    struct cpn_encrypt_key_secret sk;
    struct cpn_encrypt_key_public pk;
};

/** @brief Symmetric key used to encrypt/decrypt data */
struct cpn_symmetric_key {
    uint8_t data[crypto_secretbox_KEYBYTES];
};
/** @brief Hex representation of a symmetric key */
struct cpn_symmetric_key_hex {
    char data[crypto_secretbox_KEYBYTES * 2 + 1];
};
/** @brief Generate a new encryption key pair
 *
 * @param[out] out Pointer to store public encryption key pair at.
 * @return <code>0</code>
 */
int cpn_encrypt_key_pair_generate(struct cpn_encrypt_key_pair *out);

/** @brief Read a public encryption key from binary data
 *
 * @param[out] out Pointer to store public encryption key at.
 * @param[in] pk Binary representation of the key.
 * @param[in] pklen Length of the binary data.
 * @return <code>0</code> on success, <code>-1</code> otherwise
 */
int cpn_encrypt_key_public_from_bin(struct cpn_encrypt_key_public *out,
        uint8_t *pk, size_t pklen);

/** @brief Generate a new symmetric key
 *
 * @param[out] out Pointer to store symmetric key at.
 * @return <code>0</code>
 */
int cpn_symmetric_key_generate(struct cpn_symmetric_key *out);

/** @brief Read a symmetric key from hex
 *
 * @param[out] out Pointer to store symmetric key at.
 * @param[in] hex Hex representation of the key.
 * @return <code>0</code> on success, <code>-1</code> otherwise
 */
int cpn_symmetric_key_from_hex(struct cpn_symmetric_key *out, const char *hex);

/** @brief Read a symmetric key from binary data
 *
 * @param[out] out Pointer to store symmetric key at.
 * @param[in] pk Binary representation of the key.
 * @param[in] pklen Length of the binary data.
 * @return <code>0</code> on success, <code>-1</code> otherwise
 */
int cpn_symmetric_key_from_bin(struct cpn_symmetric_key *out, const uint8_t *key, size_t keylen);

/** @brief Read a symmetric key from binary data
 *
 * @param[out] out Pointer to store symmetric key at.
 * @param[in] pk Binary representation of the key.
 * @param[in] pklen Length of the binary data.
 * @return <code>0</code> on success, <code>-1</code> otherwise
 */
int cpn_symmetric_key_hex_from_bin(struct cpn_symmetric_key_hex *out, const uint8_t *data, size_t datalen);

/** @brief Convert symmetric key into hex representation
 *
 * @param[out] out Pointer to store symmetric hex
 *             representation at.
 * @param[in] key Public signature key to convert.
 */
void cpn_symmetric_key_hex_from_key(struct cpn_symmetric_key_hex *out, const struct cpn_symmetric_key *key);

#endif

/** @} */
