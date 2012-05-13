#ifndef foouserdatafoo
#define foouserdatafoo

#include <pulsecore/core.h>

#define PA_POLICY_DEFAULT_GROUP_NAME     "othermedia"
#define PA_POLICY_CONNECTED              "1"
#define PA_POLICY_DISCONNECTED           "0"

#define PA_PROP_APPLICATION_PROCESS_ARGS "application.process.args"
#define PA_PROP_APPLICATION_PROCESS_ARG0 "application.process.arg0"
#define PA_PROP_POLICY_GROUP             "policy.group"
#define PA_PROP_POLICY_DEVTYPELIST       "policy.device.typelist"
#define PA_PROP_POLICY_CARDTYPELIST      "policy.card.typelist"
#define PA_PROP_MAEMO_AUDIO_MODE         "x-maemo.mode"
#define PA_PROP_MAEMO_ACCESSORY_HWID     "x-maemo.accessory_hwid"

struct pa_index_hash;
struct pa_client_evsubscr;
struct pa_sink_evsubscr;
struct pa_source_evsubscr;
struct pa_sinp_evsubscr;
struct pa_sout_evsubscr;
struct pa_card_evsubscr;
struct pa_module_evsubscr;
struct pa_policy_groupset;
struct pa_classify;
struct pa_policy_context;
struct pa_policy_dbusif;

struct userdata {
    pa_core                   *core;
    pa_module                 *module;
    struct pa_null_sink       *nullsink;
    struct pa_index_hash      *hsnk;     /* sink index hash */
    struct pa_index_hash      *hsi;      /* sink input index hash */
    struct pa_client_evsubscr *scl;      /* client event susbscription */
    struct pa_sink_evsubscr   *ssnk;     /* sink event subscription */
    struct pa_source_evsubscr *ssrc;     /* source event subscription */
    struct pa_sinp_evsubscr   *ssi;      /* sink input event susbscription */
    struct pa_sout_evsubscr   *sso;      /* source output event susbscription*/
    struct pa_card_evsubscr   *scrd;     /* card event subscription */
    struct pa_module_evsubscr *smod;     /* module event subscription */
    struct pa_policy_groupset *groups;   /* policy groups */
    struct pa_classify        *classify; /* rules for classification */
    struct pa_policy_context  *context;  /* for processing context variables */
    struct pa_policy_dbusif   *dbusif;
};


/*
 * Some day this should go to a better place
 */
const char *pa_policy_file_path(const char *file, char *buf, size_t len);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
