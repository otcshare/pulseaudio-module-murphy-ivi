#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>

#include <pulsecore/core-util.h>

#include "userdata.h"
#include "audiomgr.h"
#include "node.h"
#include "discover.h"
#include "dbusif.h"

#define AUDIOMGR_DOMAIN   "PULSE"
#define AUDIOMGR_NODE     "pulsePlugin"

/*
 * these must match their counterpart
 * in audiomanagertypes.h
 */
/* domain status */
#define DS_UNKNOWN        0
#define DS_CONTROLLED     1
#define DS_RUNDOWN        2
#define DS_DOWN           255

/* interrupt state */
#define IS_OFF            1
#define IS_INTERRUPTED    2

/* availability status */
#define AS_AVAILABLE      1
#define AS_UNAVAILABLE    2

/* availability reason */
#define AR_NEWMEDIA       1
#define AR_SAMEMEDIA      2
#define AR_NOMEDIA        3
#define AR_TEMPERATURE    4
#define AR_VOLTAGE        5
#define AR_ERRORMEDIA     6

/* mute state */
#define MS_MUTED          1
#define MS_UNMUTED        2


typedef struct {
    const char *name;
    uint16_t    id;
    uint16_t    state;
} domain_t;


typedef struct pa_audiomgr {
    domain_t      domain;
    pa_hashmap   *nodes;
} pa_audiomgr;


static void *hash_key(mir_direction, uint16_t);


struct pa_audiomgr *pa_audiomgr_init(struct userdata *u)
{
    /* pa_module *m = u->module; */
    pa_audiomgr *am;
    
    am = pa_xnew0(pa_audiomgr, 1);

    am->domain.id = AM_ID_INVALID;
    am->domain.state = DS_DOWN;
    am->nodes = pa_hashmap_new(pa_idxset_trivial_hash_func,
                                     pa_idxset_trivial_compare_func);
    return am;
}

void pa_audiomgr_done(struct userdata *u)
{
    pa_audiomgr *am;

    if (u && (am = u->audiomgr)) {
        if (u->dbusif && am->domain.id != AM_ID_INVALID)
            pa_policy_dbusif_unregister_domain(u, am->domain.id);

        pa_hashmap_free(am->nodes, NULL,NULL);
        pa_xfree((void *)am->domain.name);
        pa_xfree(am);
    }
}


void pa_audiomgr_register_domain(struct userdata *u)
{
    pa_audiomgr        *am;
    am_domainreg_data  *dr;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    dr = pa_xnew0(am_domainreg_data, 1);

    dr->domain_id = 0;
    dr->name      = AUDIOMGR_DOMAIN;  /* AM domain name */
    dr->bus_name  = AUDIOMGR_NODE;    /* AM internal bus name. */
    dr->node_name = AUDIOMGR_NODE;    /* node name on AM's internal bus */
    dr->early     = FALSE;
    dr->complete  = FALSE;
    dr->state     = 1;

    pa_policy_dbusif_register_domain(u, dr);
}

void pa_audiomgr_domain_registered(struct userdata   *u,
                                   uint16_t           id,
                                   uint16_t           state, 
                                   am_domainreg_data *dr)
{
    pa_audiomgr *am;

    pa_assert(u);
    pa_assert(dr);
    pa_assert_se((am = u->audiomgr));


    am->domain.name  = pa_xstrdup(dr->name);
    am->domain.id    = id;
    am->domain.state = state;

    pa_log_debug("start domain registration for '%s' domain", dr->name);
    
    pa_discover_domain_up(u);
    
    pa_log_debug("domain registration for '%s' domain is complete", dr->name);

    pa_policy_dbusif_domain_complete(u, id);

    pa_xfree(dr);
}


void pa_audiomgr_unregister_domain(struct userdata *u, pa_bool_t send_state)
{
    pa_audiomgr *am;
    mir_node    *node;
    const void  *key;
    void        *state = NULL;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    pa_log_debug("unregistering domain '%s'", am->domain.name);

    while ((node  = pa_hashmap_iterate(am->nodes, &state, &key))) {
        pa_log_debug("   unregistering '%s' (%p/%p)", node->amname, key,node);
        node->amid = AM_ID_INVALID;
        pa_hashmap_remove(am->nodes, key);
    }

    am->domain.id = AM_ID_INVALID;
    am->domain.state = DS_DOWN;
}


void pa_audiomgr_register_node(struct userdata *u, mir_node *node)
{
    pa_audiomgr      *am;
    am_nodereg_data  *rd;
    const char       *method;
    pa_bool_t         success;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    if (am->domain.state == DS_DOWN || am->domain.state == DS_RUNDOWN)
        pa_log_debug("skip registering nodes while the domain is down");
    else {
        if (node->direction == mir_input || node->direction == mir_output) {
            rd = pa_xnew0(am_nodereg_data, 1);
            rd->key     = pa_xstrdup(node->key);
            rd->name    = pa_xstrdup(node->amname);
            rd->domain  = am->domain.id;
            rd->class   = 0x43;
            rd->volume  = 32767;
            rd->visible = node->visible;
            rd->avail.status = AS_AVAILABLE;
            rd->avail.reason = 0;
            rd->mainvol = 32767;
            
            if (node->direction == mir_input) {
                rd->interrupt = IS_OFF;
                method = AUDIOMGR_REGISTER_SOURCE;
            } 
            else {
                rd->mute = MS_UNMUTED;
                method = AUDIOMGR_REGISTER_SINK;
            }
            
            success = pa_policy_dbusif_register_node(u, method, rd);
            
            if (success) {
                pa_log_debug("initiate registration node '%s' (%p)"
                             "to audio manager", rd->name, node);
            }
            else {
                pa_log("%s: failed to register node '%s' (%p)"
                       "to audio manager", __FILE__, rd->name, node);
            }
        }
    }
}

void pa_audiomgr_node_registered(struct userdata *u,
                                 uint16_t         id,
                                 uint16_t         state,
                                 am_nodereg_data *rd)
{
    pa_audiomgr *am;
    mir_node    *node;
    void        *key;

    pa_assert(u);
    pa_assert(rd);
    pa_assert(rd->key);
    pa_assert_se((am = u->audiomgr));

    if (!(node = pa_discover_find_node(u, rd->key)))
        pa_log("%s: can't find node with key '%s'", __FILE__, rd->key);
    else {
        node->amid = id;

        key = hash_key(node->direction, id);

        pa_log_debug("registering node '%s' (%p/%p)",
                     node->amname, key, node);

        pa_hashmap_put(am->nodes, key, node);
    }

    pa_xfree((void *)rd->key);
    pa_xfree((void *)rd->name);
    pa_xfree((void *)rd);
}

void pa_audiomgr_unregister_node(struct userdata *u, mir_node *node)
{
    pa_audiomgr       *am;
    am_nodeunreg_data *ud;
    const char        *method;
    mir_node          *removed;
    pa_bool_t          success;
    void              *key;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    if (am->domain.state == DS_DOWN || am->domain.state == DS_RUNDOWN)
        pa_log_debug("skip unregistering nodes while the domain is down");
    else if (node->amid == AM_ID_INVALID)
        pa_log_debug("node '%s' was not registered", node->amname);
    else if (node->direction == mir_input || node->direction == mir_output) {
        ud = pa_xnew0(am_nodeunreg_data, 1);
        ud->id   = node->amid;
        ud->name = pa_xstrdup(node->amname);

        key = hash_key(node->direction, node->amid);
        removed = pa_hashmap_remove(am->nodes, key);

        if (node != removed) {
            if (removed)
                pa_log("%s: confused with data structures: key mismatch. "
                       "attempted to remove '%s' (%p/%p); "
                       "actually removed '%s' (%p/%p)", __FILE__,
                       node->amname, key, node, removed->amname,
                       hash_key(removed->direction, removed->amid), removed);
            else
                pa_log("%s: confused with data structures: node %u (%p)"
                       "is not in the hash table", __FILE__, node->amid, node);
        }
        
        
        if (node->direction == mir_input)
            method = AUDIOMGR_DEREGISTER_SOURCE;
        else
            method = AUDIOMGR_DEREGISTER_SINK;
        
        
        success = pa_policy_dbusif_unregister_node(u, method, ud);
        
        if (success) {
            pa_log_debug("sucessfully unregistered node '%s' (%p/%p)"
                         "from audio manager", node->amname, key, node);
        }
        else {
            pa_log("%s: failed to unregister node '%s' (%p)"
                   "from audio manager", __FILE__, node->amname, node);
        }
    }
}

void pa_audiomgr_node_unregistered(struct userdata   *u,
                                   am_nodeunreg_data *ud)
{
    (void)u;

    /* can't do too much here anyways,
       since the node is gone already */

    pa_xfree((void *)ud->name);
    pa_xfree((void *)ud);
}


void pa_audiomgr_connect(struct userdata *u, am_connect_data *cd)
{
    struct am_ack_data  ad;
    int err = E_OK;

    memset(&ad, 0, sizeof(ad));
    ad.handle = cd->handle;
    ad.param1 = cd->connection;
    ad.error  = err;

    pa_policy_dbusif_acknowledge(u, AUDIOMGR_CONNECT_ACK, &ad);
}

void pa_audiomgr_disconnect(struct userdata *u, am_connect_data *cd)
{
    struct am_ack_data  ad;
    int err = E_OK;

    memset(&ad, 0, sizeof(ad));
    ad.handle = cd->handle;
    ad.param1 = cd->connection;
    ad.error  = err;

    pa_policy_dbusif_acknowledge(u, AUDIOMGR_DISCONNECT_ACK, &ad);
}

static void *hash_key(mir_direction direction, uint16_t amid)
{
    return NULL + ((uint32_t)direction << 16 | (uint32_t)amid);
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */

