/*
 * module-murphy-ivi -- PulseAudio module for providing audio routing support
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <pulse/utf8.h>
#include <pulsecore/pulsecore-config.h>
#include <pulsecore/llist.h>
#include <pulsecore/idxset.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/core-util.h>

#include "resource.h"
#include "node.h"
#include "stream-state.h"


struct pa_resource {
    struct {
        pa_hashmap *id;
        pa_hashmap *name;
        unsigned    nres[2];
    } rsets;
    struct {
        pa_hashmap *id;
        pa_hashmap *name;
        pa_hashmap *node;
    } streams;
};



struct pa_resource_rset_entry {
    size_t                     nstream;
    pa_resource_stream_entry **streams;
    char                      *name;
    char                      *id;
    pa_resource_rset_data     *rset;
    bool                       type[2];
    uint32_t                   updid;
    bool                       dead;
};

struct pa_resource_stream_entry {
    size_t                     nrset;
    pa_resource_rset_entry   **rsets;
    char                      *name;
    char                      *id;
    mir_node                  *node;
};

static void rset_data_copy(pa_resource_rset_data *,pa_resource_rset_data *,int);

static pa_resource_rset_entry *rset_entry_new(pa_resource *, const char *,
                                              const char *);
static void rset_entry_free(pa_resource *, pa_resource_rset_entry *);
static int rset_entry_add_stream_link(pa_resource_rset_entry *,
                                      pa_resource_stream_entry *);
static int rset_entry_remove_stream_link( pa_resource_rset_entry *,
                                          pa_resource_stream_entry *);
static void rset_entry_is_dead(pa_resource *, pa_resource_rset_entry *);


static pa_resource_stream_entry *stream_entry_new(pa_resource *, const char *,
                                                  const char *, mir_node *);
static void stream_entry_free(pa_resource *, pa_resource_stream_entry *);
static int stream_entry_add_rset_link(pa_resource_stream_entry *,
                                      pa_resource_rset_entry *);
static int stream_entry_remove_rset_link(pa_resource_stream_entry *,
                                         pa_resource_rset_entry *);

static bool is_number(const char *);

static void enforce_policy(struct userdata *, mir_node *,
                           pa_resource_rset_data *, int);



pa_resource *pa_resource_init(struct userdata *u)
{
    pa_resource *resource;

    resource = pa_xnew0(pa_resource, 1);

    resource->rsets.id = pa_hashmap_new(pa_idxset_string_hash_func,
                                        pa_idxset_string_compare_func);
    resource->rsets.name = pa_hashmap_new(pa_idxset_string_hash_func,
                                         pa_idxset_string_compare_func);

    resource->streams.id = pa_hashmap_new(pa_idxset_string_hash_func,
                                          pa_idxset_string_compare_func);
    resource->streams.name = pa_hashmap_new(pa_idxset_string_hash_func,
                                            pa_idxset_string_compare_func);
    resource->streams.node = pa_hashmap_new(pa_idxset_trivial_hash_func,
                                            pa_idxset_trivial_compare_func);
}

void pa_resource_done(struct userdata *u)
{
    pa_resource *resource;
    pa_resource_rset_entry *re;
    pa_resource_stream_entry *se;
    void *state;

    if (u && (resource = u->resource)) {
        PA_HASHMAP_FOREACH(re, resource->rsets.id, state)
            rset_entry_free(resource, re);


        PA_HASHMAP_FOREACH(re, resource->rsets.name, state)
            rset_entry_free(resource, re);


        PA_HASHMAP_FOREACH(se, resource->streams.id, state)
            stream_entry_free(resource, se);

        PA_HASHMAP_FOREACH(se, resource->streams.name, state)
            stream_entry_free(resource, se);

        pa_hashmap_free(resource->rsets.id);
        pa_hashmap_free(resource->rsets.name);
        pa_hashmap_free(resource->streams.id);
        pa_hashmap_free(resource->streams.name);
        pa_hashmap_free(resource->streams.node);
    }
}

unsigned pa_resource_get_number_of_resources(struct userdata *u, int type)
{
    pa_resource *resource;

    pa_assert(u);
    pa_assert_se((resource = u->resource));
    pa_assert(type == PA_RESOURCE_INPUT || type == PA_RESOURCE_OUTPUT);

    return resource->rsets.nres[type];
}

void pa_resource_purge(struct userdata *u, uint32_t updid, int type)
{
    pa_resource *resource;
    pa_resource_rset_entry *re;
    void *state;

    pa_assert(u);
    pa_assert_se((resource = u->resource));
    pa_assert(type == PA_RESOURCE_INPUT || type == PA_RESOURCE_OUTPUT);

    PA_HASHMAP_FOREACH(re, resource->rsets.id, state) {
        if (re->type[type] && re->updid != updid)
            rset_entry_is_dead(resource, re);
    }
}


int pa_resource_enforce_policies(struct userdata *u, int type)
{
    pa_resource *resource;
    mir_direction direction;
    pa_resource_stream_entry *se;
    pa_resource_rset_entry *re;
    pa_resource_rset_data rset;
    mir_node *node;
    void *state;
    bool *grant;
    char **policy;
    size_t i;

    pa_assert(u);
    pa_assert_se((resource = u->resource));
    pa_assert(type == PA_RESOURCE_INPUT || type == PA_RESOURCE_OUTPUT);

    direction = (type == PA_RESOURCE_INPUT) ? mir_input : mir_output;

    PA_HASHMAP_FOREACH(se, resource->streams.node, state) {
        pa_assert_se((node = se->node));

        if (direction == node->direction) {
            pa_assert_se((re = se->rsets[0]));
            pa_assert(re->rset);

            if (se->nrset == 1)
                enforce_policy(u, node, re->rset, type);
            else {
                grant  = &rset.grant[type];
                policy = &rset.policy[type];

                memcpy(&rset, re->rset, sizeof(rset));
                *grant = false;

                for (i = 0;  i < se->nrset;  i++) {
                    re = se->rsets[i];

                    if (!pa_streq(re->rset->policy[type], *policy))
                        *policy = "strict";

                    *grant |= re->rset->grant[type];
                }

                enforce_policy(u, node, &rset, type);
            }
        }
    }

    return 0;
}


pa_resource_rset_data *pa_resource_rset_data_new(void)
{
    pa_resource_rset_data *rset;

    rset = pa_xnew0(pa_resource_rset_data, 1);

    rset->dead = false;
    rset->id   = pa_xstrdup("<unknown>");
    rset->name = pa_xstrdup("<unknown>");
    rset->pid  = pa_xstrdup("<unknown>");

    rset->policy[PA_RESOURCE_INPUT]  = pa_xstrdup("<unknown>");
    rset->policy[PA_RESOURCE_OUTPUT] = pa_xstrdup("<unknown>");

    return rset;
}


void pa_resource_rset_data_free(pa_resource_rset_data *rset)
{
    if (rset) {
        pa_xfree(rset->id);
        pa_xfree(rset->policy[PA_RESOURCE_INPUT]);
        pa_xfree(rset->policy[PA_RESOURCE_OUTPUT]);
        pa_xfree(rset->name);
        pa_xfree(rset->pid);

        pa_xfree(rset);
    }
}

static void rset_data_copy(pa_resource_rset_data *dst,
                           pa_resource_rset_data *src,
                           int type)
{
    pa_assert(dst);
    pa_assert(type == PA_RESOURCE_INPUT || type == PA_RESOURCE_OUTPUT);

    if (!src)
        return;

    if (dst->id && !pa_streq(dst->id, "<unknown>")) {
        if (!src->id || (src->id && !pa_streq(src->id, dst->id))) {
            pa_log_error("refuse to update rset: mismatching ids (%s vs %s)",
                         dst->id, src->id ? src->id : "<null>");
            return;
        }
    }

    if (dst->name && !pa_streq(dst->name, "<unknown>")) {
        if (!src->name || (src->name && !pa_streq(src->name, dst->name))) {
            pa_log_error("refuse to update rset: mismatching names (%s vs %s)",
                         dst->name, src->name ? src->name:"<null>");
            return;
        }
    }

    if (dst->pid && !pa_streq(dst->pid, "<unknown>")) {
        if (!src->pid || (src->pid && !pa_streq(src->pid, dst->pid))) {
            pa_log_error("refuse to update rset: mismatching pids (%s vs %s)",
                         dst->pid, src->pid ? src->pid : "<null>");
            return;
        }
    }

    pa_xfree(dst->id);
    pa_xfree(dst->policy);
    pa_xfree(dst->name);
    pa_xfree(dst->pid);

    dst->autorel = src->autorel;
    dst->state   = src->state;
    dst->id      = src->id     ?  pa_xstrdup(src->id)     : NULL;
    dst->name    = src->name   ?  pa_xstrdup(src->name)   : NULL;
    dst->pid     = src->pid    ?  pa_xstrdup(src->pid)    : NULL;

    dst->policy[type]  = src->policy[type] ?  pa_xstrdup(src->policy[type]) : NULL;
    dst->grant[type]   = src->grant[type];
}


int pa_resource_rset_update(struct userdata *u,
                            const char *name,
                            const char *id,
                            int type,
                            pa_resource_rset_data *rset,
                            uint32_t updid)
{
    pa_resource *resource;
    pa_resource_rset_entry *re, *de;
    pa_resource_stream_entry *se;
    bool has_name, has_id;

    pa_assert(u);
    pa_assert_se((resource = u->resource));
    pa_assert(type == PA_RESOURCE_INPUT || type == PA_RESOURCE_OUTPUT);

    re = NULL;
    has_name = name && name[0] && !pa_streq(name, "<unknown>");
    has_id = id && id[0] && !pa_streq(id, "<unknown>");

    if (!has_id)
        return -1;

    if (!(re = pa_hashmap_get(resource->rsets.id, id))) {
        if ((has_name && (re = pa_hashmap_get(resource->rsets.name, name)))) {
            /* we have an incomplete rset created by a stream, ie.
               the stream was created first */

            pa_xfree(re->id);
            re->id = pa_xstrdup(id);

            if (pa_hashmap_put(resource->rsets.id, re->id, re) != 0) {
                pa_log_error("failed to add rset (id='%s' name='%s') "
                             "to id hashmap",
                             se->id ? se->id : "<unknown>",
                             se->name ? se->name : "<unknown>");
                return -1;
            }
        }
        else {
            /* we need to create a new rset entry */
            if ((has_name && (se = pa_hashmap_get(resource->streams.name, name))) ||
                (has_id   && (se = pa_hashmap_get(resource->streams.id, id))))
            {
                /* found a matching stream entry, ie.
                   that stream is controlled by multiple rsets */
                pa_assert(se->nrset > 0);

                if (!(re = rset_entry_new(resource, NULL, id))) {
                    pa_log("failed to create rset (id='%s' name='%s'): "
                           "invalid rset id or duplicate rset",
                           id ? id : "<unknown>", name ? name : "<unknown>");
                    return -1;
                }

                if (has_name) {
                    pa_log("removing rset (name='%s') from name hash", name);
                    if ((de = pa_hashmap_remove(resource->rsets.name, name))) {
                        pa_xfree(de->name);
                        de->name = NULL;
                    }
                }
            }
            else {
                /* could not find matching stream entry, ie.
                   the rset was created first*/

                if (!(re = rset_entry_new(resource, name, id))) {
                    pa_log("failed to create rset (id='%s' name='%s'): "
                           "invalid rset name/id or duplicate rset",
                           id ? id : "<unknown>", name ? name : "<unknown>");
                    return -1;
                }


                if (!(se = stream_entry_new(resource, name, id, NULL))) {
                    pa_log("failed to link rset (id='%s' name='%s') to stream: "
                           "invalid stream id/name or duplicate stream",
                           id ? id : "<null>", name ? name : "<null>");
                    rset_entry_free(resource, re);
                    return -1;
                }
            }

            pa_assert(se);

            rset_entry_add_stream_link(re, se);
            stream_entry_add_rset_link(se, re);
        }
    }

    pa_assert(re);

    if (re->dead)
        return -1;

    if (!re->type[type]) {
        re->type[type] = true;
        resource->rsets.nres[type]++;
    }

    rset_data_copy(re->rset, rset, type);
    re->updid = updid;

    return 0;
}


int pa_resource_rset_remove(struct userdata *u,
                            const char *name,
                            const char *id)
{
    pa_resource *resource;
    pa_resource_rset_entry *re;

    pa_assert(u);
    pa_assert_se((resource = u->resource));

    if ((id   && (re = pa_hashmap_get(resource->rsets.id, id))) ||
        (name && (re = pa_hashmap_get(resource->rsets.name, name))))
    {
        rset_entry_is_dead(resource, re);
        return 0;
    }

    return -1;
}

static pa_resource_rset_entry *rset_entry_new(pa_resource *resource,
                                              const char *name,
                                              const char *id)
{
    pa_resource_rset_entry *re;

    pa_assert(resource);
    pa_assert(name || id);

    re = pa_xnew0(pa_resource_rset_entry, 1);

    re->streams = pa_xnew0(pa_resource_stream_entry *, 1);
    re->rset = pa_resource_rset_data_new();

    if (name && !pa_streq(name, "<unknown>")) {
        pa_xfree(re->name);
        re->name = pa_xstrdup(name);

        if (pa_hashmap_put(resource->rsets.name, re->name, re) != 0) {
            rset_entry_free(resource, re);
            return NULL;
        }
    }

    if (id && !pa_streq(id, "<unknown>")) {
        pa_xfree(re->id);
        re->id = pa_xstrdup(id);

        if (pa_hashmap_put(resource->rsets.id, re->id, re) != 0) {
            rset_entry_free(resource, re);
            return NULL;
        }
    }

    return re;
}

static void rset_entry_free(pa_resource *resource, pa_resource_rset_entry *re)
{
    if (re) {
        if (re->name && !pa_streq(re->name, "<unknown>"))
            pa_hashmap_remove(resource->rsets.name, re->name);
        if (re->id && !pa_streq(re->id, "<unknown>"))
            pa_hashmap_remove(resource->rsets.id, re->id);

        pa_xfree(re->streams);
        pa_xfree(re->name);
        pa_xfree(re->id);
        pa_resource_rset_data_free(re->rset);

        pa_xfree(re);
    }
}


static int rset_entry_add_stream_link(pa_resource_rset_entry *re,
                                      pa_resource_stream_entry *se)
{
    size_t size;
    int i;

    pa_assert(re);

    if (!se)
        return -1;

    for (i = 0;  i < re->nstream;  i++) {
        if (re->streams[i] == se)
            return -1;
    }

    re->nstream++;
    size = sizeof(pa_resource_stream_entry *) * (re->nstream + 1);
    re->streams = pa_xrealloc(re->streams, size);

    re->streams[i+0] = se;
    re->streams[i+1] = NULL;

    return 0;
}


static int rset_entry_remove_stream_link( pa_resource_rset_entry *re,
                                          pa_resource_stream_entry *se)
{
    size_t i,j;

    pa_assert(re);

    if (se) {
        for (i = 0;  i < re->nstream;  i++) {
            if (re->streams[i] == se) {
                for (j = i;  j < re->nstream;  j++)
                    re->streams[j] = re->streams[j+1];

                re->nstream--;

                return 0;
            }
        }
    }

    return -1;
}

static void rset_entry_is_dead(pa_resource *resource, pa_resource_rset_entry *re)
{
    pa_assert(resource);
    pa_assert(re);

    if (!re->dead) {
        if (re->type[PA_RESOURCE_INPUT])
            resource->rsets.nres[PA_RESOURCE_INPUT]--;

        if (re->type[PA_RESOURCE_OUTPUT])
            resource->rsets.nres[PA_RESOURCE_OUTPUT]--;

        if (re->nstream > 1 || re->streams[0]->node) {
            pa_log_debug("rset (id='%s' name='%s') "
                         "was not updated => mark it as 'dead'",
                         re->id ? re->id : "<unknown>",
                         re->name ? re->name : "<unknown>");
            re->dead = true;
        }
        else {
            pa_log_debug("rset (id='%s' name='%s') "
                         "was not updated => remove it",
                         re->id ? re->id : "<unknown>",
                         re->name ? re->name : "<unknown>");

            stream_entry_free(resource, re->streams[0]);
            rset_entry_free(resource, re);
        }
    }
}


int pa_resource_stream_update(struct userdata *u,
                              const char *name,
                              const char *id,
                              mir_node *node)
{
    pa_resource *resource;
    pa_resource_stream_entry *se, *de;
    pa_resource_rset_entry *re;
    bool has_name, has_id;
    size_t i;

    pa_assert(u);
    pa_assert_se((resource = u->resource));

    if (!node)
        return -1;

    re = NULL;
    has_name = name && name[0] && !pa_streq(name, "<unknown>");
    has_id = id && id[0] && !pa_streq(id, "<unknown>");

    if (!has_name && !has_id)
        return -1;

    if (!(se = pa_hashmap_get(resource->streams.node, node))) {
        if (((has_id   && (se = pa_hashmap_get(resource->streams.id, id)))  ||
             (has_name && (se = pa_hashmap_get(resource->streams.name, name)))) &&
            (se->node == NULL))
        {
            /* we have an incomplete stream entry created by an rset, ie.
               the rset was created first*/

            se->node = node;

            if (pa_hashmap_put(resource->streams.node, se->node, se) != 0) {
                pa_log_error("failed to add stream (id='%s' name='%s') "
                             "to node hashmap",
                             se->id   ? se->id : "<unknown>",
                             se->name ? se->name : "<unknown>");
                return -1;
            }
        }
        else {
            /* we need to create a new stream entry */
            if ((has_name && (re = pa_hashmap_get(resource->rsets.name, name)))||
                (has_id   && (re = pa_hashmap_get(resource->rsets.id, id))))
            {
                /* found a matching rset that controls multiple streams, ie.
                   the rset was created first */
                pa_assert(re->nstream > 0);

                if (!(se = stream_entry_new(resource, NULL, NULL, node))) {
                    pa_log("failed to create stream (id='%s' name='%s'): "
                           "duplicate stream node",
                           id ? id : "<unknown>", name ? name : "<unknown>");
                    return -1;
                }

                if (has_id) {
                    pa_log("removing stream (id='%s') form id hash", id);
                    if ((de = pa_hashmap_remove(resource->streams.id, id))) {
                        pa_xfree(de->id);
                        de->id = NULL;
                    }
                }

                if (has_name) {
                    pa_log("removing stream (name='%s') form name hash", name);
                    if ((de = pa_hashmap_remove(resource->streams.name, name))) {
                        pa_xfree(de->name);
                        de->name = NULL;
                    }
                }
            }
            else {
                /* could not find matching rset entry, ie.
                   the stream was created first */

                if (!(se = stream_entry_new(resource, name, id, node))) {
                    pa_log("failed to create stream (id='%s' name='%s'): "
                           "invalid stream id/name or duplicate stream",
                           id ? id : "<unknown>", name ? name : "<unknown>");
                    return -1;
                }

                if (!(re = rset_entry_new(resource, name, id))) {
                    pa_log("failed to link stream (id='%s' name='%s') to rset: "
                           "invalid rset id/name or duplicate rset",
                           id ? id : "<null>", name ? name : "<null>");
                    stream_entry_free(resource, se);
                    return -1;
                }
            }

            pa_assert(re);

            stream_entry_add_rset_link(se, re);
            rset_entry_add_stream_link(re, se);
        }
    }

    pa_assert(se);

    return 0;
}

int pa_resource_stream_remove(struct userdata *u, mir_node *node)
{
    pa_resource *resource;
    pa_resource_stream_entry *se;
    pa_resource_rset_entry *re;


    pa_assert(u);
    pa_assert_se((resource = u->resource));
    pa_assert(node);

    if (!(se = pa_hashmap_remove(resource->streams.node, node))) {
        pa_log("failed to remove stream (name='%s'): can't find it",
               node->amname ? node->amname : "<unknown>");
        return -1;
    }

    se->node = NULL;

    pa_assert(se->nrset > 0);
    pa_assert_se((re = se->rsets[0]));
    pa_assert(re->nstream > 0);

    if (se->nrset == 1) {
        /* stream is controlled by a single rset */

        if (re->nstream == 1) {
            /* the rset controls only this stream */
            pa_assert(re->streams[0] == se);

            if (re->dead) {
                stream_entry_free(resource, se);
                rset_entry_free(resource, re);
            }
        }
        else {
            /* beside this stream the rset controls other streams as well
               so it is safe to destroy this stream as the rset does not
               become streamless */
            rset_entry_remove_stream_link(re, se);
            stream_entry_remove_rset_link(se, re);

            stream_entry_free(resource, se);
        }
    }

    return 0;
}


static pa_resource_stream_entry *stream_entry_new(pa_resource *resource,
                                                  const char *name,
                                                  const char *id,
                                                  mir_node *node)
{
    pa_resource_stream_entry *se;

    pa_assert(resource);
    pa_assert(name || id);

    se = pa_xnew0(pa_resource_stream_entry, 1);

    se->rsets = pa_xnew0(pa_resource_rset_entry *, 1);

    if (name && !pa_streq(name, "<unknown>")) {
        se->name = pa_xstrdup(name);

        if (pa_hashmap_put(resource->streams.name, se->name, se) != 0) {
            stream_entry_free(resource, se);
            return NULL;
        }
    }

    if (is_number(id)) {
        if (node && is_number(node->rset.id)) {
            if (!pa_streq(id, node->rset.id)) {
                stream_entry_free(resource, se);
                return NULL;
            }
        }

        se->id = pa_xstrdup(id);

        if (pa_hashmap_put(resource->streams.id, se->id, se) != 0) {
            stream_entry_free(resource, se);
            return NULL;
        }
    }

    if (node) {
        se->node = node;

        if (pa_hashmap_put(resource->streams.node, se->node, se) != 0) {
            stream_entry_free(resource, se);
            return NULL;
        }
    }

    return se;
}


static void stream_entry_free(pa_resource *resource,
                              pa_resource_stream_entry *se)
{
    if (se) {
        if (se->name)
            pa_hashmap_remove(resource->streams.name, se->name);
        if (se->id)
            pa_hashmap_remove(resource->streams.id, se->id);
        if (se->node)
            pa_hashmap_remove(resource->streams.node, se->node);

        pa_xfree(se->rsets);
        pa_xfree(se->name);
        pa_xfree(se->id);

        pa_xfree(se);
    }
}

static int stream_entry_add_rset_link(pa_resource_stream_entry *se,
                                      pa_resource_rset_entry *re)
{
    size_t size;
    int i;

    pa_assert(se);

    if (!re)
        return -1;

    for (i = 0;  i < se->nrset;  i++) {
        if (se->rsets[i] == re)
            return -1;
    }

    se->nrset++;
    size = sizeof(pa_resource_rset_entry *) * (se->nrset + 1);
    se->rsets = pa_xrealloc(se->rsets, size);

    se->rsets[i+0] = re;
    se->rsets[i+1] = NULL;

    return 0;
}

static int stream_entry_remove_rset_link(pa_resource_stream_entry *se,
                                         pa_resource_rset_entry *re)
{
    size_t i,j;

    pa_assert(se);

    if (re) {
        for (i = 0;  i < se->nrset;  i++) {
            if (se->rsets[i] == re) {
                for (j = i;  j < se->nrset;  j++)
                    se->rsets[j] = se->rsets[j+1];

                se->nrset--;

                return 0;
            }
        }
    }

    return -1;
}


static bool is_number(const char *string)
{
    const char *p = string;

    if (string)
        for (p = string;  isdigit(*p);   p++) ;
    else
        p = "a";

    return *p ? false : true;
}

static void enforce_policy(struct userdata *u,
                           mir_node *node,
                           pa_resource_rset_data *rset,
                           int type)
{
    int req;
    char *policy;

    pa_assert(u);
    pa_assert(node);
    pa_assert(rset);
    pa_assert(type == PA_RESOURCE_INPUT || PA_RESOURCE_OUTPUT);
    pa_assert_se((policy = rset->policy[type]));


    if (pa_streq(policy, "relaxed"))
        req = PA_STREAM_RUN;
    else if (pa_streq(policy, "strict")) {
        if (rset->state == PA_RESOURCE_RELEASE && rset->autorel)
            req = PA_STREAM_KILL;
        else {
            if (rset->grant)
                req = PA_STREAM_RUN;
            else
                req = PA_STREAM_BLOCK;
        }
    }
    else {
        req = PA_STREAM_BLOCK;
    }

    pa_stream_state_change(u, node, req);
}
