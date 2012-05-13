#ifndef fooclientextfoo
#define fooclientextfoo

#include <stdint.h>
#include <sys/types.h>

#include <pulsecore/client.h>
#include <pulsecore/core-subscribe.h>

#include "userdata.h"

struct pa_client;

struct pa_client_evsubscr {
    pa_subscription         *events;
};

struct pa_client_evsubscr *pa_client_ext_subscription(struct userdata *);
void   pa_client_ext_subscription_free(struct pa_client_evsubscr *);
void   pa_client_ext_discover(struct userdata *);
char  *pa_client_ext_name(struct pa_client *);
char  *pa_client_ext_id(struct pa_client *);
pid_t  pa_client_ext_pid(struct pa_client *);
uid_t  pa_client_ext_uid(struct pa_client *);
char  *pa_client_ext_exe(struct pa_client *);
char  *pa_client_ext_args(struct pa_client *);
char  *pa_client_ext_arg0(struct pa_client *);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
