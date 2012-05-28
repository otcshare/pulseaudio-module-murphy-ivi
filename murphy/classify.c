#include <stdio.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/card.h>
#include <pulsecore/device-port.h>
#include <pulsecore/core-util.h>

#include "classify.h"
#include "node.h"


void pa_classify_node_by_card(mir_node        *node,
                              pa_card         *card,
                              pa_card_profile *prof,
                              pa_device_port  *port)
{
    const char *bus;
    const char *form;
    
    pa_assert(node);
    pa_assert(card);

    bus  = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_BUS);
    form = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_FORM_FACTOR);

    if (form) {
        if (!strcasecmp(form, "internal")) {
            node->location = mir_external;
            if (port && !strcasecmp(bus, "pci")) {
                pa_classify_guess_device_node_type_and_name(node, port->name,
                                                            port->description);
            }
        }
        else if (!strcasecmp(form, "speaker") || !strcasecmp(form, "car")) {
            if (node->direction == mir_output) {
                node->location = mir_internal;
                node->type = mir_speakers;
            }
        }
        else if (!strcasecmp(form, "handset")) {
            node->location = mir_external;
            node->type = mir_phone;
            node->privacy = mir_private;
        }
        else if (!strcasecmp(form, "headset")) {
            node->location = mir_external;
            if (bus) {
                if (!strcasecmp(bus,"usb")) {
                    node->type = mir_usb_headset;
                }
                else if (!strcasecmp(bus,"bluetooth")) {
                    if (prof && !strcmp(prof->name, "a2dp"))
                        node->type = mir_bluetooth_a2dp;
                    else 
                        node->type = mir_bluetooth_sco;
                }
                else {
                    node->type = mir_wired_headset;
                }
            }
        }
        else if (!strcasecmp(form, "headphone")) {
            if (node->direction == mir_output) {
                node->location = mir_external;
                if (bus) {
                    if (!strcasecmp(bus,"usb"))
                        node->type = mir_usb_headphone;
                    else if (strcasecmp(bus,"bluetooth"))
                        node->type = mir_wired_headphone;
                }
            }
        }
        else if (!strcasecmp(form, "microphone")) {
            if (node->direction == mir_input) {
                node->location = mir_external;
                node->type = mir_microphone;
            }
        }
    }
    else {
        if (port && !strcasecmp(bus, "pci")) {
            pa_classify_guess_device_node_type_and_name(node, port->name,
                                                        port->description);
        }
    }

    if (!node->amname[0]) {
        if (node->type != mir_node_type_unknown)
            node->amname = (char *)mir_node_type_str(node->type);
        else if (port && port->description)
            node->amname = port->description;
        else if (port && port->name)
            node->amname = port->name;
        else
            node->amname = node->paname;
    }


    if (node->direction == mir_input)
        node->privacy = mir_privacy_unknown;
    else {
        switch (node->type) {
            /* public */
        default:
        case mir_speakers:
        case mir_front_speakers:
        case mir_rear_speakers:
            node->privacy = mir_public;
            break;
            
            /* private */
        case mir_phone:
        case mir_wired_headset:
        case mir_wired_headphone:
        case mir_usb_headset:
        case mir_usb_headphone:
        case mir_bluetooth_sco:
        case mir_bluetooth_a2dp:
            node->privacy = mir_private;
            break;
            
            /* unknown */
        case mir_null:
        case mir_jack:
        case mir_spdif:
        case mir_hdmi:
            node->privacy = mir_privacy_unknown;
            break;
        } /* switch */
    }
}


/* data->direction must be set */
void pa_classify_guess_device_node_type_and_name(mir_node   *node,
                                                 const char *name,
                                                 const char *desc)
{
    pa_assert(node);
    pa_assert(name);
    pa_assert(desc);

    if (node->direction == mir_output && strcasestr(name, "headphone")) {
        node->type = mir_wired_headphone;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "headset")) {
        node->type = mir_wired_headset;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "line")) {
        node->type = mir_jack;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "spdif")) {
        node->type = mir_spdif;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "hdmi")) {
        node->type = mir_hdmi;
        node->amname = (char *)desc;
    }
    else if (node->direction == mir_input && strcasestr(name, "microphone")) {
        node->type = mir_microphone;
        node->amname = (char *)desc;
    }
    else if (node->direction == mir_output && strcasestr(name,"analog-output"))
        node->type = mir_speakers;
    else if (node->direction == mir_input && strcasestr(name, "analog-input"))
        node->type = mir_jack;
    else {
        node->type = mir_node_type_unknown;
    }
}


mir_node_type pa_classify_guess_stream_node_type(pa_proplist *pl)
{
    typedef struct {
        char *id;
        mir_node_type type;
    } map_t;

    static map_t role_map[] = {
        {"video"    , mir_player },
        {"music"    , mir_player },
        {"game"     , mir_game   },
        {"event"    , mir_event  },
        {"phone"    , mir_player },
        {"animation", mir_browser},
        {"test"     , mir_player },
        {NULL, mir_node_type_unknown}
    };

    static map_t bin_map[] = {
        {"rhytmbox"    , mir_player },
        {"firefox"     , mir_browser},
        {"chrome"      , mir_browser},
        {"sound-juicer", mir_player },
        {NULL, mir_node_type_unknown}
    };


    mir_node_type  rtype, btype;
    const char    *role;
    const char    *bin;
    map_t         *m;

    pa_assert(pl);

    rtype = btype = mir_node_type_unknown;

    role = pa_proplist_gets(pl, PA_PROP_MEDIA_ROLE);
    bin  = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_BINARY);

    if (role) {
        for (m = role_map;  m->id;  m++) {
            if (pa_streq(role, m->id))
                break;
        }
        rtype = m->type;
    }

    if (rtype != mir_node_type_unknown && rtype != mir_player)
        return rtype;

    if (bin) {
        for (m = bin_map;  m->id;  m++) {
            if (pa_streq(bin, m->id))
                break;
        }
        btype = m->type;
    }

    if (btype == mir_node_type_unknown)
        return rtype;

    return btype;
}


pa_bool_t pa_classify_multiplex_stream(mir_node *node)
{
    static pa_bool_t multiplex[mir_application_class_end] = {
        [ mir_player  ] = TRUE,
        [ mir_game    ] = TRUE,
        [ mir_browser ] = TRUE,
    };

    mir_node_type class;

    pa_assert(node);

    //return FALSE;

    if (node->implement == mir_stream && node->direction == mir_input) {
        class = node->type;

        if (class > mir_application_class_begin &&
            class < mir_application_class_end)
        {
            return multiplex[class];
        }
    }

    return FALSE;
}
