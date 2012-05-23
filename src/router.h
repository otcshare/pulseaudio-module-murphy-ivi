#ifndef foomirrouterfoo
#define foomirrouterfoo

#include <sys/types.h>

#include "userdata.h"
#include "list.h"

typedef pa_bool_t (*mir_rtgroup_accept_t)(struct userdata *, mir_rtgroup *,
                                          mir_node *);
typedef int       (*mir_rtgroup_compare_t)(struct userdata *u,
                                           mir_node *, mir_node *);

struct pa_router {
    pa_hashmap   *rtgroups;
    int           maplen;       /**< length of the class- and priormap */
    mir_rtgroup **classmap;     /**< to map device node types to rtgroups  */
    int          *priormap;     /**< stream node priorities */
    mir_dlist     nodlist;      /**< priorized list of the nodes  */
};


struct mir_rtentry {
    mir_dlist    link;        /**< rtgroup chain */
    mir_dlist    nodchain;    /**< node chain */
    mir_node    *node;        /**< pointer to the owning node */
    bool         blocked;     /**< weather this routing entry is active */
    uint32_t     stamp;
};

struct mir_rtgroup {
    char                 *name;     /**< name of the rtgroup  */
    mir_dlist             entries;  /**< listhead of ordered rtentries */
    mir_rtgroup_accept_t  accept;   /**< wheter to accept a node or not */
    mir_rtgroup_compare_t compare;  /**< comparision function for ordering */
};


pa_router *pa_router_init(struct userdata *);
void pa_router_done(struct userdata *);

pa_bool_t mir_router_create_rtgroup(struct userdata *, const char *,
                                    mir_rtgroup_accept_t,
                                    mir_rtgroup_compare_t);
void mir_router_destroy_rtgroup(struct userdata *, const char *);
pa_bool_t mir_router_assign_class_to_rtgroup(struct userdata *, mir_node_type,
                                             const char *);

void mir_router_register_node(struct userdata *, mir_node *);
void mir_router_unregister_node(struct userdata *, mir_node *);

mir_node *mir_router_make_prerouting(struct userdata *, mir_node *);
void mir_router_make_routing(struct userdata *);

int mir_router_print_rtgroups(struct userdata *, char *, int);

pa_bool_t mir_router_default_accept(struct userdata *, mir_rtgroup *,
                                    mir_node *);
int mir_router_default_compare(struct userdata *, mir_node *, mir_node *);


#endif  /* foomirrouterfoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
