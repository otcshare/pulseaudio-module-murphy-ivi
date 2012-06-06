#ifndef foodbusiffoo
#define foodbusiffoo

#include "routerif.h"

/*
 * audiomanager methods
 */
#define AUDIOMGR_REGISTER_DOMAIN    "registerDomain"
#define AUDIOMGR_DOMAIN_COMPLETE    "hookDomainRegistrationComplete"
#define AUDIOMGR_DEREGISTER_DOMAIN  "deregisterDomain"

#define AUDIOMGR_REGISTER_SOURCE    "registerSource"
#define AUDIOMGR_DEREGISTER_SOURCE  "deregisterSource"

#define AUDIOMGR_REGISTER_SINK      "registerSink"
#define AUDIOMGR_DEREGISTER_SINK    "deregisterSink"

#define AUDIOMGR_CONNECT            "asyncConnect"
#define AUDIOMGR_CONNECT_ACK        "ackConnect"

#define AUDIOMGR_DISCONNECT         "asyncDisconnect"
#define AUDIOMGR_DISCONNECT_ACK     "ackDisconnect"

#define AUDIOMGR_SETSINKVOL_ACK     "ackSetSinkVolume"
#define AUDIOMGR_SETSRCVOL_ACK      "ackSetSourceVolume"
#define AUDIOMGR_SINKVOLTICK_ACK    "ackSinkVolumeTick"
#define AUDIOMGR_SRCVOLTICK_ACK     "ackSourceVolumeTick"
#define AUDIOMGR_SETSINKPROP_ACK    "ackSetSinkSoundProperty"


#if 0
void pa_routerif_send_device_state(struct userdata *,char *,char **,int);
void pa_routerif_send_media_status(struct userdata *, const char *,
                                   const char *, int);
#endif

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
