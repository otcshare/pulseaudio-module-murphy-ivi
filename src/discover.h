#ifndef foodiscoverfoo
#define foodiscoverfoo

#include <sys/types.h>
#include <regex.h>

#include "userdata.h"

struct pa_card;


/*
 * generally in PA a routing target is a
 * card/profile + sink/port combination
 * the struct below represent such entity
 */
#define AM_ID_INVALID  (~((uint16_t)0))

struct pa_routing_target {
    char *name;          /**< internal  name */
    char *descr;         /**< UI description */
    uint16_t id;         /**< link to audiomanager, if any */
    struct {
        uint32_t index;
        char *profile;
    } card;
    struct {
        uint32_t index;  /**< PA_IDXSET_INVALID if the sink is not loaded  */
        char *name;      /**< sink name */
        char *port;      /**< port name for the target  */
    } sink;
};

struct pa_discover *pa_discover_init(struct userdata *);
void pa_discover_new_card(struct userdata *, struct pa_card *);

#endif


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
