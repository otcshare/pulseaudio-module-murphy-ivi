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
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include "murphy-config.h"
#include "node.h"
#include "router.h"
#include "volume.h"
#include "scripting.h"

typedef struct {
    mir_direction          type;
    const char            *name;
    mir_rtgroup_accept_t   accept;
    mir_rtgroup_compare_t  compare;
} rtgroup_def;

typedef struct {
    mir_node_type  class;
    mir_direction  type;
    const char    *rtgroup;
} classmap_def;

typedef struct {
    mir_node_type  class;
    int            priority;
} prior_def;


static rtgroup_def  rtgroups[] = {
    {mir_input,
     "phone",
     mir_router_phone_accept,
     mir_router_phone_compare
    },

    {mir_output,
     "default",
     mir_router_default_accept,
     mir_router_default_compare
    },

    {mir_output,
     "phone",
     mir_router_phone_accept,
     mir_router_phone_compare
    },

    {0,NULL,NULL,NULL}
};

static classmap_def classmap[] = {
    {mir_phone    , mir_input , "phone"  },

    {mir_radio    , mir_output, "default"},
    {mir_player   , mir_output, "default"},
    {mir_navigator, mir_output, "default"},
    {mir_game     , mir_output, "default"},
    {mir_browser  , mir_output, "default"},
    {mir_phone    , mir_output, "phone"  },
    {mir_event    , mir_output, "default"},
    {mir_node_type_unknown, mir_direction_unknown, NULL}
};

static prior_def priormap[] = {
    {mir_radio    , 1},
    {mir_player   , 1},
    {mir_navigator, 2},
    {mir_game     , 3},
    {mir_browser  , 1},
    {mir_phone    , 4},
    {mir_event    , 5},
    {mir_node_type_unknown, 0}
};

static double speedvol;
static double supprvol = -20.0;
static int exception_classes[] = {mir_phone, mir_navigator};
static mir_volume_suppress_arg suppress = {
    &supprvol, {DIM(exception_classes), exception_classes}
};


static pa_bool_t use_default_configuration(struct userdata *);

#if 0
static pa_bool_t parse_config_file(struct userdata *, FILE *);
#endif

pa_mir_config *pa_mir_config_init(struct userdata *u)
{
    pa_mir_config *config;

    pa_assert(u);

    config = pa_xnew0(pa_mir_config, 1);

    return config;
}

void pa_mir_config_done(struct userdata *u)
{
    pa_mir_config *config;

    if (u && (config = u->config)) {
        pa_xfree(config);
        u->config = NULL;
    }
}


pa_bool_t pa_mir_config_parse_file(struct userdata *u, const char *path)
{
    pa_module *module;
    pa_mir_config *config;
    int success;

    pa_assert(u);
    pa_assert_se((module = u->module));
    pa_assert_se((config = u->config));

    if (!path)
        success = FALSE;
    else {
        pa_log_info("%s: configuration file is '%s'", module->name, path);
        success =  pa_scripting_dofile(u, path);
    }

    if (!success) {
        pa_log_info("%s: builtin default configuration applies", module->name);
        success = use_default_configuration(u);
    }

    return success;
}

static pa_bool_t use_default_configuration(struct userdata *u)
{
    rtgroup_def  *r;
    classmap_def *c;
    prior_def    *p;

    pa_assert(u);

    for (r = rtgroups;  r->name;   r++)
        mir_router_create_rtgroup(u, r->type, r->name, r->accept, r->compare);

    for (c = classmap;  c->rtgroup;  c++)
        mir_router_assign_class_to_rtgroup(u, c->class, c->type, c->rtgroup);

    for (p = priormap;  p->class;  p++)
        mir_router_assign_class_priority(u, p->class, p->priority);


    mir_volume_add_generic_limit(u, mir_volume_correction, &speedvol);

    mir_volume_add_class_limit(u, mir_phone,mir_volume_suppress, &suppress);
    mir_volume_add_class_limit(u, mir_navigator,mir_volume_suppress,&suppress);


    return TRUE;
}

#if 0
static pa_bool_t parse_config_file(struct userdata *u, FILE *f)
{
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
