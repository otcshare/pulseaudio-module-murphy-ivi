#ifndef foopolicygroupfoo
#define foopolicygroupfoo

#include <pulse/volume.h>
#include <pulsecore/sink.h>

#include "userdata.h"

#define PA_POLICY_GROUP_HASH_BITS 6
#define PA_POLICY_GROUP_HASH_DIM  (1 << PA_POLICY_GROUP_HASH_BITS)
#define PA_POLICY_GROUP_HASH_MASK (PA_POLICY_GROUP_HASH_DIM - 1)


#define PA_POLICY_GROUP_BIT(b)             (1UL << (b))
#define PA_POLICY_GROUP_FLAG_NONE          0
#define PA_POLICY_GROUP_FLAG_SET_SINK      PA_POLICY_GROUP_BIT(0)
#define PA_POLICY_GROUP_FLAG_SET_SOURCE    PA_POLICY_GROUP_BIT(1)
#define PA_POLICY_GROUP_FLAG_ROUTE_AUDIO   PA_POLICY_GROUP_BIT(2)
#define PA_POLICY_GROUP_FLAG_LIMIT_VOLUME  PA_POLICY_GROUP_BIT(3)
#define PA_POLICY_GROUP_FLAG_CORK_STREAM   PA_POLICY_GROUP_BIT(4)
#define PA_POLICY_GROUP_FLAG_MEDIA_NOTIFY  PA_POLICY_GROUP_BIT(5)
#define PA_POLICY_GROUP_FLAG_MUTE_BY_ROUTE PA_POLICY_GROUP_BIT(6)

#define PA_POLICY_GROUP_FLAGS_CLIENT      (PA_POLICY_GROUP_FLAG_LIMIT_VOLUME |\
                                           PA_POLICY_GROUP_FLAG_CORK_STREAM  )

#define PA_POLICY_GROUP_FLAGS_NOPOLICY     PA_POLICY_GROUP_FLAG_NONE

struct pa_sink_input_list {
    struct pa_sink_input_list    *next;
    uint32_t                      index;
    struct pa_sink_input         *sink_input;
};

struct pa_source_output_list {
    struct pa_source_output_list *next;
    uint32_t                      index;
    struct pa_source_output      *source_output;
};

struct pa_policy_group {
    struct pa_policy_group       *next;     /* hash link*/
    uint32_t                      flags;    /* or'ed PA_POLICY_GROUP_FLAG_x's*/
    char                         *name;     /* name of the policy group */
    char                         *sinkname; /* name of the default sink */
    char                         *portname; /* name of the default port */
    struct pa_sink               *sink;     /* default sink for the group */
    uint32_t                      sinkidx;  /* index of the default sink */
    char                         *srcname;  /* name of the default source */
    struct pa_source             *source;   /* default source fror the group */
    uint32_t                      srcidx;   /* index of the default source */
    pa_volume_t                   limit;    /* volume limit for the group */
    int                           locmute;  /* mute by local policy */
    int                           corked;
    int                           mutebyrt; /* muted by routing to null sink */
    struct pa_sink_input_list    *sinpls;   /* sink input list */
    struct pa_source_output_list *soutls;   /* source output list */
    int                           sinpcnt;  /* sink input counter */
    int                           soutcnt;  /* source output counter */
};

struct pa_policy_groupset {
    struct pa_policy_group    *dflt;     /*  default group */
    struct pa_policy_group    *hash_tbl[PA_POLICY_GROUP_HASH_DIM];
};

enum pa_policy_route_class {
    pa_policy_route_unknown = 0,
    pa_policy_route_to_sink,
    pa_policy_route_to_source,
    pa_policy_route_max
};


struct pa_policy_groupset *pa_policy_groupset_new(struct userdata *);
void pa_policy_groupset_free(struct pa_policy_groupset *);
void pa_policy_groupset_update_default_sink(struct userdata *, uint32_t);
void pa_policy_groupset_register_sink(struct userdata *, struct pa_sink *);
void pa_policy_groupset_unregister_sink(struct userdata *, uint32_t);
void pa_policy_groupset_register_source(struct userdata *, struct pa_source *);
void pa_policy_groupset_unregister_source(struct userdata *, uint32_t);
void pa_policy_groupset_create_default_group(struct userdata *, const char *);
int pa_policy_groupset_restore_volume(struct userdata *, struct pa_sink *);

struct pa_policy_group *pa_policy_group_new(struct userdata *, char*,
                                            char *, char *, uint32_t);
void pa_policy_group_free(struct pa_policy_groupset *, char *);
struct pa_policy_group *pa_policy_group_find(struct userdata *, char *);


void pa_policy_group_insert_sink_input(struct userdata *, char *,
                                       struct pa_sink_input *, uint32_t);
void pa_policy_group_remove_sink_input(struct userdata *, uint32_t);


void pa_policy_group_insert_source_output(struct userdata *, char *,
                                          struct pa_source_output *);
void pa_policy_group_remove_source_output(struct userdata *, uint32_t);

int  pa_policy_group_move_to(struct userdata *, char *,
                             enum pa_policy_route_class, char *,
                             char *, char *);
int  pa_policy_group_cork(struct userdata *u, char *, int);
int  pa_policy_group_volume_limit(struct userdata *, char *, uint32_t);
struct pa_policy_group *pa_policy_group_scan(struct pa_policy_groupset *,
                                             void **);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
