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

#include "capone/cfg.h"
#include "capone/common.h"
#include "capone/crypto/sign.h"

#include "test.h"

static struct cpn_sign_keys sign_pair;
static struct cpn_sign_pk_hex sign_hex;
static struct cpn_sign_pk pk;

static struct cpn_cfg config;

static void assert_sign_pk_matches(const struct cpn_sign_pk *pk, const char *key)
{
    uint8_t bin[sizeof(pk->data)];
    assert_success(parse_hex(bin, sizeof(bin), key, strlen(key)));
    assert_memory_equal(bin, pk->data, sizeof(pk->data));
}

static void assert_sign_sk_matches(const struct cpn_sign_sk *sk, const char *key)
{
    uint8_t bin[sizeof(sk->data)];
    assert_success(parse_hex(bin, sizeof(bin), key, strlen(key)));
    assert_memory_equal(bin, sk->data, sizeof(sk->data));
}

static int setup()
{
    memset(&sign_pair, 0, sizeof(sign_pair));
    return 0;
}

static int teardown()
{
    cpn_cfg_free(&config);
    return 0;
}

static void generate_sign_key_pair()
{
    assert_success(cpn_sign_keys_generate(&sign_pair));
}

static void sign_key_pair_from_config()
{
    const char text[] =
        "[core]\n"
        "public_key="PK"\n"
        "secret_key="SK"\n";
    assert_success(cpn_cfg_parse_string(&config, text, strlen(text)));

    assert_success(cpn_sign_keys_from_config(&sign_pair, &config));
    assert_sign_pk_matches(&sign_pair.pk, PK);
    assert_sign_sk_matches(&sign_pair.sk, SK);
}

static void sign_key_pair_from_config_with_invalid_pk_fails()
{
    const char text[] =
        "[core]\n"
        "public_key=dbc08ee5b911INVALIDc78f3e35a0091df2e422b471065845c8d227486fb0e54\n"
        "secret_key="SK"\n";

    assert_success(cpn_cfg_parse_string(&config, text, strlen(text)));

    assert_failure(cpn_sign_keys_from_config(&sign_pair, &config));
}

static void sign_key_pair_from_config_with_invalid_sk_fails()
{
    const char text[] =
        "[core]\n"
        "public_key="PK"\n"
        "secret_key=990ce9f899c2b4d3b4fb20af4de539d2b6352ba1fbf658d1d4926123293f61c6"
                   "dbc08ee5INVALID24cfc78f3e35a0091df2e422b471065845c8d227486fb0e54\n";

    assert_success(cpn_cfg_parse_string(&config, text, strlen(text)));

    assert_failure(cpn_sign_keys_from_config(&sign_pair, &config));
}

static void sign_key_pair_from_config_with_missing_pk_fails()
{
    const char text[] =
        "[core]\n"
        "secret_key="SK"\n";
    assert_success(cpn_cfg_parse_string(&config, text, strlen(text)));

    assert_failure(cpn_sign_keys_from_config(&sign_pair, &config));
}

static void sign_key_pair_from_config_with_missing_sk_fails()
{
    const char text[] =
        "[core]\n"
        "public_key="PK"\n";
    assert_success(cpn_cfg_parse_string(&config, text, strlen(text)));

    assert_failure(cpn_sign_keys_from_config(&sign_pair, &config));
}

static void sign_key_pair_from_config_with_invalid_pk_length_fails()
{
    const char text[] =
        "[core]\n"
        "public_key=3d77986bd77de57576a79dddebd7396af9b9f213a8816d6b9ec07d51dc82a51\n"
        "secret_key=9d5e3d6788699115e16214a05b21263bf39e00d7ab5d08ec2b7b1064cafd03e4"
                   "3d77986bd77de57576a79dddebd7396af9b9f213a8816d6b9ec07d51dc82a517\n";
    assert_success(cpn_cfg_parse_string(&config, text, strlen(text)));

    assert_failure(cpn_sign_keys_from_config(&sign_pair, &config));
}

static void sign_key_pair_from_config_with_invalid_sk_length_fails()
{
    const char text[] =
        "[core]\n"
        "public_key=3d77986bd77de57576a79dddebd7396af9b9f213a8816d6b9ec07d51dc82a517\n"
        "secret_key=9d5e3d6788699115e16214a05b21263bf39e00d7ab5d08ec2b7b1064cafd03e4"
                   "3d77986bd77de57576a79dddebd7396af9b9f213a8816d6b9ec07d51dc82a51\n";
    assert_success(cpn_cfg_parse_string(&config, text, strlen(text)));

    assert_failure(cpn_sign_keys_from_config(&sign_pair, &config));
}

static void sign_key_pair_from_missing_file_fails()
{
    assert_failure(cpn_sign_keys_from_config_file(&sign_pair, "/path/to/missing/file"));
}

static void sign_key_public_from_hex_succeeds()
{
    assert_success(cpn_sign_pk_from_hex(&pk, PK));
    assert_sign_pk_matches(&pk, PK);
}

static void sign_key_from_too_short_hex_fails()
{
    assert_failure(cpn_sign_pk_from_hex(&pk, "29d"));
}

static void sign_key_from_too_long_hex_fails()
{
    assert_failure(cpn_sign_pk_from_hex(&pk, PK "1"));
}

static void sign_key_hex_from_bin_succeeds()
{
    assert_success(cpn_sign_pk_from_hex(&pk, PK));
    assert_success(cpn_sign_pk_hex_from_bin(&sign_hex, pk.data, sizeof(pk.data)));
    assert_string_equal(sign_hex.data, PK);
}

static void sign_key_hex_from_bin_with_invalid_lengths_fails()
{
    assert_success(cpn_sign_pk_from_hex(&pk, PK));
    assert_failure(cpn_sign_pk_hex_from_bin(&sign_hex, pk.data, sizeof(pk.data) + 1));
    assert_failure(cpn_sign_pk_hex_from_bin(&sign_hex, pk.data, sizeof(pk.data) - 1));
}

int crypto_sign_test_run_suite(void)
{
    const struct CMUnitTest tests[] = {
        test(generate_sign_key_pair),

        test(sign_key_pair_from_config),
        test(sign_key_pair_from_config_with_invalid_pk_fails),
        test(sign_key_pair_from_config_with_invalid_sk_fails),
        test(sign_key_pair_from_config_with_missing_pk_fails),
        test(sign_key_pair_from_config_with_missing_sk_fails),
        test(sign_key_pair_from_config_with_invalid_pk_length_fails),
        test(sign_key_pair_from_config_with_invalid_sk_length_fails),
        test(sign_key_pair_from_missing_file_fails),

        test(sign_key_public_from_hex_succeeds),
        test(sign_key_from_too_short_hex_fails),
        test(sign_key_from_too_long_hex_fails),

        test(sign_key_hex_from_bin_succeeds),
        test(sign_key_hex_from_bin_with_invalid_lengths_fails),
    };

    return execute_test_suite("sign", tests, setup, teardown);
}
