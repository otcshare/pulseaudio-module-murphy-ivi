#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>
#include <pulsecore/core-util.h>

#include <pulsecore/card.h>
#include <pulsecore/sink.h>
#include <pulsecore/device-port.h>
#include <pulsecore/source.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#include "switch.h"
#include "node.h"
#include "multiplex.h"


pa_bool_t set_profile(struct userdata *, mir_node *);


pa_bool_t mir_switch_setup_link(struct userdata *u,mir_node *from,mir_node *to)
{
    pa_core       *core;
    pa_sink_input *sinp;
    pa_sink       *sink;
    pa_muxnode    *mux;

    pa_assert(u);
    pa_assert(to);
    pa_assert_se((core = u->core));

    if (!set_profile(u, to)) {
        pa_log("can't route to '%s'", to->amname);
        return FALSE;
    }

    if (to->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("can't route to '%s': no sink", to->amname);
        return FALSE;
    }

    if (!(sink = pa_idxset_get_by_index(core->sinks, to->paidx))) {
        pa_log_debug("can't route to '%s': cant find sink", to->amname);
        return FALSE;
    }

    if (!from)
        return TRUE;

    if (!set_profile(u, from)) {
        pa_log("can't route from '%s'", from->amname);
        return FALSE;
    }


    if (from->implement == mir_stream && (mux = from->mux)) {
        if (!(sinp = pa_multiplex_default_stream(core, mux))) {
            pa_log_debug("can't find default stream on mux %u",
                         mux->module_index); 
            return FALSE;
        }
    }
    else {
        if (from->paidx == PA_IDXSET_INVALID) {
            pa_log_debug("can't route '%s': no sink-input", to->amname);
            return FALSE;
        }

        if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, from->paidx))) {
            pa_log_debug("can't route '%s': cant find sink-input", to->amname);
            return FALSE;
        }
    }

    if (pa_sink_input_move_to(sinp, sink, FALSE) < 0)
        return FALSE;

    return TRUE;
}


pa_bool_t set_profile(struct userdata *u, mir_node *node)
{
    pa_core         *core;
    pa_card         *card;
    pa_card_profile *prof;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    if (node->implement != mir_device)
        return TRUE;

    if (node->type == mir_bluetooth_a2dp ||
        node->type == mir_bluetooth_sco)
    {
        card = pa_idxset_get_by_index(core->cards, node->pacard.index);

        if (!card) {
            pa_log("can't find card for '%s'", node->amname);
            return FALSE;
        }

        pa_assert_se(prof = card->active_profile);
    
        if (!pa_streq(node->pacard.profile, prof->name)) {
            pa_log_debug("changing profile '%s' => '%s'",
                         prof->name, node->pacard.profile);

            if (u->state.profile) {
                pa_log("nested profile setting is not allowed. won't change "
                       "'%s' => '%s'", prof->name, node->pacard.profile);
                return FALSE;
            }

            u->state.profile = node->pacard.profile;

            pa_card_set_profile(card, node->pacard.profile, FALSE);

            u->state.profile = NULL;            
        }
    }

    return TRUE;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
