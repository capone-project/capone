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

#include "capone/socket.h"

#include "test.h"

static struct cpn_socket remote;
static struct cpn_channel channel;
static enum cpn_channel_type type;

static int setup()
{
    type = CPN_CHANNEL_TYPE_TCP;
    return 0;
}

static int teardown()
{
    cpn_socket_close(&remote);
    cpn_channel_close(&channel);
    return 0;
}

static void set_local_address_to_localhost()
{
    assert_success(cpn_socket_init(&remote, "localhost", 8080, type));
    assert_true(remote.fd >= 0);
}

static void set_local_address_to_127001()
{
    assert_success(cpn_socket_init(&remote, "127.0.0.1", 8080, type));
    assert_true(remote.fd >= 0);
}

static void set_local_address_to_empty_address()
{
    assert_success(cpn_socket_init(&remote, NULL, 8080, type));
    assert_true(remote.fd >= 0);
}

static void set_local_address_to_invalid_address()
{
    assert_failure(cpn_socket_init(&remote, "999.999.999.999", 8080, type));
    assert_true(remote.fd < 0);
}

static void connect_to_localhost_succeeds()
{
    struct cpn_channel connected;
    uint8_t data[] = "test";

    assert_success(cpn_socket_init(&remote, "127.0.0.1", 8080, type));
    if (type == CPN_CHANNEL_TYPE_TCP)
        assert_success(cpn_socket_listen(&remote));

    assert_success(cpn_channel_init_from_host(&channel, "127.0.0.1", 8080, type));
    assert_success(cpn_channel_connect(&channel));

    assert_success(cpn_socket_accept(&remote, &connected));

    assert_success(cpn_channel_write_data(&connected, data, sizeof(data)));

    assert_success(cpn_channel_close(&connected));
}

static void getting_address_succeeds()
{
    char host[20];
    uint32_t port;

    assert_success(cpn_socket_init(&remote, "127.0.0.1", 12345, type));
    assert_success(cpn_socket_get_address(&remote, host, sizeof(host), &port));

    assert_string_equal(host, "127.0.0.1");
    assert_int_equal(port, 12345);
}

int socket_test_run_suite(void)
{
    const struct CMUnitTest tests[] = {
        test(set_local_address_to_localhost),
        test(set_local_address_to_127001),
        test(set_local_address_to_empty_address),
        test(set_local_address_to_invalid_address),
        test(connect_to_localhost_succeeds),
        test(getting_address_succeeds)
    };

    return execute_test_suite("socket", tests, NULL, NULL);
}
