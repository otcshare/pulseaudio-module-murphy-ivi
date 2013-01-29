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
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <pulse/utf8.h>
#include <pulsecore/pulsecore-config.h>
#include <pulsecore/module.h>
#include <pulsecore/llist.h>
#include <pulsecore/idxset.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#ifdef WITH_MURPHYIF
#define WITH_DOMCTL
#define WITH_RESOURCES
#endif

#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
#include <murphy/common/macros.h>
#include <murphy/common/mainloop.h>
#include <murphy/pulse/pulse-glue.h>
#endif

#ifdef WITH_RESOURCES
#include <murphy/resource/protocol.h>
#include <murphy/common/transport.h>
#include <murphy/resource/protocol.h>
#include <murphy/resource/data-types.h>
#endif

#include "murphyif.h"
#include "node.h"

#ifdef WITH_RESOURCES
#define INVALID_ID      (~(uint32_t)0)
#define INVALID_INDEX   (~(uint32_t)0)
#define INVALID_SEQNO   (~(uint32_t)0)
#define INVALID_REQUEST (~(uint16_t)0)

#define PUSH_VALUE(msg, tag, typ, val) \
    mrp_msg_append(msg, MRP_MSG_TAG_##typ(RESPROTO_##tag, val))

#define PUSH_ATTRS(msg, rif, proplist)                  \
    resource_push_attributes(msg, rif, proplist)

typedef struct resource_attribute  resource_attribute;
typedef struct resource_request    resource_request;

struct resource_attribute {
    PA_LLIST_FIELDS(resource_attribute);
    const char *prop;
    mrp_attr_t  def;
};

struct resource_request {
    PA_LLIST_FIELDS(resource_request);
    uint32_t nodidx;
    uint16_t reqid;
    uint32_t seqno;
};

#endif

typedef struct {
    const char           *addr;
#ifdef WITH_DOMCTL
    mrp_domctl_t         *ctl;
    int                   ntable;
    mrp_domctl_table_t   *tables;
    int                   nwatch;
    mrp_domctl_watch_t   *watches;
    pa_murphyif_watch_cb  watchcb;
#endif
} domctl_interface;

typedef struct {
    const char      *addr;
    const char      *inpres;
    const char      *outres;
#ifdef WITH_RESOURCES
    mrp_transport_t *transp;
    mrp_sockaddr_t   saddr;
    socklen_t        alen;
    const char      *atype;
    pa_bool_t        connected;
    struct {
        uint32_t request;
        uint32_t reply;
    }                seqno;
    pa_hashmap      *nodes;
    PA_LLIST_HEAD(resource_attribute, attrs);
    PA_LLIST_HEAD(resource_request, reqs);
#endif
} resource_interface;


struct pa_murphyif {
#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
    mrp_mainloop_t *ml;
#endif
    domctl_interface domctl;
    resource_interface resource;
    pa_hashmap *nodes;
};


#ifdef WITH_DOMCTL
static void domctl_connect_notify(mrp_domctl_t *,int,int,const char *,void *);
static void domctl_watch_notify(mrp_domctl_t *,mrp_domctl_data_t *,int,void *);
static void domctl_dump_data(mrp_domctl_data_t *);
#endif

#ifdef WITH_RESOURCES
static void       resource_attribute_destroy(resource_interface *,
                                             resource_attribute *);
static pa_bool_t  resource_transport_connect(resource_interface *);
static void       resource_xport_closed_evt(mrp_transport_t *, int, void *);

static mrp_msg_t *resource_create_request(uint32_t, mrp_resproto_request_t);
static pa_bool_t  resource_send_message(resource_interface *, mrp_msg_t *,
                                        uint32_t, uint16_t, uint32_t);
static pa_bool_t  resource_set_create(struct userdata *, uint32_t,
                                      mir_direction, const char *,
                                      const char *, uint32_t, pa_proplist *);
static pa_bool_t  resource_set_destroy(struct userdata *, uint32_t);
static pa_bool_t  resource_set_acquire(struct userdata *, uint32_t, uint32_t);
static pa_bool_t  resource_push_attributes(mrp_msg_t *, resource_interface *,
                                           pa_proplist *);

static void       resource_recv_msg(mrp_transport_t *, mrp_msg_t *, void *);
static void       resource_recvfrom_msg(mrp_transport_t *, mrp_msg_t *,
                                        mrp_sockaddr_t *, socklen_t, void *);
static void       resource_set_create_response(struct userdata *, mir_node *,
                                               mrp_msg_t *, void **);

static pa_bool_t  resource_fetch_seqno(mrp_msg_t *, void **, uint32_t *);
static pa_bool_t  resource_fetch_request(mrp_msg_t *, void **, uint16_t *);
static pa_bool_t  resource_fetch_status(mrp_msg_t *, void **, int *);
static pa_bool_t  resource_fetch_rset_id(mrp_msg_t *, void **, uint32_t*);
static pa_bool_t  resource_fetch_rset_state(mrp_msg_t *, void **,
                                            mrp_resproto_state_t *);
static pa_bool_t  resource_fetch_rset_mask(mrp_msg_t *, void **,
                                           mrp_resproto_state_t *);
#endif


pa_murphyif *pa_murphyif_init(struct userdata *u,
                              const char *ctl_addr,
                              const char *res_addr)
{
#ifdef WITH_RESOURCES
    static mrp_transport_evt_t ev = {
        { .recvmsg     = resource_recv_msg },
        { .recvmsgfrom = resource_recvfrom_msg },
        .closed        = resource_xport_closed_evt,
        .connection    = NULL
    };
#endif

    pa_murphyif *murphyif;
    domctl_interface *dif;
    resource_interface *rif;
#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
    mrp_mainloop_t *ml;

    if (!(ml = mrp_mainloop_pulse_get(u->core->mainloop))) {
        pa_log_error("Failed to set up murphy mainloop.");
        return NULL;
    }
#endif
#ifdef WITH_RESOURCES
#endif

    murphyif = pa_xnew0(pa_murphyif, 1);
    dif = &murphyif->domctl;
    rif = &murphyif->resource;

#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
    murphyif->ml = ml;
#endif

    dif->addr = pa_xstrdup(ctl_addr ? ctl_addr:MRP_DEFAULT_DOMCTL_ADDRESS);
#ifdef WITH_DOMCTL
#endif

    rif->addr = pa_xstrdup(res_addr ? res_addr:RESPROTO_DEFAULT_ADDRESS);
#ifdef WITH_RESOURCES
    rif->alen = mrp_transport_resolve(NULL, rif->addr, &rif->saddr,
                                      sizeof(rif->saddr), &rif->atype);
    if (rif->alen <= 0) {
        pa_log("can't resolve resource transport address '%s'", rif->addr);
    }
    else {
        rif->transp = mrp_transport_create(murphyif->ml, rif->atype, &ev, u,0);

        if ((rif->transp))
            resource_transport_connect(rif);
        else
            pa_log("failed to create resource transport");
    }    

    rif->seqno.request = 1;
    rif->nodes = pa_hashmap_new(pa_idxset_trivial_hash_func,
                                pa_idxset_trivial_compare_func);
    PA_LLIST_HEAD_INIT(resource_attribute, rif->attrs);
    PA_LLIST_HEAD_INIT(resource_request, rif->reqs);
#endif

    murphyif->nodes = pa_hashmap_new(pa_idxset_trivial_hash_func,
                                     pa_idxset_trivial_compare_func);
    return murphyif;
}


void pa_murphyif_done(struct userdata *u)
{
    pa_murphyif *murphyif;
    domctl_interface *dif;
    resource_interface *rif;
#ifdef WITH_RESOURCES
    resource_attribute *attr, *a;
    resource_request *req, *r;
#endif

    if (u && (murphyif = u->murphyif)) {
#ifdef WITH_DOMCTL
        mrp_domctl_table_t *t;
        mrp_domctl_watch_t *w;
        int i;

        dif = &murphyif->domctl;
        rif = &murphyif->resource;

        mrp_domctl_destroy(dif->ctl);
        mrp_mainloop_destroy(murphyif->ml);

        if (dif->ntable > 0 && dif->tables) {
            for (i = 0;  i < dif->ntable;  i++) {
                t = dif->tables + i;
                pa_xfree((void *)t->table);
                pa_xfree((void *)t->mql_columns);
                pa_xfree((void *)t->mql_index);
            }
            pa_xfree(dif->tables);
        }

        if (dif->nwatch > 0 && dif->watches) {
            for (i = 0;  i < dif->nwatch;  i++) {
                w = dif->watches + i;
                pa_xfree((void *)w->table);
                pa_xfree((void *)w->mql_columns);
                pa_xfree((void *)w->mql_where);
            }
            pa_xfree(dif->watches);
        }
#endif

#ifdef WITH_RESOURCES
        pa_xfree((void *)rif->atype);
        pa_hashmap_free(rif->nodes, NULL, NULL);

        PA_LLIST_FOREACH_SAFE(attr, a, rif->attrs)
            resource_attribute_destroy(rif, attr);

        PA_LLIST_FOREACH_SAFE(req, r, rif->reqs)
            pa_xfree(req);
#endif

        pa_xfree((void *)dif->addr);
        pa_xfree((void *)rif->addr);

        pa_hashmap_free(murphyif->nodes, NULL, NULL);

        pa_xfree(murphyif);
    }
}



void pa_murphyif_add_table(struct userdata *u,
                           const char *table,
                           const char *columns,
                           const char *index)
{
    pa_murphyif *murphyif;
    domctl_interface *dif;
    mrp_domctl_table_t *t;
    size_t size;
    size_t idx;
    
    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

    idx = dif->ntable++;
    size = sizeof(mrp_domctl_table_t) * dif->ntable;
    t = (dif->tables = pa_xrealloc(dif->tables, size)) + idx;

    t->table = pa_xstrdup(table);
    t->mql_columns = pa_xstrdup(columns);
    t->mql_index = index ? pa_xstrdup(index) : NULL;
}

void pa_murphyif_add_watch(struct userdata *u,
                           const char *table,
                           const char *columns,
                           const char *where,
                           int max_rows)
{
    pa_murphyif *murphyif;
    domctl_interface *dif;
    mrp_domctl_watch_t *w;
    size_t size;
    size_t idx;
    
    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert(max_rows > 0 && max_rows < MQI_QUERY_RESULT_MAX);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

    idx = dif->nwatch++;
    size = sizeof(mrp_domctl_watch_t) * dif->nwatch;
    w = (dif->watches = pa_xrealloc(dif->watches, size)) + idx;

    w->table = pa_xstrdup(table);
    w->mql_columns = pa_xstrdup(columns);
    w->mql_where = where ? pa_xstrdup(where) : NULL;
    w->max_rows = max_rows;
}

void pa_murphyif_setup_domainctl(struct userdata *u, pa_murphyif_watch_cb wcb)
{
    static const char *name = "pulse";

    pa_murphyif *murphyif;
    domctl_interface *dif;

    pa_assert(u);
    pa_assert(wcb);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

#ifdef WITH_DOMCTL
    if (dif->ntable || dif->nwatch) {
        dif->ctl = mrp_domctl_create(name, murphyif->ml,
                                     dif->tables, dif->ntable,
                                     dif->watches, dif->nwatch,
                                     domctl_connect_notify,
                                     domctl_watch_notify, u);
        if (!dif->ctl) {
            pa_log("failed to create '%s' domain controller", name);
            return;
        }

        if (!mrp_domctl_connect(dif->ctl, dif->addr, 0)) {
            pa_log("failed to conect to murphyd");
            return;
        }

        dif->watchcb = wcb;
        pa_log_info("'%s' domain controller sucessfully created", name);
    }
#endif
}

void  pa_murphyif_add_audio_resource(struct userdata *u,
                                     mir_direction dir,
                                     const char *name)
{
    pa_murphyif *murphyif;
    resource_interface *rif;

    pa_assert(u);
    pa_assert(dir == mir_input || dir == mir_output);
    pa_assert(name);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    if (dir == mir_input) {
        if (rif->inpres)
            pa_log("attempt to register playback resource multiple time");
        else
            rif->inpres = pa_xstrdup(name);
    }
    else {
        if (rif->outres)
            pa_log("attempt to register recording resource multiple time");
        else
            rif->outres = pa_xstrdup(name);
    }
}

void pa_murphyif_add_audio_attribute(struct userdata *u,
                                     const char *propnam,
                                     const char *attrnam,
                                     mqi_data_type_t type,
                                     ... ) /* default value */
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;
    resource_interface *rif;
    resource_attribute *attr;
    mrp_attr_value_t *val;
    va_list ap;

    pa_assert(u);
    pa_assert(propnam);
    pa_assert(attrnam);
    pa_assert(type == mqi_string  || type == mqi_integer ||
              type == mqi_unsignd || type == mqi_floating);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    attr = pa_xnew0(resource_attribute, 1);
    val  = &attr->def.value;

    attr->prop = pa_xstrdup(propnam);
    attr->def.name = pa_xstrdup(attrnam);
    attr->def.type = type;

    va_start(ap, type);

    switch (type){
    case mqi_string:   val->string    = pa_xstrdup(va_arg(ap, char *));  break;
    case mqi_integer:  val->integer   = va_arg(ap, int32_t);             break;
    case mqi_unsignd:  val->unsignd   = va_arg(ap, uint32_t);            break;
    case mqi_floating: val->floating  = va_arg(ap, double);              break;
    default:           attr->def.type = mqi_error;                       break;
    }

    va_end(ap);

     if (attr->def.type == mqi_error)
         resource_attribute_destroy(rif, attr);
     else
         PA_LLIST_PREPEND(resource_attribute, rif->attrs, attr);
#endif
}

void pa_murphyif_create_resource_set(struct userdata *u, mir_node *node)
{
    pa_core *core;
    pa_murphyif *murphyif;
    resource_interface *rif;
    const char *class;
    uint32_t audio_flags = 0;
    pa_proplist *proplist = NULL;
    pa_sink_input *sinp;
    pa_source_output *sout;

    pa_assert(u);
    pa_assert(node);
    pa_assert(node->implement = mir_stream);
    pa_assert(node->direction == mir_input || node->direction == mir_output);
    pa_assert(node->zone);
    pa_assert(!node->rsetid);

    pa_assert_se((core = u->core));
    pa_assert_se((class = pa_nodeset_get_class(u, node->type)));

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    resource_transport_connect(rif);

    if (node->direction == mir_output) {
        if ((sout = pa_idxset_get_by_index(core->source_outputs, node->paidx)))
            proplist = sout->proplist;
    }
    else {
        if ((sinp = pa_idxset_get_by_index(core->sink_inputs, node->paidx)))
            proplist = sinp->proplist;
    }

    node->localrset = resource_set_create(u, node->index, node->direction,
                                          class, node->zone, audio_flags,
                                          proplist);
}

void pa_murphyif_destroy_resource_set(struct userdata *u, mir_node *node)
{
    pa_murphyif *murphyif;
    uint32_t rsetid;
    char *e;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((murphyif = u->murphyif));

    if (node->localrset && node->rsetid) {
        rsetid = strtoul(node->rsetid, &e, 10);

        if (e == node->rsetid || *e) {
            pa_log("can't destroy resource set: invalid rsetid '%s'",
                   node->rsetid);
        }
        else {
            if (resource_set_destroy(u, rsetid))
                pa_log_debug("resource set %u destruction request", rsetid);
            else {
                pa_log("falied to destroy resourse set %u for node '%s'",
                       rsetid, node->amname);
            }
        }

        pa_murphyif_delete_node(u, node);
    }
}

int pa_murphyif_add_node(struct userdata *u, mir_node *node)
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;

    pa_assert(u);
    pa_assert(node);
    pa_assert(node->implement == mir_stream);

    pa_assert_se((murphyif = u->murphyif));

    if (!node->rsetid) {
        pa_log("can't register resource set for node '%s'.: missing rsetid",
               node->amname);
    }
    else {
        if (pa_hashmap_put(murphyif->nodes, node->rsetid, node) == 0)
            return 0;
        else {
            pa_log("can't register resource set for node '%s': conflicting "
                   "resource id '%s'", node->amname, node->rsetid);
        } 
    }

    return -1;
#else
    return 0;
#endif
}

void pa_murphyif_delete_node(struct userdata *u, mir_node *node)
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;
    mir_node *deleted;

    pa_assert(u);
    pa_assert(node);
    pa_assert(node->implement == mir_stream);

    pa_assert_se((murphyif = u->murphyif));

    if (node->rsetid) {
        deleted = pa_hashmap_remove(murphyif->nodes, node->rsetid);
        pa_assert(deleted == node);
    }
#endif
}

mir_node *pa_murphyif_find_node(struct userdata *u, const char *rsetid)
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;
    mir_node *node;

    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    if (!rsetid)
        node = NULL;
    else
        node = pa_hashmap_get(murphyif->nodes, rsetid);

    return node;
#else
    return NULL;
#endif
}


#ifdef WITH_DOMCTL
static void domctl_connect_notify(mrp_domctl_t *dc, int connected, int errcode,
                                  const char *errmsg, void *user_data)
{
    MRP_UNUSED(dc);
    MRP_UNUSED(user_data);

    if (connected) {
        pa_log_info("Successfully registered to Murphy.");
    }
    else
        pa_log_error("Connection to Murphy failed (%d: %s).", errcode, errmsg);
}

static void domctl_watch_notify(mrp_domctl_t *dc, mrp_domctl_data_t *tables,
                                int ntable, void *user_data)
{
    struct userdata *u = (struct userdata *)user_data;
    pa_murphyif *murphyif;
    domctl_interface *dif;
    mrp_domctl_data_t *t;
    mrp_domctl_watch_t *w;
    int i;

    MRP_UNUSED(dc);

    pa_assert(tables);
    pa_assert(ntable > 0);
    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

    pa_log_info("Received change notification for %d tables.", ntable);

    for (i = 0; i < ntable; i++) {
        t = tables + i;

        domctl_dump_data(t);

        pa_assert(t->id >= 0);
        pa_assert(t->id < dif->nwatch);

        w = dif->watches + t->id;

        dif->watchcb(u, w->table, t->nrow, t->rows);
    }
}

static void domctl_dump_data(mrp_domctl_data_t *table)
{
    mrp_domctl_value_t *row;
    int                 i, j;
    char                buf[1024], *p;
    const char         *t;
    int                 n, l;

    pa_log_debug("Table #%d: %d rows x %d columns", table->id,
           table->nrow, table->ncolumn);

    for (i = 0; i < table->nrow; i++) {
        row = table->rows[i];
        p   = buf;
        n   = sizeof(buf);

        for (j = 0, t = ""; j < table->ncolumn; j++, t = ", ") {
            switch (row[j].type) {
            case MRP_DOMCTL_STRING:
                l  = snprintf(p, n, "%s'%s'", t, row[j].str);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_INTEGER:
                l  = snprintf(p, n, "%s%d", t, row[j].s32);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_UNSIGNED:
                l  = snprintf(p, n, "%s%u", t, row[j].u32);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_DOUBLE:
                l  = snprintf(p, n, "%s%f", t, row[j].dbl);
                p += l;
                n -= l;
                break;
            default:
                l  = snprintf(p, n, "%s<invalid column 0x%x>",
                              t, row[j].type);
                p += l;
                n -= l;
            }
        }

        pa_log_debug("row #%d: { %s }", i, buf);
    }
}
#endif

#ifdef WITH_RESOURCES
static void resource_attribute_destroy(resource_interface *rif,
                                       resource_attribute *attr)
{
    if (attr) {
       if (rif)
           PA_LLIST_REMOVE(resource_attribute, rif->attrs, attr);

       pa_xfree((void *)attr->prop);
       pa_xfree((void *)attr->def.name);

       if (attr->def.type == mqi_string)
           pa_xfree((void *)attr->def.value.string);

       pa_xfree(attr);
    }
}

static pa_bool_t resource_transport_connect(resource_interface *rif)
{
    pa_assert(rif);

    if (!rif->connected) {
        if (mrp_transport_connect(rif->transp, &rif->saddr, rif->alen)) {
            pa_log_info("resource transport connected to '%s'", rif->addr);
            rif->connected = TRUE;
        }
        else {
            pa_log("can't connect resource transport to '%s'", rif->addr);
            return FALSE;
        }
    }

    return TRUE;
}

static void resource_xport_closed_evt(mrp_transport_t *transp, int error,
                                      void *void_u)
{
    struct userdata *u = (struct userdata *)void_u;
    pa_murphyif *murphyif;
    resource_interface *rif;

    MRP_UNUSED(transp);

    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if (!error)
        pa_log("peer has closed the resource transport connection");
    else {
        pa_log("resource transport connection closed with error %d (%s)",
               error, strerror(error));
    }

    rif->connected = FALSE;
}

static mrp_msg_t *resource_create_request(uint32_t seqno,
                                          mrp_resproto_request_t req)
{
    uint16_t   type  = req;
    mrp_msg_t *msg;

    msg = mrp_msg_create(RESPROTO_SEQUENCE_NO , MRP_MSG_FIELD_UINT32, seqno,
                         RESPROTO_REQUEST_TYPE, MRP_MSG_FIELD_UINT16, type ,
                         RESPROTO_MESSAGE_END                               );

    if (!msg)
        pa_log("can't to create new resource message");
 
    return msg;
}

static pa_bool_t resource_send_message(resource_interface *rif,
                                       mrp_msg_t          *msg,
                                       uint32_t            nodidx,
                                       uint16_t            reqid,
                                       uint32_t            seqno)
{
    resource_request *req;
    pa_bool_t success = TRUE;

    if (!mrp_transport_send(rif->transp, msg)) {
        pa_log("failed to send resource message");
        success = FALSE;
    }
    else {
        req = pa_xnew0(resource_request, 1);
        req->nodidx = nodidx;
        req->reqid  = reqid;
        req->seqno  = seqno;

        PA_LLIST_PREPEND(resource_request, rif->reqs, req);
    }

    mrp_msg_unref(msg);

    return success;
}


static pa_bool_t resource_set_create(struct userdata *u,
                                     uint32_t nodidx,
                                     mir_direction dir,
                                     const char *class,
                                     const char *zone,
                                     uint32_t audio_flags,
                                     pa_proplist *proplist)
{
    static uint32_t rset_flags = 0 /* RESPROTO_RSETFLAG_AUTORELEASE */ ;

    pa_murphyif *murphyif;
    resource_interface *rif;
    resource_request *req;
    mrp_msg_t *msg;
    uint16_t reqid;
    uint32_t seqno;
    const char *resnam;
    pa_bool_t success = TRUE;

    pa_assert(u);
    pa_assert(nodidx != PA_IDXSET_INVALID);
    pa_assert(dir == mir_input || dir == mir_output);
    pa_assert(class);
    pa_assert(zone);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    reqid  = RESPROTO_CREATE_RESOURCE_SET;
    seqno  = rif->seqno.request++;
    resnam = (dir == mir_input) ? rif->inpres : rif->outres;

    pa_assert(resnam);

    msg = resource_create_request(seqno, reqid);

    if (PUSH_VALUE(msg,   RESOURCE_FLAGS   , UINT32, rset_flags)  &&
        PUSH_VALUE(msg,   RESOURCE_PRIORITY, UINT32, 0)           &&
        PUSH_VALUE(msg,   CLASS_NAME       , STRING, class)       &&
        PUSH_VALUE(msg,   ZONE_NAME        , STRING, zone)        &&
        PUSH_VALUE(msg,   RESOURCE_NAME    , STRING, resnam)      &&
        PUSH_VALUE(msg,   RESOURCE_FLAGS   , UINT32, audio_flags) &&
        PUSH_ATTRS(msg,   rif, proplist)                          &&
        PUSH_VALUE(msg,   SECTION_END      , UINT8 , 0)            )
    {
        success = resource_send_message(rif, msg, nodidx, reqid, seqno);
    }
    else {
        success = FALSE;
        mrp_msg_unref(msg);
    }

    return success;
}

static pa_bool_t resource_set_destroy(struct userdata *u, uint32_t rsetid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    mrp_msg_t *msg;
    uint16_t reqid;
    uint32_t seqno;
    uint32_t nodidx;
    pa_bool_t success;

    pa_assert(u);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    reqid = RESPROTO_DESTROY_RESOURCE_SET;
    seqno = rif->seqno.request++;
    nodidx = PA_IDXSET_INVALID;
    msg = resource_create_request(seqno, reqid);

    if (PUSH_VALUE(msg, RESOURCE_SET_ID, UINT32, rsetid))
        success = resource_send_message(rif, msg, nodidx, reqid, seqno);
    else {
        success = FALSE;
        mrp_msg_unref(msg);
    }

    return success;
}

static pa_bool_t resource_set_acquire(struct userdata *u,
                                      uint32_t nodidx,
                                      uint32_t rsetid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    mrp_msg_t *msg;
    uint16_t reqid;
    uint32_t seqno;
    pa_bool_t success;

    pa_assert(u);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    reqid = RESPROTO_ACQUIRE_RESOURCE_SET;
    seqno = rif->seqno.request++;
    msg = resource_create_request(seqno, reqid);

    if (PUSH_VALUE(msg, RESOURCE_SET_ID, UINT32, rsetid))
        success = resource_send_message(rif, msg, nodidx, reqid, seqno);
    else {
        success = FALSE;
        mrp_msg_unref(msg);
    }

    return success;
}

static pa_bool_t resource_push_attributes(mrp_msg_t *msg,
                                          resource_interface *rif,
                                          pa_proplist *proplist)
{
    resource_attribute *attr;
    union {
        const void *ptr;
        const char *str;
        int32_t    *i32;
        uint32_t   *u32;
        double     *dbl;
    } v;
    size_t size;
    int sts;

    pa_assert(msg);
    pa_assert(rif);

    PA_LLIST_FOREACH(attr, rif->attrs) {
        if (!PUSH_VALUE(msg, ATTRIBUTE_NAME, STRING, attr->def.name))
            return FALSE;

        if (proplist)
            sts = pa_proplist_get(proplist, attr->prop, &v.ptr, &size);
        else
            sts = -1;

        switch (attr->def.type) {
        case mqi_string:
            if (sts < 0)
                v.str = attr->def.value.string;
            else if (v.str[size-1] != '\0' || strlen(v.str) != (size-1) ||
                     !pa_utf8_valid(v.str))
                return FALSE;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, STRING, v.str))
                return FALSE;
            break;

        case mqi_integer:
            if (sts < 0)
                v.i32 = &attr->def.value.integer;
            else if (size != sizeof(*v.i32))
                return FALSE;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, SINT8, *v.i32))
                return FALSE;
            break;
            
        case mqi_unsignd:
            if (sts < 0)
                v.u32 = &attr->def.value.unsignd;
            else if (size != sizeof(*v.u32))
                return FALSE;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, SINT8, *v.u32))
                return FALSE;
            break;
            
        case mqi_floating:
            if (sts < 0)
                v.dbl = &attr->def.value.floating;
            else if (size != sizeof(*v.dbl))
                return FALSE;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, SINT8, *v.dbl))
                return FALSE;
            break;

        default: /* we should never get here */
            return FALSE;
        }
    }

    return TRUE;
}



static void resource_recv_msg(mrp_transport_t *t, mrp_msg_t *msg, void *void_u)
{
    return resource_recvfrom_msg(t, msg, NULL, 0, void_u);
}

static void resource_recvfrom_msg(mrp_transport_t *transp, mrp_msg_t *msg,
                                  mrp_sockaddr_t *addr, socklen_t addrlen,
                                  void *void_u)
{
    struct userdata *u = (struct userdata *)void_u;
    pa_core *core;
    pa_murphyif *murphyif;
    resource_interface *rif;
    void     *curs = NULL;
    uint32_t  seqno;
    uint16_t  reqid;
    uint32_t  nodidx;
    resource_request *req, *n;
    mir_node *node;

    MRP_UNUSED(transp);
    MRP_UNUSED(addr);
    MRP_UNUSED(addrlen);

    pa_assert(u);
    pa_assert_se((core = u->core));
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if (!resource_fetch_seqno   (msg, &curs, &seqno) ||
        !resource_fetch_request (msg, &curs, &reqid)   )
    {
        pa_log("ignoring malformed message");
        return;
    }

    PA_LLIST_FOREACH_SAFE(req, n, rif->reqs) {
        if (req->seqno <= seqno) {
            nodidx = req->nodidx;
            
            if (req->reqid == reqid) {
                PA_LLIST_REMOVE(resource_request, rif->reqs, req);
                pa_xfree(req);
            }
            
            if (!(node = mir_node_find_by_index(u, nodidx))) {
                if (reqid != RESPROTO_DESTROY_RESOURCE_SET) {
                    pa_log("got response (reqid:%u seqno:%u) but can't "
                           "find the corresponding node", reqid, seqno);
                }
            }
            else {
                if (req->seqno < seqno) {
                    pa_log("unanswered request %d", req->seqno);
                }
                else {
                    pa_log_debug("got response (reqid:%u seqno:%u "
                                 "node:'%s')", reqid, seqno,
                                 node ? node->amname : "<unknown>");
                    
                    switch (reqid) {
                    case RESPROTO_CREATE_RESOURCE_SET:
                        resource_set_create_response(u,node,msg,&curs);
                        break;
#if 0
                    case RESPROTO_ACQUIRE_RESOURCE_SET:
                        resource_set_acquire_response(u,node,msg,&curs);
                        break;
                    case RESPROTO_RELEASE_RESOURCE_SET:
                        resource_set_release_response(u,node,msg,&curs);
                        break;
                    case RESPROTO_RESOURCES_EVENT:
                        resource_event(u, seqno, msg, &curs);
                        break;
#endif
                    default:
                        pa_log("ignoring unsupported resource request "
                               "type %u", reqid);
                        break;
                    }
                }
            }
        } /* PA_LLIST_FOREACH_SAFE */
    }
}

static void resource_set_create_response(struct userdata *u, mir_node *node,
                                         mrp_msg_t *msg, void **pcursor)
{
    int status;
    uint32_t rsetid;
    char buf[4096];

    pa_assert(u);
    pa_assert(node);
    pa_assert(msg);
    pa_assert(pcursor);

    if (!resource_fetch_status(msg, pcursor, &status) || (status == 0 &&
        !resource_fetch_rset_id(msg, pcursor, &rsetid)))
    {
        pa_log("ignoring malformed response to resource set creation");
        return;
    }

    if (status) {
        pa_log("creation of resource set failed. error code %u", status);
        return;
    }

    node->rsetid = pa_sprintf_malloc("%d", rsetid);
                
    if (pa_murphyif_add_node(u, node) == 0) {
        pa_log_debug("resource set was successfully created");
        mir_node_print(node, buf, sizeof(buf));
        pa_log_debug("modified node:\n%s", buf);

        if (resource_set_acquire(u, node->index, rsetid))
            pa_log_debug("acquire request sent");
        else
            pa_log("failed to send acquire request");
    }
    else {
        pa_log("failed to create resource set: "
               "conflicting resource set id");
    }
}


static pa_bool_t resource_fetch_seqno(mrp_msg_t *msg,
                                      void **pcursor,
                                      uint32_t *pseqno)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_SEQUENCE_NO || type != MRP_MSG_FIELD_UINT32)
    {
        *pseqno = INVALID_SEQNO;
        return false;
    }

    *pseqno = value.u32;
    return true;
}


static pa_bool_t resource_fetch_request(mrp_msg_t *msg,
                                        void **pcursor,
                                        uint16_t *preqtype)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_REQUEST_TYPE || type != MRP_MSG_FIELD_UINT16)
    {
        *preqtype = INVALID_REQUEST;
        return false;
    }

    *preqtype = value.u16;
    return true;
}

static pa_bool_t resource_fetch_status(mrp_msg_t *msg,
                                       void **pcursor,
                                       int *pstatus)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_REQUEST_STATUS || type != MRP_MSG_FIELD_SINT16)
    {
        *pstatus = EINVAL;
        return FALSE;
    }

    *pstatus = value.s16;
    return TRUE;
}

static pa_bool_t resource_fetch_rset_id(mrp_msg_t *msg,
                                        void **pcursor,
                                        uint32_t *pid)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_RESOURCE_SET_ID || type != MRP_MSG_FIELD_UINT32)
    {
        *pid = INVALID_ID;
        return FALSE;
    }

    *pid = value.u32;
    return TRUE;
}

static pa_bool_t resource_fetch_rset_state(mrp_msg_t *msg,
                                           void **pcursor,
                                           mrp_resproto_state_t *pstate)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_RESOURCE_STATE || type != MRP_MSG_FIELD_UINT16)
    {
        *pstate = 0;
        return FALSE;
    }

    *pstate = value.u16;
    return TRUE;
}


static pa_bool_t resource_fetch_rset_mask(mrp_msg_t *msg,
                                          void **pcursor,
                                          mrp_resproto_state_t *pmask)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_RESOURCE_GRANT || type != MRP_MSG_FIELD_UINT32)
    {
        *pmask = 0;
        return FALSE;
    }

    *pmask = value.u32;
    return TRUE;
}


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
