/*
 * nautilus-window-slot-dnd.c - Handle DnD for widgets acting as
 * NautilusWindowSlot proxies
 *
 * Copyright (C) 2000, 2001 Eazel, Inc.
 * Copyright (C) 2010, Red Hat, Inc.
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * see <http://www.gnu.org/licenses/>.
 *
 * Authors: Pavel Cisler <pavel@eazel.com>,
 *      Ettore Perazzoli <ettore@gnu.org>
 */

#include <config.h>

#include "nautilus-notebook.h"
#include "nautilus-application.h"
#include "nautilus-files-view-dnd.h"
#include "nautilus-window-slot-dnd.h"

typedef struct
{
    gboolean have_data;
    gboolean have_valid_data;

    gboolean drop_occurred;

    unsigned int info;
    union
    {
        GList *selection_list;
        GList *uri_list;
        GtkSelectionData *selection_data;
    } data;

    NautilusFile *target_file;
    NautilusWindowSlot *target_slot;
    GtkWidget *widget;

    gboolean is_notebook;
    guint switch_location_timer;
} NautilusDragSlotProxyInfo;

static void
switch_tab (NautilusDragSlotProxyInfo *drag_info)
{
    GtkWidget *notebook, *slot;
    gint idx, n_pages;

    if (drag_info->target_slot == NULL)
    {
        return;
    }

    notebook = gtk_widget_get_ancestor (GTK_WIDGET (drag_info->target_slot), NAUTILUS_TYPE_NOTEBOOK);
    n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));

    for (idx = 0; idx < n_pages; idx++)
    {
        slot = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), idx);
        if (NAUTILUS_WINDOW_SLOT (slot) == drag_info->target_slot)
        {
            gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), idx);
            break;
        }
    }
}

static void
switch_location (NautilusDragSlotProxyInfo *drag_info)
{
    GFile *location;
    GtkWidget *window;

    if (drag_info->target_file == NULL)
    {
        return;
    }

    window = gtk_widget_get_toplevel (drag_info->widget);
    g_assert (NAUTILUS_IS_WINDOW (window));

    location = nautilus_file_get_location (drag_info->target_file);
    nautilus_application_open_location_full (NAUTILUS_APPLICATION (g_application_get_default ()),
                                             location, NAUTILUS_WINDOW_OPEN_FLAG_DONT_MAKE_ACTIVE,
                                             NULL, NAUTILUS_WINDOW (window), NULL);
    g_object_unref (location);
}

static gboolean
slot_proxy_switch_location_timer (gpointer user_data)
{
    NautilusDragSlotProxyInfo *drag_info = user_data;

    drag_info->switch_location_timer = 0;

    if (drag_info->is_notebook)
    {
        switch_tab (drag_info);
    }
    else
    {
        switch_location (drag_info);
    }

    return FALSE;
}

static void
slot_proxy_check_switch_location_timer (NautilusDragSlotProxyInfo *drag_info,
                                        GtkWidget                 *widget)
{
    GtkSettings *settings;
    guint timeout;

    if (drag_info->switch_location_timer)
    {
        return;
    }

    settings = gtk_widget_get_settings (widget);
    g_object_get (settings, "gtk-timeout-expand", &timeout, NULL);

    drag_info->switch_location_timer =
        gdk_threads_add_timeout (timeout,
                                 slot_proxy_switch_location_timer,
                                 drag_info);
}

static void
slot_proxy_remove_switch_location_timer (NautilusDragSlotProxyInfo *drag_info)
{
    if (drag_info->switch_location_timer != 0)
    {
        g_source_remove (drag_info->switch_location_timer);
        drag_info->switch_location_timer = 0;
    }
}

static gboolean
slot_proxy_drag_motion (GtkWidget      *widget,
                        GdkDragContext *context,
                        int             x,
                        int             y,
                        unsigned int    time,
                        gpointer        user_data)
{
    NautilusDragSlotProxyInfo *drag_info;
    NautilusWindowSlot *target_slot;
    GtkWidget *window;
    GdkAtom target;
    int action;
    char *target_uri;
    GFile *location;
    gboolean valid_text_drag;
    gboolean valid_xds_drag;

    drag_info = user_data;

    action = 0;
    valid_text_drag = FALSE;
    valid_xds_drag = FALSE;

    if (gtk_drag_get_source_widget (context) == widget)
    {
        goto out;
    }

    window = gtk_widget_get_toplevel (widget);
    g_assert (NAUTILUS_IS_WINDOW (window));

    if (!drag_info->have_data)
    {
        target = gtk_drag_dest_find_target (widget, context, NULL);

        if (target == GDK_NONE)
        {
            goto out;
        }

        gtk_drag_get_data (widget, context, target, time);
    }

    target_uri = NULL;
    if (drag_info->target_file != NULL)
    {
        target_uri = nautilus_file_get_uri (drag_info->target_file);
    }
    else
    {
        if (drag_info->target_slot != NULL)
        {
            target_slot = drag_info->target_slot;
        }
        else
        {
            target_slot = nautilus_window_get_active_slot (NAUTILUS_WINDOW (window));
        }

        if (target_slot != NULL)
        {
            location = nautilus_window_slot_get_location (target_slot);
            target_uri = g_file_get_uri (location);
        }
    }

    if (target_uri != NULL)
    {
        NautilusFile *file;
        NautilusDirectory *directory;
        gboolean can;
        file = nautilus_file_get_existing_by_uri (target_uri);
        directory = nautilus_directory_get_for_file (file);
        can = nautilus_file_can_write (file) && nautilus_directory_is_editable (directory);
        nautilus_directory_unref (directory);
        g_object_unref (file);
        if (!can)
        {
            action = 0;
            goto out;
        }
    }

    if (drag_info->have_data &&
        drag_info->have_valid_data)
    {
        if (drag_info->info == NAUTILUS_ICON_DND_GNOME_ICON_LIST)
        {
            nautilus_drag_default_drop_action_for_icons (context, target_uri,
                                                         drag_info->data.selection_list,
                                                         0,
                                                         &action);
        }
        else if (drag_info->info == NAUTILUS_ICON_DND_URI_LIST)
        {
            action = nautilus_drag_default_drop_action_for_uri_list (context, target_uri);
        }
        else if (drag_info->info == NAUTILUS_ICON_DND_TEXT)
        {
            valid_text_drag = TRUE;
        }
        else if (drag_info->info == NAUTILUS_ICON_DND_XDNDDIRECTSAVE ||
                 drag_info->info == NAUTILUS_ICON_DND_RAW)
        {
            valid_xds_drag = TRUE;
        }
    }

    g_free (target_uri);

out:
    if (action != 0 || valid_text_drag || valid_xds_drag)
    {
        gtk_drag_highlight (widget);
        slot_proxy_check_switch_location_timer (drag_info, widget);
    }
    else
    {
        gtk_drag_unhighlight (widget);
        slot_proxy_remove_switch_location_timer (drag_info);
    }

    gdk_drag_status (context, action, time);

    return TRUE;
}

static void
drag_info_free (gpointer user_data)
{
    NautilusDragSlotProxyInfo *drag_info = user_data;

    g_clear_object (&drag_info->target_file);
    g_clear_object (&drag_info->target_slot);

    g_slice_free (NautilusDragSlotProxyInfo, drag_info);
}

static void
drag_info_clear (NautilusDragSlotProxyInfo *drag_info)
{
    slot_proxy_remove_switch_location_timer (drag_info);

    if (!drag_info->have_data)
    {
        goto out;
    }

    if (drag_info->info == NAUTILUS_ICON_DND_GNOME_ICON_LIST)
    {
        nautilus_drag_destroy_selection_list (drag_info->data.selection_list);
    }
    else if (drag_info->info == NAUTILUS_ICON_DND_URI_LIST)
    {
        g_list_free (drag_info->data.uri_list);
    }
    else if (drag_info->info == NAUTILUS_ICON_DND_TEXT ||
             drag_info->info == NAUTILUS_ICON_DND_XDNDDIRECTSAVE ||
             drag_info->info == NAUTILUS_ICON_DND_RAW)
    {
        if (drag_info->data.selection_data != NULL)
        {
            gtk_selection_data_free (drag_info->data.selection_data);
        }
    }

out:
    drag_info->have_data = FALSE;
    drag_info->have_valid_data = FALSE;

    drag_info->drop_occurred = FALSE;
}

static void
slot_proxy_drag_leave (GtkWidget      *widget,
                       GdkDragContext *context,
                       unsigned int    time,
                       gpointer        user_data)
{
    NautilusDragSlotProxyInfo *drag_info;

    drag_info = user_data;

    gtk_drag_unhighlight (widget);
    drag_info_clear (drag_info);
}

static gboolean
slot_proxy_drag_drop (GtkWidget      *widget,
                      GdkDragContext *context,
                      int             x,
                      int             y,
                      unsigned int    time,
                      gpointer        user_data)
{
    GdkAtom target;
    NautilusDragSlotProxyInfo *drag_info;

    drag_info = user_data;
    g_assert (!drag_info->have_data);

    drag_info->drop_occurred = TRUE;

    target = gtk_drag_dest_find_target (widget, context, NULL);
    gtk_drag_get_data (widget, context, target, time);

    return TRUE;
}


static void
slot_proxy_handle_drop (GtkWidget                 *widget,
                        GdkDragContext            *context,
                        unsigned int               time,
                        NautilusDragSlotProxyInfo *drag_info)
{
    GtkWidget *window;
    NautilusWindowSlot *target_slot;
    NautilusFilesView *target_view;
    char *target_uri;
    GList *uri_list;
    GFile *location;

    if (!drag_info->have_data ||
        !drag_info->have_valid_data)
    {
        gtk_drag_finish (context, FALSE, FALSE, time);
        drag_info_clear (drag_info);
        return;
    }

    window = gtk_widget_get_toplevel (widget);
    g_assert (NAUTILUS_IS_WINDOW (window));

    if (drag_info->target_slot != NULL)
    {
        target_slot = drag_info->target_slot;
    }
    else
    {
        target_slot = nautilus_window_get_active_slot (NAUTILUS_WINDOW (window));
    }

    target_uri = NULL;
    if (drag_info->target_file != NULL)
    {
        target_uri = nautilus_file_get_uri (drag_info->target_file);
    }
    else if (target_slot != NULL)
    {
        location = nautilus_window_slot_get_location (target_slot);
        target_uri = g_file_get_uri (location);
    }

    target_view = NULL;
    if (target_slot != NULL)
    {
        NautilusView *view;

        view = nautilus_window_slot_get_current_view (target_slot);

        if (view && NAUTILUS_IS_FILES_VIEW (view))
        {
            target_view = NAUTILUS_FILES_VIEW (view);
        }
    }

    if (target_slot != NULL && target_view != NULL)
    {
        if (drag_info->info == NAUTILUS_ICON_DND_GNOME_ICON_LIST)
        {
            uri_list = nautilus_drag_uri_list_from_selection_list (drag_info->data.selection_list);
            g_assert (uri_list != NULL);

            nautilus_files_view_drop_proxy_received_uris (target_view,
                                                          uri_list,
                                                          target_uri,
                                                          gdk_drag_context_get_selected_action (context));
            g_list_free_full (uri_list, g_free);
        }
        else if (drag_info->info == NAUTILUS_ICON_DND_URI_LIST)
        {
            nautilus_files_view_drop_proxy_received_uris (target_view,
                                                          drag_info->data.uri_list,
                                                          target_uri,
                                                          gdk_drag_context_get_selected_action (context));
        }

        gtk_drag_finish (context, TRUE, FALSE, time);
    }
    else
    {
        gtk_drag_finish (context, FALSE, FALSE, time);
    }

    g_free (target_uri);

    drag_info_clear (drag_info);
}

static void
slot_proxy_drag_data_received (GtkWidget        *widget,
                               GdkDragContext   *context,
                               int               x,
                               int               y,
                               GtkSelectionData *data,
                               unsigned int      info,
                               unsigned int      time,
                               gpointer          user_data)
{
    NautilusDragSlotProxyInfo *drag_info;
    char **uris;

    drag_info = user_data;

    g_assert (!drag_info->have_data);

    drag_info->have_data = TRUE;
    drag_info->info = info;

    if (gtk_selection_data_get_length (data) < 0)
    {
        drag_info->have_valid_data = FALSE;
        return;
    }

    if (info == NAUTILUS_ICON_DND_GNOME_ICON_LIST)
    {
        drag_info->data.selection_list = nautilus_drag_build_selection_list (data);

        drag_info->have_valid_data = drag_info->data.selection_list != NULL;
    }
    else if (info == NAUTILUS_ICON_DND_URI_LIST)
    {
        uris = gtk_selection_data_get_uris (data);
        drag_info->data.uri_list = nautilus_drag_uri_list_from_array ((const char **) uris);
        g_strfreev (uris);

        drag_info->have_valid_data = drag_info->data.uri_list != NULL;
    }
    else if (info == NAUTILUS_ICON_DND_TEXT ||
             info == NAUTILUS_ICON_DND_XDNDDIRECTSAVE ||
             info == NAUTILUS_ICON_DND_RAW)
    {
        drag_info->data.selection_data = gtk_selection_data_copy (data);
        drag_info->have_valid_data = drag_info->data.selection_data != NULL;
    }

    if (drag_info->drop_occurred)
    {
        slot_proxy_handle_drop (widget, context, time, drag_info);
    }
}

void
nautilus_drag_slot_proxy_init (GtkWidget          *widget,
                               NautilusFile       *target_file,
                               NautilusWindowSlot *target_slot)
{
    NautilusDragSlotProxyInfo *drag_info;

    const GtkTargetEntry targets[] =
    {
        { NAUTILUS_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NAUTILUS_ICON_DND_GNOME_ICON_LIST },
        { NAUTILUS_ICON_DND_XDNDDIRECTSAVE_TYPE, 0, NAUTILUS_ICON_DND_XDNDDIRECTSAVE }, /* XDS Protocol Type */
        { NAUTILUS_ICON_DND_RAW_TYPE, 0, NAUTILUS_ICON_DND_RAW }
    };
    GtkTargetList *target_list;

    g_assert (GTK_IS_WIDGET (widget));

    drag_info = g_slice_new0 (NautilusDragSlotProxyInfo);

    g_object_set_data_full (G_OBJECT (widget), "drag-slot-proxy-data", drag_info,
                            drag_info_free);

    drag_info->is_notebook = (g_object_get_data (G_OBJECT (widget), "nautilus-notebook-tab") != NULL);

    if (target_file != NULL)
    {
        drag_info->target_file = nautilus_file_ref (target_file);
    }

    if (target_slot != NULL)
    {
        drag_info->target_slot = g_object_ref (target_slot);
    }

    drag_info->widget = widget;

    gtk_drag_dest_set (widget, 0,
                       NULL, 0,
                       GDK_ACTION_MOVE |
                       GDK_ACTION_COPY |
                       GDK_ACTION_LINK |
                       GDK_ACTION_ASK);

    target_list = gtk_target_list_new (targets, G_N_ELEMENTS (targets));
    gtk_target_list_add_uri_targets (target_list, NAUTILUS_ICON_DND_URI_LIST);
    gtk_target_list_add_text_targets (target_list, NAUTILUS_ICON_DND_TEXT);
    gtk_drag_dest_set_target_list (widget, target_list);
    gtk_target_list_unref (target_list);

    g_signal_connect (widget, "drag-motion",
                      G_CALLBACK (slot_proxy_drag_motion),
                      drag_info);
    g_signal_connect (widget, "drag-drop",
                      G_CALLBACK (slot_proxy_drag_drop),
                      drag_info);
    g_signal_connect (widget, "drag-data-received",
                      G_CALLBACK (slot_proxy_drag_data_received),
                      drag_info);
    g_signal_connect (widget, "drag-leave",
                      G_CALLBACK (slot_proxy_drag_leave),
                      drag_info);
}
