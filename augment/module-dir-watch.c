/***
  This file is part of PulseAudio.

  Copyright 2012 Ismo Puustinen

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

#include <stdint.h>

#ifdef __linux__
#define HAVE_INOTIFY
#endif

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <sys/types.h>

#include <pulse/xmalloc.h>
#include <pulse/proplist.h>

#include <pulsecore/client.h>
#include <pulsecore/macro.h>
#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/log.h>
#include <pulsecore/llist.h>

#include "module-dir-watch-symdef.h"

PA_MODULE_AUTHOR("Ismo Puustinen");
PA_MODULE_DESCRIPTION("Directory watch module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);
PA_MODULE_USAGE("");

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_hook_slot *client_watch_slot;
    pa_hook_slot *client_unlink_slot;
#ifdef HAVE_INOTIFY
    pa_hashmap *paths_to_clients;
    int inotify_fd;
    pa_io_event *inotify_io;
#endif
};

#ifdef HAVE_INOTIFY
struct client_id {
    PA_LLIST_FIELDS(struct client_id);
    uint32_t id;
};

struct client_data {
    PA_LLIST_HEAD(struct client_id, ids);
    int i;
    char *directory;
    int wd;
};
#endif

static void fire(
        struct userdata *u,
        pa_client *c,
        const char *action,
        const char *dir,
        const char *fn) {
    pa_proplist *list;

    pa_assert(u);
    pa_assert(fn);
    pa_assert(action);

    list = pa_proplist_new();

    pa_proplist_sets(list, "action", action);
    pa_proplist_sets(list, "directory", dir);
    pa_proplist_sets(list, "file", fn);

    pa_client_send_event(c, "dir_watch_event", list);

    pa_proplist_free(list);
}

static void fire_created(
        struct userdata *u,
        pa_client *c,
        char *dir,
        char *fn) {

    fire(u, c, "create", dir, fn);
}

static void fire_deleted(
        struct userdata *u,
        pa_client *c,
        char *dir,
        char *fn) {

    fire(u, c, "create", dir, fn);
}

static void fire_modified(
        struct userdata *u,
        pa_client *c,
        char *dir,
        char *fn) {

    fire(u, c, "modify", dir, fn);
}

static void fire_attributed(
        struct userdata *u,
        pa_client *c,
        char *dir,
        char *fn) {

    fire(u, c, "attribute", dir, fn);
}

#ifdef HAVE_INOTIFY
static void client_data_free(struct client_data *cd, struct userdata *u) {

    pa_assert(cd);

    pa_xfree(cd->directory);
    while(cd->ids) {
        struct client_id *id = cd->ids;
        PA_LLIST_REMOVE(struct client_id, cd->ids, id);
        pa_xfree(id);
    }

    pa_xfree(cd);
}

static void dir_watch_inotify_cb(
        pa_mainloop_api *a,
        pa_io_event *e,
        int fd,
        pa_io_event_flags_t events,
        void *userdata) {
    ssize_t r;
    struct inotify_event *event;
    int type = 0;
    uint8_t eventbuf[2 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    struct userdata *u = userdata;

    pa_log_debug("> inotify_cb");

    while (TRUE) {

        r = pa_read(fd, &eventbuf, sizeof(eventbuf), &type);

        if (r <= 0) {
            if (r < 0 && errno == EAGAIN)
                break;

            goto fail;
        }

        event = (struct inotify_event *) &eventbuf;

        while (r > 0) {
            size_t len;

            if ((size_t) r < sizeof(struct inotify_event)) {
                pa_log("read() too short.");
                goto fail;
            }

            len = sizeof(struct inotify_event) + event->len;

            if ((size_t) r < len) {
                goto fail;
            }

            if (event->len > 0) {
                struct client_data *cd = pa_hashmap_get(u->paths_to_clients, (const void *)NULL + event->wd);
                struct client_id *id;

                if (!cd) {
                    goto fail;
                }

                PA_LLIST_FOREACH(id, cd->ids) {

                    pa_client *c = pa_idxset_get_by_index(u->core->clients, id->id);

                    if (!c) {
                        pa_log_error("client not found!");
                        continue;
                    }

                    if (event->mask & IN_MODIFY) {
                        pa_log_debug("File %s modified", event->name);
                        fire_modified(u, c, cd->directory, event->name);
                    }
                    if (event->mask & IN_CREATE) {
                        pa_log_debug("File %s created", event->name);
                        fire_created(u, c, cd->directory, event->name);
                    }
                    if (event->mask & IN_ATTRIB) {
                        pa_log_debug("File %s attribute change", event->name);
                        fire_attributed(u, c, cd->directory, event->name);
                    }
                    if (event->mask & IN_DELETE) {
                        pa_log_debug("File %s deleted", event->name);
                        fire_deleted(u, c, cd->directory, event->name);
                    }
                }
            }

            event = (struct inotify_event*) ((uint8_t*) event + len);
            r -= len;
        }
    }

    return;

fail:
    if (u->inotify_io) {
        a->io_free(u->inotify_io);
        u->inotify_io = NULL;
    }

    if (u->inotify_fd >= 0) {
        pa_close(u->inotify_fd);
        u->inotify_fd = -1;
    }
}
#endif

static int add_directory(const char *directory, pa_client *c, struct userdata *u) {
#ifdef HAVE_INOTIFY
    struct client_data *cd;
    int wd;

    wd = inotify_add_watch(u->inotify_fd, directory, IN_CREATE|IN_DELETE|IN_MODIFY|IN_ATTRIB);
    if (wd < 0) {
        pa_log_error("Failed to add directory %s to watch list", directory);
        goto fail;
    }

    cd = pa_hashmap_get(u->paths_to_clients, (const void *)NULL + wd);

    if (cd) {
        struct client_id *id = pa_xnew0(struct client_id, 1);
        id->id = c->index;

        PA_LLIST_PREPEND(struct client_id, cd->ids, id);
    }
    else {
        struct client_id *id = pa_xnew0(struct client_id, 1);
        id->id = c->index;

        cd = pa_xnew0(struct client_data, 1);
        cd->directory = pa_xstrdup(directory);
        cd->wd = wd;
        PA_LLIST_HEAD_INIT(struct client_id, cd->ids);

        PA_LLIST_PREPEND(struct client_id, cd->ids, id);

        pa_hashmap_put(u->paths_to_clients, (const void *)0 + wd, (void *) cd);
    }
#endif
    return 0;

fail:
    return -1;
}

static pa_hook_result_t client_unlink_cb(
        pa_core *core,
        pa_client *c,
        struct userdata *u) {
#ifdef HAVE_INOTIFY
    void *state;
    struct client_data *cd;
    struct client_id *id;
    pa_bool_t found = FALSE;

    PA_HASHMAP_FOREACH(cd, u->paths_to_clients, state) {
        PA_LLIST_FOREACH(id, cd->ids) {
            if (id->id == c->index) {
                PA_LLIST_REMOVE(struct client_id, cd->ids, id);
                pa_xfree(id);
                found = TRUE;
                break;
            }
        }

        if (!found)
            continue;

        if (!cd->ids) {
            /* no-one is interested in the directory anymore */
            inotify_rm_watch(u->inotify_fd, cd->wd);
            client_data_free(cd, NULL);
        }
        break;
    }
#endif

    return PA_HOOK_OK;
}

static pa_hook_result_t client_watch_cb(
        pa_core *core,
        pa_client *c,
        struct userdata *u) {
    const char *type, *directory;

    pa_log("received directory watch event");

    type = pa_proplist_gets(c->proplist, "type");

    if (!type || strcmp(type, "directory-watch") != 0) {
        /* not for us */
        return PA_HOOK_OK;
    }

    directory = pa_proplist_gets(c->proplist, "dir-watch.directory");

    if (!directory) {
        goto fail;
    }

    if (add_directory(directory, c, u) < 0) {
        goto fail;
    }

fail:
    /* error, kill the client */
    pa_client_kill(c);
    return PA_HOOK_OK;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    pa_assert(m->userdata);

    u = m->userdata;

#ifdef HAVE_INOTIFY
    if (u->inotify_io)
        m->core->mainloop->io_free(u->inotify_io);

    if (u->inotify_fd >= 0)
        pa_close(u->inotify_fd);


    if (u->paths_to_clients)
        pa_hashmap_free(u->paths_to_clients, (pa_free_cb_t) client_data_free);
#endif

    pa_xfree(u);
}

int pa__init(pa_module *m) {
    struct userdata *u;

    pa_log_debug("Init directory watch module");

    pa_assert(m);

    u = pa_xnew0(struct userdata, 1);
    u->module = m;
    u->core = m->core;
    u->client_watch_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_CLIENT_PUT], PA_HOOK_EARLY, (pa_hook_cb_t) client_watch_cb, u);
    u->client_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_CLIENT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) client_unlink_cb, u);

    m->userdata = u;

#ifdef HAVE_INOTIFY
    u->paths_to_clients = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    u->inotify_fd = inotify_init1(IN_CLOEXEC|IN_NONBLOCK);
    if (u->inotify_fd < 0) {
        goto fail;
    }

    u->inotify_io = u->core->mainloop->io_new(u->core->mainloop, u->inotify_fd, PA_IO_EVENT_INPUT, dir_watch_inotify_cb, u);
    if (!u->inotify_io) {
        goto fail;
    }
#endif

    return 0;

 fail:
    pa__done(m);

    return -1;
}
