/*
 * nautilus-debug: debug loggers for nautilus
 *
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Based on Empathy's empathy-debug.
 */

#pragma once

#include <config.h>
#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  NAUTILUS_DEBUG_APPLICATION = 1 << 1,
  NAUTILUS_DEBUG_ASYNC_JOBS = 1 << 2,
  NAUTILUS_DEBUG_BOOKMARKS = 1 << 3,
  NAUTILUS_DEBUG_DBUS = 1 << 4,
  NAUTILUS_DEBUG_DIRECTORY_VIEW = 1 << 5,
  NAUTILUS_DEBUG_FILE = 1 << 6,
  NAUTILUS_DEBUG_CANVAS_CONTAINER = 1 << 7,
  NAUTILUS_DEBUG_CANVAS_VIEW = 1 << 8,
  NAUTILUS_DEBUG_LIST_VIEW = 1 << 9,
  NAUTILUS_DEBUG_MIME = 1 << 10,
  NAUTILUS_DEBUG_PLACES = 1 << 11,
  NAUTILUS_DEBUG_PREVIEWER = 1 << 12,
  NAUTILUS_DEBUG_SMCLIENT = 1 << 13,
  NAUTILUS_DEBUG_WINDOW = 1 << 14,
  NAUTILUS_DEBUG_UNDO = 1 << 15,
  NAUTILUS_DEBUG_SEARCH = 1 << 16,
  NAUTILUS_DEBUG_SEARCH_HIT = 1 << 17,
  NAUTILUS_DEBUG_THUMBNAILS = 1 << 18,
  NAUTILUS_DEBUG_TAG_MANAGER = 1 << 19,
} DebugFlags;

void nautilus_debug_set_flags (DebugFlags flags);
gboolean nautilus_debug_flag_is_set (DebugFlags flag);

void nautilus_debug_valist (DebugFlags flag,
                            const gchar *format, va_list args);

void nautilus_debug (DebugFlags flag, const gchar *format, ...)
  G_GNUC_PRINTF (2, 3);

void nautilus_debug_files (DebugFlags flag, GList *files,
                           const gchar *format, ...) G_GNUC_PRINTF (3, 4);

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  nautilus_debug (DEBUG_FLAG, "%s: %s: " format, G_STRFUNC, G_STRLOC, \
                  ##__VA_ARGS__)

#define DEBUG_FILES(files, format, ...) \
  nautilus_debug_files (DEBUG_FLAG, files, "%s:" format, G_STRFUNC, \
                        ##__VA_ARGS__)

#define DEBUGGING nautilus_debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

G_END_DECLS
