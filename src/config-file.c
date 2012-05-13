
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>

#ifndef __USE_ISOC99
#define __USE_ISOC99
#include <ctype.h>
#undef __USE_ISOC99
#else
#include <ctype.h>
#endif

#include <pulsecore/pulsecore-config.h>

#include <pulsecore/core-util.h>
#include <pulsecore/log.h>

#include "config-file.h"
#include "policy-group.h"
#include "classify.h"
#include "context.h"

#define DEFAULT_CONFIG_FILE        "policy.conf"
#define DEFAULT_CONFIG_DIRECTORY   "/etc/pulse/xpolicy.conf.d"

enum section_type {
    section_unknown = 0,
    section_group,
    section_device,
    section_card,
    section_stream,
    section_context,
    section_max
};

enum device_class {
    device_unknown = 0,
    device_sink,
    device_source,
    device_max
};


#define PROPERTY_ACTION_COMMON                                                \
    enum pa_policy_object_type objtype; /* eg. sink, source, sink-input etc */\
    enum pa_classify_method    method;  /* obj.name based classif. method   */\
    char                      *arg;     /* obj.name based classif. argument */\
    char                      *propnam  /* name of property                 */

struct anyprop {
    PROPERTY_ACTION_COMMON;
};

struct setprop {                         /* set property of a PA object */
    PROPERTY_ACTION_COMMON;
    enum pa_policy_value_type  valtype;  /* type of prop.value to be set */
    char                      *valarg;   /* arg for value setting, if any */
};

struct delprop {                         /* delete property of a PA object */
    PROPERTY_ACTION_COMMON;
};


struct ctxact {                          /* context rule actions */
    enum pa_policy_action_type type;     /* context action type */
    int                        lineno;   /* reference to config file */
    union {
        struct anyprop         anyprop;  /* common for all prop.operation */
        struct setprop         setprop;  /* setting property of an object */
        struct delprop         delprop;  /* deleting property of an object */
    };
};


struct groupdef {
    char                    *name;
    char                    *sink;
    char                    *source;
    uint32_t                 flags;
};

struct devicedef {
    enum device_class        class;
    char                    *type;
    char                    *prop;
    enum pa_classify_method  method;
    char                    *arg;
    pa_hashmap              *ports; /* Key: device name, value:
                                     * pa_classify_port_entry. */
    uint32_t                 flags;
};

struct carddef {
    char                    *type;
    enum pa_classify_method  method;
    char                    *arg;
    char                    *profile;
    uint32_t                 flags;
};

struct streamdef {
    char                    *prop;   /* stream property to classify it */
    enum pa_classify_method  method; /* property based classification method */
    char                    *arg;    /* param for prop.based classification */
    char                    *clnam;  /* client's name in pulse audio */
    uid_t                    uid;    /* client's user id */
    char                    *exe;    /* the executable name (i.e. argv[0]) */
    char                    *group;  /* group name the stream belong to */
    uint32_t                 flags;  /* stream flags */
    char                    *port;   /* port for local routing, if any */
};


struct contextdef {
    char                    *varnam; /* context variable name */
    enum pa_classify_method  method; /* context value based classification */
    char                    *arg;    /* param for ctx.value classification */
    int                      nact;   /* number of actions */
    struct ctxact           *acts;   /* array of actions */
};

struct section {
    enum section_type        type;
    union {
        void              *any;
        struct groupdef   *group;
        struct devicedef  *device;
        struct carddef    *card;
        struct streamdef  *stream;
        struct contextdef *context;
    }                        def;
};


static int preprocess_buffer(int, char *, char *);

static int section_header(int, char *, enum section_type *);
static int section_open(struct userdata *, enum section_type,struct section *);
static int section_close(struct userdata *, struct section *);

static int groupdef_parse(int, char *, struct groupdef *);
static int devicedef_parse(int, char *, struct devicedef *);
static int carddef_parse(int, char *, struct carddef *);
static int streamdef_parse(int, char *, struct streamdef *);
static int contextdef_parse(int, char *, struct contextdef *);

static int deviceprop_parse(int, enum device_class,char *,struct devicedef *);
static int ports_parse(int, const char *, struct devicedef *);
static int streamprop_parse(int, char *, struct streamdef *);
static int contextval_parse(int, char *, struct contextdef *);
static int contextsetprop_parse(int, char *, struct contextdef *);
static int contextdelprop_parse(int, char *, struct contextdef *);
static int contextanyprop_parse(int, char *, char *, struct anyprop *);
static int cardname_parse(int, char *, struct carddef *);
static int flags_parse(int, char *, enum section_type, uint32_t *);
static int valid_label(int, char *);

#ifndef HAS_SPLIT_STRV
static char **pa_split_strv(const char *, const char *);
#endif

int pa_policy_parse_config_file(struct userdata *u, const char *cfgfile)
{
#define BUFSIZE 512

    FILE              *f;
    char               cfgpath[PATH_MAX];
    char               ovrpath[PATH_MAX];
    char              *path;
    char               buf[BUFSIZE];
    char               line[BUFSIZE];
    int                lineno;
    enum section_type  newsect;
    struct section     section;
    struct groupdef   *grdef;
    struct devicedef  *devdef;
    struct carddef    *carddef;
    struct streamdef  *strdef;
    struct contextdef *ctxdef;
    int                success;

    pa_assert(u);

    if (!cfgfile)
        cfgfile = DEFAULT_CONFIG_FILE;

    pa_policy_file_path(cfgfile, cfgpath, PATH_MAX);
    snprintf(ovrpath, PATH_MAX, "%s.override", cfgpath);

    if ((f = fopen(ovrpath,"r")) != NULL)
        path = ovrpath;
    else if ((f = fopen(cfgpath, "r")) != NULL)
        path = cfgpath;
    else {
        pa_log("Can't open config file '%s': %s", cfgpath, strerror(errno));
        return 0;
    }

    pa_log_info("parsing config file '%s'", path);

    success = TRUE;                    /* assume successful operation */

    memset(&section, 0, sizeof(section));

    for (errno = 0, lineno = 1;  fgets(buf, BUFSIZE, f) != NULL;  lineno++) {
        if (preprocess_buffer(lineno, buf, line) < 0)
            break;

        if (*line == '\0')
            continue;

        if (section_header(lineno, line, &newsect)) {
            if (section_close(u, &section) < 0)
                success = FALSE;

            section.type = newsect;

            if (section_open(u, newsect, &section) < 0)
                success = FALSE;
        }
        else {
            switch (section.type) {

            case section_group:
                grdef = section.def.group;

                if (groupdef_parse(lineno, line, grdef) < 0)
                    success = FALSE;

                break;

            case section_device:
                devdef = section.def.device;

                if (devicedef_parse(lineno, line, devdef) < 0)
                    success = FALSE;

                break;

            case section_card:
                carddef = section.def.card;

                if (carddef_parse(lineno, line, carddef) < 0)
                    success = FALSE;

                break;

            case section_stream:
                strdef = section.def.stream;

                if (streamdef_parse(lineno, line, strdef) < 0)
                    success = FALSE;
                
                break;

            case section_context:
                ctxdef = section.def.context;

                if (contextdef_parse(lineno, line, ctxdef) < 0)
                    success = FALSE;

                break;
                
            default:
                break;

            }
        }
    }

    section_close(u, &section);
    endpwent();

    if (fclose(f) != 0) {
        pa_log("Can't close config file '%s': %s", path, strerror(errno));
    }

    return success;
}

int pa_policy_parse_files_in_configdir(struct userdata *u, const char *cfgdir)
{
#define BUFSIZE 512

    DIR               *d;
    FILE              *f;
    struct dirent     *e;
    const char        *p;
    char              *q;
    int                l;
    char               cfgpath[PATH_MAX];
    char             **overrides;
    int                noverride;
    char               buf[BUFSIZE];
    char               line[BUFSIZE];
    int                lineno;
    enum section_type  newsect;
    struct section     section;
    int                i;

    pa_assert(u);

    if (!cfgdir)
        cfgdir = DEFAULT_CONFIG_DIRECTORY;

    pa_log_info("policy config directory is '%s'", cfgdir);

    overrides = NULL;
    noverride = 0;

    if ((d = opendir(cfgdir)) != NULL) {
        while ((e = readdir(d)) != NULL) {
            if ((p = strstr(e->d_name, ".conf.override")) == NULL || p[14])
                continue;       /* does not match '*.conf.override' */

            l = (p + 5) - e->d_name; /* length of '*.conf' */
            q = pa_xmalloc(l + 1);
            strncpy(q, e->d_name, l);
            q[l] = '\0';

            overrides = pa_xrealloc(overrides, (noverride+1) * sizeof(char *));
            overrides[noverride++] = q;
        }
        closedir(d);
    }

    if ((d = opendir(cfgdir)) == NULL)
        pa_log("Can't find config directory '%s'", cfgdir);
    else {
        for (p = cfgdir, q = cfgpath;  (q-cfgpath < PATH_MAX) && *p;   p++,q++)
            *q = *p;
        if (q == cfgpath || q[-1] != '/')
            *q++ = '/'; 
        l = (cfgpath + PATH_MAX) - q;

        errno = 0;

        while (l > 1 && (e = readdir(d)) != NULL) {
            if ((p = strstr(e->d_name, ".conf")) != NULL && !p[5]) {
                for (i = 0;  i < noverride; i++) {
                    if (!strcmp(e->d_name, overrides[i]))
                        break;
                }

                if (i < noverride) {
                    strncpy(q, e->d_name, l);
                    cfgpath[PATH_MAX-1] = '\0';
                    pa_log_info("skip overriden config file '%s'", cfgpath);
                    continue;
                }
            }
            else if ((p = strstr(e->d_name,".conf.override")) == NULL || p[14])
                continue;       /* neither '*.conf' nor '*.conf.override' */

            strncpy(q, e->d_name, l);
            cfgpath[PATH_MAX-1] = '\0';

            pa_log_info("parsing config file '%s'", cfgpath);


            if ((f = fopen(cfgpath, "r")) == NULL) {
                pa_log("Can't open config file '%s': %s",
                       cfgpath, strerror(errno));
                continue;
            }

            memset(&section, 0, sizeof(section));

            for (errno = 0, lineno = 1;  fgets(buf, BUFSIZE, f);   lineno++) {

                if (preprocess_buffer(lineno, buf, line) < 0)
                    break;

                if (*line == '\0')
                    continue;

                if (section_header(lineno, line, &newsect)) {
                    section_close(u, &section);

                    if ((section.type = newsect) == section_stream)
                        section_open(u, newsect, &section);
                    else {
                        pa_log("line %d: only [stream] section is allowed",
                               lineno);
                    }
                }
                else {
                    switch (section.type) {
                    case section_stream:
                        streamdef_parse(lineno, line, section.def.stream);
                        break;
                    default:
                        break;
                    }
                }
            } /* for fgets() */

            section_close(u, &section);
            endpwent();

            if (fclose(f) != 0) {
                pa_log("Can't close config file '%s': %s",
                       cfgpath, strerror(errno));
            }
        } /* while readdir() */

        closedir(d);

    } /* if opendir() */


    for (i = 0; i < noverride; i++)
        pa_xfree(overrides[i]);

    pa_xfree(overrides);


    return TRUE;
}

static int preprocess_buffer(int lineno, char *inbuf, char *outbuf)
{
    char c, *p, *q;
    int  quote;
    int  sts = 0;

    for (quote = 0, p = inbuf, q = outbuf;   (c = *p) != '\0';   p++) {
        if (!quote && isblank(c))
            continue;
        
        if (c == '\n' || (!quote && c == '#'))
            break;
        
        if (c == '"') {
            quote ^= 1;
            continue;
        }
        
        if (c < 0x20) {
            pa_log("Illegal character 0x%02x in line %d", c, lineno);
            sts = -1;
            errno = EILSEQ;
            break;
        }
        
        *q++ = c;
    }
    *q = '\0';

    if (quote) {
        pa_log("unterminated quoted string '%s' in line %d", inbuf, lineno);
    }

    return sts;
}


static int section_header(int lineno, char *line, enum section_type *type)
{
    int is_section;

    if (line[0] != '[')
        is_section = 0;
    else {
        is_section = 1;

        if (!strcmp(line, "[group]"))
            *type = section_group;
        else if (!strcmp(line,"[device]"))
            *type = section_device;
        else if (!strcmp(line,"[card]"))
            *type = section_card;
        else if (!strcmp(line, "[stream]"))
            *type = section_stream;
        else if (!strcmp(line, "[context-rule]"))
            *type = section_context;
        else {
            *type = section_unknown;
            pa_log("Invalid section type '%s' in line %d", line, lineno);
        }
    }

    return is_section;
}

static int section_open(struct userdata *u, enum section_type type,
                        struct section *sec)
{
    int status;

    if (sec == NULL)
        status = -1;
    else {
        switch (type) {
            
        case section_group:
            sec->def.group = pa_xnew0(struct groupdef, 1);
            status = 0;
            break;
            
        case section_device:
            sec->def.device = pa_xnew0(struct devicedef, 1);
            status = 0;
            break;

        case section_card:
            sec->def.card = pa_xnew0(struct carddef, 1);
            status = 0;
            break;

        case section_stream:
            sec->def.stream = pa_xnew0(struct streamdef, 1);
            sec->def.stream->uid = -1;
            status = 0;
            break;

        case section_context:
            sec->def.context = pa_xnew0(struct contextdef, 1);
            sec->def.context->method = pa_method_true;
            status = 0;
            break;
            
        default:
            type = section_unknown;
            sec->def.any = NULL;
            status = -1;
            break;
        }

        sec->type = type;
    }

    return status;
}

static int section_close(struct userdata *u, struct section *sec)
{
    struct groupdef   *grdef;
    struct devicedef  *devdef;
    struct carddef    *carddef;
    struct streamdef  *strdef;
    struct contextdef *ctxdef;
    struct ctxact     *act;
    struct pa_policy_context_rule *rule;
    struct setprop    *setprop;
    struct delprop    *delprop;
    int                i;
    int                status;

    if (sec == NULL)
        status = -1;
    else {
        switch (sec->type) {
            
        case section_group:
            status = 0;
            grdef  = sec->def.group;

            pa_policy_group_new(u, grdef->name, grdef->sink,
                                grdef->source, grdef->flags);

            pa_xfree(grdef->name);
            pa_xfree(grdef->sink);
            pa_xfree(grdef->source);
            pa_xfree(grdef);

            break;
            
        case section_device:
            status = 0;
            devdef = sec->def.device;

            switch (devdef->class) {

            case device_sink:
                /* All devdef values are deep copied. */
                pa_classify_add_sink(u, devdef->type,
                                     devdef->prop, devdef->method, devdef->arg,
                                     devdef->ports, devdef->flags);
                break;

            case device_source:
                /* All devdef values are deep copied. */
                pa_classify_add_source(u, devdef->type,
                                       devdef->prop, devdef->method,
                                       devdef->arg, devdef->ports,
                                       devdef->flags);
                break;

            default:
                break;
            }

            if (devdef->ports) {
                struct pa_classify_port_entry *port;

                while ((port = pa_hashmap_steal_first(devdef->ports)))
                    pa_classify_port_entry_free(port);

                pa_hashmap_free(devdef->ports, NULL, NULL);
            }

            pa_xfree(devdef->type);
            pa_xfree(devdef->prop);
            pa_xfree(devdef->arg);
            pa_xfree(devdef);

            break;

        case section_card:
            status = 0;
            carddef = sec->def.card;

            pa_classify_add_card(u, carddef->type, carddef->method, 
                                 carddef->arg, carddef->profile,
                                 carddef->flags);
            
            pa_xfree(carddef->type);
            pa_xfree(carddef->arg);
            pa_xfree(carddef->profile);
            pa_xfree(carddef);

            break;

        case section_stream:
            status = 0;
            strdef = sec->def.stream;

            if (strdef->port)
                strdef->flags |= PA_POLICY_LOCAL_ROUTE;

            pa_classify_add_stream(u, strdef->prop,strdef->method,strdef->arg,
                                   strdef->clnam, strdef->uid, strdef->exe,
                                   strdef->group, strdef->flags, strdef->port);

            pa_xfree(strdef->prop);
            pa_xfree(strdef->arg);
            pa_xfree(strdef->clnam);
            pa_xfree(strdef->exe);
            pa_xfree(strdef->group);
            pa_xfree(strdef->port);
            pa_xfree(strdef);

            break;

        case section_context:
            status = 0;
            ctxdef = sec->def.context;

            rule = pa_policy_context_add_property_rule(u, ctxdef->varnam,
                                                       ctxdef->method,
                                                       ctxdef->arg);

            for (i = 0;  i < ctxdef->nact;  i++) {
                act = ctxdef->acts + i;

                switch (act->type) {

                case pa_policy_set_property:
                    setprop = &act->setprop;

                    if (rule != NULL) {
                        pa_policy_context_add_property_action(
                                          rule, act->lineno,
                                          setprop->objtype,
                                          setprop->method,
                                          setprop->arg,
                                          setprop->propnam,
                                          setprop->valtype,
                                          setprop->valarg
                        );
                    }

                    pa_xfree(setprop->arg);
                    pa_xfree(setprop->propnam);
                    pa_xfree(setprop->valarg);

                    break;

                case pa_policy_delete_property:
                    delprop = &act->delprop;

                    if (rule != NULL) {
                        pa_policy_context_delete_property_action(
                                          rule, act->lineno,
                                          delprop->objtype,
                                          delprop->method,
                                          delprop->arg,
                                          delprop->propnam
                        );
                    }

                    pa_xfree(delprop->arg);
                    pa_xfree(delprop->propnam);

                    break;

                default:
                    break;
                }
            }

            pa_xfree(ctxdef->varnam);
            pa_xfree(ctxdef->arg);
            pa_xfree(ctxdef->acts);
            pa_xfree(ctxdef);

            break;
            
        default:
            status = 0;
            break;
        }
        
        sec->type = section_unknown;
        sec->def.any = NULL;
    }

    return status;
}


static int groupdef_parse(int lineno, char *line, struct groupdef *grdef)
{
    int       sts = 0;
    char     *end;
    char     *comma;
    char     *fldef;
    char     *flname;
    int       len;
    uint32_t  flags;

    if (grdef == NULL)
        sts = -1;
    else {
        if (!strncmp(line, "name=", 5)) {
            if (!valid_label(lineno, line+5))
                sts = -1;
            else
                grdef->name = pa_xstrdup(line+5);
        }
        else if (!strncmp(line, "sink=", 5)) {
            grdef->sink = pa_xstrdup(line+5);
        }
        else if (!strncmp(line, "source=", 7)) {
            grdef->source = pa_xstrdup(line+7);
        }
        else if (!strncmp(line, "flags=", 6)) { 
            fldef = line + 6;
            
            if (fldef[0] == '\0') {
                sts = -1;
                pa_log("missing flag definition in line %d", lineno);
            }
            else {
                sts = 0;

                if (!strcmp(fldef, "client"))
                    flags = PA_POLICY_GROUP_FLAGS_CLIENT;
                else if (!strcmp(fldef, "nopolicy"))
                    flags = PA_POLICY_GROUP_FLAGS_NOPOLICY;
                else {
                    flags = 0;

                    for (flname = fldef;  *flname;  flname += len) {
                        if ((comma = strchr(flname, ',')) == NULL)
                            len = strlen(flname);
                        else {
                            *comma = '\0';
                            len = (comma - flname) + 1;
                        }

                        if (!strcmp(flname, "set_sink"))
                            flags |= PA_POLICY_GROUP_FLAG_SET_SINK;
                        else if (!strcmp(flname, "set_source"))
                            flags |= PA_POLICY_GROUP_FLAG_SET_SOURCE;
                        else if (!strcmp(flname, "route_audio"))
                            flags |= PA_POLICY_GROUP_FLAG_ROUTE_AUDIO;
                        else if (!strcmp(flname, "limit_volume"))
                            flags |= PA_POLICY_GROUP_FLAG_LIMIT_VOLUME;
                        else if (!strcmp(flname, "cork_stream"))
                            flags |= PA_POLICY_GROUP_FLAG_CORK_STREAM;
                        else if (!strcmp(flname, "mute_by_route"))
                            flags |= PA_POLICY_GROUP_FLAG_MUTE_BY_ROUTE;
                        else {
                            pa_log("invalid flag '%s' in line %d",
                                   flname, lineno);
                            sts = -1;
                            break;
                        }
                    } /* for */
                }

                if (sts >= 0) {
                    grdef->flags = flags;
                }
            }
        }
        else {
            if ((end = strchr(line, '=')) == NULL) {
                pa_log("invalid definition '%s' in line %d", line, lineno);
            }
            else {
                *end = '\0';
                pa_log("invalid key value '%s' in line %d", line, lineno);
            }
            sts = -1;
        }
    }

    return sts;
}

static int devicedef_parse(int lineno, char *line, struct devicedef *devdef)
{
    int   sts;
    char *end;

    if (devdef == NULL)
        sts = -1;
    else {
        sts = 0;

        if (!strncmp(line, "type=", 5)) {
            devdef->type = pa_xstrdup(line+5);
        }
        else if (!strncmp(line, "sink=", 5)) {
            sts = deviceprop_parse(lineno, device_sink, line+5, devdef);
        }
        else if (!strncmp(line, "source=", 7)) {
            sts = deviceprop_parse(lineno, device_source, line+7, devdef);
        }
        else if (!strncmp(line, "ports=", 6)) {
            sts = ports_parse(lineno, line+6, devdef);
        }
        else if (!strncmp(line, "flags=", 6)) {
            sts = flags_parse(lineno, line+6, section_device, &devdef->flags);
        }
        else {
            if ((end = strchr(line, '=')) == NULL) {
                pa_log("invalid definition '%s' in line %d", line, lineno);
            }
            else {
                *end = '\0';
                pa_log("invalid key value '%s' in line %d", line, lineno);
            }
            sts = -1;
        }
    }

    return sts;
}

static int carddef_parse(int lineno, char *line, struct carddef *carddef)
{
    int   sts;
    char *end;

    if (carddef == NULL)
        sts = -1;
    else {
        sts = 0;

        if (!strncmp(line, "type=", 5)) {
            carddef->type = pa_xstrdup(line+5);
        }
        else if (!strncmp(line, "name=", 5)) {
            sts = cardname_parse(lineno, line+5, carddef);
        }
        else if (!strncmp(line, "profile=", 8)) {
            carddef->profile = pa_xstrdup(line+8);
        }
        else if (!strncmp(line, "flags=", 6)) {
            sts = flags_parse(lineno, line+6, section_card, &carddef->flags);
        }
        else {
            if ((end = strchr(line, '=')) == NULL) {
                pa_log("invalid definition '%s' in line %d", line, lineno);
            }
            else {
                *end = '\0';
                pa_log("invalid key value '%s' in line %d", line, lineno);
            }
            sts = -1;
        }
    }

    return sts;
}

static int streamdef_parse(int lineno, char *line, struct streamdef *strdef)
{
    int            sts;
    char          *user;
    struct passwd *pwd;
    int            uid;
    char          *end;

    if (strdef == NULL)
        sts = -1;
    else {
        sts = 0;

        if (!strncmp(line, "name=", 5)) {
            strdef->prop   = pa_xstrdup(PA_PROP_MEDIA_NAME);
            strdef->method = pa_method_equals;
            strdef->arg = pa_xstrdup(line+5);
        }
        else if (!strncmp(line, "property=", 9)) {
            sts = streamprop_parse(lineno, line+9, strdef);
        }
        else if (!strncmp(line, "client=", 7)) {
            strdef->clnam = pa_xstrdup(line+7);
        }
        else if (!strncmp(line, "user=", 5)) {
            user = line+5;
            uid  = strtol(user, &end, 10);

            if (end == user || *end != '\0' || uid < 0) {
                uid = -1;
                setpwent();

                while ((pwd = getpwent()) != NULL) {
                    if (!strcmp(user, pwd->pw_name)) {
                        uid = pwd->pw_uid;
                        break;
                    }
                }

                if (uid < 0) {
                    pa_log("invalid user '%s' in line %d", user, lineno);
                    sts = -1;
                }
            }

            strdef->uid = (uid_t) uid;
        }
        else if (!strncmp(line, "exe=", 4)) {
            strdef->exe = pa_xstrdup(line+4);
        }
        else if (!strncmp(line, "group=", 6)) {
            strdef->group = pa_xstrdup(line+6);
        }
        else if (!strncmp(line, "flags=", 6)) {
            sts = flags_parse(lineno, line+6, section_stream, &strdef->flags);
        }
        else if (!strncmp(line, "port_if_active=", 15)) {
            strdef->port = pa_xstrdup(line+15);
        }
        else {
            if ((end = strchr(line, '=')) == NULL) {
                pa_log("invalid definition '%s' in line %d", line, lineno);
            }
            else {
                *end = '\0';
                pa_log("invalid key value '%s' in line %d", line, lineno);
            }
            sts = -1;
        }
    }

    return sts;
}

static int contextdef_parse(int lineno, char *line, struct contextdef *ctxdef)
{
    int   sts;
    char *end;

    if (ctxdef == NULL)
        sts = -1;
    else {
        sts = 0;

        if (!strncmp(line, "variable=", 9)) {
            ctxdef->varnam = pa_xstrdup(line+9);
        }
        else if (!strncmp(line, "value=", 6)) {
            sts = contextval_parse(lineno, line+6, ctxdef);
        }
        else if (!strncmp(line, "set-property=", 13)) {
            sts = contextsetprop_parse(lineno, line+13, ctxdef);
        }
        else if (!strncmp(line, "delete-property=", 16)) { 
            sts = contextdelprop_parse(lineno, line+16, ctxdef);
        }
        else {
            if ((end = strchr(line, '=')) == NULL) {
                pa_log("invalid definition '%s' in line %d", line, lineno);
            }
            else {
                *end = '\0';
                pa_log("invalid key value '%s' in line %d", line, lineno);
            }
            sts = -1;
        }
    }

    return sts;
}

static int deviceprop_parse(int lineno, enum device_class class,
                            char *propdef, struct devicedef *devdef)
{
    char *colon;
    char *at;
    char *prop;
    char *method;
    char *arg;

    if ((colon = strchr(propdef, ':')) == NULL) {
        pa_log("invalid definition '%s' in line %d", propdef, lineno);
        return -1;
    }

    *colon = '\0';
    arg    = colon + 1;

    if ((at = strchr(propdef, '@')) == NULL) {
        prop   = "name";
        method = propdef;
    }
    else {
        *at    = '\0';
        prop   = propdef;
        method = at + 1;
    }
    
    if (!strcmp(method, "equals"))
        devdef->method = pa_method_equals;
    else if (!strcmp(method, "startswith"))
        devdef->method = pa_method_startswith;
    else if (!strcmp(method, "matches"))
        devdef->method = pa_method_matches;
    else {
        pa_log("invalid method '%s' in line %d", method, lineno);
        return -1;
    }
    
    devdef->class = class;
    devdef->prop  = pa_xstrdup(prop);
    devdef->arg   = pa_xstrdup(arg);
    
    return 0;
}

static int ports_parse(int lineno, const char *portsdef,
                       struct devicedef *devdef)
{
#if PULSEAUDIO_HAS_PORTS
    char **entries;

    if (devdef->ports) {
        struct pa_classify_port_entry *port;

        pa_log("Duplicate ports= line in line %d, using the last "
               "occurrence.", lineno);

        while ((port = pa_hashmap_steal_first(devdef->ports)))
            pa_classify_port_entry_free(port);

        pa_hashmap_free(devdef->ports, NULL, NULL);
    }

    devdef->ports = pa_hashmap_new(pa_idxset_string_hash_func,
                                   pa_idxset_string_compare_func);

    if ((entries = pa_split_strv(portsdef, ","))) {
        char *entry; /* This string has format "sinkname:portname". */
        int i = 0;

        while ((entry = entries[i++])) {
            struct pa_classify_port_entry *port;
            size_t entry_len;
            size_t colon_pos;

            if (!*entry) {
                pa_log_debug("Ignoring a redundant comma in line %d", lineno);
                continue;
            }

            entry_len = strlen(entry);
            colon_pos = strcspn(entry, ":");

            if (colon_pos == entry_len) {
                pa_log("Colon missing in port entry '%s' in line %d, ignoring "
                       "the entry", entry, lineno);
                continue;
            } else if (colon_pos == 0) {
                pa_log("Empty device name in port entry '%s' in line %d, "
                       "ignoring the entry", entry, lineno);
                continue;
            } else if (colon_pos == entry_len - 1) {
                pa_log("Empty port name in port entry '%s' in line %d, "
                       "ignoring the entry", entry, lineno);
                continue;
            }

            port = pa_xnew(struct pa_classify_port_entry, 1);
            port->device_name = pa_xstrndup(entry, colon_pos);
            port->port_name = pa_xstrdup(entry + colon_pos + 1);

            if (pa_hashmap_put(devdef->ports, port->device_name, port) < 0) {
                pa_log("Duplicate device name in port entry '%s' in line %d, "
                       "using the first occurrence", entry, lineno);

                pa_classify_port_entry_free(port);
            }
        }

        pa_xstrfreev(entries);

    } else
        pa_log_warn("Empty ports= definition in line %d", lineno);
#endif

    return 0;
}

static int streamprop_parse(int lineno,char *propdef,struct streamdef *strdef)
{
    char *colon;
    char *at;
    char *prop;
    char *method;
    char *arg;

    if ((colon = strchr(propdef, ':')) == NULL) {
        pa_log("invalid definition '%s' in line %d", propdef, lineno);
        return -1;
    }

    *colon = '\0';
    arg    = colon + 1;

    if ((at = strchr(propdef, '@')) == NULL) {
        pa_log("invalid definition '%s' in line %d", propdef, lineno);
        return -1;
    }

    *at    = '\0';
    prop   = propdef;
    method = at + 1;
    
    if (!strcmp(method, "equals"))
        strdef->method = pa_method_equals;
    else if (!strcmp(method, "startswith"))
        strdef->method = pa_method_startswith;
    else if (!strcmp(method, "matches"))
        strdef->method = pa_method_matches;
    else {
        pa_log("invalid method '%s' in line %d", method, lineno);
        return -1;
    }
    
    strdef->prop  = pa_xstrdup(prop);
    strdef->arg   = pa_xstrdup(arg);
    
    return 0;
}

static int contextval_parse(int lineno,char *valdef, struct contextdef *ctxdef)
{
    char *colon;
    char *method;
    char *arg;

    if ((colon = strchr(valdef, ':')) == NULL) {
        pa_log("invalid definition '%s' in line %d", valdef, lineno);
        return -1;
    }

    *colon = '\0';
    method = valdef;
    arg    = colon + 1;
    
    if (!strcmp(method, "equals"))
        ctxdef->method = pa_method_equals;
    else if (!strcmp(method, "startswith"))
        ctxdef->method = pa_method_startswith;
    else if (!strcmp(method, "matches"))
        ctxdef->method = strcmp(arg, "*") ? pa_method_matches : pa_method_true;
    else {
        pa_log("invalid method '%s' in line %d", method, lineno);
        return -1;
    }
    
    ctxdef->arg = (ctxdef->method == pa_method_true) ? NULL : pa_xstrdup(arg);
    
    return 0;
}

static int contextsetprop_parse(int lineno, char *setpropdef,
                                struct contextdef *ctxdef)
{
    size_t          size;
    struct ctxact  *act;
    struct setprop *setprop;
    struct anyprop *anyprop;
    char           *comma1;
    char           *comma2;
    char           *objdef;
    char           *propdef;
    char           *valdef;
    char           *valarg;

    /*
     * sink-name@startswidth:alsa,property:foo,value@constant:bar
     */

    size = sizeof(*act) * (ctxdef->nact + 1);
    act  = (ctxdef->acts = pa_xrealloc(ctxdef->acts, size)) + ctxdef->nact;

    memset(act, 0, sizeof(*act));
    act->type   = pa_policy_set_property;
    act->lineno = lineno;

    setprop = &act->setprop;
    anyprop = &act->anyprop;

    if ((comma1 = strchr(setpropdef, ',')) == NULL ||
        (comma2 = strchr(comma1 + 1, ',')) == NULL   )
    {
        pa_log("invalid definition '%s' in line %d", setpropdef, lineno);
        return -1;
    }

    *comma1 = '\0';
    *comma2 = '\0';
    
    objdef  = setpropdef;
    propdef = comma1 + 1;
    valdef  = comma2 + 1;

    if (!strncmp(valdef, "value@constant:", 15)) {
        setprop->valtype = pa_policy_value_constant;
        valarg = valdef + 15;
    }
    else if (!strncmp(valdef, "value@copy-from-context", 23)) {
        setprop->valtype = pa_policy_value_copy;
        valarg = NULL;
    }
    else {
        pa_log("invalid value definition '%s' in line %d", valdef, lineno);
        return -1;
    }
    
    if (contextanyprop_parse(lineno, objdef, propdef, anyprop) < 0)
        return -1;

    setprop->valarg  = valarg ? pa_xstrdup(valarg) : NULL;

    ctxdef->nact++;
    
    return 0;
}

static int contextdelprop_parse(int lineno, char *delpropdef,
                                struct contextdef *ctxdef)
{
    size_t          size;
    struct ctxact  *act;
    struct anyprop *anyprop;
    char           *comma;
    char           *objdef;
    char           *propdef;

    /*
     * sink-name@startswidth:alsa,property:foo
     */

    size = sizeof(*act) * (ctxdef->nact + 1);
    act  = (ctxdef->acts = pa_xrealloc(ctxdef->acts, size)) + ctxdef->nact;

    memset(act, 0, sizeof(*act));
    act->type   = pa_policy_delete_property;
    act->lineno = lineno;

    anyprop = &act->anyprop;

    if ((comma = strchr(delpropdef, ',')) == NULL) {
        pa_log("invalid definition '%s' in line %d", delpropdef, lineno);
        return -1;
    }

    *comma = '\0';
    
    objdef  = delpropdef;
    propdef = comma + 1;

    if (contextanyprop_parse(lineno, objdef, propdef, anyprop) < 0)
        return -1;

    ctxdef->nact++;
    
    return 0;
}

static int contextanyprop_parse(int lineno, char *objdef, char *propdef,
                                struct anyprop *anyprop)
{
    char          *colon;
    char          *method;
    char          *arg;
    char          *propnam;

    /*
     * objdef  = "sink-name@startswidth:alsa"
     * propdef = "property:foo"
     */
    if (!strncmp(objdef, "module-name@", 12)) {
        anyprop->objtype = pa_policy_object_module;
        method = objdef + 12;
    }
    else if (!strncmp(objdef, "card-name@", 10)) {
        anyprop->objtype = pa_policy_object_card;
        method = objdef + 10;
    } 
    else if (!strncmp(objdef, "sink-name@", 10)) {
        anyprop->objtype = pa_policy_object_sink;
        method = objdef + 10;
    }
    else if (!strncmp(objdef, "source-name@", 12)) {
        anyprop->objtype = pa_policy_object_source;
        method = objdef + 12;
    }
    else if (!strncmp(objdef, "sink-input-name@", 16)) {
        anyprop->objtype = pa_policy_object_sink_input;
        method = objdef + 16;
    }
    else if (!strncmp(objdef, "source-output-name@", 19)) {
        anyprop->objtype = pa_policy_object_source_output;
        method = objdef + 19;
    }
    else {
        pa_log("invalid object definition in line %d", lineno);
        return -1;
    }

    if ((colon = strchr(method, ':')) == NULL) {
        pa_log("invalid object definition in line %d", lineno);
        return -1;
    }

    *colon = '\0';
    arg = colon + 1;


    if (!strcmp(method, "equals"))
        anyprop->method = pa_method_equals;
    else if (!strcmp(method, "startswith"))
        anyprop->method = pa_method_startswith;
    else if (!strcmp(method, "matches"))
        anyprop->method = pa_method_matches;
    else {
        pa_log("invalid method '%s' in line %d", method, lineno);
        return -1;
    }
    
    if (!strncmp(propdef, "property:", 9))
        propnam = propdef + 9;
    else {
        pa_log("invalid property definition '%s' in line %d", propdef, lineno);
        return -1;
    }

    anyprop->arg     = pa_xstrdup(arg);
    anyprop->propnam = pa_xstrdup(propnam);
    
    return 0;
}

static int cardname_parse(int lineno, char *namedef, struct carddef *carddef)
{
    char *colon;
    char *method;
    char *arg;

    if ((colon = strchr(namedef, ':')) == NULL) {
        pa_log("invalid definition '%s' in line %d", namedef, lineno);
        return -1;
    }

    *colon = '\0';
    method = namedef;
    arg    = colon + 1;

    if (!strcmp(method, "equals"))
        carddef->method = pa_method_equals;
    else if (!strcmp(method, "startswith"))
        carddef->method = pa_method_startswith;
    else if (!strcmp(method, "matches"))
        carddef->method = pa_method_matches;
    else {
        pa_log("invalid method '%s' in line %d", method, lineno);
        return -1;
    }
    
    carddef->arg   = pa_xstrdup(arg);
    
    return 0;
}

static int flags_parse(int lineno, char  *flagdef,
                       enum section_type  sectn,
                       uint32_t          *flags_ret)
{
    char     *comma;
    char     *flagname;
    uint32_t  flags;
    int       device, card, stream;

    flags = 0;

    device = card = stream = FALSE;

    switch (sectn) {
    case section_device:   device = TRUE;   break;
    case section_card:     card   = TRUE;   break;
    case section_stream:   stream = TRUE;   break;
    default:                                break;
    }


    while (*(flagname = flagdef) != '\0') {
        if ((comma = strchr(flagdef, ',')) == NULL)
            flagdef += strlen(flagdef);
        else {
            *comma = '\0';
            flagdef = comma + 1;
        }

        if ((device || card) && !strcmp(flagname, "disable_notify"))
            flags |= PA_POLICY_DISABLE_NOTIFY;
        else if (stream && !strcmp(flagname, "mute_if_active"))
            flags |= PA_POLICY_LOCAL_MUTE;
        else if (stream && !strcmp(flagname, "max_volume"))
            flags |= PA_POLICY_LOCAL_VOLMAX;
        else {
            pa_log("invalid flag '%s' in line %d", flagname, lineno);
            return -1;
        }
    }

    *flags_ret = flags;

    return 0;
}

static int valid_label(int lineno, char *label)
{
    int c;

    if (!isalpha(*label))
        goto invalid;

    while((c = *label++) != '\0') {
        if (!isalpha(c) && isdigit(c) && c != '-' && c != '_')
            goto invalid;
    }

    return 1;

 invalid:
    pa_log("invalid label '%s' in line %d", label, lineno);
    return 0;
}

#ifndef HAS_SPLIT_STRV
static char **pa_split_strv(const char *str, const char *sep)
{
#define MAX_ENTRIES  1024

    char  *ptrs[MAX_ENTRIES];
    char   buf[4096];
    char  *p, *q;
    int    len;
    int    i;
    char **ret;

    pa_assert(str);
    pa_assert(sep);

    strncpy(buf, str, sizeof(buf));
    buf[sizeof(buf)-1] = '\0';

    len = strlen(sep);

    for (p = buf, i = 0;    *p && i < MAX_ENTRIES - 1;    p = q) {
        ptrs[i++] = pa_xstrdup(p);

        if (!(q = strstr(p, sep)))
            q = p + strlen(p);
        else {
            *q = '\0';
            q += len;
        }
    }

    ptrs[i++] = NULL;

    len = sizeof(char *) * i;
    ret = pa_xmalloc(len);

    memcpy(ret, ptrs, len);

    return ret;

#undef MAX_ENTRIES
}
#endif


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
