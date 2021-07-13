/*
 *  nautilus-column.h - Info columns exported by 
 *                      NautilusColumnProvider objects.
 *
 *  Copyright (C) 2003 Novell, Inc.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <http://www.gnu.org/licenses/>.
 * 
 *  Author:  Dave Camp <dave@ximian.com>
 *
 */

#pragma once

#if !defined (NAUTILUS_EXTENSION_H) && !defined (NAUTILUS_COMPILATION)
#warning "Only <nautilus-extension.h> should be included directly."
#endif

#include <glib-object.h>
/* This should be removed at some point. */
#include "nautilus-extension-types.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_COLUMN (nautilus_column_get_type())

G_DECLARE_FINAL_TYPE (NautilusColumn, nautilus_column, NAUTILUS, COLUMN, GObject)

/**
 * SECTION:nautilus-column
 * @title: NautilusColumn
 * @short_description: List view column descriptor object
 *
 * #NautilusColumn is an object that describes a column in the file manager
 * list view. Extensions can provide #NautilusColumn by registering a
 * #NautilusColumnProvider and returning them from
 * nautilus_column_provider_get_columns(), which will be called by the main
 * application when creating a view.
 */

/**
 * nautilus_column_new:
 * @name: (not nullable): identifier of the column
 * @attribute: (not nullable): the file attribute to be displayed in the column
 * @label: (not nullable): the user-visible label for the column
 * @description: (not nullable): a user-visible description of the column
 *
 * Returns: (transfer full): a new #NautilusColumn
 */
NautilusColumn *nautilus_column_new  (const char *name,
                                      const char *attribute,
                                      const char *label,
                                      const char *description);

/* NautilusColumn has the following properties:
 *   name (string)        - the identifier for the column
 *   attribute (string)   - the file attribute to be displayed in the 
 *                          column
 *   label (string)       - the user-visible label for the column
 *   description (string) - a user-visible description of the column
 *   xalign (float)       - x-alignment of the column 
 *   default-sort-order (GtkSortType) - default sort order of the column
 */

G_END_DECLS
