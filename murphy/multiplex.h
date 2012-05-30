#ifndef foomultiplexfoo
#define foomultiplexfoo

#include <pulsecore/core.h>
#include <pulsecore/sink-input.h>

#include "list.h"

typedef struct pa_muxnode pa_muxnode;

typedef struct pa_multiplex {
    PA_LLIST_HEAD(pa_muxnode, muxnodes);
} pa_multiplex;


struct pa_muxnode {
    PA_LLIST_FIELDS(pa_muxnode);
    uint32_t   module_index;
    uint32_t   sink_index;
    uint32_t   defstream_index;
};

pa_multiplex *pa_multiplex_init(void);

void pa_multiplex_done(pa_multiplex *, pa_core *);

pa_muxnode *pa_multiplex_create(pa_multiplex *, pa_core *, uint32_t,
                                pa_channel_map *, const char *, int);
void pa_multiplex_destroy(pa_multiplex *, pa_core *, pa_muxnode *);

pa_muxnode *pa_multiplex_find(pa_multiplex *, uint32_t);

pa_bool_t pa_multiplex_add_default_route(pa_core *, pa_muxnode *,pa_sink *,int);
pa_bool_t pa_multiplex_remove_default_route(pa_core *,pa_muxnode *,pa_bool_t);
pa_bool_t pa_multiplex_change_default_route(pa_core *,pa_muxnode *,pa_sink *);

pa_bool_t pa_multiplex_add_explicit_route(pa_core*, pa_muxnode*, pa_sink*,int);
pa_bool_t pa_multiplex_remove_explicit_route(pa_core *, pa_muxnode *, pa_sink *);

pa_bool_t pa_multiplex_duplicate_route(pa_core *, pa_muxnode *,
                                       pa_sink_input *, pa_sink *);

int pa_multiplex_print(pa_muxnode *, char *, int);


#endif /* foomultiplexfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
