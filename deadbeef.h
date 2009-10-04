/*
  deadbeef.h -- plugin API of the DeaDBeeF audio player
  http://deadbeef.sourceforge.net

  Copyright (C) 2009 Alexey Yakovenko

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Note: DeaDBeeF player itself uses different license
*/


#ifndef __DEADBEEF_H
#define __DEADBEEF_H

#include <stdint.h>
#include <time.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// every plugin must define following entry-point:
// extern "C" DB_plugin_t* $MODULENAME_load (DB_functions_t *api);
// where $MODULENAME is a name of module
// e.g. if your plugin is called "myplugin.so", $MODULENAME is "myplugin"
// this function should return pointer to DB_plugin_t structure
// that is enough for both static and dynamic modules

// add DB_PLUGIN_SET_API_VERSION macro when you define plugin structure
// like this:
// static DB_decoder_t plugin = {
//   DB_PLUGIN_SET_API_VERSION
//  ............
// }
// this is required for versioning
// if you don't do it -- no version checking will be done (usefull for
// debugging/development)
// DON'T release plugins without DB_PLUGIN_SET_API_VERSION

#define DB_API_VERSION_MAJOR 0
#define DB_API_VERSION_MINOR 2

#define DB_PLUGIN_SET_API_VERSION\
    .plugin.api_vmajor = DB_API_VERSION_MAJOR,\
    .plugin.api_vminor = DB_API_VERSION_MINOR,

////////////////////////////
// playlist structures

// playlist item
// these are "public" fields, available to plugins
typedef struct {
    char *fname; // full pathname
    struct DB_decoder_s *decoder; // codec to use with this file
    int tracknum; // used for stuff like sid, nsf, cue (will be ignored by most codecs)
    int startsample; // start sample of track, or -1 for auto
    int endsample; // end sample of track, or -1 for auto
    float duration; // in seconds
    int shufflerating; // sort order for shuffle mode
    float playtime; // total playtime
    time_t started_timestamp; // result of calling time(NULL)
    const char *filetype; // e.g. MP3 or OGG
    float replaygain_album_gain;
    float replaygain_album_peak;
    float replaygain_track_gain;
    float replaygain_track_peak;
} DB_playItem_t;

// plugin types
enum {
    DB_PLUGIN_DECODER = 1,
    DB_PLUGIN_OUTPUT  = 2,
    DB_PLUGIN_DSP     = 3,
    DB_PLUGIN_MISC    = 4
};

typedef struct {
    int event;
    double time;
} DB_event_t;

typedef struct {
    DB_event_t ev;
    DB_playItem_t *song;
} DB_event_song_t;

// event callback type
typedef int (*DB_callback_t)(DB_event_t *, uintptr_t data);

// events
enum {
    DB_EV_FRAMEUPDATE = 0, // ticks around 20 times per second, but ticker may stop sometimes
    DB_EV_SONGCHANGED = 1, // triggers when song was just changed
    DB_EV_SONGSTARTED = 2, // triggers when song started playing (for scrobblers and such)
    DB_EV_SONGFINISHED = 3, // triggers when song finished playing (for scrobblers and such)
    DB_EV_MAX
};

// typecasting macros
#define DB_PLUGIN(x) ((DB_plugin_t *)(x))
#define DB_CALLBACK(x) ((DB_callback_t)(x))
#define DB_EVENT(x) ((DB_event_t *)(x))
#define DB_PLAYITEM(x) ((DB_playItem_t *)(x))

// forward decl for plugin struct
struct DB_plugin_s;

// player api definition
typedef struct {
    // versioning
    int vmajor;
    int vminor;
    // event subscribing
    void (*ev_subscribe) (struct DB_plugin_s *plugin, int ev, DB_callback_t callback, uintptr_t data);
    void (*ev_unsubscribe) (struct DB_plugin_s *plugin, int ev, DB_callback_t callback, uintptr_t data);
    // md5sum calc
    void (*md5) (uint8_t sig[16], const char *in, int len);
    void (*md5_to_str) (char *str, const uint8_t sig[16]);
    // playback control
    void (*playback_next) (void);
    void (*playback_prev) (void);
    void (*playback_pause) (void);
    void (*playback_stop) (void);
    void (*playback_play) (void);
    void (*playback_random) (void);
    float (*playback_get_pos) (void); // [0..100]
    void (*playback_set_pos) (float pos); // [0..100]
    int (*playback_get_samplerate) (void); // output samplerate
    // process control
    const char *(*get_config_dir) (void);
    void (*quit) (void);
    // threading
    intptr_t (*thread_start) (void (*fn)(uintptr_t ctx), uintptr_t ctx);
    int (*thread_join) (intptr_t tid);
    uintptr_t (*mutex_create) (void);
    void (*mutex_free) (uintptr_t mtx);
    int (*mutex_lock) (uintptr_t mtx);
    int (*mutex_unlock) (uintptr_t mtx);
    uintptr_t (*cond_create) (void);
    void (*cond_free) (uintptr_t cond);
    int (*cond_wait) (uintptr_t cond, uintptr_t mutex);
    int (*cond_signal) (uintptr_t cond);
    int (*cond_broadcast) (uintptr_t cond);
    // playlist access
    DB_playItem_t * (*pl_item_alloc) (void);
    void (*pl_item_free) (DB_playItem_t *it);
    void (*pl_item_copy) (DB_playItem_t *out, DB_playItem_t *in);
    DB_playItem_t *(*pl_insert_item) (DB_playItem_t *after, DB_playItem_t *it);
    // metainfo
    void (*pl_add_meta) (DB_playItem_t *it, const char *key, const char *value);
    const char *(*pl_find_meta) (DB_playItem_t *song, const char *meta);
    // cuesheet support
    DB_playItem_t *(*pl_insert_cue_from_buffer) (DB_playItem_t *after, const char *fname, const uint8_t *buffer, int buffersize, struct DB_decoder_s *decoder, const char *ftype, int numsamples, int samplerate);
    DB_playItem_t * (*pl_insert_cue) (DB_playItem_t *after, const char *filename, struct DB_decoder_s *decoder, const char *ftype, int numsamples, int samplerate);
    // volume control
    void (*volume_set_db) (float dB);
    float (*volume_get_db) (void);
    void (*volume_set_amp) (float amp);
    float (*volume_get_amp) (void);
    // junk reading
    int (*junk_read_id3v1) (DB_playItem_t *it, FILE *fp);
    int (*junk_read_id3v2) (DB_playItem_t *it, FILE *fp);
    int (*junk_read_ape) (DB_playItem_t *it, FILE *fp);
    int (*junk_get_leading_size) (FILE *fp);
} DB_functions_t;

// base plugin interface
typedef struct DB_plugin_s {
    // type must be one of DB_PLUGIN_ types
    int32_t type;
    // api version
    int16_t api_vmajor;
    int16_t api_vminor;
    // plugin version
    int16_t version_major;
    int16_t version_minor;
    // may be deactivated on failures after load
    int inactive;
    // any of those can be left NULL
    // though it's much better to fill them with something useful
    const char *name;
    const char *descr;
    const char *author;
    const char *email;
    const char *website;
    // start is called to start plugin; can be NULL
    int (*start) (void);
    // stop is called to deinit plugin; can be NULL
    int (*stop) (void);
    // exec_cmdline may be called at any moment when user sends commandline to player
    // can be NULL if plugin doesn't support commandline processing
    // cmdline is 0-separated list of strings, guaranteed to have 0 at the end
    // cmdline_size is number of bytes pointed by cmdline
    int (*exec_cmdline) (const char *cmdline, int cmdline_size);
} DB_plugin_t;

typedef struct {
    int bps;
    int channels;
    int samplerate;
    float readpos;
} DB_fileinfo_t;

// decoder plugin
typedef struct DB_decoder_s {
    DB_plugin_t plugin;
    DB_fileinfo_t info;
    // init is called to prepare song to be started
    int (*init) (DB_playItem_t *it);

    // free is called after decoding is finished
    void (*free) (void);

    // read is called by streamer to decode specified number of bytes
    // must return number of bytes that were successfully decoded (sample aligned)
    
    // read_int16 must always output 16 bit signed integer samples
    int (*read_int16) (char *buffer, int size);

    // read_float32 must always output 32 bit floating point samples
    int (*read_float32) (char *buffer, int size);

    int (*seek) (float seconds);

    // perform seeking in samples (if possible)
    // return -1 if failed, or 0 on success
    // if -1 is returned, that will mean that streamer must skip that song
    int (*seek_sample) (int sample);

    // 'insert' is called to insert new item to playlist
    // decoder is responsible to calculate duration, split it into subsongs, load cuesheet, etc
    // after==NULL means "prepend before 1st item in playlist"
    DB_playItem_t * (*insert) (DB_playItem_t *after, const char *fname); 

    int (*numvoices) (void);
    void (*mutevoice) (int voice, int mute);

    // NULL terminated array of all supported extensions
    const char **exts;

    // NULL terminated array of all file type names
    const char **filetypes;

    // codec id used for playlist serialization
    const char *id;
} DB_decoder_t;

// output plugin
typedef struct {
    DB_plugin_t plugin;
    // init is called once at plugin activation
    int (*init) (void (*callback)(char *stream, int len));
    // free is called if output plugin was changed to another, or unload is about to happen
    int (*free) (void);
    // play, stop, pause, unpause are called by deadbeef in response to user
    // events, or as part of streaming process
    // state must be 0 for stopped, 1 for playing and 2 for paused
    int (*play) (void);
    int (*stop) (void);
    int (*pause) (void);
    int (*unpause) (void);
    int (*state) (void);
    // following functions must return output sampling rate, bits per sample and number
    // of channels
    int (*samplerate) (void);
    int (*bitspersample) (void);
    int (*channels) (void);
    // must return 0 for little endian output, or 1 for big endian
    int (*endianess) (void);
} DB_output_t;

// dsp plugin
typedef struct {
    DB_plugin_t plugin;
    // process gets called before SRC
    // stereo samples are stored in interleaved format
    // stereo sample is counted as 1 sample
    void (*process) (float *samples, int channels, int nsamples);
} DB_dsp_t;

// misc plugin
// purpose is to provide extra services
// e.g. scrobbling, converting, tagging, custom gui, etc.
// misc plugins should be mostly event driven, so no special entry points in them
typedef struct {
    DB_plugin_t plugin;
} DB_misc_t;

#ifdef __cplusplus
}
#endif

#endif // __DEADBEEF_H
