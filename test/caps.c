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

#define NULL_SECRET "00000000000000000000000000000000" \
                    "00000000000000000000000000000000"
#define SECRET "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define PK "dbc08ee5b91124024cfc78f3e35a0091" \
           "df2e422b471065845c8d227486fb0e54"
#define OTHER_PK "0e29d67c6f96d2594bd7af24dc2ab3bc" \
                 "3eebb1f8444b422e30441b0743d5dde3"

static char *string;

static struct cpn_cap *root;
static struct cpn_cap *ref;
static struct cpn_sign_key_public pk;
static struct cpn_sign_key_public other_pk;

static int setup()
{
    root = NULL;
    ref = NULL;
    string = NULL;
    assert_success(cpn_sign_key_public_from_hex(&pk, PK));
    assert_success(cpn_sign_key_public_from_hex(&other_pk, OTHER_PK));
    return 0;
}

static int teardown()
{
    free(string);
    cpn_cap_free(root);
    cpn_cap_free(ref);
    return 0;
}

static void adding_capability_succeeds()
{
    assert_success(cpn_cap_create_root(&root));
    assert_int_equal(root->chain_depth, 0);
    assert_null(root->chain);
}

static void creating_ref_succeeds()
{
    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));

    assert_int_equal(ref->chain_depth, 1);
    assert_int_equal(ref->chain[0].rights, CPN_CAP_RIGHT_EXEC);
    assert_memory_equal(&ref->chain[0].entity, &pk, sizeof(pk));
}

static void creating_nested_refs_succeeds()
{
    struct cpn_cap *nested;

    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_success(cpn_cap_create_ref(&nested, ref, CPN_CAP_RIGHT_EXEC, &other_pk));

    assert_int_equal(nested->chain_depth, 2);
    assert_int_equal(nested->chain[0].rights, CPN_CAP_RIGHT_EXEC);
    assert_memory_equal(&nested->chain[0].entity, &pk, sizeof(pk));
    assert_int_equal(nested->chain[1].rights, CPN_CAP_RIGHT_EXEC);
    assert_memory_equal(&nested->chain[1].entity, &other_pk, sizeof(other_pk));

    cpn_cap_free(nested);
}

static void creating_nested_refs_with_additional_rights_fails()
{
    struct cpn_cap *nested;

    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_failure(cpn_cap_create_ref(&nested, ref, CPN_CAP_RIGHT_EXEC|CPN_CAP_RIGHT_TERM, &other_pk));
}

static void verifying_valid_ref_succeeds()
{
    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_success(cpn_caps_verify(ref, root, &pk, CPN_CAP_RIGHT_EXEC));
}

static void verifying_valid_ref_with_different_pk_fails()
{
    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_failure(cpn_caps_verify(ref, root, &other_pk, CPN_CAP_RIGHT_EXEC));
}

static void verifying_valid_ref_with_different_rights_fails()
{
    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_failure(cpn_caps_verify(ref, root, &pk, CPN_CAP_RIGHT_TERM));
}

static void verifying_valid_ref_with_additional_rights_fails()
{
    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_failure(cpn_caps_verify(ref, root, &pk, CPN_CAP_RIGHT_EXEC | CPN_CAP_RIGHT_TERM));
}

static void verifying_reference_extending_rights_fails()
{
    struct cpn_cap *other;

    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, CPN_CAP_RIGHT_EXEC, &pk));
    assert_success(cpn_cap_create_ref(&other, root, CPN_CAP_RIGHT_EXEC, &pk));

    other->chain[0].rights |= CPN_CAP_RIGHT_TERM;
    other->chain[1].rights |= CPN_CAP_RIGHT_TERM;

    assert_failure(cpn_caps_verify(other, root, &pk, CPN_CAP_RIGHT_TERM));

    cpn_cap_free(other);
}

static void parsing_cap_succeeds()
{
    char secret[] = SECRET;

    assert_success(cpn_cap_from_string(&ref, secret));
}

static void parsing_cap_with_invalid_secret_length_fails()
{
    char secret[] = SECRET "a";

    assert_failure(cpn_cap_from_string(&ref, secret));
}

static void parsing_cap_with_invalid_secret_chars_fails()
{
    char secret[] = SECRET;
    secret[0] = 'x';

    assert_failure(cpn_cap_from_string(&ref, secret));
}

static void parsing_root_cap_with_rights_fails()
{
    char secret[] = SECRET ":r";

    assert_failure(cpn_cap_from_string(&ref, secret));
}

static void parsing_cap_with_single_chain_succeeds()
{
    char secret[] = SECRET "|" PK ":t";
    assert_success(cpn_cap_from_string(&ref, secret));

    assert_int_equal(ref->chain_depth, 1);
    assert_int_equal(ref->chain[0].rights, CPN_CAP_RIGHT_TERM);
    assert_memory_equal(&ref->chain[0].entity, &pk, sizeof(pk));
}

static void parsing_cap_with_multiple_chain_elements_succeeds()
{
    char secret[] = SECRET "|" PK ":t|" OTHER_PK ":t";
    assert_success(cpn_cap_from_string(&ref, secret));

    assert_int_equal(ref->chain_depth, 2);
    assert_memory_equal(&ref->chain[0].entity, &pk, sizeof(pk));
    assert_int_equal(ref->chain[0].rights, CPN_CAP_RIGHT_TERM);
    assert_memory_equal(&ref->chain[1].entity, &other_pk, sizeof(other_pk));
    assert_int_equal(ref->chain[1].rights, CPN_CAP_RIGHT_TERM);
}

static void parsing_cap_with_extending_rights_fails()
{
    char secret[] = SECRET "|" PK ":t|" PK ":xt";
    assert_failure(cpn_cap_from_string(&ref, secret));
}

static void parsing_cap_with_invalid_right_fails()
{
    char secret[] = SECRET "|" PK ":z";
    assert_failure(cpn_cap_from_string(&ref, secret));
}

static void cap_to_string_succeeds_with_root_ref()
{
    struct cpn_cap cap;

    memset(&cap, 0, sizeof(struct cpn_cap));

    assert_success(cpn_cap_to_string(&string, &cap));
    assert_string_equal(string, NULL_SECRET);
}

static void cap_to_string_succeeds_with_reference()
{
    struct cpn_cap cap;

    memset(&cap, 0, sizeof(struct cpn_cap));

    assert_success(cpn_cap_create_ref(&ref, &cap, CPN_CAP_RIGHT_EXEC | CPN_CAP_RIGHT_TERM, &pk));

    assert_success(cpn_cap_to_string(&string, ref));
    assert_string_equal(string, "60d5175bdb2dcb0bdb6eb4884e082414"
                                "20f6bb3b8eded2d33b2b3f8f29951bde"
                                "|" PK ":xt");
}

static void reference_to_string_fails_without_rights()
{
    assert_success(cpn_cap_create_root(&root));
    assert_success(cpn_cap_create_ref(&ref, root, 0, &pk));

    assert_failure(cpn_cap_to_string(&string, ref));
}

int caps_test_run_suite(void)
{
    const struct CMUnitTest tests[] = {
        test(adding_capability_succeeds),

        test(creating_ref_succeeds),
        test(creating_nested_refs_succeeds),
        test(creating_nested_refs_with_additional_rights_fails),

        test(verifying_valid_ref_succeeds),
        test(verifying_valid_ref_with_different_pk_fails),
        test(verifying_valid_ref_with_different_rights_fails),
        test(verifying_valid_ref_with_additional_rights_fails),
        test(verifying_reference_extending_rights_fails),

        test(parsing_cap_succeeds),
        test(parsing_cap_with_invalid_secret_length_fails),
        test(parsing_cap_with_invalid_secret_chars_fails),
        test(parsing_root_cap_with_rights_fails),
        test(parsing_cap_with_single_chain_succeeds),
        test(parsing_cap_with_multiple_chain_elements_succeeds),
        test(parsing_cap_with_extending_rights_fails),
        test(parsing_cap_with_invalid_right_fails),

        test(cap_to_string_succeeds_with_root_ref),
        test(cap_to_string_succeeds_with_reference),
        test(reference_to_string_fails_without_rights)
    };

    return execute_test_suite("caps", tests, NULL, NULL);
}
