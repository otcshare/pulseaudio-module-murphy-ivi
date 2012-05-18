#include <stdio.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/card.h>
#include <pulsecore/device-port.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/strbuf.h>


#include "discover.h"
#include "node.h"
#include "card-ext.h"

#define MAX_CARD_TARGET   4
#define MAX_NAME_LENGTH   256


static void add_alsa_card(struct userdata *, pa_card *);
static void add_bluetooth_card(struct userdata *, pa_card *);

static void handle_add_udev_loaded_card(struct userdata *, pa_card *,
                                        mir_node *, char *);
static void handle_card_ports(struct userdata *, mir_node *,
                              pa_card *, pa_card_profile *);

static mir_node *create_node(struct userdata *, mir_node *, pa_bool_t *);
static void destroy_node(struct userdata *, mir_node *);

static void parse_profile_name(pa_card_profile *,
                               char **, char **, char *, int);

static void classify_node_by_card(mir_node *, pa_card *,
                                  pa_card_profile *, pa_device_port *);
static void guess_node_type_and_name(mir_node *, const char *, const char *);



struct pa_discover *pa_discover_init(struct userdata *u)
{
    pa_discover *dsc = pa_xnew0(pa_discover, 1);

    dsc->chmin = 1;
    dsc->chmax = 2;
    dsc->nodes.pahash = pa_hashmap_new(pa_idxset_string_hash_func,
                                       pa_idxset_string_compare_func);
    return dsc;
}

void pa_discover_add_card(struct userdata *u, pa_card *card)
{
    const char *bus;

    pa_assert(u);
    pa_assert(card);

    if ((bus = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_BUS)) == NULL) {
        pa_log_debug("ignoring card '%s' due to lack of %s property",
                     pa_card_ext_get_name(card), PA_PROP_DEVICE_BUS);
        return;
    }

    if (pa_streq(bus, "pci") || pa_streq(bus, "usb")) {
        add_alsa_card(u, card);
        return;
    }
    else if (pa_streq(bus, "bluetooth")) {
        add_bluetooth_card(u, card);
        return;
    }

    pa_log_debug("ignoring card '%s' due to unsupported bus type '%s'",
                 pa_card_ext_get_name(card), bus);
}

void pa_discover_remove_card(struct userdata *u, pa_card *card)
{
    pa_discover *discover;
    mir_node    *node;
    void        *state;

    pa_assert(u);
    pa_assert(card);
    pa_assert_se((discover = u->discover));

    PA_HASHMAP_FOREACH(node, discover->nodes.pahash, state) {
        if (node->implement == mir_device &&
            node->pacard.index == card->index)
        {
            destroy_node(u, node);
        }
    }
}

static void add_alsa_card(struct userdata *u, pa_card *card)
{
    pa_discover     *discover;
    mir_node         data;
    const char      *udd;
    char            *cnam;
    char            *cid;

    pa_assert_se((discover = u->discover));

    memset(&data, 0, sizeof(data));
    data.visible = TRUE;
    data.amid = AM_ID_INVALID;
    data.implement = mir_device;
    data.paidx = PA_IDXSET_INVALID;

    cnam = pa_card_ext_get_name(card);
    udd  = pa_proplist_gets(card->proplist, "module-udev-detect.discovered");

    if (udd && pa_streq(udd, "1")) {
        /* udev loaded alsa card */
        if (!strncmp(cnam, "alsa_card.", 10)) {
            cid = cnam + 10;
            handle_add_udev_loaded_card(u, card, &data, cid);
            return;
        }
    }
    else {
        /* manually loaded pci or usb card */
    }

    pa_log_debug("ignoring unrecognized pci card '%s'", cnam);
}

static void add_bluetooth_card(struct userdata *u, pa_card *card)
{
    pa_discover     *discover;
    pa_card_profile *prof;
    mir_node         data;
    char            *cnam;
    char            *cid;
    const char      *cdescr;
    void            *state;
    char             paname[MAX_NAME_LENGTH+1];
    char             amname[MAX_NAME_LENGTH+1];
    char             key[MAX_NAME_LENGTH+1];

    pa_assert_se((discover = u->discover));

    cdescr = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);


    memset(paname, 0, sizeof(paname));
    memset(amname, 0, sizeof(amname));
    memset(key   , 0, sizeof(key)   );

    memset(&data, 0, sizeof(data));
    data.key = key;
    data.visible = TRUE;
    data.amid = AM_ID_INVALID;
    data.implement = mir_device;
    data.paidx = PA_IDXSET_INVALID;
    data.paname = paname;
    data.amname = amname;
    data.amdescr = (char *)cdescr;
    data.pacard.index = card->index;

    cnam = pa_card_ext_get_name(card);

    if (!strncmp(cnam, "bluez_card.", 11)) { 
        cid = cnam + 11;

        PA_HASHMAP_FOREACH(prof, card->profiles, state) {
            data.available = (prof == card->active_profile);
            data.pacard.profile = prof->name;

            if (prof->n_sinks > 0) {
                data.direction = mir_output;
                data.channels = prof->max_sink_channels; 
                data.amname = amname;
                amname[0] = '\0';
                snprintf(paname, sizeof(paname), "bluez_sink.%s", cid);
                snprintf(key, sizeof(key), "%s@%s", paname, prof->name);
                classify_node_by_card(&data, card, prof, NULL);
                create_node(u, &data, NULL);
            }

            if (prof->n_sources > 0) {
                data.direction = mir_input;
                data.channels = prof->max_source_channels; 
                data.amname = amname;
                amname[0] = '\0';
                snprintf(paname, sizeof(paname), "bluez_source.%s", cid);
                snprintf(key, sizeof(key), "%s@%s", paname, prof->name);
                classify_node_by_card(&data, card, prof, NULL);
                create_node(u, &data, NULL);
            }
        }
    }
}

static void handle_add_udev_loaded_card(struct userdata *u, pa_card *card,
                                        mir_node *data, char *cardid)
{
    pa_discover      *discover;
    pa_card_profile  *prof;
    pa_card_profile  *active;
    void             *state;
    const char       *alsanam;
    char             *sid;
    char             *sinks[MAX_CARD_TARGET+1];
    char             *sources[MAX_CARD_TARGET+1];
    char              buf[MAX_NAME_LENGTH+1];
    char              paname[MAX_NAME_LENGTH+1];
    char              amname[MAX_NAME_LENGTH+1];
    int               i;

    pa_assert(card);
    pa_assert(card->profiles);
    pa_assert_se((discover = u->discover));

    alsanam = pa_proplist_gets(card->proplist, "alsa.card_name");

    memset(amname, 0, sizeof(amname));

    data->paname  = paname;
    data->amname  = amname;
    data->amdescr = (char *)alsanam;

    data->pacard.index = card->index;

    active = card->active_profile;

    PA_HASHMAP_FOREACH(prof, card->profiles, state) {
        if (active && prof != active)
            continue;

        if (!prof->n_sinks && !prof->n_sources)
            continue;

        if (prof->n_sinks && 
            (prof->max_sink_channels < discover->chmin ||
             prof->max_sink_channels  > discover->chmax  ))
            continue;
        
        if (prof->n_sources &&
            (prof->max_source_channels <  discover->chmin ||
             prof->max_source_channels >  discover->chmax   ))
            continue;

        data->pacard.profile = prof->name;

        parse_profile_name(prof, sinks,sources, buf,sizeof(buf));
        
        data->direction = mir_output;
        data->channels = prof->max_sink_channels;
        for (i = 0;  (sid = sinks[i]);  i++) {
            snprintf(paname, sizeof(paname), "alsa_output.%s.%s", cardid, sid);
            handle_card_ports(u, data, card, prof);
        }

        data->direction = mir_input;
        data->channels = prof->max_source_channels;
        for (i = 0;  (sid = sources[i]);  i++) {
            snprintf(paname, sizeof(paname), "alsa_input.%s.%s", cardid, sid);
            handle_card_ports(u, data, card, prof);
        }        
    }
}


static void handle_card_ports(struct userdata *u, mir_node *data,
                              pa_card *card, pa_card_profile *prof)
{
    mir_node       *node = NULL;
    pa_bool_t       have_ports = FALSE;
    char           *amname = data->amname;
    pa_device_port *port;
    void           *state;
    pa_bool_t       created;
    char            key[MAX_NAME_LENGTH+1];

    pa_assert(u);
    pa_assert(data);
    pa_assert(card);
    pa_assert(prof);

    if (card->ports) {        
        PA_HASHMAP_FOREACH(port, card->ports, state) {
            /*
             * if this port did not belong to any profile 
             * (ie. prof->profiles == NULL) we assume that this port
             * does works with all the profiles
             */
            if (port->profiles && pa_hashmap_get(port->profiles, prof->name) &&
                ((port->is_input && data->direction == mir_input)||
                 (port->is_output && data->direction == mir_output)))
            {
                have_ports = TRUE;

                /* make constrain if node != NULL and add node to it */

                amname[0] = '\0';
                snprintf(key, sizeof(key), "%s@%s", data->paname, port->name);

                data->key       = key;
                data->available = (port->available == PA_PORT_AVAILABLE_YES);
                data->type      = 0;
                data->amname    = amname;
                data->paport    = port->name;

                classify_node_by_card(data, card, prof, port);

                node = create_node(u, data, &created);

                /* if constrain != NULL add the node to it */
            }
        }
    }
    
    if (!have_ports) {
        data->key = data->paname;
        data->available = TRUE;
        classify_node_by_card(data, card, prof, NULL);
        create_node(u, data, NULL);
    }

    data->amname = amname;
    *amname = '\0';
}


static mir_node *create_node(struct userdata *u, mir_node *data, 
                             pa_bool_t *created_ret)
{
    pa_discover *discover;
    mir_node    *node;
    pa_bool_t    created;
    char         buf[2048];

    pa_assert(u);
    pa_assert(data);
    pa_assert(data->key);
    pa_assert(data->paname);
    pa_assert_se((discover = u->discover));
    
    if ((node = pa_hashmap_get(discover->nodes.pahash, data->key)))
        created = FALSE;
    else {
        created = TRUE;

        node = mir_node_create(u, data);
        pa_hashmap_put(discover->nodes.pahash, node->key, node);

        mir_node_print(node, buf, sizeof(buf));
        pa_log_debug("new node:\n%s", buf);
    }

    if (created_ret)
        *created_ret = created;
    
    return node;
}

static void destroy_node(struct userdata *u, mir_node *node)
{
    pa_discover *discover;
    mir_node    *removed;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    if (node) {
        removed = pa_hashmap_remove(discover->nodes.pahash, node->key);

        if (node != removed) {
            if (removed) {
                pa_log("%s: confused with data structures: key mismatch. "
                       " attempted to destroy '%s'; actually destroyed '%s'",
                       __FILE__, node->key, removed->key);
            }
            else {
                pa_log("%s: confused with data structures: node '%s' "
                       "is not in the has table", __FILE__, node->key);
            }
            return;
        }
    }

    pa_log_debug("destroying node: %s / '%s'", node->key, node->amname);

    mir_node_destroy(u, node);
}



static char *get_name(char **string_ptr, int offs)
{
    char c, *name, *end;

    name = *string_ptr + offs;

    for (end = name;  (c = *end);   end++) {
        if (c == '+') {
            *end++ = '\0';
            break;
        }
    }

    *string_ptr = end;

    return name;
} 

static void parse_profile_name(pa_card_profile *prof,
                               char           **sinks,
                               char           **sources,
                               char            *buf,
                               int              buflen)
{
    char *p = buf;
    int   i = 0;
    int   j = 0;

    pa_assert(prof->name);

    strncpy(buf, prof->name, buflen);
    buf[buflen-1] = '\0';

    memset(sinks, 0, sizeof(char *) * (MAX_CARD_TARGET+1));
    memset(sources, 0, sizeof(char *) * (MAX_CARD_TARGET+1));

    do {
        if (!strncmp(p, "output:", 7)) {
            if (i >= MAX_CARD_TARGET) {
                pa_log_debug("number of outputs exeeds the maximum %d in "
                             "profile name '%s'", MAX_CARD_TARGET, prof->name);
                return;
            }
            sinks[i++] = get_name(&p, 7);
        } 
        else if (!strncmp(p, "input:", 6)) {
            if (j >= MAX_CARD_TARGET) {
                pa_log_debug("number of inputs exeeds the maximum %d in "
                             "profile name '%s'", MAX_CARD_TARGET, prof->name);
                return;
            }
            sources[j++] = get_name(&p, 6);            
        }
        else {
            pa_log("%s: failed to parse profile name '%s'",
                   __FILE__, prof->name);
            return;
        }
    } while (*p);
}


static void classify_node_by_card(mir_node *data, pa_card *card,
                                  pa_card_profile *prof, pa_device_port *port)
{
    const char *bus;
    const char *form;
    
    pa_assert(data);
    pa_assert(card);

    bus  = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_BUS);
    form = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_FORM_FACTOR);

    if (form) {
        if (!strcasecmp(form, "internal")) {
            data->location = mir_external;
            if (port && !strcasecmp(bus, "pci"))
                guess_node_type_and_name(data, port->name, port->description);
        }
        else if (!strcasecmp(form, "speaker") || !strcasecmp(form, "car")) {
            if (data->direction == mir_output) {
                data->location = mir_internal;
                data->type = mir_speakers;
            }
        }
        else if (!strcasecmp(form, "handset")) {
            data->location = mir_external;
            data->type = mir_phone;
            data->privacy = mir_private;
        }
        else if (!strcasecmp(form, "headset")) {
            data->location = mir_external;
            if (bus) {
                if (!strcasecmp(bus,"usb")) {
                    data->type = mir_usb_headset;
                }
                else if (!strcasecmp(bus,"bluetooth")) {
                    if (prof && !strcmp(prof->name, "a2dp"))
                        data->type = mir_bluetooth_a2dp;
                    else 
                        data->type = mir_bluetooth_sco;
                }
                else {
                    data->type = mir_wired_headset;
                }
            }
        }
        else if (!strcasecmp(form, "headphone")) {
            if (data->direction == mir_output) {
                data->location = mir_external;
                if (bus) {
                    if (!strcasecmp(bus,"usb"))
                        data->type = mir_usb_headphone;
                    else if (strcasecmp(bus,"bluetooth"))
                        data->type = mir_wired_headphone;
                }
            }
        }
        else if (!strcasecmp(form, "microphone")) {
            if (data->direction == mir_input) {
                data->location = mir_external;
                data->type = mir_microphone;
            }
        }
    }
    else {
        if (port && !strcasecmp(bus, "pci"))
            guess_node_type_and_name(data, port->name, port->description);
    }

    if (!data->amname[0]) {
        if (data->type != mir_node_type_unknown)
            data->amname = (char *)mir_node_type_str(data->type);
        else if (port && port->description)
            data->amname = port->description;
        else if (port && port->name)
            data->amname = port->name;
        else
            data->amname = data->paname;
    }


    if (data->direction == mir_input)
        data->privacy = mir_privacy_unknown;
    else {
        switch (data->type) {
            /* public */
        default:
        case mir_speakers:
        case mir_front_speakers:
        case mir_rear_speakers:
            data->privacy = mir_public;
            break;
            
            /* private */
        case mir_phone:
        case mir_wired_headset:
        case mir_wired_headphone:
        case mir_usb_headset:
        case mir_usb_headphone:
        case mir_bluetooth_sco:
        case mir_bluetooth_a2dp:
            data->privacy = mir_private;
            break;
            
            /* unknown */
        case mir_null:
        case mir_jack:
        case mir_spdif:
        case mir_hdmi:
            data->privacy = mir_privacy_unknown;
            break;
        } /* switch */
    }
}


/* data->direction must be set */
static void guess_node_type_and_name(mir_node *data, const char *pnam,
                                     const char *pdesc)
{
    if (data->direction == mir_output && strcasestr(pnam, "headphone")) {
        data->type = mir_wired_headphone;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "headset")) {
        data->type = mir_wired_headset;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "line")) {
        data->type = mir_jack;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "spdif")) {
        data->type = mir_spdif;
        data->amname = (char *)pdesc;
    }
    else if (strcasestr(pnam, "hdmi")) {
        data->type = mir_hdmi;
        data->amname = (char *)pdesc;
    }
    else if (data->direction == mir_input && strcasestr(pnam, "microphone")) {
        data->type = mir_microphone;
        data->amname = (char *)pdesc;
    }
    else if (data->direction == mir_output && strcasestr(pnam, "analog-output"))
        data->type = mir_speakers;
    else if (data->direction == mir_input && strcasestr(pnam, "analog-input"))
        data->type = mir_jack;
    else {
        data->type = mir_node_type_unknown;
    }
}


                                  
/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
