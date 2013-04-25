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
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/core-util.h>

#include "stream-state.h"
#include "node.h"
#include "loopback.h"

static const char *scache_driver = "play-memblockq.c";
static pa_sink_input_flags_t flag_mask = PA_SINK_INPUT_NO_CREATE_ON_SUSPEND |
                                         PA_SINK_INPUT_KILL_ON_SUSPEND;


static void sink_input_block(pa_sink_input *, pa_bool_t);

pa_bool_t pa_stream_state_start_corked(struct userdata *u,
                                       pa_sink_input_new_data *data,
                                       pa_nodeset_resdef *resdef)
{
    if (resdef) {
        if (pa_streq(data->driver, scache_driver)) {
            pa_assert((data->flags & flag_mask) == flag_mask);
        }

        data->flags &= ~flag_mask;
        data->flags |= PA_SINK_INPUT_START_CORKED;

        return TRUE;
    }

    return FALSE;
}

void pa_stream_state_change(struct userdata *u, mir_node *node, int req)
{
    pa_loopnode *loop;
    uint32_t idx;
    pa_sink_input *sinp;
    pa_source_output *sout;
    pa_core *core;

    pa_assert(u);
    pa_assert(node);

    pa_assert_se((core = u->core));

    loop = node->loop;

    pa_assert((!loop && node->implement == mir_stream) ||
              ( loop && node->implement == mir_device)   );
    pa_assert(node->direction == mir_input || node->direction == mir_output);

    if (loop) {
        if (node->direction == mir_input) {
            sinp = pa_idxset_get_by_index(core->sink_inputs,
                                          loop->sink_input_index);
            pa_assert(sinp);

            switch (req) {
            case PA_STREAM_KILL:
            case PA_STREAM_BLOCK:
                pa_log_debug("mute '%s'", node->amname);
                pa_sink_input_set_mute(sinp, TRUE, FALSE);
                break;
                
            case PA_STREAM_RUN:
                pa_log_debug("unmute '%s'", node->amname);
                pa_sink_input_set_mute(sinp, FALSE, FALSE);
                break;
                
            default:
                pa_assert_not_reached();
                break;
            }
        }
        else {
            pa_log_debug("no enforcement for loopback on '%s'", node->amname);
            sout = pa_idxset_get_by_index(core->source_outputs,
                                          loop->source_output_index);
            pa_assert(sout);
        }
    }
    else {
        if (node->direction == mir_input) {
            sinp = pa_idxset_get_by_index(core->sink_inputs, node->paidx);
            pa_assert(sinp);

            switch (req) {
            case PA_STREAM_KILL:
                pa_log_debug("killing '%s'", node->amname);
                sinp->kill(sinp);
                break;
                
            case PA_STREAM_BLOCK:
                pa_log_debug("blocking '%s'", node->amname);
                sink_input_block(sinp, TRUE);
                break;
                
            case PA_STREAM_RUN:
                pa_log_debug("unblock '%s'", node->amname);
                sink_input_block(sinp, FALSE);
                break;
                
            default:
                pa_assert_not_reached();
                break;
            }
        }
        else {
            pa_log_debug("no enforcement for stream '%s'", node->amname);
            sout = pa_idxset_get_by_index(core->source_outputs, node->paidx);
            pa_assert(sout);
        }
    }
}


static void sink_input_block(pa_sink_input *sinp, pa_bool_t block)
{
    const char *event;
    pa_proplist *pl;

    pa_assert(sinp);

    if (sinp->driver && pa_streq(sinp->driver, scache_driver)) {
        if (block)
            sinp->flags &= ~flag_mask;
        else
            sinp->flags |= flag_mask;
    }

    pa_sink_input_cork_internal(sinp, block);

    if (sinp->send_event) {
        if (block)
            event = PA_STREAM_EVENT_REQUEST_CORK;
        else
            event = PA_STREAM_EVENT_REQUEST_UNCORK;

        pl = pa_proplist_new();

        sinp->send_event(sinp, event, pl);

        pa_proplist_free(pl);
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
