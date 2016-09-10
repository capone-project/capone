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

#include "capone/channel.h"
#include "capone/log.h"
#include "capone/session.h"
#include "capone/proto.h"

#include "capone/proto/connect.pb-c.h"

int cpn_proto_receive_connection_type(enum cpn_connection_type *out,
        struct cpn_channel *channel)
{
    ConnectionInitiationMessage *initiation;
    int ret = 0;;

    if (cpn_channel_receive_protobuf(channel,
                (ProtobufCMessageDescriptor *) &connection_initiation_message__descriptor,
                (ProtobufCMessage **) &initiation) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Failed receiving connection type");
        return -1;
    }

    switch (initiation->type) {
        case CONNECTION_INITIATION_MESSAGE__TYPE__QUERY:
            *out = CPN_CONNECTION_TYPE_QUERY;
            break;
        case CONNECTION_INITIATION_MESSAGE__TYPE__REQUEST:
            *out = CPN_CONNECTION_TYPE_REQUEST;
            break;
        case CONNECTION_INITIATION_MESSAGE__TYPE__CONNECT:
            *out = CPN_CONNECTION_TYPE_CONNECT;
            break;
        case CONNECTION_INITIATION_MESSAGE__TYPE__TERMINATE:
            *out = CPN_CONNECTION_TYPE_TERMINATE;
            break;
        case _CONNECTION_INITIATION_MESSAGE__TYPE_IS_INT_SIZE:
        default:
            ret = -1;
            break;
    }

    connection_initiation_message__free_unpacked(initiation, NULL);

    return ret;
}

int cpn_proto_handle_session(struct cpn_channel *channel,
        const struct cpn_sign_key_public *remote_key,
        const struct cpn_service *service,
        const struct cpn_cfg *cfg)
{
    SessionInitiationMessage *initiation = NULL;
    SessionResult msg = SESSION_RESULT__INIT;
    struct cpn_session *session = NULL;
    struct cpn_cap *cap = NULL;
    int err;

    if ((err = cpn_channel_receive_protobuf(channel,
                &session_initiation_message__descriptor,
                (ProtobufCMessage **) &initiation)) < 0)
    {
        cpn_log(LOG_LEVEL_ERROR, "Could not receive connection initiation");
        goto out;
    }

    if (cpn_cap_from_protobuf(&cap, initiation->capability) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not read capability");
        err = -1;
        goto out_notify;
    }

    if (cpn_sessions_find((const struct cpn_session **) &session, initiation->identifier) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not find session for client");
        err = -1;
        goto out_notify;
    }

    if (cpn_caps_verify(cap, session->cap, remote_key, CPN_CAP_RIGHT_EXEC) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not authorize session initiation");
        err = -1;
        goto out_notify;
    }

    if ((err = cpn_sessions_remove(&session, initiation->identifier)) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not find session for client");
        goto out_notify;
    }

out_notify:
    msg.result = err;
    if (cpn_channel_write_protobuf(channel, &msg.base) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not send session ack");
        goto out;
    }

    if (err)
        goto out;

    if ((err = service->plugin->server_fn(channel, remote_key, session, cfg)) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Service could not handle connection");
        goto out;
    }

out:
    if (initiation) {
        session_initiation_message__free_unpacked(initiation, NULL);
        cpn_session_free(session);
    }

    cpn_cap_free(cap);

    return 0;
}

int cpn_proto_answer_query(struct cpn_channel *channel,
        const struct cpn_service *service)
{
    ServiceDescription results = SERVICE_DESCRIPTION__INIT;

    results.name = service->name;
    results.location = service->location;
    results.port = service->port;
    results.category = (char *) service->plugin->category;
    results.type = (char *) service->plugin->type;
    results.version = (char *) service->plugin->version;

    if (cpn_channel_write_protobuf(channel, (ProtobufCMessage *) &results) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not send query results");
        return -1;
    }

    return 0;
}

static int create_cap(CapabilityMessage **out, const struct cpn_cap *root, uint32_t rights, const struct cpn_sign_key_public *key)
{
    CapabilityMessage *msg = NULL;
    struct cpn_cap *cap = NULL;
    int err = -1;

    if (cpn_cap_create_ref(&cap, root, rights, key) < 0)
        goto out;

    if (cpn_cap_to_protobuf(&msg, cap) < 0)
        goto out;

    *out = msg;
    err = 0;

out:
    if (err)
        free(msg);
    cpn_cap_free(cap);

    return err;
}

int cpn_proto_answer_request(struct cpn_channel *channel,
        const struct cpn_sign_key_public *remote_key,
        const struct cpn_service_plugin *service)
{
    SessionRequestMessage *request = NULL;
    ProtobufCMessage *parameters = NULL;
    SessionMessage session_message = SESSION_MESSAGE__INIT;
    const struct cpn_session *session;
    int err = -1;

    if (cpn_channel_receive_protobuf(channel,
            &session_request_message__descriptor,
            (ProtobufCMessage **) &request) < 0)
    {
        cpn_log(LOG_LEVEL_ERROR, "Unable to receive request");
        goto out;
    }

    if (service->params_desc) {
        if ((parameters = protobuf_c_message_unpack(service->params_desc, NULL,
                request->parameters.len, request->parameters.data)) == NULL)
            goto out;
    }

    if (cpn_sessions_add(&session, parameters, remote_key) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Unable to add session");
        goto out;
    }

    session_message.identifier = session->identifier;

    if (create_cap(&session_message.cap, session->cap,
                CPN_CAP_RIGHT_EXEC | CPN_CAP_RIGHT_TERM, remote_key) < 0)
    {
        cpn_log(LOG_LEVEL_ERROR, "Unable to add invoker capability");
        goto out;
    }

    if (cpn_channel_write_protobuf(channel, &session_message.base) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Unable to send connection session");
        cpn_sessions_remove(NULL, session->identifier);
        goto out;
    }

    err = 0;

out:
    if (session_message.cap)
        capability_message__free_unpacked(session_message.cap, NULL);
    if (request)
        session_request_message__free_unpacked(request, NULL);

    return err;
}

int cpn_proto_handle_termination(struct cpn_channel *channel,
        const struct cpn_sign_key_public *remote_key)
{
    SessionTerminationMessage *msg = NULL;
    const struct cpn_session *session;
    struct cpn_cap *cap = NULL;
    int err = -1;

    if (cpn_channel_receive_protobuf(channel,
            &session_termination_message__descriptor,
            (ProtobufCMessage **) &msg) < 0)
    {
        cpn_log(LOG_LEVEL_ERROR, "Unable to receive termination protobuf");
        goto out;
    }

    /* If session could not be found we have nothing to do */
    if (cpn_sessions_find(&session, msg->identifier) < 0) {
        goto out;
    }

    if (cpn_cap_from_protobuf(&cap, msg->capability) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Received invalid capability");
        goto out;
    }

    if (cpn_caps_verify(cap, session->cap, remote_key, CPN_CAP_RIGHT_TERM) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Received unauthorized request");
        goto out;
    }

    if (cpn_sessions_remove(NULL, msg->identifier) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Unable to terminate session");
        goto out;
    }

    err = 0;

out:
    if (msg)
        session_termination_message__free_unpacked(msg, NULL);
    cpn_cap_free(cap);

    return err;
}
