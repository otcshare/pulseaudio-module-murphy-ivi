#ifndef foosinkinputextfoo
#define foosinkinputextfoo

#include <stdint.h>
#include <sys/types.h>

#include <pulse/volume.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/sink.h>
#include <pulsecore/core-subscribe.h>


#include "userdata.h"

struct pa_sinp_evsubscr {
    pa_hook_slot    *neew;
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
};

struct pa_sink_input_ext {
    struct {
        int route;
        int mute;
    }                local;     /* local policies */
};

struct pa_sinp_evsubscr *pa_sink_input_ext_subscription(struct userdata *);
void  pa_sink_input_ext_subscription_free(struct pa_sinp_evsubscr *);
void  pa_sink_input_ext_discover(struct userdata *);
struct pa_sink_input_ext *pa_sink_input_ext_lookup(struct userdata *,
                                                   struct pa_sink_input *);
int   pa_sink_input_ext_set_policy_group(struct pa_sink_input *, char *);
char *pa_sink_input_ext_get_policy_group(struct pa_sink_input *);
char *pa_sink_input_ext_get_name(struct pa_sink_input *);
int   pa_sink_input_ext_set_volume_limit(struct pa_sink_input *, pa_volume_t);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
