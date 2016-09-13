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

#include "capone/client.h"
#include "capone/channel.h"
#include "capone/common.h"
#include "capone/socket.h"
#include "capone/server.h"
#include "capone/service.h"
#include "capone/session.h"

#include "test.h"
#include "test-service.h"

struct await_query_args {
    struct cpn_channel *channel;
    struct cpn_service *s;
};

struct await_request_args {
    struct cpn_channel *channel;
    struct cpn_service *s;
    struct cpn_sign_key_public *r;
    struct cpn_sign_key_public *whitelist;
    size_t nwhitelist;
};

struct handle_session_args {
    struct cpn_channel *channel;
    struct cpn_sign_key_public *remote_key;
    struct cpn_service *service;
    struct cpn_cfg *cfg;
};

struct handle_termination_args {
    struct cpn_channel *channel;
    struct cpn_sign_key_public *terminator;
};

static struct cpn_cfg config;
static struct cpn_service service;
static struct cpn_channel local, remote;
static struct cpn_sign_key_pair local_keys, remote_keys;
static const struct cpn_service_plugin *test_service;

static int setup()
{
    memset(&local, 0, sizeof(local));
    memset(&remote, 0, sizeof(remote));

    stub_sockets(&local, &remote, CPN_CHANNEL_TYPE_TCP);
    local.crypto = remote.crypto = CPN_CHANNEL_CRYPTO_NONE;

    return 0;
}

static int teardown()
{
    cpn_channel_close(&local);
    cpn_channel_close(&remote);
    cpn_sessions_clear();
    return 0;
}

static int await_type(struct cpn_channel *c, ConnectionInitiationMessage__Type expected)
{
    ConnectionInitiationMessage *msg = NULL;
    int err = -1;

    if (cpn_channel_receive_protobuf(c,
                &connection_initiation_message__descriptor, (ProtobufCMessage **) &msg) < 0)
        goto out_err;

    if (msg->type != expected)
        goto out_err;

    err = 0;

out_err:
    if (msg)
        connection_initiation_message__free_unpacked(msg, NULL);

    return err;
}

static void *initiate_connection(void *payload)
{
    struct cpn_channel c;

    UNUSED(payload);

    UNUSED(cpn_client_connect(&c, "127.0.0.1", "31248",
                &local_keys, &remote_keys.pk));

    UNUSED(cpn_channel_close(&c));

    return NULL;
}

static void *await_query(void *payload)
{
    struct await_query_args *args = (struct await_query_args *) payload;

    await_type(args->channel, CONNECTION_INITIATION_MESSAGE__TYPE__QUERY);

    UNUSED(cpn_server_handle_query(args->channel, args->s));

    return NULL;
}

static void *await_request(void *payload)
{
    struct await_request_args *args = (struct await_request_args *) payload;

    await_type(args->channel, CONNECTION_INITIATION_MESSAGE__TYPE__REQUEST);

    UNUSED(cpn_server_handle_request(args->channel, args->r, service.plugin));

    return NULL;
}

static void *handle_session(void *payload)
{
    struct handle_session_args *args = (struct handle_session_args *) payload;

    await_type(args->channel, CONNECTION_INITIATION_MESSAGE__TYPE__CONNECT);

    UNUSED(cpn_server_handle_session(args->channel,
                args->remote_key, args->service, args->cfg));

    return NULL;
}

static void *handle_termination(void *payload)
{
    struct handle_termination_args *args = (struct handle_termination_args *) payload;

    await_type(args->channel, CONNECTION_INITIATION_MESSAGE__TYPE__TERMINATE);
    UNUSED(cpn_server_handle_termination(args->channel, args->terminator));

    return NULL;
}

static void connection_initiation_succeeds()
{
    struct cpn_thread t;
    struct cpn_socket s;
    struct cpn_channel c;
    struct cpn_sign_key_public key;

    assert_success(cpn_socket_init(&s, "127.0.0.1", "31248", CPN_CHANNEL_TYPE_TCP));
    assert_success(cpn_socket_listen(&s));

    assert_success(cpn_spawn(&t, initiate_connection, NULL));
    assert_success(cpn_socket_accept(&s, &c));
    assert_success(cpn_server_await_encryption(&c, &remote_keys, &key));

    assert_success(cpn_channel_close(&c));
    assert_success(cpn_join(&t, NULL));

    assert_success(cpn_socket_close(&s));
}

static void query_succeeds()
{
    struct cpn_thread t;
    struct await_query_args args = {
        &remote, &service
    };
    struct cpn_query_results results;

    cpn_spawn(&t, await_query, &args);
    assert_success(cpn_client_query_service(&results, &local));
    cpn_join(&t, NULL);

    assert_string_equal(results.name, "Foo");
    assert_string_equal(results.type, "test");
    assert_string_equal(results.category, "Test");
    assert_string_equal(results.location, "Dunno");
    assert_string_equal(results.port, "1234");
    assert_string_equal(results.version, "0.0.1");

    cpn_query_results_free(&results);
}

static void whitelisted_query_succeeds()
{
    struct await_query_args args = {
        &remote, &service
    };
    struct cpn_thread t;
    struct cpn_query_results results;

    cpn_spawn(&t, await_query, &args);
    assert_success(cpn_client_query_service(&results, &local));
    cpn_join(&t, NULL);

    cpn_query_results_free(&results);
}

static void request_constructs_session()
{
    ProtobufCMessage *parsed;
    const char *params[] = { "text" };
    struct await_request_args args = {
        &remote, &service, &local_keys.pk, NULL, 0
    };
    struct cpn_cap *cap = NULL;
    struct cpn_session *added;
    struct cpn_thread t;
    uint32_t sessionid;

    cpn_spawn(&t, await_request, &args);
    assert_success(test_service->parse_fn(&parsed, ARRAY_SIZE(params), params));
    assert_success(cpn_client_request_session(&sessionid, &cap, &local, parsed));
    cpn_join(&t, NULL);

    assert_success(cpn_sessions_remove(&added, sessionid));
    assert_int_equal(sessionid, added->identifier);

    cpn_session_free(added);
    cpn_cap_free(cap);
}

static void whitlisted_request_constructs_session()
{
    ProtobufCMessage *parsed;
    const char *params[] = { "testdata" };
    struct await_request_args args = {
        &remote, &service, &local_keys.pk, &local_keys.pk, 1
    };
    struct cpn_session *added;
    struct cpn_thread t;
    struct cpn_cap *cap = NULL;
    uint32_t sessionid;

    cpn_spawn(&t, await_request, &args);
    assert_success(test_service->parse_fn(&parsed, ARRAY_SIZE(params), params));
    assert_success(cpn_client_request_session(&sessionid, &cap, &local, parsed));
    cpn_join(&t, NULL);

    assert_success(cpn_sessions_remove(&added, sessionid));
    assert_int_equal(sessionid, added->identifier);

    cpn_session_free(added);
    cpn_cap_free(cap);
    protobuf_c_message_free_unpacked(parsed, NULL);
}

static void service_connects()
{
    ProtobufCMessage *params_proto;
    const char *params[] = { "parameter-data" };
    struct handle_session_args args = {
        &remote, &local_keys.pk, &service, &config
    };
    struct cpn_cap *cap;
    struct cpn_thread t;
    const struct cpn_session *session;
    struct cpn_session *received_session = NULL;
    uint8_t *received;
    uint32_t sessionid;

    cpn_spawn(&t, handle_session, &args);

    assert_success(service.plugin->parse_fn(&params_proto, ARRAY_SIZE(params), params));
    assert_success(cpn_sessions_add(&session, params_proto, &remote_keys.pk));
    assert_success(cpn_cap_create_ref(&cap, session->cap, CPN_CAP_RIGHT_EXEC, &local_keys.pk));
    sessionid = session->identifier;

    assert_success(cpn_client_start_session(&received_session, &local, session->identifier, cap, service.plugin));
    assert_success(service.plugin->client_fn(&local, 0, NULL, &config) < 0);

    cpn_cap_free(cap);
    cpn_join(&t, NULL);

    assert_non_null(received_session);
    assert_int_equal(received_session->identifier, sessionid);

    received = cpn_test_service_get_data();
    assert_string_equal(params[0], received);
}

static void connect_refuses_without_session()
{
    struct handle_session_args args = {
        &remote, &local_keys.pk, &service, &config
    };
    struct cpn_thread t;
    struct cpn_cap cap;
    struct cpn_session *session = NULL;

    cap.chain_depth = 0;

    cpn_spawn(&t, handle_session, &args);

    assert_failure(cpn_client_start_session(&session, &local, 1, &cap, service.plugin));

    cpn_join(&t, NULL);

    assert_null(session);
    cpn_session_free(session);
}

static void termination_kills_session()
{
    struct handle_termination_args args = {
        &remote, &local_keys.pk
    };
    struct cpn_thread t;
    struct cpn_cap *cap;
    const struct cpn_session *session;
    uint32_t sessionid;

    assert_success(cpn_sessions_add(&session, 0, &remote_keys.pk));
    sessionid = session->identifier;

    assert_success(cpn_cap_create_ref(&cap, session->cap, CPN_CAP_RIGHT_TERM, &local_keys.pk));

    cpn_spawn(&t, handle_termination, &args);
    assert_success(cpn_client_terminate_session(&local, sessionid, cap));

    cpn_cap_free(cap);
    cpn_join(&t, NULL);

    assert_failure(cpn_sessions_find(NULL, sessionid));
}

static void terminating_nonexistent_does_nothing()
{
    struct handle_termination_args args = {
        &remote, &local_keys.pk
    };
    struct cpn_thread t;
    struct cpn_cap cap;
    cap.chain_depth = 0;

    cpn_spawn(&t, handle_termination, &args);
    assert_success(cpn_client_terminate_session(&local, 12345, &cap));
    cpn_join(&t, NULL);
}

int proto_test_run_suite(void)
{
    static const char *service_cfg =
        "[service]\n"
        "name=Foo\n"
        "type=test\n"
        "location=Dunno\n"
        "port=1234\n";

    const struct CMUnitTest tests[] = {
        test(connection_initiation_succeeds),

        test(query_succeeds),
        test(whitelisted_query_succeeds),

        test(request_constructs_session),
        test(whitlisted_request_constructs_session),

        test(service_connects),
        test(connect_refuses_without_session),

        test(termination_kills_session),
        test(terminating_nonexistent_does_nothing)
    };

    assert_success(cpn_test_init_service(&test_service));
    assert_success(cpn_service_plugin_register(test_service));

    assert_success(cpn_cfg_parse_string(&config, service_cfg, strlen(service_cfg)));
    assert_success(cpn_service_from_config(&service, "Foo", &config));

    assert_success(cpn_sign_key_pair_generate(&local_keys));
    assert_success(cpn_sign_key_pair_generate(&remote_keys));

    return execute_test_suite("proto", tests, setup, teardown);
}
