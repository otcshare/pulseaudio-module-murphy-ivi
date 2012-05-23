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

#include <pulsecore/card.h>
#include <pulsecore/sink.h>
#include <pulsecore/source.h>
#include <pulsecore/sink-input.h>

#include "userdata.h"
#include "utils.h"

#ifndef DEFAULT_CONFIG_DIR
#define DEFAULT_CONFIG_DIR "/etc/pulse"
#endif

#define DEFAULT_NULL_SINK_NAME "null.mir"

typedef struct pa_null_sink {
    char      *name;
    uint32_t   module_index;
    uint32_t   sink_index;
} pa_null_sink;


static uint32_t stamp;

static char *sink_input_name(pa_proplist *);


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

    if (sinp && (name = sink_input_name(sinp->proplist)))
        return name;
    
    return "<unknown>";
}

char *pa_utils_get_sink_input_name_from_data(pa_sink_input_new_data *data)
{
    char *name;

    if (data && (name = sink_input_name(data->proplist)))
        return name;
    
    return "<unknown>";
}

static char *sink_input_name(pa_proplist *pl)
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
    /*
    pa_assert(file);
    pa_assert(buf);
    pa_assert(len > 0);
    */

    snprintf(buf, len, "%s/%s", DEFAULT_CONFIG_DIR, file);

    return buf;
}


const uint32_t pa_utils_new_stamp(void)
{
    return ++stamp;
}

const uint32_t pa_utils_get_stamp(void)
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


