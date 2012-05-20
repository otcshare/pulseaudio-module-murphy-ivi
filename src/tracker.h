#ifndef footrackerfoo
#define footrackerfoo

#include "userdata.h"


typedef struct pa_card_hooks {
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
    pa_hook_slot    *profchg;
} pa_card_hooks;

typedef struct pa_sink_hooks {
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
    pa_hook_slot    *portchg;
    pa_hook_slot    *portavail;
} pa_sink_hooks;

typedef struct pa_source_hooks {
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
    pa_hook_slot    *portchg;
    pa_hook_slot    *portavail;
} pa_source_hooks;



typedef struct pa_tracker {
    pa_card_hooks    card;
    pa_sink_hooks    sink;
    pa_source_hooks  source;
} pa_tracker;

pa_tracker *pa_tracker_init(struct userdata *);
void pa_tracker_done(struct userdata *);

void pa_tracker_synchronize(struct userdata *);



#endif /* footrackerfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
