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
#include <string.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/proplist.h>
#include <pulsecore/core-util.h>
#include <pulsecore/module.h>

#include "volume.h"
#include "node.h"

#define VLIM_CLASS_ALLOC_BUCKET  8

typedef struct vlim_entry  vlim_entry;
typedef struct vlim_table  vlim_table;


struct vlim_entry {
    mir_volume_func_t func;      /**< volume limit function */
    void             *arg;       /**< arg given at registration time */
};

struct vlim_table {
    size_t       nentry;
    vlim_entry  *entries;
};

struct pa_mir_volume {
    int          classlen;   /**< class table length  */
    vlim_table  *classlim;   /**< class indexed table */
    vlim_table   genlim;     /**< generic limit */
};


static void add_to_table(vlim_table *, mir_volume_func_t, void *);
static void destroy_table(vlim_table *);
static double apply_table(double,vlim_table*, struct userdata *,int,mir_node*);

static void reset_outdated_volume_limit(mir_vlim *, uint32_t);


pa_mir_volume *pa_mir_volume_init(struct userdata *u)
{
    pa_mir_volume *volume = pa_xnew0(pa_mir_volume, 1);

    (void)u;

    return volume;
}

void pa_mir_volume_done(struct userdata *u)
{
    pa_mir_volume *volume;
    int i;

    if (u && (volume = u->volume)) {
        for (i = 0;   i < volume->classlen;   i++) {
            destroy_table(volume->classlim + i);
        }
        free(volume->classlim);

        destroy_table(&volume->genlim);

        pa_xfree(volume);

        u->volume = NULL;
    }
}

void mir_volume_add_class_limit(struct userdata  *u,
                                int               class,
                                mir_volume_func_t func,
                                void             *arg)
{
    pa_mir_volume *volume;
    vlim_table    *classlim;
    vlim_table    *table;
    size_t         newlen;
    size_t         size;
    size_t         diff;

    pa_assert(u);
    pa_assert(func);
    pa_assert(class > 0);
    pa_assert_se((volume = u->volume));

    if (class < volume->classlen)
        table = volume->classlim + class;
    else {
        newlen = class + 1;
        size = sizeof(vlim_table) * newlen;
        diff = sizeof(vlim_table) * (newlen - volume->classlen);

        pa_assert_se((classlim = realloc(volume->classlim, size)));
        memset(classlim + volume->classlen, 0, diff);


        volume->classlen = newlen;
        volume->classlim = classlim;

        table = classlim + class;
    }

    add_to_table(table, func, arg);
}


void mir_volume_add_generic_limit(struct userdata  *u,
                                  mir_volume_func_t func,
                                  void             *arg)
{
    pa_mir_volume *volume;

    pa_assert(u);
    pa_assert(func);
    pa_assert_se((volume = u->volume));

    add_to_table(&volume->genlim, func, arg);
}

void mir_volume_add_limiting_class(struct userdata *u,
                                   mir_node        *node,
                                   int              class,
                                   uint32_t         stamp)
{
    pa_mir_volume *volume;
    mir_vlim      *vlim;
    int           *classes;
    size_t         classes_size;
    size_t         i;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((volume = u->volume));
    pa_assert(class >= 0);

    if (node->implement == mir_device && node->direction == mir_output) {

        vlim = &node->vlim;

        reset_outdated_volume_limit(vlim, stamp);

        if (class < volume->classlen && volume->classlim[class].nentry > 0) {
            for (i = 0;   i < vlim->nclass;   i++) {
                if (class == vlim->classes[i])
                    return; /* it is already registered */
            }

            pa_log_debug("add limiting class %d (%s) to node '%s'",
                         class, mir_node_type_str(class), node->amname);

            if (vlim->nclass < vlim->maxentry)
                classes = vlim->classes;
            else {
                vlim->maxentry += VLIM_CLASS_ALLOC_BUCKET;
                classes_size    = sizeof(int *) * vlim->maxentry;
                vlim->classes   = realloc(vlim->classes, classes_size);

                pa_assert_se((classes = vlim->classes));
            }

            vlim->classes[vlim->nclass++] = class;
        }
    }
}


double mir_volume_apply_limits(struct userdata *u,
                               mir_node *node,
                               int class,
                               uint32_t stamp)
{
    pa_mir_volume *volume;
    mir_vlim *vlim;
    double attenuation = 0.0;
    vlim_table *tbl;
    size_t i;
    int c;

    pa_assert(u);
    pa_assert_se((volume = u->volume));

    if (class < 0 || class >= volume->classlen) {
        if (class < 0 || class >= mir_application_class_end)
            attenuation = -90.0;
    }
    else {
        /* generic limits */
        attenuation = apply_table(attenuation, &volume->genlim, u,class,node); 

        /* class-based limits */
        if (node && (vlim = &node->vlim) && stamp <= vlim->stamp) {
            for (i = 0;   i < vlim->nclass;   i++) {
                c = vlim->classes[i];
                
                pa_assert(c >= 0 && c < volume->classlen);
                tbl = volume->classlim + c;
                
                attenuation = apply_table(attenuation, tbl, u, class, node);
            }
        }
    }

    return attenuation;
}
                               

double mir_volume_suppress(struct userdata *u, int class, mir_node *node,
                           void *arg)
{
    mir_volume_suppress_arg *suppress = arg;
    size_t i;

    pa_assert(u);
    pa_assert(node);

    if (suppress) {
        for (i = 0;   i < suppress->exception.nclass;   i++) {
            if (suppress->exception.classes[i] == class)
                return 0.0;
        }

        return *suppress->attenuation;
    }

    return 0.0;
}

double mir_volume_correction(struct userdata *u, int class, mir_node *node,
                             void *arg)
{
    pa_assert(u);
    pa_assert(node);

    if (arg && node->implement == mir_device && node->privacy == mir_public)
        return **(double **)arg;

    return 0.0;
}

static void add_to_table(vlim_table *tbl, mir_volume_func_t func, void *arg)
{
    size_t      size;
    vlim_entry *entries;
    vlim_entry *entry;

    pa_assert(tbl);
    pa_assert(func);

    size = sizeof(vlim_entry) * (tbl->nentry + 1);
    pa_assert_se((entries = realloc(tbl->entries,  size)));
    entry = entries + tbl->nentry;

    entry->func = func;
    entry->arg  = arg;

    tbl->nentry += 1;
    tbl->entries = entries;
}

static void destroy_table(vlim_table *tbl)
{
    pa_assert(tbl);
    
    free(tbl->entries);
}

static double apply_table(double attenuation,
                          vlim_table *tbl,
                          struct userdata *u,
                          int class,
                          mir_node *node)
{
    static mir_node fake_node;

    double a;
    vlim_entry *e;
    size_t i;

    pa_assert(tbl);
    pa_assert(u);

    if (!node)
        node = &fake_node;

    for (i = 0;   i < tbl->nentry;  i++) {
        e = tbl->entries + i;
        a = e->func(u, class, node, e->arg);

        pa_log_debug("     limit = %.2lf", a);

        if (a < attenuation)
            attenuation = a;
    }

    return attenuation;
}



static void reset_outdated_volume_limit(mir_vlim *vl, uint32_t stamp)
{
    if (stamp > vl->stamp) {
        vl->nclass = 0;
        vl->stamp  = stamp;
    }
}





/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
