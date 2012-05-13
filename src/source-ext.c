#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>

#include <pulsecore/core-util.h>
#include <pulsecore/source.h>

#include "source-ext.h"
#include "classify.h"
#include "context.h"
#include "policy-group.h"
#include "dbusif.h"

/* this included for the sake of pa_policy_send_device_state()
   which is temporarily hosted by sink-ext.c*/
#include "sink-ext.h"

/* hooks */
static pa_hook_result_t source_put(void *, void *, void *);
static pa_hook_result_t source_unlink(void *, void *, void *);

static void handle_new_source(struct userdata *, struct pa_source *);
static void handle_removed_source(struct userdata *, struct pa_source *);



struct pa_source_evsubscr *pa_source_ext_subscription(struct userdata *u)
{
    pa_core                   *core;
    pa_hook                   *hooks;
    struct pa_source_evsubscr *subscr;
    pa_hook_slot              *put;
    pa_hook_slot              *unlink;
    
    pa_assert(u);
    pa_assert_se((core = u->core));

    hooks  = core->hooks;
    
    put    = pa_hook_connect(hooks + PA_CORE_HOOK_SOURCE_PUT,
                             PA_HOOK_LATE, source_put, (void *)u);
    unlink = pa_hook_connect(hooks + PA_CORE_HOOK_SOURCE_UNLINK,
                             PA_HOOK_LATE, source_unlink, (void *)u);


    subscr = pa_xnew0(struct pa_source_evsubscr, 1);
    
    subscr->put    = put;
    subscr->unlink = unlink;
    
    return subscr;
}

void pa_source_ext_subscription_free(struct pa_source_evsubscr *subscr)
{
    if (subscr != NULL) {
        pa_hook_slot_free(subscr->put);
        pa_hook_slot_free(subscr->unlink);

        pa_xfree(subscr);
    }
}

void pa_source_ext_discover(struct userdata *u)
{
    void             *state = NULL;
    pa_idxset        *idxset;
    struct pa_source *source;

    pa_assert(u);
    pa_assert(u->core);
    pa_assert_se((idxset = u->core->sources));

    while ((source = pa_idxset_iterate(idxset, &state, NULL)) != NULL)
        handle_new_source(u, source);
}


char *pa_source_ext_get_name(struct pa_source *source)
{
    return source->name ? source->name : (char *)"<unknown>";
}

int pa_source_ext_set_mute(struct userdata *u, char *type, int mute)
{
    void              *state = NULL;
    pa_idxset         *idxset;
    struct pa_source  *source;
    char              *name;
    pa_bool_t          current_mute;

    pa_assert(u);
    pa_assert(type);
    pa_assert(u->core);
    pa_assert_se((idxset = u->core->sources));

    while ((source = pa_idxset_iterate(idxset, &state, NULL)) != NULL) {
        if (pa_classify_is_source_typeof(u, source, type, NULL)) {
            name = pa_source_ext_get_name(source);
            current_mute = pa_source_get_mute(source, 0);

            if ((current_mute && mute) || (!current_mute && !mute)) {
                pa_log_debug("%s() source '%s' type '%s' is already %smuted",
                             __FUNCTION__, name, type, mute ? "" : "un");
            }
            else {
                pa_log_debug("%s() %smute source '%s' type '%s'",
                             __FUNCTION__, mute ? "" : "un", name, type);
            
#if PULSEAUDIO_HAS_PORTS
                pa_source_set_mute(source, mute, TRUE);
#else
                pa_source_set_mute(source, mute);
#endif
            }
            
            return 0;
        }
    }


    return -1;
}

int pa_source_ext_set_ports(struct userdata *u, const char *type)
{
    int ret = 0;

#if PULSEAUDIO_HAS_PORTS
    pa_source *source;
    struct pa_classify_device_data *data;
    uint32_t idx;

    pa_assert(u);
    pa_assert(u->core);

    PA_IDXSET_FOREACH(source, u->core->sources, idx) {
        /* Check whether the port of this source should be changed. */
        if (pa_classify_is_port_source_typeof(u, source, type, &data)) {
            struct pa_classify_port_entry *port_entry;

            pa_assert_se(port_entry = pa_hashmap_get(data->ports,
                                                     source->name));

            if (!source->active_port ||
                    !pa_streq(port_entry->port_name,
                              source->active_port->name)) {

                if (pa_source_set_port(source, port_entry->port_name,
                                       FALSE) < 0) {
                    ret = -1;
                    pa_log("failed to set source '%s' port to '%s'",
                           source->name, port_entry->port_name);
                }
                else {
                    pa_log_debug("changed source '%s' port to '%s'",
                                 source->name, port_entry->port_name);
                }
            }
        }
    }
#endif

    return ret;
}

static pa_hook_result_t source_put(void *hook_data, void *call_data,
                                       void *slot_data)
{
    struct pa_source  *source = (struct pa_source *)call_data;
    struct userdata *u    = (struct userdata *)slot_data;

    handle_new_source(u, source);

    return PA_HOOK_OK;
}


static pa_hook_result_t source_unlink(void *hook_data, void *call_data,
                                          void *slot_data)
{
    struct pa_source  *source = (struct pa_source *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    handle_removed_source(u, source);

    return PA_HOOK_OK;
}

static void handle_new_source(struct userdata *u, struct pa_source *source)
{
    char            *name;
    uint32_t         idx;
    char             buf[1024];
    int              len;
    int              ret;

    if (source && u) {
        name = pa_source_ext_get_name(source);
        idx  = source->index;
        len  = pa_classify_source(u, source, 0,0, buf, sizeof(buf));


        if (len <= 0)
                pa_log_debug("new source '%s' (idx=%d)", name, idx);
        else {
            ret = pa_proplist_sets(source->proplist,
                                   PA_PROP_POLICY_DEVTYPELIST, buf);

            pa_policy_context_register(u,pa_policy_object_source,name,source);

            if (ret < 0) {
                pa_log("failed to set property '%s' on source '%s'",
                       PA_PROP_POLICY_DEVTYPELIST, name);
            }
            else {
                pa_log_debug("new source '%s' (idx=%d type %s)",
                             name, idx, buf);
#if 0
                pa_policy_groupset_update_default_source(u, PA_IDXSET_INVALID);
#endif
                pa_policy_groupset_register_source(u, source);

                len = pa_classify_source(u, source, PA_POLICY_DISABLE_NOTIFY,0,
                                         buf, sizeof(buf));
                if (len > 0) {
                    pa_policy_send_device_state(u, PA_POLICY_CONNECTED, buf);
                }
            }
        }
    }
}

static void handle_removed_source(struct userdata *u, struct pa_source *source)
{
    char            *name;
    uint32_t         idx;
    char             buf[1024];
    int              len;

    if (source && u) {
        name = pa_source_ext_get_name(source);
        idx  = source->index;
        len  = pa_classify_source(u, source, 0,0, buf, sizeof(buf));

        pa_policy_context_unregister(u, pa_policy_object_source,
                                     name, source, idx);

        if (len <= 0)
            pa_log_debug("remove source '%s' (idx=%d)", name, idx);
        else {
            pa_log_debug("remove source '%s' (idx=%d, type=%s)", name,idx,buf);
            
#if 0
            pa_policy_groupset_update_default_source(u, idx);
#endif
            pa_policy_groupset_unregister_source(u, idx);

            len = pa_classify_source(u, source, PA_POLICY_DISABLE_NOTIFY,0,
                                     buf, sizeof(buf));
            if (len > 0) {
                pa_policy_send_device_state(u, PA_POLICY_DISCONNECTED, buf);
            }
        }
    }
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
