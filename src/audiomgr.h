#ifndef fooaudiomgrfoo
#define fooaudiomgrfoo

#include "userdata.h"

struct pa_audiomgr;

struct am_register_data {
    uint16_t     id;
    const char  *name;
    uint16_t     domain;
    uint16_t     class;
    int16_t      volume;
    bool         visible;
    struct {
        int16_t  status;
        int16_t  reason;
    } avail;
    uint16_t     mute;
    uint16_t     mainvol;
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
