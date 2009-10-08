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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <FLAC/stream_decoder.h>
#include "../../deadbeef.h"

static DB_decoder_t plugin;
static DB_functions_t *deadbeef;

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

static FLAC__StreamDecoder *decoder = 0;
#define BUFFERSIZE 100000
static char buffer[BUFFERSIZE]; // this buffer always has float samples
static int remaining; // bytes remaining in buffer from last read
static int startsample;
static int endsample;
static int currentsample;

typedef struct {
    DB_playItem_t *after;
    DB_playItem_t *last;
    const char *fname;
    int samplerate;
    int channels;
    int totalsamples;
    int bps;
} cue_cb_data_t;

static FLAC__StreamDecoderWriteStatus
cflac_write_callback (const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const inputbuffer[], void *client_data) {
    if (frame->header.blocksize == 0) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    int readbytes = frame->header.blocksize * plugin.info.channels * plugin.info.bps / 8;
    int bufsize = BUFFERSIZE-remaining;
    int bufsamples = bufsize / (plugin.info.channels * plugin.info.bps / 8);
    int nsamples = min (bufsamples, frame->header.blocksize);
    char *bufptr = &buffer[remaining];
    float mul = 1.f/ ((1 << (plugin.info.bps-1))-1);

    for (int i = 0; i < nsamples; i++) {
        int32_t sample = inputbuffer[0][i];
        *((float*)bufptr) = sample * mul;
        bufptr += sizeof (float);
        remaining += sizeof (float);
        if (plugin.info.channels > 1) {
            int32_t sample = inputbuffer[1][i];
            *((float*)bufptr) = sample * mul;
            bufptr += sizeof (float);
            remaining += sizeof (float);
        }
    }
    if (readbytes > bufsize) {
        fprintf (stderr, "flac: buffer overflow, distortion will occur\n");
    //    return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
cflac_metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data) {
    cue_cb_data_t *cb = (cue_cb_data_t *)client_data;
    cb->totalsamples = metadata->data.stream_info.total_samples;
    cb->samplerate = metadata->data.stream_info.sample_rate;
    cb->channels = metadata->data.stream_info.channels;
    cb->bps = metadata->data.stream_info.bits_per_sample;
}

static void
cflac_error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data) {
//    fprintf(stderr, "cflac: got error callback: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}

static int cflac_init_stop_decoding;

static void
cflac_init_error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data) {
//    fprintf(stderr, "cflac: got error callback: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
    cflac_init_stop_decoding = 1;
}

static void
cflac_free (void);

static int
cflac_init (DB_playItem_t *it) {
    FILE *fp = fopen (it->fname, "rb");
    if (!fp) {
        return -1;
    }
    int skip = deadbeef->junk_get_leading_size (fp);
    if (skip > 0) {
        fseek (fp, skip, SEEK_SET);
    }
    char sign[4];
    if (fread (sign, 1, 4, fp) != 4) {
        fclose (fp);
        return -1;
    }
    if (strncmp (sign, "fLaC", 4)) {
        fclose (fp);
        return -1;
    }
    fclose (fp);
    fp = NULL;

    FLAC__StreamDecoderInitStatus status;
    decoder = FLAC__stream_decoder_new();
    if (!decoder) {
//        printf ("FLAC__stream_decoder_new failed\n");
        return -1;
    }
    FLAC__stream_decoder_set_md5_checking(decoder, 0);
    cue_cb_data_t cb;
    status = FLAC__stream_decoder_init_file(decoder, it->fname, cflac_write_callback, cflac_metadata_callback, cflac_error_callback, &cb);
    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        cflac_free ();
        return -1;
    }
    //plugin.info.samplerate = -1;
    if (!FLAC__stream_decoder_process_until_end_of_metadata (decoder)) {
        cflac_free ();
        return -1;
    }
    plugin.info.samplerate = cb.samplerate;
    plugin.info.channels = cb.channels;
    plugin.info.bps = cb.bps;
    plugin.info.readpos = 0;
    if (plugin.info.samplerate == -1) { // not a FLAC stream
        cflac_free ();
        return -1;
    }
    if (it->endsample > 0) {
        startsample = it->startsample;
        endsample = it->endsample;
        if (plugin.seek_sample (0) < 0) {
            cflac_free ();
            return -1;
        }
        trace ("flac(cue): startsample=%d, endsample=%d, totalsamples=%d, currentsample=%d\n", startsample, endsample, flac_callbacks.totalsamples, currentsample);
    }
    else {
        startsample = 0;
        endsample = cb.totalsamples-1;
        currentsample = 0;
        trace ("flac: startsample=%d, endsample=%d, totalsamples=%d\n", startsample, endsample, flac_callbacks.totalsamples);
    }

    remaining = 0;
    return 0;
}

static void
cflac_free (void) {
    if (decoder) {
        FLAC__stream_decoder_delete(decoder);
        decoder = NULL;
    }
}

static int
cflac_read_int16 (char *bytes, int size) {
    if (size / (2 * plugin.info.channels) + currentsample > endsample) {
        size = (endsample - currentsample + 1) * 2 * plugin.info.channels;
        trace ("size truncated to %d bytes, cursample=%d, endsample=%d\n", size, currentsample, endsample);
        if (size <= 0) {
            return 0;
        }
    }
    int initsize = size;
    do {
        if (remaining) {
            int s = size * 2;
            int sz = min (remaining, s);
            // convert from float to int16
            float *in = (float *)buffer;
            for (int i = 0; i < sz/4; i++) {
                *((int16_t *)bytes) = (int16_t)((*in) * 0x7fff);
                size -= 2;
                bytes += 2;
                in++;
            }
            if (sz < remaining) {
                memmove (buffer, &buffer[sz], remaining-sz);
            }
            remaining -= sz;
            currentsample += sz / (4 * plugin.info.channels);
            plugin.info.readpos += (float)sz / (plugin.info.channels * plugin.info.samplerate * sizeof (float));
        }
        if (!size) {
            break;
        }
        if (!FLAC__stream_decoder_process_single (decoder)) {
            break;
        }
        if (FLAC__stream_decoder_get_state (decoder) == FLAC__STREAM_DECODER_END_OF_STREAM) {
            break;
        }
    } while (size > 0);

    return initsize - size;
}

static int
cflac_read_float32 (char *bytes, int size) {
    if (size / (4 * plugin.info.channels) + currentsample > endsample) {
        size = (endsample - currentsample + 1) * 4 * plugin.info.channels;
        trace ("size truncated to %d bytes, cursample=%d, endsample=%d\n", size, currentsample, endsample);
        if (size <= 0) {
            return 0;
        }
    }
    int initsize = size;
    do {
        if (remaining) {
            int sz = min (remaining, size);
            memcpy (bytes, buffer, sz);
            size -= sz;
            bytes += sz;
            if (sz < remaining) {
                memmove (buffer, &buffer[sz], remaining-sz);
            }
            remaining -= sz;
            currentsample += sz / (4 * plugin.info.channels);
            plugin.info.readpos += (float)sz / (plugin.info.channels * plugin.info.samplerate * sizeof (int32_t));
        }
        if (!size) {
            break;
        }
        if (!FLAC__stream_decoder_process_single (decoder)) {
            break;
        }
        if (FLAC__stream_decoder_get_state (decoder) == FLAC__STREAM_DECODER_END_OF_STREAM) {
            break;
        }
    } while (size > 0);

    return initsize - size;
}

static int
cflac_seek_sample (int sample) {
    sample += startsample;
    if (!FLAC__stream_decoder_seek_absolute (decoder, (FLAC__uint64)(sample))) {
        return -1;
    }
    remaining = 0;
    currentsample = sample;
    plugin.info.readpos = (float)(sample - startsample)/ plugin.info.samplerate;
    return 0;
}

static int
cflac_seek (float time) {
    return cflac_seek_sample (time * plugin.info.samplerate);
}

static FLAC__StreamDecoderWriteStatus
cflac_init_write_callback (const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const inputbuffer[], void *client_data) {
    if (frame->header.blocksize == 0 || cflac_init_stop_decoding) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
cflac_init_cue_metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data) {
    if (cflac_init_stop_decoding) {
        return;
    }
    cue_cb_data_t *cb = (cue_cb_data_t *)client_data;
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        cb->samplerate = metadata->data.stream_info.sample_rate;
        cb->channels = metadata->data.stream_info.channels;
        //cb->duration = metadata->data.stream_info.total_samples / (float)metadata->data.stream_info.sample_rate;
        cb->totalsamples = metadata->data.stream_info.total_samples;
    }
    else if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
        const FLAC__StreamMetadata_VorbisComment *vc = &metadata->data.vorbis_comment;
        for (int i = 0; i < vc->num_comments; i++) {
            const FLAC__StreamMetadata_VorbisComment_Entry *c = &vc->comments[i];
            if (c->length > 0) {
                char s[c->length+1];
                s[c->length] = 0;
                memcpy (s, c->entry, c->length);
                if (!strncasecmp (s, "cuesheet=", 9)) {
                    cb->last = deadbeef->pl_insert_cue_from_buffer (cb->after, cb->fname, s+9, c->length-9, &plugin, "FLAC", cb->totalsamples, cb->samplerate);
                }
            }
        }
    }
}

static void
cflac_init_metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data) {
    if (cflac_init_stop_decoding) {
        fprintf (stderr, "error flag is set, ignoring init_metadata callback..\n");
        return;
    }
    DB_playItem_t *it = (DB_playItem_t *)client_data;
    //it->tracknum = 0;
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        it->duration = metadata->data.stream_info.total_samples / (float)metadata->data.stream_info.sample_rate;
    }
    else if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
        const FLAC__StreamMetadata_VorbisComment *vc = &metadata->data.vorbis_comment;
        int title_added = 0;
        for (int i = 0; i < vc->num_comments; i++) {
            const FLAC__StreamMetadata_VorbisComment_Entry *c = &vc->comments[i];
            if (c->length > 0) {
                char s[c->length+1];
                s[c->length] = 0;
                memcpy (s, c->entry, c->length);
                if (!strncasecmp (s, "ARTIST=", 7)) {
                    deadbeef->pl_add_meta (it, "artist", s + 7);
                }
                else if (!strncasecmp (s, "TITLE=", 6)) {
                    deadbeef->pl_add_meta (it, "title", s + 6);
                    title_added = 1;
                }
                else if (!strncasecmp (s, "ALBUM=", 6)) {
                    deadbeef->pl_add_meta (it, "album", s + 6);
                }
                else if (!strncasecmp (s, "TRACKNUMBER=", 12)) {
                    deadbeef->pl_add_meta (it, "track", s + 12);
                }
                else if (!strncasecmp (s, "DATE=", 5)) {
                    deadbeef->pl_add_meta (it, "date", s + 5);
                }
                else if (!strncasecmp (s, "replaygain_album_gain=", 22)) {
                    it->replaygain_album_gain = atof (s + 22);
                }
                else if (!strncasecmp (s, "replaygain_album_peak=", 22)) {
                    it->replaygain_album_peak = atof (s + 22);
                }
                else if (!strncasecmp (s, "replaygain_track_gain=", 22)) {
                    it->replaygain_track_gain = atof (s + 22);
                }
                else if (!strncasecmp (s, "replaygain_track_peak=", 22)) {
                    it->replaygain_track_peak = atof (s + 22);
                }
                else {
                    trace ("found flac meta: %s\n", s);
                }
            }
        }
        if (!title_added) {
            deadbeef->pl_add_meta (it, "title", NULL);
        }

//    pl_add_meta (it, "artist", performer);
//    pl_add_meta (it, "album", albumtitle);
//    pl_add_meta (it, "track", track);
//    pl_add_meta (it, "title", title);
    }
//    int *psr = (int *)client_data;
//    *psr = metadata->data.stream_info.sample_rate;
}

static DB_playItem_t *
cflac_insert (DB_playItem_t *after, const char *fname) {
    DB_playItem_t *it = NULL;
    FLAC__StreamDecoder *decoder = NULL;
    FILE *fp = fopen (fname, "rb");
    if (!fp) {
        goto cflac_insert_fail;
    }
    // skip id3 junk
    int skip = deadbeef->junk_get_leading_size (fp);
    if (skip > 0) {
        fseek (fp, skip, SEEK_SET);
    }
    char sign[4];
    if (fread (sign, 1, 4, fp) != 4) {
        goto cflac_insert_fail;
    }
    if (strncmp (sign, "fLaC", 4)) {
        goto cflac_insert_fail;
    }
    fclose (fp);
    fp = NULL;
    cflac_init_stop_decoding = 0;
    //try embedded cue, and calculate duration
    FLAC__StreamDecoderInitStatus status;
    decoder = FLAC__stream_decoder_new();
    if (!decoder) {
        goto cflac_insert_fail;
    }
    FLAC__stream_decoder_set_md5_checking(decoder, 0);

    // try embedded cue
    cue_cb_data_t cb = {
        .fname = fname,
        .after = after,
        .last = after
    };
    FLAC__stream_decoder_set_metadata_respond_all (decoder);
    status = FLAC__stream_decoder_init_file (decoder, fname, cflac_init_write_callback, cflac_init_cue_metadata_callback, cflac_init_error_callback, &cb);
    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK || cflac_init_stop_decoding) {
        goto cflac_insert_fail;
    }
    if (!FLAC__stream_decoder_process_until_end_of_metadata (decoder) || cflac_init_stop_decoding) {
        goto cflac_insert_fail;
    }

    FLAC__stream_decoder_delete(decoder);
    decoder = NULL;
    if (cb.last != after) {
        // that means embedded cue is loaded
        return cb.last;
    }

    // try external cue
    DB_playItem_t *cue_after = deadbeef->pl_insert_cue (after, fname, &plugin, "flac", cb.totalsamples, cb.samplerate);
    if (cue_after) {
        return cue_after;
    }
    decoder = FLAC__stream_decoder_new();
    if (!decoder) {
        goto cflac_insert_fail;
    }
    FLAC__stream_decoder_set_md5_checking(decoder, 0);
    // try single FLAC file without cue
    FLAC__stream_decoder_set_metadata_respond_all (decoder);
    int samplerate = -1;
    it = deadbeef->pl_item_alloc ();
    it->decoder = &plugin;
    it->fname = strdup (fname);
    status = FLAC__stream_decoder_init_file (decoder, fname, cflac_init_write_callback, cflac_init_metadata_callback, cflac_init_error_callback, it);
    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK || cflac_init_stop_decoding) {
        goto cflac_insert_fail;
    }
    if (!FLAC__stream_decoder_process_until_end_of_metadata (decoder) || cflac_init_stop_decoding) {
        goto cflac_insert_fail;
    }
    FLAC__stream_decoder_delete(decoder);
    decoder = NULL;
    it->filetype = "FLAC";
    after = deadbeef->pl_insert_item (after, it);
    return after;
cflac_insert_fail:
    if (it) {
        deadbeef->pl_item_free (it);
    }
    if (decoder) {
        FLAC__stream_decoder_delete(decoder);
    }
    if (fp) {
        fclose (fp);
    }
    return NULL;
}

static const char *exts[] = { "flac", NULL };

static const char *filetypes[] = { "FLAC", NULL };

// define plugin interface
static DB_decoder_t plugin = {
    DB_PLUGIN_SET_API_VERSION
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_DECODER,
    .plugin.name = "FLAC decoder",
    .plugin.author = "Alexey Yakovenko",
    .plugin.email = "waker@users.sourceforge.net",
    .plugin.website = "http://deadbeef.sf.net",
    .init = cflac_init,
    .free = cflac_free,
    .read_int16 = cflac_read_int16,
    .read_float32 = cflac_read_float32,
    .seek = cflac_seek,
    .seek_sample = cflac_seek_sample,
    .insert = cflac_insert,
    .exts = exts,
    .id = "stdflac",
    .filetypes = filetypes
};

DB_plugin_t *
flac_load (DB_functions_t *api) {
    deadbeef = api;
    return DB_PLUGIN (&plugin);
}
