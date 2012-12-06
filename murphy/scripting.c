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
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-utils/strarray.h>

#include "scripting.h"
#include "node.h"
#include "router.h"
#include "volume.h"
#include "config.h"

#define NODE_CLASS         MRP_LUA_CLASS(node, instance)
#define RTGROUP_CLASS      MRP_LUA_CLASS_SIMPLE(routing_group)

#define USERDATA           "murphy_ivi_userdata"

#undef  MRP_LUA_ENTER
#define MRP_LUA_ENTER                                           \
    pa_log_debug("%s() enter", __FUNCTION__)

#undef  MRP_LUA_LEAVE
#define MRP_LUA_LEAVE(_v)                                       \
    do {                                                        \
        pa_log_debug("%s() leave (%d)", __FUNCTION__, (_v));    \
        return (_v);                                            \
    } while (0)

#undef  MRP_LUA_LEAVE_NOARG
#define MRP_LUA_LEAVE_NOARG                                     \
    pa_log_debug("%s() leave", __FUNCTION__)

struct pa_scripting {
    lua_State *L;
};

struct scripting_node {
    struct userdata  *userdata;
    const char       *id;
    mir_node         *node;
};

struct scripting_rtgroup {
    struct userdata  *userdata;
    mir_rtgroup      *rtg;
    mir_direction     type;
    mrp_funcbridge_t *accept;
    mrp_funcbridge_t *compare;
};

typedef struct {
    const char *name;
    int value;
} const_def_t;

typedef struct {
    const char *name;
    const char *sign;
    mrp_funcbridge_cfunc_t func;
    void *data;
} funcbridge_def_t;

typedef enum {
    NAME = 1,
    TYPE,
    NODE_TYPE,
    PRIORITY,
    ROUTE,
    INPUT,
    OUTPUT,
    ACCEPT,
    COMPARE,
    DIRECTION,
    IMPLEMENT,
    CHANNELS,
    LOCATION,
    PRIVACY,
    DESCRIPTION,
    AVAILABLE,
} field_t;

static int  node_create(lua_State *);
static int  node_getfield(lua_State *);
static int  node_setfield(lua_State *);
static int  node_tostring(lua_State *);
static void node_destroy(void *);

static int  rtgroup_create(lua_State *);
static int  rtgroup_getfield(lua_State *);
static int  rtgroup_setfield(lua_State *);
static int  rtgroup_tostring(lua_State *);
static void rtgroup_destroy(void *);

static pa_bool_t rtgroup_accept(struct userdata *, mir_rtgroup *, mir_node *);
static bool accept_bridge(lua_State *, void *, const char *,
                          mrp_funcbridge_value_t *, char *,
                          mrp_funcbridge_value_t *);
static bool compare_bridge(lua_State *, void *, const char *,
                           mrp_funcbridge_value_t *, char *,
                           mrp_funcbridge_value_t *);
static int rtgroup_compare(struct userdata *, mir_rtgroup *,
                           mir_node *, mir_node *);
static field_t field_check(lua_State *, int, const char **);
static field_t field_name_to_type(const char *, size_t);
static int make_id(char *buf, size_t len, const char *fmt, ...);
static bool define_constants(lua_State *);
static bool register_methods(lua_State *);


MRP_LUA_METHOD_LIST_TABLE (
    node_methods,             /* methodlist name */
    MRP_LUA_METHOD_CONSTRUCTOR  (node_create)
);


MRP_LUA_METHOD_LIST_TABLE (
    node_overrides,           /* methodlist name */
    MRP_LUA_OVERRIDE_CALL       (node_create)
    MRP_LUA_OVERRIDE_GETFIELD   (node_getfield)
    MRP_LUA_OVERRIDE_SETFIELD   (node_setfield)
    MRP_LUA_OVERRIDE_STRINGIFY  (node_tostring)
);


MRP_LUA_CLASS_DEF (
   node,                     /* class name */
   instance,                 /* constructor name */
   scripting_node,           /* userdata type */
   node_destroy,             /* userdata destructor */
   node_methods,             /* class methods */
   node_overrides            /* override methods */
);

MRP_LUA_CLASS_DEF_SIMPLE (
   routing_group,               /* class name */
   scripting_rtgroup,           /* userdata type */
   node_destroy,                /* userdata destructor */
   MRP_LUA_METHOD_LIST (        /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (rtgroup_create)
   ),
   MRP_LUA_METHOD_LIST (        /* overrides */
      MRP_LUA_OVERRIDE_CALL       (rtgroup_create)
      MRP_LUA_OVERRIDE_GETFIELD   (rtgroup_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (rtgroup_setfield)
      MRP_LUA_OVERRIDE_STRINGIFY  (rtgroup_tostring)
   )
);

pa_scripting *pa_scripting_init(struct userdata *u)
{
    pa_scripting *scripting;
    lua_State *L;

    pa_assert(u);

    scripting = pa_xnew0(pa_scripting, 1);

    if (!(L = luaL_newstate()))
        pa_log("failed to initialize Lua");
    else {
        luaL_openlibs(L);
        mrp_create_funcbridge_class(L);
        mrp_lua_create_object_class(L, NODE_CLASS);
        mrp_lua_create_object_class(L, RTGROUP_CLASS);

        define_constants(L);
        register_methods(L);

        lua_pushlightuserdata(L, u);
        lua_setglobal(L, USERDATA);

        scripting->L = L;
    }

    return scripting;
}

void pa_scripting_done(struct userdata *u)
{
    pa_scripting *scripting;

    if (u && (scripting = u->scripting)) {
        pa_xfree(scripting);
        u->scripting = NULL;
    }
}

pa_bool_t pa_scripting_dofile(struct userdata *u, const char *file)
{
    pa_scripting *scripting;
    lua_State *L;
    pa_bool_t success;

    pa_assert(u);
    pa_assert(file);

    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    if (!luaL_loadfile(L, file) && !lua_pcall(L, 0, 0, 0))
        success =TRUE;
    else {
        success = FALSE;
        pa_log("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    return success;
}

scripting_node *pa_scripting_node_create(struct userdata *u, mir_node *node)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_node *sn;
    char id[256];

    pa_assert(u);
    pa_assert(node);
    pa_assert(node->amname);

    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    make_id(id, sizeof(id), "%s_%d", node->amname, node->index);

    if ((sn = (scripting_node *)mrp_lua_create_object(L, NODE_CLASS, id))) {
        sn->userdata = u;
        sn->id = pa_xstrdup(id);
        sn->node = node;
    }

    return sn;
}

void pa_scripting_node_destroy(struct userdata *u, mir_node *node)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_node *sn;

    MRP_LUA_ENTER;

    pa_assert(u);
    pa_assert(node);
    
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    if ((sn = node->scripting))
        mrp_lua_destroy_object(L, sn->id, sn);

    MRP_LUA_LEAVE_NOARG;
}

static int node_create(lua_State *L)
{
    MRP_LUA_ENTER;

    lua_pushnil(L);

    MRP_LUA_LEAVE(1);
}

static int node_getfield(lua_State *L)
{
    scripting_node *sn;
    mir_node *node;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(sn = (scripting_node *)mrp_lua_check_object(L, NODE_CLASS, 1)))
        lua_pushnil(L);
    else {
        pa_assert_se((node = sn->node));

        switch (fld) {
        case NAME:           lua_pushstring(L, node->amname);         break;
        case DESCRIPTION:    lua_pushstring(L, node->amdescr);        break;
        case DIRECTION:      lua_pushinteger(L, node->direction);     break;
        case IMPLEMENT:      lua_pushinteger(L, node->implement);     break;
        case CHANNELS:       lua_pushinteger(L, node->channels);      break;
        case LOCATION:       lua_pushinteger(L, node->location);      break;
        case PRIVACY:        lua_pushinteger(L, node->privacy);       break;
        case TYPE:           lua_pushinteger(L, node->type);          break;
        case AVAILABLE:      lua_pushboolean(L, node->available);     break;
        default:             lua_pushnil(L);                          break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int node_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;
    
    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only node", f);

    MRP_LUA_LEAVE(0);
}

static int node_tostring(lua_State *L)
{
    scripting_node *sn;

    MRP_LUA_ENTER;

    sn = (scripting_node *)mrp_lua_check_object(L, NODE_CLASS, 1);

    lua_pushstring(L, (sn && sn->id) ? sn->id : "<unknown node>");

    MRP_LUA_LEAVE(1);
}

static void node_destroy(void *data)
{
    scripting_node *sn = (scripting_node *)data;
    mir_node *node;

    MRP_LUA_ENTER;

    pa_assert_se((node = sn->node));
    pa_assert(sn == node->scripting);

    pa_xfree((void *)sn->id);

    node->scripting = NULL;

    MRP_LUA_LEAVE_NOARG;
}

static int rtgroup_create(lua_State *L)
{
    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    mir_rtgroup *rtg;
    scripting_rtgroup *rtgs;
    const char *name = NULL;
    mir_direction type = 0;
    mrp_funcbridge_t *accept = NULL;
    mrp_funcbridge_t *compare = NULL;

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case NAME:      name    = luaL_checkstring(L, -1);               break;
        case NODE_TYPE: type    = luaL_checkint(L, -1);                  break;
        case ACCEPT:    accept  = mrp_funcbridge_create_luafunc(L, -1);  break;
        case COMPARE:   compare = mrp_funcbridge_create_luafunc(L, -1);  break;
        default:        luaL_error(L, "bad field '%s'", fldnam);         break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (!name)
        luaL_error(L, "missing name field");
    if (type != mir_input && type != mir_output)
        luaL_error(L, "missing or invalid node_type");
    if (!accept)
        luaL_error(L, "missing or invalid accept field");
    if (!compare)
        luaL_error(L, "missing or invalid compare field");

    rtgs = (scripting_rtgroup *)mrp_lua_create_object(L, RTGROUP_CLASS, name);

    rtg  = mir_router_create_rtgroup(u, type, pa_xstrdup(name),
                                     rtgroup_accept, rtgroup_compare);
    if (!rtgs || !rtg)
        luaL_error(L, "failed to create routing group '%s'", name);

    rtg->scripting = rtgs;

    rtgs->userdata = u;
    rtgs->rtg = rtg;
    rtgs->type = type;
    rtgs->accept = accept;
    rtgs->compare = compare;

    MRP_LUA_LEAVE(1);
}

static int rtgroup_getfield(lua_State *L)
{
    scripting_rtgroup *rtgs;
    mir_rtgroup *rtg;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(rtgs = (scripting_rtgroup *)mrp_lua_check_object(L,RTGROUP_CLASS,1)))
        lua_pushnil(L);
    else {
        pa_assert_se((rtg = rtgs->rtg));

        switch (fld) {
        case NAME:           lua_pushstring(L, rtg->name);         break;
        case NODE_TYPE:      lua_pushinteger(L, rtgs->type);       break;
        default:             lua_pushnil(L);                       break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int rtgroup_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only routing_group", f);
    
    MRP_LUA_LEAVE(0);
}

static int rtgroup_tostring(lua_State *L)
{
    scripting_rtgroup *rtgs;
    mir_rtgroup *rtg;

    MRP_LUA_ENTER;

    rtgs = (scripting_rtgroup *)mrp_lua_check_object(L, RTGROUP_CLASS, 1);
    pa_assert_se((rtg = rtgs->rtg));

    lua_pushstring(L, rtg->name);

    MRP_LUA_LEAVE(1);
}

static void rtgroup_destroy(void *data)
{
    scripting_rtgroup *rtgs = (scripting_rtgroup *)data;
    mir_rtgroup *rtg;

    MRP_LUA_ENTER;

    pa_assert_se((rtg = rtgs->rtg));
    pa_assert(rtgs == rtg->scripting);

    rtg->scripting = NULL;

    MRP_LUA_LEAVE_NOARG;
}


static pa_bool_t rtgroup_accept(struct userdata *u,
                                mir_rtgroup *rtg,
                                mir_node *node)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_rtgroup *rtgs;
    mrp_funcbridge_value_t  args[2];
    char rt;
    mrp_funcbridge_value_t  rv;
    pa_bool_t accept;

    pa_assert(u);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));
    pa_assert(rtg);
    pa_assert_se((rtgs = rtg->scripting));
    pa_assert(u == rtgs->userdata);
    pa_assert(rtgs->accept);
    pa_assert(node);

    accept = false;

    if ((rtgs = rtg->scripting) && node->scripting) {

        args[0].pointer = rtgs;
        args[1].pointer = node->scripting;

        if (!mrp_funcbridge_call_from_c(L, rtgs->accept, "oo",args, &rt,&rv))
            pa_log("failed to call accept function");
        else {
            if (rt != MRP_FUNCBRIDGE_BOOLEAN)
                pa_log("accept function returned invalid type");
            else
                accept = rv.boolean;
        }
    }

    return accept;
}

static int rtgroup_compare(struct userdata *u,
                           mir_rtgroup *rtg,
                           mir_node *node1,
                           mir_node *node2)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_rtgroup *rtgs;
    mrp_funcbridge_value_t  args[3];
    char rt;
    mrp_funcbridge_value_t  rv;
    int result;

    pa_assert(u);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));
    pa_assert(rtg);
    pa_assert_se((rtgs = rtg->scripting));
    pa_assert(u == rtgs->userdata);
    pa_assert(rtgs->compare);
    pa_assert(node1);
    pa_assert(node2);

    result = -1;

    if ((rtgs = rtg->scripting) && node1->scripting && node2->scripting) {

        args[0].pointer = rtgs;
        args[1].pointer = node1->scripting;
        args[2].pointer = node2->scripting;

        if (!mrp_funcbridge_call_from_c(L, rtgs->compare, "ooo",args, &rt,&rv))
            pa_log("failed to call compare function");
        else {
            if (rt != MRP_FUNCBRIDGE_FLOATING)
                pa_log("compare function returned invalid type");
            else
                result = rv.floating;
        }
    }

    return result;
}


static bool accept_bridge(lua_State *L, void *data,
                          const char *signature, mrp_funcbridge_value_t *args,
                          char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mir_rtgroup_accept_t accept;
    scripting_rtgroup *rtgs;
    scripting_node *ns;
    struct userdata *u;
    mir_rtgroup *rtg;
    mir_node *node;
    bool success;

    (void)L;
    (void)data;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((accept = (mir_rtgroup_accept_t)data));

    if (strcmp(signature, "oo"))
        success = false;
    else {
        pa_assert_se((rtgs = args[0].pointer));
        pa_assert_se((u = rtgs->userdata));
        pa_assert_se((ns = args[1].pointer));

        if (!(rtg = rtgs->rtg) || !(node = ns->node))
            success = false;
        else {
            success = true;
            *ret_type = MRP_FUNCBRIDGE_BOOLEAN;
            ret_val->boolean = accept(u, rtg, node);
        }
    }

    return success;
}


static bool compare_bridge(lua_State *L, void *data,
                           const char *signature, mrp_funcbridge_value_t *args,
                           char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mir_rtgroup_compare_t compare;
    scripting_rtgroup *rtgs;
    scripting_node *ns1, *ns2;
    struct userdata *u;
    mir_rtgroup *rtg;
    mir_node *node1, *node2;
    bool success;

    (void)L;
    (void)data;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((compare = (mir_rtgroup_compare_t)data));

    if (strcmp(signature, "ooo"))
        success = false;
    else {
        pa_assert_se((rtgs = args[0].pointer));
        pa_assert_se((u = rtgs->userdata));
        pa_assert_se((ns1 = args[1].pointer));
        pa_assert_se((ns2 = args[2].pointer));


        if (!(rtg = rtgs->rtg) || !(node1 = ns1->node) || !(node2 = ns2->node))
            success = false;
        else {
            success = true;
            *ret_type = MRP_FUNCBRIDGE_FLOATING;
            ret_val->floating = compare(u, rtg, node1, node2);
        }
    }

    return success;
}


static field_t field_check(lua_State *L, int idx, const char **ret_fldnam)
{
    const char *fldnam;
    size_t fldnamlen;
    field_t fldtyp;

    if (!(fldnam = lua_tolstring(L, idx, &fldnamlen)))
        fldtyp = 0;
    else
        fldtyp = field_name_to_type(fldnam, fldnamlen);

    if (ret_fldnam)
        *ret_fldnam = fldnam;

    return fldtyp;
}

static field_t field_name_to_type(const char *name, size_t len)
{
    switch (len) {

    case 4:
        if (!strcmp(name, "name"))
            return NAME;
        if (!strcmp(name, "type"))
            return TYPE;
        break;

    case 5:
        if (!strcmp(name, "route"))
            return ROUTE;
        if (!strcmp(name, "input"))
            return INPUT;
        break;

    case 6:
        if (!strcmp(name, "output"))
            return OUTPUT;
        if (!strcmp(name, "accept"))
            return ACCEPT;
        break;

    case 7:
        if (!strcmp(name, "compare"))
            return COMPARE;
        if (!strcmp(name, "privacy"))
            return PRIVACY;
        break;

    case 8:
        if (!strcmp(name, "priority"))
            return PRIORITY;
        if (!strcmp(name, "channels"))
            return CHANNELS;
        if (!strcmp(name, "location"))
            return LOCATION;
        break;

    case 9:
        if (!strcmp(name, "node_type"))
            return NODE_TYPE;
        if (!strcmp(name, "direction"))
            return DIRECTION;
        if (!strcmp(name, "implement"))
            return IMPLEMENT;
        if (!strcmp(name, "available"))
            return AVAILABLE;
        break;

    case 11:
        if (!strcmp(name, "description"))
            return DESCRIPTION;
        break;

    default:
        break;
    }

    return 0;
}

static int make_id(char *buf, size_t len, const char *fmt, ...)
{
    va_list ap;
    int l;
    char *p, c;

    va_start(ap, fmt);
    l = vsnprintf(buf, len, fmt, ap);
    va_end(ap);

    for (p = buf;  (c = *p);  p++) {
        if (isalpha(c))
            c = tolower(c);
        else if (!isdigit(c))
            c = '_';
        *p = c;
    }

    return l;
}



static bool define_constants(lua_State *L)
{
    static const_def_t const_defs[] = {
        { "input"            , mir_input            },
        { "output"           , mir_output           },
        { "device"           , mir_device           },
        { "stream"           , mir_stream           },
        { "internal"         , mir_internal         },
        { "external"         , mir_external         },
        { "radio"            , mir_radio            },
        { "player"           , mir_player           },
        { "navigator"        , mir_navigator        },
        { "game"             , mir_game             },
        { "browser"          , mir_browser          },
        { "camera"           , mir_camera           },
        { "phone"            , mir_phone            },
        { "alert"            , mir_alert            },
        { "event"            , mir_event            },
        { "system"           , mir_system           },
        { "speakers"         , mir_speakers         },
        { "microphone"       , mir_microphone       },
        { "jack"             , mir_jack             },
        { "spdif"            , mir_spdif            },
        { "hdmi"             , mir_hdmi             },
        { "wired_headset"    , mir_wired_headset    },
        { "wired_headphone"  , mir_wired_headphone  },
        { "usb_headset"      , mir_usb_headset      },
        { "usb_headphone"    , mir_usb_headphone    },
        { "bluetooth_sco"    , mir_bluetooth_sco    },
        { "bluetooth_a2dp"   , mir_bluetooth_a2dp   },
        { "bluetooth_carkit" , mir_bluetooth_carkit },
        { "bluetooth_source" , mir_bluetooth_source },
        { "bluetooth_sink"   , mir_bluetooth_sink   },
        {       NULL         ,         0            }
    };

    const_def_t *cd;
    bool success = false;

    lua_getglobal(L, "node");

    if (lua_istable(L, -1)) {
        for (cd = const_defs;   cd->name;   cd++) {
            lua_pushinteger(L, cd->value);
            lua_setfield(L, -2, cd->name);
        }

        lua_pop(L, 1);

        success = true;
    }

    return success;
}


static bool register_methods(lua_State *L)
{
    static funcbridge_def_t funcbridge_defs[] = {
        {"accept_default" , "oo" , accept_bridge , mir_router_default_accept },
        {"compare_default", "ooo", compare_bridge, mir_router_default_compare},
        {"accept_phone"   , "oo" , accept_bridge , mir_router_phone_accept   },
        {"compare_phone"  , "ooo", compare_bridge, mir_router_phone_compare  },
        {       NULL      ,  NULL,      NULL     ,         NULL              }
    };

    funcbridge_def_t *d;
    mrp_funcbridge_t *f;
    bool success = true;

    for (d = funcbridge_defs;   d->name;    d++) {
        if (!mrp_funcbridge_create_cfunc(L,d->name,d->sign,d->func,d->data)) {
            pa_log("%s: failed to register builtin function '%s'",
                   __FILE__, d->name);
            success = false;
        }
    }

    return success;
}


                                  
/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
