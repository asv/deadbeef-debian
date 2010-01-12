/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009-2010 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdint.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <string.h>
#include "../../deadbeef.h"

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

static DB_output_t plugin;
DB_functions_t *deadbeef;

static intptr_t null_tid;
static int null_terminate;
static int null_rate;
static int state;

static void
pnull_callback (char *stream, int len);

static void
pnull_thread (void *context);

static int
pnull_init (void);

static int
pnull_free (void);

static int
pnull_change_rate (int rate);

static int
pnull_play (void);

static int
pnull_stop (void);

static int
pnull_pause (void);

static int
pnull_unpause (void);

static int
pnull_get_rate (void);

static int
pnull_get_bps (void);

static int
pnull_get_channels (void);

static int
pnull_get_endianness (void);

int
pnull_init (void) {
    trace ("pnull_init\n");
    state = OUTPUT_STATE_STOPPED;
    null_rate = 44100;
    null_terminate = 0;
    null_tid = deadbeef->thread_start (pnull_thread, NULL);
    return 0;
}

int
pnull_change_rate (int rate) {
    null_rate = rate;
    return null_rate;
}

int
pnull_free (void) {
    trace ("pnull_free\n");
    if (!null_terminate) {
        if (null_tid) {
            null_terminate = 1;
            deadbeef->thread_join (null_tid);
        }
        null_tid = 0;
        state = OUTPUT_STATE_STOPPED;
        null_terminate = 0;
    }
    return 0;
}

int
pnull_play (void) {
    if (!null_tid) {
        pnull_init ();
    }
    state = OUTPUT_STATE_PLAYING;
    return 0;
}

int
pnull_stop (void) {
    state = OUTPUT_STATE_STOPPED;
    deadbeef->streamer_reset (1);
    return 0;
}

int
pnull_pause (void) {
    if (state == OUTPUT_STATE_STOPPED) {
        return -1;
    }
    // set pause state
    state = OUTPUT_STATE_PAUSED;
    return 0;
}

int
pnull_unpause (void) {
    // unset pause state
    if (state == OUTPUT_STATE_PAUSED) {
        state = OUTPUT_STATE_PLAYING;
    }
    return 0;
}

int
pnull_get_rate (void) {
    return null_rate;
}

int
pnull_get_bps (void) {
    return 16;
}

int
pnull_get_channels (void) {
    return 2;
}

static int
pnull_get_endianness (void) {
#if WORDS_BIGENDIAN
    return 1;
#else
    return 0;
#endif
}

static void
pnull_thread (void *context) {
    prctl (PR_SET_NAME, "deadbeef-null", 0, 0, 0, 0);
    for (;;) {
        if (null_terminate) {
            break;
        }
        if (state != OUTPUT_STATE_PLAYING) {
            usleep (10000);
            continue;
        }
        
        char buf[4096];
        pnull_callback (buf, 1024);
    }
}

static void
pnull_callback (char *stream, int len) {
    if (!deadbeef->streamer_ok_to_read (len)) {
        memset (stream, 0, len);
        return;
    }
    int bytesread = deadbeef->streamer_read (stream, len);

    if (bytesread < len) {
        memset (stream + bytesread, 0, len-bytesread);
    }
}

int
pnull_get_state (void) {
    return state;
}

int
null_start (void) {
    return 0;
}

int
null_stop (void) {
    return 0;
}

DB_plugin_t *
nullout_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}

// define plugin interface
static DB_output_t plugin = {
    DB_PLUGIN_SET_API_VERSION
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.nostop = 1,
    .plugin.type = DB_PLUGIN_OUTPUT,
    .plugin.name = "null output plugin",
    .plugin.descr = "doesn't play anything",
    .plugin.author = "Alexey Yakovenko",
    .plugin.email = "waker@users.sourceforge.net",
    .plugin.website = "http://deadbeef.sf.net",
    .plugin.start = null_start,
    .plugin.stop = null_stop,
    .init = pnull_init,
    .free = pnull_free,
    .change_rate = pnull_change_rate,
    .play = pnull_play,
    .stop = pnull_stop,
    .pause = pnull_pause,
    .unpause = pnull_unpause,
    .state = pnull_get_state,
    .samplerate = pnull_get_rate,
    .bitspersample = pnull_get_bps,
    .channels = pnull_get_channels,
    .endianness = pnull_get_endianness,
};
