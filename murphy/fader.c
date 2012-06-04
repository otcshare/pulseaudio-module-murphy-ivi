#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/proplist.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sink.h>
#include <pulsecore/sink-input.h>

#include "fader.h"
#include "node.h"
#include "discover.h"
#include "volume.h"
#include "utils.h"

static void set_stream_volume_limit(struct userdata *, pa_sink_input *, double);

void pa_fader_apply_volume_limits(struct userdata *u, uint32_t stamp)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    mir_node      *node;
    double         atten;
    uint32_t       i,j;
    int            class;

    pa_assert(u);
    pa_assert_se((core = u->core));

    pa_log_debug("applying volume limits ...");

    PA_IDXSET_FOREACH(sink, core->sinks, i) {
        if ((node = pa_discover_find_node_by_ptr(u, sink))) {
            pa_log_debug("   node '%s'", node->amname);
            
            PA_IDXSET_FOREACH(sinp, sink->inputs, j) {
                class = pa_utils_get_stream_class(sinp->proplist);
                atten = mir_volume_apply_limits(u, node, class, stamp);
                
                pa_log_debug("     stream %u attenuation %.2lf dB", sinp->index, atten);

                set_stream_volume_limit(u, sinp, atten);
            }
        }
    }
}


static void set_stream_volume_limit(struct userdata *u, pa_sink_input *sinp, double limit)
{
    pa_sink *sink;
    pa_volume_t vol;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((sink = sinp->sink));

    vol = pa_sw_volume_from_dB(limit);
    pa_cvolume_set(&sinp->volume_factor, sinp->volume.channels, vol);

    if (pa_sink_flat_volume_enabled(sink)) {
        pa_sink_set_volume(sink, NULL, TRUE, FALSE);
    }
    else {
        pa_sw_cvolume_multiply(&sinp->soft_volume, &sinp->real_ratio, &sinp->volume_factor);

        pa_asyncmsgq_send(sink->asyncmsgq, PA_MSGOBJECT(sinp),
                          PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME, NULL, 0, NULL);
    }
    

}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
