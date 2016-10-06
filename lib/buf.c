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

#include "capone/buf.h"

#include <sodium/utils.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int ensure_allocated(struct cpn_buf *buf, size_t size)
{
    if (size == 0)
        return 0;

    if (buf->allocated < size) {
        buf->data = realloc(buf->data, size);
    }

    buf->allocated = size;

    return 0;
}

int cpn_buf_set(struct cpn_buf *buf, const char *string)
{
    size_t len = strlen(string);

    if (ensure_allocated(buf, len + 1) < 0)
        return -1;

    assert(buf->data);

    memcpy(buf->data, string, len);
    buf->length = len;
    buf->data[buf->length] = '\0';

    return 0;
}

int cpn_buf_append(struct cpn_buf *buf, const char *string)
{
    size_t len = strlen(string);

    if (ensure_allocated(buf, buf->length + len + 1) < 0)
        return -1;

    assert(buf->data);

    memcpy(buf->data + buf->length, string, len);
    buf->length = buf->length + len;
    buf->data[buf->length] = '\0';

    return 0;
}

int cpn_buf_append_data(struct cpn_buf *buf, const unsigned char *data, size_t len)
{
    if (ensure_allocated(buf, buf->length + len) < 0)
        return -1;

    assert(buf->data);

    memcpy(buf->data + buf->length, data, len);
    buf->length = buf->length + len;

    return 0;
}

int cpn_buf_append_hex(struct cpn_buf *buf, const unsigned char *data, size_t len)
{
    int hexlen = (len * 2);
    int newlen = buf->length + hexlen + 1;

    if (ensure_allocated(buf, newlen) < 0)
        return -1;

    assert(buf->data);

    if (sodium_bin2hex(buf->data + buf->length, hexlen + 1, data, len) == NULL)
        return -1;
    buf->length = buf->length + hexlen;

    return 0;
}

int cpn_buf_printf(struct cpn_buf *buf, const char *format, ...)
{
    char buffer[4096];
    va_list ap;
    int err;

    va_start(ap, format);
    err = vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);

    if (err < 0)
        return -1;

    return cpn_buf_append(buf, buffer);
}

void cpn_buf_reset(struct cpn_buf *buf)
{
    buf->length = 0;
}

void cpn_buf_clear(struct cpn_buf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->allocated = 0;
    buf->length = 0;
}
