/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_SPECIAL_LOCATION_BAR (nautilus_special_location_bar_get_type ())
G_DECLARE_FINAL_TYPE (NautilusSpecialLocationBar, nautilus_special_location_bar, NAUTILUS, SPECIAL_LOCATION_BAR, GtkInfoBar)

typedef enum {
	NAUTILUS_SPECIAL_LOCATION_TEMPLATES,
	NAUTILUS_SPECIAL_LOCATION_SCRIPTS,
} NautilusSpecialLocation;

GtkWidget	*nautilus_special_location_bar_new (NautilusSpecialLocation location);

G_END_DECLS