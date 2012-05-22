#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/card.h>
#include <pulsecore/sink.h>
#include <pulsecore/source.h>
#include <pulsecore/sink-input.h>

#include "userdata.h"
#include "utils.h"

#ifndef DEFAULT_CONFIG_DIR
#define DEFAULT_CONFIG_DIR "/etc/pulse"
#endif

static uint32_t stamp;

static char *sink_input_name(pa_proplist *);


char *pa_utils_get_card_name(pa_card *card)
{
    return (card && card->name) ? card->name : "<unknown>";
}

char *pa_utils_get_sink_name(pa_sink *sink)
{
    return (sink && sink->name) ? sink->name : "<unknown>";
}

char *pa_utils_get_source_name(pa_source *source)
{
    return (source && source->name) ? source->name : "<unknown>";
}

char *pa_utils_get_sink_input_name(pa_sink_input *sinp)
{
    char *name;

    if (sinp && (name = sink_input_name(sinp->proplist)))
        return name;
    
    return "<unknown>";
}

char *pa_utils_get_sink_input_name_from_data(pa_sink_input_new_data *data)
{
    char *name;

    if (data && (name = sink_input_name(data->proplist)))
        return name;
    
    return "<unknown>";
}

static char *sink_input_name(pa_proplist *pl)
{
    const char  *appnam;
    const char  *binnam;

    if ((appnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_NAME)))
        return (char *)appnam;

    if ((binnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_BINARY)))
        return (char *)binnam;

    return NULL;
}


const char *pa_utils_file_path(const char *file, char *buf, size_t len)
{
    /*
    pa_assert(file);
    pa_assert(buf);
    pa_assert(len > 0);
    */

    snprintf(buf, len, "%s/%s", DEFAULT_CONFIG_DIR, file);

    return buf;
}


const uint32_t pa_utils_new_stamp(void)
{
    return ++stamp;
}

const uint32_t pa_utils_get_stamp(void)
{
    return stamp;
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */


