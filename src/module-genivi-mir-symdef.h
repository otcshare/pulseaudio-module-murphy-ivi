#ifndef foogenivimirsymdeffoo
#define foogenivimirsymdeffoo

#include <pulsecore/core.h>
#include <pulsecore/module.h>
#include <pulsecore/macro.h>

#define pa__init module_genivi_mir_LTX_pa__init
#define pa__done module_genivi_mir_LTX_pa__done
#define pa__get_author module_genivi_mir_LTX_pa__get_author
#define pa__get_description module_genivi_mir_LTX_pa__get_description
#define pa__get_usage module_genivi_mir_LTX_pa__get_usage
#define pa__get_version module_genivi_mir_LTX_pa__get_version
#define pa__load_once module_genivi_mir_LTX_pa__load_once

int pa__init(pa_module *m);
void pa__done(pa_module *m);

const char* pa__get_author(void);
const char* pa__get_description(void);
const char* pa__get_usage(void);
const char* pa__get_version(void);
pa_bool_t pa__load_once(void);

#endif
