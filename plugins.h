/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009  Alexey Yakovenko

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifndef __PLUGINS_H
#define __PLUGINS_H
#include "deadbeef.h"

void
plug_load_all (void);

void
plug_unload_all (void);

void
plug_ev_subscribe (DB_plugin_t *plugin, int ev, DB_callback_t callback, uintptr_t data);

void
plug_ev_unsubscribe (DB_plugin_t *plugin, int ev, DB_callback_t callback, uintptr_t data);

void
plug_trigger_event (int ev);

void
plug_md5 (uint8_t sig[16], const char *in, int len);

void
plug_md5_to_str (char *str, const uint8_t sig[16]);

void
plug_playback_next (void);

void
plug_playback_prev (void);

void
plug_playback_pause (void);

void 
plug_playback_stop (void);

void 
plug_playback_play (void);

void 
plug_playback_random (void);

void 
plug_quit (void);

float
plug_playback_get_pos (void);

void
plug_playback_set_pos (float pos);

struct DB_decoder_s **
plug_get_decoder_list (void);

void
plug_volume_set_db (float db);

void
plug_volume_set_amp (float amp);

const char *
plug_get_config_dir (void);

#endif // __PLUGINS_H
