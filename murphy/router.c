#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/proplist.h>
#include <pulsecore/module.h>

#include "router.h"
#include "node.h"
#include "switch.h"


static void rtgroup_destroy(struct userdata *, mir_rtgroup *);
static int rtgroup_print(mir_rtgroup *, char *, int);
static void rtgroup_update_module_property(struct userdata *, mir_rtgroup *);

static void add_rtentry(struct userdata *, mir_rtgroup *, mir_node *);
static void remove_rtentry(struct userdata *, mir_rtentry *);

static void make_explicit_routes(struct userdata *, uint32_t);
static mir_node *find_default_route(struct userdata *, mir_node *);


static int uint32_cmp(uint32_t, uint32_t);

static int node_priority(struct userdata *, mir_node *);


static void pa_hashmap_rtgroup_free(void *rtg, void *u)
{
    rtgroup_destroy(u, rtg);
}


pa_router *pa_router_init(struct userdata *u)
{
    size_t     num_classes = mir_application_class_end;
    pa_router *router = pa_xnew0(pa_router, 1);
    
    router->rtgroups = pa_hashmap_new(pa_idxset_string_hash_func,
                                      pa_idxset_string_compare_func);
    router->maplen = num_classes;
    router->classmap = pa_xnew0(mir_rtgroup *, num_classes);
    router->priormap = pa_xnew0(int, num_classes);
    MIR_DLIST_INIT(router->nodlist);
    MIR_DLIST_INIT(router->connlist);
    
    return router;
}

void pa_router_done(struct userdata *u)
{
    pa_router      *router;
    mir_connection *conn, *c;
    mir_node       *e,*n;

    if (u && (router = u->router)) {
        MIR_DLIST_FOR_EACH_SAFE(mir_node, rtentries, e,n, &router->nodlist) {
            MIR_DLIST_UNLINK(mir_node, rtentries, e);
        }

        MIR_DLIST_FOR_EACH_SAFE(mir_connection,link, conn,c,&router->connlist){
            MIR_DLIST_UNLINK(mir_connection, link, conn);
            pa_xfree(conn);
        }

        pa_hashmap_free(router->rtgroups, pa_hashmap_rtgroup_free,u);

        pa_xfree(router->classmap);
        pa_xfree(router->priormap);
        pa_xfree(router);

        u->router = NULL;
    }
}


pa_bool_t mir_router_create_rtgroup(struct userdata      *u,
                                    const char           *name,
                                    mir_rtgroup_accept_t  accept,
                                    mir_rtgroup_compare_t compare)
{
    pa_router   *router;
    mir_rtgroup *rtg;

    pa_assert(u);
    pa_assert(name);
    pa_assert(accept);
    pa_assert(compare);
    pa_assert_se((router = u->router));

    rtg = pa_xnew0(mir_rtgroup, 1);
    rtg->name    = pa_xstrdup(name);
    rtg->accept  = accept;
    rtg->compare = compare;
    MIR_DLIST_INIT(rtg->entries);

    if (pa_hashmap_put(router->rtgroups, rtg->name, rtg) < 0) {
        pa_xfree(rtg->name);
        pa_xfree(rtg);
        return FALSE;
    }

    pa_log_debug("routing group '%s' created", name);

    return TRUE;
}

void mir_router_destroy_rtgroup(struct userdata *u, const char *name)
{
    pa_router *router;
    mir_rtgroup *rtg;

    pa_assert(u);
    pa_assert(name);
    pa_assert_se((router = u->router));

    if (!(rtg = pa_hashmap_remove(router->rtgroups, name)))
        pa_log_debug("can't destroy routing group '%s': group not found",name);
    else {
        rtgroup_destroy(u, rtg);
        pa_log_debug("routing group '%s' destroyed", name);
    }
}


pa_bool_t mir_router_assign_class_to_rtgroup(struct userdata *u,
                                             mir_node_type    class,
                                             const char      *rtgrpnam)
{
    pa_router *router;
    mir_rtgroup *rtg;
    const char * clnam;

    pa_assert(u);
    pa_assert(rtgrpnam);
    pa_assert_se((router = u->router));

    if (class < 0 || class >= router->maplen) {
        pa_log_debug("can't assign class (%d) to  routing group '%s': "
                     "class id is out of range (0 - %d)",
                     class, rtgrpnam, router->maplen);
        return FALSE;
    }

    clnam = mir_node_type_str(class);

    if (!(rtg = pa_hashmap_get(router->rtgroups, rtgrpnam))) {
        pa_log_debug("can't assign class '%s' to routing group '%s': "
                     "router group not found", clnam, rtgrpnam);
    }

    router->classmap[class] = rtg;

    pa_log_debug("class '%s' assigned to routing group '%s'", clnam, rtgrpnam);

    return TRUE;
}


void mir_router_register_node(struct userdata *u, mir_node *node)
{
    pa_router   *router;
    mir_rtgroup *rtg;
    void        *state;
    int          priority;
    mir_node    *before;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((router = u->router));
    
    if (node->implement == mir_device) {
        if (node->direction == mir_output) {
            PA_HASHMAP_FOREACH(rtg, router->rtgroups, state) {
                add_rtentry(u, rtg, node);
            }
        }
        return;
    }
    
    if (node->implement == mir_stream) {
        if (node->direction == mir_input) {
            priority = node_priority(u, node);
            
            MIR_DLIST_FOR_EACH(mir_node, rtentries, before, &router->nodlist) {
                if (priority < node_priority(u, before)) {
                    MIR_DLIST_INSERT_BEFORE(mir_node, rtentries, node,
                                            &before->rtentries);
                    return;
                }
            }
            
            MIR_DLIST_APPEND(mir_node, rtentries, node, &router->nodlist);
        }
        return;
    }
}

void mir_router_unregister_node(struct userdata *u, mir_node *node)
{
    pa_router *router;
    mir_rtentry *rte, *n;
    
    pa_assert(u);
    pa_assert(node);
    pa_assert_se((router = u->router));

    if (node->implement == mir_device && node->direction == mir_output) {
        MIR_DLIST_FOR_EACH_SAFE(mir_rtentry,nodchain, rte,n, &node->rtentries){
            remove_rtentry(u, rte);
        }
        return;
    }

    if (node->implement == mir_stream && node->direction == mir_input) {
        MIR_DLIST_UNLINK(mir_node, rtentries, node);
        return;
    }
}

mir_connection *mir_router_add_explicit_route(struct userdata *u,
                                              uint16_t   amid,
                                              mir_node  *from,
                                              mir_node  *to)
{
    pa_router *router;
    mir_connection *conn;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert_se((router = u->router));

    conn = pa_xnew0(mir_connection, 1);
    MIR_DLIST_INIT(conn->link);
    conn->amid = amid;
    conn->from = from->index;
    conn->to = to->index;
    
    MIR_DLIST_APPEND(mir_connection, link, conn, &router->connlist);

    mir_router_make_routing(u);

    return conn;
}

void mir_router_remove_explicit_route(struct userdata *u, mir_connection *conn)
{
    pa_core   *core;
    pa_router *router;
    mir_node  *from;
    mir_node  *to;

    pa_assert(u);
    pa_assert(conn);
    pa_assert_se((core = u->core));
    pa_assert_se((router = u->router));

    MIR_DLIST_UNLINK(mir_connection, link, conn);

    if (!(from = mir_node_find_by_index(u, conn->from)) ||
        !(to   = mir_node_find_by_index(u, conn->to))     )
    {
        pa_log_debug("can't remove explicit route: some node was not found");
    }
    else {
        pa_log_debug("tear down link '%s' => '%s'", from->amname, to->amname);

        if (!mir_switch_teardown_link(u, from, to)) {
            pa_log_debug("can't remove explicit route: "
                         "failed to teardown link");
        }
        else {
            if (!conn->blocked)
                mir_router_make_routing(u);
        }
    }

    pa_xfree(conn);
}

int mir_router_print_rtgroups(struct userdata *u, char *buf, int len)
{
    pa_router *router;
    mir_rtgroup *rtg;
    void *state;
    char *p, *e;

    pa_assert(u);
    pa_assert(buf);
    pa_assert(len > 0);
    pa_assert_se((router = u->router));
    pa_assert(router->rtgroups);

    e = (p = buf) + len;

    if (len > 0) {
        p += snprintf(p, e-p, "routing table:\n");

        if (p < e) {
            PA_HASHMAP_FOREACH(rtg, router->rtgroups, state) {
                if (p >= e) break;
                p += snprintf(p, e-p, "   %s:", rtg->name);
                
                if (p >= e) break;
                p += rtgroup_print(rtg, p, e-p);
                
                if (p >= e) break;
                p += snprintf(p, e-p, "\n");
            }
        }
    }

    return p - buf;
}


mir_node *mir_router_make_prerouting(struct userdata *u, mir_node *data)
{
    pa_router     *router;
    mir_node      *from;
    mir_node      *to;
    int            priority;
    pa_bool_t      done;
    mir_node      *target;
    uint32_t       stamp;

    pa_assert(u);
    pa_assert_se((router = u->router));
    pa_assert_se((data->implement == mir_stream));
    pa_assert_se((data->direction == mir_input));

    priority = node_priority(u, data);
    done = FALSE;
    target = NULL;
    stamp = pa_utils_new_stamp();

    make_explicit_routes(u, stamp);

    MIR_DLIST_FOR_EACH_BACKWARD(mir_node,rtentries, from, &router->nodlist) {
        if (priority >= node_priority(u, from)) {
            if ((target = find_default_route(u, data)))
                mir_switch_setup_link(u, NULL, target, FALSE);
            done = TRUE;
        }

        if (from->stamp >= stamp)
            continue;

        if ((to = find_default_route(u, from)))
            mir_switch_setup_link(u, from, to, FALSE);
    }    

    if (!done && (target = find_default_route(u, data)))
        mir_switch_setup_link(u, NULL, target, FALSE);

    return target;
}


void mir_router_make_routing(struct userdata *u)
{
    static pa_bool_t ongoing_routing;

    pa_router  *router;
    mir_node   *from;
    mir_node   *to;
    uint32_t    stamp;

    pa_assert(u);
    pa_assert_se((router = u->router));

    if (ongoing_routing)
        return;

    ongoing_routing = TRUE;
    stamp = pa_utils_new_stamp();

    make_explicit_routes(u, stamp);

    MIR_DLIST_FOR_EACH_BACKWARD(mir_node,rtentries, from, &router->nodlist) {
        if (from->stamp >= stamp)
            continue;

        if ((to = find_default_route(u, from)))
            mir_switch_setup_link(u, from, to, FALSE);
    }    

    ongoing_routing = FALSE;
}



pa_bool_t mir_router_default_accept(struct userdata *u, mir_rtgroup *rtg,
                                    mir_node *node)
{
    pa_bool_t accept;

    pa_assert(u);
    pa_assert(rtg);
    pa_assert(node);

    accept = (node->type >= mir_device_class_begin &&
              node->type < mir_device_class_end);
        
    return accept;
}


pa_bool_t mir_router_phone_accept(struct userdata *u, mir_rtgroup *rtg,
                                  mir_node *node)
{
    mir_node_type class;

    pa_assert(u);
    pa_assert(rtg);
    pa_assert(node);

    class = node->type;

    if (class >= mir_device_class_begin &&  class < mir_device_class_end) {
        if (class != mir_bluetooth_a2dp  &&
            class != mir_usb_headphone   &&
            class != mir_wired_headphone &&
            class != mir_hdmi            &&
            class != mir_spdif             )
        {
            return TRUE;
        }
    }

    return FALSE;
}


int mir_router_default_compare(struct userdata *u, mir_node *n1, mir_node *n2)
{
    uint32_t p1, p2;

    (void)u;

    pa_assert(n1);
    pa_assert(n2);

    if (n1->type == mir_null)
        return -1;
    if (n2->type == mir_null)
        return 1;

    p1 = ((((n1->channels & 31) << 5) + n1->privacy) << 2) + n1->location;
    p2 = ((((n2->channels & 31) << 5) + n2->privacy) << 2) + n2->location;

    p1 = (p1 << 8) + ((n1->type - mir_device_class_begin) & 0xff);
    p2 = (p2 << 8) + ((n2->type - mir_device_class_begin) & 0xff);

    return uint32_cmp(p1,p2);
}


int mir_router_phone_compare(struct userdata *u, mir_node *n1, mir_node *n2)
{
    uint32_t p1, p2;

    (void)u;

    pa_assert(n1);
    pa_assert(n2);

    if (n1->type == mir_null)
        return -1;
    if (n2->type == mir_null)
        return 1;

    p1 = (n1->privacy << 8) + ((n1->type - mir_device_class_begin) & 0xff);
    p2 = (n2->privacy << 8) + ((n2->type - mir_device_class_begin) & 0xff);

    return uint32_cmp(p1,p2);
}


static void rtgroup_destroy(struct userdata *u, mir_rtgroup *rtg)
{
    mir_rtentry *rte, *n;

    pa_assert(u);
    pa_assert(rtg);

    MIR_DLIST_FOR_EACH_SAFE(mir_rtentry, link, rte,n, &rtg->entries) {
        remove_rtentry(u, rte);
    }

    pa_xfree(rtg->name);
    pa_xfree(rtg);
}

static int rtgroup_print(mir_rtgroup *rtg, char *buf, int len)
{
    mir_rtentry *rte;
    mir_node *node;
    char *p, *e;

    e = (p = buf) + len;

    MIR_DLIST_FOR_EACH_BACKWARD(mir_rtentry, link, rte, &rtg->entries) {
        node = rte->node;
        if (p >= e)
            break;
        p += snprintf(p, e-p, " '%s'", node->amname);
    }

    return p - buf;
}

static void rtgroup_update_module_property(struct userdata *u,mir_rtgroup *rtg)
{
    pa_module *module;
    char       key[64];
    char       value[512];

    pa_assert(u);
    pa_assert(rtg);
    pa_assert_se((module = u->module));

    snprintf(key, sizeof(key), PA_PROP_ROUTING_TABLE ".%s", rtg->name);
    rtgroup_print(rtg, value, sizeof(value));

    pa_proplist_sets(module->proplist, key, value+1); /* skip ' '@beginning */
}

static void add_rtentry(struct userdata *u, mir_rtgroup *rtg, mir_node *node)
{
    pa_router *router;
    mir_rtentry *rte, *before;

    pa_assert(u);
    pa_assert(rtg);
    pa_assert(node);
    pa_assert_se((router = u->router));

    if (!rtg->accept(u, rtg, node)) {
        pa_log_debug("refuse node '%s' registration to routing group '%s'",
                     node->amname, rtg->name);
        return;
    }

    rte = pa_xnew0(mir_rtentry, 1);

    MIR_DLIST_APPEND(mir_rtentry, nodchain, rte, &node->rtentries);
    rte->node = node;

    MIR_DLIST_FOR_EACH(mir_rtentry, link, before, &rtg->entries) {
        if (rtg->compare(u, node, before->node) < 0) {
            MIR_DLIST_INSERT_BEFORE(mir_rtentry, link, rte, &before->link);
            goto added;
        }
    }

    MIR_DLIST_APPEND(mir_rtentry, link, rte, &rtg->entries);

 added:
    rtgroup_update_module_property(u, rtg);
    pa_log_debug("node '%s' added to routing group '%s'",
                 node->amname, rtg->name);
}

static void remove_rtentry(struct userdata *u, mir_rtentry *rte)
{
    pa_assert(u);
    pa_assert(rte);

    MIR_DLIST_UNLINK(mir_rtentry, link, rte);
    MIR_DLIST_UNLINK(mir_rtentry, nodchain, rte);

    pa_xfree(rte);
}

static void make_explicit_routes(struct userdata *u, uint32_t stamp)
{
    pa_router *router;
    mir_connection *conn;
    mir_node *from;
    mir_node *to;

    pa_assert(u);
    pa_assert_se((router = u->router));

    MIR_DLIST_FOR_EACH_BACKWARD(mir_connection,link, conn, &router->connlist) {
        if (conn->blocked)
            continue;
        
        if (!(from = mir_node_find_by_index(u, conn->from)) ||
            !(to   = mir_node_find_by_index(u, conn->to))     )
        {
            pa_log_debug("ignoring explicit route %u: some of the nodes "
                         "not found", conn->amid);
            continue;
        }

        if (!mir_switch_setup_link(u, from, to, TRUE))
            continue;

        if (from->implement == mir_stream)
            from->stamp = stamp;
    }
}


static mir_node *find_default_route(struct userdata *u, mir_node *from)
{
    pa_router     *router = u->router;
    mir_node_type  class  = from->type;
    mir_node      *to;
    mir_rtgroup   *rtg;
    mir_rtentry   *rte;


    if (class < 0 || class > router->maplen) {
        pa_log_debug("can't route '%s': class %d is out of range (0 - %d)",
                     from->amname, class, router->maplen);
        return NULL;
    }
    
    if (!(rtg = router->classmap[class])) {
        pa_log_debug("node '%s' won't be routed beacuse its class '%s' "
                     "is not assigned to any router group",
                     from->amname, mir_node_type_str(class));
        return NULL;
    }
    
    pa_log_debug("using '%s' router group when routing '%s'",
                 rtg->name, from->amname);

        
    MIR_DLIST_FOR_EACH_BACKWARD(mir_rtentry, link, rte, &rtg->entries) {
        if (!(to = rte->node)) {
            pa_log("   node was null in mir_rtentry");
            continue;
        }
        
        if (to->ignore) {
            pa_log_debug("   '%s' ignored. Skipping...",to->amname);
            continue;
        }

        if (!to->available) {
            pa_log_debug("   '%s' not available. Skipping...", to->amname);
            continue;
        }

        if (to->paidx == PA_IDXSET_INVALID) {
            if (to->type != mir_bluetooth_a2dp &&
                to->type != mir_bluetooth_sco)
            {
                pa_log_debug("   '%s' has no to. Skipping...", to->amname);
                continue;
            }
        }
        
        pa_log_debug("routing '%s' => '%s'", from->amname, to->amname);

        return to;
    }
    
    pa_log_debug("could not find route for '%s'", from->amname);

    return NULL;
}



static int uint32_cmp(uint32_t v1, uint32_t v2)
{
    if (v1 > v2)
        return 1;
    if (v1 < v2)
        return -1;
    return 0;
}


static int node_priority(struct userdata *u, mir_node *node)
{
    pa_router *router;
    mir_node_type type;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((router = u->router));
    pa_assert(router->priormap);

    type = node->type;

    if (type < 0 || type >= router->maplen)
        return 0;

    return router->priormap[type];
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
