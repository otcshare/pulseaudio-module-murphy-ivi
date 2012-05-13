#ifndef foopolicycontextfoo
#define foopolicycontextfoo

#include "classify.h"

enum pa_policy_action_type {
    pa_policy_action_unknown = 0,
    pa_policy_action_min = pa_policy_action_unknown,

    pa_policy_set_property,
    pa_policy_delete_property,

    pa_policy_action_max
};

enum pa_policy_object_type {
    pa_policy_object_unknown = 0,
    pa_policy_object_min = pa_policy_object_unknown,

    pa_policy_object_module,
    pa_policy_object_card,
    pa_policy_object_sink,
    pa_policy_object_source,
    pa_policy_object_sink_input,
    pa_policy_object_source_output,

    pa_policy_object_max
};

enum pa_policy_value_type {
    pa_policy_value_unknown = 0,
    pa_policy_value_min = pa_policy_value_unknown,

    pa_policy_value_constant, /* constant value */
    pa_policy_value_copy,     /* copy of the value of context var. */

    pa_policy_value_max
};

struct pa_policy_match {
    int                               (*method)(const char *,
                                                union pa_classify_arg *);
    union pa_classify_arg               arg;
};

struct pa_policy_object {
    enum pa_policy_object_type          type;
    struct pa_policy_match              match;
    void                               *ptr;
    unsigned long                       index;
};

struct pa_policy_value_constant {
    enum pa_policy_value_type           type;
    char                               *string;    
};

struct pa_policy_value_copy {
    enum pa_policy_value_type           type;
};

union pa_policy_value {
    enum pa_policy_value_type           type;
    struct pa_policy_value_constant     constant;
    struct pa_policy_value_copy         copy;
};

#define PA_POLICY_CONTEXT_ACTION_COMMON         \
    union pa_policy_context_action     *next;   \
    int                                 lineno; \
    enum pa_policy_action_type          type

struct pa_policy_context_action_any {
    PA_POLICY_CONTEXT_ACTION_COMMON;
};


struct pa_policy_set_property {
    PA_POLICY_CONTEXT_ACTION_COMMON;
    struct pa_policy_object             object;
    char                               *property;
    union pa_policy_value               value;
};

struct pa_policy_del_property {
    PA_POLICY_CONTEXT_ACTION_COMMON;
    struct pa_policy_object             object;
    char                               *property;
};

union pa_policy_context_action {
    struct pa_policy_context_action_any any;
    struct pa_policy_set_property       setprop;
    struct pa_policy_del_property       delprop;
};

struct pa_policy_context_rule {
    struct pa_policy_context_rule      *next;
    struct pa_policy_match              match; /* for the variable value */
    union pa_policy_context_action     *actions;
};

struct pa_policy_context_variable {
    struct pa_policy_context_variable  *next;
    char                               *name;
    char                               *value;
    struct pa_policy_context_rule      *rules;
};

struct pa_policy_context {
    struct pa_policy_context_variable  *variables;
};


struct pa_policy_context *pa_policy_context_new(struct userdata *);
void pa_policy_context_free(struct pa_policy_context *);

void pa_policy_context_register(struct userdata *, enum pa_policy_object_type,
                                const char *, void *);
void pa_policy_context_unregister(struct userdata *,enum pa_policy_object_type,
                                  const char *, void *, unsigned long);


struct pa_policy_context_rule
    *pa_policy_context_add_property_rule(struct userdata *, char *,
                                         enum pa_classify_method, char *);

void pa_policy_context_add_property_action(struct pa_policy_context_rule *,int,
                                           enum pa_policy_object_type,
                                           enum pa_classify_method, char *,
                                           char *,
                                           enum pa_policy_value_type, ...);

void pa_policy_context_delete_property_action(struct pa_policy_context_rule *,
                                              int,
                                              enum pa_policy_object_type,
                                              enum pa_classify_method,
                                              char *, char *);

int pa_policy_context_variable_changed(struct userdata *, char *, char *);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
