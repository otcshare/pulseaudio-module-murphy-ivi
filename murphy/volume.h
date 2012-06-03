#ifndef foomirvolumefoo
#define foomirvolumefoo

#include <sys/types.h>

#include "userdata.h"
#include "list.h"

typedef double (*mir_volume_func_t)(struct userdata *, int, mir_node *, void*);


struct mir_vlim {
    size_t         maxentry;    /**< length of the class table  */
    size_t         nclass;      /**< number of classes (0 - maxentry) */
    int           *classes;     /**< class table  */
    uint32_t       stamp;
};

struct mir_volume_suppress_arg {
    double attenuation;
    struct {
        size_t nclass;
        int *classes;
    } exception;
};


pa_mir_volume *pa_mir_volume_init(struct userdata *);
void pa_mir_volume_done(struct userdata *);

void mir_volume_add_class_limit(struct userdata *,int,mir_volume_func_t,void*);
void mir_volume_add_generic_limit(struct userdata *, mir_volume_func_t,void *);

void mir_volume_add_limiting_class(struct userdata *,mir_node *,int,uint32_t);
double mir_volume_apply_limits(struct userdata *, mir_node *, int, uint32_t);

double mir_volume_suppress(struct userdata *, int, mir_node *, void *);
double mir_volume_correction(struct userdata *, int, mir_node *, void *);

#endif  /* foomirvolumefoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
