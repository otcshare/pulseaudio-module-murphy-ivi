#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pulsecore/pulsecore-config.h>
#include <pulsecore/dbus-shared.h>

#include "userdata.h"
#include "dbusif.h"
#include "classify.h"
#include "context.h"
#include "policy-group.h"
#include "sink-ext.h"
#include "source-ext.h"
#include "card-ext.h"

#define ADMIN_DBUS_MANAGER          "org.freedesktop.DBus"
#define ADMIN_DBUS_PATH             "/org/freedesktop/DBus"
#define ADMIN_DBUS_INTERFACE        "org.freedesktop.DBus"

#define ADMIN_NAME_OWNER_CHANGED    "NameOwnerChanged"

#define POLICY_DBUS_INTERFACE       "com.nokia.policy"
#define POLICY_DBUS_MYPATH          "/com/nokia/policy/enforce/pulseaudio"
#define POLICY_DBUS_MYNAME          "com.nokia.policy.pulseaudio"
#define POLICY_DBUS_PDPATH          "/com/nokia/policy"
#define POLICY_DBUS_PDNAME          "org.freedesktop.ohm"

#define POLICY_DECISION             "decision"
#define POLICY_STREAM_INFO          "stream_info"
#define POLICY_ACTIONS              "audio_actions"
#define POLICY_STATUS               "status"

#define PROP_ROUTE_SINK_TARGET      "policy.sink_route.target"
#define PROP_ROUTE_SINK_MODE        "policy.sink_route.mode"
#define PROP_ROUTE_SINK_HWID        "policy.sink_route.hwid"
#define PROP_ROUTE_SOURCE_TARGET    "policy.source_route.target"
#define PROP_ROUTE_SOURCE_MODE      "policy.source_route.mode"
#define PROP_ROUTE_SOURCE_HWID      "policy.source_route.hwid"


#define STRUCT_OFFSET(s,m) ((char *)&(((s *)0)->m) - (char *)0)

struct pa_policy_dbusif {
    pa_dbus_connection *conn;
    char               *ifnam;   /* signal interface */
    char               *mypath;  /* my signal path */
    char               *pdpath;  /* policy daemon's signal path */
    char               *pdnam;   /* policy daemon's D-Bus name */
    char               *admrule; /* match rule to catch name changes */
    char               *actrule; /* match rule to catch action signals */
    char               *strrule; /* match rule to catch stream info signals */
    int                 regist;  /* wheter or not registered to policy daemon*/
};

struct actdsc {                 /* action descriptor */
    const char         *name;
    int               (*parser)(struct userdata *u, DBusMessageIter *iter);
};

struct argdsc {                 /* argument descriptor for actions */
    const char         *name;
    int                 offs;
    int                 type;
};

struct argrt {                  /* audio_route arguments */
    char               *type;
    char               *device;
    char               *mode;
    char               *hwid;
};

struct argvol {                 /* volume_limit arguments */
    char               *group;
    int32_t             limit;
};

struct argcork {                /* audio_cork arguments */
    char               *group;
    char               *cork;
};

struct argmute {
    char               *device;
    char               *mute;
};

struct argctx {                 /* context arguments */
    char               *variable;
    char               *value;
};

static int action_parser(DBusMessageIter *, struct argdsc *, void *, int);
static int audio_route_parser(struct userdata *, DBusMessageIter *);
static int volume_limit_parser(struct userdata *, DBusMessageIter *);
static int audio_cork_parser(struct userdata *, DBusMessageIter *);
static int audio_mute_parser(struct userdata *, DBusMessageIter *);
static int context_parser(struct userdata *, DBusMessageIter *);

static DBusHandlerResult filter(DBusConnection *, DBusMessage *, void *);
static void handle_admin_message(struct userdata *, DBusMessage *);
static void handle_info_message(struct userdata *, DBusMessage *);
static void handle_action_message(struct userdata *, DBusMessage *);
static void registration_cb(DBusPendingCall *, void *);
static int  register_to_pdp(struct pa_policy_dbusif *, struct userdata *);
static int  signal_status(struct userdata *, uint32_t, uint32_t);
static void pa_policy_free_dbusif(struct pa_policy_dbusif *,struct userdata *);



struct pa_policy_dbusif *pa_policy_dbusif_init(struct userdata *u,
                                               const char      *ifnam,
                                               const char      *mypath,
                                               const char      *pdpath,
                                               const char      *pdnam)
{
    pa_module               *m = u->module;
    struct pa_policy_dbusif *dbusif = NULL;
    DBusConnection          *dbusconn;
    DBusError                error;
    char                     actrule[512];
    char                     strrule[512];
    char                     admrule[512];
    
    dbusif = pa_xnew0(struct pa_policy_dbusif, 1);

    dbus_error_init(&error);
    dbusif->conn = pa_dbus_bus_get(m->core, DBUS_BUS_SYSTEM, &error);

    if (dbusif->conn == NULL || dbus_error_is_set(&error)) {
        pa_log("%s: failed to get SYSTEM Bus: %s: %s",
               __FILE__, error.name, error.message);
        goto fail;
    }

    dbusconn = pa_dbus_connection_get(dbusif->conn);

#if 0
    flags  = DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE;
    result = dbus_bus_request_name(dbusconn, POLICY_DBUS_MYNAME, flags,&error);

    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER &&
        result != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER    ) {
        pa_log("%s: D-Bus name request failed: %s: %s",
               __FILE__, error.name, error.message);
        goto fail;
    }
#endif
 
    if (!dbus_connection_add_filter(dbusconn, filter,u, NULL)) {
        pa_log("%s: failed to add filter function", __FILE__);
        goto fail;
    }

    if (!ifnam)
        ifnam = POLICY_DBUS_INTERFACE;

    if (!mypath)
        mypath = POLICY_DBUS_MYPATH;

    if (!pdpath)
        pdpath = POLICY_DBUS_PDPATH;

    if (!pdnam)
        pdnam = POLICY_DBUS_PDNAME;

    snprintf(admrule, sizeof(admrule), "type='signal',sender='%s',path='%s',"
             "interface='%s',member='%s',arg0='%s'", ADMIN_DBUS_MANAGER,
             ADMIN_DBUS_PATH, ADMIN_DBUS_INTERFACE, ADMIN_NAME_OWNER_CHANGED,
             pdnam);
    dbus_bus_add_match(dbusconn, admrule, &error);

    if (dbus_error_is_set(&error)) {
        pa_log("%s: unable to subscribe name change signals on %s: %s: %s",
               __FILE__, ADMIN_DBUS_INTERFACE, error.name, error.message);
        goto fail;
    }

    snprintf(actrule, sizeof(actrule), "type='signal',interface='%s',"
             "member='%s',path='%s/%s'", ifnam, POLICY_ACTIONS,
             pdpath, POLICY_DECISION);
    dbus_bus_add_match(dbusconn, actrule, &error);

    if (dbus_error_is_set(&error)) {
        pa_log("%s: unable to subscribe policy %s signal on %s: %s: %s",
               __FILE__, POLICY_ACTIONS, ifnam, error.name, error.message);
        goto fail;
    }

    snprintf(strrule, sizeof(strrule), "type='signal',interface='%s',"
             "member='%s',path='%s/%s'", ifnam, POLICY_STREAM_INFO,
             pdpath, POLICY_DECISION);
    dbus_bus_add_match(dbusconn, strrule, &error);

    if (dbus_error_is_set(&error)) {
        pa_log("%s: unable to subscribe policy %s signal on %s: %s: %s",
               __FILE__, POLICY_STREAM_INFO, ifnam, error.name, error.message);
        goto fail;
    }

    pa_log_info("%s: subscribed policy signals on %s", __FILE__, ifnam);

    dbusif->ifnam   = pa_xstrdup(ifnam);
    dbusif->mypath  = pa_xstrdup(mypath);
    dbusif->pdpath  = pa_xstrdup(pdpath);
    dbusif->pdnam   = pa_xstrdup(pdnam);
    dbusif->admrule = pa_xstrdup(admrule);
    dbusif->actrule = pa_xstrdup(actrule);
    dbusif->strrule = pa_xstrdup(strrule);

    register_to_pdp(dbusif, u);

    return dbusif;

 fail:
    pa_policy_free_dbusif(dbusif, u);
    dbus_error_free(&error);
    return NULL;
}

static void pa_policy_free_dbusif(struct pa_policy_dbusif *dbusif,
                                  struct userdata *u)
{
    DBusConnection          *dbusconn;

    if (dbusif) {

        if (dbusif->conn) {
            dbusconn = pa_dbus_connection_get(dbusif->conn);

            if (u) {
                dbus_connection_remove_filter(dbusconn, filter,u);
            }

            dbus_bus_remove_match(dbusconn, dbusif->admrule, NULL);
            dbus_bus_remove_match(dbusconn, dbusif->actrule, NULL);
            dbus_bus_remove_match(dbusconn, dbusif->strrule, NULL);

            pa_dbus_connection_unref(dbusif->conn);
        }

        pa_xfree(dbusif->ifnam);
        pa_xfree(dbusif->mypath);
        pa_xfree(dbusif->pdpath);
        pa_xfree(dbusif->pdnam);
        pa_xfree(dbusif->admrule);
        pa_xfree(dbusif->actrule);
        pa_xfree(dbusif->strrule);

        pa_xfree(dbusif);
    }
}

void pa_policy_dbusif_done(struct userdata *u)
{
    if (u) {
        pa_policy_free_dbusif(u->dbusif, u);
    }
}

void pa_policy_dbusif_send_device_state(struct userdata *u, char *state,
                                        char **types, int ntype)
{
    static char             *path = (char *)"/com/nokia/policy/info";

    struct pa_policy_dbusif *dbusif = u->dbusif;
    DBusConnection          *conn   = pa_dbus_connection_get(dbusif->conn);
    DBusMessage             *msg;
    DBusMessageIter          mit;
    DBusMessageIter          dit;
    int                      i;
    int                      sts;

    if (!types || ntype < 1)
        return;

    msg = dbus_message_new_signal(path, dbusif->ifnam, "info");

    if (msg == NULL) {
        pa_log("%s: failed to make new info message", __FILE__);
        goto fail;
    }

    dbus_message_iter_init_append(msg, &mit);

    if (!dbus_message_iter_append_basic(&mit, DBUS_TYPE_STRING, &state) ||
        !dbus_message_iter_open_container(&mit, DBUS_TYPE_ARRAY,"s", &dit)){
        pa_log("%s: failed to build info message", __FILE__);
        goto fail;
    }

    for (i = 0; i < ntype; i++) {
        if (!dbus_message_iter_append_basic(&dit, DBUS_TYPE_STRING,&types[i])){
            pa_log("%s: failed to build info message", __FILE__);
            goto fail;
        }
    }

    dbus_message_iter_close_container(&mit, &dit);

    sts = dbus_connection_send(conn, msg, NULL);

    if (!sts) {
        pa_log("%s: Can't send info message: out of memory", __FILE__);
    }

 fail:
    dbus_message_unref(msg);    /* should cope with NULL msg */
}

void pa_policy_dbusif_send_media_status(struct userdata *u, const char *media,
                                        const char *group, int active)
{
    static char             *path = (char *)"/com/nokia/policy/info";
    static const char       *type = "media";

    struct pa_policy_dbusif *dbusif = u->dbusif;
    DBusConnection          *conn   = pa_dbus_connection_get(dbusif->conn);
    DBusMessage             *msg;
    const char              *state;
    int                      success;

    msg = dbus_message_new_signal(path, dbusif->ifnam, "info");

    if (msg == NULL)
        pa_log("%s: failed to make new info message", __FILE__);
    else {
        state = active ? "active" : "inactive";

        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_STRING, &type,
                                           DBUS_TYPE_STRING, &media,
                                           DBUS_TYPE_STRING, &group,
                                           DBUS_TYPE_STRING, &state,
                                           DBUS_TYPE_INVALID);
        
        if (!success)
            pa_log("%s: Can't build D-Bus info message", __FILE__);
        else {
            if (!dbus_connection_send(conn, msg, NULL)) {
                pa_log("%s: Can't send info message: out of memory", __FILE__);
            }
        }

        dbus_message_unref(msg);
    }
}

static DBusHandlerResult filter(DBusConnection *conn, DBusMessage *msg,
                                void *arg)
{
    struct userdata  *u = arg;

    if (dbus_message_is_signal(msg, ADMIN_DBUS_INTERFACE,
                               ADMIN_NAME_OWNER_CHANGED))
    {
        handle_admin_message(u, msg);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }


    if (dbus_message_is_signal(msg, POLICY_DBUS_INTERFACE,POLICY_STREAM_INFO)){
        handle_info_message(u, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_signal(msg, POLICY_DBUS_INTERFACE, POLICY_ACTIONS)) {
        handle_action_message(u, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void handle_admin_message(struct userdata *u, DBusMessage *msg)
{
    struct pa_policy_dbusif *dbusif;
    char                    *name;
    char                    *before;
    char                    *after;
    int                      success;

    pa_assert(u);
    pa_assert_se((dbusif = u->dbusif));

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_STRING, &name,
                                    DBUS_TYPE_STRING, &before,
                                    DBUS_TYPE_STRING, &after,
                                    DBUS_TYPE_INVALID);

    if (!success || !name) {
        pa_log("Received malformed '%s' message", ADMIN_NAME_OWNER_CHANGED);
        return;
    }

    if (strcmp(name, dbusif->pdnam)) {
        return;
    }

    if (after && strcmp(after, "")) {
        pa_log_debug("policy decision point is up");

        if (!dbusif->regist) {
            register_to_pdp(dbusif, u);
        }
    }


    if (name && before && (!after || !strcmp(after, ""))) {
        pa_log_info("policy decision point is gone");
        dbusif->regist = 0;
    } 
}

static void handle_info_message(struct userdata *u, DBusMessage *msg)
{
    dbus_uint32_t  txid;
    dbus_uint32_t  pid;
    char          *oper;
    char          *group;
    char          *arg;
    char          *method_str;
    enum pa_classify_method method;
    char          *prop;
    int            success;

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_UINT32, &txid,
                                    DBUS_TYPE_STRING, &oper,
                                    DBUS_TYPE_STRING, &group,
                                    DBUS_TYPE_UINT32, &pid,
                                    DBUS_TYPE_STRING, &arg,
                                    DBUS_TYPE_STRING, &method_str,
                                    DBUS_TYPE_STRING, &prop,
                                    DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: failed to parse info message", __FILE__);
        return;
    }

    if (!method_str)
        method = pa_method_unknown;
    else {
        switch (method_str[0]) {
        case 'e':
            if (!strcmp(method_str, "equals"))
                method = pa_method_equals;
            break;
        case 's':
            if (!strcmp(method_str, "startswith"))
                method = pa_method_startswith;
            break;
        case 'm':
            if (!strcmp(method_str, "matches"))
                method = pa_method_matches;
            break;
        case 't':
            if (!strcmp(method_str, "true"))
                method = pa_method_true;
            break;
        default:
            method = pa_method_unknown;
            break;
        }
    }

    if (!arg)
        method = pa_method_unknown;
    else if (!strcmp(arg, "*"))
        method = pa_method_true;

    if (!strcmp(oper, "register")) {

        if (pa_policy_group_find(u, group) == NULL) {
            pa_log_debug("register client (%s|%u) failed: unknown group",
                         group, pid);
        }
        else {
            pa_log_debug("register client (%s|%u)", group, pid);
            pa_classify_register_pid(u, (pid_t)pid, prop, method, arg, group);
        }
        
    }
    else if (!strcmp(oper, "unregister")) {
        pa_log_debug("unregister client (%s|%u)", group, pid);
        pa_classify_unregister_pid(u, (pid_t)pid, prop, method, arg);
    }
    else {
        pa_log("%s: invalid operation: '%s'", __FILE__, oper);
    }
}

static void handle_action_message(struct userdata *u, DBusMessage *msg)
{
    static struct actdsc actions[] = {
        { "com.nokia.policy.audio_route" , audio_route_parser  },
        { "com.nokia.policy.volume_limit", volume_limit_parser },
        { "com.nokia.policy.audio_cork"  , audio_cork_parser   },
        { "com.nokia.policy.audio_mute"  , audio_mute_parser   },
        { "com.nokia.policy.context"     , context_parser      },
        {               NULL             , NULL                }
    };

    struct actdsc   *act;
    dbus_uint32_t    txid;
    char            *actname;
    DBusMessageIter  msgit;
    DBusMessageIter  arrit;
    DBusMessageIter  entit;
    DBusMessageIter  actit;
    int              success = TRUE;

    pa_log_debug("got policy actions");

    dbus_message_iter_init(msg, &msgit);

    if (dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_UINT32)
        return;

    dbus_message_iter_get_basic(&msgit, (void *)&txid);

    pa_log_debug("got actions (txid:%d)", txid);

    if (!dbus_message_iter_next(&msgit) ||
        dbus_message_iter_get_arg_type(&msgit) != DBUS_TYPE_ARRAY) {
        success = FALSE;
        goto send_signal;
    }

    dbus_message_iter_recurse(&msgit, &arrit);

    do {
        if (dbus_message_iter_get_arg_type(&arrit) != DBUS_TYPE_DICT_ENTRY) {
            success = FALSE;
            continue;
        }

        dbus_message_iter_recurse(&arrit, &entit);

        do {
            if (dbus_message_iter_get_arg_type(&entit) != DBUS_TYPE_STRING) {
                success = FALSE;
                continue;
            }
            
            dbus_message_iter_get_basic(&entit, (void *)&actname);
            
            if (!dbus_message_iter_next(&entit) ||
                dbus_message_iter_get_arg_type(&entit) != DBUS_TYPE_ARRAY) {
                success = FALSE;
                continue;
            }
            
            dbus_message_iter_recurse(&entit, &actit);
            
            if (dbus_message_iter_get_arg_type(&actit) != DBUS_TYPE_ARRAY) {
                success = FALSE;
                continue;
            }
            
            for (act = actions;   act->name != NULL;   act++) {
                if (!strcmp(actname, act->name))
                    break;
            }
                                    
            if (act->parser != NULL)
                success &= act->parser(u, &actit);

        } while (dbus_message_iter_next(&entit));

    } while (dbus_message_iter_next(&arrit));

 send_signal:
    signal_status(u, txid, success);
}

static int action_parser(DBusMessageIter *actit, struct argdsc *descs,
                         void *args, int len)
{
    DBusMessageIter  cmdit;
    DBusMessageIter  argit;
    DBusMessageIter  valit;
    struct argdsc   *desc;
    char            *argname;
    void            *argval;
    
    dbus_message_iter_recurse(actit, &cmdit);

    memset(args, 0, len);

    do {
        if (dbus_message_iter_get_arg_type(&cmdit) != DBUS_TYPE_STRUCT)
            return FALSE;

        dbus_message_iter_recurse(&cmdit, &argit);

        if (dbus_message_iter_get_arg_type(&argit) != DBUS_TYPE_STRING)
            return FALSE;

        dbus_message_iter_get_basic(&argit, (void *)&argname);
            
        if (!dbus_message_iter_next(&argit))
            return FALSE;

        if (dbus_message_iter_get_arg_type(&argit) != DBUS_TYPE_VARIANT)
            return FALSE;

        dbus_message_iter_recurse(&argit, &valit);
                    
        for (desc = descs;  desc->name != NULL;  desc++) {
            if (!strcmp(argname, desc->name)) {
                if (desc->offs + (int)sizeof(char *) > len) {
                    pa_log("%s: %s() desc offset %d  is out of range %d",
                           __FILE__, __FUNCTION__, desc->offs, len);
                    return FALSE;
                }
                else {
                    if (dbus_message_iter_get_arg_type(&valit) != desc->type)
                        return FALSE;

                    argval = (char *)args + desc->offs;

                    dbus_message_iter_get_basic(&valit, argval);
                }
                break;
            }
        }

    } while (dbus_message_iter_next(&cmdit));

    return TRUE;
}

static int audio_route_parser(struct userdata *u, DBusMessageIter *actit)
{
    static struct argdsc descs[] = {
        {"type"  , STRUCT_OFFSET(struct argrt, type)  , DBUS_TYPE_STRING },
        {"device", STRUCT_OFFSET(struct argrt, device), DBUS_TYPE_STRING },
        {"mode"  , STRUCT_OFFSET(struct argrt, mode),   DBUS_TYPE_STRING },
        {"hwid"  , STRUCT_OFFSET(struct argrt, hwid),   DBUS_TYPE_STRING },
        {  NULL  ,            0                       , DBUS_TYPE_INVALID}
    };

    struct argrt args;
    enum pa_policy_route_class class;
    char *target;
    char *mode;
    char *hwid;
    pa_proplist *p = NULL;

    do {
        if (!action_parser(actit, descs, &args, sizeof(args)))
            return FALSE;

        if (args.type == NULL || args.device == NULL)
            return FALSE;

        if (!strcmp(args.type, "sink"))
            class = pa_policy_route_to_sink;
        else if (!strcmp(args.type, "source"))
            class = pa_policy_route_to_source;
        else
            return FALSE;

        target = args.device;
        mode   = (args.mode && strcmp(args.mode, "na")) ? args.mode : ""; 
        hwid   = (args.hwid && strcmp(args.hwid, "na")) ? args.hwid : "";

        pa_log_debug("route %s to %s (%s|%s)", args.type, target, mode, hwid);

        p = pa_proplist_new();

        if (class == pa_policy_route_to_sink) {
            pa_proplist_sets(p, PROP_ROUTE_SINK_TARGET, target);
            pa_proplist_sets(p, PROP_ROUTE_SINK_MODE, mode);
            pa_proplist_sets(p, PROP_ROUTE_SINK_HWID, hwid);
        } else {
            pa_proplist_sets(p, PROP_ROUTE_SOURCE_TARGET, target);
            pa_proplist_sets(p, PROP_ROUTE_SOURCE_MODE, mode);
            pa_proplist_sets(p, PROP_ROUTE_SOURCE_HWID, hwid);
        }

        pa_module_update_proplist(u->module, PA_UPDATE_REPLACE, p);
        pa_proplist_free(p);

        if (pa_card_ext_set_profile(u, target) < 0 ||
            (class == pa_policy_route_to_sink && 
                pa_sink_ext_set_ports(u, target) < 0) ||
            (class == pa_policy_route_to_source &&
                pa_source_ext_set_ports(u, target) < 0) ||
            pa_policy_group_move_to(u, NULL, class, target, mode, hwid) < 0)
        {
            pa_log("%s: can't route to %s %s", __FILE__, args.type, target);
            return FALSE;
        }

    } while (dbus_message_iter_next(actit));

    return TRUE;
}

static int volume_limit_parser(struct userdata *u, DBusMessageIter *actit)
{
    static struct argdsc descs[] = {
        {"group", STRUCT_OFFSET(struct argvol, group), DBUS_TYPE_STRING },
        {"limit", STRUCT_OFFSET(struct argvol, limit), DBUS_TYPE_INT32  },
        {  NULL ,            0                       , DBUS_TYPE_INVALID}
    };

    struct argvol  args;
    int            success = TRUE;

    do {
        if (!action_parser(actit, descs, &args, sizeof(args))) {
            success = FALSE;
            break;
        }

        if (args.group == NULL || args.limit < 0 || args.limit > 100) {
            success = FALSE;
            break;
        }

        pa_log_debug("volume limit (%s|%d)", args.group, args.limit); 

        pa_policy_group_volume_limit(u, args.group, (uint32_t)args.limit);

    } while (dbus_message_iter_next(actit));

    pa_sink_ext_set_volumes(u);

    return success;
}

static int audio_cork_parser(struct userdata *u, DBusMessageIter *actit)
{
    static struct argdsc descs[] = {
        {"group", STRUCT_OFFSET(struct argcork, group), DBUS_TYPE_STRING },
        {"cork" , STRUCT_OFFSET(struct argcork, cork) , DBUS_TYPE_STRING },
        { NULL  ,            0                        , DBUS_TYPE_INVALID}
    };
    
    struct argcork  args;
    char           *grp;
    int             val;
    
    do {
        if (!action_parser(actit, descs, &args, sizeof(args)))
            return FALSE;

        if (args.group == NULL || args.cork == NULL)
            return FALSE;

        grp = args.group;

        if (!strcmp(args.cork, "corked"))
            val = 1;
        else if (!strcmp(args.cork, "uncorked"))
            val = 0;
        else
            return FALSE;
        
        pa_log_debug("cork stream (%s|%d)", grp, val);
        pa_policy_group_cork(u, grp, val);

    } while (dbus_message_iter_next(actit));
    
    return TRUE;
}

static int audio_mute_parser(struct userdata *u, DBusMessageIter *actit)
{
    static struct argdsc descs[] = {
        {"device", STRUCT_OFFSET(struct argmute, device), DBUS_TYPE_STRING },
        {"mute"  , STRUCT_OFFSET(struct argmute, mute)  , DBUS_TYPE_STRING },
        { NULL   ,            0                         , DBUS_TYPE_INVALID}
    };
    
    struct argmute  args;
    char           *device;
    int             val;
    
    do {
        if (!action_parser(actit, descs, &args, sizeof(args)))
            return FALSE;

        if (args.device == NULL || args.mute == NULL)
            return FALSE;

        device = args.device;

        if (!strcmp(args.mute, "muted"))
            val = 1;
        else if (!strcmp(args.mute, "unmuted"))
            val = 0;
        else
            return FALSE;
        
        pa_log_debug("mute device (%s|%d)", device, val);
        pa_source_ext_set_mute(u, device, val);

    } while (dbus_message_iter_next(actit));
    
    return TRUE;
}

static int context_parser(struct userdata *u, DBusMessageIter *actit)
{
    static struct argdsc descs[] = {
        {"variable", STRUCT_OFFSET(struct argctx,variable), DBUS_TYPE_STRING },
        {"value"   , STRUCT_OFFSET(struct argctx,value)   , DBUS_TYPE_STRING },
        {  NULL    ,            0                         , DBUS_TYPE_INVALID}
    };
    
    struct argctx   args;
    
    do {
        if (!action_parser(actit, descs, &args, sizeof(args)))
            return FALSE;

        if (args.variable == NULL || args.value == NULL)
            return FALSE;

        pa_log_debug("context (%s|%s)", args.variable, args.value);

        pa_policy_context_variable_changed(u, args.variable, args.value);

    } while (dbus_message_iter_next(actit));
    
    return TRUE;
}

static void registration_cb(DBusPendingCall *pend, void *data)
{
    struct userdata *u = (struct userdata *)data;
    DBusMessage     *reply;
    const char      *error_descr;
    int              success;

    if ((reply = dbus_pending_call_steal_reply(pend)) == NULL || u == NULL) {
        pa_log("%s: registartion setting failed: invalid argument", __FILE__);
        return;
    }

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_STRING, &error_descr,
                                        DBUS_TYPE_INVALID);

        if (!success)
            error_descr = dbus_message_get_error_name(reply);

        pa_log_info("%s: registration to policy decision point failed: %s",
                    __FILE__, error_descr);
    }
    else {
        pa_log_info("got reply to registration");

        if (u->dbusif) {
            u->dbusif->regist = 1;
        }
    }

    dbus_message_unref(reply);
}

static int register_to_pdp(struct pa_policy_dbusif *dbusif, struct userdata *u)
{
    static const char *name = "pulseaudio";

    DBusConnection  *conn   = pa_dbus_connection_get(dbusif->conn);
    DBusMessage     *msg;
    DBusPendingCall *pend;
    const char      *signals[4];
    const char     **v_ARRAY;
    int              i;
    int              success;

    pa_log_info("%s: registering to policy daemon: name='%s' path='%s' if='%s'"
                , __FILE__, dbusif->pdnam, dbusif->pdpath, dbusif->ifnam);

    msg = dbus_message_new_method_call(dbusif->pdnam, dbusif->pdpath,
                                       dbusif->ifnam, "register");

    if (msg == NULL) {
        pa_log("%s: Failed to create D-Bus message to register", __FILE__);
        success = FALSE;
        goto failed;
    }

    signals[i=0] = POLICY_ACTIONS;
    v_ARRAY = signals;

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_STRING, &name,
                                       DBUS_TYPE_ARRAY,
                                       DBUS_TYPE_STRING, &v_ARRAY, i+1,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: Failed to build D-Bus message to register", __FILE__);
        goto failed;
    }


    success = dbus_connection_send_with_reply(conn, msg, &pend, 10000);
    if (!success) {
        pa_log("%s: Failed to register", __FILE__);
        goto failed;
    }

    success = dbus_pending_call_set_notify(pend, registration_cb, u, NULL);

    if (!success) {
        pa_log("%s: Can't set notification for registartion", __FILE__);
    }

 failed:
    dbus_message_unref(msg);
    return success;
}


static int signal_status(struct userdata *u, uint32_t txid, uint32_t status)
{
    struct pa_policy_dbusif *dbusif = u->dbusif;
    DBusConnection          *conn   = pa_dbus_connection_get(dbusif->conn);
    DBusMessage             *msg;
    char                     path[256];
    int                      ret;

    if (txid == 0) {
    
        /* When transaction ID is 0, the policy manager does not expect
         * a response. */
        
        pa_log_debug("Not sending status message since transaction ID is 0");
        return 0;
    }

    snprintf(path, sizeof(path), "%s/%s", dbusif->pdpath, POLICY_DECISION);

    pa_log_debug("sending signal to: path='%s', if='%s' member='%s' "
                 "content: txid=%d status=%d", path, dbusif->ifnam,
                 POLICY_STATUS, txid, status);

    msg = dbus_message_new_signal(path, dbusif->ifnam, POLICY_STATUS);

    if (msg == NULL) {
        pa_log("%s: failed to make new status message", __FILE__);
        goto fail;
    }

    ret = dbus_message_append_args(msg,
            DBUS_TYPE_UINT32, &txid,
            DBUS_TYPE_UINT32, &status,
            DBUS_TYPE_INVALID);

    if (!ret) {
        pa_log("%s: Can't build D-Bus status message", __FILE__);
        goto fail;
    }

    ret = dbus_connection_send(conn, msg, NULL);

    if (!ret) {
        pa_log("%s: Can't send status message: out of memory", __FILE__);
        goto fail;
    }

    dbus_message_unref(msg);

    return 0;

 fail:
    dbus_message_unref(msg);    /* should cope with NULL msg */

    return -1;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

