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


pa_bool_t mir_switch_setup_link(struct userdata *u,
                                mir_node *from,
                                mir_node *to,
                                pa_bool_t prepare_only)
{
    pa_core         *core;
    pa_card         *card;
    pa_card_profile *prof;
    pa_sink_input   *sinp;
    pa_sink         *sink;

    pa_assert(u);
    pa_assert(to);
    pa_assert_se((core = u->core));


    if (to->type == mir_bluetooth_a2dp || to->type == mir_bluetooth_sco) {
        if (!(card = pa_idxset_get_by_index(core->cards, to->pacard.index))) {
            pa_log("can't find card for '%s'", to->amname);
            return FALSE;
        }

        prof = card->active_profile;
    
        if (!pa_streq(to->pacard.profile, prof->name)) {
            pa_log_debug("changing profile '%s' => '%s'",
                         prof->name, to->pacard.profile);

            if (u->state.profile) {
                pa_log("nested profile setting is not allowed. won't change "
                       "'%s' => '%s'", prof->name, to->pacard.profile);
                return FALSE;
            }

            u->state.profile = to->pacard.profile;

            pa_card_set_profile(card, to->pacard.profile, FALSE);

            u->state.profile = NULL;            
        }
    }

    if (to->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("can't route to '%s': no sink", to->amname);
        return FALSE;
    }

    if (!(sink = pa_idxset_get_by_index(core->sinks, to->paidx))) {
        pa_log_debug("can't route to '%s': cant find sink", to->amname);
        return FALSE;
    }

    if (prepare_only)
        return TRUE;

    if (!from || from->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("can't route '%s': no sink-input", to->amname);
        return FALSE;
    }

    if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, from->paidx))) {
        pa_log_debug("can't route '%s': cant find sink-input", to->amname);
        return FALSE;
    }

    if (pa_sink_input_move_to(sinp, sink, FALSE) < 0)
        return FALSE;

    return TRUE;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
