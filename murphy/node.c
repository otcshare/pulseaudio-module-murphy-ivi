#include <stdio.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/strbuf.h>


#include "node.h"
#include "discover.h"
#include "router.h"


mir_node *mir_node_create(struct userdata *u, mir_node *data)
{
    mir_node *node;

    pa_assert(u);
    pa_assert(data);
    pa_assert(data->key);
    pa_assert(data->paname);
    
    node = pa_xnew0(mir_node, 1);

    node->key       = pa_xstrdup(data->key);
    node->direction = data->direction;
    node->implement = data->implement;
    node->channels  = data->channels;
    node->location  = data->location;
    node->privacy   = data->privacy;
    node->type      = data->type;
    node->visible   = data->visible;
    node->available = data->available;
    node->amname    = pa_xstrdup(data->amname ? data->amname : data->paname);
    node->amdescr   = pa_xstrdup(data->amdescr ? data->amdescr : "");
    node->amid      = data->amid;
    node->paname    = pa_xstrdup(data->paname);
    node->paidx     = data->paidx;
    node->stamp     = data->stamp;
    MIR_DLIST_INIT(node->rtentries);
    
    if (node->implement == mir_device) {
        node->pacard.index   = data->pacard.index;
        if (data->pacard.profile)
            node->pacard.profile = pa_xstrdup(data->pacard.profile);
        if (data->paport)
            node->paport = pa_xstrdup(data->paport);
    }

    mir_router_register_node(u, node);
    
    return node;
}

void mir_node_destroy(struct userdata *u, mir_node *node)
{
    pa_assert(u);

    if (node) {
        mir_router_unregister_node(u, node);

        pa_xfree(node->key);
        pa_xfree(node->amname);
        pa_xfree(node->amdescr);
        pa_xfree(node->paname);
        pa_xfree(node->pacard.profile);
        pa_xfree(node->paport);

        pa_xfree(node);
    }
}

int mir_node_print(mir_node *node, char *buf, int len)
{
    char *p, *e;

    pa_assert(node);
    pa_assert(buf);
    pa_assert(len > 0);

    e = (p = buf) + len;

#define PRINT(f,v) if (p < e) p += snprintf(p, e-p, f "\n", v)

    PRINT("   key           : '%s'",  node->key ? node->key : "");
    PRINT("   direction     : %s"  ,  mir_direction_str(node->direction));
    PRINT("   implement     : %s"  ,  mir_implement_str(node->implement));
    PRINT("   channels      : %u"  ,  node->channels);
    PRINT("   location      : %s"  ,  mir_location_str(node->location));
    PRINT("   privacy       : %s"  ,  mir_privacy_str(node->privacy));
    PRINT("   type          : %s"  ,  mir_node_type_str(node->type));
    PRINT("   visible       : %s"  ,  node->visible ? "yes" : "no");
    PRINT("   available     : %s"  ,  node->available ? "yes" : "no");
    PRINT("   amname        : '%s'",  node->amname ? node->amname : "");
    PRINT("   amdescr       : '%s'",  node->amdescr ? node->amdescr : "");
    PRINT("   amid          : %u"  ,  node->amid);
    PRINT("   paname        : '%s'",  node->paname ? node->paname : "");
    PRINT("   paidx         : %u"  ,  node->paidx);
    PRINT("   pacard.index  : %u"  ,  node->pacard.index);
    PRINT("   pacard.profile: '%s'",  node->pacard.profile ?
                                      node->pacard.profile : "");
    PRINT("   paport        : '%s'",  node->paport ? node->paport : "");
    PRINT("   muxidx        : %u"  ,  node->muxidx);
    PRINT("   stamp         : %u"  ,  node->stamp);

#undef PRINT

    return p - buf;
}

const char *mir_direction_str(mir_direction direction)
{
    switch (direction) {
    case mir_direction_unknown:  return "unknown";
    case mir_input:              return "input";
    case mir_output:             return "output";
    default:                     return "< ??? >";
    }
}

const char *mir_implement_str(mir_implement implement)
{
    switch (implement) {
    case mir_implementation_unknown:  return "unknown";
    case mir_device:                  return "device";
    case mir_stream:                  return "stream";
    default:                          return "< ??? >";
    }
}

const char *mir_location_str(mir_location location)
{
    switch (location) {
    case mir_location_unknown:  return "unknown";
    case mir_internal:          return "internal";
    case mir_external:          return "external";
    default:                    return "< ??? >";
    }
} 


const char *mir_node_type_str(mir_node_type type)
{
    switch (type) {
    case mir_node_type_unknown:   return "Unknown";
    case mir_radio:               return "Radio";
    case mir_player:              return "Player";
    case mir_navigator:           return "Navigator";
    case mir_game:                return "Game";
    case mir_browser:             return "Browser";
    case mir_phone:               return "Phone";
    case mir_event:               return "Event";
    case mir_speakers:            return "Speakers";
    case mir_microphone:          return "Microphone";
    case mir_jack:                return "Line";
    case mir_spdif:               return "SPDIF";
    case mir_hdmi:                return "HDMI";
    case mir_wired_headset:       return "Wired Headset";
    case mir_wired_headphone:     return "Wired Headphone";
    case mir_usb_headset:         return "USB Headset";
    case mir_bluetooth_sco:       return "Bluetooth Handsfree";
    case mir_bluetooth_a2dp:      return "Bluetooth Stereo";
    default:                      return "<user defined>";
    }
}


const char *mir_privacy_str(mir_privacy privacy)
{
    switch (privacy) {
    case mir_privacy_unknown:  return "<unknown>";
    case mir_public:           return "public";
    case mir_private:          return "private";
    default:                   return "< ??? >";
    }
}

                                  
/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
