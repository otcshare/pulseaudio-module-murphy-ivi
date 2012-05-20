#ifndef fooutilsfoo
#define fooutilsfoo

#include <stdbool.h>
#include <pulsecore/core.h>

char *pa_utils_get_card_name(pa_card *);
char *pa_utils_get_sink_name(pa_sink *);
char *pa_utils_get_source_name(pa_source *);
char *pa_utils_get_sink_input_name(pa_sink_input *);
char *pa_utils_get_sink_input_name_from_data(pa_sink_input_new_data *);


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
