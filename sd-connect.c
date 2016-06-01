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

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sodium.h>
#include <inttypes.h>

#include "lib/common.h"
#include "lib/channel.h"
#include "lib/proto.h"
#include "lib/service.h"

static struct sd_sign_key_pair local_keys;
static struct sd_sign_key_public remote_key;

static void usage(const char *prog)
{
    printf("USAGE: %s (request|connect)\n"
            "\tquery <CONFIG> <KEY> <HOST> <PORT>\n"
            "\trequest <CONFIG> <KEY> <HOST> <PORT> [<PARAMETER>...]\n"
            "\tconnect <CONFIG> <KEY> <HOST> <PORT> <SERVICE> <SESSIONID>\n"
            "\tterminate <CONFIG> <KEY> <HOST> <PORT> <SESSIONID> <INVOKER>\n",
            prog);
    exit(-1);
}

static int cmd_query(int argc, char *argv[])
{
    struct sd_query_results results;
    struct sd_channel channel;
    char *config, *key, *host, *port;
    size_t i;

    if (argc != 6)
        usage(argv[0]);

    config = argv[2];
    key = argv[3];
    host = argv[4];
    port = argv[5];

    if (sd_sign_key_pair_from_config_file(&local_keys, config) < 0) {
        puts("Could not parse sign keys");
        return -1;
    }

    if (sd_sign_key_public_from_hex(&remote_key, key) < 0) {
        puts("Could not parse remote public key");
        return -1;
    }

    if (sd_proto_initiate_connection(&channel, host, port,
                &local_keys, &remote_key, SD_CONNECTION_TYPE_QUERY) < 0) {
        puts("Could not establish connection");
        return -1;
    }

    if (sd_proto_send_query(&results, &channel) < 0) {
        puts("Could not query service");
        return -1;
    }

    printf("%s\n"
            "\tname:     %s\n"
            "\tcategory: %s\n"
            "\ttype:     %s\n"
            "\tversion:  %s\n"
            "\tlocation: %s\n"
            "\tport:     %s\n",
            key,
            results.name,
            results.category,
            results.type,
            results.version,
            results.location,
            results.port);

    for (i = 0; i < results.nparams; i++) {
        struct sd_parameter *param = &results.params[i];

        printf("\tparam:    %s=%s\n", param->key, param->value);
    }

    sd_query_results_free(&results);
    sd_channel_close(&channel);

    return 0;
}

static int cmd_request(int argc, char *argv[])
{
    const char *config, *key, *host, *port;
    struct sd_parameter *params = NULL;
    struct sd_channel channel;
    uint32_t sessionid;
    ssize_t nparams;

    memset(&channel, 0, sizeof(channel));

    if (argc < 6) {
        usage(argv[0]);
    }

    config = argv[2];
    key = argv[3];
    host = argv[4];
    port = argv[5];

    if ((nparams = sd_parameters_parse(&params, argc - 6, argv + 6)) < 0) {
        puts("Could not parse parameters");
        goto out_err;
    }

    if (sd_sign_key_pair_from_config_file(&local_keys, config) < 0) {
        puts("Could not parse config");
        goto out_err;
    }

    if (sd_sign_key_public_from_hex(&remote_key, key) < 0) {
        puts("Could not parse remote public key");
        goto out_err;
    }

    if (sd_proto_initiate_connection(&channel, host, port,
                &local_keys, &remote_key, SD_CONNECTION_TYPE_REQUEST) < 0) {
        puts("Could not establish connection");
        goto out_err;
    }

    if (sd_proto_send_request(&sessionid, &channel,
                &local_keys.pk, params, nparams) < 0)
    {
        puts("Unable to request session");
        goto out_err;
    }

    printf("sessionid:  %"PRIu32"\n", sessionid);

    sd_channel_close(&channel);

    return 0;

out_err:
    sd_channel_close(&channel);
    sd_parameters_free(params, nparams);
    return -1;
}

static int cmd_connect(int argc, char *argv[])
{
    const char *config, *key, *host, *port, *service_type, *session;
    struct sd_sign_key_public remote_key;
    struct sd_service service;
    struct sd_channel channel;
    uint32_t sessionid;

    if (argc < 8)
        usage(argv[0]);

    config = argv[2];
    key = argv[3];
    host = argv[4];
    port = argv[5];
    service_type = argv[6];
    session = argv[7];

    if (sd_sign_key_pair_from_config_file(&local_keys, config) < 0) {
        puts("Could not parse config");
        return -1;
    }

    if (sd_sign_key_public_from_hex(&remote_key, key) < 0) {
        puts("Could not parse remote public key");
        return -1;
    }

    if (sd_service_from_type(&service, service_type) < 0) {
        printf("Invalid service %s\n", service_type);
        return -1;
    }

    if (parse_uint32t(&sessionid, session) < 0) {
        printf("Invalid session ID %s\n", session);
        return -1;
    }

    if (sd_proto_initiate_connection(&channel, host, port,
                &local_keys, &remote_key, SD_CONNECTION_TYPE_CONNECT) < 0) {
        puts("Could not start connection");
        return -1;
    }

    if (sd_proto_initiate_session(&channel, sessionid) < 0) {
        puts("Could not connect to session");
        return -1;
    }

    if (service.invoke(&channel, argc - 8, argv + 8) < 0) {
        puts("Could not invoke service");
        return -1;
    }

    sd_channel_close(&channel);

    return 0;
}

static int cmd_terminate(int argc, char *argv[])
{
    struct sd_sign_key_public remote_key, invoker_key;
    struct sd_sign_key_pair local_keys;
    struct sd_channel channel;
    const char *config, *key, *host, *port, *session, *invoker;
    uint32_t sessionid;

    if (argc != 8)
        usage(argv[0]);

    config = argv[2];
    key = argv[3];
    host = argv[4];
    port = argv[5];
    session = argv[6];
    invoker = argv[7];

    if (sd_sign_key_pair_from_config_file(&local_keys, config) < 0) {
        puts("Could not parse config");
        return -1;
    }

    if (sd_sign_key_public_from_hex(&remote_key, key) < 0) {
        puts("Could not parse remote public key");
        return -1;
    }

    if (sd_sign_key_public_from_hex(&invoker_key, invoker) < 0) {
        puts("Could not parse invoker key");
        return -1;
    }

    if (parse_uint32t(&sessionid, session) < 0) {
        printf("Invalid session ID %s\n", session);
        return -1;
    }

    if (sd_proto_initiate_connection(&channel, host, port,
                &local_keys, &remote_key, SD_CONNECTION_TYPE_TERMINATE) < 0) {
        puts("Could not start connection");
        return -1;
    }

    if (sd_proto_initiate_termination(&channel, sessionid, &invoker_key) < 0) {
        puts("Could not initiate termination");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        usage(argv[0]);

    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts("sd-connect " VERSION "\n"
             "Copyright (C) 2016 Patrick Steinhardt\n"
             "License GPLv3: GNU GPL version 3 <http://gnu.org/licenses/gpl.html>.\n"
             "This is free software; you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by the law.");
        return 0;
    }

    if (sodium_init() < 0) {
        puts("Could not init libsodium");
        return -1;
    }

    if (!strcmp(argv[1], "query"))
        return cmd_query(argc, argv);
    if (!strcmp(argv[1], "request"))
        return cmd_request(argc, argv);
    else if (!strcmp(argv[1], "connect"))
        return cmd_connect(argc, argv);
    else if (!strcmp(argv[1], "terminate"))
        return cmd_terminate(argc, argv);

    usage(argv[0]);

    return 0;
}
