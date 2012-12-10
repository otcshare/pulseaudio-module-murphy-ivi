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
#define APPLICATION_CLASS  MRP_LUA_CLASS_SIMPLE(application_class)
#define VOLLIM_CLASS       MRP_LUA_CLASS_SIMPLE(volume_limit)

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
    const char       *input;
    const char       *output;
} route_t;

struct scripting_apclass {
    struct userdata  *userdata;
    const char       *name;
    mir_node_type     type;
    int               priority;
    route_t          *route;
};

typedef enum {
    vollim_class = 1,
    vollim_generic
} vollim_type;

typedef struct {
    size_t      nint;
    int        *ints;
} intarray_t;

typedef struct {
    pa_bool_t   mallocd;
    double     *value;
} limit_data_t;

struct scripting_vollim {
    struct userdata  *userdata;
    const char       *name;
    vollim_type       type;
    intarray_t       *classes;
    limit_data_t     *limit;
    mrp_funcbridge_t *calculate;
    char              args[0];
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
    CALCULATE,
    LIMIT,
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
static int  rtgroup_compare(struct userdata *, mir_rtgroup *,
                            mir_node *, mir_node *);

static bool accept_bridge(lua_State *, void *, const char *,
                          mrp_funcbridge_value_t *, char *,
                          mrp_funcbridge_value_t *);
static bool compare_bridge(lua_State *, void *, const char *,
                           mrp_funcbridge_value_t *, char *,
                           mrp_funcbridge_value_t *);

static int  apclass_create(lua_State *);
static int  apclass_getfield(lua_State *);
static int  apclass_setfield(lua_State *);
static int  apclass_tostring(lua_State *);
static void apclass_destroy(void *);

static route_t *route_check(lua_State *, int);
static int route_push(lua_State *, route_t *);
static void route_destroy(route_t *);

static int  vollim_create(lua_State *);
static int  vollim_getfield(lua_State *);
static int  vollim_setfield(lua_State *);
static int  vollim_tostring(lua_State *);
static void vollim_destroy(void *);

static double vollim_calculate(struct userdata *, int, mir_node *, void *);
static bool calculate_bridge(lua_State *, void *, const char *,
                             mrp_funcbridge_value_t *, char *,
                             mrp_funcbridge_value_t *);

static limit_data_t *limit_data_check(lua_State *, int);
static int limit_data_push(lua_State *, limit_data_t *);
static void limit_data_destroy(limit_data_t *);

static intarray_t *intarray_check(lua_State *, int, int, int);
static int intarray_push(lua_State *, intarray_t *);
static void intarray_destroy(intarray_t *);

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
   node,                        /* class name */
   instance,                    /* constructor name */
   scripting_node,              /* userdata type */
   node_destroy,                /* userdata destructor */
   node_methods,                /* class methods */
   node_overrides               /* override methods */
);

MRP_LUA_CLASS_DEF_SIMPLE (
   routing_group,                /* class name */
   scripting_rtgroup,            /* userdata type */
   node_destroy,                 /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (rtgroup_create)
   ),
   MRP_LUA_METHOD_LIST (         /* overrides */
      MRP_LUA_OVERRIDE_CALL       (rtgroup_create)
      MRP_LUA_OVERRIDE_GETFIELD   (rtgroup_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (rtgroup_setfield)
      MRP_LUA_OVERRIDE_STRINGIFY  (rtgroup_tostring)
   )
);

MRP_LUA_CLASS_DEF_SIMPLE (
   application_class,            /* class name */
   scripting_apclass,            /* userdata type */
   apclass_destroy,              /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (apclass_create)
   ),
   MRP_LUA_METHOD_LIST (        /* overrides */
      MRP_LUA_OVERRIDE_CALL       (apclass_create)
      MRP_LUA_OVERRIDE_GETFIELD   (apclass_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (apclass_setfield)
      MRP_LUA_OVERRIDE_STRINGIFY  (apclass_tostring)
   )
);

MRP_LUA_CLASS_DEF_SIMPLE (
   volume_limit,                 /* class name */
   scripting_vollim,             /* userdata type */
   vollim_destroy,               /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (vollim_create)
   ),
   MRP_LUA_METHOD_LIST (        /* overrides */
      MRP_LUA_OVERRIDE_CALL       (vollim_create)
      MRP_LUA_OVERRIDE_GETFIELD   (vollim_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (vollim_setfield)
      MRP_LUA_OVERRIDE_STRINGIFY  (vollim_tostring)
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
        mrp_lua_create_object_class(L, APPLICATION_CLASS);
        mrp_lua_create_object_class(L, VOLLIM_CLASS);

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

    if ((sn = node->scripting)) {
        mrp_lua_destroy_object(L, sn->id, sn);
        node->scripting = NULL;
    }

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
    char id[256];

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

    make_id(id,sizeof(id), "%s_%sput", name, (type == mir_input) ? "in":"out");

    rtgs = (scripting_rtgroup *)mrp_lua_create_object(L, RTGROUP_CLASS, id);

    rtg  = mir_router_create_rtgroup(u, type, pa_xstrdup(name),
                                     rtgroup_accept, rtgroup_compare);
    if (!rtgs || !rtg)
        luaL_error(L, "failed to create routing group '%s'", id);

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


static int apclass_create(lua_State *L)
{
    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    scripting_apclass *ac;
    char name[256];
    mir_node_type type = -1;
    int priority = -1;
    route_t *route = NULL;
    pa_bool_t ir, or;

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case NODE_TYPE:   type = luaL_checkint(L, -1);               break;
        case PRIORITY:    priority = luaL_checkint(L, -1);           break;
        case ROUTE:       route = route_check(L, -1);                break;
        default:          luaL_error(L, "bad field '%s'", fldnam);   break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (type < mir_application_class_begin ||
        type >= mir_application_class_end    )
        luaL_error(L, "missing or invalid node_type %d", type);
    if (priority < 0)
        luaL_error(L, "missing or invalid priority field");
    if (!route)
        luaL_error(L, "missing or invalid route field");

    make_id(name, sizeof(name), "%s", mir_node_type_str(type));

    mir_router_assign_class_priority(u, type, priority); 

    ir = !route->input  ? TRUE : mir_router_assign_class_to_rtgroup(
                                                        u, type,
                                                        mir_input,
                                                        route->input);
    or = !route->output ? TRUE : mir_router_assign_class_to_rtgroup(
                                                        u, type,
                                                        mir_output,
                                                        route->output);
    ac = (scripting_apclass *)mrp_lua_create_object(L, APPLICATION_CLASS,
                                                    name);
    if (!ir || !or || !ac)
        luaL_error(L, "failed to create application class '%s'", name);

    ac->userdata = u;
    ac->name     = pa_xstrdup(name);
    ac->type     = type;
    ac->priority = priority;
    ac->route    = route;

    MRP_LUA_LEAVE(1);
}

static int apclass_getfield(lua_State *L)
{
    scripting_apclass *ac;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(ac=(scripting_apclass *)mrp_lua_check_object(L,APPLICATION_CLASS,1)))
        lua_pushnil(L);
    else {
        switch (fld) {
        case NAME:           lua_pushstring(L, ac->name);        break;
        case NODE_TYPE:      lua_pushinteger(L, ac->type);       break;
        case PRIORITY:       lua_pushinteger(L, ac->priority);   break;
        case ROUTE:          route_push(L, ac->route);           break;
        default:             lua_pushnil(L);                     break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int apclass_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L,"attempt to set '%s' field of read-only application class",f);
    
    MRP_LUA_LEAVE(0);
}

static int apclass_tostring(lua_State *L)
{
    scripting_apclass *ac;

    MRP_LUA_ENTER;

    ac = (scripting_apclass *)mrp_lua_check_object(L, APPLICATION_CLASS, 1);

    lua_pushstring(L, ac->name);

    MRP_LUA_LEAVE(1);
}

static void apclass_destroy(void *data)
{
    scripting_apclass *ac = (scripting_apclass *)data;

    MRP_LUA_ENTER;

    route_destroy(ac->route);
    ac->route = NULL;

    MRP_LUA_LEAVE_NOARG;
}

static const char *route_name_check(lua_State *L, int idx)
{
    scripting_rtgroup *rtgs;
    mir_rtgroup *rtg;
    const char *name;

    switch (lua_type(L, idx)) {
    case LUA_TSTRING:
        name = lua_tostring(L, idx);
        break;
    case LUA_TTABLE:
        rtgs = (scripting_rtgroup*)mrp_lua_check_object(L, RTGROUP_CLASS, idx);
        if (!rtgs || !(rtg = rtgs->rtg))
            name = NULL;
        else
            name = rtg->name;
        break;
    default:
        name = NULL;
        break;
    }

    return name;
}

static route_t *route_check(lua_State *L, int idx)
{
    size_t fldnamlen;
    const char *fldnam;
    route_t *rt;
    const char *input = NULL;
    const char *output = NULL;

    idx = (idx < 0) ? lua_gettop(L) + idx + 1 : idx;

    luaL_checktype(L, idx, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, idx, fldnam, fldnamlen) {
        switch (field_name_to_type(fldnam, fldnamlen)) {
        case        INPUT:  input  = route_name_check(L, -1);      break;
        case        OUTPUT: output = route_name_check(L, -1);      break;
        default:    luaL_error(L, "invalid field '%s'", fldnam);   break;
        }
    } /* MRP_LUA_FOREACH_FIELD */

    if (!input && !output)
        luaL_error(L, "neither input nor output routing group were specified");

    rt = pa_xmalloc(sizeof(route_t));
    rt->input  = input  ? pa_xstrdup(input)  : NULL;
    rt->output = output ? pa_xstrdup(output) : NULL;

    return rt;
}

static int route_push(lua_State *L, route_t *rt)
{
    if (!rt || (!rt->input && !rt->output))
        lua_pushnil(L);
    else {
        lua_createtable(L, 0, 2);

        if (rt->input) {
            lua_pushstring(L, "input");
            lua_pushstring(L, rt->input);
            lua_settable(L, -3);
        }
        
        if (rt->output) {
            lua_pushstring(L, "output");
            lua_pushstring(L, rt->output);
            lua_settable(L, -3);
        }
    }

    return 1;
}

static void route_destroy(route_t *rt)
{
    if (rt) {
        pa_xfree((void *)rt->input);
        pa_xfree((void *)rt->output);
        pa_xfree((void *)rt);
    }
}


static int vollim_create(lua_State *L)
{
    static int min = mir_application_class_begin;
    static int max = mir_application_class_end;

    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    scripting_vollim *vlim;
    const char *name = NULL;
    vollim_type type = 0;
    limit_data_t *limit = NULL;
    mrp_funcbridge_t *calculate = NULL;
    intarray_t *classes = NULL;
    pa_bool_t suppress = FALSE;
    pa_bool_t correct = FALSE;
    size_t arglgh = 0;
    size_t i;
    char id[256];

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case NAME:      name      = luaL_checkstring(L, -1);             break;
        case TYPE:      type      = luaL_checkint(L, -1);                break;
        case NODE_TYPE: classes = intarray_check(L, -1, min, max);       break;
        case LIMIT:     limit     = limit_data_check(L, -1);             break;
        case CALCULATE: calculate = mrp_funcbridge_create_luafunc(L,-1); break;
        default:        luaL_error(L, "bad field '%s'", fldnam);         break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (!name)
        luaL_error(L, "missing name field");
    if (type != vollim_class && type != vollim_generic)
        luaL_error(L, "missing or invalid type");
    if (type == vollim_class && !classes)
        luaL_error(L, "missing or invalid node_type for class volume limit");
    if (type == vollim_generic && classes)
        luaL_error(L, "can't specify node_type for generic volume limit");
    if (!limit)
        luaL_error(L, "missing or invalid limit");
    if (!calculate || strcmp(calculate->c.signature, "odo"))
        luaL_error(L, "missing or invalid calculate field");
    if (calculate->type == MRP_C_FUNCTION) {
        if (calculate->c.data == mir_volume_suppress) {
            if (type != vollim_class)
                luaL_error(L, "attempt to make generic volume supression");
            suppress = TRUE;
            arglgh = sizeof(mir_volume_suppress_arg);
        }
        else if (calculate->c.data == mir_volume_correction) {
            if (type != vollim_generic)
                luaL_error(L, "attempt to make class based volume correction");
            correct = TRUE;
            arglgh = sizeof(double *);
        }
        else {
            luaL_error(L, "invalid builtin.method for calculate");
        }
    }

    make_id(id, sizeof(id), "%s", name);

    (VOLLIM_CLASS)->userdata_size = sizeof(scripting_vollim) + arglgh;
    vlim = (scripting_vollim *)mrp_lua_create_object(L, VOLLIM_CLASS, id);
    (VOLLIM_CLASS)->userdata_size = sizeof(scripting_vollim);

    vlim->userdata = u;
    vlim->name = pa_xstrdup(name);
    vlim->type = type;
    vlim->classes = classes;
    vlim->limit = limit;
    vlim->calculate = calculate;

    if (suppress) {
        mir_volume_suppress_arg *args = (mir_volume_suppress_arg *)vlim->args;
        size_t size = sizeof(int) * classes->nint;

        args->attenuation = limit->value;
        args->exception.nclass = classes->nint;
        args->exception.classes = pa_xmalloc(size);

        memcpy(args->exception.classes, classes->ints, size);
    }
    else if (correct) {
        *(double **)vlim->args = limit->value;
    }

    if (type == vollim_generic)
        mir_volume_add_generic_limit(u, vollim_calculate, vlim->args);
    else {
        for (i = 0;  i < classes->nint;  i++) {
            mir_volume_add_class_limit(u, classes->ints[i], vollim_calculate,
                                       vlim->args);
        }
    }

    MRP_LUA_LEAVE(1);
}

static int vollim_getfield(lua_State *L)
{
    scripting_vollim *vlim;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(vlim = (scripting_vollim *)mrp_lua_check_object(L, VOLLIM_CLASS, 1)))
        lua_pushnil(L);
    else {
        switch (fld) {
        case NAME:         lua_pushstring(L, vlim->name);           break;
        case TYPE:         lua_pushinteger(L, vlim->type);          break;
        case NODE_TYPE:    intarray_push(L, vlim->classes);         break;
        case LIMIT:        lua_pushnumber(L, *vlim->limit->value);  break;
        default:           lua_pushnil(L);                          break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int vollim_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only volume_limit", f);
    
    MRP_LUA_LEAVE(0);
}

static int vollim_tostring(lua_State *L)
{
    scripting_vollim *vlim;

    MRP_LUA_ENTER;

    vlim = (scripting_vollim *)mrp_lua_check_object(L, VOLLIM_CLASS, 1);

    lua_pushstring(L, vlim->name);

    MRP_LUA_LEAVE(1);
}

static void vollim_destroy(void *data)
{
    scripting_vollim *vlim = (scripting_vollim *)data;

    MRP_LUA_ENTER;

    pa_xfree((void *)vlim->name);
    intarray_destroy(vlim->classes);
    limit_data_destroy(vlim->limit);

    MRP_LUA_LEAVE_NOARG;
}


static double vollim_calculate(struct userdata *u, int class, mir_node *node,
                               void *data)
{
    static int offset = ((scripting_vollim *)0)->args - (char *)0;

    pa_scripting *scripting;
    lua_State *L;
    scripting_vollim *vlim;
    mrp_funcbridge_value_t args[3];
    char rt;
    mrp_funcbridge_value_t  rv;
    double limit;

    pa_assert(u);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));
    pa_assert(class >= mir_application_class_begin &&
              class <  mir_application_class_end);
    pa_assert(node);
    pa_assert(args);

    vlim = (scripting_vollim *)(data - offset);

    pa_assert(u == vlim->userdata);

    limit = -90.0;

    if (node->scripting) {

        args[0].pointer = vlim;
        args[1].integer = class;
        args[2].pointer = node->scripting;

        if (!mrp_funcbridge_call_from_c(L,vlim->calculate,"odo",args,&rt,&rv))
            pa_log("failed to call calculate function");
        else {
            if (rt != MRP_FUNCBRIDGE_FLOATING)
                pa_log("accept function returned invalid type");
            else
                limit = rv.floating;
        }
    }

    return limit;
}

static bool calculate_bridge(lua_State *L, void *data, const char *signature,
                             mrp_funcbridge_value_t *args,
                             char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mir_volume_func_t calculate;
    scripting_vollim *vlim;
    scripting_node *ns;
    struct userdata *u;
    int class;
    mir_node *node;
    bool success;

    (void)L;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((calculate = (mir_volume_func_t)data));

    if (strcmp(signature, "odo"))
        success = false;
    else {
        pa_assert_se((vlim = args[0].pointer));
        pa_assert_se((u = vlim->userdata));
        pa_assert_se((ns = args[2].pointer));

        class = args[1].integer;

        pa_assert(class >= mir_application_class_begin &&
                  class <  mir_application_class_end);

        if (!(node = ns->node))
            success = false;
        else {
            success = true;
            *ret_type = MRP_FUNCBRIDGE_FLOATING;
            ret_val->floating = calculate(u, class, node, vlim->args);
        }
    }

    return success;
}

static limit_data_t *limit_data_check(lua_State *L, int idx)
{
    static double nolimit = 0.0;

    limit_data_t *ld;
    double value;

    switch (lua_type(L, idx)) {
    case LUA_TNUMBER:
        if ((value = lua_tonumber(L, idx)) > 0.0)
            luaL_error(L, "volume limit is in dB and can't be positive");
        else {
            ld = pa_xmalloc0(sizeof(limit_data_t));
            ld->mallocd = TRUE;
            ld->value = pa_xmalloc(sizeof(double));
            *ld->value = value;
        }
        break;
    default:
        ld->mallocd = FALSE;
        ld->value = &nolimit;
        break;
    }

    return ld;
}

static int limit_data_push(lua_State *L, limit_data_t *ld)
{
    if (ld)
        lua_pushnumber(L, *ld->value);
    else
        lua_pushnil;

    return 1;
}

static void limit_data_destroy(limit_data_t *ld)
{
    if (ld) {
        if (ld->mallocd)
            pa_xfree(ld->value);
        pa_xfree(ld);
    }
}


static intarray_t *intarray_check(lua_State *L, int idx, int min, int max)
{
    size_t len;
    size_t size;
    intarray_t *arr;
    int val;
    size_t i;

    idx = (idx < 0) ? lua_gettop(L) + idx + 1 : idx;

    luaL_checktype(L, idx, LUA_TTABLE);

    if ((len = luaL_getn(L, idx)) < 1)
        arr = NULL;
    else {
        size = sizeof(intarray_t) + sizeof(int) * len;
        arr  = pa_xmalloc0(sizeof(intarray_t));

        arr->nint = len;
        arr->ints = pa_xmalloc0(size);

        for (i = 0;  i < len;  i++) {
            lua_pushnumber(L, (int)(i+1));
            lua_gettable(L, idx);
            
            val = luaL_checkint(L, -1);
            
            lua_pop(L, 1);
            
            if (val < min || val >= max)
                luaL_error(L, "array [%u]: out of range value (%d)", i, val); 
            
            arr->ints[i] = val;
        }
    }

    return arr;
}

static int intarray_push(lua_State *L, intarray_t *arr)
{
    size_t i;

    if (!arr)
        lua_pushnil(L);
    else {
        lua_createtable(L, arr->nint, 0);

        for (i = 0;  i < arr->nint;  i++) {
            lua_pushinteger(L, (int)(i+1));
            lua_pushinteger(L, arr->ints[i]);
            lua_settable(L, -3);
        }
    }

    return 1;
}

static void intarray_destroy(intarray_t *arr)
{
    if (arr) {
        pa_xfree(arr->ints);
        pa_xfree(arr);
    }
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
        if (!strcmp(name, "limit"))
            return LIMIT;
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
        if (!strcmp(name, "calculate"))
            return CALCULATE;
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
    static const_def_t node_const[] = {
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

    static const_def_t vollim_const[] = {
        { "class"            , vollim_class         },
        { "generic"          , vollim_generic       },
        {       NULL         ,         0            }
    };

    const_def_t *cd;
    bool success = true;

    lua_getglobal(L, "node");

    if (!lua_istable(L, -1))
        success = false;
    else {
        for (cd = node_const;   cd->name;   cd++) {
            lua_pushstring(L, cd->name);
            lua_pushinteger(L, cd->value);
            lua_rawset(L, -3);
        }

        lua_pop(L, 1);
    }


    lua_getglobal(L, "volume_limit");

    if (!lua_istable(L, -1))
        success = false;
    else {
        for (cd = vollim_const;   cd->name;   cd++) {
            lua_pushstring(L, cd->name);
            lua_pushinteger(L, cd->value);
            lua_rawset(L, -3);
        }

        lua_pop(L, 1);
    }


    return success;
}


static bool register_methods(lua_State *L)
{
    static funcbridge_def_t funcbridge_defs[] = {
        {"accept_default" ,"oo" , accept_bridge   ,mir_router_default_accept },
        {"compare_default","ooo", compare_bridge  ,mir_router_default_compare},
        {"accept_phone"   ,"oo" , accept_bridge   ,mir_router_phone_accept   },
        {"compare_phone"  ,"ooo", compare_bridge  ,mir_router_phone_compare  },
        {"volume_supress" ,"odo", calculate_bridge,mir_volume_suppress       },
        {"volume_correct" ,"odo", calculate_bridge,mir_volume_correction     },
        {       NULL      , NULL,      NULL       ,         NULL             }
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