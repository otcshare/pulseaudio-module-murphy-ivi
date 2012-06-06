#ifndef foorouteriffoo
#define foorouteriffoo

#include "userdata.h"


enum am_method {
    audiomgr_unknown_method = 0,

    audiomgr_register_domain,
    audiomgr_domain_complete,
    audiomgr_deregister_domain,

    audiomgr_register_source,
    audiomgr_deregister_source,

    audiomgr_register_sink,
    audiomgr_deregister_sink,
    
    audiomgr_connect,
    audiomgr_connect_ack,
    
    audiomgr_disconnect,
    audiomgr_disconnect_ack,
    
    audiomgr_setsinkvol_ack,
    audiomgr_setsrcvol_ack,
    audiomgr_sinkvoltick_ack,
    audiomgr_srcvoltick_ack,
    audiomgr_setsinkprop_ack,

    audiomgr_method_dim
};


#ifdef WITH_DBUS
pa_routerif *pa_routerif_init(struct userdata *, const char *,
                              const char *, const char *,
                              const char *, const char *,
                              const char *);
#else
pa_routerif *pa_routerif_init(struct userdata *, const char *,
                              const char *, const char *);

#endif

void pa_routerif_done(struct userdata *);


pa_bool_t pa_routerif_register_domain(struct userdata *,
                                      am_domainreg_data *);
pa_bool_t pa_routerif_domain_complete(struct userdata *, uint16_t);
pa_bool_t pa_routerif_unregister_domain(struct userdata *, uint16_t);

pa_bool_t pa_routerif_register_node(struct userdata *, am_method,
                                    am_nodereg_data *);
pa_bool_t pa_routerif_unregister_node(struct userdata *, am_method,
                                      am_nodeunreg_data *);
pa_bool_t pa_routerif_acknowledge(struct userdata *, am_method, am_ack_data *);

#endif  /* foorouteriffoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
