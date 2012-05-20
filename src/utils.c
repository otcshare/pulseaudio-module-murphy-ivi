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

#include "userdata.h"
#include "utils.h"

#ifndef DEFAULT_CONFIG_DIR
#define DEFAULT_CONFIG_DIR "/etc/pulse"
#endif

static uint32_t stamp;


char *pa_utils_get_card_name(pa_card *card)
{
    return card->name ? card->name : (char *)"<unknown>";
}

char *pa_utils_get_sink_name(pa_sink *sink)
{
    return sink->name ? sink->name : (char *)"<unknown>";
}

char *pa_utils_get_source_name(pa_source *source)
{
    return source->name ? source->name : (char *)"<unknown>";
}



const char *pa_utils_file_path(const char *file, char *buf, size_t len)
{
    snprintf(buf, len, "%s/x%s", DEFAULT_CONFIG_DIR, file);

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


