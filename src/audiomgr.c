#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>

#include <pulsecore/core-util.h>

#include "userdata.h"
#include "audiomgr.h"
#include "dbusif.h"

/*
 * these must match their counterpart
 * in audiomanagertypes.h
 */
#define DOMAIN_STATE_UNKNOWN     0
#define DOMAIN_STATE_CONTROLLED  1
#define DOMAIN_STATE_RUNDOWN     2

typedef struct {
    const char *name;
    uint16_t    id;
    uint16_t    state;
} domain_t;


struct pa_audiomgr {
    domain_t      domain;
};


static pa_bool_t register_sink(struct userdata *);


struct pa_audiomgr *pa_audiomgr_init(struct userdata *u)
{
    /* pa_module       *m = u->module; */
    struct pa_audiomgr *audiomgr;
    
    audiomgr = pa_xnew0(struct pa_audiomgr, 1);

    return audiomgr;
}

void pa_audiomgr_done(struct userdata *u)
{
    struct pa_audiomgr *am;

    if (u && (am = u->audiomgr)) {
        pa_xfree((void *)am->domain.name);
        pa_xfree(am);
    }
}

void pa_audiomgr_domain_registered(struct userdata *u,
                                   const char      *name,
                                   uint16_t         id,
                                   uint16_t         state)
{
    struct pa_audiomgr *am = u->audiomgr;

    pa_assert(u);
    pa_assert(name);

    if (am) {
        am->domain.name  = pa_xstrdup(name);
        am->domain.id    = id;
        am->domain.state = state;

        register_sink(u);
    }
}

static pa_bool_t register_sink(struct userdata *u)
{
    struct pa_audiomgr *am = u->audiomgr;
    struct am_register_data  *rd = pa_xnew0(struct am_register_data, 1);

    rd->id = 0;
    rd->name = pa_xstrdup("fakeSink");
    rd->domain = am->domain.id;
    rd->class = 0x43;
    rd->volume = 32768;
    rd->visible = TRUE;
    rd->avail.status = 1;
    rd->avail.reason = 0;
    rd->mute = 2;
    rd->mainvol = 32768;

    return pa_policy_dbusif_register(u, AUDIOMGR_REGISTER_SINK, rd);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

