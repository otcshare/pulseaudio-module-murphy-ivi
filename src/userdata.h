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

struct userdata {
    pa_core           *core;
    pa_module         *module;
    pa_null_sink      *nullsink;
    pa_audiomgr       *audiomgr;
    pa_policy_dbusif  *dbusif;
    pa_discover       *discover;
    pa_tracker        *tracker;
};

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
