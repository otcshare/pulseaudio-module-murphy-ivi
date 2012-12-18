#include <pulsecore/pulsecore-config.h>
#include <pulsecore/module.h>

#ifdef WITH_MURPHYIF
#include <murphy/common/macros.h>
#include <murphy/common/mainloop.h>
#include <murphy/pulse/pulse-glue.h>
#include <murphy/domain-control/client.h>

#include "murphyif.h"

#define PLAYBACK_TABLE    "devices"
#define PLAYBACK_COLUMNS  "*"
#define PLAYBACK_FILTER   NULL
#define RECORDING_TABLE   "audio_recording_owner"
#define RECORDING_COLUMNS "*"
#define RECORDING_FILTER  NULL


static mrp_domctl_watch_t audio_tables[] = {
    MRP_DOMCTL_WATCH(PLAYBACK_TABLE , PLAYBACK_COLUMNS , PLAYBACK_FILTER , 0),
    MRP_DOMCTL_WATCH(RECORDING_TABLE, RECORDING_COLUMNS, RECORDING_FILTER, 0),
};


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


static void watch_notify(mrp_domctl_t *dc, mrp_domctl_data_t *tables,
                         int ntable, void *user_data)
{
    int i;

    MRP_UNUSED(dc);
    MRP_UNUSED(user_data);

    pa_log_info("Received change notification for %d tables.", ntable);

    for (i = 0; i < ntable; i++)
        dump_data(tables + i);
}


pa_domctl *pa_murphyif_init(struct userdata *u, const char *addr)
{
    pa_domctl          *domctl;
    mrp_mainloop_t     *ml;
    mrp_domctl_t       *ctl;
    mrp_domctl_table_t *tables;
    int                 ntable;
    mrp_domctl_watch_t *watches;
    int                 nwatch;

    ml = mrp_mainloop_pulse_get(u->core->mainloop);

    if (ml == NULL) {
        pa_log_error("Failed to set up murphy mainloop.");
        return NULL;
    }

    if (addr == NULL)
        addr = MRP_DEFAULT_DOMCTL_ADDRESS;

    domctl  = pa_xnew0(pa_domctl, 1);
    tables  = NULL;
    ntable  = 0;
    watches = audio_tables;
    nwatch  = MRP_ARRAY_SIZE(audio_tables);

    ctl = mrp_domctl_create("pulse", ml, tables, ntable, watches, nwatch,
                            connect_notify, watch_notify, u);

    if (ctl != NULL) {
        if (mrp_domctl_connect(ctl, addr, 0)) {
            pa_log_info("Murphy domain controller created.");

            domctl->ml  = ml;
            domctl->ctl = ctl;

            return domctl;
        }
        else
            pa_log_error("Connection attempt to murphy daemon failed.");
    }
    else
        pa_log_error("Failed to create murphy domain controller context.");

    mrp_domctl_destroy(ctl);
    mrp_mainloop_destroy(ml);
    pa_xfree(domctl);

    return NULL;
}


void pa_murphyif_done(struct userdata *u)
{
    if (u->domctl != NULL) {
        mrp_domctl_destroy(u->domctl->ctl);
        mrp_mainloop_destroy(u->domctl->ml);

        pa_xfree(u->domctl);
        u->domctl = NULL;
    }
}

#else /* !WITH_MURPHYIF */

#include "murphyif.h"

pa_domctl *pa_murphyif_init(struct userdata *u, const char *addr)
{
    (void)u;
    (void)addr;

    return NULL;
}


void pa_murphyif_done(struct userdata *u)
{
    (void)u;
}

#endif /* !WITH_MURPHYIF */
