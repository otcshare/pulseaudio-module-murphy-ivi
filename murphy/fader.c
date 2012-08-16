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

typedef struct {
    uint32_t fade_out;
    uint32_t fade_in;
} transition_time;


struct pa_fader {
    transition_time transit;
};

static void set_stream_volume_limit(struct userdata *, pa_sink_input *,
                                    double, uint32_t);

pa_fader *pa_fader_init(const char *fade_out_str, const char *fade_in_str)
{
    pa_fader *fader = pa_xnew0(pa_fader, 1);

    if (!fade_out_str || pa_atou(fade_out_str, &fader->transit.fade_out) < 0)
        fader->transit.fade_out = 200;

    if (!fade_in_str || pa_atou(fade_in_str, &fader->transit.fade_in) < 0)
        fader->transit.fade_in = 1000;

    if (fader->transit.fade_out > 10000)
        fader->transit.fade_out = 10000;

    if (fader->transit.fade_in > 10000)
        fader->transit.fade_in = 10000;

    pa_log_info("fader transition times: out %u ms, in %u ms",
                fader->transit.fade_out, fader->transit.fade_in);

    return fader;
}

void pa_fader_done(struct userdata *u)
{
    if (u) {
        pa_xfree(u->fader);
    }
}



void pa_fader_apply_volume_limits(struct userdata *u, uint32_t stamp)
{
    pa_core         *core;
    transition_time *transit;
    pa_sink         *sink;
    pa_sink_input   *sinp;
    mir_node        *node;
    double           atten;
    uint32_t         time;
    uint32_t         i,j;
    int              class;

    pa_assert(u);
    pa_assert_se(u->fader);
    pa_assert_se((core = u->core));

    transit = &u->fader->transit;

    pa_log_debug("applying volume limits ...");

    PA_IDXSET_FOREACH(sink, core->sinks, i) {
        if ((node = pa_discover_find_node_by_ptr(u, sink))) {
            pa_log_debug("   node '%s'", node->amname);
            
            PA_IDXSET_FOREACH(sinp, sink->inputs, j) {
                class = pa_utils_get_stream_class(sinp->proplist);
                atten = mir_volume_apply_limits(u, node, class, stamp);
                time  = (atten == 0.0) ? transit->fade_in : transit->fade_out;

                pa_log_debug("     stream %u attenuation %.2lf dB "
                             "transition time %u ms", sinp->index, atten,time);

                set_stream_volume_limit(u, sinp, atten, time);
            }
        }
    }
}


static void set_stream_volume_limit(struct userdata *u,
                                    pa_sink_input   *sinp,
                                    double           limit,
                                    uint32_t         ramp_time)
{
    pa_sink *sink;
    pa_volume_t vol;
    pa_cvolume rampvol;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((sink = sinp->sink));

    vol = pa_sw_volume_from_dB(limit);

    if (!ramp_time) {
        pa_cvolume_set(&sinp->volume_factor, sinp->volume.channels, vol);

        if (pa_sink_flat_volume_enabled(sink)) {
            pa_sink_set_volume(sink, NULL, TRUE, FALSE);
        }
        else {
            pa_sw_cvolume_multiply(&sinp->soft_volume, &sinp->real_ratio,
                                   &sinp->volume_factor);

            pa_asyncmsgq_send(sink->asyncmsgq, PA_MSGOBJECT(sinp),
                              PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME,
                              NULL, 0, NULL);
        }
    }
    else {
        pa_cvolume_set(&rampvol, sinp->volume.channels, vol);

        pa_sink_input_set_volume_ramp(sinp, &rampvol, ramp_time,
                                      PA_VOLUME_RAMP_TYPE_LINEAR,
                                      TRUE, FALSE);
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
