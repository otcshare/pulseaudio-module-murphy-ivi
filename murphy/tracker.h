/*
 * module-murphy-ivi -- PulseAudio module for providing audio routing support
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#ifndef footrackerfoo
#define footrackerfoo

#include "userdata.h"


struct pa_card_hooks {
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
    pa_hook_slot    *profchg;
};

struct pa_sink_hooks {
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
    pa_hook_slot    *portchg;
    pa_hook_slot    *portavail;
};

struct pa_source_hooks {
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
    pa_hook_slot    *portchg;
    pa_hook_slot    *portavail;
};

struct pa_sink_input_hooks {
    pa_hook_slot    *neew;
    pa_hook_slot    *put;
    pa_hook_slot    *unlink;
};


struct pa_tracker {
    pa_card_hooks       card;
    pa_sink_hooks       sink;
    pa_source_hooks     source;
    pa_sink_input_hooks sink_input;
};

pa_tracker *pa_tracker_init(struct userdata *);
void pa_tracker_done(struct userdata *);

void pa_tracker_synchronize(struct userdata *);



#endif /* footrackerfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
