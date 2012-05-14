#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "userdata.h"
#include "audiomgr.h"

typedef struct {
    uint16_t    id;
    uint_16_t   status;
} domain_t;


struct pa_audiomgr {
    domain_t      domain;
};



struct pa_audiomgr *pa_audiomgr_init(struct userdata *u)
{
    pa_module          *m = u->module;
    struct pa_audiomgr *audiomgr;
    
    audiomgr = pa_xnew0(struct pa_audiomgr, 1);

    return audiomgr;
}

void pa_policy_dbusif_done(struct userdata *u)
{
    if (u && u->audiomgr) {
        pa_xfree(u->audiomgr);
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

