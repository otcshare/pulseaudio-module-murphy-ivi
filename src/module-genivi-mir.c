#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>
#include <pulsecore/module.h>
#include <pulsecore/idxset.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>

#include "module-genivi-mir-symdef.h"
#include "userdata.h"
#include "index-hash.h"
#include "config-file.h"
#include "policy-group.h"
#include "classify.h"
#include "context.h"
#include "client-ext.h"
#include "sink-ext.h"
#include "source-ext.h"
#include "sink-input-ext.h"
#include "source-output-ext.h"
#include "card-ext.h"
#include "module-ext.h"
#include "audiomgr.h"
#include "dbusif.h"
#include "discover.h"

#ifndef PA_DEFAULT_CONFIG_DIR
#define PA_DEFAULT_CONFIG_DIR "/etc/pulse"
#endif

PA_MODULE_AUTHOR("Janos Kovacs");
PA_MODULE_DESCRIPTION("Policy enforcement module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);
PA_MODULE_USAGE(
    "config_file=<policy configuration file> "
    "dbus_if_name=<policy dbus interface> "
    "dbus_murphy_path=<policy daemon's path> "
    "dbus_murphy_name=<policy daemon's name> "
    "dbus_audiomgr_path=<GenIVI audio manager's path> " 
    "dbus_audiomgr_name=<GenIVI audio manager's name> " 
    "null_sink_name=<name of the null sink> "
    "othermedia_preemption=<on|off> "
    "configdir=<configuration directory>"
);

static const char* const valid_modargs[] = {
    "config_file",
    "dbus_if_name",
    "dbus_murphy_path",
    "dbus_murphy_name",
    "dbus_audiomgr_path",
    "dbus_audiomgr_name",
    "null_sink_name",
    "othermedia_preemption",
    "configdir",
    NULL
};


int pa__init(pa_module *m) {
    struct userdata *u = NULL;
    pa_modargs      *ma = NULL;
    const char      *cfgfile;
    const char      *ifnam;
    const char      *mrppath;
    const char      *mrpnam;
    const char      *ampath;
    const char      *amnam;
    const char      *nsnam;
    const char      *preempt;
    const char      *cfgdir;
    
    pa_assert(m);
    
    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    cfgfile = pa_modargs_get_value(ma, "config_file", NULL);
    ifnam   = pa_modargs_get_value(ma, "dbus_if_name", NULL);
    mrppath = pa_modargs_get_value(ma, "dbus_murphy_path", NULL);
    mrpnam  = pa_modargs_get_value(ma, "dbus_murphy_name", NULL);
    ampath  = pa_modargs_get_value(ma, "dbus_audiomgr_path", NULL);
    amnam   = pa_modargs_get_value(ma, "dbus_audiomgr_name", NULL);
    nsnam   = pa_modargs_get_value(ma, "null_sink_name", NULL);
    preempt = pa_modargs_get_value(ma, "othermedia_preemption", NULL);
    cfgdir  = pa_modargs_get_value(ma, "configdir", NULL);

    
    u = pa_xnew0(struct userdata, 1);
    u->core     = m->core;
    u->module   = m;
    u->nullsink = pa_sink_ext_init_null_sink(nsnam);
    u->hsnk     = pa_index_hash_init(8);
    u->hsi      = pa_index_hash_init(10);
    u->scl      = pa_client_ext_subscription(u);
    u->ssnk     = pa_sink_ext_subscription(u);
    u->ssrc     = pa_source_ext_subscription(u);
    u->ssi      = pa_sink_input_ext_subscription(u);
    u->sso      = pa_source_output_ext_subscription(u);
    u->scrd     = pa_card_ext_subscription(u);
    u->smod     = pa_module_ext_subscription(u);
    u->groups   = pa_policy_groupset_new(u);
    u->classify = pa_classify_new(u);
    u->context  = pa_policy_context_new(u);
    u->audiomgr = pa_audiomgr_init(u);
    u->dbusif   = pa_policy_dbusif_init(u,ifnam, mrppath,mrpnam, ampath,amnam);
    u->discover = pa_discover_init(u);

    if (u->scl == NULL      || u->ssnk == NULL     || u->ssrc == NULL ||
        u->ssi == NULL      || u->sso == NULL      || u->scrd == NULL ||
        u->smod == NULL     || u->groups == NULL   || u->nullsink == NULL ||
        u->classify == NULL || u->context == NULL  || u->dbusif == NULL  ||
        u->audiomgr == NULL || u->discover == NULL)
        goto fail;

    pa_policy_groupset_update_default_sink(u, PA_IDXSET_INVALID);
    pa_policy_groupset_create_default_group(u, preempt);

    if (!pa_policy_parse_config_file(u, cfgfile) ||
        !pa_policy_parse_files_in_configdir(u, cfgdir))
        goto fail;

    m->userdata = u;
    
    pa_sink_ext_discover(u);
    pa_source_ext_discover(u);
    pa_client_ext_discover(u);
    pa_sink_input_ext_discover(u);
    pa_source_output_ext_discover(u);
    pa_card_ext_discover(u);
    pa_module_ext_discover(u);

    pa_modargs_free(ma);
    
    return 0;
    
 fail:
    
    if (ma)
        pa_modargs_free(ma);
    
    pa__done(m);
    
    return -1;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    
    if (!(u = m->userdata))
        return;
    
    pa_discover_done(u);
    pa_audiomgr_done(u);
    pa_policy_dbusif_done(u);

    pa_client_ext_subscription_free(u->scl);
    pa_sink_ext_subscription_free(u->ssnk);
    pa_source_ext_subscription_free(u->ssrc);
    pa_sink_input_ext_subscription_free(u->ssi);
    pa_source_output_ext_subscription_free(u->sso);
    pa_card_ext_subscription_free(u->scrd);
    pa_module_ext_subscription_free(u->smod);

    pa_policy_groupset_free(u->groups);
    pa_classify_free(u->classify);
    pa_policy_context_free(u->context);
    pa_index_hash_free(u->hsnk);
    pa_index_hash_free(u->hsi);
    pa_sink_ext_null_sink_free(u->nullsink);

    
    pa_xfree(u);
}


/*
 * For the time being the prototypes are in the userdata.h which is
 * not the best possible place for it
 */
const char *pa_policy_file_path(const char *file, char *buf, size_t len)
{
    snprintf(buf, len, "%s/x%s", PA_DEFAULT_CONFIG_DIR, file);

    return buf;
}

static uint32_t stamp;

const uint32_t pa_policy_new_stamp(void)
{
    return ++stamp;
}

const uint32_t pa_policy_get_stamp(void)
{
    return stamp;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */


