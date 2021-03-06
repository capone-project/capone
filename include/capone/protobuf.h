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

#ifndef CPN_PROTOBUF_H
#define CPN_PROTOBUF_H

#include <protobuf-c/protobuf-c.h>

#include "capone/buf.h"

/** Function to format a protobuf message
 *
 * \param[in,out] buf The string being built up for the text format protobuf.
 * \param[in] level Indent level - increments in 2's.
 * \param[in] msg The \c ProtobufCMessage being serialised.
 */
int cpn_protobuf_to_string(struct cpn_buf *buf, int level, ProtobufCMessage *msg);

#endif
