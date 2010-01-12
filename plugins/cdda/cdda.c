/*
    CD audio plugin for DeaDBeeF
    Copyright (C) 2009 Viktor Semykin <thesame.ml@gmail.com>

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

/* screwed/maintained by Alexey Yakovenko <waker@users.sourceforge.net> */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>

#include <cdio/cdio.h>
#include <cddb/cddb.h>

#include "../../deadbeef.h"

#define trace(...) { fprintf (stderr, __VA_ARGS__); }
//#define trace(fmt,...)

#define SECTORSIZE CDIO_CD_FRAMESIZE_RAW //2352
#define SAMPLESIZE 4 //bytes
#define BUFSIZE (CDIO_CD_FRAMESIZE_RAW * 2)

static DB_decoder_t plugin;
static DB_functions_t *deadbeef;

static CdIo_t* cdio = NULL;
static lsn_t first_sector;
static unsigned int sector_count;
static uint8_t tail [SECTORSIZE];
static unsigned int tail_len;
static int current_sector;
static unsigned int current_sample = 0;
static uintptr_t mutex;
static intptr_t cddb_tid;

#define DEFAULT_SERVER "freedb.org"
#define DEFAULT_PORT 888
#define DEFAULT_USE_CDDB 1
#define DEFAULT_PROTOCOL 1


struct cddb_thread_params
{
    DB_playItem_t *items[100];
    CdIo_t *cdio;
};

static inline int
min (int a, int b) {
    return a < b ? a : b;
}

static int
cda_init (DB_playItem_t *it) {
//    trace ("CDA: initing %s\n", it->fname);

    size_t l = strlen (it->fname);
    char location[l+1];
    memcpy (location, it->fname, l+1);

    char *nr = strchr (location, '#');
    if (nr) {
        *nr = 0; nr++;
    }
    else {
        trace ("malformed cdaudio track filename\n");
        return -1;
    }
    int track_nr = atoi (nr);
    char *fname = (*location) ? location : NULL; //NULL if empty string; means pysical CD drive

    cdio = cdio_open (fname, DRIVER_UNKNOWN);
    if  (!cdio)
    {
        trace ("Could not open CD\n");
        return -1;
    }

    if (TRACK_FORMAT_AUDIO != cdio_get_track_format (cdio, track_nr))
    {
        trace ("Not an audio track (%d)\n", track_nr);
        return -1;
    }

    plugin.info.bps = 16,
    plugin.info.channels = 2,
    plugin.info.samplerate = 44100,
    plugin.info.readpos = 0;

    first_sector = cdio_get_track_lsn (cdio, track_nr);
    sector_count = cdio_get_track_sec_count (cdio, track_nr);
    current_sector = first_sector;
    tail_len = 0;
    current_sample = 0;
    return 0;
}

int
cda_read_int16 (char *bytes, int size) {
    int extrasize = 0;
    
    if (tail_len > 0)
    {
        if (tail_len >= size)
        {
//            trace ("Easy case\n");
            memcpy (bytes, tail, size);
            tail_len -= size;
            memmove (tail, tail+size, tail_len);
            return size;
        }
//        trace ("Prepending with tail of %d bytes\n", tail_len);
        extrasize = tail_len;
        memcpy (bytes, tail, tail_len);
        bytes += tail_len;
        size -= tail_len;
        tail_len = 0;
    }

    int sectors_to_read = size / SECTORSIZE + 1;
    int end = 0;
    
    if (current_sector + sectors_to_read > first_sector + sector_count) //we reached end of track
    {
        end = 1;
        sectors_to_read = first_sector + sector_count - current_sector;
//        trace ("We reached end of track\n");
    }

    int bufsize = sectors_to_read * SECTORSIZE;

    tail_len = end ? 0 : bufsize - size;

    char *buf = alloca (bufsize);

    driver_return_code_t ret = cdio_read_audio_sectors (cdio, buf, current_sector, sectors_to_read);
    if (ret != DRIVER_OP_SUCCESS)
        return 0;
    current_sector += sectors_to_read;

    int retsize = end ? bufsize : size;

    memcpy (bytes, buf, retsize);
    if (!end)
        memcpy (tail, buf+retsize, tail_len);

    retsize += extrasize;
//    trace ("requested: %d; tail_len: %d; size: %d; sectors_to_read: %d; return: %d\n", initsize, tail_len, size, sectors_to_read, retsize);
    current_sample += retsize / SAMPLESIZE;
    plugin.info.readpos = current_sample / 44100;
    return retsize;
}

static void
cda_free ()
{
    if (cdio)
    {
        cdio_destroy (cdio);
        cdio = NULL;
    }
}

static int
cda_seek_sample (int sample)
{
    int sector = sample / (SECTORSIZE / SAMPLESIZE) + first_sector;
    int offset = (sample % (SECTORSIZE / SAMPLESIZE)) * SAMPLESIZE; //in bytes
    char buf [SECTORSIZE];

    driver_return_code_t ret = cdio_read_audio_sector (cdio, buf, sector);
    if (ret != DRIVER_OP_SUCCESS)
        return -1;
    memcpy (tail, buf + offset, SECTORSIZE - offset );
    current_sector = sector;
    current_sample = sample;
    plugin.info.readpos = current_sample / 44100;
    return 0;
}

static int
cda_seek (float sec)
{
    return cda_seek_sample (sec * 44100);
}

cddb_disc_t*
resolve_disc (CdIo_t *cdio)
{
    track_t first_track = cdio_get_first_track_num (cdio);
    track_t tracks = cdio_get_num_tracks (cdio);
    track_t i;
    cddb_track_t *track;

    cddb_disc_t *disc = cddb_disc_new();

    cddb_disc_set_length (disc, cdio_get_track_lba (cdio, CDIO_CDROM_LEADOUT_TRACK) / CDIO_CD_FRAMES_PER_SEC);

    for (i = 0; i < tracks; i++)
    {
        lsn_t offset = cdio_get_track_lba (cdio, i+first_track);
        track = cddb_track_new();
        cddb_track_set_frame_offset (track, offset);
        cddb_disc_add_track (disc, track);
    }
    cdio_destroy (cdio);

    cddb_conn_t *conn = NULL;

    conn = cddb_new();

    cddb_set_server_name (conn, deadbeef->conf_get_str ("cdda.freedb.host", DEFAULT_SERVER));
    cddb_set_server_port (conn, deadbeef->conf_get_int ("cdda.freedb.port", DEFAULT_PORT));

    if (!deadbeef->conf_get_int ("cdda.protocol", DEFAULT_PROTOCOL))
    {
        cddb_http_enable (conn);
        if (deadbeef->conf_get_int ("network.proxy", 0))
        {
            cddb_set_server_port(conn, deadbeef->conf_get_int ("network.proxy.port", 8080));
            cddb_set_server_name(conn, deadbeef->conf_get_str ("network.proxy.address", ""));
        }
    }

    int matches = cddb_query (conn, disc);
    if (matches == -1)
    {
        cddb_disc_destroy (disc);
        cddb_destroy (conn);
        return NULL;
    }
    cddb_read (conn, disc);
    cddb_destroy (conn);
    return disc;
}

static DB_playItem_t *
insert_single_track (CdIo_t* cdio, DB_playItem_t *after, const char* file, int track_nr)
{
    char tmp[file ? strlen (file) + 20 : 20];
    if (file)
        snprintf (tmp, sizeof (tmp), "%s#%d.cda", file, track_nr);
    else
        snprintf (tmp, sizeof (tmp), "#%d.cda", track_nr);

    if (TRACK_FORMAT_AUDIO != cdio_get_track_format (cdio, track_nr))
    {
        trace ("Not an audio track (%d)\n", track_nr);
        return NULL;
    }

    sector_count = cdio_get_track_sec_count (cdio, track_nr);

    DB_playItem_t *it = deadbeef->pl_item_alloc ();
    it->decoder = &plugin;
    it->fname = strdup (tmp);
    it->filetype = "cdda";
    deadbeef->pl_set_item_duration (it, (float)sector_count / 75.0);

    snprintf (tmp, sizeof (tmp), "CD Track %02d", track_nr);
    deadbeef->pl_add_meta (it, "title", tmp);
    snprintf (tmp, sizeof (tmp), "%02d", track_nr);
    deadbeef->pl_add_meta (it, "track", tmp);

    after = deadbeef->pl_insert_item (after, it);

    return after;
}

static void
cddb_thread (void *items_i)
{
    struct cddb_thread_params *params = (struct cddb_thread_params*)items_i;
    DB_playItem_t **items = params->items;

    trace ("calling resolve_disc\n");
    deadbeef->mutex_lock (mutex);
    cddb_disc_t* disc = resolve_disc (params->cdio);
    deadbeef->mutex_unlock (mutex);
    if (!disc)
    {
        trace ("disc not resolved\n");
        free (params);
        return;
    }
    trace ("disc resolved\n");

    deadbeef->mutex_lock (mutex);
    const char *disc_title = cddb_disc_get_title (disc);
    const char *artist = cddb_disc_get_artist (disc);
    trace ("disc_title=%s, disk_artist=%s\n", disc_title, artist);
    cddb_track_t *track;
    int i;
    
    // FIXME: playlist must be locked before doing that
    int trk = 1;
    for (i = 0, track = cddb_disc_get_track_first (disc); items[i]; trk++, ++i, track = cddb_disc_get_track_next (disc))
    {
        // FIXME: problem will happen here if item(s) were deleted from playlist, and new items were added in their places
        // possible solutions: catch EV_TRACKDELETED and mark item(s) in every thread as NULL
        int idx = deadbeef->pl_get_idx_of (items[i]);
        trace ("track %d, artist=%s, album=%s, title=%s\n", i, artist, disc_title, cddb_track_get_title (track));
        if (idx == -1)
            continue;

        deadbeef->pl_delete_all_meta (items[i]);
        deadbeef->pl_add_meta (items[i], "artist", artist);
        deadbeef->pl_add_meta (items[i], "album", disc_title);
        deadbeef->pl_add_meta (items[i], "title", cddb_track_get_title (track));
        char tmp[5];
        snprintf (tmp, sizeof (tmp), "%02d", trk);
        deadbeef->pl_add_meta (items[i], "track", tmp);
        deadbeef->sendmessage (M_TRACKCHANGED, 0, idx, 0);
    }
    cddb_disc_destroy (disc);
    deadbeef->mutex_unlock (mutex);
    free (params);
    cddb_tid = 0;
}

static DB_playItem_t *
cda_insert (DB_playItem_t *after, const char *fname) {
//    trace ("CDA insert: %s\n", fname);

    int track_nr;
    DB_playItem_t *res;

    const char* shortname = strrchr (fname, '/');
    if (shortname) {
        shortname++;
    }
    else {
        shortname = fname;
    }
    const char *ext = strrchr (shortname, '.') + 1;
    int is_image = ext && (0 == strcmp (ext, "nrg"));

    if (0 == strcmp (ext, "cda")) {
        cdio = cdio_open (NULL, DRIVER_UNKNOWN);
    }
    else if (is_image) {
        cdio = cdio_open (fname, DRIVER_NRG);
    }

    if (!cdio) {
        return NULL;
    }

    if (0 == strcasecmp (shortname, "all.cda") || is_image)
    {
        track_t first_track = cdio_get_first_track_num (cdio);
        if (first_track == 0xff) {
            trace ("cdda: no medium found\n");
            cdio_destroy (cdio);
            return NULL;
        }
        track_t tracks = cdio_get_num_tracks (cdio);
        track_t i;
        res = after;
        struct cddb_thread_params *p = malloc (sizeof (struct cddb_thread_params));
        memset (p, 0, sizeof (struct cddb_thread_params));
        p->cdio = cdio;
        for (i = 0; i < tracks; i++)
        {
            res = insert_single_track (cdio, res, is_image ? fname : NULL, i+first_track);
            p->items[i] = res;
        }
        trace ("cdda: querying freedb...\n");
        if (deadbeef->conf_get_int ("cdda.freedb.enable", DEFAULT_USE_CDDB)) {
            if (cddb_tid) {
                deadbeef->thread_join (cddb_tid);
            }
            cddb_tid = deadbeef->thread_start (cddb_thread, p); //will destroy cdio
        }
    }
    else
    {
        track_nr = atoi (shortname);
        res = insert_single_track (cdio, after, NULL, track_nr);
        cdio_destroy (cdio);
    }
    return res;
}

static int
cda_start (void) {
    mutex = deadbeef->mutex_create ();
    return 0;
}

static int
cda_stop (void) {
    if (cddb_tid) {
        fprintf (stderr, "cdda: waiting cddb query to end\n");
        deadbeef->thread_join (cddb_tid);
    }
    deadbeef->mutex_free (mutex);
    return 0;
}

static const char *exts[] = { "cda", "nrg", NULL };
static const char *filetypes[] = { "cdda", NULL };

static const char settings_dlg[] =
    "property \"Use CDDB/FreeDB\" checkbox cdda.freedb.enable 1;\n"
    "property \"CDDB url (e.g. 'freedb.org')\" entry cdda.freedb.host freedb.org;\n"
    "property \"CDDB port number (e.g. '888')\" entry cdda.freedb.port 888;\n"
    "property \"Prefer CDDB protocol over HTTP\" checkbox cdda.protocol 1;"
;

// define plugin interface
static DB_decoder_t plugin = {
    DB_PLUGIN_SET_API_VERSION
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_DECODER,
    .plugin.name = "Audio CD player",
    .plugin.descr = "using libcdio, includes .nrg image support",
    .plugin.author = "Viktor Semykin, Alexey Yakovenko",
    .plugin.email = "thesame.ml@gmail.com, waker@users.sourceforge.net",
    .plugin.website = "http://deadbeef.sf.net",
    .plugin.start = cda_start,
    .plugin.stop = cda_stop,
    .plugin.configdialog = settings_dlg,
    .init = cda_init,
    .free = cda_free,
    .read_int16 = cda_read_int16,
    .seek = cda_seek,
    .seek_sample = cda_seek_sample,
    .insert = cda_insert,
    .exts = exts,
    .id = "cda",
    .filetypes = filetypes,
};

DB_plugin_t *
cdda_load (DB_functions_t *api) {
    deadbeef = api;
//    read_config();
    return DB_PLUGIN (&plugin);
}

