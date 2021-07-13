/*
 * Copyright (C) 2016 Neil Herald <neil.herald@gmail.com>
 *
 * Nautilus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _NautilusToolbarMenuSections NautilusToolbarMenuSections;

struct _NautilusToolbarMenuSections {
        GtkWidget *zoom_section;
        GtkWidget *extended_section;
        gboolean   supports_undo_redo;
};

G_END_DECLS