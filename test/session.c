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
#include "capone/keys.h"
#include "capone/session.h"
#include "capone/service.h"

#include "test.h"

static struct cpn_session *session;
static uint32_t id;

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
    assert_success(cpn_sessions_add(&id, 0, NULL));
    assert_success(cpn_sessions_remove(&session, id));

    assert_int_equal(session->sessionid, id);
    assert_int_equal(session->argc, 0);
    assert_null(session->argv);

    cpn_session_free(session);
}

static void add_session_with_params_succeeds()
{
    const char *params[] = {
        "data", "block"
    };

    assert_success(cpn_sessions_add(&id, ARRAY_SIZE(params), params));
    assert_success(cpn_sessions_remove(&session, id));

    assert_int_equal(session->argc, 2);
    assert_string_equal(session->argv[0], params[0]);
    assert_string_equal(session->argv[1], params[1]);

    cpn_session_free(session);
}

static void *add_session(void *ptr)
{
    assert_success(cpn_sessions_add((uint32_t *) ptr, 0, NULL));

    return (void *)(long) id;
}

static void adding_session_from_multiple_threads_succeeds()
{
    struct cpn_thread threads[100];
    uint32_t ids[ARRAY_SIZE(threads)];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(threads); i++) {
        assert_success(cpn_spawn(&threads[i], add_session, &ids[i]));
    }

    for (i = 0; i < ARRAY_SIZE(threads); i++) {
        assert_success(cpn_join(&threads[i], NULL));
    }

    for (i = 0; i < ARRAY_SIZE(threads); i++) {
        assert_success(cpn_sessions_remove(&session, ids[i]));
        assert_int_equal(session->sessionid, ids[i]);
        cpn_session_free(session);
    }
}

static void adding_session_with_different_invoker_succeeds()
{
    assert_success(cpn_sessions_add(&id, 0, NULL));
    assert_success(cpn_sessions_remove(&session, id));

    assert_int_equal(session->sessionid, id);
    cpn_session_free(session);
}

static void removing_session_twice_fails()
{
    assert_success(cpn_sessions_add(&id, 0, NULL));

    assert_success(cpn_sessions_remove(&session, id));
    cpn_session_free(session);
    assert_failure(cpn_sessions_remove(&session, id));
}

static void remove_session_fails_without_sessions()
{
    assert_failure(cpn_sessions_remove(&session, 0));
    cpn_session_free(session);
}

static void remove_session_fails_for_empty_session()
{
    struct cpn_sign_key_public key;

    memset(&key, 0, sizeof(key));

    assert_failure(cpn_sessions_remove(&session, 0));
    cpn_session_free(session);
}

static void finding_invalid_session_fails()
{
    assert_failure(cpn_sessions_find(&session, 0));
}

static void finding_session_with_invalid_id_fails()
{
    assert_success(cpn_sessions_add(&id, 0, NULL));
    assert_failure(cpn_sessions_find(&session, id + 1));
}

static void finding_existing_session_succeeds()
{
    assert_success(cpn_sessions_add(&id, 0, NULL));
    assert_success(cpn_sessions_find(&session, id));

    assert_int_equal(session->sessionid, id);
}

static void finding_session_without_out_param_succeeds()
{
    assert_success(cpn_sessions_add(&id, 0, NULL));
    assert_success(cpn_sessions_find(NULL, id));
}

static void finding_intermediate_session_returns_correct_index()
{
    uint32_t id1, id2, id3;

    assert_success(cpn_sessions_add(&id1, 0, NULL));
    assert_success(cpn_sessions_add(&id2, 0, NULL));
    assert_success(cpn_sessions_add(&id3, 0, NULL));

    assert_success(cpn_sessions_find(&session, id2));
    assert_int_equal(session->sessionid, id2);
}

static void finding_session_with_multiple_sessions_succeeds()
{
    uint32_t ids[8];
    uint32_t i;

    assert_success(cpn_sessions_add(&ids[0], 0, NULL));
    assert_success(cpn_sessions_add(&ids[1], 0, NULL));
    assert_success(cpn_sessions_add(&ids[2], 0, NULL));
    assert_success(cpn_sessions_add(&ids[3], 0, NULL));
    assert_success(cpn_sessions_add(&ids[4], 0, NULL));
    assert_success(cpn_sessions_add(&ids[5], 0, NULL));
    assert_success(cpn_sessions_add(&ids[6], 0, NULL));
    assert_success(cpn_sessions_add(&ids[7], 0, NULL));

    for (i = 0; i < 8; i++) {
        assert_success(cpn_sessions_find(&session, ids[i]));
        assert_int_equal(session->sessionid, ids[i]);
    }
}

static void free_session_succeeds_without_params()
{
    session = calloc(1, sizeof(struct cpn_session));
    cpn_session_free(session);
}

static void free_session_succeeds_with_params()
{
    session = calloc(1, sizeof(struct cpn_session));
    session->argc = 2;
    session->argv = malloc(sizeof(char *) * 2);
    session->argv[0] = strdup("data");
    session->argv[1] = strdup("block");

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
