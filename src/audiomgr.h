#ifndef fooaudiomgrfoo
#define fooaudiomgrfoo

#include "userdata.h"

struct pa_policy_audiomgr;

struct pa_policy_audiomgr *pa_policy_audiomgr_init(struct userdata *);
void pa_policy_audiomgr_done(struct userdata *);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
