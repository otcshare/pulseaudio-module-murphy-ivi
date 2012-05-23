#ifndef foodbusiffoo
#define foodbusiffoo

#include "userdata.h"

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


struct pa_policy_dbusif *pa_policy_dbusif_init(struct userdata *, const char *,
                                               const char *, const char *,
                                               const char *, const char *);
void pa_policy_dbusif_done(struct userdata *);

#if 0
void pa_policy_dbusif_send_device_state(struct userdata *,char *,char **,int);
void pa_policy_dbusif_send_media_status(struct userdata *, const char *,
                                        const char *, int);
#endif

/* audiomgr stuff */
pa_bool_t pa_policy_dbusif_register_domain(struct userdata *,
                                           am_domainreg_data *);
pa_bool_t pa_policy_dbusif_domain_complete(struct userdata *, uint16_t);
pa_bool_t pa_policy_dbusif_unregister_domain(struct userdata *, uint16_t);

pa_bool_t pa_policy_dbusif_register_node(struct userdata *, const char *,
                                         am_nodereg_data *);
pa_bool_t pa_policy_dbusif_unregister_node(struct userdata *, const char *,
                                           am_nodeunreg_data *);
pa_bool_t pa_policy_dbusif_acknowledge(struct userdata *, const char *,
                                       am_ack_data *);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
