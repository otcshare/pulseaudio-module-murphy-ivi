#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>
#include <pulsecore/thread.h>
#include <pulsecore/strlist.h>
#include <pulsecore/time-smoother.h>
#include <pulsecore/sink.h>
#include <pulsecore/sink-input.h>

#include <combine/userdata.h>

#include "multiplex.h"
#include "utils.h"

#ifndef DEFAULT_RESAMPLER
#define DEFAULT_RESAMPLER "speex-float-3"
#endif


pa_multiplex *pa_multiplex_init(void)
{
    pa_multiplex *multiplex = pa_xnew0(pa_multiplex, 1);

    return multiplex;
}


void pa_multiplex_done(pa_multiplex *multiplex, pa_core *core)
{
    pa_muxnode *mux, *n;

    PA_LLIST_FOREACH_SAFE(mux,n, multiplex->muxnodes) {
        pa_module_unload_by_index(core, mux->module_index, FALSE);
    }
}



pa_muxnode *pa_multiplex_create(pa_multiplex   *multiplex,
                                pa_core        *core,
                                uint32_t        sink_index,
                                pa_channel_map *chmap,
                                const char     *resampler,
                                int             type)
{
    static char *modnam = "module-combine-sink";

    struct userdata *u;         /* combine's userdata! */
    struct output   *o;
    pa_muxnode      *mux;
    pa_sink         *sink;
    pa_sink_input   *sinp;
    pa_module       *module;
    char             args[512];
    uint32_t         idx;

    pa_assert(core);

    if (!resampler)
        resampler = DEFAULT_RESAMPLER;

    if (!(sink = pa_idxset_get_by_index(core->sinks, sink_index))) {
        pa_log_debug("can't find the primary sink (index %u) for multiplexer",
                     sink_index);
        return NULL;
    }

    snprintf(args, sizeof(args), "slaves=\"%s\" resample_method=\"%s\" "
             "channels=%u", sink->name, resampler, chmap->channels);

    if (!(module = pa_module_load(core, modnam, args))) {
        pa_log("failed to load module '%s %s'. can't multiplex", modnam, args);
        return NULL;
    }

    pa_assert_se((u = module->userdata));
    pa_assert(u->sink);

    mux = pa_xnew0(pa_muxnode, 1);
    mux->module_index = module->index;
    mux->sink_index = u->sink->index;

    PA_LLIST_PREPEND(pa_muxnode, multiplex->muxnodes, mux);

    if (!(o = pa_idxset_first(u->outputs, &idx)))
        pa_log("can't find default multiplexer stream");
    else {
        if ((sinp = o->sink_input))
            pa_utils_set_stream_routing_properties(sinp->proplist, type, NULL);
    }

    pa_log_debug("multiplexer succesfully loaded");

    return mux;
}


void pa_multiplex_destroy(pa_multiplex *multiplex,
                          pa_core      *core,
                          pa_muxnode   *mux)
{
    pa_assert(multiplex);
    pa_assert(core);

    if (mux) {
        PA_LLIST_REMOVE(pa_muxnode, multiplex->muxnodes, mux);
        pa_module_unload_by_index(core, mux->module_index, FALSE);
        pa_xfree(mux);
    }
}


pa_muxnode *pa_multiplex_find(pa_multiplex *multiplex, uint32_t sink_index)
{
    pa_muxnode *mux;

    PA_LLIST_FOREACH(mux, multiplex->muxnodes) {
        if (sink_index == mux->sink_index) {
            pa_log_debug("muxnode found for sink %u", sink_index);
            return mux;
        }
    }

    pa_log_debug("can't find muxnode for sink %u", sink_index);

    return NULL;
}

pa_sink_input *pa_multiplex_default_stream(pa_core *core, pa_muxnode *mux)
{
    pa_module       *module;
    pa_sink_input   *sinp;
    struct userdata *u;         /* combine's userdata! */
    struct output   *o;
    uint32_t         idx;

    pa_assert(core);
    pa_assert(mux);
   
    if ((module = pa_idxset_get_by_index(core->modules, mux->module_index)) &&
        (u = module->userdata))
    {
        pa_log_debug("trying to find the default stream on mux %u",
                     module->index);

        PA_IDXSET_FOREACH(o, u->outputs, idx) {            
            if (!(sinp = o->sink_input))
                continue;

            pa_log_debug("  trying sink input %u", sinp->index);

            if (pa_utils_stream_has_default_route(sinp->proplist)) {
                pa_log_debug("sink input %u is the default", sinp->index);
                return sinp;
            }

            pa_log_debug("  no");
        }
    }

    pa_log_debug("could not find the default stream on mux %u", module->index);
    
    return NULL;
}


int pa_multiplex_print(pa_muxnode *mux, char *buf, int len)
{
    char *p, *e;

    pa_assert(buf);
    pa_assert(len > 0);

    e = (p = buf) + len;

    if (!mux)
        p += snprintf(p, e-p, "<not set>");
    else {
        p += snprintf(p, e-p, "module %u, sink %u",
                      mux->module_index, mux->sink_index);
    }
    
    return p - buf;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

