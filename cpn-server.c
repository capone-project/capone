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

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sodium.h>

#include "lib/acl.h"
#include "lib/common.h"
#include "lib/log.h"
#include "lib/proto.h"
#include "lib/server.h"
#include "lib/service.h"

struct handle_connection_args {
    const struct sd_cfg *cfg;
    struct sd_channel channel;
};

static struct sd_acl request_acl = SD_ACL_INIT;
static struct sd_acl query_acl = SD_ACL_INIT;

static struct sd_sign_key_pair local_keys;
static struct sd_service service;

static int read_acl(struct sd_acl *acl, const char *file)
{
    struct sd_sign_key_public pk;
    FILE *stream = NULL;
    char *line = NULL;
    size_t length;
    ssize_t read;

    sd_acl_clear(acl);

    stream = fopen(file, "r");
    if (stream == NULL)
        return -1;

    while ((read = getline(&line, &length, stream)) != -1) {
        if (line[read - 1] == '\n')
            line[read - 1] = '\0';

        if (sd_sign_key_public_from_hex(&pk, line) < 0) {
            sd_log(LOG_LEVEL_ERROR, "Invalid key '%s'", line);
            goto out_err;
        }

        if (sd_acl_add_right(acl, &pk, SD_ACL_RIGHT_EXEC) < 0) {
            sd_log(LOG_LEVEL_ERROR, "Could not add right to ACL");
            goto out_err;
        }
    }
    free(line);
    line = NULL;

    if (!feof(stream))
        goto out_err;

    fclose(stream);

    return 0;

out_err:
    fclose(stream);
    free(line);

    return -1;
}

static void
sigchild_handler(int sig)
{
    int status;
    UNUSED(sig);
    while (waitpid(-1, &status, WNOHANG) > 0);
}

static void
exit_handler(int sig)
{
    kill(0, sig);
    exit(0);
}

static int setup_signals(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigchild_handler;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = exit_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGKILL, &sa, NULL);

    return 0;
}

static void *handle_connection(void *payload)
{
    struct handle_connection_args *args = (struct handle_connection_args *) payload;
    struct sd_sign_key_public remote_key;
    enum sd_connection_type type;

    if (sd_proto_await_encryption(&args->channel, &local_keys, &remote_key) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Unable to negotiate encryption");
        goto out;
    }

    if (sd_proto_receive_connection_type(&type, &args->channel) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not receive connection type");
        goto out;
    }

    switch (type) {
        case SD_CONNECTION_TYPE_QUERY:
            sd_log(LOG_LEVEL_DEBUG, "Received query");

            if (!sd_acl_is_allowed(&query_acl, &remote_key, SD_ACL_RIGHT_EXEC)) {
                sd_log(LOG_LEVEL_ERROR, "Received unauthorized query");
                goto out;
            }

            if (sd_proto_answer_query(&args->channel, &service) < 0) {
                sd_log(LOG_LEVEL_ERROR, "Received invalid query");
                goto out;
            }

            goto out;
        case SD_CONNECTION_TYPE_REQUEST:
            sd_log(LOG_LEVEL_DEBUG, "Received request");

            if (!sd_acl_is_allowed(&request_acl, &remote_key, SD_ACL_RIGHT_EXEC)) {
                sd_log(LOG_LEVEL_ERROR, "Received unauthorized query");
                goto out;
            }

            if (sd_proto_answer_request(&args->channel, &remote_key) < 0) {
                sd_log(LOG_LEVEL_ERROR, "Received invalid request");
                goto out;
            }

            goto out;
        case SD_CONNECTION_TYPE_CONNECT:
            sd_log(LOG_LEVEL_DEBUG, "Received connect");

            if (sd_proto_handle_session(&args->channel, &remote_key, &service, args->cfg) < 0)
            {
                sd_log(LOG_LEVEL_ERROR, "Received invalid connect");
            }

            goto out;
        case SD_CONNECTION_TYPE_TERMINATE:
            sd_log(LOG_LEVEL_DEBUG, "Received termination request");

            if (sd_proto_handle_termination(&args->channel, &remote_key) < 0) {
                sd_log(LOG_LEVEL_ERROR, "Received invalid termination request");
            }

            goto out;
        default:
            sd_log(LOG_LEVEL_ERROR, "Unknown connection envelope type %d", type);
            goto out;
    }

out:
    sd_channel_close(&args->channel);
    free(payload);
    return NULL;
}

static int setup(struct sd_cfg *cfg, int argc, char *argv[])
{
    const char *servicename;
    int err;

    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts("sd-server " VERSION "\n"
             "Copyright (C) 2016 Patrick Steinhardt\n"
             "License GPLv3: GNU GPL version 3 <http://gnu.org/licenses/gpl.html>.\n"
             "This is free software; you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by the law.");
        return 0;
    } else if (argc < 3 || argc > 5) {
        printf("USAGE: %s <CONFIG> <SERVICENAME> [<REQUEST_ACL> [<QUERY_ACL>]]\n", argv[0]);
        return -1;
    }

    servicename = argv[2];

    memset(cfg, 0, sizeof(*cfg));

    if (sd_cfg_parse(cfg, argv[1]) < 0) {
        puts("Could not parse config");
        err = -1;
        goto out;
    }

    if (argc == 5) {
        read_acl(&request_acl, argv[3]);
        read_acl(&query_acl, argv[4]);
    } else if (argc == 4) {
        read_acl(&request_acl, argv[3]);
        sd_acl_add_wildcard(&query_acl, SD_ACL_RIGHT_EXEC);
    } else {
        sd_acl_add_wildcard(&request_acl, SD_ACL_RIGHT_EXEC);
        sd_acl_add_wildcard(&query_acl, SD_ACL_RIGHT_EXEC);
    }

    if (setup_signals() < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not set up signal handlers");
        err = -1;
        goto out;
    }

    if (sodium_init() < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not init libsodium");
        err = -1;
        goto out;
    }

    if (sd_sessions_init() < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not initialize sessions");
        err = -1;
        goto out;
    }

    if (sd_service_from_config(&service, servicename, cfg) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not parse services");
        err = -1;
        goto out;
    }

    if (sd_sign_key_pair_from_config(&local_keys, cfg) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not parse config");
        err = -1;
        goto out;
    }

    return 0;

out:
    sd_cfg_free(cfg);

    return err;
}

int main(int argc, char *argv[])
{
    struct sd_server server;
    struct sd_cfg cfg;
    int err;

    if (setup(&cfg, argc, argv) < 0) {
        return -1;
    }

    if (sd_server_init(&server, NULL, service.port, SD_CHANNEL_TYPE_TCP) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not set up server");
        err = -1;
        goto out;
    }

    if (sd_server_listen(&server) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not start listening");
        err = -1;
        goto out;
    }

    while (1) {
        struct handle_connection_args *args = malloc(sizeof(*args));;

        if (sd_server_accept(&server, &args->channel) < 0) {
            sd_log(LOG_LEVEL_ERROR, "Could not accept connection");
            err = -1;
            goto out;
        }

        args->cfg = &cfg;

        sd_spawn(NULL, handle_connection, args);
    }

out:
    sd_server_close(&server);
    sd_cfg_free(&cfg);

    return err;
}