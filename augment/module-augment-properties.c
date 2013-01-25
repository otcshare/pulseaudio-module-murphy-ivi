/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <pulsecore/pulsecore-config.h>

#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#if defined(HAVE_REGEX_H)
#include <regex.h>
#elif defined(HAVE_PCREPOSIX_H)
#include <pcreposix.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/client.h>
#include <pulsecore/conf-parser.h>
#include <pulsecore/hashmap.h>

#include "module-augment-properties-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("Augment the property sets of streams with additional static information");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

#ifndef CONFIG_FILE_DIR
#define CONFIG_FILE_DIR "/etc/pulse/augment_property_client_rules"
#endif

#ifndef SINK_INPUT_RULE_DIR
#define SINK_INPUT_RULE_DIR "/etc/pulse/augment_property_sink_input_rules"
#endif

#define STAT_INTERVAL 30
#define MAX_CACHE_SIZE 50

enum rule_match {
    RULE_UNDEFINED = 1,
    RULE_HIT,
    RULE_MISS
};

static const char* const valid_modargs[] = {
    NULL
};

struct rule {
    time_t timestamp;
    pa_bool_t good;
    time_t desktop_mtime;
    time_t conf_mtime;
    char *process_name;
    char *application_name;
    char *icon_name;
    char *role;
    pa_proplist *proplist;
};

struct sink_input_rule_file {
    pa_hashmap *rules;
    char *target_key;
    char *target_value;
    char *client_name;
    char *fn; /* for hashmap memory management */
};

struct sink_input_rule_section {
    char *stream_key;
    pa_bool_t comp;
    regex_t stream_value;
    char *section_name; /* for hashmap memory management */
};

struct userdata {
    pa_hashmap *cache;
    pa_hook_slot *client_new_slot, *client_proplist_changed_slot, *sink_input_new_slot;
    pa_hashmap *sink_input_rules;
    pa_client *directory_watch_client;
};

static void rule_free(struct rule *r) {
    pa_assert(r);

    pa_xfree(r->process_name);
    pa_xfree(r->application_name);
    pa_xfree(r->icon_name);
    pa_xfree(r->role);
    if (r->proplist)
        pa_proplist_free(r->proplist);
    pa_xfree(r);
}

static void sink_input_rule_free(struct sink_input_rule_section *s, struct userdata *u) {
    pa_assert(s);

    pa_xfree(s->stream_key);
    if (s->comp)
        regfree(&s->stream_value);
    pa_xfree(s->section_name);

    pa_xfree(s);
}

static void sink_input_rule_file_free(struct sink_input_rule_file *rf, struct userdata *u) {
    pa_assert(rf);

    pa_hashmap_free(rf->rules, (pa_free2_cb_t) sink_input_rule_free, NULL);
    pa_xfree(rf->client_name);
    pa_xfree(rf->target_key);
    pa_xfree(rf->target_value);
    pa_xfree(rf->fn);

    pa_xfree(rf);
}

static int parse_properties(
        const char *filename,
        unsigned line,
        const char *section,
        const char *lvalue,
        const char *rvalue,
        void *data,
        void *userdata) {

    struct rule *r = userdata;
    pa_proplist *n;

    if (!(n = pa_proplist_from_string(rvalue)))
        return -1;

    if (r->proplist) {
        pa_proplist_update(r->proplist, PA_UPDATE_MERGE, n);
        pa_proplist_free(n);
    } else
        r->proplist = n;

    return 0;
}

static int parse_categories(
        const char *filename,
        unsigned line,
        const char *section,
        const char *lvalue,
        const char *rvalue,
        void *data,
        void *userdata) {

    struct rule *r = userdata;
    const char *state = NULL;
    char *c;

    while ((c = pa_split(rvalue, ";", &state))) {

        if (pa_streq(c, "Game")) {
            pa_xfree(r->role);
            r->role = pa_xstrdup("game");
        } else if (pa_streq(c, "Telephony")) {
            pa_xfree(r->role);
            r->role = pa_xstrdup("phone");
        }

        pa_xfree(c);
    }

    return 0;
}

static int check_type(
        const char *filename,
        unsigned line,
        const char *section,
        const char *lvalue,
        const char *rvalue,
        void *data,
        void *userdata) {

    return pa_streq(rvalue, "Application") ? 0 : -1;
}

static int catch_all(
        const char *filename,
        unsigned line,
        const char *section,
        const char *lvalue,
        const char *rvalue,
        void *data,
        void *userdata) {

    return 0;
}

static void parse_file(struct rule *r, const char *fn, pa_config_item *table, pa_bool_t first) {
    char *application_name = NULL;
    char *icon_name = NULL;
    char *role = NULL;
    pa_proplist *p = NULL;

    if (first) {
        /* clean up before update */
        pa_xfree(r->application_name);
        pa_xfree(r->icon_name);
        pa_xfree(r->role);

        if (r->proplist)
            pa_proplist_clear(r->proplist);
    }
    else {
        /* keep the old data safe */
        application_name = r->application_name;
        icon_name = r->icon_name;
        role = r->role;
        p = r->proplist;
    }

    r->application_name = r->icon_name = r->role = NULL;

    table[0].data = &r->application_name;
    table[1].data = &r->icon_name;

    if (pa_config_parse(fn, NULL, table, r) < 0)
        pa_log_warn("Failed to parse file %s.", fn);

    if (!first) {
        /* copy the old data in place if there was no new data, merge the proplist */

        if (r->application_name)
            pa_xfree(application_name);
        else
            r->application_name = application_name;

        if (r->icon_name)
            pa_xfree(icon_name);
        else
            r->icon_name = icon_name;

        if (r->role)
            pa_xfree(role);
        else
            r->role = role;

        if (p) {
            if (r->proplist) {
                pa_proplist_update(r->proplist, PA_UPDATE_MERGE, p);
                pa_proplist_clear(p);
            }
            else {
                r->proplist = p;
            }
        }
    }
}

static void update_rule(struct rule *r) {
    char *fn;
    struct stat st;
    static pa_config_item table[] = {
        { "Name", pa_config_parse_string,              NULL, "Desktop Entry" },
        { "Icon", pa_config_parse_string,              NULL, "Desktop Entry" },
        { "Type", check_type,                          NULL, "Desktop Entry" },
        { "X-PulseAudio-Properties", parse_properties, NULL, "Desktop Entry" },
        { "Categories", parse_categories,              NULL, "Desktop Entry" },
        { NULL,  catch_all, NULL, NULL },
        { NULL, NULL, NULL, NULL },
    };
    pa_bool_t found = FALSE;

    pa_assert(r);

    /* Check first the non-graphical applications configuration file. If a
       file corresponding to the process isn't found, go and check the desktop
       files. If a file is found, augment it with the desktop data anyway. */

    fn = pa_sprintf_malloc(CONFIG_FILE_DIR PA_PATH_SEP "%s.conf", r->process_name);

    pa_log_debug("Looking for file %s", fn);

    if (stat(fn, &st) == 0)
        found = TRUE;

    if (!found)
        r->good = FALSE;

    if (found && !(r->good && st.st_mtime == r->conf_mtime)) {
        /* Theoretically the filename could have changed, but if so
           having the same mtime is very unlikely so not worth tracking it in r */
        if (r->good)
            pa_log_debug("Found %s (which has been updated since we last checked).", fn);
        else
            pa_log_debug("Found %s.", fn);

        parse_file(r, fn, table, TRUE);
        r->conf_mtime = st.st_mtime;
        r->good = TRUE;
    }

    pa_xfree(fn);
    found = FALSE;

    fn = pa_sprintf_malloc(DESKTOPFILEDIR PA_PATH_SEP "%s.desktop", r->process_name);

    pa_log_debug("Looking for file %s", fn);

    if (stat(fn, &st) == 0)
        found = TRUE;
    else {
#ifdef DT_DIR
        DIR *desktopfiles_dir;
        struct dirent *dir;

        /* Let's try a more aggressive search, but only one level */
        if ((desktopfiles_dir = opendir(DESKTOPFILEDIR))) {
            while ((dir = readdir(desktopfiles_dir))) {
                if (dir->d_type != DT_DIR
                    || strcmp(dir->d_name, ".") == 0
                    || strcmp(dir->d_name, "..") == 0)
                    continue;

                pa_xfree(fn);
                fn = pa_sprintf_malloc(DESKTOPFILEDIR PA_PATH_SEP "%s" PA_PATH_SEP "%s.desktop", dir->d_name, r->process_name);

                if (stat(fn, &st) == 0) {
                    found = TRUE;
                    break;
                }
            }
            closedir(desktopfiles_dir);
        }
#endif
    }
    if (!found) {
        r->good = FALSE;
        pa_xfree(fn);
        return;
    }

    if (r->good) {
        if (st.st_mtime == r->desktop_mtime) {
            /* Theoretically the filename could have changed, but if so
               having the same mtime is very unlikely so not worth tracking it in r */
            pa_xfree(fn);
            return;
        }
        pa_log_debug("Found %s (which has been updated since we last checked).", fn);
    } else
        pa_log_debug("Found %s.", fn);

    parse_file(r, fn, table, FALSE);
    r->desktop_mtime = st.st_mtime;
    r->good = TRUE;


    pa_xfree(fn);
}

static void apply_rule(struct rule *r, pa_proplist *p) {
    pa_assert(r);
    pa_assert(p);

    if (!r->good)
        return;

    if (r->proplist)
        pa_proplist_update(p, PA_UPDATE_MERGE, r->proplist);

    if (r->icon_name)
        if (!pa_proplist_contains(p, PA_PROP_APPLICATION_ICON_NAME))
            pa_proplist_sets(p, PA_PROP_APPLICATION_ICON_NAME, r->icon_name);

    if (r->application_name) {
        const char *t;

        t = pa_proplist_gets(p, PA_PROP_APPLICATION_NAME);

        if (!t || pa_streq(t, r->process_name))
            pa_proplist_sets(p, PA_PROP_APPLICATION_NAME, r->application_name);
    }

    if (r->role)
        if (!pa_proplist_contains(p, PA_PROP_MEDIA_ROLE))
            pa_proplist_sets(p, PA_PROP_MEDIA_ROLE, r->role);
}

static void make_room(pa_hashmap *cache) {
    pa_assert(cache);

    while (pa_hashmap_size(cache) >= MAX_CACHE_SIZE) {
        struct rule *r;

        pa_assert_se(r = pa_hashmap_steal_first(cache));
        rule_free(r);
    }
}

static pa_hook_result_t process(struct userdata *u, pa_proplist *p) {

    struct rule *r;
    time_t now;
    const char *pn;

    pa_assert(u);
    pa_assert(p);

    if (!(pn = pa_proplist_gets(p, PA_PROP_APPLICATION_PROCESS_BINARY)))
        return PA_HOOK_OK;

    if (*pn == '.' || strchr(pn, '/'))
        return PA_HOOK_OK;

    time(&now);

    pa_log_debug("Looking for configuration file for %s", pn);

    if ((r = pa_hashmap_get(u->cache, pn))) {
        if (now-r->timestamp > STAT_INTERVAL) {
            r->timestamp = now;
            update_rule(r);
        }
    } else {
        make_room(u->cache);

        r = pa_xnew0(struct rule, 1);
        r->process_name = pa_xstrdup(pn);
        r->timestamp = now;
        pa_hashmap_put(u->cache, r->process_name, r);
        update_rule(r);
    }

    apply_rule(r, p);
    return PA_HOOK_OK;
}

static pa_hook_result_t client_new_cb(pa_core *core, pa_client_new_data *data, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_assert(data);
    pa_assert(u);

    return process(u, data->proplist);
}

static pa_hook_result_t client_proplist_changed_cb(pa_core *core, pa_client *client, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_assert(client);
    pa_assert(u);

    return process(u, client->proplist);
}

static char **filter_by_client(pa_hashmap *h, pa_client *c) {

    char **possible = pa_xnew0(char *, pa_hashmap_size(h)+1);
    const char *process_binary;
    char **iter;
    void *state = NULL;
    const void *key = NULL;

    if (!c || !c->proplist) {
        pa_xfree(possible);
        return NULL;
    }

    process_binary = pa_proplist_gets(c->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);

    iter = possible;

    while (pa_hashmap_iterate(h, &state, &key)) {
        struct sink_input_rule_file *rf = pa_hashmap_get(h, key);

        pa_assert(rf);

        if (!rf->client_name ||
            (process_binary && strcmp(process_binary, rf->client_name) == 0)) {
            *iter = pa_xstrdup((const char *) key);
            iter++;
        }
    }

    return possible;
}

static pa_hook_result_t process_sink_input(
        struct userdata *u,
        pa_proplist *p,
        pa_client *client) {

    void *state = NULL;
    const void *fn = NULL;
    const char *prop = NULL;
    struct sink_input_rule_file *rf;
    char **possible = NULL, **iter;
    pa_hashmap *valid;

    possible = filter_by_client(u->sink_input_rules, client);

    if (!possible)
        return PA_HOOK_OK;

    /* size of possible must be at least one, or otherwise we wouldn't be here */

    if (possible[0] == NULL) {
        goto end;
    }

    valid = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    iter = possible;

    /* init the map */

    while (*iter) {
        pa_hashmap_put(valid, (const void *) *iter, (void *) RULE_UNDEFINED);
        iter++;
    }

    prop = pa_proplist_iterate(p, &state);

    while (prop) {

        const char *value = pa_proplist_gets(p, prop);

        iter = possible;

        while (*iter) {

            enum rule_match status = (enum rule_match) pa_hashmap_get(valid, *iter);

            if (status == RULE_MISS) {
                iter++;
                continue;
            }

            rf = pa_hashmap_get(u->sink_input_rules, *iter);

            if (rf) {
                void *section_state = NULL;
                const void *section = NULL;
                while (pa_hashmap_iterate(rf->rules, &section_state, &section)) {
                    struct sink_input_rule_section *s = pa_hashmap_get(rf->rules, section);

                    if (!s) {
                        pa_log_error("no section associated with %s", (const char *) section);
                        continue;
                    }
                    if (strcmp(prop, s->stream_key) == 0) {

                        if (value && !regexec(&s->stream_value, value, 0, NULL, 0)) {
                            /* hit */
                            pa_log_debug("hit %s", *iter);

                            if (status == RULE_UNDEFINED) {
                                pa_hashmap_remove(valid, *iter);
                                pa_hashmap_put(valid, (const void *) *iter, (void *) RULE_HIT);
                            }
                        }
                        else {
                            /* miss, no more processing for this rule file*/
                            pa_hashmap_remove(valid, *iter);
                            pa_hashmap_put(valid, (const void *) *iter, (void *) RULE_MISS);
                            pa_log_debug("miss %s", *iter);
                        }
                    }
                }
            }
            iter++;
        }

        prop = pa_proplist_iterate(p, &state);
    }

    /* go do the changes for the rule files that actually were matching */
    state = NULL;

    while (pa_hashmap_iterate(valid, &state, &fn)) {
        enum rule_match status = (enum rule_match) pa_hashmap_get(valid, fn);
        if (status == RULE_HIT) {
            pa_log_debug("rule hit: %s", (const char *) fn);
            rf = pa_hashmap_get(u->sink_input_rules, (const char *) fn);
            pa_proplist_sets(p, rf->target_key, rf->target_value);
        }
        else if (status == RULE_MISS) {
            pa_log_debug("rule miss: %s", (const char *) fn);
        }
        else if (status == RULE_UNDEFINED) {
            pa_log_debug("rule undefined: %s", (const char *) fn);
        }
        else {
            pa_log_error("memory corruption for %s (%i)", (const char *) fn, status);
        }
    }

    pa_hashmap_free(valid, NULL, NULL);

    iter = possible;
    while (*iter) {
        pa_xfree(*iter);
        iter++;
    }

end:
    pa_xfree(possible);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_new_cb(
        pa_core *core,
        pa_sink_input_new_data *new_data,
        struct userdata *u) {

    pa_core_assert_ref(core);
    pa_assert(new_data);
    pa_assert(u);

    if (!new_data->client)
        return PA_HOOK_OK;

    if (!u->sink_input_rules)
        return PA_HOOK_OK;

    return process_sink_input(u, new_data->proplist, new_data->client);
}

static int parse_rule_sections(
        const char *filename,
        unsigned line,
        const char *section,
        const char *lvalue,
        const char *rvalue,
        void *data,
        void *userdata) {

    struct sink_input_rule_file **rfp = data;
    struct sink_input_rule_file *rf = *rfp;

   struct sink_input_rule_section *s = pa_hashmap_get(rf->rules, section);

    if (!s) {
        s = pa_xnew0(struct sink_input_rule_section, 1);
        s->comp = FALSE;

        /* add key to the struct for later freeing */
        s->section_name = pa_xstrdup(section);
        pa_hashmap_put(rf->rules, s->section_name, s);
    }

    if (strcmp(lvalue, "prop_key") == 0) {
        if (s->stream_key)
            pa_xfree(s->stream_key);
        s->stream_key = pa_xstrdup(rvalue);
    }
    else if (strcmp(lvalue, "prop_value") == 0) {
        int ret;
        if (s->comp)
            regfree(&s->stream_value);

        ret = regcomp(&s->stream_value, rvalue, REG_EXTENDED|REG_NOSUB);
        s->comp = TRUE;

        if (ret != 0) {
            char errbuf[256];
            regerror(ret, &s->stream_value, errbuf, 256);
            pa_log_error("Failed compiling regular expression: %s", errbuf);
        }
    }

    return 0;
}

static pa_bool_t validate_sink_input_rule(struct sink_input_rule_file *rf) {

    void *state;
    struct sink_input_rule_section *s;

    if (!rf->target_key || !rf->target_value) {
        pa_log_error("No result condition listed for rule file");
        /* no result condition listed, so no point in using this rule file */
        return FALSE;
    }

    PA_HASHMAP_FOREACH(s, rf->rules, state) {
        if (!s->stream_key || s->comp == FALSE) {
            pa_log_error("Incomplete rule section [%s] in rule file", s->section_name);
            return FALSE;
        }
    }

    return TRUE;
}

static pa_hashmap *update_sink_input_rules() {

    struct dirent *file;
    DIR *sinkinputrulefiles_dir;
    pa_hashmap *rules = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    sinkinputrulefiles_dir = opendir(SINK_INPUT_RULE_DIR);

    if (!sinkinputrulefiles_dir)
        return NULL;

    while ((file = readdir(sinkinputrulefiles_dir))) {

        if (file->d_type == DT_REG) {

            struct sink_input_rule_file *rf = pa_xnew0(struct sink_input_rule_file, 1);

            /* parse the file */

            pa_config_item table[] = {
                { "target_key",   pa_config_parse_string, &rf->target_key,   "result"  },
                { "target_value", pa_config_parse_string, &rf->target_value, "result"  },
                { "client_name",  pa_config_parse_string, &rf->client_name,  "general" },
                { NULL,           parse_rule_sections,    &rf,               NULL      },
                { NULL, NULL, NULL, NULL },
            };

            rf->rules = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
            rf->fn = pa_sprintf_malloc(SINK_INPUT_RULE_DIR PA_PATH_SEP "%s", file->d_name);

            if (pa_config_parse(rf->fn, NULL, table, rf) >= 0) {

                pa_log_info("Successfully parsed sink input conf file %s", file->d_name);

                if (!validate_sink_input_rule(rf)) {
                    pa_log_error("validating rule file failed");
                    sink_input_rule_file_free(rf, NULL);
                    rf = NULL;
                }
                else {
                    pa_log_info("adding filename %s to %p", rf->fn, rf);
                    pa_hashmap_put(rules, rf->fn, rf);
                }
            }
        }
    }

    closedir(sinkinputrulefiles_dir);

    if (pa_hashmap_isempty(rules)) {
        pa_hashmap_free(rules, NULL, NULL);
        return NULL;
    }

    return rules;
}

static void send_event(pa_client *c, const char *evt, pa_proplist *d) {

    struct userdata *u = c->userdata;

    const char *action = pa_proplist_gets(d, "action");
    const char *dir = pa_proplist_gets(d, "directory");
    const char *file = pa_proplist_gets(d, "file");

    pa_log_debug("received event '%s': action: %s, dir: %s, file: %s", evt, action, dir, file);

    /* update the rules */
    pa_xfree(u->sink_input_rules);
    u->sink_input_rules = update_sink_input_rules();
}

static pa_client *create_directory_watch_client(pa_module *m, const char *directory, struct userdata *u) {
    pa_client_new_data data;
    pa_client *c;

    /* Create a virtual client for triggering the directory callbacks. The
       protocol is a property list. */

    pa_client_new_data_init(&data);
    data.module = m;
    data.driver = __FILE__;
    pa_proplist_sets(data.proplist, PA_PROP_APPLICATION_NAME, "Directory watch client");
    pa_proplist_sets(data.proplist, "type", "directory-watch");
    pa_proplist_sets(data.proplist, "dir-watch.directory", directory);

    c = pa_client_new(m->core, &data);

    pa_client_new_data_done(&data);

    if (!c) {
        pa_log_error("Failed to create directory watch client");
        return NULL;
    }

    c->send_event = send_event;
    c->userdata = u;

    return c;
}

int pa__init(pa_module *m) {
    pa_modargs *ma = NULL;
    struct userdata *u;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew(struct userdata, 1);

    u->sink_input_rules = update_sink_input_rules();

    u->cache = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    u->directory_watch_client = create_directory_watch_client(m, SINK_INPUT_RULE_DIR, u);
    if (!u->directory_watch_client)
        goto fail;

    u->client_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_CLIENT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) client_new_cb, u);
    u->client_proplist_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_CLIENT_PROPLIST_CHANGED], PA_HOOK_EARLY, (pa_hook_cb_t) client_proplist_changed_cb, u);
    u->sink_input_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_new_cb, u);


    pa_modargs_free(ma);

    return 0;

fail:
    pa__done(m);

    if (ma)
        pa_modargs_free(ma);

    return -1;
}

void pa__done(pa_module *m) {
    struct userdata* u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->client_new_slot)
        pa_hook_slot_free(u->client_new_slot);
    if (u->client_proplist_changed_slot)
        pa_hook_slot_free(u->client_proplist_changed_slot);
    if (u->sink_input_new_slot)
        pa_hook_slot_free(u->sink_input_new_slot);

    if (u->cache) {
        struct rule *r;

        while ((r = pa_hashmap_steal_first(u->cache)))
            rule_free(r);

        pa_hashmap_free(u->cache, NULL, NULL);
    }

    if (u->sink_input_rules)
        pa_hashmap_free(u->sink_input_rules, (pa_free2_cb_t) sink_input_rule_file_free, NULL);

    if (u->directory_watch_client)
        pa_client_free(u->directory_watch_client);

    pa_xfree(u);
}
