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

#ifndef SD_LIB_SERVICE_H
#define SD_LIB_SERVICE_H

#include "lib/cfg.h"
#include "lib/keys.h"

struct sd_channel;

struct sd_service_parameter {
    const char *name;

    size_t nvalues;
    const char **values;
};

struct sd_service_session {
    uint32_t sessionid;
    struct sd_symmetric_key session_key;
    struct sd_sign_key_public identity;

    struct sd_service_parameter *parameters;
    size_t nparameters;

    struct sd_service_session *next;
};

typedef int (*invoke_fn)(struct sd_channel *channel, int argc, char **argv);
typedef int (*handle_fn)(struct sd_channel *channel, const struct sd_service_session *session);
typedef int (*parameters_fn)(const struct sd_service_parameter **out);
typedef const char *(*version_fn)(void);

struct sd_service {
    char *name;
    char *type;
    char *subtype;
    char *port;
    char *location;

    version_fn version;
    parameters_fn parameters;
    handle_fn handle;
    invoke_fn invoke;
};

int sd_service_from_type(struct sd_service *out, const char *type);
int sd_service_from_config_file(struct sd_service *out, const char *name, const char *file);
int sd_service_from_config(struct sd_service *out, const char *name, const struct cfg *cfg);
int sd_service_from_section(struct sd_service *out, const struct cfg_section *section);
void sd_service_free(struct sd_service *service);

int sd_services_from_config_file(struct sd_service **out, const char *file);
int sd_services_from_config(struct sd_service **out, const struct cfg *cfg);

int sd_service_parameters_get_value(const char **out, const char *value, const struct sd_service_parameter *parameters, size_t n);
int sd_service_parameters_get_values(const char ***out, const char *value, const struct sd_service_parameter *parameters, size_t n);
void sd_service_parameters_free(struct sd_service_parameter *params, size_t nparams);

#endif
