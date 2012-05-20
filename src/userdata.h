#ifndef foouserdatafoo
#define foouserdatafoo

#include <stdbool.h>
#include <pulsecore/core.h>


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
