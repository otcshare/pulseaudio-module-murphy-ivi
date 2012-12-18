#ifndef foomurphyiffoo
#define foomurphyiffoo

#include "userdata.h"

#ifdef WITH_MURPHYIF
#include <murphy/domain-control/client.h>

struct pa_domctl {
    mrp_mainloop_t *ml;
    mrp_domctl_t   *ctl;
};

#else



struct pa_domctl {
    void *ml;
    void *ctl;
};

#endif

pa_domctl *pa_murphyif_init(struct userdata *u, const char *addr);
void pa_murphyif_done(struct userdata *u);

#endif /* foomurphyiffoo */
