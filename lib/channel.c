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
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sodium/crypto_auth.h>
#include <sodium/utils.h>
#include <sodium/randombytes.h>

#include "lib/log.h"
#include "lib/common.h"

#include "channel.h"

#define PACKAGE_LENGTH 512

int getsock(struct sockaddr_storage *addr, size_t *addrlen,
        const char *host, const char *port,
        enum sd_channel_type type)
{
    struct addrinfo hints, *servinfo, *hint;
    int ret, fd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG;
    switch (type) {
        case SD_CHANNEL_TYPE_TCP:
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            break;
        case SD_CHANNEL_TYPE_UDP:
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            break;
        default:
            sd_log(LOG_LEVEL_ERROR, "Unknown channel type");
            return -1;
    }

    ret = getaddrinfo(host, port, &hints, &servinfo);
    if (ret != 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not get addrinfo for address %s:%s",
                host, port);
        return -1;
    }

    for (hint = servinfo; hint != NULL; hint = hint->ai_next) {
        fd = socket(hint->ai_family, hint->ai_socktype, hint->ai_protocol);
        if (fd < 0)
            continue;

        break;
    }

    if (hint == NULL) {
        sd_log(LOG_LEVEL_ERROR, "Unable to resolve address");
        freeaddrinfo(servinfo);
        return -1;
    }

    if ((unsigned int) hint->ai_addrlen > sizeof(struct sockaddr_storage)) {
        sd_log(LOG_LEVEL_ERROR, "Hint's addrlen is greater than sockaddr_storage length");
        freeaddrinfo(servinfo);
        close(fd);
        return -1;
    }

    memcpy(addr, hint->ai_addr, hint->ai_addrlen);
    *addrlen = hint->ai_addrlen;
    freeaddrinfo(servinfo);

    return fd;
}

int sd_channel_init_from_host(struct sd_channel *c, const char *host,
        const char *port, enum sd_channel_type type)
{
    int fd;
    struct sockaddr_storage addr;
    size_t addrlen;

    fd = getsock(&addr, &addrlen, host, port, type);
    if (fd < 0) {
        return -1;
    }

    return sd_channel_init_from_fd(c, fd, &addr, addrlen, type);
}

int sd_channel_init_from_fd(struct sd_channel *c,
        int fd, const struct sockaddr_storage *addr, size_t addrlen,
        enum sd_channel_type type)
{
    memset(c, 0, sizeof(struct sd_channel));

    c->fd = fd;
    c->type = type;
    c->crypto = SD_CHANNEL_CRYPTO_NONE;
    memcpy(&c->addr, addr, sizeof(c->addr));
    c->addrlen = addrlen;

    return 0;
}

int sd_channel_disable_encryption(struct sd_channel *c)
{
    memset(&c->key, 0, sizeof(c->key));

    c->crypto = SD_CHANNEL_CRYPTO_NONE;

    return 0;
}

int sd_channel_enable_encryption(struct sd_channel *c,
        const struct sd_symmetric_key *key, enum sd_channel_nonce nonce)
{
    memcpy(&c->key, key, sizeof(c->key));

    memset(c->local_nonce, 0, sizeof(c->local_nonce));
    memset(c->remote_nonce, 0, sizeof(c->remote_nonce));

    switch (nonce) {
        case SD_CHANNEL_NONCE_CLIENT:
            sodium_increment(c->remote_nonce, sizeof(c->remote_nonce));
            break;
        case SD_CHANNEL_NONCE_SERVER:
            sodium_increment(c->local_nonce, sizeof(c->local_nonce));
            break;
    }

    c->crypto = SD_CHANNEL_CRYPTO_SYMMETRIC;

    return 0;
}

int sd_channel_close(struct sd_channel *c)
{
    if (c->fd < 0) {
        sd_log(LOG_LEVEL_WARNING, "Closing channel with invalid fd");
        return -1;
    }

    close(c->fd);
    c->fd = -1;

    return 0;
}

bool sd_channel_is_closed(struct sd_channel *c)
{
    struct timeval tv = { 0, 0 };
    fd_set fds;
    int n = 0;

    if (c->fd < 0)
        return false;

    FD_ZERO(&fds);
    FD_SET(c->fd, &fds);

    select(c->fd + 1, &fds, 0, 0, &tv);
    if (!FD_ISSET(c->fd, &fds))
        return false;

    ioctl(c->fd, FIONREAD, &n);

    return n == 0;
}

int sd_channel_connect(struct sd_channel *c)
{
    assert(c->fd >= 0);

    if (connect(c->fd, (struct sockaddr*) &c->addr, c->addrlen) < 0) {
        sd_log(LOG_LEVEL_ERROR, "Could not connect: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int write_data(struct sd_channel *c, uint8_t *data, uint32_t datalen)
{
    ssize_t ret;
    uint32_t written = 0;

    while (written != datalen) {
        switch (c->type) {
            case SD_CHANNEL_TYPE_TCP:
                ret = send(c->fd, data + written, datalen - written, 0);
                break;
            case SD_CHANNEL_TYPE_UDP:
                ret = sendto(c->fd, data + written, datalen - written, 0,
                        (struct sockaddr *) &c->addr, sizeof(c->addr));
                break;
            default:
                sd_log(LOG_LEVEL_ERROR, "Unknown channel type");
                return -1;
        }

        if (ret <= 0) {
            sd_log(LOG_LEVEL_ERROR, "Could not send data: %s",
                    strerror(errno));
            return -1;
        }

        written += ret;
    }

    return written;
}

int sd_channel_write_data(struct sd_channel *c, uint8_t *data, uint32_t datalen)
{
    uint8_t block[PACKAGE_LENGTH];
    size_t written = 0, offset;
    uint32_t networklen;

    networklen = htonl(datalen);
    memcpy(block, &networklen, sizeof(networklen));
    offset = 4;

    while (offset || written != datalen) {
        uint32_t len;
        if (c->crypto == SD_CHANNEL_CRYPTO_SYMMETRIC) {
            len = MIN(datalen - written, sizeof(block) - offset - crypto_secretbox_MACBYTES);
        } else {
            len = MIN(datalen - written, sizeof(block) - offset);
        }

        memset(block + offset, 0, sizeof(block) - offset);
        memcpy(block + offset, data + written, len);

        if (c->crypto == SD_CHANNEL_CRYPTO_SYMMETRIC) {
            if (crypto_secretbox_easy(block, block, sizeof(block) - crypto_secretbox_MACBYTES,
                        c->local_nonce, c->key.data) < 0) {
                sd_log(LOG_LEVEL_ERROR, "Unable to encrypt message");
                return -1;
            }
            sodium_increment(c->local_nonce, crypto_secretbox_NONCEBYTES);
            sodium_increment(c->local_nonce, crypto_secretbox_NONCEBYTES);
        }

        if (write_data(c, block, sizeof(block)) < 0) {
            sd_log(LOG_LEVEL_ERROR, "Unable to write encrypted data");
            return -1;
        }
        written += len;

        offset = 0;
    }

    return 0;
}

int sd_channel_write_protobuf(struct sd_channel *c, ProtobufCMessage *msg)
{
    size_t size;
    uint8_t buf[4096];

    if (!protobuf_c_message_check(msg)) {
        sd_log(LOG_LEVEL_ERROR, "Invalid protobuf message");
        return -1;
    }

    size = protobuf_c_message_get_packed_size(msg);
    if (size > sizeof(buf)) {
        sd_log(LOG_LEVEL_ERROR, "Protobuf message exceeds buffer length");
        return -1;
    }

    sd_log(LOG_LEVEL_TRACE, "Writing protobuf %s:%s of length %"PRIuMAX,
            msg->descriptor->package_name, msg->descriptor->name, size);

    protobuf_c_message_pack(msg, buf);

    return sd_channel_write_data(c, buf, size);
}

static int receive_data(struct sd_channel *c, uint8_t *out, size_t len)
{
    ssize_t ret;
    size_t received = 0;

    while (received != len) {
        ret = recv(c->fd, out + received, len - received, 0);
        if (ret <= 0) {
            return -1;
        }

        received += ret;
    }

    return received;
}

ssize_t sd_channel_receive_data(struct sd_channel *c, uint8_t *out, size_t maxlen)
{
    uint8_t block[PACKAGE_LENGTH];
    uint32_t pkglen, received = 0, offset = sizeof(uint32_t);

    while (offset || received < pkglen) {
        uint32_t networklen, blocklen;

        if (receive_data(c, block, sizeof(block)) < 0) {
            sd_log(LOG_LEVEL_ERROR, "Unable to receive data");
            return -1;
        }

        if (c->crypto == SD_CHANNEL_CRYPTO_SYMMETRIC) {
            if (crypto_secretbox_open_easy(block, block, sizeof(block),
                        c->remote_nonce, c->key.data) < 0)
            {
                sd_log(LOG_LEVEL_ERROR, "Unable to decrypt received block");
                return -1;
            }
            sodium_increment(c->remote_nonce, crypto_secretbox_NONCEBYTES);
            sodium_increment(c->remote_nonce, crypto_secretbox_NONCEBYTES);
        }

        if (offset) {
            memcpy(&networklen, block, sizeof(networklen));
            pkglen = ntohl(networklen);
            if (pkglen > maxlen) {
                sd_log(LOG_LEVEL_ERROR, "Received package length exceeds maxlen");
                return -1;
            }
        }

        if (c->crypto == SD_CHANNEL_CRYPTO_SYMMETRIC) {
            blocklen = MIN(pkglen - received, sizeof(block) - offset - crypto_secretbox_MACBYTES);
        } else {
            blocklen = MIN(pkglen - received, sizeof(block) - offset);
        }

        memcpy(out + received, block + offset, blocklen);

        received += blocklen;
        offset = 0;
    }

    return received;
}

int sd_channel_receive_protobuf(struct sd_channel *c, const ProtobufCMessageDescriptor *descr, ProtobufCMessage **msg)
{
    ProtobufCMessage *result;
    uint8_t buf[4096];
    ssize_t len;

    len = sd_channel_receive_data(c, buf, sizeof(buf));
    if (len < 0) {
        return -1;
    }

    sd_log(LOG_LEVEL_TRACE, "Receiving protobuf %s:%s of length %"PRIuMAX,
            descr->package_name, descr->name, len);

    result = protobuf_c_message_unpack(descr, NULL, len, buf);
    if (result == NULL) {
        sd_log(LOG_LEVEL_ERROR, "Protobuf message could not be unpacked");
        return -1;
    }

    *msg = result;

    return 0;
}

int sd_channel_relay(struct sd_channel *channel, int nfds, ...)
{
    fd_set fds;
    uint8_t buf[2048];
    int written, received, maxfd, infd, fd, i, ret;
    va_list ap;

    if (nfds <= 0) {
        sd_log(LOG_LEVEL_ERROR, "Relay called with nfds == 0");
        return -1;
    }

    maxfd = channel->fd;
    va_start(ap, nfds);
    for (i = 0; i < nfds; i++) {
        fd = va_arg(ap, int);
        maxfd = MAX(maxfd, fd);

        if (i == 0)
            infd = fd;
    }
    va_end(ap);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(channel->fd, &fds);

        va_start(ap, nfds);
        for (i = 0; i < nfds; i++) {
            fd = va_arg(ap, int);
            FD_SET(fd, &fds);
        }
        va_end(ap);

        if (select(maxfd + 1, &fds, NULL, NULL, NULL) <= 0) {
            sd_log(LOG_LEVEL_ERROR, "Error selecting fds");
            return -1;
        }

        if (FD_ISSET(channel->fd, &fds)) {
            received = sd_channel_receive_data(channel, buf, sizeof(buf));
            if (received == 0) {
                sd_log(LOG_LEVEL_VERBOSE, "Channel closed, stopping relay");
                return 0;
            } else if (received < 0) {
                sd_log(LOG_LEVEL_ERROR, "Error relaying data from channel: %s", strerror(errno));
                return -1;
            }

            written = 0;
            while (written != received) {
                ret = write(infd, buf + written, received - written);
                if (ret <= 0) {
                    sd_log(LOG_LEVEL_ERROR, "Error relaying data to fd: %s", strerror(errno));
                    return -1;
                }
                written += ret;
            }
        }

        va_start(ap, nfds);
        for (i = 0; i < nfds; i++) {
            fd = va_arg(ap, int);

            if (FD_ISSET(fd, &fds)) {
                received = read(fd, buf, sizeof(buf));
                if (received == 0) {
                    sd_log(LOG_LEVEL_VERBOSE, "File descriptor closed, stopping relay");
                    return 0;
                } else if (received < 0) {
                    sd_log(LOG_LEVEL_ERROR, "Error relaying data from fd");
                    return -1;
                }

                if (sd_channel_write_data(channel, buf, received) < 0) {
                    sd_log(LOG_LEVEL_ERROR, "Error relaying data to channel");
                    return -1;
                }
            }
        }
        va_end(ap);
    }

    return 0;
}
