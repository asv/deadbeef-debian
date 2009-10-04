/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009  Alexey Yakovenko

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
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include "../../deadbeef.h"

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

static DB_decoder_t plugin;
static DB_functions_t *deadbeef;

static FILE *file;
static OggVorbis_File vorbis_file;
static vorbis_info *vi;
static int cur_bit_stream;
static int startsample;
static int endsample;
static int currentsample;

static void
cvorbis_free (void);

static int
cvorbis_init (DB_playItem_t *it) {
    file = NULL;
    vi = NULL;
    cur_bit_stream = -1;

    file = fopen (it->fname, "rb");
    if (!file) {
        return -1;
    }

    memset (&plugin.info, 0, sizeof (plugin.info));
    ov_open (file, &vorbis_file, NULL, 0);
    vi = ov_info (&vorbis_file, -1);
    if (!vi) { // not a vorbis stream
        cvorbis_free ();
        return -1;
    }
    plugin.info.bps = 16;
    //plugin.info.dataSize = ov_pcm_total (&vorbis_file, -1) * vi->channels * 2;
    plugin.info.channels = vi->channels;
    plugin.info.samplerate = vi->rate;
    plugin.info.readpos = 0;
    currentsample = 0;
    if (it->endsample > 0) {
        startsample = it->startsample;
        endsample = it->endsample;
        plugin.seek_sample (0);
    }
    else {
        startsample = 0;
        endsample = ov_pcm_total (&vorbis_file, -1)-1;
    }
    return 0;
}

static void
cvorbis_free (void) {
    if (file) {
        ov_clear (&vorbis_file);
        //fclose (file); //-- ov_clear closes it
        file = NULL;
        vi = NULL;
    }
}

static int
cvorbis_read (char *bytes, int size) {
    if (currentsample + size / (2 * plugin.info.channels) > endsample) {
        size = (endsample - currentsample + 1) * 2 * plugin.info.channels;
        trace ("size truncated to %d bytes, cursample=%d, endsample=%d, totalsamples=%d\n", size, currentsample, endsample, ov_pcm_total (&vorbis_file, -1));
        if (size <= 0) {
            return 0;
        }
    }
    int initsize = size;
    for (;;)
    {
        // read ogg
        int endianess = 0;
#if WORDS_BIGENDIAN
        endianess = 1;
#endif
        long ret=ov_read (&vorbis_file, bytes, size, endianess, 2, 1, &cur_bit_stream);
        if (ret <= 0)
        {
            // error or eof
            break;
        }
        else if (ret < size)
        {
            currentsample += ret / (vi->channels * 2);
            size -= ret;
            bytes += ret;
        }
        else {
            currentsample += ret / (vi->channels * 2);
            size = 0;
            break;
        }
    }
    plugin.info.readpos = (float)(ov_pcm_tell(&vorbis_file)-startsample)/vi->rate;
    return initsize - size;
}

static int
cvorbis_seek_sample (int sample) {
    if (!file) {
        return -1;
    }
    sample += startsample;
    int res = ov_pcm_seek (&vorbis_file, sample);
    if (res != 0 && res != OV_ENOSEEK)
        return -1;
    int tell = ov_pcm_tell (&vorbis_file);
    if (tell != sample) {
        fprintf (stderr, "oggvorbis: failed to do sample-accurate seek (%d->%d)\n", sample, tell);
    }
    currentsample = sample;
    plugin.info.readpos = (float)(ov_pcm_tell(&vorbis_file) - startsample)/vi->rate;
    return 0;
}

static int
cvorbis_seek (float time) {
    return cvorbis_seek_sample (time * vi->rate);
}

static DB_playItem_t *
cvorbis_insert (DB_playItem_t *after, const char *fname) {
    // check for validity
    FILE *fp = fopen (fname, "rb");
    if (!fp) {
        return NULL;
    }
    OggVorbis_File vorbis_file;
    vorbis_info *vi;
    ov_open (fp, &vorbis_file, NULL, 0);
    vi = ov_info (&vorbis_file, -1);
    if (!vi) { // not a vorbis stream
        return NULL;
    }
    float duration = ov_time_total (&vorbis_file, -1);
    int totalsamples = ov_pcm_total (&vorbis_file, -1);
    DB_playItem_t *cue_after = deadbeef->pl_insert_cue (after, fname, &plugin, "OggVorbis", totalsamples, vi->rate);
    if (cue_after) {
        ov_clear (&vorbis_file);
        return cue_after;
    }

    DB_playItem_t *it = deadbeef->pl_item_alloc ();
    it->decoder = &plugin;
    it->fname = strdup (fname);
    it->filetype = "OggVorbis";
    it->duration = duration;

    // metainfo
    int title_added = 0;
    vorbis_comment *vc = ov_comment (&vorbis_file, -1);
    if (vc) {
        deadbeef->pl_add_meta (it, "vendor", vc->vendor);
        for (int i = 0; i < vc->comments; i++) {
            if (!strncasecmp (vc->user_comments[i], "artist=", 7)) {
                deadbeef->pl_add_meta (it, "artist", vc->user_comments[i] + 7);
            }
            else if (!strncasecmp (vc->user_comments[i], "album=", 6)) {
                deadbeef->pl_add_meta (it, "album", vc->user_comments[i] + 6);
            }
            else if (!strncasecmp (vc->user_comments[i], "title=", 6)) {
                deadbeef->pl_add_meta (it, "title", vc->user_comments[i] + 6);
                title_added = 1;
            }
            else if (!strncasecmp (vc->user_comments[i], "date=", 5)) {
                deadbeef->pl_add_meta (it, "date", vc->user_comments[i] + 5);
            }
            else if (!strncasecmp (vc->user_comments[i], "replaygain_album_gain=", 22)) {
                it->replaygain_album_gain = atof (vc->user_comments[i] + 22);
            }
            else if (!strncasecmp (vc->user_comments[i], "replaygain_album_peak=", 22)) {
                it->replaygain_album_peak = atof (vc->user_comments[i] + 22);
            }
            else if (!strncasecmp (vc->user_comments[i], "replaygain_track_gain=", 22)) {
                it->replaygain_track_gain = atof (vc->user_comments[i] + 22);
            }
            else if (!strncasecmp (vc->user_comments[i], "replaygain_track_peak=", 22)) {
                it->replaygain_track_peak = atof (vc->user_comments[i] + 22);
            }
        }
    }
    if (!title_added) {
        deadbeef->pl_add_meta (it, "title", NULL);
    }
    ov_clear (&vorbis_file);
    after = deadbeef->pl_insert_item (after, it);
    return after;
}

static const char * exts[] = { "ogg", NULL };
static const char *filetypes[] = { "OggVorbis", NULL };

// define plugin interface
static DB_decoder_t plugin = {
    DB_PLUGIN_SET_API_VERSION
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_DECODER,
    .plugin.name = "OggVorbis decoder",
    .plugin.author = "Alexey Yakovenko",
    .plugin.email = "waker@users.sourceforge.net",
    .plugin.website = "http://deadbeef.sf.net",
    .init = cvorbis_init,
    .free = cvorbis_free,
    .read_int16 = cvorbis_read,
    // vorbisfile can't output float32
//    .read_float32 = cvorbis_read_float32,
    .seek = cvorbis_seek,
    .seek_sample = cvorbis_seek_sample,
    .insert = cvorbis_insert,
    .exts = exts,
    .id = "stdogg",
    .filetypes = filetypes
};

DB_plugin_t *
vorbis_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}
