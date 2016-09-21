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

#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "capone/common.h"
#include "capone/log.h"
#include "capone/opts.h"
#include "capone/socket.h"
#include "capone/service.h"

#include "capone/services/xpra.h"

static int invoke(struct cpn_channel *channel,
        const struct cpn_session *session,
        const struct cpn_cfg *cfg)
{
    struct cpn_channel xpra_channel;
    int port;
    char buf[1];

    UNUSED(session);

    xpra_channel.fd = -1;

    if ((port = cpn_cfg_get_int_value(cfg, "xpra", "port")) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "No port for xpra specified in 'xpra.port'");
        goto out;
    }

    if (cpn_channel_init_from_host(&xpra_channel, "127.0.0.1",
                port, CPN_CHANNEL_TYPE_TCP) < 0)
    {
        cpn_log(LOG_LEVEL_ERROR, "Could not initialize local xpra channel");
        goto out;
    }

    /* As xpra uses a timeout waiting for initial data when
     * connecting to the service we have to make sure that the
     * remote side has already started the connection from the
     * xpra client. As such, we wait for the first byte to appear
     * and when it does, we do the actual connection to the xpra
     * socket.
     */
    if (recv(channel->fd, buf, sizeof(buf), MSG_PEEK) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not await xpra connection");
        goto out;
    }

    if (cpn_channel_connect(&xpra_channel) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not connect to local xpra socket");
        goto out;
    }

    if (cpn_channel_relay(channel, 1, xpra_channel.fd) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not relay data from xpra connection");
        goto out;
    }

out:
    cpn_channel_close(&xpra_channel);

    return 0;
}

static int handle(struct cpn_channel *channel,
        const struct cpn_sign_key_public *invoker,
        const struct cpn_session *session,
        const struct cpn_cfg *cfg)
{
    struct cpn_socket socket;
    struct cpn_channel xpra_channel;
    char *args[] = {
        "xpra",
        "attach",
        NULL,
        "--no-notifications",
        NULL
    };
    uint32_t port;
    int len, pid;

    UNUSED(cfg);
    UNUSED(invoker);
    UNUSED(session);

    if (cpn_socket_init(&socket, "127.0.0.1", 0, CPN_CHANNEL_TYPE_TCP) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not initialize xpra relay socket");
        return -1;
    }

    if (cpn_socket_listen(&socket) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not listen on xpra relay socket");
        return -1;
    }

    if (cpn_socket_get_address(&socket, NULL, 0, &port) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Could not retrieve address of xpra relay socket");
        return -1;
    }

    len = snprintf(NULL, 0, "tcp:localhost:%"PRIu32":100", port) + 1;
    args[2] = malloc(len);
    len = snprintf(args[2], len, "tcp:localhost:%"PRIu32":100", port);

    pid = fork();
    if (pid == 0) {
        if (execvp("xpra", args) != 0) {
            cpn_log(LOG_LEVEL_ERROR, "Unable to execute xpra client");
            _exit(-1);
        }

        _exit(0);
    } else if (pid > 0) {
        if (cpn_socket_accept(&socket, &xpra_channel) < 0) {
            cpn_log(LOG_LEVEL_ERROR, "Could not accept xpra relay socket connection");
            return -1;
        }

        if (cpn_channel_relay(channel, 1, xpra_channel.fd) < 0) {
            cpn_log(LOG_LEVEL_ERROR, "Could not relay xpra socket");
            return -1;
        }
    } else {
        return -1;
    }

    kill(pid, SIGKILL);
    cpn_log(LOG_LEVEL_VERBOSE, "Terminated xpra");
    free(args[2]);

    return 0;
}

int cpn_xpra_init_service(const struct cpn_service_plugin **out)
{
    static struct cpn_service_plugin plugin = {
        "Display",
        "xpra",
        "0.0.1",
        handle,
        invoke,
        NULL,
        NULL
    };

    *out = &plugin;

    return 0;
}
