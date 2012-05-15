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
/* domain status */
#define DS_UNKNOWN        0
#define DS_CONTROLLED     1
#define DS_RUNDOWN        2

/* interrupt state */
#define IS_OFF            1
#define IS_INTERRUPTED    2

/* availability status */
#define AS_AVAILABLE      1
#define AS_UNAVAILABLE    2

/* availability reason */
#define AR_NEWMEDIA       1
#define AR_SAMEMEDIA      2
#define AR_NOMEDIA        3
#define AR_TEMPERATURE    4
#define AR_VOLTAGE        5
#define AR_ERRORMEDIA     6

/* mute state */
#define MS_MUTED          1
#define MS_UNMUTED        2


typedef struct {
    const char *name;
    uint16_t    id;
    uint16_t    state;
} domain_t;


struct pa_audiomgr {
    domain_t      domain;
};


static pa_bool_t register_sink(struct userdata *);
static pa_bool_t register_source(struct userdata *);


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
        register_source(u);

        pa_policy_dbusif_domain_complete(u, id);
    }
}

void pa_audiomgr_connect(struct userdata *u, struct am_connect_data *cd)
{
    struct am_ack_data  ad;
    int err = E_OK;

    memset(&ad, 0, sizeof(ad));
    ad.handle = cd->handle;
    ad.param1 = cd->connection;
    ad.error  = err;

    pa_policy_dbusif_acknowledge(u, AUDIOMGR_CONNECT_ACK, &ad);
}

void pa_audiomgr_disconnect(struct userdata *u, struct am_connect_data *cd)
{
    struct am_ack_data  ad;
    int err = E_OK;

    memset(&ad, 0, sizeof(ad));
    ad.handle = cd->handle;
    ad.param1 = cd->connection;
    ad.error  = err;

    pa_policy_dbusif_acknowledge(u, AUDIOMGR_DISCONNECT_ACK, &ad);
}

static pa_bool_t register_sink(struct userdata *u)
{
    struct pa_audiomgr *am = u->audiomgr;
    struct am_register_data  *rd = pa_xnew0(struct am_register_data, 1);

    rd->id = 0;
    rd->name = pa_xstrdup("fakeSink");
    rd->domain = am->domain.id;
    rd->class = 0x43;
    rd->volume = 32767;
    rd->visible = TRUE;
    rd->avail.status = AS_AVAILABLE;
    rd->avail.reason = 0;
    rd->mute = MS_UNMUTED;
    rd->mainvol = 32767;

    return pa_policy_dbusif_register(u, AUDIOMGR_REGISTER_SINK, rd);
}

static pa_bool_t register_source(struct userdata *u)
{
    struct pa_audiomgr *am = u->audiomgr;
    struct am_register_data  *rd = pa_xnew0(struct am_register_data, 1);

    rd->id = 0;
    rd->name = pa_xstrdup("fakeSource");
    rd->domain = am->domain.id;
    rd->class = 0x43;
    rd->volume = 32767;
    rd->visible = TRUE;
    rd->avail.status = 1;
    rd->avail.reason = 0;
    rd->mainvol = 32767;
    rd->interrupt = IS_OFF;

    return pa_policy_dbusif_register(u, AUDIOMGR_REGISTER_SOURCE, rd);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

