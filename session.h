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
#ifndef __SESSION_H
#define __SESSION_H

#include <stdint.h>

int
session_save (const char *fname);

int
session_load (const char *fname);

void
session_capture_window_attrs (uintptr_t window);

void
session_set_directory (const char *path);

void
session_set_volume (float vol);

void
session_restore_window_attrs (uintptr_t window);

const char *
session_get_directory (void);

float
session_get_volume (void);

void
session_set_playlist_order (int order);

int
session_get_playlist_order (void);

void
session_set_cursor_follows_playback (int on);

int
session_get_cursor_follows_playback (void);

void
session_set_playlist_looping (int looping);

int
session_get_playlist_looping (void);

#endif // __SESSION_H
