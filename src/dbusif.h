#ifndef foodbusiffoo
#define foodbusiffoo

#include "userdata.h"

struct pa_policy_dbusif;

struct pa_policy_dbusif *pa_policy_dbusif_init(struct userdata *, const char *,
                                               const char *, const char *,
                                               const char *);
void pa_policy_dbusif_done(struct userdata *);
void pa_policy_dbusif_send_device_state(struct userdata *,char *,char **,int);
void pa_policy_dbusif_send_media_status(struct userdata *, const char *,
                                        const char *, int);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
