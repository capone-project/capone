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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

#include "parameter.h"

size_t sd_parameters_filter(struct sd_parameter **out, const char *key,
        const struct sd_parameter *params, size_t nparams)
{
    struct sd_parameter *result;
    size_t i, n = 0;

    for (i = 0; i < nparams; i++)
        if (!strcmp(params[i].key, key))
            n++;

    if (n == 0) {
        *out = NULL;
        return 0;
    }

    result = calloc(n, sizeof(struct sd_parameter));

    for (n = 0, i = 0; i < nparams; i++)
        if (!strcmp(params[i].key, key)) {
            result[n].key = params[i].key;
            result[n].value = params[i].value;
            n++;
        }

    *out = result;

    return n;
}

int sd_parameters_get_value(const char **out, const char *value, const struct sd_parameter *parameters, size_t n)
{
    const char **values;
    int nvalues;

    *out = NULL;

    nvalues = sd_parameters_get_values(&values, value, parameters, n);
    if (nvalues < 0) {
        sd_log(LOG_LEVEL_WARNING, "Could not retrieve parameter value '%s'", value);
        goto out_err;
    } else if (nvalues == 0) {
        sd_log(LOG_LEVEL_WARNING, "Requested parameter value '%s' not present", value);
        goto out_err;
    } else if (nvalues > 1) {
        sd_log(LOG_LEVEL_WARNING, "Requested parameter value '%s' has more than one value", value);
        goto out_err;
    }

    *out = values[0];
    free(values);

    return 0;

out_err:
    free(values);
    return -1;
}

int sd_parameters_get_values(const char ***out, const char *value, const struct sd_parameter *parameters, size_t n)
{
    const struct sd_parameter *param;
    const char **values = NULL;
    int nvalues = 0;
    size_t i;

    *out = NULL;

    for (i = 0; i < n; i++) {
        param = &parameters[i];

        if (!strcmp(param->key, value) && param->value != NULL) {
            values = realloc(values, sizeof(char *) * (nvalues + 1));
            values[nvalues++] = param->value;
        }
    }

    *out = values;

    return nvalues;
}

void sd_parameters_free(struct sd_parameter *params, size_t nparams)
{
    size_t i;

    if (!params || nparams == 0)
        return;

    for (i = 0; i < nparams; i++) {
        free((void *) params[i].key);
        free((void *) params[i].value);
    }

    free(params);
}

size_t sd_parameters_to_proto(Parameter ***out, const struct sd_parameter *params, size_t nparams)
{
    size_t i;

    if (!nparams) {
        *out = NULL;
        return 0;
    }

    *out = calloc(nparams, sizeof(Parameter *));

    for (i = 0; i < nparams; i++) {
        Parameter *parameter = malloc(sizeof(Parameter));
        parameter__init(parameter);

        parameter->key = params[i].key ? strdup(params[i].key) : NULL;
        parameter->value = params[i].value ? strdup(params[i].value) : NULL;

        (*out)[i] = parameter;
    }

    return nparams;
}

void sd_parameters_proto_free(Parameter **params, size_t nparams)
{
    size_t i;

    if (!params || nparams == 0)
        return;

    for (i = 0; i < nparams; i++) {
        free(params[i]->key);
        free(params[i]->value);
    }

    free(params);
}
