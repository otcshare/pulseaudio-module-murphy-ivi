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
#include <pulsecore/pulsecore-config.h>
#include <pulsecore/module.h>

#ifdef WITH_MURPHYIF
#include <murphy/common/macros.h>
#include <murphy/common/mainloop.h>
#include <murphy/pulse/pulse-glue.h>
#endif

#include "murphyif.h"


struct pa_domctl {
    const char           *addr;
#ifdef WITH_MURPHYIF
    mrp_mainloop_t       *ml;
    mrp_domctl_t         *ctl;
    int                   ntable;
    mrp_domctl_table_t   *tables;
    int                   nwatch;
    mrp_domctl_watch_t   *watches;
    pa_murphyif_watch_cb  watchcb;
#endif
};


#ifdef WITH_MURPHYIF
static void connect_notify(mrp_domctl_t *, int, int, const char *, void *);
static void watch_notify(mrp_domctl_t *, mrp_domctl_data_t *, int, void *);
static void dump_data(mrp_domctl_data_t *);
#endif



pa_domctl *pa_murphyif_init(struct userdata *u, const char *addr)
{
    pa_domctl *domctl;
#ifdef WITH_MURPHYIF
    mrp_mainloop_t *ml;

    if (!(ml = mrp_mainloop_pulse_get(u->core->mainloop))) {
        pa_log_error("Failed to set up murphy mainloop.");
        return NULL;
    }
#endif

    domctl = pa_xnew0(pa_domctl, 1);
    domctl->addr = pa_xstrdup(addr ? addr : MRP_DEFAULT_DOMCTL_ADDRESS);
#ifdef WITH_MURPHYIF
    domctl->ml = ml;
#endif

    return domctl;
}


void pa_murphyif_done(struct userdata *u)
{
    pa_domctl *domctl;

    if (u && (domctl = u->domctl)) {
#ifdef WITH_MURPHYIF
        mrp_domctl_table_t *t;
        mrp_domctl_watch_t *w;
        int i;

        mrp_domctl_destroy(domctl->ctl);
        mrp_mainloop_destroy(domctl->ml);

        if (domctl->ntable > 0 && domctl->tables) {
            for (i = 0;  i < domctl->ntable;  i++) {
                t = domctl->tables + i;
                pa_xfree((void *)t->table);
                pa_xfree((void *)t->mql_columns);
                pa_xfree((void *)t->mql_index);
            }
            pa_xfree(domctl->tables);
        }

        if (domctl->nwatch > 0 && domctl->watches) {
            for (i = 0;  i < domctl->nwatch;  i++) {
                w = domctl->watches + i;
                pa_xfree((void *)w->table);
                pa_xfree((void *)w->mql_columns);
                pa_xfree((void *)w->mql_where);
            }
            pa_xfree(domctl->watches);
        }
#endif
        pa_xfree((void *)domctl->addr);

        pa_xfree(domctl);
    }
}

void pa_murphyif_add_table(struct userdata *u,
                           const char *table,
                           const char *columns,
                           const char *index)
{
    pa_domctl *domctl;
    mrp_domctl_table_t *t;
    size_t size;
    size_t idx;
    
    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert_se((domctl = u->domctl));

    idx = domctl->ntable++;
    size = sizeof(mrp_domctl_table_t) * domctl->ntable;
    t = (domctl->tables = pa_xrealloc(domctl->tables, size)) + idx;

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
    pa_domctl *domctl;
    mrp_domctl_watch_t *w;
    size_t size;
    size_t idx;
    
    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert(max_rows > 0 && max_rows < MQI_QUERY_RESULT_MAX);
    pa_assert_se((domctl = u->domctl));

    idx = domctl->nwatch++;
    size = sizeof(mrp_domctl_watch_t) * domctl->nwatch;
    w = (domctl->watches = pa_xrealloc(domctl->watches, size)) + idx;

    w->table = pa_xstrdup(table);
    w->mql_columns = pa_xstrdup(columns);
    w->mql_where = where ? pa_xstrdup(where) : NULL;
    w->max_rows = max_rows;
}

void pa_murphyif_setup_domainctl(struct userdata *u, pa_murphyif_watch_cb wcb)
{
    static const char *name = "pulse";

    pa_domctl *domctl;

    pa_assert(u);
    pa_assert(wcb);
    pa_assert_se((domctl = u->domctl));

#ifdef WITH_MURPHYIF
    if (domctl->ntable || domctl->nwatch) {
        domctl->ctl = mrp_domctl_create(name, domctl->ml,
                                        domctl->tables, domctl->ntable,
                                        domctl->watches, domctl->nwatch,
                                        connect_notify, watch_notify, u);
        if (!domctl->ctl) {
            pa_log("failed to create '%s' domain controller", name);
            return;
        }

        if (!mrp_domctl_connect(domctl->ctl, domctl->addr, 0)) {
            pa_log("failed to conect to murphyd");
            return;
        }

        domctl->watchcb = wcb;
        pa_log_info("'%s' domain controller sucessfully created", name);
    }
#endif
}

#ifdef WITH_MURPHYIF
static void connect_notify(mrp_domctl_t *dc, int connected, int errcode,
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

static void watch_notify(mrp_domctl_t *dc, mrp_domctl_data_t *tables,
                         int ntable, void *user_data)
{
    struct userdata *u = (struct userdata *)user_data;
    pa_domctl *domctl;
    mrp_domctl_data_t *t;
    mrp_domctl_watch_t *w;
    int i;

    MRP_UNUSED(dc);

    pa_assert(tables);
    pa_assert(ntable > 0);
    pa_assert(u);
    pa_assert_se((domctl = u->domctl));

    pa_log_info("Received change notification for %d tables.", ntable);

    for (i = 0; i < ntable; i++) {
        t = tables + i;

        dump_data(t);

        pa_assert(t->id >= 0);
        pa_assert(t->id < domctl->nwatch);

        w = domctl->watches + t->id;

        domctl->watchcb(u, w->table, t->nrow, t->rows);
    }
}

static void dump_data(mrp_domctl_data_t *table)
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

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
