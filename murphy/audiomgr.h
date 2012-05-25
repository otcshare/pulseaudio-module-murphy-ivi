#ifndef fooaudiomgrfoo
#define fooaudiomgrfoo

#include <dbus/dbus.h>

#include "userdata.h"


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



struct am_domainreg_data {
    dbus_uint16_t  domain_id;
    const char    *name;      /**< domain name in audio manager  */
    const char    *bus_name;  /**< audio manager's internal bus name
                                   (not to confuse this with D-Bus name)  */
    const char    *node_name; /**< node name on audio manager's internal bus*/
    dbus_bool_t    early;
    dbus_bool_t    complete;
    dbus_uint16_t  state;
};

struct am_nodereg_data {
    const char    *key;        /* for node lookup's */
    dbus_uint16_t  id;
    const char    *name;
    dbus_uint16_t  domain;
    dbus_uint16_t  class;
    dbus_uint16_t  state;      /* 1=on, 2=off */
    dbus_int16_t   volume;
    dbus_bool_t    visible;
    struct {
        dbus_int16_t  status; /* 1=available, 2=unavailable */
        dbus_int16_t  reason; /* 1=newmedia, 2=same media, 3=nomedia */
    } avail;
    dbus_uint16_t  mute;
    dbus_uint16_t  mainvol;
    dbus_uint16_t  interrupt;  /* 1=off, 2=interrupted */
};

struct am_nodeunreg_data {
    dbus_uint16_t  id;
    const char    *name;
};


struct am_connect_data {
    dbus_uint16_t  handle;
    dbus_uint16_t  connection;
    dbus_uint16_t  source;
    dbus_uint16_t  sink;
    dbus_int16_t   format;
};

struct am_ack_data {
    dbus_uint32_t  handle;
    dbus_uint16_t  param1;
    dbus_uint16_t  param2;
    dbus_uint16_t  error;
};


pa_audiomgr *pa_audiomgr_init(struct userdata *);
void pa_audiomgr_done(struct userdata *);

void pa_audiomgr_register_domain(struct userdata *);
void pa_audiomgr_domain_registered(struct userdata *,  uint16_t, uint16_t,
                                   am_domainreg_data *);

void pa_audiomgr_unregister_domain(struct userdata *, pa_bool_t);


void pa_audiomgr_register_node(struct userdata *, mir_node *);
void pa_audiomgr_node_registered(struct userdata *, uint16_t, uint16_t,
                                 am_nodereg_data *);

void pa_audiomgr_unregister_node(struct userdata *, mir_node *);
void pa_audiomgr_node_unregistered(struct userdata *, am_nodeunreg_data *);

void pa_audiomgr_connect(struct userdata *, am_connect_data *);
void pa_audiomgr_disconnect(struct userdata *, am_connect_data *);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
