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
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/card.h>
#include <pulsecore/device-port.h>
#include <pulsecore/core-util.h>

#include "classify.h"
#include "node.h"
#include "utils.h"


void pa_classify_node_by_card(mir_node        *node,
                              pa_card         *card,
                              pa_card_profile *prof,
                              pa_device_port  *port)
{
    const char *bus;
    const char *form;
    /*
    const char *desc;
    */

    pa_assert(node);
    pa_assert(card);

    bus  = pa_utils_get_card_bus(card);
    form = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_FORM_FACTOR);
    /*
    desc = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);
    */

    node->type = mir_node_type_unknown;

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
        else if (prof && !strcasecmp(bus, "bluetooth")) {
            if (!strcmp(prof->name, "a2dp"))
                node->type = mir_bluetooth_a2dp;
            else if (!strcmp(prof->name, "hsp"))
                node->type = mir_bluetooth_sco;
            else if (!strcmp(prof->name, "hfgw"))
                node->type = mir_bluetooth_carkit;
            else if (!strcmp(prof->name, "a2dp_source"))
                node->type = mir_bluetooth_source;
            else if (!strcmp(prof->name, "a2dp_sink"))
                node->type = mir_bluetooth_sink;
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
        case mir_bluetooth_sink:
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
        {"video"    , mir_player    },
        {"music"    , mir_player    },
        {"game"     , mir_game      },
        {"event"    , mir_event     },
	{"navigator", mir_navigator },
        {"phone"    , mir_phone     },
        {"carkit"   , mir_phone     },
        {"animation", mir_browser   },
        {"test"     , mir_player    },
        {"ringtone" , mir_alert     },
        {"camera"   , mir_camera    },
        {"system"   , mir_system    },
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

mir_node_type pa_classify_guess_application_class(mir_node *node)
{
    mir_node_type class;

    pa_assert(node);

    if (node->implement == mir_stream)
        class = node->type;
    else {
        if (node->direction == mir_output)
            class = mir_node_type_unknown;
        else {
            switch (node->type) {
            default:                    class = mir_node_type_unknown;   break;
            case mir_bluetooth_carkit:  class = mir_phone;               break;
            case mir_bluetooth_source:  class = mir_player;              break;
            }
        }
    }

    return class;
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

const char *pa_classify_loopback_stream(mir_node *node)
{
    const char *role[mir_device_class_end - mir_device_class_begin] = {
        [ mir_bluetooth_carkit - mir_device_class_begin ] = "phone",
        [ mir_bluetooth_source - mir_device_class_begin ] = "music" ,
    };

    int class;

    pa_assert(node);

    if (node->implement == mir_device) {
        class = node->type;

        if (class >= mir_device_class_begin && class < mir_device_class_end) {
            return role[class - mir_device_class_begin];
        }
    }

    return NULL;
}

