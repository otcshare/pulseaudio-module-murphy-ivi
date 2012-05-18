#ifndef foodiscoverfoo
#define foodiscoverfoo

#include <sys/types.h>
#include <regex.h>

#include "userdata.h"

struct pa_card;


#define PA_BIT(a)      (1UL << (a))

enum pa_bus_type {
    pa_bus_unknown = 0,
    pa_bus_pci,
    pa_bus_usb,
    pa_bus_bluetooth,
};

enum pa_form_factor {
    pa_form_factor_unknown,
    pa_internal,
    pa_speaker,
    pa_handset,
    pa_tv,
    pa_webcam,
    pa_microphone,
    pa_headset,
    pa_headphone,
    pa_hands_free,
    pa_car,
    pa_hifi,
    pa_computer,
    pa_portable
};

typedef struct pa_discover {
    unsigned                chmin;
    unsigned                chmax;
    struct {
        pa_hashmap *pahash;
        pa_hashmap *amhash;
    }                       nodes;    
} pa_discover;


struct pa_discover *pa_discover_init(struct userdata *);
void pa_discover_add_card(struct userdata *, struct pa_card *);
void pa_discover_remove_card(struct userdata *, pa_card *);

#endif


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
