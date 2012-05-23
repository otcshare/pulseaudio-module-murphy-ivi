#ifndef foomirconfigfoo
#define foomirconfigfoo

#include "userdata.h"

struct pa_mir_config {
    int dummy;
};

pa_mir_config *pa_mir_config_init(struct userdata *);
void pa_mir_config_done(struct userdata *);

pa_bool_t pa_mir_config_parse_file(struct userdata *, const char *);


#endif /* foomirconfigfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
