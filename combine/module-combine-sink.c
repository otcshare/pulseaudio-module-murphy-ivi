/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering

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

#include <stdio.h>
#include <errno.h>

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>
#include <pulsecore/module.h>
#include <pulsecore/llist.h>
#include <pulsecore/sink.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/memblockq.h>
#include <pulsecore/log.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/namereg.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/time-smoother.h>
#include <pulsecore/strlist.h>

#include "module-combine-sink-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("Combine multiple sinks to one");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(false);
PA_MODULE_USAGE(
        "sink_name=<name for the sink> "
        "sink_properties=<properties for the sink> "
        "slaves=<slave sinks> "
        "adjust_time=<how often to readjust rates in s> "
        "resample_method=<method> "
        "format=<sample format> "
        "rate=<sample rate> "
        "channels=<number of channels> "
        "channel_map=<channel map>");

#define DEFAULT_SINK_NAME "combined"

#define MEMBLOCKQ_MAXLENGTH (1024*1024*16)

#define DEFAULT_ADJUST_TIME_USEC (10*PA_USEC_PER_SEC)

#define BLOCK_USEC (PA_USEC_PER_MSEC * 200)

static const char* const valid_modargs[] = {
    "sink_name",
    "sink_properties",
    "slaves",
    "adjust_time",
    "resample_method",
    "format",
    "rate",
    "channels",
    "channel_map",
    NULL
};

#include "userdata.h"

enum {
    SINK_MESSAGE_ADD_OUTPUT = PA_SINK_MESSAGE_MAX,
    SINK_MESSAGE_REMOVE_OUTPUT,
    SINK_MESSAGE_NEED,
    SINK_MESSAGE_UPDATE_LATENCY,
    SINK_MESSAGE_UPDATE_MAX_REQUEST,
    SINK_MESSAGE_UPDATE_REQUESTED_LATENCY
};

enum {
    SINK_INPUT_MESSAGE_POST = PA_SINK_INPUT_MESSAGE_MAX,
};

static void output_disable(struct output *o);
static void output_enable(struct output *o);
static void output_free(struct output *o);
static int output_create_sink_input(struct output *o);
static pa_sink_input *add_slave(struct userdata *u, pa_sink *sink);
static void remove_slave(struct userdata *u, pa_sink_input *i, pa_sink *s);
static int move_slave(struct userdata *u, pa_sink_input *i, pa_sink *s);

static void adjust_rates(struct userdata *u) {
    struct output *o;
    pa_usec_t max_sink_latency = 0, min_total_latency = (pa_usec_t) -1, target_latency, avg_total_latency = 0;
    uint32_t base_rate;
    uint32_t idx;
    unsigned n = 0;

    pa_assert(u);
    pa_sink_assert_ref(u->sink);

    if (pa_idxset_size(u->outputs) <= 0)
        return;

    if (!PA_SINK_IS_OPENED(pa_sink_get_state(u->sink)))
        return;

    PA_IDXSET_FOREACH(o, u->outputs, idx) {
        pa_usec_t sink_latency;

        if (!o->sink_input || !PA_SINK_IS_OPENED(pa_sink_get_state(o->sink)))
            continue;

        o->total_latency = pa_sink_input_get_latency(o->sink_input, &sink_latency);
        o->total_latency += sink_latency;

        if (sink_latency > max_sink_latency)
            max_sink_latency = sink_latency;

        if (min_total_latency == (pa_usec_t) -1 || o->total_latency < min_total_latency)
            min_total_latency = o->total_latency;

        avg_total_latency += o->total_latency;
        n++;

        /* pa_log_debug("[%s] total=%0.2fms sink=%0.2fms ", o->sink->name, (double) o->total_latency / PA_USEC_PER_MSEC, (double) sink_latency / PA_USEC_PER_MSEC); */

        if (o->total_latency > 10*PA_USEC_PER_SEC)
            pa_log_warn("[%s] Total latency of output is very high (%0.2fms), most likely the audio timing in one of your drivers is broken.", o->sink->name, (double) o->total_latency / PA_USEC_PER_MSEC);
    }

    if (min_total_latency == (pa_usec_t) -1)
        return;

    avg_total_latency /= n;

    target_latency = max_sink_latency > min_total_latency ? max_sink_latency : min_total_latency;

    /*
    pa_log_info("[%s] avg total latency is %0.2f msec.", u->sink->name, (double) avg_total_latency / PA_USEC_PER_MSEC);
    pa_log_info("[%s] target latency is %0.2f msec.", u->sink->name, (double) target_latency / PA_USEC_PER_MSEC);
    */

    base_rate = u->sink->sample_spec.rate;

    PA_IDXSET_FOREACH(o, u->outputs, idx) {
        uint32_t new_rate = base_rate;
        uint32_t current_rate;

        if (!o->sink_input || !PA_SINK_IS_OPENED(pa_sink_get_state(o->sink)))
            continue;

	current_rate = o->sink_input->sample_spec.rate;

        if (o->total_latency != target_latency)
            new_rate += (uint32_t) (((double) o->total_latency - (double) target_latency) / (double) u->adjust_time * (double) new_rate);

        if (new_rate < (uint32_t) (base_rate*0.8) || new_rate > (uint32_t) (base_rate*1.25)) {
            pa_log_warn("[%s] sample rates too different, not adjusting (%u vs. %u).", o->sink_input->sink->name, base_rate, new_rate);
            new_rate = base_rate;
        } else {
            if (base_rate < new_rate + 20 && new_rate < base_rate + 20)
              new_rate = base_rate;
            /* Do the adjustment in small steps; 2‰ can be considered inaudible */
            if (new_rate < (uint32_t) (current_rate*0.998) || new_rate > (uint32_t) (current_rate*1.002)) {
                /* pa_log_info("[%s] new rate of %u Hz not within 2‰ of %u Hz, forcing smaller adjustment", o->sink_input->sink->name, new_rate, current_rate); */
                new_rate = PA_CLAMP(new_rate, (uint32_t) (current_rate*0.998), (uint32_t) (current_rate*1.002));
            }
            pa_log_info("[%s] new rate is %u Hz; ratio is %0.3f; latency is %0.2f msec.", o->sink_input->sink->name, new_rate, (double) new_rate / base_rate, (double) o->total_latency / PA_USEC_PER_MSEC);
        }
        pa_sink_input_set_rate(o->sink_input, new_rate);
    }

    pa_asyncmsgq_send(u->sink->asyncmsgq, PA_MSGOBJECT(u->sink), SINK_MESSAGE_UPDATE_LATENCY, NULL, (int64_t) avg_total_latency, NULL);
}

static void time_callback(pa_mainloop_api *a, pa_time_event *e, const struct timeval *t, void *userdata) {
    struct userdata *u = userdata;

    pa_assert(u);
    pa_assert(a);
    pa_assert(u->time_event == e);

    adjust_rates(u);

    pa_core_rttime_restart(u->core, e, pa_rtclock_now() + u->adjust_time);
}

static void process_render_null(struct userdata *u, pa_usec_t now) {
    size_t ate = 0;
    pa_assert(u);

    /* If we are not running, we cannot produce any data */
    if (!pa_atomic_load(&u->thread_info.running))
        return;

    if (u->thread_info.in_null_mode)
        u->thread_info.timestamp = now;

    while (u->thread_info.timestamp < now + u->block_usec) {
        pa_memchunk chunk;

        pa_sink_render(u->sink, u->sink->thread_info.max_request, &chunk);
        pa_memblock_unref(chunk.memblock);

        u->thread_info.counter += chunk.length;

/*         pa_log_debug("Ate %lu bytes.", (unsigned long) chunk.length); */
        u->thread_info.timestamp += pa_bytes_to_usec(chunk.length, &u->sink->sample_spec);

        ate += chunk.length;

        if (ate >= u->sink->thread_info.max_request)
            break;
    }

/*     pa_log_debug("Ate in sum %lu bytes (of %lu)", (unsigned long) ate, (unsigned long) nbytes); */

    pa_smoother_put(u->thread_info.smoother, now,
                    pa_bytes_to_usec(u->thread_info.counter, &u->sink->sample_spec) - (u->thread_info.timestamp - now));
}

static void thread_func(void *userdata) {
    struct userdata *u = userdata;

    pa_assert(u);

    pa_log_debug("Thread starting up");

    if (u->core->realtime_scheduling)
        pa_make_realtime(u->core->realtime_priority+1);

    pa_thread_mq_install(&u->thread_mq);

    u->thread_info.timestamp = pa_rtclock_now();
    u->thread_info.in_null_mode = false;

    for (;;) {
        int ret;

        if (PA_SINK_IS_OPENED(u->sink->thread_info.state))
            if (u->sink->thread_info.rewind_requested)
                pa_sink_process_rewind(u->sink, 0);

        /* If no outputs are connected, render some data and drop it immediately. */
        if (PA_SINK_IS_OPENED(u->sink->thread_info.state) && !u->thread_info.active_outputs) {
            pa_usec_t now;

            now = pa_rtclock_now();

            if (!u->thread_info.in_null_mode || u->thread_info.timestamp <= now)
                process_render_null(u, now);

            pa_rtpoll_set_timer_absolute(u->rtpoll, u->thread_info.timestamp);
            u->thread_info.in_null_mode = true;
        } else {
            pa_rtpoll_set_timer_disabled(u->rtpoll);
            u->thread_info.in_null_mode = false;
        }

        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(u->rtpoll)) < 0) {
            pa_log_info("pa_rtpoll_run() = %i", ret);
            goto fail;
        }

        if (ret == 0)
            goto finish;
    }

fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_post(u->thread_mq.outq, PA_MSGOBJECT(u->core), PA_CORE_MESSAGE_UNLOAD_MODULE, u->module, 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("Thread shutting down");
}

/* Called from I/O thread context */
static void render_memblock(struct userdata *u, struct output *o, size_t length) {
    pa_assert(u);
    pa_assert(o);

    /* We are run by the sink thread, on behalf of an output (o). The
     * output is waiting for us, hence it is safe to access its
     * mainblockq and asyncmsgq directly. */

    /* If we are not running, we cannot produce any data */
    if (!pa_atomic_load(&u->thread_info.running))
        return;

    /* Maybe there's some data in the requesting output's queue
     * now? */
    while (pa_asyncmsgq_process_one(o->inq) > 0)
        ;

    /* Ok, now let's prepare some data if we really have to */
    while (!pa_memblockq_is_readable(o->memblockq)) {
        struct output *j;
        pa_memchunk chunk;

        /* Render data! */
        pa_sink_render(u->sink, length, &chunk);

        u->thread_info.counter += chunk.length;

        /* OK, let's send this data to the other threads */
        PA_LLIST_FOREACH(j, u->thread_info.active_outputs) {
            if (j == o)
                continue;

            pa_asyncmsgq_post(j->inq, PA_MSGOBJECT(j->sink_input), SINK_INPUT_MESSAGE_POST, NULL, 0, &chunk, NULL);
        }

        /* And place it directly into the requesting output's queue */
        pa_memblockq_push_align(o->memblockq, &chunk);
        pa_memblock_unref(chunk.memblock);
    }
}

/* Called from I/O thread context */
static void request_memblock(struct output *o, size_t length) {
    pa_assert(o);
    pa_sink_input_assert_ref(o->sink_input);
    pa_sink_assert_ref(o->userdata->sink);

    /* If another thread already prepared some data we received
     * the data over the asyncmsgq, hence let's first process
     * it. */
    while (pa_asyncmsgq_process_one(o->inq) > 0)
        ;

    /* Check whether we're now readable */
    if (pa_memblockq_is_readable(o->memblockq))
        return;

    /* OK, we need to prepare new data, but only if the sink is actually running */
    if (pa_atomic_load(&o->userdata->thread_info.running))
        pa_asyncmsgq_send(o->outq, PA_MSGOBJECT(o->userdata->sink), SINK_MESSAGE_NEED, o, (int64_t) length, NULL);
}

/* Called from I/O thread context */
static int sink_input_pop_cb(pa_sink_input *i, size_t nbytes, pa_memchunk *chunk) {
    struct output *o;

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    /* If necessary, get some new data */
    request_memblock(o, nbytes);

    /* pa_log("%s q size is %u + %u (%u/%u)", */
    /*        i->sink->name, */
    /*        pa_memblockq_get_nblocks(o->memblockq), */
    /*        pa_memblockq_get_nblocks(i->thread_info.render_memblockq), */
    /*        pa_memblockq_get_maxrewind(o->memblockq), */
    /*        pa_memblockq_get_maxrewind(i->thread_info.render_memblockq)); */

    if (pa_memblockq_peek(o->memblockq, chunk) < 0)
        return -1;

    pa_memblockq_drop(o->memblockq, chunk->length);

    return 0;
}

/* Called from I/O thread context */
static void sink_input_process_rewind_cb(pa_sink_input *i, size_t nbytes) {
    struct output *o;

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    pa_memblockq_rewind(o->memblockq, nbytes);
}

/* Called from I/O thread context */
static void sink_input_update_max_rewind_cb(pa_sink_input *i, size_t nbytes) {
    struct output *o;

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    pa_memblockq_set_maxrewind(o->memblockq, nbytes);
}

/* Called from I/O thread context */
static void sink_input_update_max_request_cb(pa_sink_input *i, size_t nbytes) {
    struct output *o;

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    if (pa_atomic_load(&o->max_request) == (int) nbytes)
        return;

    pa_atomic_store(&o->max_request, (int) nbytes);
    pa_asyncmsgq_post(o->outq, PA_MSGOBJECT(o->userdata->sink), SINK_MESSAGE_UPDATE_MAX_REQUEST, NULL, 0, NULL, NULL);
}

/* Called from thread context */
static void sink_input_update_sink_requested_latency_cb(pa_sink_input *i) {
    struct output *o;
    pa_usec_t c;

    pa_assert(i);

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    c = pa_sink_get_requested_latency_within_thread(i->sink);

    if (c == (pa_usec_t) -1)
        c = i->sink->thread_info.max_latency;

    if (pa_atomic_load(&o->requested_latency) == (int) c)
        return;

    pa_atomic_store(&o->requested_latency, (int) c);
    pa_asyncmsgq_post(o->outq, PA_MSGOBJECT(o->userdata->sink), SINK_MESSAGE_UPDATE_REQUESTED_LATENCY, NULL, 0, NULL, NULL);
}

/* Called from I/O thread context */
static void sink_input_attach_cb(pa_sink_input *i) {
    struct output *o;
    pa_usec_t c;

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    /* Set up the queue from the sink thread to us */
    pa_assert(!o->inq_rtpoll_item_read && !o->outq_rtpoll_item_write);

    o->inq_rtpoll_item_read = pa_rtpoll_item_new_asyncmsgq_read(
            i->sink->thread_info.rtpoll,
            PA_RTPOLL_LATE,  /* This one is not that important, since we check for data in _peek() anyway. */
            o->inq);

    o->outq_rtpoll_item_write = pa_rtpoll_item_new_asyncmsgq_write(
            i->sink->thread_info.rtpoll,
            PA_RTPOLL_EARLY,
            o->outq);

    pa_sink_input_request_rewind(i, 0, false, true, true);

    pa_atomic_store(&o->max_request, (int) pa_sink_input_get_max_request(i));

    c = pa_sink_get_requested_latency_within_thread(i->sink);
    pa_atomic_store(&o->requested_latency, (int) (c == (pa_usec_t) -1 ? 0 : c));

    pa_asyncmsgq_post(o->outq, PA_MSGOBJECT(o->userdata->sink), SINK_MESSAGE_UPDATE_MAX_REQUEST, NULL, 0, NULL, NULL);
    pa_asyncmsgq_post(o->outq, PA_MSGOBJECT(o->userdata->sink), SINK_MESSAGE_UPDATE_REQUESTED_LATENCY, NULL, 0, NULL, NULL);
}

/* Called from I/O thread context */
static void sink_input_detach_cb(pa_sink_input *i) {
    struct output *o;

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    if (o->inq_rtpoll_item_read) {
        pa_rtpoll_item_free(o->inq_rtpoll_item_read);
        o->inq_rtpoll_item_read = NULL;
    }

    if (o->outq_rtpoll_item_write) {
        pa_rtpoll_item_free(o->outq_rtpoll_item_write);
        o->outq_rtpoll_item_write = NULL;
    }
}

/* Called from main context */
static void sink_input_kill_cb(pa_sink_input *i) {
    struct output *o;

    pa_sink_input_assert_ref(i);
    pa_assert_se(o = i->userdata);

    pa_module_unload_request(o->userdata->module, true);
    output_free(o);
}

/* Called from thread context */
static int sink_input_process_msg(pa_msgobject *obj, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct output *o = PA_SINK_INPUT(obj)->userdata;

    switch (code) {

        case PA_SINK_INPUT_MESSAGE_GET_LATENCY: {
            pa_usec_t *r = data;

            *r = pa_bytes_to_usec(pa_memblockq_get_length(o->memblockq), &o->sink_input->sample_spec);

            /* Fall through, the default handler will add in the extra
             * latency added by the resampler */
            break;
        }

        case SINK_INPUT_MESSAGE_POST:

            if (PA_SINK_IS_OPENED(o->sink_input->sink->thread_info.state))
                pa_memblockq_push_align(o->memblockq, chunk);
            else
                pa_memblockq_flush_write(o->memblockq, true);

            return 0;
    }

    return pa_sink_input_process_msg(obj, code, data, offset, chunk);
}

/* Called from main context */
static void suspend(struct userdata *u) {
    struct output *o;
    uint32_t idx;

    pa_assert(u);

    /* Let's suspend by unlinking all streams */
    PA_IDXSET_FOREACH(o, u->outputs, idx)
        output_disable(o);

    pa_log_info("Device suspended...");
}

/* Called from main context */
static void unsuspend(struct userdata *u) {
    struct output *o;
    uint32_t idx;

    pa_assert(u);

    /* Let's resume */
    PA_IDXSET_FOREACH(o, u->outputs, idx)
        output_enable(o);

    pa_log_info("Resumed successfully...");
}

/* Called from main context */
static int sink_set_state(pa_sink *sink, pa_sink_state_t state) {
    struct userdata *u;

    pa_sink_assert_ref(sink);
    pa_assert_se(u = sink->userdata);

    /* Please note that in contrast to the ALSA modules we call
     * suspend/unsuspend from main context here! */

    switch (state) {
        case PA_SINK_SUSPENDED:
            pa_assert(PA_SINK_IS_OPENED(pa_sink_get_state(u->sink)));

            suspend(u);
            break;

        case PA_SINK_IDLE:
        case PA_SINK_RUNNING:

            if (pa_sink_get_state(u->sink) == PA_SINK_SUSPENDED)
                unsuspend(u);

            break;

        case PA_SINK_UNLINKED:
        case PA_SINK_INIT:
        case PA_SINK_INVALID_STATE:
            ;
    }

    return 0;
}

/* Called from IO context */
static void update_max_request(struct userdata *u) {
    size_t max_request = 0;
    struct output *o;

    pa_assert(u);
    pa_sink_assert_io_context(u->sink);

    /* Collects the max_request values of all streams and sets the
     * largest one locally */

    PA_LLIST_FOREACH(o, u->thread_info.active_outputs) {
        size_t mr = (size_t) pa_atomic_load(&o->max_request);

        if (mr > max_request)
            max_request = mr;
    }

    if (max_request <= 0)
        max_request = pa_usec_to_bytes(u->block_usec, &u->sink->sample_spec);

    pa_sink_set_max_request_within_thread(u->sink, max_request);
}

/* Called from IO context */
static void update_fixed_latency(struct userdata *u) {
    pa_usec_t fixed_latency = 0;
    struct output *o;

    pa_assert(u);
    pa_sink_assert_io_context(u->sink);

    /* Collects the requested_latency values of all streams and sets
     * the largest one as fixed_latency locally */

    PA_LLIST_FOREACH(o, u->thread_info.active_outputs) {
        pa_usec_t rl = (size_t) pa_atomic_load(&o->requested_latency);

        if (rl > fixed_latency)
            fixed_latency = rl;
    }

    if (fixed_latency <= 0)
        fixed_latency = u->block_usec;

    pa_sink_set_fixed_latency_within_thread(u->sink, fixed_latency);
}

/* Called from thread context of the io thread */
static void output_add_within_thread(struct output *o) {
    pa_assert(o);
    pa_sink_assert_io_context(o->sink);

    PA_LLIST_PREPEND(struct output, o->userdata->thread_info.active_outputs, o);

    pa_assert(!o->outq_rtpoll_item_read && !o->inq_rtpoll_item_write);

    o->outq_rtpoll_item_read = pa_rtpoll_item_new_asyncmsgq_read(
            o->userdata->rtpoll,
            PA_RTPOLL_EARLY-1,  /* This item is very important */
            o->outq);
    o->inq_rtpoll_item_write = pa_rtpoll_item_new_asyncmsgq_write(
            o->userdata->rtpoll,
            PA_RTPOLL_EARLY,
            o->inq);
}

/* Called from thread context of the io thread */
static void output_remove_within_thread(struct output *o) {
    pa_assert(o);
    pa_sink_assert_io_context(o->sink);

    PA_LLIST_REMOVE(struct output, o->userdata->thread_info.active_outputs, o);

    if (o->outq_rtpoll_item_read) {
        pa_rtpoll_item_free(o->outq_rtpoll_item_read);
        o->outq_rtpoll_item_read = NULL;
    }

    if (o->inq_rtpoll_item_write) {
        pa_rtpoll_item_free(o->inq_rtpoll_item_write);
        o->inq_rtpoll_item_write = NULL;
    }
}

/* Called from thread context of the io thread */
static int sink_process_msg(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SINK(o)->userdata;

    switch (code) {

        case PA_SINK_MESSAGE_SET_STATE: {
            bool running = (PA_PTR_TO_UINT(data) == PA_SINK_RUNNING);

            pa_atomic_store(&u->thread_info.running, running);

            if (running)
                pa_smoother_resume(u->thread_info.smoother, pa_rtclock_now(), true);
            else
                pa_smoother_pause(u->thread_info.smoother, pa_rtclock_now());

            break;
        }

        case PA_SINK_MESSAGE_GET_LATENCY: {
            pa_usec_t x, y, c, *delay = data;

            x = pa_rtclock_now();
            y = pa_smoother_get(u->thread_info.smoother, x);

            c = pa_bytes_to_usec(u->thread_info.counter, &u->sink->sample_spec);

            if (y < c)
                *delay = c - y;
            else
                *delay = 0;

            return 0;
        }

        case SINK_MESSAGE_ADD_OUTPUT:
            output_add_within_thread(data);
            update_max_request(u);
            update_fixed_latency(u);
            return 0;

        case SINK_MESSAGE_REMOVE_OUTPUT:
            output_remove_within_thread(data);
            update_max_request(u);
            update_fixed_latency(u);
            return 0;

        case SINK_MESSAGE_NEED:
            render_memblock(u, (struct output*) data, (size_t) offset);
            return 0;

        case SINK_MESSAGE_UPDATE_LATENCY: {
            pa_usec_t x, y, latency = (pa_usec_t) offset;

            x = pa_rtclock_now();
            y = pa_bytes_to_usec(u->thread_info.counter, &u->sink->sample_spec);

            if (y > latency)
                y -= latency;
            else
                y = 0;

            pa_smoother_put(u->thread_info.smoother, x, y);
            return 0;
        }

        case SINK_MESSAGE_UPDATE_MAX_REQUEST:
            update_max_request(u);
            break;

        case SINK_MESSAGE_UPDATE_REQUESTED_LATENCY:
            update_fixed_latency(u);
            break;
}

    return pa_sink_process_msg(o, code, data, offset, chunk);
}

static void update_description(struct userdata *u) {
    bool first = true;
    char *t;
    struct output *o;
    uint32_t idx;

    pa_assert(u);

    if (!u->auto_desc)
        return;

    if (pa_idxset_isempty(u->outputs)) {
        pa_sink_set_description(u->sink, "Simultaneous output");
        return;
    }

    t = pa_xstrdup("Simultaneous output to");

    PA_IDXSET_FOREACH(o, u->outputs, idx) {
        char *e;

        if (first) {
            e = pa_sprintf_malloc("%s %s", t, pa_strnull(pa_proplist_gets(o->sink->proplist, PA_PROP_DEVICE_DESCRIPTION)));
            first = false;
        } else
            e = pa_sprintf_malloc("%s, %s", t, pa_strnull(pa_proplist_gets(o->sink->proplist, PA_PROP_DEVICE_DESCRIPTION)));

        pa_xfree(t);
        t = e;
    }

    pa_sink_set_description(u->sink, t);
    pa_xfree(t);
}

static int output_create_sink_input(struct output *o) {
    pa_sink_input_new_data data;

    pa_assert(o);

    if (o->sink_input)
        return 0;

    pa_sink_input_new_data_init(&data);
    pa_sink_input_new_data_set_sink(&data, o->sink, false);
    data.driver = __FILE__;
    pa_proplist_setf(data.proplist, PA_PROP_MEDIA_NAME, "Simultaneous output on %s", pa_strnull(pa_proplist_gets(o->sink->proplist, PA_PROP_DEVICE_DESCRIPTION)));
    pa_proplist_sets(data.proplist, PA_PROP_MEDIA_ROLE, "filter");
    pa_sink_input_new_data_set_sample_spec(&data, &o->userdata->sink->sample_spec);
    pa_sink_input_new_data_set_channel_map(&data, &o->userdata->sink->channel_map);
    data.module = o->userdata->module;
    data.resample_method = o->userdata->resample_method;
    data.flags = PA_SINK_INPUT_VARIABLE_RATE|PA_SINK_INPUT_DONT_MOVE|PA_SINK_INPUT_NO_CREATE_ON_SUSPEND;

    pa_sink_input_new(&o->sink_input, o->userdata->core, &data);

    pa_sink_input_new_data_done(&data);

    if (!o->sink_input)
        return -1;

    o->sink_input->parent.process_msg = sink_input_process_msg;
    o->sink_input->pop = sink_input_pop_cb;
    o->sink_input->process_rewind = sink_input_process_rewind_cb;
    o->sink_input->update_max_rewind = sink_input_update_max_rewind_cb;
    o->sink_input->update_max_request = sink_input_update_max_request_cb;
    o->sink_input->update_sink_requested_latency = sink_input_update_sink_requested_latency_cb;
    o->sink_input->attach = sink_input_attach_cb;
    o->sink_input->detach = sink_input_detach_cb;
    o->sink_input->kill = sink_input_kill_cb;
    o->sink_input->userdata = o;

    pa_sink_input_set_requested_latency(o->sink_input, BLOCK_USEC);

    return 0;
}

/* Called from main context */
static struct output *output_new(struct userdata *u, pa_sink *sink) {
    struct output *o;

    pa_assert(u);
    pa_assert(sink);
    pa_assert(u->sink);

    o = pa_xnew0(struct output, 1);
    o->userdata = u;
    o->inq = pa_asyncmsgq_new(0);
    o->outq = pa_asyncmsgq_new(0);
    o->sink = sink;
    o->memblockq = pa_memblockq_new(
            "module-combine-sink output memblockq",
            0,
            MEMBLOCKQ_MAXLENGTH,
            MEMBLOCKQ_MAXLENGTH,
            &u->sink->sample_spec,
            1,
            0,
            0,
            &u->sink->silence);

    pa_assert_se(pa_idxset_put(u->outputs, o, NULL) == 0);
    update_description(u);

    return o;
}

/* Called from main context */
static void output_free(struct output *o) {
    pa_assert(o);

    output_disable(o);

    pa_assert_se(pa_idxset_remove_by_data(o->userdata->outputs, o, NULL));
    update_description(o->userdata);

    if (o->inq_rtpoll_item_read)
        pa_rtpoll_item_free(o->inq_rtpoll_item_read);
    if (o->inq_rtpoll_item_write)
        pa_rtpoll_item_free(o->inq_rtpoll_item_write);

    if (o->outq_rtpoll_item_read)
        pa_rtpoll_item_free(o->outq_rtpoll_item_read);
    if (o->outq_rtpoll_item_write)
        pa_rtpoll_item_free(o->outq_rtpoll_item_write);

    if (o->inq)
        pa_asyncmsgq_unref(o->inq);

    if (o->outq)
        pa_asyncmsgq_unref(o->outq);

    if (o->memblockq)
        pa_memblockq_free(o->memblockq);

    pa_xfree(o);
}

/* Called from main context */
static void output_enable(struct output *o) {
    pa_assert(o);

    if (o->sink_input)
        return;

    /* This might cause the sink to be resumed. The state change hook
     * of the sink might hence be called from here, which might then
     * cause us to be called in a loop. Make sure that state changes
     * for this output don't cause this loop by setting a flag here */
    o->ignore_state_change = true;

    if (output_create_sink_input(o) >= 0) {

        if (pa_sink_get_state(o->sink) != PA_SINK_INIT) {

            /* First we register the output. That means that the sink
             * will start to pass data to this output. */
            pa_asyncmsgq_send(o->userdata->sink->asyncmsgq, PA_MSGOBJECT(o->userdata->sink), SINK_MESSAGE_ADD_OUTPUT, o, 0, NULL);

            /* Then we enable the sink input. That means that the sink
             * is now asked for new data. */
            pa_sink_input_put(o->sink_input);

        } else
            /* Hmm the sink is not yet started, do things right here */
            output_add_within_thread(o);
    }

    o->ignore_state_change = false;
}

/* Called from main context */
static void output_disable(struct output *o) {
    pa_assert(o);

    if (!o->sink_input)
        return;

    /* First we disable the sink input. That means that the sink is
     * not asked for new data anymore  */
    pa_sink_input_unlink(o->sink_input);

    /* Then we unregister the output. That means that the sink doesn't
     * pass any further data to this output */
    pa_asyncmsgq_send(o->userdata->sink->asyncmsgq, PA_MSGOBJECT(o->userdata->sink), SINK_MESSAGE_REMOVE_OUTPUT, o, 0, NULL);

    /* Now deallocate the stream */
    pa_sink_input_unref(o->sink_input);
    o->sink_input = NULL;

    /* Finally, drop all queued data */
    pa_memblockq_flush_write(o->memblockq, true);
    pa_asyncmsgq_flush(o->inq, false);
    pa_asyncmsgq_flush(o->outq, false);
}

/* Called from main context */
static void output_verify(struct output *o) {
    pa_assert(o);

    if (PA_SINK_IS_OPENED(pa_sink_get_state(o->userdata->sink)))
        output_enable(o);
    else
        output_disable(o);
}

/* Called from main context */
static bool is_suitable_sink(struct userdata *u, pa_sink *s) {
    const char *t;

    pa_sink_assert_ref(s);

    if (s == u->sink)
        return false;

    if (!(s->flags & PA_SINK_HARDWARE))
        return false;

    if (!(s->flags & PA_SINK_LATENCY))
        return false;

    if ((t = pa_proplist_gets(s->proplist, PA_PROP_DEVICE_CLASS)))
        if (!pa_streq(t, "sound"))
            return false;

    return true;
}

/* Called from main context */
static pa_hook_result_t sink_put_hook_cb(pa_core *c, pa_sink *s, struct userdata* u) {
    struct output *o;

    pa_core_assert_ref(c);
    pa_sink_assert_ref(s);
    pa_assert(u);

    if (u->automatic) {
        if (!is_suitable_sink(u, s))
            return PA_HOOK_OK;
    } else {
        /* Check if the sink is a previously unlinked slave (non-automatic mode) */
        pa_strlist *l = u->unlinked_slaves;

        while (l && !pa_streq(pa_strlist_data(l), s->name))
            l = pa_strlist_next(l);

        if (!l) {
	    pa_log_debug("This sink is not previously unlinked, so returning from sink put cb");
            return PA_HOOK_OK;
	}

        u->unlinked_slaves = pa_strlist_remove(u->unlinked_slaves, s->name);
    }

    pa_log_info("Configuring new sink: %s", s->name);
    if (!(o = output_new(u, s))) {
        pa_log("Failed to create sink input on sink '%s'.", s->name);
        return PA_HOOK_OK;
    }

    output_verify(o);

    return PA_HOOK_OK;
}

/* Called from main context */
static struct output* find_output(struct userdata *u, pa_sink *s) {
    struct output *o;
    uint32_t idx;

    pa_assert(u);
    pa_assert(s);

    if (u->sink == s)
        return NULL;

    PA_IDXSET_FOREACH(o, u->outputs, idx)
        if (o->sink == s)
            return o;

    return NULL;
}

/* Called from main context */
static pa_hook_result_t sink_unlink_hook_cb(pa_core *c, pa_sink *s, struct userdata* u) {
    struct output *o;

    pa_assert(c);
    pa_sink_assert_ref(s);
    pa_assert(u);

    if (!(o = find_output(u, s)))
        return PA_HOOK_OK;

    pa_log_info("Unconfiguring sink: %s", s->name);

    if (!u->automatic && !u->no_reattach) {
        u->unlinked_slaves = pa_strlist_prepend(u->unlinked_slaves, s->name);
	pa_log_debug("Adding sink %s to the combine modules unlinked slaves list", s->name);
    }

    output_free(o);

    return PA_HOOK_OK;
}

/* Called from main context */
static pa_hook_result_t sink_state_changed_hook_cb(pa_core *c, pa_sink *s, struct userdata* u) {
    struct output *o;

    if (!(o = find_output(u, s)))
        return PA_HOOK_OK;

    /* This state change might be triggered because we are creating a
     * stream here, in that case we don't want to create it a second
     * time here and enter a loop */
    if (o->ignore_state_change)
        return PA_HOOK_OK;

    output_verify(o);

    return PA_HOOK_OK;
}

int pa__init(pa_module*m) {
    struct userdata *u;
    pa_modargs *ma = NULL;
    const char *slaves, *rm;
    int resample_method = PA_RESAMPLER_TRIVIAL;
    pa_sample_spec ss;
    pa_channel_map map;
    struct output *o;
    uint32_t idx;
    pa_sink_new_data data;
    uint32_t adjust_time_sec;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("failed to parse module arguments");
        goto fail;
    }

    if ((rm = pa_modargs_get_value(ma, "resample_method", NULL))) {
        if ((resample_method = pa_parse_resample_method(rm)) < 0) {
            pa_log("invalid resample method '%s'", rm);
            goto fail;
        }
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    u->rtpoll = pa_rtpoll_new();
    pa_thread_mq_init(&u->thread_mq, m->core->mainloop, u->rtpoll);
    u->resample_method = resample_method;
    u->outputs = pa_idxset_new(NULL, NULL);
    u->thread_info.smoother = pa_smoother_new(
            PA_USEC_PER_SEC,
            PA_USEC_PER_SEC*2,
            true,
            true,
            10,
            pa_rtclock_now(),
            true);

    u->add_slave = add_slave;
    u->remove_slave = remove_slave;
    u->move_slave = move_slave;

    adjust_time_sec = DEFAULT_ADJUST_TIME_USEC / PA_USEC_PER_SEC;
    if (pa_modargs_get_value_u32(ma, "adjust_time", &adjust_time_sec) < 0) {
        pa_log("Failed to parse adjust_time value");
        goto fail;
    }

    if (adjust_time_sec != DEFAULT_ADJUST_TIME_USEC / PA_USEC_PER_SEC)
        u->adjust_time = adjust_time_sec * PA_USEC_PER_SEC;
    else
        u->adjust_time = DEFAULT_ADJUST_TIME_USEC;

    slaves = pa_modargs_get_value(ma, "slaves", NULL);
    u->automatic = !slaves;

    ss = m->core->default_sample_spec;
    map = m->core->default_channel_map;

    /* Check the specified slave sinks for sample_spec and channel_map to use for the combined sink */
    if (!u->automatic) {
        const char*split_state = NULL;
        char *n = NULL;
        pa_sample_spec slaves_spec;
        pa_channel_map slaves_map;
        bool is_first_slave = true;

        pa_sample_spec_init(&slaves_spec);

        while ((n = pa_split(slaves, ",", &split_state))) {
            pa_sink *slave_sink;

            if (!(slave_sink = pa_namereg_get(m->core, n, PA_NAMEREG_SINK))) {
                pa_log("Invalid slave sink '%s'", n);
                pa_xfree(n);
                goto fail;
            }

            pa_xfree(n);

            if (is_first_slave) {
                slaves_spec = slave_sink->sample_spec;
                slaves_map = slave_sink->channel_map;
                is_first_slave = false;
            } else {
                if (slaves_spec.format != slave_sink->sample_spec.format)
                    slaves_spec.format = PA_SAMPLE_INVALID;

                if (slaves_spec.rate < slave_sink->sample_spec.rate)
                    slaves_spec.rate = slave_sink->sample_spec.rate;

                if (!pa_channel_map_equal(&slaves_map, &slave_sink->channel_map))
                    slaves_spec.channels = 0;
            }
        }

        if (!is_first_slave) {
            if (slaves_spec.format != PA_SAMPLE_INVALID)
                ss.format = slaves_spec.format;

            ss.rate = slaves_spec.rate;

            if (slaves_spec.channels > 0) {
                map = slaves_map;
                ss.channels = slaves_map.channels;
            }
        }
    }

    if ((pa_modargs_get_sample_spec_and_channel_map(ma, &ss, &map, PA_CHANNEL_MAP_DEFAULT) < 0)) {
        pa_log("Invalid sample specification.");
        goto fail;
    }

    pa_sink_new_data_init(&data);
    data.namereg_fail = false;
    data.driver = __FILE__;
    data.module = m;
    pa_sink_new_data_set_name(&data, pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME));
    pa_sink_new_data_set_sample_spec(&data, &ss);
    pa_sink_new_data_set_channel_map(&data, &map);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_CLASS, "filter");

    if (slaves)
        pa_proplist_sets(data.proplist, "combine.slaves", slaves);

    if (pa_modargs_get_proplist(ma, "sink_properties", data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log("Invalid properties");
        pa_sink_new_data_done(&data);
        goto fail;
    }

    /* Check proplist for a description & fill in a default value if not */
    u->auto_desc = false;
    if (NULL == pa_proplist_gets(data.proplist, PA_PROP_DEVICE_DESCRIPTION)) {
        u->auto_desc = true;
        pa_proplist_sets(data.proplist, PA_PROP_DEVICE_DESCRIPTION, "Simultaneous Output");
    }

    u->sink = pa_sink_new(m->core, &data, PA_SINK_LATENCY);
    pa_sink_new_data_done(&data);

    if (!u->sink) {
        pa_log("Failed to create sink");
        goto fail;
    }

    u->sink->parent.process_msg = sink_process_msg;
    u->sink->set_state = sink_set_state;
    u->sink->userdata = u;

    pa_sink_set_rtpoll(u->sink, u->rtpoll);
    pa_sink_set_asyncmsgq(u->sink, u->thread_mq.inq);

    u->block_usec = BLOCK_USEC;
    pa_sink_set_max_request(u->sink, pa_usec_to_bytes(u->block_usec, &u->sink->sample_spec));

    if (!u->automatic) {
        const char*split_state;
        char *n = NULL;
        pa_assert(slaves);

        /* The slaves have been specified manually */

        split_state = NULL;
        while ((n = pa_split(slaves, ",", &split_state))) {
            pa_sink *slave_sink;

            if (!(slave_sink = pa_namereg_get(m->core, n, PA_NAMEREG_SINK)) || slave_sink == u->sink) {
                pa_log("Invalid slave sink '%s'", n);
                pa_xfree(n);
                goto fail;
            }

            pa_xfree(n);

            if (!output_new(u, slave_sink)) {
                pa_log("Failed to create slave sink input on sink '%s'.", slave_sink->name);
                goto fail;
            }
        }

        if (pa_idxset_size(u->outputs) < 1)
            pa_log_warn("No slave sinks specified.");

        u->sink_put_slot = NULL;

    } else {
        pa_sink *s;

        /* We're in automatic mode, we add every sink that matches our needs  */

        PA_IDXSET_FOREACH(s, m->core->sinks, idx) {

            if (!is_suitable_sink(u, s))
                continue;

            if (!output_new(u, s)) {
                pa_log("Failed to create sink input on sink '%s'.", s->name);
                goto fail;
            }
        }
    }

    u->sink_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE, (pa_hook_cb_t) sink_put_hook_cb, u);
    u->sink_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_unlink_hook_cb, u);
    u->sink_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_STATE_CHANGED], PA_HOOK_NORMAL, (pa_hook_cb_t) sink_state_changed_hook_cb, u);

    if (!(u->thread = pa_thread_new("combine", thread_func, u))) {
        pa_log("Failed to create thread.");
        goto fail;
    }

    /* Activate the sink and the sink inputs */
    pa_sink_put(u->sink);

    PA_IDXSET_FOREACH(o, u->outputs, idx)
        output_verify(o);

    if (u->adjust_time > 0)
        u->time_event = pa_core_rttime_new(m->core, pa_rtclock_now() + u->adjust_time, time_callback, u);

    pa_modargs_free(ma);

    return 0;

fail:

    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;
    struct output *o;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    pa_strlist_free(u->unlinked_slaves);

    if (u->sink_put_slot)
        pa_hook_slot_free(u->sink_put_slot);

    if (u->sink_unlink_slot)
        pa_hook_slot_free(u->sink_unlink_slot);

    if (u->sink_state_changed_slot)
        pa_hook_slot_free(u->sink_state_changed_slot);

    if (u->outputs) {
        while ((o = pa_idxset_first(u->outputs, NULL)))
            output_free(o);

        pa_idxset_free(u->outputs, NULL);
    }

    if (u->sink)
        pa_sink_unlink(u->sink);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
    }

    pa_thread_mq_done(&u->thread_mq);

    if (u->sink)
        pa_sink_unref(u->sink);

    if (u->rtpoll)
        pa_rtpoll_free(u->rtpoll);

    if (u->time_event)
        u->core->mainloop->time_free(u->time_event);

    if (u->thread_info.smoother)
        pa_smoother_free(u->thread_info.smoother);

    pa_xfree(u);
}

static pa_sink_input *add_slave(struct userdata *u, pa_sink *sink) {
    struct output *o;

    pa_assert(u);
    pa_assert(sink);

    pa_log_debug("Adding a slave to module combine");

    if (!(o = output_new(u, sink))) {
        pa_log("Failed to create slave sink input on sink '%s'.", sink->name);
        return NULL;
    }

    output_verify(o);

    return o->sink_input;
}

static void remove_slave(struct userdata *u, pa_sink_input *i, pa_sink *s) {
    struct output *o;

    pa_assert(u);
    pa_assert(i || s);

    if (s == NULL)
	s = i->sink;

    o = find_output(u, s);

    if (o == NULL) {
	pa_log_debug("Couldn't find output in module combine");
        return;
    }

    output_disable(o);
    output_free(o);
}

static int move_slave(struct userdata *u, pa_sink_input *i, pa_sink *s) {
    struct output *o;
    int sts;

    pa_assert(u);
    pa_assert(i);
    pa_assert(s);

    pa_assert(i->sink);

    o = find_output(u, i->sink);

    if (o == NULL) {
	pa_log_debug("Could not find output with sink %s for moving", s->name);
	return -1;
    }

    if (i->sink == s)
	return 0;

    i->flags &= ~(pa_sink_input_flags_t)PA_SINK_INPUT_DONT_MOVE;

    sts = pa_sink_input_move_to(i, s, false);

    i->flags |= (pa_sink_input_flags_t)PA_SINK_INPUT_DONT_MOVE;


    if (sts < 0)
	return -1;

    o->sink = s;

    return 0;
}
