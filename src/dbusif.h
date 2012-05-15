#ifndef foodbusiffoo
#define foodbusiffoo

#include "userdata.h"

#define AUDIOMGR_REGISTER_DOMAIN    "registerDomain"
#define AUDIOMGR_REGISTER_SOURCE    "registerSource"
#define AUDIOMGR_REGISTER_SINK      "registerSink"
#define AUDIOMGR_REGISTER_GATEWAY   "registerGateway"

#define AUDIOMGR_DOMAIN_COMPLETE    "hookDomainRegistrationComplete"


struct pa_policy_dbusif;

struct am_register_data;
struct am_ack_data;

struct pa_policy_dbusif *pa_policy_dbusif_init(struct userdata *, const char *,
                                               const char *, const char *,
                                               const char *, const char *);
void pa_policy_dbusif_done(struct userdata *);
void pa_policy_dbusif_send_device_state(struct userdata *,char *,char **,int);
void pa_policy_dbusif_send_media_status(struct userdata *, const char *,
                                        const char *, int);

pa_bool_t pa_policy_dbusif_domain_complete(struct userdata *, uint16_t);
pa_bool_t pa_policy_dbusif_register(struct userdata *, const char *,
                                    struct am_register_data *);
pa_bool_t pa_policy_dbusif_acknowledge(struct userdata *, const char *,
                                       struct am_ack_data *);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
