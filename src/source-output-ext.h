#ifndef foosourceoutputextfoo
#define foosourceoutputextfoo

#include <stdint.h>
#include <sys/types.h>

#include <pulse/volume.h>
#include <pulsecore/source-output.h>
#include <pulsecore/source.h>
#include <pulsecore/core-subscribe.h>


#include "userdata.h"

struct pa_sout_evsubscr {
    pa_hook_slot    *neew;
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
};

struct pa_sout_evsubscr *pa_source_output_ext_subscription(struct userdata *);
void  pa_source_output_ext_subscription_free(struct pa_sout_evsubscr *);
void  pa_source_output_ext_discover(struct userdata *);
int   pa_source_output_ext_set_policy_group(struct pa_source_output *, char *);
char *pa_source_output_ext_get_policy_group(struct pa_source_output *);
char *pa_source_output_ext_get_name(struct pa_source_output *);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
