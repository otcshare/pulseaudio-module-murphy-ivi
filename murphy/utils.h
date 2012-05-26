#ifndef foomurphyiviutilsfoo
#define foomurphyiviutilsfoo

#include <stdbool.h>
#include <pulsecore/core.h>

struct pa_null_sink;

struct pa_null_sink *pa_utils_create_null_sink(struct userdata *,const char *);
void pa_utils_destroy_null_sink(struct userdata *);
pa_sink *pa_utils_get_null_sink(struct userdata *);


char *pa_utils_get_card_name(pa_card *);
char *pa_utils_get_sink_name(pa_sink *);
char *pa_utils_get_source_name(pa_source *);
char *pa_utils_get_sink_input_name(pa_sink_input *);
char *pa_utils_get_sink_input_name_from_data(pa_sink_input_new_data *);

void  pa_utils_set_stream_routing_properties(pa_proplist *, int, pa_sink *);
pa_bool_t pa_utils_stream_has_default_route(pa_proplist *);


const char *pa_utils_file_path(const char *, char *, size_t);

const uint32_t pa_utils_new_stamp(void);
const uint32_t pa_utils_get_stamp(void);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
