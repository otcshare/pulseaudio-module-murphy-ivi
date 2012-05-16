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

struct pa_discover {
    pa_hashmap *targets;
};

struct pa_discover *pa_discover_init(struct userdata *u)
{
    struct pa_discover *dsc = pa_xnew0(struct pa_discover, 1);

    dsc->targets = pa_hashmap_new(pa_idxset_string_hash_func,
                                  pa_idxset_string_compare_func);
    return dsc;
}

void pa_discover_new_card(struct userdata *u, struct pa_card *card)
{
}

                                  
/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
