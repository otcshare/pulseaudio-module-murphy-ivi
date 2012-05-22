#ifndef foouserdatafoo
#define foouserdatafoo

#include <stdbool.h>
#include <pulsecore/core.h>

#define PA_PROP_ROUTING_CLASS_NAME  "routing.class.name"
#define PA_PROP_ROUTING_CLASS_ID    "routing.class.id"
#define PA_PROP_ROUTING_METHOD      "routing.method"

#define PA_ROUTING_DEFAULT       "default"
#define PA_ROUTING_EXPLICIT      "explicit"

typedef struct pa_null_sink      pa_null_sink;
typedef struct pa_tracker        pa_tracker;
typedef struct pa_audiomgr       pa_audiomgr;
typedef struct pa_policy_dbusif  pa_policy_dbusif;
typedef struct pa_discover       pa_discover;
typedef struct pa_router         pa_router;
typedef struct pa_mir_config     pa_mir_config;

typedef struct {
    char *profile;    /**< During profile change it contains the new profile
                           name. Otherwise it is NULL. When sink tracking
                           hooks called the card's active_profile still
                           points to the old profile */
} pa_mir_state;


struct userdata {
    pa_core           *core;
    pa_module         *module;
    pa_null_sink      *nullsink;
    pa_audiomgr       *audiomgr;
    pa_policy_dbusif  *dbusif;
    pa_discover       *discover;
    pa_tracker        *tracker;
    pa_router         *router;
    pa_mir_config     *config;
    pa_mir_state       state;
};

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
