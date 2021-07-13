/*
   Copyright (C) 1999, 2000, 2001 Eazel, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, see <http://www.gnu.org/licenses/>.

   Author: Pavel Cisler <pavel@eazel.com>  
*/

#pragma once

#include <gdk/gdk.h>
#include <gio/gio.h>

void nautilus_file_changes_queue_file_added                      (GFile      *location);
void nautilus_file_changes_queue_file_changed                    (GFile      *location);
void nautilus_file_changes_queue_file_removed                    (GFile      *location);
void nautilus_file_changes_queue_file_moved                      (GFile      *from,
								  GFile      *to);

void nautilus_file_changes_consume_changes                       (gboolean    consume_all);
