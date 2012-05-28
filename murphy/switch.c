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


static pa_bool_t explicit_link_from_stream_to_device(struct userdata *,
                                                     mir_node *, mir_node *);
static pa_bool_t default_link_from_stream_to_device(struct userdata *,
                                                    mir_node *, mir_node *);


static pa_sink *setup_device_output(struct userdata *, mir_node *);

static pa_bool_t set_profile(struct userdata *, mir_node *);


pa_bool_t mir_switch_setup_link(struct userdata *u,
                                mir_node *from,
                                mir_node *to,
                                pa_bool_t explicit)
{
    pa_core *core;

    pa_assert(u);
    pa_assert(to);
    pa_assert_se((core = u->core));
    pa_assert(!from || from->direction == mir_input);
    pa_assert(to->direction == mir_output);

    if (explicit) {
        /*
         * links for explic routes
         */
        pa_assert(from);

        switch (from->implement) {

        case mir_stream:
            switch (to->implement) {

            case mir_stream:
                pa_log_debug("routing to streams is not implemented yet");
                break;

            case mir_device:
                if (!explicit_link_from_stream_to_device(u, from, to))
                    return FALSE;
                break;

            default:
                pa_log("%s: can't setup link: invalid sink node "
                       "implement", __FILE__);
                return FALSE;
            }
            break;

        case mir_device:
            pa_log_debug("input device routing is not implemented yet");
            break;

        default:
            pa_log("%s: can't setup link: invalid source node "
                   "implement", __FILE__);
            return FALSE;
        }
    }
    else {
        /*
         * links for default routes
         */
        switch (to->implement) {

        case mir_stream:
            pa_log_debug("routing to a stream is not implemented yet");
            break;

        case mir_device:
            if (!from) /* prerouting */
                return (!explicit && setup_device_output(u, to) != NULL);
            else {
                switch (from->implement) {

                case mir_stream:
                    if (!default_link_from_stream_to_device(u, from, to))
                        return FALSE;
                    break;

                case mir_device:
                    pa_log("%s: default device -> device route is "
                           "not supported", __FILE__);
                    break;

                default:
                    pa_log("%s: can't setup link: invalid source node "
                           "implement", __FILE__);
                    return FALSE;
                }
            }
            break;

        default:
            pa_log("%s: can't setup link: invalid sink node "
                   "implement", __FILE__);
            return FALSE;
        }
    }

    pa_log_debug("link %s => %s is established", from->amname, to->amname);

    return TRUE;
}

static pa_bool_t explicit_link_from_stream_to_device(struct userdata *u,
                                                     mir_node *from,
                                                     mir_node *to)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    pa_muxnode    *mux;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert((core = u->core));

    if (!(sink = setup_device_output(u, to)))
        return FALSE;

    if (!set_profile(u, from)) {
        pa_log("can't route from '%s'", from->amname);
        return FALSE;
    }

    if ((mux = from->mux)) {
        sinp = pa_idxset_get_by_index(core->sink_inputs, mux->defstream_index);

        if (sinp && sinp->sink == sink) {
            if (!pa_multiplex_remove_default_route(core, mux, TRUE))
                return FALSE;
        }
        else if (pa_multiplex_duplicate_route(core, mux, NULL, sink)) {
            pa_log_debug("multiplex route %s => %s already exists",
                         from->amname, to->amname);
            return TRUE;
        }
        else {
            if (!pa_multiplex_add_explicit_route(core, mux, sink, from->type))
                return FALSE;
        }
    }
    else {
        if ((sinp = pa_idxset_get_by_index(core->sink_inputs, from->paidx))) {
            if (sinp->sink == sink)
                pa_log_debug("direct route already exists. nothing to do");
            else {
                pa_log_debug("direct route: sink-input.%u -> sink.%u",
                             sinp->index, sink->index);

                if (pa_sink_input_move_to(sinp, sink, FALSE) < 0)
                    return FALSE;
            }
        }
    }

    pa_log_debug("link %s => %s is established", from->amname, to->amname);

    return TRUE;
}


static pa_bool_t default_link_from_stream_to_device(struct userdata *u,
                                                    mir_node *from,
                                                    mir_node *to)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    pa_muxnode    *mux;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert((core = u->core));

    if (!(sink = setup_device_output(u, to)))
        return FALSE;

    if (!set_profile(u, from)) {
        pa_log("can't route from '%s'", from->amname);
        return FALSE;
    }

    if ((mux = from->mux)) {
        sinp = pa_idxset_get_by_index(core->sink_inputs, mux->defstream_index);

        if (!sinp) {
            pa_log_debug("no default sstream found on multiplex %u",
                         mux->module_index);
            mux->defstream_index = PA_IDXSET_INVALID;
            return FALSE;
        }
        else if (pa_multiplex_duplicate_route(core, mux, sinp, sink)) {
            pa_log_debug("default stream on mux %u would be a duplicate "
                         "to an explicit route. Removing it ...",
                         mux->module_index);
            return TRUE;        /* the routing is a success */
        }
            
        pa_log_debug("multiplex route: sink-input.%d -> (sink.%d - "
                     "sink-input.%d) -> sink.%d", from->paidx,
                     mux->sink_index, sinp->index, sink->index);
    }
    else {
        if (from->paidx == PA_IDXSET_INVALID) {
            pa_log_debug("can't route '%s': no sink-input", to->amname);
            return FALSE;
        }

        if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, from->paidx))) {
            pa_log_debug("can't find sink input for '%s'", from->amname);
            return FALSE;
        }

        pa_log_debug("direct route: sink-input.%d -> sink.%d",
                     sinp->index, sink->index);
    }

    if (pa_sink_input_move_to(sinp, sink, FALSE) < 0)
        return FALSE;

    return TRUE;
}

static pa_sink *setup_device_output(struct userdata *u, mir_node *node)
{
    pa_core *core;
    pa_sink *sink;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    if (!set_profile(u, node)) {
        pa_log("can't route to '%s'", node->amname);
        return NULL;
    }

    if (node->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("can't route to '%s': no sink", node->amname);
        return NULL;
    }

    if (!(sink = pa_idxset_get_by_index(core->sinks, node->paidx))) {
        pa_log_debug("can't route to '%s': cant find sink", node->amname);
        return NULL;
    }

    return sink;
}


static pa_bool_t set_profile(struct userdata *u, mir_node *node)
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
