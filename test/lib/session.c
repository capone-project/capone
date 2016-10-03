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

#include "capone/common.h"
#include "capone/session.h"
#include "capone/service.h"

#include "test.h"
#include "lib/test.pb-c.h"

static struct cpn_sign_key_public pk;
static const struct cpn_session *session;

static int setup()
{
    return 0;
}

static int teardown()
{
    session = NULL;
    assert_success(cpn_sessions_clear());
    return 0;
}

static void add_sessions_adds_session()
{
    struct cpn_session *removed;

    assert_success(cpn_sessions_add(&session, NULL, &pk));
    assert_success(cpn_sessions_remove(&removed, session->identifier));

    assert_int_equal(removed->identifier, session->identifier);
    assert_null(removed->parameters);
    assert_memory_equal(&removed->creator, &pk, sizeof(struct cpn_sign_key_public));

    cpn_session_free(removed);
}

static void add_session_with_params_succeeds()
{
    TestParams *params;
    struct cpn_session *removed;

    params = malloc(sizeof(TestParams));
    test_params__init(params);
    params->msg = strdup("test");

    assert_success(cpn_sessions_add(&session, &params->base, &pk));
    assert_success(cpn_sessions_remove(&removed, session->identifier));

    assert_ptr_equal(removed->parameters, params);

    cpn_session_free(removed);
}

static void *add_session(void *ptr)
{
    assert_success(cpn_sessions_add((const struct cpn_session **) ptr, NULL, &pk));

    return NULL;
}

static void adding_session_from_multiple_threads_succeeds()
{
    const struct cpn_session *sessions[100];
    struct cpn_session *removed;
    struct cpn_thread threads[ARRAY_SIZE(sessions)];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(threads); i++) {
        assert_success(cpn_spawn(&threads[i], add_session, &sessions[i]));
    }

    for (i = 0; i < ARRAY_SIZE(threads); i++) {
        assert_success(cpn_join(&threads[i], NULL));
    }

    for (i = 0; i < ARRAY_SIZE(threads); i++) {
        assert_success(cpn_sessions_remove(&removed, sessions[i]->identifier));
        assert_int_equal(removed->identifier, sessions[i]->identifier);
        cpn_session_free(removed);
    }
}

static void adding_session_with_different_invoker_succeeds()
{
    struct cpn_session *removed;

    assert_success(cpn_sessions_add(&session, NULL, &pk));
    assert_success(cpn_sessions_remove(&removed, session->identifier));

    assert_int_equal(removed->identifier, session->identifier);
    cpn_session_free(removed);
}

static void removing_session_twice_fails()
{
    struct cpn_session *removed;
    uint32_t identifier;

    assert_success(cpn_sessions_add(&session, NULL, &pk));
    identifier = session->identifier;

    assert_success(cpn_sessions_remove(&removed, session->identifier));
    cpn_session_free(removed);
    assert_failure(cpn_sessions_remove(&removed, identifier));
}

static void remove_session_fails_without_sessions()
{
    struct cpn_session *session;
    assert_failure(cpn_sessions_remove(&session, 0));
}

static void remove_session_fails_for_empty_session()
{
    struct cpn_sign_key_public key;
    struct cpn_session *session;

    memset(&key, 0, sizeof(key));

    assert_failure(cpn_sessions_remove(&session, 0));
}

static void finding_invalid_session_fails()
{
    assert_failure(cpn_sessions_find(&session, 0));
}

static void finding_session_with_invalid_id_fails()
{
    assert_success(cpn_sessions_add(&session, NULL, &pk));
    assert_failure(cpn_sessions_find(&session, session->identifier + 1));
}

static void finding_existing_session_succeeds()
{
    const struct cpn_session *found;

    assert_success(cpn_sessions_add(&session, NULL, &pk));
    assert_success(cpn_sessions_find(&found, session->identifier));

    assert_int_equal(found->identifier, session->identifier);
}

static void finding_session_without_out_param_succeeds()
{
    assert_success(cpn_sessions_add(&session, NULL, &pk));
    assert_success(cpn_sessions_find(NULL, session->identifier));
}

static void finding_intermediate_session_returns_correct_index()
{
    const struct cpn_session *sessions[3];

    assert_success(cpn_sessions_add(&sessions[0], NULL, &pk));
    assert_success(cpn_sessions_add(&sessions[1], NULL, &pk));
    assert_success(cpn_sessions_add(&sessions[2], NULL, &pk));

    assert_success(cpn_sessions_find(&session, sessions[2]->identifier));
    assert_int_equal(session, sessions[2]);
}

static void finding_session_with_multiple_sessions_succeeds()
{
    const struct cpn_session *sessions[8];
    uint32_t i;

    for (i = 0; i < ARRAY_SIZE(sessions); i++)
        assert_success(cpn_sessions_add(&sessions[i], NULL, &pk));

    for (i = 0; i < ARRAY_SIZE(sessions); i++) {
        assert_success(cpn_sessions_find(&session, sessions[i]->identifier));
        assert_int_equal(session->identifier, sessions[i]->identifier);
    }
}

static void free_session_succeeds_without_params()
{
    struct cpn_session *session = calloc(1, sizeof(struct cpn_session));
    cpn_session_free(session);
}

static void free_session_succeeds_with_params()
{
    TestParams *params;
    struct cpn_session *session = calloc(1, sizeof(struct cpn_session));

    params = malloc(sizeof(TestParams));
    test_params__init(params);
    params->msg = strdup("test");

    session->parameters = &params->base;

    cpn_session_free(session);
}

int session_test_run_suite(void)
{
    const struct CMUnitTest tests[] = {
        test(add_sessions_adds_session),
        test(add_session_with_params_succeeds),
        test(adding_session_from_multiple_threads_succeeds),
        test(adding_session_with_different_invoker_succeeds),

        test(removing_session_twice_fails),
        test(remove_session_fails_without_sessions),
        test(remove_session_fails_for_empty_session),

        test(finding_invalid_session_fails),
        test(finding_session_with_invalid_id_fails),
        test(finding_existing_session_succeeds),
        test(finding_session_without_out_param_succeeds),
        test(finding_intermediate_session_returns_correct_index),
        test(finding_session_with_multiple_sessions_succeeds),

        test(free_session_succeeds_without_params),
        test(free_session_succeeds_with_params),
    };

    return execute_test_suite("proto", tests, setup, teardown);
}
