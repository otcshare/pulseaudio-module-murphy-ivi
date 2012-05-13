#ifndef foosourceextfoo
#define foosourceextfoo

#include "userdata.h"

struct pa_source;

struct pa_source_evsubscr {
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
};

struct pa_source_evsubscr *pa_source_ext_subscription(struct userdata *);
void  pa_source_ext_subscription_free(struct pa_source_evsubscr *);
void  pa_source_ext_discover(struct userdata *);
char *pa_source_ext_get_name(struct pa_source *);
int   pa_source_ext_set_mute(struct userdata *, char *, int);
int   pa_source_ext_set_ports(struct userdata *, const char *);

#endif /* foosourceextfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
