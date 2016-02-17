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

#include "xpra.h"

#include "lib/common.h"
#include "lib/service.h"

static const char *version(void)
{
    return "0.0.1";
}

int sd_xpra_init_service(struct sd_service *service)
{
    service->version = version;
    service->status = NULL;
    service->handle = NULL;
    service->parameters = NULL;

    return 0;
}
