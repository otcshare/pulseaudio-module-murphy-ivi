#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>
#include <pulsecore/card.h>

#include "tracker.h"
#include "utils.h"
#include "discover.h"
#include "router.h"
#include "node.h"


static pa_hook_result_t card_put(void *, void *, void *);
static pa_hook_result_t card_unlink(void *, void *, void *);
static pa_hook_result_t card_profile_changed(void *, void *, void *);

static pa_hook_result_t sink_put(void *, void *, void *);
static pa_hook_result_t sink_unlink(void *, void *, void *);
static pa_hook_result_t sink_port_changed(void *, void *, void *);
static pa_hook_result_t sink_port_available_changed(void *, void *, void *);

static pa_hook_result_t source_put(void *, void *, void *);
static pa_hook_result_t source_unlink(void *, void *, void *);
static pa_hook_result_t source_port_changed(void *, void *, void *);
static pa_hook_result_t source_port_available_changed(void *, void *, void *);

static pa_hook_result_t sink_input_new(void *, void *, void *);
static pa_hook_result_t sink_input_put(void *, void *, void *);
static pa_hook_result_t sink_input_unlink(void *, void *, void *);


pa_tracker *pa_tracker_init(struct userdata *u)
{
    pa_core             *core;
    pa_hook             *hooks;
    pa_tracker          *tracker;
    pa_card_hooks       *card;
    pa_sink_hooks       *sink;
    pa_source_hooks     *source;
    pa_sink_input_hooks *sinp;

    pa_assert(u);
    pa_assert_se((core = u->core));
    pa_assert_se((hooks = core->hooks));

    tracker = pa_xnew0(pa_tracker, 1);
    card   = &tracker->card;
    sink   = &tracker->sink;
    source = &tracker->source;
    sinp   = &tracker->sink_input;

    /* card */
    card->put     = pa_hook_connect(
                        hooks + PA_CORE_HOOK_CARD_PUT,
                        PA_HOOK_LATE, card_put, u
                    );
    card->unlink  = pa_hook_connect(
                        hooks + PA_CORE_HOOK_CARD_UNLINK,
                        PA_HOOK_LATE, card_unlink, u
                    );
    card->profchg = pa_hook_connect(
                        hooks + PA_CORE_HOOK_CARD_PROFILE_CHANGED,
                        PA_HOOK_LATE, card_profile_changed, u
                    );
    /* sink */
    sink->put       = pa_hook_connect(
                          hooks + PA_CORE_HOOK_SINK_PUT,
                          PA_HOOK_LATE, sink_put, u
                      );
    sink->unlink    = pa_hook_connect(
                          hooks + PA_CORE_HOOK_SINK_UNLINK,
                          PA_HOOK_LATE, sink_unlink, u
                      );
    sink->portchg   = pa_hook_connect(
                          hooks + PA_CORE_HOOK_SINK_PORT_CHANGED,
                          PA_HOOK_LATE, sink_port_changed, u
                      );
    sink->portavail = pa_hook_connect(
                          hooks + PA_CORE_HOOK_PORT_AVAILABLE_CHANGED,
                          PA_HOOK_LATE, sink_port_available_changed, u
                      );    
    /* source */
    source->put       = pa_hook_connect(
                            hooks + PA_CORE_HOOK_SOURCE_PUT,
                            PA_HOOK_LATE, source_put, u
                        );
    source->unlink    = pa_hook_connect(
                            hooks + PA_CORE_HOOK_SOURCE_UNLINK,
                            PA_HOOK_LATE, source_unlink, u
                        );
    source->portchg   = pa_hook_connect(
                            hooks + PA_CORE_HOOK_SOURCE_PORT_CHANGED,
                            PA_HOOK_LATE, source_port_changed, u
                        );
    source->portavail = pa_hook_connect(
                            hooks + PA_CORE_HOOK_PORT_AVAILABLE_CHANGED,
                            PA_HOOK_LATE, source_port_available_changed, u
                        );
    /* sink-input */
    sinp->neew   = pa_hook_connect(
                       hooks + PA_CORE_HOOK_SINK_INPUT_NEW,
                       PA_HOOK_EARLY, sink_input_new, u
                   );
    sinp->put    = pa_hook_connect(
                       hooks + PA_CORE_HOOK_SINK_INPUT_PUT,
                       PA_HOOK_LATE, sink_input_put, u
                   );
    sinp->unlink = pa_hook_connect(
                       hooks + PA_CORE_HOOK_SINK_INPUT_UNLINK,
                       PA_HOOK_LATE, sink_input_unlink, u
                   );
    
    return tracker;
}

void pa_tracker_done(struct userdata *u)
{
    pa_tracker          *tracker;
    pa_card_hooks       *card;
    pa_sink_hooks       *sink;
    pa_source_hooks     *source;
    pa_sink_input_hooks *sinp;

    if (u && (tracker = u->tracker)) {

        card = &tracker->card;
        pa_hook_slot_free(card->put);
        pa_hook_slot_free(card->unlink);
        pa_hook_slot_free(card->profchg);
        
        sink = &tracker->sink;
        pa_hook_slot_free(sink->put);
        pa_hook_slot_free(sink->unlink);
        pa_hook_slot_free(sink->portchg);
        pa_hook_slot_free(sink->portavail);
        
        source = &tracker->source;
        pa_hook_slot_free(source->put);
        pa_hook_slot_free(source->unlink);
        pa_hook_slot_free(source->portchg);
        pa_hook_slot_free(source->portavail);
        
        sinp = &tracker->sink_input;
        pa_hook_slot_free(sinp->neew);
        pa_hook_slot_free(sinp->put);
        pa_hook_slot_free(sinp->unlink);

        pa_xfree(tracker);
        
        u->tracker = NULL;
    }
}

void pa_tracker_synchronize(struct userdata *u)
{
    pa_core       *core;
    pa_card       *card;
    pa_sink       *sink;
    pa_source     *source;
    pa_sink_input *sinp;
    uint32_t       index;

    pa_assert(u);
    pa_assert_se((core = u->core));


    PA_IDXSET_FOREACH(card, core->cards, index) {
        pa_discover_add_card(u, card);
    }

    PA_IDXSET_FOREACH(sink, core->sinks, index) {
        pa_discover_add_sink(u, sink, FALSE);
    }

    PA_IDXSET_FOREACH(source, core->sources, index) {
        pa_discover_add_source(u, source);
    }

    /* Hmm... we should first collect all sink-inputs, assign
       priority to them, sort them, and call pa_discover_register_sink_input()
       in reverse priority order. Until than we may experience sound leaks
       unnecessary profile changes etc ... */

    PA_IDXSET_FOREACH(sinp, core->sink_inputs, index) {
        pa_discover_register_sink_input(u, sinp);
    }

    mir_router_make_routing(u);
}


static pa_hook_result_t card_put(void *hook_data,
                                 void *call_data,
                                 void *slot_data)
{
    pa_card *card = (pa_card *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(card);

    pa_discover_add_card(u, card);

    return PA_HOOK_OK;
}

static pa_hook_result_t card_unlink(void *hook_data,
                                    void *call_data,
                                    void *slot_data)
{
    pa_card  *card = (pa_card *)call_data;
    struct userdata *u = (struct userdata *)slot_data;
    char buf[4096];

    pa_assert(u);
    pa_assert(card);

    pa_discover_remove_card(u, card);

    mir_router_print_rtgroups(u, buf, sizeof(buf));
    pa_log_debug("%s", buf);

    mir_router_make_routing(u);

    return PA_HOOK_OK;
}


static pa_hook_result_t card_profile_changed(void *hook_data,
                                             void *call_data,
                                             void *slot_data)
{
    pa_card  *card = (pa_card *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(card);

    pa_discover_profile_changed(u, card);

    return PA_HOOK_OK;
}


static pa_hook_result_t sink_put(void *hook_data,
                                 void *call_data,
                                 void *slot_data)
{
    pa_sink *sink = (pa_sink *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(sink);

    pa_discover_add_sink(u, sink, TRUE);

    return PA_HOOK_OK;
}


static pa_hook_result_t sink_unlink(void *hook_data,
                                    void *call_data,
                                    void *slot_data)
{
    pa_sink *sink = (pa_sink *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(sink);

    pa_discover_remove_sink(u, sink);

    return PA_HOOK_OK;
}


static pa_hook_result_t sink_port_changed(void *hook_data,
                                          void *call_data,
                                          void *slot_data)
{
    pa_sink *sink = (pa_sink *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(sink);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_port_available_changed(void *hook_data,
                                                    void *call_data,
                                                    void *slot_data)
{
    pa_sink *sink = (pa_sink *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(sink);

    return PA_HOOK_OK;
}


static pa_hook_result_t source_put(void *hook_data,
                                   void *call_data,
                                   void *slot_data)
{
    pa_source *source = (pa_source *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(source);

    pa_discover_add_source(u, source);

    return PA_HOOK_OK;
}


static pa_hook_result_t source_unlink(void *hook_data,
                                      void *call_data,
                                      void *slot_data)
{
    pa_source *source = (pa_source *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(source);

    pa_discover_remove_source(u, source);

    return PA_HOOK_OK;
}


static pa_hook_result_t source_port_changed(void *hook_data,
                                            void *call_data,
                                            void *slot_data)
{
    pa_source *source = (pa_source *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(source);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_port_available_changed(void *hook_data,
                                                      void *call_data,
                                                      void *slot_data)
{
    pa_source *source = (pa_source *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(source);

    return PA_HOOK_OK;
}



static pa_hook_result_t sink_input_new(void *hook_data,
                                       void *call_data,
                                       void *slot_data)
{
    pa_sink_input_new_data *data = (pa_sink_input_new_data *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(data);

    pa_discover_preroute_sink_input(u, data);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_put(void *hook_data,
                                       void *call_data,
                                       void *slot_data)
{
    pa_sink_input *sinp = (pa_sink_input *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(sinp);

    pa_discover_add_sink_input(u, sinp);

    return PA_HOOK_OK;
}


static pa_hook_result_t sink_input_unlink(void *hook_data,
                                          void *call_data,
                                          void *slot_data)
{
    struct pa_sink_input *sinp = (pa_sink_input *)call_data;
    struct userdata *u = (struct userdata *)slot_data;

    pa_assert(u);
    pa_assert(sinp);

    pa_discover_remove_sink_input(u, sinp);

    return PA_HOOK_OK;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

