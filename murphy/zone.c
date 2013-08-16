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

#include <pulsecore/pulsecore-config.h>

#include <pulse/proplist.h>
#include <pulsecore/idxset.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/module.h>

#include "zone.h"


struct pa_zoneset {
    pa_hashmap     *zones;
};


static void free_zone_cb(void *);


pa_zoneset *pa_zoneset_init(struct userdata *u)
{
    pa_zoneset *zs;

    pa_assert(u);

    zs = pa_xnew0(pa_zoneset, 1);
    zs->zones = pa_hashmap_new(pa_idxset_string_hash_func,
                               pa_idxset_string_compare_func);

    return zs;
}


void pa_zoneset_done(struct userdata *u)
{
    pa_zoneset *zs;
    int i;

    if (u && (zs = u->zoneset)) {
        pa_hashmap_free(zs->zones, free_zone_cb);

        free(zs);
    }    
}

int pa_zoneset_add_zone(struct userdata *u, const char *name, uint32_t index)
{
    pa_zoneset *zs;
    mir_zone *zone;

    pa_assert(u);
    pa_assert(name);
    pa_assert_se((zs = u->zoneset));

    zone = pa_xnew0(mir_zone, 1);
    zone->name = pa_xstrdup(name);
    zone->index = index;

    return pa_hashmap_put(zs->zones, zone->name, zone);
}

mir_zone *pa_zoneset_get_zone(struct userdata *u, const char *name)
{
    pa_zoneset *zs;
    mir_zone *zone;

    pa_assert(u);
    pa_assert_se((zs = u->zoneset));

    if (name && zs->zones)
        zone = pa_hashmap_get(zs->zones, name);
    else
        zone = NULL;

    return zone;
}


void pa_zoneset_update_module_property(struct userdata *u)
{
    pa_module *module;
    pa_zoneset *zs;
    mir_zone *zone;
    void *state;
    char buf[4096];
    char *p, *e;

    pa_assert(u);
    pa_assert_se((module = u->module));
    pa_assert_se((zs = u->zoneset));
    pa_assert(zs->zones);

    e = (p = buf) + sizeof(buf);

    buf[1] = 0;
    
    PA_HASHMAP_FOREACH(zone, zs->zones, state) {
        if (p >= e) break;
        p += snprintf(p, e-p, " '%s'", zone->name);
    }

    pa_proplist_sets(module->proplist, PA_PROP_ZONES, buf+1); /* skip ' '@begining */
}

static void free_zone_cb(void *void_zone)
{
    mir_zone  *zone = (mir_zone *)void_zone;

    pa_xfree((void *)zone->name);

    pa_xfree(zone);
}


                                  



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
