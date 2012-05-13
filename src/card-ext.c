#include <pulsecore/pulsecore-config.h>

#include <pulse/def.h>
#include <pulsecore/card.h>

#include "card-ext.h"
#include "classify.h"
#include "context.h"

/* this included for the sake of pa_policy_send_device_state()
   which is temporarily hosted by sink-ext.c*/
#include "sink-ext.h"


/* hooks */
static pa_hook_result_t card_put(void *, void *, void *);
static pa_hook_result_t card_unlink(void *, void *, void *);

static void handle_new_card(struct userdata *, struct pa_card *);
static void handle_removed_card(struct userdata *, struct pa_card *);


struct pa_card_evsubscr *pa_card_ext_subscription(struct userdata *u)
{
    pa_core                 *core;
    pa_hook                 *hooks;
    struct pa_card_evsubscr *subscr;
    pa_hook_slot            *put;
    pa_hook_slot            *unlink;

    pa_assert(u);
    pa_assert_se((core = u->core));

    hooks  = core->hooks;
    
    put    = pa_hook_connect(hooks + PA_CORE_HOOK_CARD_PUT,
                             PA_HOOK_LATE, card_put, (void *)u);
    unlink = pa_hook_connect(hooks + PA_CORE_HOOK_CARD_UNLINK,
                             PA_HOOK_LATE, card_unlink, (void *)u);
    

    subscr = pa_xnew0(struct pa_card_evsubscr, 1);
    
    subscr->put    = put;
    subscr->unlink = unlink;

    return subscr;


}

void pa_card_ext_subscription_free(struct pa_card_evsubscr *subscr)
{
    if (subscr != NULL) {
        pa_hook_slot_free(subscr->put);
        pa_hook_slot_free(subscr->unlink);

        pa_xfree(subscr);
    }
}

void pa_card_ext_discover(struct userdata *u)
{
    void            *state = NULL;
    pa_idxset       *idxset;
    struct pa_card  *card;

    pa_assert(u);
    pa_assert(u->core);
    pa_assert_se((idxset = u->core->cards));

    while ((card = pa_idxset_iterate(idxset, &state, NULL)) != NULL)
        handle_new_card(u, card);
}

char *pa_card_ext_get_name(struct pa_card *card)
{
    return card->name ? card->name : (char *)"<unknown>";
}

char **pa_card_ext_get_profiles(struct pa_card *card)
{
#define MAX_PROF 16

    pa_card_profile  *p;
    char            **plist = NULL;
    int               size  = sizeof(char *) * MAX_PROF;
    void             *st;
    int               l;

    if (card->profiles && (plist = pa_xmalloc(size)) != NULL) {
        memset(plist, 0, size);

        for (l = 0, st = NULL;
             (p = pa_hashmap_iterate(card->profiles,&st,NULL)) && l < MAX_PROF;
             l++)
        {
            plist[l] = p->name;
        }
        
    }

    return plist;

#undef MAX_PROF
}

int pa_card_ext_set_profile(struct userdata *u, char *type)
{    
    void            *state = NULL;
    pa_idxset       *idxset;
    struct pa_card  *card;
    struct pa_classify_card_data *data;
    char            *pn;
    char            *cn;
    pa_card_profile *ap;
    int              sts;

    pa_assert(u);
    pa_assert(u->core);
    pa_assert_se((idxset = u->core->cards));

    sts = 0;

    while ((card = pa_idxset_iterate(idxset, &state, NULL)) != NULL) {
        if (pa_classify_is_card_typeof(u, card, type, &data)) {

            ap = card->active_profile;
            pn = data->profile;
            cn = pa_card_ext_get_name(card);

            if (pn && (!ap || strcmp(pn, ap->name))) {
                if (pa_card_set_profile(card, pn, FALSE) < 0) {
                    sts = -1;
                    pa_log("failed to set card '%s' profile to '%s'", cn, pn);
                }
                else {
                    pa_log_debug("%s: changed card '%s' profile to '%s'",
                                 __FILE__, cn, pn);
                }
            }

            break;
        }
    }

    return sts;
}

static pa_hook_result_t card_put(void *hook_data, void *call_data,
                                 void *slot_data)
{
    struct pa_card  *card = (struct pa_card *)call_data;
    struct userdata *u    = (struct userdata *)slot_data;

    handle_new_card(u, card);

    return PA_HOOK_OK;
}


static pa_hook_result_t card_unlink(void *hook_data, void *call_data,
                                    void *slot_data)
{
    struct pa_card  *card = (struct pa_card *)call_data;
    struct userdata *u    = (struct userdata *)slot_data;

    handle_removed_card(u, card);

    return PA_HOOK_OK;
}


static void handle_new_card(struct userdata *u, struct pa_card *card)
{
    char     *name;
    uint32_t  idx;
    char      buf[1024];
    int       len;
    int       ret;

    if (card && u) {
        name = pa_card_ext_get_name(card);
        idx  = card->index;
        len  = pa_classify_card(u, card, 0,0, buf, sizeof(buf));

        pa_policy_context_register(u, pa_policy_object_card, name, card);

        if (len <= 0)
            pa_log_debug("new card '%s' (idx=%d)", name, idx);
        else {
            ret = pa_proplist_sets(card->proplist,
                                   PA_PROP_POLICY_CARDTYPELIST, buf);

            if (ret < 0) {
                pa_log("failed to set property '%s' on card '%s'",
                       PA_PROP_POLICY_DEVTYPELIST, name);
            }
            else {
                pa_log_debug("new card '%s' (idx=%d) (type %s)",
                             name, idx, buf);
                
                len = pa_classify_card(u, card, PA_POLICY_DISABLE_NOTIFY, 0,
                                       buf, sizeof(buf));
                if (len > 0) {
                    pa_policy_send_device_state(u, PA_POLICY_CONNECTED, buf);
                }
            }
        }
    }
}

static void handle_removed_card(struct userdata *u, struct pa_card *card)
{
    char     *name;
    uint32_t  idx;
    char      buf[1024];
    int       len;

    if (card && u) {
        name = pa_card_ext_get_name(card);
        idx  = card->index;
        len  = pa_classify_card(u, card, 0,0, buf, sizeof(buf));

        pa_policy_context_unregister(u, pa_policy_object_card, name, card,idx);

        if (len <= 0)
            pa_log_debug("remove card '%s' (idx=%d)", name, idx);
        else {
            pa_log_debug("remove card '%s' (idx=%d, type=%s)", name,idx, buf);
            
            len = pa_classify_card(u, card, PA_POLICY_DISABLE_NOTIFY, 0,
                                   buf, sizeof(buf));
            if (len > 0) {
                pa_policy_send_device_state(u, PA_POLICY_DISCONNECTED, buf);
            }
        }
    }
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
