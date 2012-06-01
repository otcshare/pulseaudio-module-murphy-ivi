#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pulsecore/pulsecore-config.h>

#include "config.h"
#include "node.h"
#include "router.h"

typedef struct {
    const char            *name;
    mir_rtgroup_accept_t   accept;
    mir_rtgroup_compare_t  compare;
} rtgroup_def;

typedef struct {
    mir_node_type  class;
    const char    *rtgroup;
} classmap_def;

typedef struct {
    mir_node_type  class;
    int            priority;
} prior_def;


rtgroup_def  rtgroups[] = {
    {"default", mir_router_default_accept, mir_router_default_compare},
    {"phone"  , mir_router_phone_accept  , mir_router_phone_compare  },
    {   NULL  ,            NULL          ,              NULL         }
};

classmap_def classmap[] = {
    {mir_radio    , "default"},
    {mir_player   , "default"},
    {mir_navigator, "default"},
    {mir_game     , "default"},
    {mir_browser  , "default"},
    {mir_phone    , "phone"  },
    {mir_event    , "default"},
    {mir_node_type_unknown, NULL}
};

prior_def priormap[] = {
    {mir_radio    , 1},
    {mir_player   , 1},
    {mir_navigator, 2},
    {mir_game     , 3},
    {mir_browser  , 1},
    {mir_phone    , 4},
    {mir_event    , 5},
    {mir_node_type_unknown, 0}
};


static pa_bool_t use_default_configuration(struct userdata *);
static pa_bool_t parse_config_file(struct userdata *, FILE *);


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
    pa_mir_config *config;
    FILE *f;
    int success;

    pa_assert(u);
    pa_assert_se((config = u->config));

    if (path) {
        if ((f = fopen(path, "r"))) {
            success = parse_config_file(u, f);
            fclose(f);
            return success;
        }
        else {
            pa_log_info("%s: failed to open config file '%s': %s",
                        __FILE__, path, strerror(errno));            
        }
    }

    pa_log_debug("%s: default config values will apply", __FILE__);

    success = use_default_configuration(u);

    return success;
}

static pa_bool_t use_default_configuration(struct userdata *u)
{
    rtgroup_def  *r;
    classmap_def *c;
    prior_def    *p;

    pa_assert(u);

    for (r = rtgroups;  r->name;   r++)
        mir_router_create_rtgroup(u, r->name, r->accept, r->compare);

    for (c = classmap;  c->rtgroup;  c++)
        mir_router_assign_class_to_rtgroup(u, c->class, c->rtgroup);

    for (p = priormap;  p->class;  p++)
        mir_router_assign_class_priority(u, p->class, p->priority);

    return TRUE;
}

static pa_bool_t parse_config_file(struct userdata *u, FILE *f)
{
    return TRUE;
}
                                  
/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
