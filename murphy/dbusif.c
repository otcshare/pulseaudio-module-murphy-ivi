/*
 * module-murphy-ivi -- PulseAudio module for providing audio routing support
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pulsecore/pulsecore-config.h>
#include <pulsecore/dbus-shared.h>

#include "userdata.h"
#include "dbusif.h"
#include "audiomgr.h"

#define ADMIN_DBUS_MANAGER          "org.freedesktop.DBus"
#define ADMIN_DBUS_PATH             "/org/freedesktop/DBus"
#define ADMIN_DBUS_INTERFACE        "org.freedesktop.DBus"

#define ADMIN_NAME_OWNER_CHANGED    "NameOwnerChanged"

#define POLICY_DBUS_INTERFACE       "org.tizen.policy"
#define POLICY_DBUS_MRPPATH         "/org/tizen/policy"
#define POLICY_DBUS_MRPNAME         "org.tizen.murphy"

#define AUDIOMGR_DBUS_INTERFACE     "org.genivi.audiomanager"
#define AUDIOMGR_DBUS_PATH          "/org/genivi/audiomanager"
#define AUDIOMGR_DBUS_ROUTE_NAME    "RoutingInterface"
#define AUDIOMGR_DBUS_ROUTE_PATH    "RoutingInterface"

#define PULSE_DBUS_INTERFACE        "org.genivi.pulse"
#define PULSE_DBUS_PATH             "/org/genivi/pulse"
#define PULSE_DBUS_NAME             "org.genivi.pulse"


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

typedef void (*pending_cb_t)(struct userdata *, const char *,
                             DBusMessage *, void *);
typedef pa_bool_t (*method_t)(struct userdata *, DBusMessage *);


struct pending {
    PA_LLIST_FIELDS(struct pending);
    struct userdata  *u;
    const char       *method;
    DBusPendingCall  *call;
    pending_cb_t      cb;
    void             *data;
};

struct pa_routerif {
    pa_dbus_connection *conn;
    char               *ifnam;    /* signal interface */
    char               *mrppath;  /* murphy signal path */
    char               *mrpnam;   /* murphy D-Bus name */
    char               *ampath;   /* audio manager path */
    char               *amnam;    /* audio manager name */
    char               *amrpath;  /* audio manager routing path */
    char               *amrnam;   /* audio manager routing name */
    char               *admmrule; /* match rule to catch murphy name changes */
    char               *admarule; /* match rule to catch audiomgr name change*/
    char               *actrule;  /* match rule to catch action signals */
    char               *strrule;  /* match rule to catch stream info signals */
    int                 mregist;  /* are we registered to murphy */
    int                 amisup;   /* is the audio manager up */
    PA_LLIST_HEAD(struct pending, pendlist);
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

static void free_routerif(pa_routerif *,struct userdata *);

static pa_bool_t send_message_with_reply(struct userdata *, 
                                         DBusConnection *, DBusMessage *,
                                         pending_cb_t, void *);


static DBusHandlerResult filter(DBusConnection *, DBusMessage *, void *);

static void handle_admin_message(struct userdata *, DBusMessage *);
#if 0
static void handle_info_message(struct userdata *, DBusMessage *);
static void handle_action_message(struct userdata *, DBusMessage *);
#endif

static void murphy_registration_cb(struct userdata *, const char *,
                                   DBusMessage *, void *);
static pa_bool_t register_to_murphy(struct userdata *);
#if 0
static int  signal_status(struct userdata *, uint32_t, uint32_t);
#endif

static DBusHandlerResult audiomgr_method_handler(DBusConnection *,
                                                 DBusMessage *, void *);
static void audiomgr_register_domain_cb(struct userdata *, const char *,
                                        DBusMessage *, void *);
static pa_bool_t register_to_audiomgr(struct userdata *);
static pa_bool_t unregister_from_audiomgr(struct userdata *);

static void audiomgr_register_node_cb(struct userdata *, const char *,
                                      DBusMessage *, void *);
static void audiomgr_unregister_node_cb(struct userdata *, const char *,
                                        DBusMessage *, void *);
static pa_bool_t build_sound_properties(DBusMessageIter *,
                                        struct am_nodereg_data *);
static pa_bool_t build_connection_formats(DBusMessageIter *,
                                          struct am_nodereg_data *);
static pa_bool_t routerif_connect(struct userdata *, DBusMessage *);
static pa_bool_t routerif_disconnect(struct userdata *, DBusMessage *);

static const char *method_str(am_method);


pa_routerif *pa_routerif_init(struct userdata *u,
                              const char      *dbustype,
                              const char      *ifnam,
                              const char      *mrppath,
                              const char      *mrpnam,
                              const char      *ampath,
                              const char      *amnam)
{
    static const DBusObjectPathVTable  vtable = {
        .message_function = audiomgr_method_handler,
    };

    pa_module      *m = u->module;
    pa_routerif    *routerif = NULL;
    DBusBusType     type;
    DBusConnection *dbusconn;
    DBusError       error;
    unsigned int    flags;
    char            nambuf[128];
    char            pathbuf[128];
    char           *amrnam;
    char           *amrpath;
    char            actrule[512];
    char            strrule[512];
    char            admmrule[512];
    char            admarule[512];
    int             result;
    
    if (!dbustype || !strcasecmp(dbustype, "session")) {
        dbustype = "session";
        type = DBUS_BUS_SESSION;
    }
    else if (!strcasecmp(dbustype, "system")) {
        dbustype = "system";
        type = DBUS_BUS_SYSTEM;
    }
    else {
        pa_log("invalid dbus type '%s'", dbustype);
        return NULL;
    }
    
    routerif = pa_xnew0(pa_routerif, 1);
    PA_LLIST_HEAD_INIT(struct pending, routerif->pendlist);

    dbus_error_init(&error);
    routerif->conn = pa_dbus_bus_get(m->core, type, &error);

    if (routerif->conn == NULL || dbus_error_is_set(&error)) {
        pa_log("%s: failed to get %s Bus: %s: %s",
               __FILE__, dbustype, error.name, error.message);
        goto fail;
    }

    dbusconn = pa_dbus_connection_get(routerif->conn);

    flags  = DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE;
    result = dbus_bus_request_name(dbusconn, PULSE_DBUS_NAME, flags,&error);

    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER &&
        result != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER    ) {
        pa_log("%s: D-Bus name request failed: %s: %s",
               __FILE__, error.name, error.message);
        goto fail;
    }
    
    pa_log_info("%s: now owner of '%s' D-Bus name on %s bus",
                __FILE__, PULSE_DBUS_NAME, dbustype);
 
    if (!dbus_connection_add_filter(dbusconn, filter,u, NULL)) {
        pa_log("%s: failed to add filter function", __FILE__);
        goto fail;
    }

    if (!ifnam)
        ifnam = POLICY_DBUS_INTERFACE;

    if (!mrppath)
        mrppath = POLICY_DBUS_MRPPATH;

    if (!mrpnam)
        mrpnam = POLICY_DBUS_MRPNAME;

    if (ampath && *ampath) {
        char *slash = ampath[strlen(ampath)-1] == '/' ? "" : "/";
        snprintf(pathbuf, sizeof(pathbuf), "%s%s" AUDIOMGR_DBUS_ROUTE_PATH,
                 ampath, slash);
        amrpath = pathbuf;
    }
    else {
        ampath  = AUDIOMGR_DBUS_PATH;
        amrpath = AUDIOMGR_DBUS_PATH "/" AUDIOMGR_DBUS_ROUTE_PATH;
    }

    if (amnam && *amnam){
        char *dot = amnam[strlen(amnam)-1] == '.' ? "" : ".";
        snprintf(nambuf, sizeof(nambuf), "%s%s" AUDIOMGR_DBUS_ROUTE_NAME,
                 amnam, dot);
        amrnam = nambuf;
    }
    else {
        amnam  = AUDIOMGR_DBUS_INTERFACE;
        amrnam = AUDIOMGR_DBUS_INTERFACE "." AUDIOMGR_DBUS_ROUTE_NAME;
    }


    snprintf(admmrule, sizeof(admmrule), "type='signal',sender='%s',path='%s',"
             "interface='%s',member='%s',arg0='%s'", ADMIN_DBUS_MANAGER,
             ADMIN_DBUS_PATH, ADMIN_DBUS_INTERFACE, ADMIN_NAME_OWNER_CHANGED,
             mrpnam);
    dbus_bus_add_match(dbusconn, admmrule, &error);

    snprintf(admarule, sizeof(admarule), "type='signal',sender='%s',path='%s',"
             "interface='%s',member='%s',arg0='%s'", ADMIN_DBUS_MANAGER,
             ADMIN_DBUS_PATH, ADMIN_DBUS_INTERFACE, ADMIN_NAME_OWNER_CHANGED,
             amnam);
    dbus_bus_add_match(dbusconn, admarule, &error);

    if (dbus_error_is_set(&error)) {
        pa_log("%s: unable to subscribe name change signals on %s: %s: %s",
               __FILE__, ADMIN_DBUS_INTERFACE, error.name, error.message);
        goto fail;
    }

    snprintf(actrule, sizeof(actrule), "type='signal',interface='%s',"
             "member='%s',path='%s/%s'", ifnam, POLICY_ACTIONS,
             mrppath, POLICY_DECISION);
    dbus_bus_add_match(dbusconn, actrule, &error);

    if (dbus_error_is_set(&error)) {
        pa_log("%s: unable to subscribe policy %s signal on %s: %s: %s",
               __FILE__, POLICY_ACTIONS, ifnam, error.name, error.message);
        goto fail;
    }

    snprintf(strrule, sizeof(strrule), "type='signal',interface='%s',"
             "member='%s',path='%s/%s'", ifnam, POLICY_STREAM_INFO,
             mrppath, POLICY_DECISION);
    dbus_bus_add_match(dbusconn, strrule, &error);

    if (dbus_error_is_set(&error)) {
        pa_log("%s: unable to subscribe policy %s signal on %s: %s: %s",
               __FILE__, POLICY_STREAM_INFO, ifnam, error.name, error.message);
        goto fail;
    }

    pa_log_info("%s: subscribed policy signals on %s", __FILE__, ifnam);

    dbus_connection_register_object_path(dbusconn, PULSE_DBUS_PATH, &vtable,u);


    routerif->ifnam    = pa_xstrdup(ifnam);
    routerif->mrppath  = pa_xstrdup(mrppath);
    routerif->mrpnam   = pa_xstrdup(mrpnam);
    routerif->ampath   = pa_xstrdup(ampath);
    routerif->amnam    = pa_xstrdup(amnam);
    routerif->amrpath  = pa_xstrdup(amrpath);
    routerif->amrnam   = pa_xstrdup(amrnam);
    routerif->admmrule = pa_xstrdup(admmrule);
    routerif->admarule = pa_xstrdup(admarule);
    routerif->actrule  = pa_xstrdup(actrule);
    routerif->strrule  = pa_xstrdup(strrule);

    u->routerif = routerif; /* Argh.. */

    register_to_murphy(u);
    register_to_audiomgr(u);

    return routerif;

 fail:
    free_routerif(routerif, u);
    dbus_error_free(&error);
    return NULL;
}

static void free_routerif(pa_routerif *routerif, struct userdata *u)
{
    DBusConnection  *dbusconn;
    struct pending  *p, *n;

    if (routerif) {

        if (routerif->conn) {
            dbusconn = pa_dbus_connection_get(routerif->conn);

            PA_LLIST_FOREACH_SAFE(p,n, routerif->pendlist) {
                PA_LLIST_REMOVE(struct pending, routerif->pendlist, p);
                dbus_pending_call_set_notify(p->call, NULL,NULL, NULL);
                dbus_pending_call_unref(p->call);
            }

            if (u) {
                dbus_connection_remove_filter(dbusconn, filter,u);
            }

            dbus_bus_remove_match(dbusconn, routerif->admmrule, NULL);
            dbus_bus_remove_match(dbusconn, routerif->admarule, NULL);
            dbus_bus_remove_match(dbusconn, routerif->actrule, NULL);
            dbus_bus_remove_match(dbusconn, routerif->strrule, NULL);

            pa_dbus_connection_unref(routerif->conn);
        }

        pa_xfree(routerif->ifnam);
        pa_xfree(routerif->mrppath);
        pa_xfree(routerif->mrpnam);
        pa_xfree(routerif->ampath);
        pa_xfree(routerif->amnam);
        pa_xfree(routerif->amrpath);
        pa_xfree(routerif->amrnam);
        pa_xfree(routerif->admmrule);
        pa_xfree(routerif->admarule);
        pa_xfree(routerif->actrule);
        pa_xfree(routerif->strrule);

        pa_xfree(routerif);
    }
}

void pa_routerif_done(struct userdata *u)
{
    if (u && u->routerif) {
        free_routerif(u->routerif, u);
        u->routerif = NULL;
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


#if 0
    if (dbus_message_is_signal(msg, POLICY_DBUS_INTERFACE,POLICY_STREAM_INFO)){
        handle_info_message(u, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_signal(msg, POLICY_DBUS_INTERFACE, POLICY_ACTIONS)) {
        handle_action_message(u, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
#endif

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static void handle_admin_message(struct userdata *u, DBusMessage *msg)
{
    pa_routerif *routerif;
    char        *name;
    char        *before;
    char        *after;
    int          success;

    pa_assert(u);
    pa_assert_se((routerif = u->routerif));

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_STRING, &name,
                                    DBUS_TYPE_STRING, &before,
                                    DBUS_TYPE_STRING, &after,
                                    DBUS_TYPE_INVALID);

    if (!success || !name) {
        pa_log("Received malformed '%s' message", ADMIN_NAME_OWNER_CHANGED);
        return;
    }

    if (!strcmp(name, routerif->mrpnam)) {
        if (after && strcmp(after, "")) {
            pa_log_debug("murphy is up");

            if (!routerif->mregist) {
                register_to_murphy(u);
            }
        }

        if (name && before && (!after || !strcmp(after, ""))) {
            pa_log_info("murphy is gone");
            routerif->mregist = 0;
        } 
    } else

    if (!strcmp(name, routerif->amnam)) {
        if (after && strcmp(after, "")) {
            pa_log_debug("audio manager is up");

            if (!routerif->amisup) {
                register_to_audiomgr(u);
            }
        }

        if (name && before && (!after || !strcmp(after, ""))) {
            pa_log_info("audio manager is gone");

            if (routerif->amisup)
                unregister_from_audiomgr(u);

            routerif->amisup = 0;
        } 
    }
}


static void reply_cb(DBusPendingCall *pend, void *data)
{
    struct pending  *pdata = (struct pending *)data;
    struct userdata *u;
    pa_routerif     *routerif;
    DBusMessage     *reply;

    pa_assert(pdata);
    pa_assert(pdata->call == pend);
    pa_assert_se((u = pdata->u));
    pa_assert_se((routerif = u->routerif));

    PA_LLIST_REMOVE(struct pending, routerif->pendlist, pdata);

    if ((reply = dbus_pending_call_steal_reply(pend)) == NULL) {
        pa_log("%s: Murphy pending call '%s' failed: invalid argument",
               __FILE__, pdata->method);
    }
    else {
        pdata->cb(u, pdata->method, reply, pdata->data);
        dbus_message_unref(reply);
    }

    pa_xfree((void *)pdata->method);
    pa_xfree((void *)pdata);
}

static pa_bool_t send_message_with_reply(struct userdata *u,
                                         DBusConnection  *conn,
                                         DBusMessage     *msg,
                                         pending_cb_t     cb,
                                         void            *data)
{
    pa_routerif     *routerif;
    struct pending  *pdata = NULL;
    const char      *method;
    DBusPendingCall *pend;

    pa_assert(u);
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(cb);
    pa_assert_se((routerif = u->routerif));

    if ((method = dbus_message_get_member(msg)) == NULL)
        goto failed;

    pdata = pa_xnew0(struct pending, 1);
    pdata->u      = u;
    pdata->method = pa_xstrdup(method);
    pdata->cb     = cb;
    pdata->data   = data;

    PA_LLIST_PREPEND(struct pending, routerif->pendlist, pdata);

    if (!dbus_connection_send_with_reply(conn, msg, &pend, -1)) {
        pa_log("%s: Failed to %s", __FILE__, method);
        goto failed;
    }

    pdata->call = pend;

    if (!dbus_pending_call_set_notify(pend, reply_cb,pdata, NULL)) {
        pa_log("%s: Can't set notification for %s", __FILE__, method);
        goto failed;
    }


    return TRUE;

 failed:
    if (pdata) {
        PA_LLIST_REMOVE(struct pending, routerif->pendlist, pdata);
        pa_xfree((void *)pdata->method);
        pa_xfree((void *)pdata);
    }
    return FALSE;
}


/**************************************************************************
 *
 * Murphy interfaces
 *
 */
#if 0
void pa_routerif_send_device_state(struct userdata *u, char *state,
                                   char **types, int ntype)
{
    static char     *path = (char *)"/org/tizen/policy/info";

    pa_routerif     *routerif = u->routerif;
    DBusConnection  *conn = pa_dbus_connection_get(routerif->conn);
    DBusMessage     *msg;
    DBusMessageIter  mit;
    DBusMessageIter  dit;
    int              i;
    int              sts;

    if (!types || ntype < 1)
        return;

    msg = dbus_message_new_signal(path, routerif->ifnam, "info");

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

void pa_routerif_send_media_status(struct userdata *u, const char *media,
                                        const char *group, int active)
{
    static char        *path = (char *)"/org/tizen/policy/info";
    static const char  *type = "media";

    pa_routerif    *routerif = u->routerif;
    DBusConnection *conn   = pa_dbus_connection_get(routerif->conn);
    DBusMessage    *msg;
    const char     *state;
    int             success;

    msg = dbus_message_new_signal(path, routerif->ifnam, "info");

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
#endif

#if 0
static void handle_info_message(struct userdata *u, DBusMessage *msg)
{
    dbus_uint32_t  txid;
    dbus_uint32_t  pid;
    char          *oper;
    char          *group;
    char          *arg;
    char          *method_str;
    enum pa_classify_method method = pa_method_unknown;
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
/*
        { "org.tizen.policy.audio_route" , audio_route_parser  },
        { "org.tizen.policy.volume_limit", volume_limit_parser },
        { "org.tizen.policy.audio_cork"  , audio_cork_parser   },
        { "org.tizen.policy.audio_mute"  , audio_mute_parser   },
        { "org.tizen.policy.context"     , context_parser      },
*/
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
#endif

static void murphy_registration_cb(struct userdata *u,
                                   const char      *method,
                                   DBusMessage     *reply,
                                   void            *data)
{
    const char      *error_descr;
    int              success;

    (void)data;

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_STRING, &error_descr,
                                        DBUS_TYPE_INVALID);

        if (!success)
            error_descr = dbus_message_get_error_name(reply);

        pa_log_info("%s: registration to Murphy failed: %s",
                    __FILE__, error_descr);
    }
    else {
        pa_log_info("Murphy replied to registration");

        if (u->routerif) {
            u->routerif->amisup = 1;
        }
    }
}

static pa_bool_t register_to_murphy(struct userdata *u)
{
    static const char *name = "pulseaudio";

    pa_routerif    *routerif = u->routerif;
    DBusConnection *conn   = pa_dbus_connection_get(routerif->conn);
    DBusMessage    *msg;
    const char     *signals[4];
    const char    **v_ARRAY;
    int             i;
    int             success;

    pa_log_info("%s: registering to murphy: name='%s' path='%s' if='%s'",
                __FILE__, routerif->mrpnam, routerif->mrppath,routerif->ifnam);

    msg = dbus_message_new_method_call(routerif->mrpnam, routerif->mrppath,
                                       routerif->ifnam, "register");

    if (msg == NULL) {
        pa_log("%s: Failed to create D-Bus message to register", __FILE__);
        success = FALSE;
        goto getout;
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
        goto getout;
    }

    if (!send_message_with_reply(u, conn, msg, murphy_registration_cb, NULL)) {
        pa_log("%s: Failed to register", __FILE__);
        goto getout;
    }

 getout:
    dbus_message_unref(msg);
    return success;
}


#if 0
static int signal_status(struct userdata *u, uint32_t txid, uint32_t status)
{
    pa_routerif    *routerif = u->routerif;
    DBusConnection *conn = pa_dbus_connection_get(routerif->conn);
    DBusMessage    *msg;
    char            path[256];
    int             ret;

    if (txid == 0) {
    
        /* When transaction ID is 0, the policy manager does not expect
         * a response. */
        
        pa_log_debug("Not sending status message since transaction ID is 0");
        return 0;
    }

    snprintf(path, sizeof(path), "%s/%s", routerif->mrppath, POLICY_DECISION);

    pa_log_debug("sending signal to: path='%s', if='%s' member='%s' "
                 "content: txid=%d status=%d", path, routerif->ifnam,
                 POLICY_STATUS, txid, status);

    msg = dbus_message_new_signal(path, routerif->ifnam, POLICY_STATUS);

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
#endif


/**************************************************************************
 *
 * Audio Manager interfaces
 *
 */
static DBusHandlerResult audiomgr_method_handler(DBusConnection *conn,
                                                 DBusMessage    *msg,
                                                 void           *arg)
{
    struct dispatch {
        const char *name;
        method_t    method;
    };

    static struct dispatch dispatch_tbl[] = {
        { AUDIOMGR_CONNECT   , routerif_connect    },
        { AUDIOMGR_DISCONNECT, routerif_disconnect },
        {        NULL,                 NULL        }
    };

    struct userdata         *u = (struct userdata *)arg;
    struct dispatch         *d;
    const char              *name;
    method_t                 method;
    //uint32_t                 serial;
    dbus_int16_t             errcod;
    DBusMessage             *reply;
    pa_bool_t                success;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {

        name = dbus_message_get_member(msg);
        // serial = dbus_message_get_serial(msg);

        pa_assert(name);

        for (method = NULL, d = dispatch_tbl;  d->name;    d++) {
            if (!strcmp(name, d->name)) {
                method = d->method;
                break;
            }
        }

        errcod = method ? E_OK : E_NOT_POSSIBLE; 
        reply  = dbus_message_new_method_return(msg);

        // dbus_message_set_reply_serial(reply, serial);
                
        success = dbus_message_append_args(reply,
                                           DBUS_TYPE_INT16, &errcod,
                                           DBUS_TYPE_INVALID);
        
        if (!success || !dbus_connection_send(conn, reply, NULL))
            pa_log("%s: failed to reply '%s'", __FILE__, name);
        else
            pa_log_debug("'%s' replied (%d)", name, errcod);

        dbus_message_unref(reply);

        if (method)
            d->method(u, msg);
        else
            pa_log_info("%s: unsupported '%s' method ignored", __FILE__, name);
                
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    pa_log_debug("got some unexpected type of D-Bus message");

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static pa_bool_t register_to_audiomgr(struct userdata *u)
{
    pa_audiomgr_register_domain(u);
    return TRUE;
}

static pa_bool_t unregister_from_audiomgr(struct userdata *u)
{
    pa_audiomgr_unregister_domain(u, FALSE);
    return TRUE;
}

static void audiomgr_register_domain_cb(struct userdata *u,
                                        const char      *method,
                                        DBusMessage     *reply,
                                        void            *data)
{
    const char        *error_descr;
    dbus_uint16_t      domain_id;
    dbus_uint16_t      status;
    int                success;

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_STRING, &error_descr,
                                        DBUS_TYPE_INVALID);

        if (!success)
            error_descr = dbus_message_get_error_name(reply);

        pa_log_info("%s: AudioManager domain registration failed: %s",
                    __FILE__, error_descr);
    }
    else {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_UINT16, &domain_id,
                                        DBUS_TYPE_UINT16, &status,
                                        DBUS_TYPE_INVALID);

        if (!success) {
            pa_log("got broken message from AudioManager.Registration failed");
        }
        else {
            pa_log_info("AudioManager replied to registration: "
                        "domainID %u, status %u", domain_id, status);

            if (u->routerif) {
                u->routerif->amisup = 1;
                pa_audiomgr_domain_registered(u, domain_id, status, data);
            }
        }
    }
}



pa_bool_t pa_routerif_register_domain(struct userdata   *u,
                                           am_domainreg_data *dr)
{
    pa_routerif    *routerif;
    DBusConnection *conn;
    DBusMessage    *msg;
    const char     *dbus_name;
    const char     *dbus_path;
    const char     *dbus_if;
    int             success;

    pa_assert(u);
    pa_assert(dr);
    pa_assert_se((routerif = u->routerif));
    pa_assert_se((conn = pa_dbus_connection_get(routerif->conn)));

    pa_log_info("%s: registering to AudioManager: name='%s' path='%s' if='%s'"
                , __FILE__, routerif->amnam, routerif->amrpath, routerif->amrnam);

    msg = dbus_message_new_method_call(routerif->amnam,
                                       routerif->amrpath,
                                       routerif->amrnam,
                                       AUDIOMGR_REGISTER_DOMAIN);
    if (msg == NULL) {
        pa_log("%s: Failed to create D-Bus message to '%s'",
               __FILE__, AUDIOMGR_REGISTER_DOMAIN);
        success = FALSE;
        goto getout;
    }

    dbus_name = PULSE_DBUS_NAME;
    dbus_path = PULSE_DBUS_PATH;
    dbus_if   = PULSE_DBUS_INTERFACE;

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_UINT16,  &dr->domain_id,
                                       DBUS_TYPE_STRING,  &dr->name,
                                       DBUS_TYPE_STRING,  &dr->node_name,
                                       DBUS_TYPE_STRING,  &dr->bus_name,
                                       DBUS_TYPE_BOOLEAN, &dr->early,
                                       DBUS_TYPE_BOOLEAN, &dr->complete,
                                       DBUS_TYPE_UINT16 , &dr->state,
                                       DBUS_TYPE_STRING , &dbus_name,
                                       DBUS_TYPE_STRING , &dbus_path,
                                       DBUS_TYPE_STRING , &dbus_if,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: Failed to build D-Bus message to register", __FILE__);
        goto getout;
    }

    success = send_message_with_reply(u, conn, msg,
                                      audiomgr_register_domain_cb, dr);
    if (!success) {
        pa_log("%s: Failed to register", __FILE__);
        goto getout;
    }

 getout:
    dbus_message_unref(msg);
    return success;
}

pa_bool_t pa_routerif_domain_complete(struct userdata *u, uint16_t domain)
{
    dbus_int32_t    id32 = domain;
    pa_routerif    *routerif;
    DBusConnection *conn;
    DBusMessage    *msg;
    pa_bool_t       success;

    pa_assert(u);
    pa_assert_se((routerif = u->routerif));
    pa_assert_se((conn = pa_dbus_connection_get(routerif->conn)));
    

    pa_log_debug("%s: domain %u AudioManager %s", __FUNCTION__,
                 domain, AUDIOMGR_DOMAIN_COMPLETE);

    msg = dbus_message_new_method_call(routerif->amnam,
                                       routerif->amrpath,
                                       routerif->amrnam,
                                       AUDIOMGR_DOMAIN_COMPLETE);
    if (msg == NULL) {
        pa_log("%s: Failed to create D-Bus message for '%s'",
               __FILE__, AUDIOMGR_DOMAIN_COMPLETE);
        success = FALSE;
        goto getout;
    }

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_INT32,  &id32,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: Failed to build D-Bus message for '%s'",
               __FILE__, AUDIOMGR_DOMAIN_COMPLETE);
        goto getout;
    }

    if (!dbus_connection_send(conn, msg, NULL)) {
        pa_log("%s: Failed to send '%s'", __FILE__, AUDIOMGR_DOMAIN_COMPLETE);
        goto getout;
    }

    dbus_connection_flush(conn);

 getout:
    dbus_message_unref(msg);
    return success;
}

pa_bool_t pa_routerif_unregister_domain(struct userdata *u, uint16_t domain)
{
    pa_routerif    *routerif;
    DBusConnection *conn;
    DBusMessage    *msg;
    pa_bool_t       success;

    pa_assert(u);
    pa_assert_se((routerif = u->routerif));
    pa_assert_se((conn = pa_dbus_connection_get(routerif->conn)));

    pa_log_info("%s: deregistreing domain %u from AudioManager",
                __FILE__, domain);

    msg = dbus_message_new_method_call(routerif->amnam,
                                       routerif->amrpath,
                                       routerif->amrnam,
                                       AUDIOMGR_DEREGISTER_DOMAIN);
    if (msg == NULL) {
        pa_log("%s: Failed to create D-Bus message for '%s'",
               __FILE__, AUDIOMGR_DEREGISTER_DOMAIN);
        success = FALSE;
        goto getout;
    }

    dbus_message_set_no_reply(msg, TRUE);

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_UINT16,  &domain,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: Failed to build D-Bus message for '%s'",
               __FILE__, AUDIOMGR_DEREGISTER_DOMAIN);
        goto getout;
    }

    if (!dbus_connection_send(conn, msg, NULL)) {
        pa_log("%s: Failed to send '%s'", __FILE__,AUDIOMGR_DEREGISTER_DOMAIN);
        goto getout;
    }

    dbus_connection_flush(conn);

 getout:
    dbus_message_unref(msg);
    return success;
}


static void audiomgr_register_node_cb(struct userdata *u,
                                      const char      *method,
                                      DBusMessage     *reply,
                                      void            *data)
{
    const char      *error_descr;
    dbus_uint16_t    object_id;
    dbus_uint16_t    status;
    int              success;
    const char      *objtype;

    pa_assert(u);
    pa_assert(method);
    pa_assert(reply);
    pa_assert(data);

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_STRING, &error_descr,
                                        DBUS_TYPE_INVALID);

        if (!success)
            error_descr = dbus_message_get_error_name(reply);

        pa_log_info("%s: AudioManager registration failed: %s",
                    __FILE__, error_descr);
    }
    else {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_UINT16, &object_id,
                                        DBUS_TYPE_UINT16, &status,
                                        DBUS_TYPE_INVALID);

        if (!success) {
            pa_log("got broken message from AudioManager.Registration failed");
        }
        else {
            if (!strncasecmp("register", method, 8))
                objtype = method + 8;
            else
                objtype = method;

            pa_log_info("AudioManager replied to registration: %sID: %u",
                        objtype, object_id);

            pa_audiomgr_node_registered(u, object_id, status, data);
        }
    }
}

static pa_bool_t build_sound_properties(DBusMessageIter *mit,
                                        struct am_nodereg_data *rd)
{
    static int16_t zero;

    DBusMessageIter ait, sit;
    int i;

#define CONT_OPEN(p,t,s,c) dbus_message_iter_open_container(p, t, s, c)
#define CONT_APPEND(i,t,v) dbus_message_iter_append_basic(i, t, v)
#define CONT_CLOSE(p,c)    dbus_message_iter_close_container(p, c)

    if (!CONT_OPEN(mit, DBUS_TYPE_ARRAY, "(nn)", &ait))
        return FALSE;

    for (i = 1;  i < 3;  i++) {
        if (! CONT_OPEN   (&ait, DBUS_TYPE_STRUCT, NULL, &sit) ||
            ! CONT_APPEND (&sit, DBUS_TYPE_INT16,  &i        ) ||
            ! CONT_APPEND (&sit, DBUS_TYPE_INT16,  &zero     ) ||
            ! CONT_CLOSE  (&ait,                         &sit)   )
        {
            return FALSE;
        }
    }

    if (!CONT_CLOSE(mit, &ait))
        return FALSE;

#undef CONT_CLOSE
#undef CONT_APPEND
#undef CONT_OPEN

    return TRUE;
}

static pa_bool_t build_connection_formats(DBusMessageIter *mit,
                                          struct am_nodereg_data *rd)
{
    DBusMessageIter ait;
    int i;

#define CONT_OPEN(t,s)   dbus_message_iter_open_container(mit, t, s, &ait)
#define CONT_APPEND(t,v) dbus_message_iter_append_basic(&ait, t, v)
#define CONT_CLOSE       dbus_message_iter_close_container(mit, &ait)

    if (!CONT_OPEN(DBUS_TYPE_ARRAY, "n")) 
        return FALSE;

    for (i = 1;  i < 2;  i++) {
        if (!CONT_APPEND(DBUS_TYPE_INT16,  &i))
            return FALSE;
    }

    if (!CONT_CLOSE)
        return FALSE;

#undef CONT_CLOSE
#undef CONT_APPEND
#undef CONT_OPEN

    return TRUE;
}

pa_bool_t pa_routerif_register_node(struct userdata *u,
                                    am_method m,
                                    am_nodereg_data *rd)
{
    const char      *method = method_str(m);
    pa_routerif     *routerif;
    DBusConnection  *conn;
    DBusMessage     *msg;
    DBusMessageIter  mit;
    DBusMessageIter  cit;
    pa_bool_t        success = FALSE;

    pa_assert(u);
    pa_assert(rd);
    pa_assert_se((routerif = u->routerif));
    pa_assert_se((conn = pa_dbus_connection_get(routerif->conn)));

    pa_log_debug("%s: %s '%s' to AudioManager", __FUNCTION__, method,rd->name);

    msg = dbus_message_new_method_call(routerif->amnam, routerif->amrpath,
                                       routerif->amrnam, method);
    
    if (msg == NULL) {
        pa_log("%s: Failed to create D-BUS message to '%s'", __FILE__, method);
        goto getout;
    }


#define MSG_APPEND(t,v)  dbus_message_iter_append_basic(&mit, t, v)
#define CONT_OPEN(t,s)   dbus_message_iter_open_container(&mit, t, s, &cit)
#define CONT_APPEND(t,v) dbus_message_iter_append_basic(&cit, t, v)
#define CONT_CLOSE       dbus_message_iter_close_container(&mit, &cit)

    dbus_message_iter_init_append(msg, &mit);

    if ((!strcmp(method, AUDIOMGR_REGISTER_SINK) &&
         (! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->id          ) ||
          ! MSG_APPEND  ( DBUS_TYPE_STRING , &rd->name        ) ||
          ! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->domain      ) ||
          ! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->class       ) ||
          ! MSG_APPEND  ( DBUS_TYPE_INT16  , &rd->volume      ) ||
          ! MSG_APPEND  ( DBUS_TYPE_BOOLEAN, &rd->visible     ) ||
          ! CONT_OPEN   ( DBUS_TYPE_STRUCT ,  NULL            ) ||
          ! CONT_APPEND ( DBUS_TYPE_INT16  , &rd->avail.status) ||
          ! CONT_APPEND ( DBUS_TYPE_INT16  , &rd->avail.reason) ||
          ! CONT_CLOSE                                          ||
          ! MSG_APPEND  ( DBUS_TYPE_INT16  , &rd->mute        ) ||
          ! MSG_APPEND  ( DBUS_TYPE_INT16  , &rd->mainvol     ) ||
          ! build_sound_properties(&mit, rd)                    ||
          ! build_connection_formats(&mit, rd)                  ||
          ! build_sound_properties(&mit, rd)                      )) ||
        (!strcmp(method, AUDIOMGR_REGISTER_SOURCE) &&
         (! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->id          ) ||
          ! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->domain      ) ||
          ! MSG_APPEND  ( DBUS_TYPE_STRING , &rd->name        ) ||
          ! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->class       ) ||
          ! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->state       ) ||
          ! MSG_APPEND  ( DBUS_TYPE_INT16  , &rd->volume      ) ||
          ! MSG_APPEND  ( DBUS_TYPE_BOOLEAN, &rd->visible     ) ||
          ! CONT_OPEN   ( DBUS_TYPE_STRUCT ,  NULL            ) ||
          ! CONT_APPEND ( DBUS_TYPE_INT16  , &rd->avail.status) ||
          ! CONT_APPEND ( DBUS_TYPE_INT16  , &rd->avail.reason) ||
          ! CONT_CLOSE                                          ||
          ! MSG_APPEND  ( DBUS_TYPE_UINT16 , &rd->interrupt   ) ||
          ! build_sound_properties(&mit, rd)                    ||
          ! build_connection_formats(&mit, rd)                  ||
          ! build_sound_properties(&mit, rd)                      )))
    {        
        pa_log("%s: failed to build message for AudioManager '%s'",
               __FILE__, method);
        goto getout;
    }
    
#undef CONT_CLOSE
#undef CONT_APPEND
#undef CONT_OPEN
#undef MSG_APPEND


    success = send_message_with_reply(u, conn, msg,
                                      audiomgr_register_node_cb, rd);
    if (!success) {
        pa_log("%s: Failed to %s", __FILE__, method);
        goto getout;
    }
    
 getout:
    dbus_message_unref(msg);
    return success;
}

static void audiomgr_unregister_node_cb(struct userdata *u,
                                        const char      *method,
                                        DBusMessage     *reply,
                                        void            *data)
{
    const char      *error_descr;
    dbus_uint16_t    status;
    int              success;
    const char      *objtype;

    pa_assert(u);
    pa_assert(method);
    pa_assert(reply);
    pa_assert(data);

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_STRING, &error_descr,
                                        DBUS_TYPE_INVALID);

        if (!success)
            error_descr = dbus_message_get_error_name(reply);

        pa_log_info("%s: AudioManager deregistration failed: %s",
                    __FILE__, error_descr);
    }
    else {
        success = dbus_message_get_args(reply, NULL,
                                        DBUS_TYPE_UINT16, &status,
                                        DBUS_TYPE_INVALID);

        if (!success) {
            pa_log("got broken message from AudioManager. "
                   "Deregistration failed");
        }
        else {
            if (!strncasecmp("deregister", method, 10))
                objtype = method + 10;
            else
                objtype = method;

            pa_log_info("AudioManager replied to %s deregistration: %u",
                        objtype, status);

            pa_audiomgr_node_unregistered(u, data);
        }
    }
}

pa_bool_t pa_routerif_unregister_node(struct userdata *u,
                                      am_method m,
                                      am_nodeunreg_data *ud)
{
    const char     *method = method_str(m);
    pa_routerif    *routerif;
    DBusConnection *conn;
    DBusMessage    *msg;
    pa_bool_t       success = FALSE;

    pa_assert(u);
    pa_assert(ud);
    pa_assert_se((routerif = u->routerif));
    pa_assert_se((conn = pa_dbus_connection_get(routerif->conn)));

    pa_log_debug("%s: %s '%s' to AudioManager", __FUNCTION__, method,ud->name);

    msg = dbus_message_new_method_call(routerif->amnam, routerif->amrpath,
                                       routerif->amrnam, method);
    
    if (msg == NULL) {
        pa_log("%s: Failed to create D-BUS message to '%s'", __FILE__, method);
        goto getout;
    }

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_INT16, &ud->id,
                                       DBUS_TYPE_INVALID);

    success = send_message_with_reply(u, conn, msg,
                                      audiomgr_unregister_node_cb,ud);
    if (!success) {
        pa_log("%s: Failed to %s", __FILE__, method);
        goto getout;
    }
    
 getout:
    dbus_message_unref(msg);
    return success;
}

static pa_bool_t routerif_connect(struct userdata *u, DBusMessage *msg)
{
    struct am_connect_data ac;
    int                    success;

    pa_assert(u);
    pa_assert(msg);

    memset(&ac, 0, sizeof(ac));

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_UINT16, &ac.handle,
                                    DBUS_TYPE_UINT16, &ac.connection,
                                    DBUS_TYPE_UINT16, &ac.source,
                                    DBUS_TYPE_UINT16, &ac.sink,
                                    DBUS_TYPE_INT16 , &ac.format,
                                    DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: got broken connect message from AudioManager. "
               "Ignoring it", __FILE__);
        return FALSE;
    }

    pa_log_debug("AudioManager connect(%u|%u|%u|%u|%d)",
                 ac.handle, ac.connection, ac.source, ac.sink, ac.format);

    pa_audiomgr_connect(u, &ac);

    return TRUE;
}

static pa_bool_t routerif_disconnect(struct userdata *u, DBusMessage *msg)
{
    struct am_connect_data ac;
    int                    success;

    pa_assert(u);
    pa_assert(msg);

    memset(&ac, 0, sizeof(ac));

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_UINT16, &ac.handle,
                                    DBUS_TYPE_UINT16, &ac.connection,
                                    DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: got broken disconnect message from AudioManager. "
               "Ignoring it",  __FILE__);
        return FALSE;
    }

    pa_log_debug("AudioManager disconnect(%u|%u)", ac.handle, ac.connection);

    pa_audiomgr_disconnect(u, &ac);

    return TRUE;
}

pa_bool_t pa_routerif_acknowledge(struct userdata *u, am_method m,
                                  struct am_ack_data *ad)
{
    const char     *method = method_str(m);
    pa_routerif    *routerif;
    DBusConnection *conn;
    DBusMessage    *msg;
    pa_bool_t       success;

    pa_assert(u);
    pa_assert(method);
    pa_assert_se((routerif = u->routerif));
    pa_assert_se((conn = pa_dbus_connection_get(routerif->conn)));

    pa_log_debug("%s: sending %s", __FILE__, method);

    msg = dbus_message_new_method_call(routerif->amnam,
                                       routerif->amrpath,
                                       routerif->amrnam,
                                       method);
    if (msg == NULL) {
        pa_log("%s: Failed to create D-Bus message for '%s'",
               __FILE__, method);
        success = FALSE;
        goto getout;
    }

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_UINT16,  &ad->handle,
                                       DBUS_TYPE_UINT16,  &ad->param1,
                                       DBUS_TYPE_UINT16,  &ad->error,
                                       DBUS_TYPE_INVALID);
    if (!success) {
        pa_log("%s: Failed to build D-Bus message message '%s'",
               __FILE__, method);
        goto getout;
    }

    if (!dbus_connection_send(conn, msg, NULL)) {
        pa_log("%s: Failed to send D-Bus message '%s'", __FILE__, method);
        goto getout;
    }

 getout:
    dbus_message_unref(msg);
    return success;
}


static const char *method_str(am_method m)
{
    switch (m) {
    case audiomgr_register_domain:   return AUDIOMGR_REGISTER_DOMAIN;
    case audiomgr_domain_complete:   return AUDIOMGR_DOMAIN_COMPLETE;
    case audiomgr_deregister_domain: return AUDIOMGR_DEREGISTER_DOMAIN;
    case audiomgr_register_source:   return AUDIOMGR_REGISTER_SOURCE;
    case audiomgr_deregister_source: return AUDIOMGR_DEREGISTER_SOURCE;
    case audiomgr_register_sink:     return AUDIOMGR_REGISTER_SINK;
    case audiomgr_deregister_sink:   return AUDIOMGR_DEREGISTER_SINK;
    case audiomgr_connect:           return AUDIOMGR_CONNECT;
    case audiomgr_connect_ack:       return AUDIOMGR_CONNECT_ACK;   
    case audiomgr_disconnect:        return AUDIOMGR_DISCONNECT;
    case audiomgr_disconnect_ack:    return AUDIOMGR_DISCONNECT_ACK;    
    case audiomgr_setsinkvol_ack:    return AUDIOMGR_SETSINKVOL_ACK;
    case audiomgr_setsrcvol_ack:     return AUDIOMGR_SETSRCVOL_ACK;
    case audiomgr_sinkvoltick_ack:   return AUDIOMGR_SINKVOLTICK_ACK;
    case audiomgr_srcvoltick_ack:    return AUDIOMGR_SRCVOLTICK_ACK;
    case audiomgr_setsinkprop_ack:   return AUDIOMGR_SETSINKPROP_ACK;
    default:                         return "invalid_method";
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

