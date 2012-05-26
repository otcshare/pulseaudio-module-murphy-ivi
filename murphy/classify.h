#ifndef foomirclassifyfoo
#define foomirclassifyfoo

#include <sys/types.h>

#include "userdata.h"

void pa_classify_node_by_card(mir_node *, pa_card *, pa_card_profile *,
                              pa_device_port *);
void pa_classify_guess_device_node_type_and_name(mir_node*, const char *,
                                                 const char *);
mir_node_type pa_classify_guess_stream_node_type(pa_proplist *);

pa_bool_t pa_classify_multiplex_stream(mir_node *);


#endif  /* foomirclassifyfoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
