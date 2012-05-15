#ifndef fooaudiomgrfoo
#define fooaudiomgrfoo

#include "userdata.h"

#define AUDIOMGR_CONNECT            "asyncConnect"
#define AUDIOMGR_DISCONNECT         "asyncDisconnect"

#define AUDIOMGR_CONNECT_ACK        "ackConnect"
#define AUDIOMGR_DISCONNECT_ACK     "ackDisconnect"
#define AUDIOMGR_SETSINKVOL_ACK     "ackSetSinkVolume"
#define AUDIOMGR_SETSRCVOL_ACK      "ackSetSourceVolume"
#define AUDIOMGR_SINKVOLTICK_ACK    "ackSinkVolumeTick"
#define AUDIOMGR_SRCVOLTICK_ACK     "ackSourceVolumeTick"
#define AUDIOMGR_SETSINKPROP_ACK    "ackSetSinkSoundProperty"

/* error codes */
#define E_OK              0
#define E_UNKNOWN         1
#define E_OUT_OF_RANGE    2
#define E_NOT_USED        3
#define E_DATABSE_ERROR   4
#define E_ALREADY_EXISTS  5
#define E_NO_CHANGE       6
#define E_NOT_POSSIBLE    7
#define E_NON_EXISTENT    8
#define E_ABORTED         9
#define E_WRONG_FORMAT    10


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

struct am_connect_data {
    uint16_t     handle;
    uint16_t     connection;
    uint16_t     source;
    uint16_t     sink;
    int16_t      format;
};

struct am_ack_data {
    uint32_t      handle;
    uint16_t      param1;
    uint16_t      param2;
    uint16_t      error;
};


struct pa_audiomgr *pa_audiomgr_init(struct userdata *);
void pa_audiomgr_done(struct userdata *);
void pa_audiomgr_domain_registered(struct userdata *, const char *,
                                   uint16_t, uint16_t);
void pa_audiomgr_connect(struct userdata *, struct am_connect_data *);
void pa_audiomgr_disconnect(struct userdata *, struct am_connect_data *);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
