/* fm-list-model.h - a GtkTreeModel for file lists.
 *
 *  Copyright (C) 2001, 2002 Anders Carlsson
 *  Copyright (C) 2003, Soeren Sandmann
 *  Copyright (C) 2004, Novell, Inc.
 *
 *  The Gnome Library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  The Gnome Library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with the Gnome Library; see the file COPYING.LIB.  If not,
 *  see <http://www.gnu.org/licenses/>.
 *
 *  Authors: Anders Carlsson <andersca@gnu.org>, Soeren Sandmann (sandmann@daimi.au.dk), Dave Camp <dave@ximian.com>
 */

#include <config.h>

#include "nautilus-list-model.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <cairo-gobject.h>

#include <eel/eel-graphic-effects.h>
#include "nautilus-dnd.h"

enum
{
    SUBDIRECTORY_UNLOADED,
    GET_ICON_SCALE,
    LAST_SIGNAL
};

static GQuark attribute_name_q,
              attribute_modification_date_q,
              attribute_date_modified_q;

/* msec delay after Loading... dummy row turns into (empty) */
#define LOADING_TO_EMPTY_DELAY 100

static guint list_model_signals[LAST_SIGNAL] = { 0 };

static int nautilus_list_model_file_entry_compare_func (gconstpointer a,
                                                        gconstpointer b,
                                                        gpointer      user_data);
static void nautilus_list_model_tree_model_init (GtkTreeModelIface *iface);
static void nautilus_list_model_sortable_init (GtkTreeSortableIface *iface);

typedef struct
{
    GSequence *files;
    GHashTable *directory_reverse_map;     /* map from directory to GSequenceIter's */
    GHashTable *top_reverse_map;           /* map from files in top dir to GSequenceIter's */

    int stamp;

    GQuark sort_attribute;
    GtkSortType order;

    gboolean sort_directories_first;

    GtkTreeView *drag_view;
    int drag_begin_x;
    int drag_begin_y;

    GPtrArray *columns;

    GList *highlight_files;
} NautilusListModelPrivate;

typedef struct
{
    NautilusListModel *model;

    GList *path_list;
} DragDataGetInfo;

typedef struct FileEntry FileEntry;

struct FileEntry
{
    NautilusFile *file;
    GHashTable *reverse_map;            /* map from files to GSequenceIter's */
    NautilusDirectory *subdirectory;
    FileEntry *parent;
    GSequence *files;
    GSequenceIter *ptr;
    guint loaded : 1;
};

G_DEFINE_TYPE_WITH_CODE (NautilusListModel, nautilus_list_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                nautilus_list_model_tree_model_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_SORTABLE,
                                                nautilus_list_model_sortable_init)
                         G_ADD_PRIVATE (NautilusListModel));

static const GtkTargetEntry drag_types [] =
{
    { NAUTILUS_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NAUTILUS_ICON_DND_GNOME_ICON_LIST },
    { NAUTILUS_ICON_DND_URI_LIST_TYPE, 0, NAUTILUS_ICON_DND_URI_LIST },
};

static void
file_entry_free (FileEntry *file_entry)
{
    nautilus_file_unref (file_entry->file);
    if (file_entry->reverse_map)
    {
        g_hash_table_destroy (file_entry->reverse_map);
        file_entry->reverse_map = NULL;
    }
    if (file_entry->subdirectory != NULL)
    {
        nautilus_directory_unref (file_entry->subdirectory);
    }
    if (file_entry->files != NULL)
    {
        g_sequence_free (file_entry->files);
    }
    g_free (file_entry);
}

static GtkTreeModelFlags
nautilus_list_model_get_flags (GtkTreeModel *tree_model)
{
    return GTK_TREE_MODEL_ITERS_PERSIST;
}

static int
nautilus_list_model_get_n_columns (GtkTreeModel *tree_model)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (NAUTILUS_LIST_MODEL (tree_model));

    return NAUTILUS_LIST_MODEL_NUM_COLUMNS + priv->columns->len;
}

static GType
nautilus_list_model_get_column_type (GtkTreeModel *tree_model,
                                     int           index)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    switch (index)
    {
        case NAUTILUS_LIST_MODEL_FILE_COLUMN:
        {
            return NAUTILUS_TYPE_FILE;
        }

        case NAUTILUS_LIST_MODEL_SUBDIRECTORY_COLUMN:
        {
            return NAUTILUS_TYPE_DIRECTORY;
        }

        case NAUTILUS_LIST_MODEL_SMALL_ICON_COLUMN:
        case NAUTILUS_LIST_MODEL_STANDARD_ICON_COLUMN:
        case NAUTILUS_LIST_MODEL_LARGE_ICON_COLUMN:
        case NAUTILUS_LIST_MODEL_LARGER_ICON_COLUMN:
        {
            return CAIRO_GOBJECT_TYPE_SURFACE;
        }

        case NAUTILUS_LIST_MODEL_FILE_NAME_IS_EDITABLE_COLUMN:
        {
            return G_TYPE_BOOLEAN;
        }

        default:
            if (index < NAUTILUS_LIST_MODEL_NUM_COLUMNS + priv->columns->len)
            {
                return G_TYPE_STRING;
            }
            else
            {
                return G_TYPE_INVALID;
            }
    }
}

static void
nautilus_list_model_ptr_to_iter (NautilusListModel *model,
                                 GSequenceIter     *ptr,
                                 GtkTreeIter       *iter)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (model);

    g_assert (!g_sequence_iter_is_end (ptr));

    if (iter != NULL)
    {
        iter->stamp = priv->stamp;
        iter->user_data = ptr;
    }
}

static gboolean
nautilus_list_model_get_iter (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter,
                              GtkTreePath  *path)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    GSequence *files;
    GSequenceIter *ptr;
    FileEntry *file_entry;
    int i, d;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);
    ptr = NULL;

    files = priv->files;
    for (d = 0; d < gtk_tree_path_get_depth (path); d++)
    {
        i = gtk_tree_path_get_indices (path)[d];

        if (files == NULL || i >= g_sequence_get_length (files))
        {
            return FALSE;
        }

        ptr = g_sequence_get_iter_at_pos (files, i);
        file_entry = g_sequence_get (ptr);
        files = file_entry->files;
    }

    nautilus_list_model_ptr_to_iter (model, ptr, iter);

    return TRUE;
}

static GtkTreePath *
nautilus_list_model_get_path (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter)
{
    GtkTreePath *path;
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    GSequenceIter *ptr;
    FileEntry *file_entry;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    g_return_val_if_fail (iter->stamp == priv->stamp, NULL);

    if (g_sequence_iter_is_end (iter->user_data))
    {
        /* FIXME is this right? */
        return NULL;
    }

    path = gtk_tree_path_new ();
    ptr = iter->user_data;
    while (ptr != NULL)
    {
        gtk_tree_path_prepend_index (path, g_sequence_iter_get_position (ptr));
        file_entry = g_sequence_get (ptr);
        if (file_entry->parent != NULL)
        {
            ptr = file_entry->parent->ptr;
        }
        else
        {
            ptr = NULL;
        }
    }

    return path;
}

static gint
nautilus_list_model_get_icon_scale (NautilusListModel *model)
{
    gint retval = -1;

    g_signal_emit (model, list_model_signals[GET_ICON_SCALE], 0,
                   &retval);

    if (retval == -1)
    {
        retval = gdk_monitor_get_scale_factor (gdk_display_get_monitor (gdk_display_get_default (), 0));
    }

    return retval;
}

guint
nautilus_list_model_get_icon_size_for_zoom_level (NautilusListZoomLevel zoom_level)
{
    switch (zoom_level)
    {
        case NAUTILUS_LIST_ZOOM_LEVEL_SMALL:
        {
            return NAUTILUS_LIST_ICON_SIZE_SMALL;
        }

        case NAUTILUS_LIST_ZOOM_LEVEL_STANDARD:
        {
            return NAUTILUS_LIST_ICON_SIZE_STANDARD;
        }

        case NAUTILUS_LIST_ZOOM_LEVEL_LARGE:
        {
            return NAUTILUS_LIST_ICON_SIZE_LARGE;
        }

        case NAUTILUS_LIST_ZOOM_LEVEL_LARGER:
            return NAUTILUS_LIST_ICON_SIZE_LARGER;
    }
    g_return_val_if_reached (NAUTILUS_LIST_ICON_SIZE_STANDARD);
}

static void
nautilus_list_model_get_value (GtkTreeModel *tree_model,
                               GtkTreeIter  *iter,
                               int           column,
                               GValue       *value)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    FileEntry *file_entry;
    NautilusFile *file;
    char *str;
    GdkPixbuf *icon, *rendered_icon;
    int icon_size, icon_scale;
    NautilusListZoomLevel zoom_level;
    NautilusFileIconFlags flags;
    cairo_surface_t *surface;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    g_return_if_fail (priv->stamp == iter->stamp);
    g_return_if_fail (!g_sequence_iter_is_end (iter->user_data));

    file_entry = g_sequence_get (iter->user_data);
    file = file_entry->file;

    switch (column)
    {
        case NAUTILUS_LIST_MODEL_FILE_COLUMN:
        {
            g_value_init (value, NAUTILUS_TYPE_FILE);

            g_value_set_object (value, file);
        }
        break;

        case NAUTILUS_LIST_MODEL_SUBDIRECTORY_COLUMN:
        {
            g_value_init (value, NAUTILUS_TYPE_DIRECTORY);

            g_value_set_object (value, file_entry->subdirectory);
        }
        break;

        case NAUTILUS_LIST_MODEL_SMALL_ICON_COLUMN:
        case NAUTILUS_LIST_MODEL_STANDARD_ICON_COLUMN:
        case NAUTILUS_LIST_MODEL_LARGE_ICON_COLUMN:
        case NAUTILUS_LIST_MODEL_LARGER_ICON_COLUMN:
        {
            g_value_init (value, CAIRO_GOBJECT_TYPE_SURFACE);

            if (file != NULL)
            {
                zoom_level = nautilus_list_model_get_zoom_level_from_column_id (column);
                icon_size = nautilus_list_model_get_icon_size_for_zoom_level (zoom_level);
                icon_scale = nautilus_list_model_get_icon_scale (model);

                flags = NAUTILUS_FILE_ICON_FLAGS_USE_THUMBNAILS |
                        NAUTILUS_FILE_ICON_FLAGS_FORCE_THUMBNAIL_SIZE |
                        NAUTILUS_FILE_ICON_FLAGS_USE_EMBLEMS |
                        NAUTILUS_FILE_ICON_FLAGS_USE_ONE_EMBLEM;

                if (priv->drag_view != NULL)
                {
                    GtkTreePath *path_a, *path_b;

                    gtk_tree_view_get_drag_dest_row (priv->drag_view,
                                                     &path_a,
                                                     NULL);
                    if (path_a != NULL)
                    {
                        path_b = gtk_tree_model_get_path (tree_model, iter);

                        if (gtk_tree_path_compare (path_a, path_b) == 0)
                        {
                            flags |= NAUTILUS_FILE_ICON_FLAGS_FOR_DRAG_ACCEPT;
                        }

                        gtk_tree_path_free (path_a);
                        gtk_tree_path_free (path_b);
                    }
                }

                icon = nautilus_file_get_icon_pixbuf (file, icon_size, TRUE, icon_scale, flags);

                if (priv->highlight_files != NULL &&
                    g_list_find_custom (priv->highlight_files,
                                        file, (GCompareFunc) nautilus_file_compare_location))
                {
                    rendered_icon = eel_create_spotlight_pixbuf (icon);

                    if (rendered_icon != NULL)
                    {
                        g_object_unref (icon);
                        icon = rendered_icon;
                    }
                }

                surface = gdk_cairo_surface_create_from_pixbuf (icon, icon_scale, NULL);
                g_value_take_boxed (value, surface);
                g_object_unref (icon);
            }
        }
        break;

        case NAUTILUS_LIST_MODEL_FILE_NAME_IS_EDITABLE_COLUMN:
        {
            g_value_init (value, G_TYPE_BOOLEAN);

            g_value_set_boolean (value, file != NULL && nautilus_file_can_rename (file));
        }
        break;

        default:
            if (column >= NAUTILUS_LIST_MODEL_NUM_COLUMNS && column < NAUTILUS_LIST_MODEL_NUM_COLUMNS + priv->columns->len)
            {
                NautilusColumn *nautilus_column;
                GQuark attribute;
                nautilus_column = priv->columns->pdata[column - NAUTILUS_LIST_MODEL_NUM_COLUMNS];

                g_value_init (value, G_TYPE_STRING);
                g_object_get (nautilus_column,
                              "attribute_q", &attribute,
                              NULL);
                if (file != NULL)
                {
                    str = nautilus_file_get_string_attribute_with_default_q (file,
                                                                             attribute);
                    g_value_take_string (value, str);
                }
                else if (attribute == attribute_name_q)
                {
                    if (file_entry->parent->loaded)
                    {
                        g_value_set_string (value, _("(Empty)"));
                    }
                    else
                    {
                        g_value_set_string (value, _("Loading…"));
                    }
                }
            }
            else
            {
                g_assert_not_reached ();
            }
    }
}

static gboolean
nautilus_list_model_iter_next (GtkTreeModel *tree_model,
                               GtkTreeIter  *iter)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    g_return_val_if_fail (priv->stamp == iter->stamp, FALSE);

    iter->user_data = g_sequence_iter_next (iter->user_data);

    return !g_sequence_iter_is_end (iter->user_data);
}

static gboolean
nautilus_list_model_iter_children (GtkTreeModel *tree_model,
                                   GtkTreeIter  *iter,
                                   GtkTreeIter  *parent)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    GSequence *files;
    FileEntry *file_entry;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    if (parent == NULL)
    {
        files = priv->files;
    }
    else
    {
        file_entry = g_sequence_get (parent->user_data);
        files = file_entry->files;
    }

    if (files == NULL || g_sequence_get_length (files) == 0)
    {
        return FALSE;
    }

    iter->stamp = priv->stamp;
    iter->user_data = g_sequence_get_begin_iter (files);

    return TRUE;
}

static gboolean
nautilus_list_model_iter_has_child (GtkTreeModel *tree_model,
                                    GtkTreeIter  *iter)
{
    FileEntry *file_entry;

    if (iter == NULL)
    {
        return !nautilus_list_model_is_empty (NAUTILUS_LIST_MODEL (tree_model));
    }

    file_entry = g_sequence_get (iter->user_data);

    return (file_entry->files != NULL && g_sequence_get_length (file_entry->files) > 0);
}

static int
nautilus_list_model_iter_n_children (GtkTreeModel *tree_model,
                                     GtkTreeIter  *iter)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    GSequence *files;
    FileEntry *file_entry;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    if (iter == NULL)
    {
        files = priv->files;
    }
    else
    {
        file_entry = g_sequence_get (iter->user_data);
        files = file_entry->files;
    }

    return g_sequence_get_length (files);
}

static gboolean
nautilus_list_model_iter_nth_child (GtkTreeModel *tree_model,
                                    GtkTreeIter  *iter,
                                    GtkTreeIter  *parent,
                                    int           n)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    GSequenceIter *child;
    GSequence *files;
    FileEntry *file_entry;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    if (parent != NULL)
    {
        file_entry = g_sequence_get (parent->user_data);
        files = file_entry->files;
    }
    else
    {
        files = priv->files;
    }

    child = g_sequence_get_iter_at_pos (files, n);

    if (g_sequence_iter_is_end (child))
    {
        return FALSE;
    }

    iter->stamp = priv->stamp;
    iter->user_data = child;

    return TRUE;
}

static gboolean
nautilus_list_model_iter_parent (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter,
                                 GtkTreeIter  *child)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    FileEntry *file_entry;

    model = NAUTILUS_LIST_MODEL (tree_model);
    priv = nautilus_list_model_get_instance_private (model);

    file_entry = g_sequence_get (child->user_data);

    if (file_entry->parent == NULL)
    {
        return FALSE;
    }

    iter->stamp = priv->stamp;
    iter->user_data = file_entry->parent->ptr;

    return TRUE;
}

static GSequenceIter *
lookup_file (NautilusListModel *model,
             NautilusFile      *file,
             NautilusDirectory *directory)
{
    NautilusListModelPrivate *priv;
    FileEntry *file_entry;
    GSequenceIter *ptr, *parent_ptr;

    priv = nautilus_list_model_get_instance_private (model);

    parent_ptr = NULL;
    if (directory)
    {
        parent_ptr = g_hash_table_lookup (priv->directory_reverse_map,
                                          directory);
    }

    if (parent_ptr)
    {
        file_entry = g_sequence_get (parent_ptr);
        ptr = g_hash_table_lookup (file_entry->reverse_map, file);
    }
    else
    {
        ptr = g_hash_table_lookup (priv->top_reverse_map, file);
    }

    if (ptr)
    {
        g_assert (((FileEntry *) g_sequence_get (ptr))->file == file);
    }

    return ptr;
}


struct GetIters
{
    NautilusListModel *model;
    NautilusFile *file;
    GList *iters;
};

static void
dir_to_iters (struct GetIters *data,
              GHashTable      *reverse_map)
{
    GSequenceIter *ptr;

    ptr = g_hash_table_lookup (reverse_map, data->file);
    if (ptr)
    {
        GtkTreeIter *iter;
        iter = g_new0 (GtkTreeIter, 1);
        nautilus_list_model_ptr_to_iter (data->model, ptr, iter);
        data->iters = g_list_prepend (data->iters, iter);
    }
}

static void
file_to_iter_cb (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
    struct GetIters *data;
    FileEntry *dir_file_entry;

    data = user_data;
    dir_file_entry = g_sequence_get ((GSequenceIter *) value);
    dir_to_iters (data, dir_file_entry->reverse_map);
}

GList *
nautilus_list_model_get_all_iters_for_file (NautilusListModel *model,
                                            NautilusFile      *file)
{
    struct GetIters data;
    NautilusListModelPrivate *priv;
    data.file = file;
    data.model = model;
    data.iters = NULL;

    priv = nautilus_list_model_get_instance_private (model);

    dir_to_iters (&data, priv->top_reverse_map);
    g_hash_table_foreach (priv->directory_reverse_map,
                          file_to_iter_cb, &data);

    return g_list_reverse (data.iters);
}

gboolean
nautilus_list_model_get_first_iter_for_file (NautilusListModel *model,
                                             NautilusFile      *file,
                                             GtkTreeIter       *iter)
{
    GList *list;
    gboolean res;

    res = FALSE;

    list = nautilus_list_model_get_all_iters_for_file (model, file);
    if (list != NULL)
    {
        res = TRUE;
        *iter = *(GtkTreeIter *) list->data;
    }
    g_list_free_full (list, g_free);

    return res;
}


gboolean
nautilus_list_model_get_tree_iter_from_file (NautilusListModel *model,
                                             NautilusFile      *file,
                                             NautilusDirectory *directory,
                                             GtkTreeIter       *iter)
{
    GSequenceIter *ptr;

    ptr = lookup_file (model, file, directory);
    if (!ptr)
    {
        return FALSE;
    }

    nautilus_list_model_ptr_to_iter (model, ptr, iter);

    return TRUE;
}

static int
nautilus_list_model_file_entry_compare_func (gconstpointer a,
                                             gconstpointer b,
                                             gpointer      user_data)
{
    FileEntry *file_entry1;
    FileEntry *file_entry2;
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    int result;

    model = NAUTILUS_LIST_MODEL (user_data);
    priv = nautilus_list_model_get_instance_private (model);

    file_entry1 = (FileEntry *) a;
    file_entry2 = (FileEntry *) b;

    if (file_entry1->file != NULL && file_entry2->file != NULL)
    {
        result = nautilus_file_compare_for_sort_by_attribute_q (file_entry1->file, file_entry2->file,
                                                                priv->sort_attribute,
                                                                priv->sort_directories_first,
                                                                (priv->order == GTK_SORT_DESCENDING));
    }
    else if (file_entry1->file == NULL)
    {
        return -1;
    }
    else
    {
        return 1;
    }

    return result;
}

int
nautilus_list_model_compare_func (NautilusListModel *model,
                                  NautilusFile      *file1,
                                  NautilusFile      *file2)
{
    NautilusListModelPrivate *priv;
    int result;

    priv = nautilus_list_model_get_instance_private (model);
    result = nautilus_file_compare_for_sort_by_attribute_q (file1, file2,
                                                            priv->sort_attribute,
                                                            priv->sort_directories_first,
                                                            (priv->order == GTK_SORT_DESCENDING));

    return result;
}

static void
nautilus_list_model_sort_file_entries (NautilusListModel *model,
                                       GSequence         *files,
                                       GtkTreePath       *path)
{
    GSequenceIter **old_order;
    GtkTreeIter iter;
    int *new_order;
    int length;
    int i;
    FileEntry *file_entry;
    gboolean has_iter;

    length = g_sequence_get_length (files);

    if (length <= 1)
    {
        return;
    }

    /* generate old order of GSequenceIter's */
    old_order = g_new (GSequenceIter *, length);
    for (i = 0; i < length; ++i)
    {
        GSequenceIter *ptr = g_sequence_get_iter_at_pos (files, i);

        file_entry = g_sequence_get (ptr);
        if (file_entry->files != NULL)
        {
            gtk_tree_path_append_index (path, i);
            nautilus_list_model_sort_file_entries (model, file_entry->files, path);
            gtk_tree_path_up (path);
        }

        old_order[i] = ptr;
    }

    /* sort */
    g_sequence_sort (files, nautilus_list_model_file_entry_compare_func, model);

    /* generate new order */
    new_order = g_new (int, length);
    /* Note: new_order[newpos] = oldpos */
    for (i = 0; i < length; ++i)
    {
        new_order[g_sequence_iter_get_position (old_order[i])] = i;
    }

    /* Let the world know about our new order */

    g_assert (new_order != NULL);

    has_iter = FALSE;
    if (gtk_tree_path_get_depth (path) != 0)
    {
        gboolean get_iter_result;
        has_iter = TRUE;
        get_iter_result = gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);
        g_assert (get_iter_result);
    }

    gtk_tree_model_rows_reordered (GTK_TREE_MODEL (model),
                                   path, has_iter ? &iter : NULL, new_order);

    g_free (old_order);
    g_free (new_order);
}

static void
nautilus_list_model_sort (NautilusListModel *model)
{
    GtkTreePath *path;
    NautilusListModelPrivate *priv;

    path = gtk_tree_path_new ();
    priv = nautilus_list_model_get_instance_private (model);

    nautilus_list_model_sort_file_entries (model, priv->files, path);

    gtk_tree_path_free (path);
}

static gboolean
nautilus_list_model_get_sort_column_id (GtkTreeSortable *sortable,
                                        gint            *sort_column_id,
                                        GtkSortType     *order)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    int id;

    model = NAUTILUS_LIST_MODEL (sortable);
    priv = nautilus_list_model_get_instance_private (model);
    id = nautilus_list_model_get_sort_column_id_from_attribute (model, priv->sort_attribute);

    if (id == -1)
    {
        return FALSE;
    }

    if (sort_column_id != NULL)
    {
        *sort_column_id = id;
    }

    if (order != NULL)
    {
        *order = priv->order;
    }

    return TRUE;
}

static void
nautilus_list_model_set_sort_column_id (GtkTreeSortable *sortable,
                                        gint             sort_column_id,
                                        GtkSortType      order)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;

    model = NAUTILUS_LIST_MODEL (sortable);
    priv = nautilus_list_model_get_instance_private (model);

    priv->sort_attribute = nautilus_list_model_get_attribute_from_sort_column_id (model, sort_column_id);

    priv->order = order;

    nautilus_list_model_sort (model);
    gtk_tree_sortable_sort_column_changed (sortable);
}

static gboolean
nautilus_list_model_has_default_sort_func (GtkTreeSortable *sortable)
{
    return FALSE;
}

static void
add_dummy_row (NautilusListModel *model,
               FileEntry         *parent_entry)
{
    NautilusListModelPrivate *priv;
    FileEntry *dummy_file_entry;
    GtkTreeIter iter;
    GtkTreePath *path;

    priv = nautilus_list_model_get_instance_private (model);
    dummy_file_entry = g_new0 (FileEntry, 1);
    dummy_file_entry->parent = parent_entry;
    dummy_file_entry->ptr = g_sequence_insert_sorted (parent_entry->files, dummy_file_entry,
                                                      nautilus_list_model_file_entry_compare_func, model);
    iter.stamp = priv->stamp;
    iter.user_data = dummy_file_entry->ptr;

    path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
    gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, &iter);
    gtk_tree_path_free (path);
}

gboolean
nautilus_list_model_add_file (NautilusListModel *model,
                              NautilusFile      *file,
                              NautilusDirectory *directory)
{
    NautilusListModelPrivate *priv;
    GtkTreeIter iter;
    GtkTreePath *path;
    FileEntry *file_entry;
    GSequenceIter *ptr, *parent_ptr;
    GSequence *files;
    gboolean replace_dummy;
    GHashTable *parent_hash;

    priv = nautilus_list_model_get_instance_private (model);

    parent_ptr = g_hash_table_lookup (priv->directory_reverse_map,
                                      directory);
    if (parent_ptr)
    {
        file_entry = g_sequence_get (parent_ptr);
        ptr = g_hash_table_lookup (file_entry->reverse_map, file);
    }
    else
    {
        file_entry = NULL;
        ptr = g_hash_table_lookup (priv->top_reverse_map, file);
    }

    if (ptr != NULL)
    {
        g_warning ("file already in tree (parent_ptr: %p)!!!\n", parent_ptr);
        return FALSE;
    }

    file_entry = g_new0 (FileEntry, 1);
    file_entry->file = nautilus_file_ref (file);
    file_entry->parent = NULL;
    file_entry->subdirectory = NULL;
    file_entry->files = NULL;

    files = priv->files;
    parent_hash = priv->top_reverse_map;

    replace_dummy = FALSE;

    if (parent_ptr != NULL)
    {
        file_entry->parent = g_sequence_get (parent_ptr);
        /* At this point we set loaded. Either we saw
         * "done" and ignored it waiting for this, or we do this
         * earlier, but then we replace the dummy row anyway,
         * so it doesn't matter */
        file_entry->parent->loaded = 1;
        parent_hash = file_entry->parent->reverse_map;
        files = file_entry->parent->files;
        if (g_sequence_get_length (files) == 1)
        {
            GSequenceIter *dummy_ptr = g_sequence_get_iter_at_pos (files, 0);
            FileEntry *dummy_entry = g_sequence_get (dummy_ptr);
            if (dummy_entry->file == NULL)
            {
                /* replace the dummy loading entry */
                priv->stamp++;
                g_sequence_remove (dummy_ptr);

                replace_dummy = TRUE;
            }
        }
    }


    file_entry->ptr = g_sequence_insert_sorted (files, file_entry,
                                                nautilus_list_model_file_entry_compare_func, model);

    g_hash_table_insert (parent_hash, file, file_entry->ptr);

    iter.stamp = priv->stamp;
    iter.user_data = file_entry->ptr;

    path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
    if (replace_dummy)
    {
        gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
    }
    else
    {
        gtk_tree_model_row_inserted (GTK_TREE_MODEL (model), path, &iter);
    }

    if (nautilus_file_is_directory (file))
    {
        file_entry->files = g_sequence_new ((GDestroyNotify) file_entry_free);

        add_dummy_row (model, file_entry);

        gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (model),
                                              path, &iter);
    }
    gtk_tree_path_free (path);

    return TRUE;
}

void
nautilus_list_model_file_changed (NautilusListModel *model,
                                  NautilusFile      *file,
                                  NautilusDirectory *directory)
{
    NautilusListModelPrivate *priv;
    FileEntry *parent_file_entry;
    GtkTreeIter iter;
    GtkTreePath *path, *parent_path;
    GSequenceIter *ptr;
    int pos_before, pos_after, length, i, old;
    int *new_order;
    gboolean has_iter;
    GSequence *files;

    priv = nautilus_list_model_get_instance_private (model);

    ptr = lookup_file (model, file, directory);
    if (!ptr)
    {
        return;
    }


    pos_before = g_sequence_iter_get_position (ptr);

    g_sequence_sort_changed (ptr, nautilus_list_model_file_entry_compare_func, model);

    pos_after = g_sequence_iter_get_position (ptr);

    if (pos_before != pos_after)
    {
        /* The file moved, we need to send rows_reordered */

        parent_file_entry = ((FileEntry *) g_sequence_get (ptr))->parent;

        if (parent_file_entry == NULL)
        {
            has_iter = FALSE;
            parent_path = gtk_tree_path_new ();
            files = priv->files;
        }
        else
        {
            has_iter = TRUE;
            nautilus_list_model_ptr_to_iter (model, parent_file_entry->ptr, &iter);
            parent_path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
            files = parent_file_entry->files;
        }

        length = g_sequence_get_length (files);
        new_order = g_new (int, length);
        /* Note: new_order[newpos] = oldpos */
        for (i = 0, old = 0; i < length; ++i)
        {
            if (i == pos_after)
            {
                new_order[i] = pos_before;
            }
            else
            {
                if (old == pos_before)
                {
                    old++;
                }
                new_order[i] = old++;
            }
        }

        gtk_tree_model_rows_reordered (GTK_TREE_MODEL (model),
                                       parent_path, has_iter ? &iter : NULL, new_order);

        gtk_tree_path_free (parent_path);
        g_free (new_order);
    }

    nautilus_list_model_ptr_to_iter (model, ptr, &iter);
    path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
    gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
    gtk_tree_path_free (path);
}

gboolean
nautilus_list_model_is_empty (NautilusListModel *model)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (model);

    return (g_sequence_get_length (priv->files) == 0);
}

static void
nautilus_list_model_remove (NautilusListModel *model,
                            GtkTreeIter       *iter)
{
    NautilusListModelPrivate *priv;
    GSequenceIter *ptr, *child_ptr;
    FileEntry *file_entry, *child_file_entry, *parent_file_entry;
    GtkTreePath *path;
    GtkTreeIter parent_iter;

    priv = nautilus_list_model_get_instance_private (model);
    ptr = iter->user_data;
    file_entry = g_sequence_get (ptr);

    if (file_entry->files != NULL)
    {
        while (g_sequence_get_length (file_entry->files) > 0)
        {
            child_ptr = g_sequence_get_begin_iter (file_entry->files);
            child_file_entry = g_sequence_get (child_ptr);
            if (child_file_entry->file != NULL)
            {
                nautilus_list_model_remove_file (model,
                                                 child_file_entry->file,
                                                 file_entry->subdirectory);
            }
            else
            {
                path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), iter);
                gtk_tree_path_append_index (path, 0);
                priv->stamp++;
                g_sequence_remove (child_ptr);
                gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);
                gtk_tree_path_free (path);
            }

            /* the parent iter didn't actually change */
            iter->stamp = priv->stamp;
        }
    }

    if (file_entry->file != NULL)       /* Don't try to remove dummy row */
    {
        if (file_entry->parent != NULL)
        {
            g_hash_table_remove (file_entry->parent->reverse_map, file_entry->file);
        }
        else
        {
            g_hash_table_remove (priv->top_reverse_map, file_entry->file);
        }
    }

    parent_file_entry = file_entry->parent;
    if (parent_file_entry && g_sequence_get_length (parent_file_entry->files) == 1 &&
        file_entry->file != NULL)
    {
        /* this is the last non-dummy child, add a dummy node */
        /* We need to do this before removing the last file to avoid
         * collapsing the row.
         */
        add_dummy_row (model, parent_file_entry);
    }

    if (file_entry->subdirectory != NULL)
    {
        g_signal_emit (model,
                       list_model_signals[SUBDIRECTORY_UNLOADED], 0,
                       file_entry->subdirectory);
        g_hash_table_remove (priv->directory_reverse_map,
                             file_entry->subdirectory);
    }

    path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), iter);

    g_sequence_remove (ptr);
    priv->stamp++;
    gtk_tree_model_row_deleted (GTK_TREE_MODEL (model), path);

    gtk_tree_path_free (path);

    if (parent_file_entry && g_sequence_get_length (parent_file_entry->files) == 0)
    {
        parent_iter.stamp = priv->stamp;
        parent_iter.user_data = parent_file_entry->ptr;
        path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &parent_iter);
        gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (model),
                                              path, &parent_iter);
        gtk_tree_path_free (path);
    }
}

void
nautilus_list_model_remove_file (NautilusListModel *model,
                                 NautilusFile      *file,
                                 NautilusDirectory *directory)
{
    GtkTreeIter iter;

    if (nautilus_list_model_get_tree_iter_from_file (model, file, directory, &iter))
    {
        nautilus_list_model_remove (model, &iter);
    }
}

static void
nautilus_list_model_clear_directory (NautilusListModel *model,
                                     GSequence         *files)
{
    NautilusListModelPrivate *priv;
    GtkTreeIter iter;
    FileEntry *file_entry;

    priv = nautilus_list_model_get_instance_private (model);

    while (g_sequence_get_length (files) > 0)
    {
        iter.user_data = g_sequence_get_begin_iter (files);

        file_entry = g_sequence_get (iter.user_data);
        if (file_entry->files != NULL)
        {
            nautilus_list_model_clear_directory (model, file_entry->files);
        }

        iter.stamp = priv->stamp;
        nautilus_list_model_remove (model, &iter);
    }
}

void
nautilus_list_model_clear (NautilusListModel *model)
{
    NautilusListModelPrivate *priv;

    g_return_if_fail (model != NULL);

    priv = nautilus_list_model_get_instance_private (model);

    nautilus_list_model_clear_directory (model, priv->files);
}

NautilusFile *
nautilus_list_model_file_for_path (NautilusListModel *model,
                                   GtkTreePath       *path)
{
    NautilusFile *file;
    GtkTreeIter iter;

    file = NULL;
    if (gtk_tree_model_get_iter (GTK_TREE_MODEL (model),
                                 &iter, path))
    {
        gtk_tree_model_get (GTK_TREE_MODEL (model),
                            &iter,
                            NAUTILUS_LIST_MODEL_FILE_COLUMN, &file,
                            -1);
    }
    return file;
}

gboolean
nautilus_list_model_load_subdirectory (NautilusListModel  *model,
                                       GtkTreePath        *path,
                                       NautilusDirectory **directory)
{
    NautilusListModelPrivate *priv;
    GtkTreeIter iter;
    FileEntry *file_entry;
    NautilusDirectory *subdirectory;

    priv = nautilus_list_model_get_instance_private (model);

    if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path))
    {
        return FALSE;
    }

    file_entry = g_sequence_get (iter.user_data);
    if (file_entry->file == NULL ||
        file_entry->subdirectory != NULL)
    {
        return FALSE;
    }

    subdirectory = nautilus_directory_get_for_file (file_entry->file);

    if (g_hash_table_lookup (priv->directory_reverse_map, subdirectory) != NULL)
    {
        nautilus_directory_unref (subdirectory);
        g_warning ("Already in directory_reverse_map, failing\n");
        return FALSE;
    }

    file_entry->subdirectory = subdirectory,
    g_hash_table_insert (priv->directory_reverse_map,
                         subdirectory, file_entry->ptr);
    file_entry->reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);

    /* Return a ref too */
    nautilus_directory_ref (subdirectory);
    *directory = subdirectory;

    return TRUE;
}

/* removes all children of the subfolder and unloads the subdirectory */
void
nautilus_list_model_unload_subdirectory (NautilusListModel *model,
                                         GtkTreeIter       *iter)
{
    NautilusListModelPrivate *priv;
    GSequenceIter *child_ptr;
    FileEntry *file_entry, *child_file_entry;
    GtkTreeIter child_iter;

    priv = nautilus_list_model_get_instance_private (model);

    file_entry = g_sequence_get (iter->user_data);
    if (file_entry->file == NULL ||
        file_entry->subdirectory == NULL)
    {
        return;
    }

    file_entry->loaded = 0;

    /* Remove all children */
    while (g_sequence_get_length (file_entry->files) > 0)
    {
        child_ptr = g_sequence_get_begin_iter (file_entry->files);
        child_file_entry = g_sequence_get (child_ptr);
        if (child_file_entry->file == NULL)
        {
            /* Don't delete the dummy node */
            break;
        }
        else
        {
            nautilus_list_model_ptr_to_iter (model, child_ptr, &child_iter);
            nautilus_list_model_remove (model, &child_iter);
        }
    }

    /* Emit unload signal */
    g_signal_emit (model,
                   list_model_signals[SUBDIRECTORY_UNLOADED], 0,
                   file_entry->subdirectory);

    /* actually unload */
    g_hash_table_remove (priv->directory_reverse_map,
                         file_entry->subdirectory);
    nautilus_directory_unref (file_entry->subdirectory);
    file_entry->subdirectory = NULL;

    g_assert (g_hash_table_size (file_entry->reverse_map) == 0);
    g_hash_table_destroy (file_entry->reverse_map);
    file_entry->reverse_map = NULL;
}



void
nautilus_list_model_set_should_sort_directories_first (NautilusListModel *model,
                                                       gboolean           sort_directories_first)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (model);

    if (priv->sort_directories_first == sort_directories_first)
    {
        return;
    }

    priv->sort_directories_first = sort_directories_first;
    nautilus_list_model_sort (model);
}

int
nautilus_list_model_get_sort_column_id_from_attribute (NautilusListModel *model,
                                                       GQuark             attribute)
{
    NautilusListModelPrivate *priv;
    guint i;

    if (attribute == 0)
    {
        return -1;
    }

    priv = nautilus_list_model_get_instance_private (model);

    /* Hack - the preferences dialog sets modification_date for some
     * rather than date_modified for some reason.  Make sure that
     * works. */
    if (attribute == attribute_modification_date_q)
    {
        attribute = attribute_date_modified_q;
    }

    for (i = 0; i < priv->columns->len; i++)
    {
        NautilusColumn *column;
        GQuark column_attribute;

        column = NAUTILUS_COLUMN (priv->columns->pdata[i]);
        g_object_get (G_OBJECT (column),
                      "attribute_q", &column_attribute,
                      NULL);
        if (column_attribute == attribute)
        {
            return NAUTILUS_LIST_MODEL_NUM_COLUMNS + i;
        }
    }

    return -1;
}

GQuark
nautilus_list_model_get_attribute_from_sort_column_id (NautilusListModel *model,
                                                       int                sort_column_id)
{
    NautilusListModelPrivate *priv;
    NautilusColumn *column;
    int index;
    GQuark attribute;

    priv = nautilus_list_model_get_instance_private (model);
    index = sort_column_id - NAUTILUS_LIST_MODEL_NUM_COLUMNS;

    if (index < 0 || index >= priv->columns->len)
    {
        g_warning ("unknown sort column id: %d", sort_column_id);
        return 0;
    }

    column = NAUTILUS_COLUMN (priv->columns->pdata[index]);
    g_object_get (G_OBJECT (column), "attribute_q", &attribute, NULL);

    return attribute;
}

NautilusListZoomLevel
nautilus_list_model_get_zoom_level_from_column_id (int column)
{
    switch (column)
    {
        case NAUTILUS_LIST_MODEL_SMALL_ICON_COLUMN:
        {
            return NAUTILUS_LIST_ZOOM_LEVEL_SMALL;
        }

        case NAUTILUS_LIST_MODEL_STANDARD_ICON_COLUMN:
        {
            return NAUTILUS_LIST_ZOOM_LEVEL_STANDARD;
        }

        case NAUTILUS_LIST_MODEL_LARGE_ICON_COLUMN:
        {
            return NAUTILUS_LIST_ZOOM_LEVEL_LARGE;
        }

        case NAUTILUS_LIST_MODEL_LARGER_ICON_COLUMN:
            return NAUTILUS_LIST_ZOOM_LEVEL_LARGER;
    }

    g_return_val_if_reached (NAUTILUS_LIST_ZOOM_LEVEL_STANDARD);
}

int
nautilus_list_model_get_column_id_from_zoom_level (NautilusListZoomLevel zoom_level)
{
    switch (zoom_level)
    {
        case NAUTILUS_LIST_ZOOM_LEVEL_SMALL:
        {
            return NAUTILUS_LIST_MODEL_SMALL_ICON_COLUMN;
        }

        case NAUTILUS_LIST_ZOOM_LEVEL_STANDARD:
        {
            return NAUTILUS_LIST_MODEL_STANDARD_ICON_COLUMN;
        }

        case NAUTILUS_LIST_ZOOM_LEVEL_LARGE:
        {
            return NAUTILUS_LIST_MODEL_LARGE_ICON_COLUMN;
        }

        case NAUTILUS_LIST_ZOOM_LEVEL_LARGER:
            return NAUTILUS_LIST_MODEL_LARGER_ICON_COLUMN;
    }

    g_return_val_if_reached (NAUTILUS_LIST_MODEL_STANDARD_ICON_COLUMN);
}

void
nautilus_list_model_set_drag_view (NautilusListModel *model,
                                   GtkTreeView       *view,
                                   int                drag_begin_x,
                                   int                drag_begin_y)
{
    NautilusListModelPrivate *priv;

    g_return_if_fail (model != NULL);
    g_return_if_fail (NAUTILUS_IS_LIST_MODEL (model));
    g_return_if_fail (!view || GTK_IS_TREE_VIEW (view));

    priv = nautilus_list_model_get_instance_private (model);

    priv->drag_view = view;
    priv->drag_begin_x = drag_begin_x;
    priv->drag_begin_y = drag_begin_y;
}

GtkTreeView *
nautilus_list_model_get_drag_view (NautilusListModel *model,
                                   int               *drag_begin_x,
                                   int               *drag_begin_y)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (model);

    if (drag_begin_x != NULL)
    {
        *drag_begin_x = priv->drag_begin_x;
    }

    if (drag_begin_y != NULL)
    {
        *drag_begin_y = priv->drag_begin_y;
    }

    return priv->drag_view;
}

GtkTargetList *
nautilus_list_model_get_drag_target_list ()
{
    GtkTargetList *target_list;

    target_list = gtk_target_list_new (drag_types, G_N_ELEMENTS (drag_types));
    gtk_target_list_add_text_targets (target_list, NAUTILUS_ICON_DND_TEXT);

    return target_list;
}

int
nautilus_list_model_add_column (NautilusListModel *model,
                                NautilusColumn    *column)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (model);

    g_ptr_array_add (priv->columns, column);
    g_object_ref (column);

    return NAUTILUS_LIST_MODEL_NUM_COLUMNS + (priv->columns->len - 1);
}

static void
nautilus_list_model_dispose (GObject *object)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;
    int i;

    model = NAUTILUS_LIST_MODEL (object);
    priv = nautilus_list_model_get_instance_private (model);

    if (priv->columns)
    {
        for (i = 0; i < priv->columns->len; i++)
        {
            g_object_unref (priv->columns->pdata[i]);
        }
        g_ptr_array_free (priv->columns, TRUE);
        priv->columns = NULL;
    }

    if (priv->files)
    {
        g_sequence_free (priv->files);
        priv->files = NULL;
    }

    if (priv->top_reverse_map)
    {
        g_hash_table_destroy (priv->top_reverse_map);
        priv->top_reverse_map = NULL;
    }
    if (priv->directory_reverse_map)
    {
        g_hash_table_destroy (priv->directory_reverse_map);
        priv->directory_reverse_map = NULL;
    }

    G_OBJECT_CLASS (nautilus_list_model_parent_class)->dispose (object);
}

static void
nautilus_list_model_finalize (GObject *object)
{
    NautilusListModel *model;
    NautilusListModelPrivate *priv;

    model = NAUTILUS_LIST_MODEL (object);
    priv = nautilus_list_model_get_instance_private (model);

    if (priv->highlight_files != NULL)
    {
        nautilus_file_list_free (priv->highlight_files);
        priv->highlight_files = NULL;
    }

    G_OBJECT_CLASS (nautilus_list_model_parent_class)->finalize (object);
}

static void
nautilus_list_model_init (NautilusListModel *model)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (model);

    priv->files = g_sequence_new ((GDestroyNotify) file_entry_free);
    priv->top_reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);
    priv->directory_reverse_map = g_hash_table_new (g_direct_hash, g_direct_equal);
    priv->stamp = g_random_int ();
    priv->sort_attribute = 0;
    priv->columns = g_ptr_array_new ();
}

static void
nautilus_list_model_class_init (NautilusListModelClass *klass)
{
    GObjectClass *object_class;

    attribute_name_q = g_quark_from_static_string ("name");
    attribute_modification_date_q = g_quark_from_static_string ("modification_date");
    attribute_date_modified_q = g_quark_from_static_string ("date_modified");

    object_class = (GObjectClass *) klass;
    object_class->finalize = nautilus_list_model_finalize;
    object_class->dispose = nautilus_list_model_dispose;

    list_model_signals[SUBDIRECTORY_UNLOADED] =
        g_signal_new ("subdirectory-unloaded",
                      NAUTILUS_TYPE_LIST_MODEL,
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (NautilusListModelClass, subdirectory_unloaded),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      NAUTILUS_TYPE_DIRECTORY);

    list_model_signals[GET_ICON_SCALE] =
        g_signal_new ("get-icon-scale",
                      NAUTILUS_TYPE_LIST_MODEL,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                      0, NULL, NULL,
                      NULL,
                      G_TYPE_INT, 0);
}

static void
nautilus_list_model_tree_model_init (GtkTreeModelIface *iface)
{
    iface->get_flags = nautilus_list_model_get_flags;
    iface->get_n_columns = nautilus_list_model_get_n_columns;
    iface->get_column_type = nautilus_list_model_get_column_type;
    iface->get_iter = nautilus_list_model_get_iter;
    iface->get_path = nautilus_list_model_get_path;
    iface->get_value = nautilus_list_model_get_value;
    iface->iter_next = nautilus_list_model_iter_next;
    iface->iter_children = nautilus_list_model_iter_children;
    iface->iter_has_child = nautilus_list_model_iter_has_child;
    iface->iter_n_children = nautilus_list_model_iter_n_children;
    iface->iter_nth_child = nautilus_list_model_iter_nth_child;
    iface->iter_parent = nautilus_list_model_iter_parent;
}

static void
nautilus_list_model_sortable_init (GtkTreeSortableIface *iface)
{
    iface->get_sort_column_id = nautilus_list_model_get_sort_column_id;
    iface->set_sort_column_id = nautilus_list_model_set_sort_column_id;
    iface->has_default_sort_func = nautilus_list_model_has_default_sort_func;
}

void
nautilus_list_model_subdirectory_done_loading (NautilusListModel *model,
                                               NautilusDirectory *directory)
{
    NautilusListModelPrivate *priv;
    GtkTreeIter iter;
    GtkTreePath *path;
    FileEntry *file_entry, *dummy_entry;
    GSequenceIter *parent_ptr, *dummy_ptr;
    GSequence *files;

    priv = nautilus_list_model_get_instance_private (model);

    if (model == NULL || priv->directory_reverse_map == NULL)
    {
        return;
    }
    parent_ptr = g_hash_table_lookup (priv->directory_reverse_map,
                                      directory);
    if (parent_ptr == NULL)
    {
        return;
    }

    file_entry = g_sequence_get (parent_ptr);
    files = file_entry->files;

    /* Only swap loading -> empty if we saw no files yet at "done",
     * otherwise, toggle loading at first added file to the model.
     */
    if (!nautilus_directory_is_not_empty (directory) &&
        g_sequence_get_length (files) == 1)
    {
        dummy_ptr = g_sequence_get_iter_at_pos (file_entry->files, 0);
        dummy_entry = g_sequence_get (dummy_ptr);
        if (dummy_entry->file == NULL)
        {
            /* was the dummy file */
            file_entry->loaded = 1;

            iter.stamp = priv->stamp;
            iter.user_data = dummy_ptr;

            path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), &iter);
            gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, &iter);
            gtk_tree_path_free (path);
        }
    }
}

static void
refresh_row (gpointer data,
             gpointer user_data)
{
    NautilusFile *file;
    NautilusListModel *model;
    GList *iters, *l;
    GtkTreePath *path;

    model = user_data;
    file = data;

    iters = nautilus_list_model_get_all_iters_for_file (model, file);
    for (l = iters; l != NULL; l = l->next)
    {
        path = gtk_tree_model_get_path (GTK_TREE_MODEL (model), l->data);
        gtk_tree_model_row_changed (GTK_TREE_MODEL (model), path, l->data);

        gtk_tree_path_free (path);
    }

    g_list_free_full (iters, g_free);
}

void
nautilus_list_model_set_highlight_for_files (NautilusListModel *model,
                                             GList             *files)
{
    NautilusListModelPrivate *priv;

    priv = nautilus_list_model_get_instance_private (model);

    if (priv->highlight_files != NULL)
    {
        g_list_foreach (priv->highlight_files, refresh_row, model);
        nautilus_file_list_free (priv->highlight_files);
        priv->highlight_files = NULL;
    }

    if (files != NULL)
    {
        priv->highlight_files = nautilus_file_list_copy (files);
        g_list_foreach (priv->highlight_files, refresh_row, model);
    }
}
