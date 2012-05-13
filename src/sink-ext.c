#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>

#include <pulsecore/core-util.h>
#include <pulsecore/sink.h>

#include "sink-ext.h"
#include "index-hash.h"
#include "classify.h"
#include "context.h"
#include "policy-group.h"
#include "dbusif.h"

/* hooks */
static pa_hook_result_t sink_put(void *, void *, void *);
static pa_hook_result_t sink_unlink(void *, void *, void *);

static void handle_new_sink(struct userdata *, struct pa_sink *);
static void handle_removed_sink(struct userdata *, struct pa_sink *);


struct pa_null_sink *pa_sink_ext_init_null_sink(const char *name)
{
    struct pa_null_sink *null_sink;

    if ((null_sink = malloc(sizeof(*null_sink))) != NULL) {
        memset(null_sink, 0, sizeof(*null_sink));

        /* sink.null is temporary to de-couple PA releases from ours */
        null_sink->name = pa_xstrdup(name ? name : /* "null" */ "sink.null");
        null_sink->sink = NULL;
    }

    return null_sink;
}

void pa_sink_ext_null_sink_free(struct pa_null_sink *null_sink)
{
    if (null_sink != NULL) {
        pa_xfree(null_sink->name);

        pa_xfree(null_sink);
    }
}

struct pa_sink_evsubscr *pa_sink_ext_subscription(struct userdata *u)
{
    pa_core                 *core;
    pa_hook                 *hooks;
    struct pa_sink_evsubscr *subscr;
    pa_hook_slot            *put;
    pa_hook_slot            *unlink;
    
    pa_assert(u);
    pa_assert_se((core = u->core));

    hooks  = core->hooks;
    
    put    = pa_hook_connect(hooks + PA_CORE_HOOK_SINK_PUT,
                             PA_HOOK_LATE, sink_put, (void *)u);
    unlink = pa_hook_connect(hooks + PA_CORE_HOOK_SINK_UNLINK,
                             PA_HOOK_LATE, sink_unlink, (void *)u);
    

    subscr = pa_xnew0(struct pa_sink_evsubscr, 1);
    
    subscr->put    = put;
    subscr->unlink = unlink;

    return subscr;
}

void  pa_sink_ext_subscription_free(struct pa_sink_evsubscr *subscr)
{
    if (subscr != NULL) {
        pa_hook_slot_free(subscr->put);
        pa_hook_slot_free(subscr->unlink);

        pa_xfree(subscr);
    }
}

void pa_sink_ext_discover(struct userdata *u)
{
    void            *state = NULL;
    pa_idxset       *idxset;
    struct pa_sink  *sink;

    pa_assert(u);
    pa_assert(u->core);
    pa_assert_se((idxset = u->core->sinks));

    while ((sink = pa_idxset_iterate(idxset, &state, NULL)) != NULL)
        handle_new_sink(u, sink);
}


struct pa_sink_ext *pa_sink_ext_lookup(struct userdata *u,struct pa_sink *sink)
{
    struct pa_sink_ext *ext;

    pa_assert(u);
    pa_assert(sink);

    ext = pa_index_hash_lookup(u->hsnk, sink->index);

    return ext;
}


char *pa_sink_ext_get_name(struct pa_sink *sink)
{
    return sink->name ? sink->name : (char *)"<unknown>";
}

int pa_sink_ext_set_ports(struct userdata *u, const char *type)
{
    int ret = 0;

#if PULSEAUDIO_HAS_PORTS
    pa_sink *sink;
    struct pa_classify_device_data *data;
    struct pa_classify_port_entry *port_entry;
    char *name;
    char *port;
    struct pa_sink_ext *ext;
    uint32_t idx;

    pa_assert(u);
    pa_assert(u->core);

    PA_IDXSET_FOREACH(sink, u->core->sinks, idx) {
        /* Check whether the port of this sink should be changed. */
        if (pa_classify_is_port_sink_typeof(u, sink, type, &data)) {

            pa_assert_se(port_entry = pa_hashmap_get(data->ports, sink->name));
            pa_assert_se(port = port_entry->port_name);

            name = pa_sink_ext_get_name(sink);
            ext  = pa_sink_ext_lookup(u, sink);

            if (ext && ext->overridden_port) {
                free(ext->overridden_port);
                ext->overridden_port = pa_xstrdup(port);
                continue;
            }

            if (!sink->active_port || !pa_streq(port,sink->active_port->name)){

                if (!ext->overridden_port) {
                    if (pa_sink_set_port(sink, port, FALSE) < 0) {
                        ret = -1;
                        pa_log("failed to set sink '%s' port to '%s'",
                               name, port);
                    }
                    else {
                        pa_log_debug("changed sink '%s' port to '%s'",
                                     name, port);
                    }
                }
                continue;
            }
        }
    } /* for */
#endif

    return ret;
}

void pa_sink_ext_set_volumes(struct userdata *u)
{
    struct pa_sink     *sink;
    struct pa_sink_ext *ext;
    uint32_t            idx;

    pa_assert(u);
    pa_assert(u->core);

    PA_IDXSET_FOREACH(sink, u->core->sinks, idx) {
        ext = pa_sink_ext_lookup(u, sink);

        pa_assert(ext);

        if (ext->need_volume_setting) {
            pa_log_debug("set sink '%s' volume", pa_sink_ext_get_name(sink));
            pa_sink_set_volume(sink, NULL, TRUE, FALSE);
            ext->need_volume_setting = FALSE;
        }
    }
}

void pa_sink_ext_override_port(struct userdata *u, struct pa_sink *sink,
                               char *port)
{
    struct pa_sink_ext *ext;
    char               *name;
    uint32_t            idx;
    char               *active_port;

    if (!sink || !u || !port)
        return;

    name = pa_sink_ext_get_name(sink);
    idx  = sink->index;
    ext  = pa_sink_ext_lookup(u, sink);

    if (ext == NULL) {
        pa_log("no extension found for sink '%s' (idx=%u)", name, idx);
        return;
    }

    active_port = sink->active_port ? sink->active_port->name : "";

    if (ext->overridden_port) {
        if (strcmp(port, active_port)) {
            pa_log_debug("attempt to multiple time to override "
                         "port on sink '%s'", name);
        }
    }
    else {
        ext->overridden_port = pa_xstrdup(active_port);

        if (strcmp(port, active_port)) {
            if (pa_sink_set_port(sink, port, FALSE) < 0)
                pa_log("failed to override sink '%s' port to '%s'", name,port);
            else
                pa_log_debug("overrode sink '%s' port to '%s'", name, port);
        }
    }
}

void pa_sink_ext_restore_port(struct userdata *u, struct pa_sink *sink)
{
    struct pa_sink_ext *ext;
    char               *name;
    uint32_t            idx;
    char               *active_port;
    char               *overridden_port;

    if (!sink || !u)
        return;

    name = pa_sink_ext_get_name(sink);
    idx  = sink->index;
    ext  = pa_sink_ext_lookup(u, sink);

    if (ext == NULL) {
        pa_log("no extension found for sink '%s' (idx=%u)", name, idx);
        return;
    }

    active_port     = sink->active_port ? sink->active_port->name : "";
    overridden_port = ext->overridden_port;

    if (overridden_port) {
        if (strcmp(overridden_port, active_port)) {
            if (pa_sink_set_port(sink, overridden_port, FALSE) < 0) {
                pa_log("failed to restore sink '%s' port to '%s'",
                       name, overridden_port);
            }
            else {
                pa_log_debug("restore sink '%s' port to '%s'",
                             name, overridden_port);
            }
        }

        pa_xfree(overridden_port);
        ext->overridden_port = NULL;
    }
}

static pa_hook_result_t sink_put(void *hook_data, void *call_data,
                                 void *slot_data)
{
    struct pa_sink  *sink = (struct pa_sink *)call_data;
    struct userdata *u    = (struct userdata *)slot_data;

    handle_new_sink(u, sink);

    return PA_HOOK_OK;
}


static pa_hook_result_t sink_unlink(void *hook_data, void *call_data,
                                    void *slot_data)
{
    struct pa_sink  *sink = (struct pa_sink *)call_data;
    struct userdata *u    = (struct userdata *)slot_data;

    handle_removed_sink(u, sink);

    return PA_HOOK_OK;
}


static void handle_new_sink(struct userdata *u, struct pa_sink *sink)
{
    char     *name;
    uint32_t  idx;
    char      buf[1024];
    int       len;
    int       ret;
    int       is_null_sink;
    struct pa_null_sink *ns;
    struct pa_sink_ext  *ext;

    if (sink && u) {
        name = pa_sink_ext_get_name(sink);
        idx  = sink->index;
        len  = pa_classify_sink(u, sink, 0,0, buf, sizeof(buf));
        ns   = u->nullsink;

        if (strcmp(name, ns->name))
            is_null_sink = FALSE;
        else {
            ns->sink = sink;
            pa_log_debug("new sink '%s' (idx=%d) will be used to "
                         "mute-by-route", name, idx);
            is_null_sink = TRUE;
        }

        pa_policy_context_register(u, pa_policy_object_sink, name, sink);

        if (len <= 0) {
            if (!is_null_sink)
                pa_log_debug("new sink '%s' (idx=%d)", name, idx);
        }
        else {
            ret = pa_proplist_sets(sink->proplist,
                                   PA_PROP_POLICY_DEVTYPELIST, buf);

            if (ret < 0) {
                pa_log("failed to set property '%s' on sink '%s'",
                       PA_PROP_POLICY_DEVTYPELIST, name);
            }
            else {
                pa_log_debug("new sink '%s' (idx=%d) (type %s)",
                             name, idx, buf);

                ext = pa_xmalloc0(sizeof(struct pa_sink_ext));
                pa_index_hash_add(u->hsnk, idx, ext);

                pa_policy_groupset_update_default_sink(u, PA_IDXSET_INVALID);
                pa_policy_groupset_register_sink(u, sink);

                len = pa_classify_sink(u, sink, PA_POLICY_DISABLE_NOTIFY,0,
                                       buf, sizeof(buf));
                if (len > 0) {
                    pa_policy_send_device_state(u, PA_POLICY_CONNECTED, buf);
                }
            }
        }
    }
}

static void handle_removed_sink(struct userdata *u, struct pa_sink *sink)
{
    char                *name;
    uint32_t             idx;
    char                 buf[1024];
    int                  len;
    struct pa_null_sink *ns;
    struct pa_sink_ext  *ext;

    if (sink && u) {
        name = pa_sink_ext_get_name(sink);
        idx  = sink->index;
        len  = pa_classify_sink(u, sink, 0,0, buf, sizeof(buf));
        ns   = u->nullsink;

        if (ns->sink == sink) {
            pa_log_debug("cease to use sink '%s' (idx=%u) to mute-by-route",
                         name, idx);

            /* TODO: move back the streams of this sink to their
               original place */

            ns->sink = NULL;
        }

        pa_policy_context_unregister(u, pa_policy_object_sink, name, sink,idx);

        if (len <= 0)
            pa_log_debug("remove sink '%s' (idx=%u)", name, idx);
        else {
            pa_log_debug("remove sink '%s' (idx=%d, type=%s)", name,idx, buf);

            pa_policy_groupset_update_default_sink(u, idx);
            pa_policy_groupset_unregister_sink(u, idx);

            if ((ext = pa_index_hash_remove(u->hsnk, idx)) == NULL)
                pa_log("no extension found for sink '%s' (idx=%u)",name,idx);
            else {
                pa_xfree(ext->overridden_port);
                pa_xfree(ext);
            }

            len = pa_classify_sink(u, sink, PA_POLICY_DISABLE_NOTIFY,0,
                                   buf, sizeof(buf));
            
            if (len > 0) {
                pa_policy_send_device_state(u, PA_POLICY_DISCONNECTED, buf);
            }
        }
    }
}

void pa_policy_send_device_state(struct userdata *u, const char *state,
                                 char *typelist) 
{
#define MAX_TYPE 256

    char *types[MAX_TYPE];
    int   ntype;
    char  buf[1024];
    char *p, *q, c;

    if (typelist && typelist[0]) {

        ntype = 0;
        
        p = typelist - 1;
        q = buf;
        
        do {
            p++;
            
            if (ntype < MAX_TYPE)
                types[ntype] = q;
            else {
                pa_log("%s() list overflow", __FUNCTION__);
                return;
            }
            
            while ((c = *p) != ' ' && c != '\0') {
                if (q < buf + sizeof(buf)-1)
                    *q++ = *p++;
                else {
                    pa_log("%s() buffer overflow", __FUNCTION__);
                    return;
                }
            }
            *q++ = '\0';
            ntype++;
            
        } while (*p);
        
        pa_policy_dbusif_send_device_state(u, (char *)state, types, ntype);
    }

#undef MAX_TYPE
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
