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

#include "capone/caps.h"
#include "capone/common.h"

#include "test.h"

static struct cpn_cap root;
static struct cpn_cap cap;
static struct cpn_sign_key_public pk;
static struct cpn_sign_key_public other_pk;

static int setup()
{
    memset(&root, 0, sizeof(root));
    memset(&cap, 0, sizeof(cap));
    other_pk.data[0] = 1;
    return 0;
}

static int teardown()
{
    return 0;
}

static void adding_capability_succeeds()
{
    assert_success(cpn_cap_init(&root));
}

static void adding_multiple_capabilities_succeeds()
{
    struct cpn_cap caps[10];
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(caps); i++)
        assert_success(cpn_cap_init(&caps[i]));
    for (i = 0; i < ARRAY_SIZE(caps) - 1; i++)
        assert_memory_not_equal(&caps[i].secret, &caps[i + 1].secret, sizeof(caps[i].secret));
}

static void creating_ref_succeeds()
{
    assert_success(cpn_cap_init(&root));
    assert_success(cpn_caps_create_reference(&cap, &root, CPN_CAP_RIGHT_EXEC, &pk));
}

static void verifying_valid_ref_succeeds()
{
    assert_success(cpn_cap_init(&root));
    assert_success(cpn_caps_create_reference(&cap, &root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_success(cpn_caps_verify(&cap, &root, &pk, CPN_CAP_RIGHT_EXEC));
}

static void verifying_valid_ref_with_different_pk_fails()
{
    assert_success(cpn_cap_init(&root));
    assert_success(cpn_caps_create_reference(&cap, &root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_failure(cpn_caps_verify(&cap, &root, &other_pk, CPN_CAP_RIGHT_EXEC));
}

static void verifying_valid_ref_with_different_rights_fails()
{
    assert_success(cpn_cap_init(&root));
    assert_success(cpn_caps_create_reference(&cap, &root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_failure(cpn_caps_verify(&cap, &root, &pk, CPN_CAP_RIGHT_TERM));
}

static void verifying_valid_ref_with_additional_rights_fails()
{
    assert_success(cpn_cap_init(&root));
    assert_success(cpn_caps_create_reference(&cap, &root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_failure(cpn_caps_verify(&cap, &root, &pk, CPN_CAP_RIGHT_EXEC | CPN_CAP_RIGHT_TERM));
}

static void parsing_cap_succeeds()
{
    char secret[CPN_CAP_SECRET_LEN * 2 + 1];

    memset(secret, 'a', sizeof(secret) - 1);
    secret[sizeof(secret) - 1] = '\0';

    assert_success(cpn_cap_parse(&cap, secret, CPN_CAP_RIGHT_EXEC));
}

static void parsing_cap_with_invalid_secret_length_fails()
{
    char secret[CPN_CAP_SECRET_LEN * 2];

    memset(secret, 'a', sizeof(secret) - 1);
    secret[sizeof(secret) - 1] = '\0';

    assert_failure(cpn_cap_parse(&cap, secret, CPN_CAP_RIGHT_EXEC));
}

static void parsing_cap_with_invalid_secret_chars_fails()
{
    char secret[CPN_CAP_SECRET_LEN * 2];

    memset(secret, 'a', sizeof(secret) - 1);
    secret[sizeof(secret) - 2] = 'x';
    secret[sizeof(secret) - 1] = '\0';

    assert_failure(cpn_cap_parse(&cap, secret, CPN_CAP_RIGHT_EXEC));
}

int caps_test_run_suite(void)
{
    const struct CMUnitTest tests[] = {
        test(adding_capability_succeeds),
        test(adding_multiple_capabilities_succeeds),

        test(creating_ref_succeeds),

        test(verifying_valid_ref_succeeds),
        test(verifying_valid_ref_with_different_pk_fails),
        test(verifying_valid_ref_with_different_rights_fails),
        test(verifying_valid_ref_with_additional_rights_fails),

        test(parsing_cap_succeeds),
        test(parsing_cap_with_invalid_secret_length_fails),
        test(parsing_cap_with_invalid_secret_chars_fails)
    };

    return execute_test_suite("caps", tests, NULL, NULL);
}
