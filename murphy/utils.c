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
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/core-util.h>
#include <pulsecore/card.h>
#include <pulsecore/sink.h>
#include <pulsecore/source.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#include "userdata.h"
#include "utils.h"
#include "node.h"

#ifndef DEFAULT_CONFIG_DIR
#define DEFAULT_CONFIG_DIR "/etc/pulse"
#endif

#define DEFAULT_NULL_SINK_NAME "null.mir"

struct pa_null_sink {
    char      *name;
    uint32_t   module_index;
    uint32_t   sink_index;
};


static uint32_t stamp;

static char *stream_name(pa_proplist *);


pa_null_sink *pa_utils_create_null_sink(struct userdata *u, const char *name)
{
    pa_core      *core;
    pa_module    *module;
    pa_null_sink *ns;
    pa_sink      *sink;
    pa_sink      *s;
    uint32_t      idx;
    char          args[256];

    pa_assert(u);
    pa_assert_se((core = u->core));


    if (!name)
        name = DEFAULT_NULL_SINK_NAME;


    snprintf(args, sizeof(args), "sink_name=\"%s\" channels=2", name);
    module = pa_module_load(core, "module-null-sink", args);
    sink = NULL;

    if (!module)
        pa_log("failed to load null sink '%s'", name);
    else {
        PA_IDXSET_FOREACH(s, core->sinks, idx) {
            if (s->module && s->module == module) {
                sink = s;
                pa_log_info("mir null sink is '%s'", name);
                break;
            }
        }
    }

    ns = pa_xnew0(pa_null_sink, 1);
    ns->name = pa_xstrdup(name);
    ns->module_index = module ? module->index : PA_IDXSET_INVALID;
    ns->sink_index = sink ? sink->index : PA_IDXSET_INVALID;

    return ns;
}

void pa_utils_destroy_null_sink(struct userdata *u)
{
    pa_core      *core;
    pa_module    *module;
    pa_null_sink *ns;

    if (u && (ns = u->nullsink) && (core = u->core)) {
        if ((module = pa_idxset_get_by_index(core->modules,ns->module_index))){
            pa_log_info("unloading null sink '%s'", ns->name);
            pa_module_unload(core, module, FALSE);
        }

        pa_xfree(ns->name);
        pa_xfree(ns);
    }
}

pa_sink *pa_utils_get_null_sink(struct userdata *u)
{
    pa_core *core;
    pa_null_sink *ns;

    pa_assert(u);
    pa_assert_se((core = u->core));
    pa_assert_se((ns = u->nullsink));

    return pa_idxset_get_by_index(core->sinks, ns->sink_index);
}

pa_source *pa_utils_get_null_source(struct userdata *u)
{
    pa_sink *ns = pa_utils_get_null_sink(u);

    return ns ? ns->monitor_source : NULL;
}



char *pa_utils_get_card_name(pa_card *card)
{
    return (card && card->name) ? card->name : "<unknown>";
}

char *pa_utils_get_sink_name(pa_sink *sink)
{
    return (sink && sink->name) ? sink->name : "<unknown>";
}

char *pa_utils_get_source_name(pa_source *source)
{
    return (source && source->name) ? source->name : "<unknown>";
}

char *pa_utils_get_sink_input_name(pa_sink_input *sinp)
{
    char *name;

    if (sinp && (name = stream_name(sinp->proplist)))
        return name;

    return "<unknown>";
}

char *pa_utils_get_sink_input_name_from_data(pa_sink_input_new_data *data)
{
    char *name;

    if (data && (name = stream_name(data->proplist)))
        return name;

    return "<unknown>";
}


char *pa_utils_get_source_output_name(pa_source_output *sout)
{
    char *name;

    if (sout && (name = stream_name(sout->proplist)))
        return name;

    return "<unknown>";
}

char *pa_utils_get_source_output_name_from_data(pa_source_output_new_data*data)
{
    char *name;

    if (data && (name = stream_name(data->proplist)))
        return name;

    return "<unknown>";
}


void pa_utils_set_stream_routing_properties(pa_proplist *pl,
                                            int          styp,
                                            void        *target)
{
    const char    *clnam;
    const char    *method;
    char           clid[32];

    pa_assert(pl);
    pa_assert(styp >= 0);

    snprintf(clid, sizeof(clid), "%d", styp);
    clnam  = mir_node_type_str(styp);
    method = target ? PA_ROUTING_EXPLICIT : PA_ROUTING_DEFAULT;

    if (pa_proplist_sets(pl, PA_PROP_ROUTING_CLASS_NAME, clnam ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_ROUTING_CLASS_ID  , clid  ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_ROUTING_METHOD    , method) < 0  )
    {
        pa_log("failed to set some stream property");
    }
}

void pa_utils_set_stream_routing_method_property(pa_proplist *pl,
                                                 pa_bool_t explicit)
{
    const char *method = explicit ? PA_ROUTING_EXPLICIT : PA_ROUTING_DEFAULT;

    pa_assert(pl);

    if (pa_proplist_sets(pl, PA_PROP_ROUTING_METHOD, method) < 0) {
        pa_log("failed to set routing method property on sink-input");
    }
}

pa_bool_t pa_utils_stream_has_default_route(pa_proplist *pl)
{
    const char *method;

    pa_assert(pl);

    method = pa_proplist_gets(pl, PA_PROP_ROUTING_METHOD);

    if (method && pa_streq(method, PA_ROUTING_DEFAULT))
        return TRUE;

    return FALSE;
}

int pa_utils_get_stream_class(pa_proplist *pl)
{
    const char *clid_str;
    char *e;
    unsigned long int clid = 0;

    pa_assert(pl);

    if ((clid_str = pa_proplist_gets(pl, PA_PROP_ROUTING_CLASS_ID))) {
        clid = strtoul(clid_str, &e, 10);

        if (*e)
            clid = 0;
    }

    return (int)clid;
}

void pa_utils_set_port_properties(pa_device_port *port, mir_node *node)
{
    char nodeidx[256];

    pa_assert(port);
    pa_assert(port->proplist);
    pa_assert(node);

    snprintf(nodeidx, sizeof(nodeidx), "%u", node->index);

    pa_proplist_sets(port->proplist, PA_PROP_NODE_INDEX, nodeidx);
}

mir_node *pa_utils_get_node_from_port(struct userdata *u,
                                      pa_device_port *port)
{
    const char *value;
    char *e;
    uint32_t index = PA_IDXSET_INVALID;
    mir_node *node = NULL;

    pa_assert(u);
    pa_assert(port);
    pa_assert(port->proplist);

    if ((value = pa_proplist_gets(port->proplist, PA_PROP_NODE_INDEX))) {
        index = strtoul(value, &e, 10);

        if (value[0] && !e[0])
            node = mir_node_find_by_index(u, index);
    }

    return node;
}

mir_node *pa_utils_get_node_from_stream(struct userdata *u,
                                        mir_direction    type,
                                        void            *ptr)
{
    pa_sink_input    *sinp;
    pa_source_output *sout;
    pa_proplist      *pl;
    mir_node         *node;
    const char       *index_str;
    uint32_t          index = PA_IDXSET_INVALID;
    char             *e;
    char              name[256];

    pa_assert(u);
    pa_assert(ptr);
    pa_assert(type == mir_input || type == mir_output);

    if (type == mir_input) {
        sinp = (pa_sink_input *)ptr;
        pl = sinp->proplist;
        snprintf(name, sizeof(name), "sink-input.%u", sinp->index);
    }
    else {
        sout = (pa_source_output *)ptr;
        pl = sout->proplist;
        snprintf(name, sizeof(name), "source-output.%u", sout->index);
    }


    if ((index_str = pa_proplist_gets(pl, PA_PROP_NODE_INDEX))) {
        index = strtoul(index_str, &e, 10);
        if (e != index_str && *e == '\0') {
            if ((node = mir_node_find_by_index(u, index)))
                return node;

            pa_log_debug("can't find find node for %s", name);
        }
    }

    return NULL;
}

mir_node *pa_utils_get_node_from_data(struct userdata *u,
                                      mir_direction    type,
                                      void            *ptr)
{
    pa_sink_input_new_data *sinp;
    pa_source_output_new_data *sout;
    pa_proplist  *pl;
    mir_node     *node;
    const char   *index_str;
    uint32_t      index = PA_IDXSET_INVALID;
    char         *e;
    char          name[256];

    pa_assert(u);
    pa_assert(ptr);
    pa_assert(type == mir_input || type == mir_output);

    if (type == mir_input) {
        sinp = (pa_sink_input_new_data *)ptr;
        pl = sinp->proplist;
        snprintf(name, sizeof(name), "sink-input");
    }
    else {
        sout = (pa_source_output_new_data *)ptr;
        pl = sout->proplist;
        snprintf(name, sizeof(name), "source-output");
    }


    if ((index_str = pa_proplist_gets(pl, PA_PROP_NODE_INDEX))) {
        index = strtoul(index_str, &e, 10);
        if (e != index_str && *e == '\0') {
            if ((node = mir_node_find_by_index(u, index)))
                return node;

            pa_log_debug("can't find find node for %s", name);
        }
    }

    return NULL;
}

static char *stream_name(pa_proplist *pl)
{
    const char  *appnam;
    const char  *binnam;

    if ((appnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_NAME)))
        return (char *)appnam;

    if ((binnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_BINARY)))
        return (char *)binnam;

    return NULL;
}


const char *pa_utils_file_path(const char *file, char *buf, size_t len)
{
    pa_assert(file);
    pa_assert(buf);
    pa_assert(len > 0);

    snprintf(buf, len, "%s/%s", DEFAULT_CONFIG_DIR, file);

    return buf;
}


uint32_t pa_utils_new_stamp(void)
{
    return ++stamp;
}

uint32_t pa_utils_get_stamp(void)
{
    return stamp;
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */


