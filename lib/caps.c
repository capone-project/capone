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

#include <arpa/inet.h>

#include <sodium.h>
#include <string.h>
#include <pthread.h>

#include "capone/buf.h"
#include "capone/caps.h"
#include "capone/common.h"
#include "capone/list.h"
#include "capone/log.h"

static int hash(uint8_t *out,
        uint32_t rights,
        const uint8_t *secret,
        const struct cpn_sign_key_public *key)
{
    crypto_generichash_state state;
    uint8_t hash[CPN_CAP_SECRET_LEN];
    uint32_t nlrights = htonl(rights);

    crypto_generichash_init(&state, NULL, 0, sizeof(hash));

    crypto_generichash_update(&state, key->data, sizeof(key->data));
    crypto_generichash_update(&state, (unsigned char *) &nlrights, sizeof(nlrights));
    crypto_generichash_update(&state, (unsigned char *) secret, CPN_CAP_SECRET_LEN);

    crypto_generichash_final(&state, hash, sizeof(hash));

    memcpy(out, hash, sizeof(hash));

    return 0;
}

int cpn_cap_from_string(struct cpn_cap **out, const char *string)
{
    struct cpn_cap *cap;
    uint32_t i, rights, chain_depth = 0;
    int err = -1;
    const char *ptr;

    cap = malloc(sizeof(struct cpn_cap));
    cap->chain = NULL;

    for (ptr = string; *ptr != '\0'; ptr++) {
        if (*ptr == '|')
            chain_depth++;
    }

    ptr = strchr(string, chain_depth ? '|' : '\0');

    if ((ptr - string) != CPN_CAP_SECRET_LEN * 2) {
        cpn_log(LOG_LEVEL_ERROR, "Invalid secret");
        goto out;
    }

    if (parse_hex(cap->secret, sizeof(cap->secret), string, ptr - string) < 0) {
        cpn_log(LOG_LEVEL_ERROR, "Invalid hex secret");
        goto out;
    }

    if (*ptr == '\0') {
        cap->chain = NULL;
        cap->chain_depth = 0;
    } else {
        cap->chain = malloc(sizeof(*cap->chain) * chain_depth);
        cap->chain_depth = chain_depth;
        rights = CPN_CAP_RIGHT_EXEC | CPN_CAP_RIGHT_TERM;

        for (i = 0; i < chain_depth; i++) {
            string = ++ptr;
            ptr = strchr(string, ':');

            if (ptr == NULL) {
                cpn_log(LOG_LEVEL_ERROR, "Capability chain entry without rights");
                goto out;
            }

            if (parse_hex(cap->chain[i].identity.data, sizeof(struct cpn_sign_key_public),
                        string, ptr - string) < 0)
            {
                cpn_log(LOG_LEVEL_ERROR, "Capability chain entry invalid identity");
                goto out;
            }

            cap->chain[i].rights = 0;
            while (*++ptr != '\0' && *ptr != '|') {
                switch (*ptr) {
                    case 'x':
                        cap->chain[i].rights |= CPN_CAP_RIGHT_EXEC;
                        break;
                    case 't':
                        cap->chain[i].rights |= CPN_CAP_RIGHT_TERM;
                        break;
                    case '|':
                        continue;
                    default:
                        goto out;
                }
            }

            if (cap->chain[i].rights == 0 || (cap->chain[i].rights & ~rights))
                goto out;
            rights = cap->chain[i].rights;
        }
    }

    err = 0;
    *out = cap;

out:
    if (err) {
        free(cap->chain);
        free(cap);
    }
    return err;
}

int cpn_cap_to_string(char **out, const struct cpn_cap *cap)
{
    struct cpn_buf buf = CPN_BUF_INIT;
    struct cpn_sign_key_hex hex;
    char buffer[CPN_CAP_SECRET_LEN * 2 + 1];
    uint32_t i;

    if (sodium_bin2hex(buffer, sizeof(buffer), cap->secret, sizeof(cap->secret)) == NULL)
        goto out_err;
    cpn_buf_append(&buf, buffer);

    if (cap->chain_depth) {
        for (i = 0; i < cap->chain_depth; i++) {
            if (!cap->chain[i].rights)
                goto out_err;

            cpn_sign_key_hex_from_key(&hex, &cap->chain[i].identity);
            cpn_buf_printf(&buf, "|%s:", hex.data);

            if (cap->chain[i].rights & CPN_CAP_RIGHT_EXEC)
                cpn_buf_append(&buf, "x");
            if (cap->chain[i].rights & CPN_CAP_RIGHT_TERM)
                cpn_buf_append(&buf, "t");
        }
    }

    *out = buf.data;

    return 0;

out_err:
    cpn_buf_clear(&buf);
    return -1;
}

struct cpn_cap *cpn_cap_dup(const struct cpn_cap *cap)
{
    uint32_t i;
    struct cpn_cap *dup;

    dup = malloc(sizeof(struct cpn_cap));
    dup->chain_depth = cap->chain_depth;
    memcpy(dup->secret, cap->secret, sizeof(dup->secret));

    if (dup->chain_depth) {
        dup->chain = malloc(sizeof(*dup->chain) * cap->chain_depth);
        for (i = 0; i < cap->chain_depth; i++) {
            memcpy(&dup->chain[i], &cap->chain[i], sizeof(dup->chain[i]));
        }
    } else {
        dup->chain = NULL;
    }

    return dup;
}

int cpn_cap_from_protobuf(struct cpn_cap **out, const CapabilityMessage *msg)
{
    struct cpn_cap *cap = NULL;
    uint32_t i;

    if (!msg || msg->secret.len != CPN_CAP_SECRET_LEN)
        goto out_err;

    cap = malloc(sizeof(struct cpn_cap));
    memcpy(cap->secret, msg->secret.data, CPN_CAP_SECRET_LEN);
    cap->chain_depth = msg->n_chain;

    if (cap->chain_depth) {
        cap->chain = malloc(sizeof(*cap->chain) * cap->chain_depth);

        for (i = 0; i < msg->n_chain; i++) {
            cap->chain[i].rights = msg->chain[i]->rights;
            if (cpn_sign_key_public_from_proto(&cap->chain[i].identity, msg->chain[i]->identity) < 0)
                goto out_err;
        }
    } else {
        cap->chain = NULL;
    }

    *out = cap;

    return 0;

out_err:
    if (cap) {
        free(cap->chain);
        free(cap);
    }

    return -1;
}

int cpn_cap_to_protobuf(CapabilityMessage **out, const struct cpn_cap *cap)
{
    CapabilityMessage *msg;
    uint32_t i;

    msg = malloc(sizeof(CapabilityMessage));
    capability_message__init(msg);

    msg->secret.data = malloc(CPN_CAP_SECRET_LEN);
    msg->secret.len = CPN_CAP_SECRET_LEN;

    msg->n_chain = cap->chain_depth;
    if (msg->n_chain) {
        msg->chain = malloc(sizeof(*msg->chain) * cap->chain_depth);

        for (i = 0;  i < cap->chain_depth; i++) {
            CapabilityMessage__Chain *chain = malloc(sizeof(*chain));
            capability_message__chain__init(chain);
            chain->rights = cap->chain[i].rights;
            cpn_sign_key_public_to_proto(&chain->identity, &cap->chain[i].identity);
            msg->chain[i] = chain;
        }
    } else {
        msg->chain = NULL;
    }

    memcpy(msg->secret.data, cap->secret, CPN_CAP_SECRET_LEN);

    *out = msg;

    return 0;
}

int cpn_cap_create_root(struct cpn_cap **out)
{
    struct cpn_cap *cap;

    *out = NULL;

    cap = malloc(sizeof(struct cpn_cap));
    randombytes_buf(cap->secret, CPN_CAP_SECRET_LEN);
    cap->chain_depth = 0;
    cap->chain = NULL;

    *out = cap;

    return 0;
}

int cpn_cap_create_ref(struct cpn_cap **out, const struct cpn_cap *root,
        uint32_t rights, const struct cpn_sign_key_public *key)
{
    struct cpn_cap *cap;

    *out = NULL;

    if (root->chain_depth && rights & ~root->chain[root->chain_depth - 1].rights)
        return -1;

    cap = malloc(sizeof(struct cpn_cap));
    hash(cap->secret, rights, root->secret, key);
    cap->chain_depth = root->chain_depth + 1;
    cap->chain = malloc(sizeof(*cap->chain) * cap->chain_depth);
    memcpy(cap->chain, root->chain, sizeof(*root->chain) * root->chain_depth);
    memcpy(&cap->chain[root->chain_depth].identity, key, sizeof(struct cpn_sign_key_public));
    cap->chain[root->chain_depth].rights = rights;

    *out = cap;

    return 0;
}

void cpn_cap_free(struct cpn_cap *cap)
{
    if (!cap)
        return;

    free(cap->chain);
    free(cap);
}

int cpn_caps_verify(const struct cpn_cap *ref, const struct cpn_cap *root,
        const struct cpn_sign_key_public *key, uint32_t right)
{
    uint8_t secret[CPN_CAP_SECRET_LEN];
    uint32_t i, rights;

    if (ref->chain_depth == 0)
        return -1;
    if (memcmp(key, &ref->chain[ref->chain_depth - 1].identity, sizeof(struct cpn_sign_key_public)))
        return -1;
    if (!(ref->chain[ref->chain_depth - 1].rights & right))
        return -1;

    rights = CPN_CAP_RIGHT_EXEC | CPN_CAP_RIGHT_TERM;
    memcpy(secret, root->secret, sizeof(secret));

    for (i = 0; i < ref->chain_depth; i++) {
        if (ref->chain[i].rights & ~rights)
            return -1;
        if (hash(secret, ref->chain[i].rights, secret, &ref->chain[i].identity) < 0)
            return -1;
        rights = ref->chain[i].rights;
    }

    if (right & ~rights)
        return -1;
    if (memcmp(secret, ref->secret, CPN_CAP_SECRET_LEN))
        return -1;

    return 0;
}
