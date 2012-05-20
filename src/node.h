#ifndef foomirnodefoo
#define foomirnodefoo

#include <sys/types.h>

#include "userdata.h"


#define AM_ID_INVALID  65535

typedef enum {
    mir_direction_unknown,
    mir_input,
    mir_output
} mir_direction;

typedef enum {
    mir_implementation_unknown = 0,
    mir_device,
    mir_stream
} mir_implement;

typedef enum {
    mir_location_unknown = 0,
    mir_internal,
    mir_external
} mir_location;

typedef enum {
    mir_node_type_unknown = 0,

    /* application classes */
    mir_application_class_begin,
    mir_radio = mir_application_class_begin,
    mir_player,
    mir_navigator,
    mir_game,
    mir_browser,
    mir_phone,
    mir_event,
    mir_application_class_end,

    /* device types */
    mir_device_class_begin = 128,
    mir_null = mir_device_class_begin,
    mir_speakers,
    mir_front_speakers,
    mir_rear_speakers,
    mir_microphone,
    mir_jack,
    mir_spdif,
    mir_hdmi,
    mir_wired_headset,
    mir_wired_headphone,
    mir_usb_headset,
    mir_usb_headphone,
    mir_bluetooth_sco,
    mir_bluetooth_a2dp,
    mir_device_class_end,

    /* extensions */
    mir_user_defined_start = 256
} mir_node_type;

typedef enum {
    mir_privacy_unknown = 0,
    mir_public,
    mir_private
} mir_privacy;


typedef struct {
    uint32_t  index;
    char     *profile;
} pa_node_card;


/**
 * @brief routing endpoint
 *
 * @details node is a routing endpoint in the GenIVI audio model.
 *          In pulseaudio terminology a routing endpoint is one of
 *          the following
 * @li      node is a pulseaudio sink or source. Such node is a
 *          combination of pulseudio card/profile + sink/port
 * @li      node is a pulseaudio stream. Such node in pulseaudio
 *          is either a sink_input or a source_output
 */
typedef struct mir_node {
    char           *key;      /**< hash key for discover lookups */
    mir_direction   direction;/**< mir_input | mir_output */
    mir_implement   implement;/**< mir_device | mir_stream */
    uint32_t        channels; /**< number of channels (eg. 1=mono, 2=stereo) */
    mir_location    location; /**< mir_internal | mir_external */
    mir_privacy     privacy;  /**< mir_public | mir_private */
    mir_node_type   type;     /**< mir_speakers | mir_headset | ...  */
    pa_bool_t       visible;  /**< internal or can appear on UI  */
    pa_bool_t       available;/**< eg. is the headset connected?  */
    char           *amname;   /**< audiomanager name */
    char           *amdescr;  /**< UI description */
    uint16_t        amid;     /**< handle to audiomanager, if any */
    char           *paname;   /**< sink|source|sink_input|source_output name */
    uint32_t        paidx;    /**< sink|source|sink_input|source_output index*/
    pa_node_card    pacard;   /**< pulse card related data, if any  */
    char           *paport;   /**< sink or source port if applies */
    uint32_t        stamp;
} mir_node;


mir_node *mir_node_create(struct userdata *, mir_node *);
void mir_node_destroy(struct userdata *, mir_node *);

int mir_node_print(mir_node *, char *, int);

const char *mir_direction_str(mir_direction);
const char *mir_implement_str(mir_implement);
const char *mir_location_str(mir_location);
const char *mir_node_type_str(mir_node_type);
const char *mir_node_type_str(mir_node_type);
const char *mir_privacy_str(mir_privacy);

#endif


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
