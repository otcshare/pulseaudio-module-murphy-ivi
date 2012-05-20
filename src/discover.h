#ifndef foodiscoverfoo
#define foodiscoverfoo

#include <sys/types.h>
#include <regex.h>

#include "userdata.h"

typedef struct pa_card  pa_card;
typedef struct mir_node mir_node;

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
    /*
     * cirtreria for filtering sinks and sources
     */
    unsigned        chmin;    /**< minimum of max channels */
    unsigned        chmax;    /**< maximum of max channels */
    pa_bool_t       selected; /**< for alsa cards: whether to consider the
                                   selected profile alone.
                                   for bluetooth cards: no effect */
    struct {
        pa_hashmap *byname;
        pa_hashmap *byptr;
    }               nodes;
} pa_discover;


struct pa_discover *pa_discover_init(struct userdata *);
void  pa_discover_done(struct userdata *);

void pa_discover_domain_up(struct userdata *);
void pa_discover_domain_down(struct userdata *);

void pa_discover_add_card(struct userdata *, pa_card *);
void pa_discover_remove_card(struct userdata *, pa_card *);
void pa_discover_profile_changed(struct userdata *, pa_card *);

void pa_discover_add_sink(struct userdata *, pa_sink *);
void pa_discover_remove_sink(struct userdata *, pa_sink *);

void pa_discover_add_source(struct userdata *, pa_source *);
void pa_discover_remove_source(struct userdata *, pa_source *);

mir_node *pa_discover_find_node(struct userdata *, const char *);

#endif


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
