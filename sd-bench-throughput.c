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

#include <stdio.h>
#include <string.h>

#include "lib/bench.h"
#include "lib/channel.h"
#include "lib/common.h"
#include "lib/keys.h"
#include "lib/server.h"

#define PORT "43281"

struct client_args {
    uint32_t datalen;
    uint32_t blocklen;
    uint32_t repeats;
};

static char encrypt;
static struct sd_symmetric_key key;

static void *client(void *payload)
{
    struct client_args *args = (struct client_args *) payload;
    struct sd_channel channel;
    uint8_t *data = malloc(args->datalen);
    uint64_t start, end;
    uint32_t i;

    if (sd_bench_set_affinity(2) < 0) {
        puts("Unable to set sched affinity");
        goto out;
    }

    if (sd_channel_init_from_host(&channel, "127.0.0.1", PORT, SD_CHANNEL_TYPE_TCP) < 0) {
        puts("Unable to init connection");
        goto out;
    }
    if (sd_channel_connect(&channel) < 0) {
        puts("Unable to connect to server");
        goto out;
    }

    if (encrypt) {
        sd_channel_enable_encryption(&channel, &key, SD_CHANNEL_NONCE_CLIENT);
    }

    sd_channel_set_blocklen(&channel, args->blocklen);

    start = sd_bench_nsecs();
    for (i = 0; i < args->repeats; i++) {
        if (sd_channel_write_data(&channel, data, args->datalen) < 0) {
            puts("Unable to write data");
            goto out;
        }
    }
    end = sd_bench_nsecs();

    printf("send (ns):\t%"PRIu64"\n", (end - start) / args->repeats);

out:
    free(data);

    return NULL;
}

static void usage(const char *executable)
{
    printf("USAGE: %s <--encrypted|--plain> <DATALEN> <BLOCKLEN>\n", executable);
}

int main(int argc, char *argv[])
{
    struct client_args args;
    struct sd_thread t;
    struct sd_server server;
    struct sd_channel channel;
    uint8_t *data;
    uint64_t start, end;
    uint32_t i;

    if (argc != 4) {
        usage(argv[0]);
        return -1;
    }

    if (!strcmp(argv[1] , "--plain")) {
        encrypt = 0;
    } else if (!strcmp(argv[1], "--encrypted")) {
        encrypt = 1;
        sd_symmetric_key_generate(&key);
    } else {
        usage(argv[0]);
        return -1;
    }

    if (parse_uint32t(&args.datalen, argv[2]) < 0) {
        puts("Invalid data length");
        usage(argv[0]);
        return -1;
    }

    if (parse_uint32t(&args.blocklen, argv[3]) < 0) {
        puts("Invalid block length");
        usage(argv[0]);
        return -1;
    }

    data = malloc(args.datalen);

    /* Always average over 1GB of data sent */
    args.repeats = (1024 * 1024 * 1024) / args.datalen;

    if (sd_bench_set_affinity(3) < 0) {
        puts("Unable to set sched affinity");
        return -1;
    }

    if (sd_server_init(&server, NULL, PORT, SD_CHANNEL_TYPE_TCP) < 0) {
        puts("Unable to init server");
        return -1;
    }

    if (sd_server_listen(&server) < 0) {
        puts("Unable to listen");
        return -1;
    }

    if (sd_spawn(&t, client, &args) < 0) {
        puts("Unable to spawn client");
        return -1;
    }

    if (sd_server_accept(&server, &channel) < 0) {
        puts("Unable to accept connection");
        return -1;
    }

    if (encrypt) {
        sd_channel_enable_encryption(&channel, &key, SD_CHANNEL_NONCE_SERVER);
    }

    sd_channel_set_blocklen(&channel, args.blocklen);

    start = sd_bench_nsecs();
    for (i = 0; i < args.repeats; i++) {
        if (sd_channel_receive_data(&channel, data, args.datalen) < 0) {
            puts("Unable to receive data");
            return -1;
        }
    }
    end = sd_bench_nsecs();

    if (sd_join(&t, NULL) < 0) {
        puts("Unable to await client thread");
        return -1;
    }

    printf("recv (ns):\t%"PRIu64"\n", (end - start) / args.repeats);

    return 0;
}
