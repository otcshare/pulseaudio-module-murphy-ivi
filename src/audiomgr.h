#ifndef fooaudiomgrfoo
#define fooaudiomgrfoo

#include "userdata.h"

struct pa_audiomgr;

struct am_register_data {
    uint16_t     id;
    const char  *name;
    uint16_t     domain;
    uint16_t     class;
    uint16_t     state;      /* 1=on, 2=off */
    int16_t      volume;
    bool         visible;
    struct {
        int16_t  status;     /* 1=available, 2=unavailable */
        int16_t  reason;     /* 1=newmedia, 2=same media, 3=nomedia */
    } avail;
    uint16_t     mute;
    uint16_t     mainvol;
    uint16_t     interrupt;  /* 1=off, 2=interrupted */
};


struct pa_audiomgr *pa_audiomgr_init(struct userdata *);
void pa_audiomgr_done(struct userdata *);
void pa_audiomgr_domain_registered(struct userdata *, const char *,
                                   uint16_t, uint16_t);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
