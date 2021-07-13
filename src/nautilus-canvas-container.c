/* nautilus-canvas-container.c - Canvas container widget.
 *
 *  Copyright (C) 1999, 2000 Free Software Foundation
 *  Copyright (C) 2000, 2001 Eazel, Inc.
 *  Copyright (C) 2002, 2003 Red Hat, Inc.
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
 *  Authors: Ettore Perazzoli <ettore@gnu.org>,
 *  Darin Adler <darin@bentspoon.com>
 */

#include "nautilus-canvas-container.h"

#include <atk/atkaction.h>
#include <eel/eel-art-extensions.h>
#include <eel/eel-gtk-extensions.h>
#include <eel/eel-vfs-extensions.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_FLAG NAUTILUS_DEBUG_CANVAS_CONTAINER
#include "nautilus-debug.h"

#include "nautilus-canvas-private.h"
#include "nautilus-global-preferences.h"
#include "nautilus-icon-info.h"
#include "nautilus-lib-self-check-functions.h"
#include "nautilus-selection-canvas-item.h"

/* Interval for updating the rubberband selection, in milliseconds.  */
#define RUBBERBAND_TIMEOUT_INTERVAL 10

#define RUBBERBAND_SCROLL_THRESHOLD 5

/* Initial unpositioned icon value */
#define ICON_UNPOSITIONED_VALUE -1

/* Timeout for making the icon currently selected for keyboard operation visible.
 * If this is 0, you can get into trouble with extra scrolling after holding
 * down the arrow key for awhile when there are many items.
 */
#define KEYBOARD_ICON_REVEAL_TIMEOUT 10

#define CONTEXT_MENU_TIMEOUT_INTERVAL 500

/* Maximum amount of milliseconds the mouse button is allowed to stay down
 * and still be considered a click.
 */
#define MAX_CLICK_TIME 1500

/* Button assignments. */
#define DRAG_BUTTON 1
#define RUBBERBAND_BUTTON 1
#define MIDDLE_BUTTON 2
#define CONTEXTUAL_MENU_BUTTON 3
#define DRAG_MENU_BUTTON 2

/* Maximum size (pixels) allowed for icons at the standard zoom level. */
#define MINIMUM_IMAGE_SIZE 24
#define MAXIMUM_IMAGE_SIZE 96

#define ICON_PAD_LEFT 4
#define ICON_PAD_RIGHT 4
#define ICON_PAD_TOP 4
#define ICON_PAD_BOTTOM 4

#define CONTAINER_PAD_LEFT 4
#define CONTAINER_PAD_RIGHT 4
#define CONTAINER_PAD_TOP 4
#define CONTAINER_PAD_BOTTOM 4

/* Width of a "grid unit". Canvas items will always take up one or more
 * grid units, rounding up their size relative to the unit width.
 * So with an 80px grid unit, a 100px canvas item would take two grid units,
 * where a 76px canvas item would only take one.
 * Canvas items are then centered in the extra available space.
 * Keep in sync with MAX_TEXT_WIDTH at nautilus-canvas-item.
 */
#define SMALL_ICON_GRID_WIDTH 124
#define STANDARD_ICON_GRID_WIDTH 112
#define LARGE_ICON_GRID_WIDTH 106
#define LARGER_ICON_GRID_WIDTH 128

/* Copied from NautilusCanvasContainer */
#define NAUTILUS_CANVAS_CONTAINER_SEARCH_DIALOG_TIMEOUT 5

/* Copied from NautilusFile */
#define UNDEFINED_TIME ((time_t) (-1))

enum
{
    ACTION_ACTIVATE,
    ACTION_MENU,
    LAST_ACTION
};

typedef struct
{
    GList *selection;
    char *action_descriptions[LAST_ACTION];
} NautilusCanvasContainerAccessiblePrivate;

static GType         nautilus_canvas_container_accessible_get_type (void);
static void          preview_selected_items (NautilusCanvasContainer *container);
static void          activate_selected_items (NautilusCanvasContainer *container);
static void          activate_selected_items_alternate (NautilusCanvasContainer *container,
                                                        NautilusCanvasIcon      *icon);
static NautilusCanvasIcon *get_first_selected_icon (NautilusCanvasContainer *container);
static NautilusCanvasIcon *get_nth_selected_icon (NautilusCanvasContainer *container,
                                                  int                      index);
static gboolean      has_multiple_selection (NautilusCanvasContainer *container);
static gboolean      all_selected (NautilusCanvasContainer *container);
static gboolean      has_selection (NautilusCanvasContainer *container);
static void          icon_destroy (NautilusCanvasContainer *container,
                                   NautilusCanvasIcon      *icon);
static gboolean      finish_adding_new_icons (NautilusCanvasContainer *container);
static inline void   icon_get_bounding_box (NautilusCanvasIcon           *icon,
                                            int                          *x1_return,
                                            int                          *y1_return,
                                            int                          *x2_return,
                                            int                          *y2_return,
                                            NautilusCanvasItemBoundsUsage usage);
static void          handle_hadjustment_changed (GtkAdjustment           *adjustment,
                                                 NautilusCanvasContainer *container);
static void          handle_vadjustment_changed (GtkAdjustment           *adjustment,
                                                 NautilusCanvasContainer *container);
static GList *nautilus_canvas_container_get_selected_icons (NautilusCanvasContainer *container);
static void          nautilus_canvas_container_update_visible_icons (NautilusCanvasContainer *container);
static void          reveal_icon (NautilusCanvasContainer *container,
                                  NautilusCanvasIcon      *icon);

static void          nautilus_canvas_container_set_rtl_positions (NautilusCanvasContainer *container);
static double        get_mirror_x_position (NautilusCanvasContainer *container,
                                            NautilusCanvasIcon      *icon,
                                            double                   x);
static void         text_ellipsis_limit_changed_container_callback (gpointer callback_data);

static int compare_icons_horizontal (NautilusCanvasContainer *container,
                                     NautilusCanvasIcon      *icon_a,
                                     NautilusCanvasIcon      *icon_b);

static int compare_icons_vertical (NautilusCanvasContainer *container,
                                   NautilusCanvasIcon      *icon_a,
                                   NautilusCanvasIcon      *icon_b);

static void schedule_redo_layout (NautilusCanvasContainer *container);

static const char *nautilus_canvas_container_accessible_action_names[] =
{
    "activate",
    "menu",
    NULL
};

static const char *nautilus_canvas_container_accessible_action_descriptions[] =
{
    "Activate selected items",
    "Popup context menu",
    NULL
};

G_DEFINE_TYPE (NautilusCanvasContainer, nautilus_canvas_container, EEL_TYPE_CANVAS);

/* The NautilusCanvasContainer signals.  */
enum
{
    ACTIVATE,
    ACTIVATE_ALTERNATE,
    ACTIVATE_PREVIEWER,
    BAND_SELECT_STARTED,
    BAND_SELECT_ENDED,
    BUTTON_PRESS,
    CONTEXT_CLICK_BACKGROUND,
    CONTEXT_CLICK_SELECTION,
    MIDDLE_CLICK,
    GET_CONTAINER_URI,
    GET_ICON_URI,
    GET_ICON_ACTIVATION_URI,
    GET_ICON_DROP_TARGET_URI,
    ICON_RENAME_STARTED,
    ICON_RENAME_ENDED,
    ICON_STRETCH_STARTED,
    ICON_STRETCH_ENDED,
    MOVE_COPY_ITEMS,
    HANDLE_NETSCAPE_URL,
    HANDLE_URI_LIST,
    HANDLE_TEXT,
    HANDLE_RAW,
    HANDLE_HOVER,
    SELECTION_CHANGED,
    ICON_ADDED,
    ICON_REMOVED,
    CLEARED,
    LAST_SIGNAL
};

typedef struct
{
    int **icon_grid;
    int *grid_memory;
    int num_rows;
    int num_columns;
    gboolean tight;
} PlacementGrid;

static guint signals[LAST_SIGNAL];

/* Functions dealing with NautilusIcons.  */

static void
icon_free (NautilusCanvasIcon *icon)
{
    /* Destroy this icon item; the parent will unref it. */
    eel_canvas_item_destroy (EEL_CANVAS_ITEM (icon->item));
    g_free (icon);
}

static gboolean
icon_is_positioned (const NautilusCanvasIcon *icon)
{
    return icon->x != ICON_UNPOSITIONED_VALUE && icon->y != ICON_UNPOSITIONED_VALUE;
}


/* x, y are the top-left coordinates of the icon. */
static void
icon_set_position (NautilusCanvasIcon *icon,
                   double              x,
                   double              y)
{
    if (icon->x == x && icon->y == y)
    {
        return;
    }

    if (icon->x == ICON_UNPOSITIONED_VALUE)
    {
        icon->x = 0;
    }
    if (icon->y == ICON_UNPOSITIONED_VALUE)
    {
        icon->y = 0;
    }

    eel_canvas_item_move (EEL_CANVAS_ITEM (icon->item),
                          x - icon->x,
                          y - icon->y);

    icon->x = x;
    icon->y = y;
}

static guint
nautilus_canvas_container_get_grid_size_for_zoom_level (NautilusCanvasZoomLevel zoom_level)
{
    switch (zoom_level)
    {
        case NAUTILUS_CANVAS_ZOOM_LEVEL_SMALL:
        {
            return SMALL_ICON_GRID_WIDTH;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_STANDARD:
        {
            return STANDARD_ICON_GRID_WIDTH;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_LARGE:
        {
            return LARGE_ICON_GRID_WIDTH;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_LARGER:
        {
            return LARGER_ICON_GRID_WIDTH;
        }
        break;

        default:
        {
            g_return_val_if_reached (STANDARD_ICON_GRID_WIDTH);
        }
        break;
    }
}

guint
nautilus_canvas_container_get_icon_size_for_zoom_level (NautilusCanvasZoomLevel zoom_level)
{
    switch (zoom_level)
    {
        case NAUTILUS_CANVAS_ZOOM_LEVEL_SMALL:
        {
            return NAUTILUS_CANVAS_ICON_SIZE_SMALL;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_STANDARD:
        {
            return NAUTILUS_CANVAS_ICON_SIZE_STANDARD;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_LARGE:
        {
            return NAUTILUS_CANVAS_ICON_SIZE_LARGE;
        }
        break;

        case NAUTILUS_CANVAS_ZOOM_LEVEL_LARGER:
        {
            return NAUTILUS_CANVAS_ICON_SIZE_LARGER;
        }
        break;

        default:
        {
            g_return_val_if_reached (NAUTILUS_CANVAS_ICON_SIZE_STANDARD);
        }
        break;
    }
}

static void
icon_get_size (NautilusCanvasContainer *container,
               NautilusCanvasIcon      *icon,
               guint                   *size)
{
    if (size != NULL)
    {
        *size = MAX (nautilus_canvas_container_get_icon_size_for_zoom_level (container->details->zoom_level),
                     NAUTILUS_CANVAS_ICON_SIZE_SMALL);
    }
}

static void
icon_raise (NautilusCanvasIcon *icon)
{
    EelCanvasItem *item, *band;

    item = EEL_CANVAS_ITEM (icon->item);
    band = NAUTILUS_CANVAS_CONTAINER (item->canvas)->details->rubberband_info.selection_rectangle;

    eel_canvas_item_send_behind (item, band);
}

static void
icon_toggle_selected (NautilusCanvasContainer *container,
                      NautilusCanvasIcon      *icon)
{
    icon->is_selected = !icon->is_selected;
    if (icon->is_selected)
    {
        container->details->selection = g_list_prepend (container->details->selection, icon->data);
        container->details->selection_needs_resort = TRUE;
    }
    else
    {
        container->details->selection = g_list_remove (container->details->selection, icon->data);
    }

    eel_canvas_item_set (EEL_CANVAS_ITEM (icon->item),
                         "highlighted_for_selection", (gboolean) icon->is_selected,
                         NULL);

    /* Raise each newly-selected icon to the front as it is selected. */
    if (icon->is_selected)
    {
        icon_raise (icon);
    }
}

/* Select an icon. Return TRUE if selection has changed. */
static gboolean
icon_set_selected (NautilusCanvasContainer *container,
                   NautilusCanvasIcon      *icon,
                   gboolean                 select)
{
    if (select == icon->is_selected)
    {
        return FALSE;
    }

    icon_toggle_selected (container, icon);
    g_assert (select == icon->is_selected);
    return TRUE;
}

static inline void
icon_get_bounding_box (NautilusCanvasIcon            *icon,
                       int                           *x1_return,
                       int                           *y1_return,
                       int                           *x2_return,
                       int                           *y2_return,
                       NautilusCanvasItemBoundsUsage  usage)
{
    double x1, y1, x2, y2;

    if (usage == BOUNDS_USAGE_FOR_DISPLAY)
    {
        eel_canvas_item_get_bounds (EEL_CANVAS_ITEM (icon->item),
                                    &x1, &y1, &x2, &y2);
    }
    else if (usage == BOUNDS_USAGE_FOR_LAYOUT)
    {
        nautilus_canvas_item_get_bounds_for_layout (icon->item,
                                                    &x1, &y1, &x2, &y2);
    }
    else if (usage == BOUNDS_USAGE_FOR_ENTIRE_ITEM)
    {
        nautilus_canvas_item_get_bounds_for_entire_item (icon->item,
                                                         &x1, &y1, &x2, &y2);
    }
    else
    {
        g_assert_not_reached ();
    }

    if (x1_return != NULL)
    {
        *x1_return = x1;
    }

    if (y1_return != NULL)
    {
        *y1_return = y1;
    }

    if (x2_return != NULL)
    {
        *x2_return = x2;
    }

    if (y2_return != NULL)
    {
        *y2_return = y2;
    }
}

/* Utility functions for NautilusCanvasContainer.  */

gboolean
nautilus_canvas_container_scroll (NautilusCanvasContainer *container,
                                  int                      delta_x,
                                  int                      delta_y)
{
    GtkAdjustment *hadj, *vadj;
    int old_h_value, old_v_value;

    hadj = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container));
    vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container));

    /* Store the old ajustment values so we can tell if we
     * ended up actually scrolling. We may not have in a case
     * where the resulting value got pinned to the adjustment
     * min or max.
     */
    old_h_value = gtk_adjustment_get_value (hadj);
    old_v_value = gtk_adjustment_get_value (vadj);

    gtk_adjustment_set_value (hadj, gtk_adjustment_get_value (hadj) + delta_x);
    gtk_adjustment_set_value (vadj, gtk_adjustment_get_value (vadj) + delta_y);

    /* return TRUE if we did scroll */
    return gtk_adjustment_get_value (hadj) != old_h_value || gtk_adjustment_get_value (vadj) != old_v_value;
}

static void
pending_icon_to_reveal_destroy_callback (NautilusCanvasItem      *item,
                                         NautilusCanvasContainer *container)
{
    g_assert (NAUTILUS_IS_CANVAS_CONTAINER (container));
    g_assert (container->details->pending_icon_to_reveal != NULL);
    g_assert (container->details->pending_icon_to_reveal->item == item);

    container->details->pending_icon_to_reveal = NULL;
}

static NautilusCanvasIcon *
get_pending_icon_to_reveal (NautilusCanvasContainer *container)
{
    return container->details->pending_icon_to_reveal;
}

static void
set_pending_icon_to_reveal (NautilusCanvasContainer *container,
                            NautilusCanvasIcon      *icon)
{
    NautilusCanvasIcon *old_icon;

    old_icon = container->details->pending_icon_to_reveal;

    if (icon == old_icon)
    {
        return;
    }

    if (old_icon != NULL)
    {
        g_signal_handlers_disconnect_by_func
            (old_icon->item,
            G_CALLBACK (pending_icon_to_reveal_destroy_callback),
            container);
    }

    if (icon != NULL)
    {
        g_signal_connect (icon->item, "destroy",
                          G_CALLBACK (pending_icon_to_reveal_destroy_callback),
                          container);
    }

    container->details->pending_icon_to_reveal = icon;
}

static void
item_get_canvas_bounds (EelCanvasItem *item,
                        EelIRect      *bounds)
{
    EelDRect world_rect;

    eel_canvas_item_get_bounds (item,
                                &world_rect.x0,
                                &world_rect.y0,
                                &world_rect.x1,
                                &world_rect.y1);
    eel_canvas_item_i2w (item->parent,
                         &world_rect.x0,
                         &world_rect.y0);
    eel_canvas_item_i2w (item->parent,
                         &world_rect.x1,
                         &world_rect.y1);

    world_rect.x0 -= ICON_PAD_LEFT + ICON_PAD_RIGHT;
    world_rect.x1 += ICON_PAD_LEFT + ICON_PAD_RIGHT;

    world_rect.y0 -= ICON_PAD_TOP + ICON_PAD_BOTTOM;
    world_rect.y1 += ICON_PAD_TOP + ICON_PAD_BOTTOM;

    eel_canvas_w2c (item->canvas,
                    world_rect.x0,
                    world_rect.y0,
                    &bounds->x0,
                    &bounds->y0);
    eel_canvas_w2c (item->canvas,
                    world_rect.x1,
                    world_rect.y1,
                    &bounds->x1,
                    &bounds->y1);
}

static void
icon_get_row_and_column_bounds (NautilusCanvasContainer *container,
                                NautilusCanvasIcon      *icon,
                                EelIRect                *bounds)
{
    GList *p;
    NautilusCanvasIcon *one_icon;
    EelIRect one_bounds;

    item_get_canvas_bounds (EEL_CANVAS_ITEM (icon->item), bounds);

    for (p = container->details->icons; p != NULL; p = p->next)
    {
        one_icon = p->data;

        if (icon == one_icon)
        {
            continue;
        }

        if (compare_icons_horizontal (container, icon, one_icon) == 0)
        {
            item_get_canvas_bounds (EEL_CANVAS_ITEM (one_icon->item), &one_bounds);
            bounds->x0 = MIN (bounds->x0, one_bounds.x0);
            bounds->x1 = MAX (bounds->x1, one_bounds.x1);
        }

        if (compare_icons_vertical (container, icon, one_icon) == 0)
        {
            item_get_canvas_bounds (EEL_CANVAS_ITEM (one_icon->item), &one_bounds);
            bounds->y0 = MIN (bounds->y0, one_bounds.y0);
            bounds->y1 = MAX (bounds->y1, one_bounds.y1);
        }
    }
}

static void
reveal_icon (NautilusCanvasContainer *container,
             NautilusCanvasIcon      *icon)
{
    GtkAllocation allocation;
    GtkAdjustment *hadj, *vadj;
    EelIRect bounds;

    if (!icon_is_positioned (icon))
    {
        set_pending_icon_to_reveal (container, icon);
        return;
    }

    set_pending_icon_to_reveal (container, NULL);

    gtk_widget_get_allocation (GTK_WIDGET (container), &allocation);

    hadj = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container));
    vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container));

    /* ensure that we reveal the entire row/column */
    icon_get_row_and_column_bounds (container, icon, &bounds);

    if (bounds.y0 < gtk_adjustment_get_value (vadj))
    {
        gtk_adjustment_set_value (vadj, bounds.y0);
    }
    else if (bounds.y1 > gtk_adjustment_get_value (vadj) + allocation.height)
    {
        gtk_adjustment_set_value
            (vadj, bounds.y1 - allocation.height);
    }

    if (bounds.x0 < gtk_adjustment_get_value (hadj))
    {
        gtk_adjustment_set_value (hadj, bounds.x0);
    }
    else if (bounds.x1 > gtk_adjustment_get_value (hadj) + allocation.width)
    {
        gtk_adjustment_set_value
            (hadj, bounds.x1 - allocation.width);
    }
}

static void
process_pending_icon_to_reveal (NautilusCanvasContainer *container)
{
    NautilusCanvasIcon *pending_icon_to_reveal;

    pending_icon_to_reveal = get_pending_icon_to_reveal (container);

    if (pending_icon_to_reveal != NULL)
    {
        reveal_icon (container, pending_icon_to_reveal);
    }
}

static gboolean
keyboard_icon_reveal_timeout_callback (gpointer data)
{
    NautilusCanvasContainer *container;
    NautilusCanvasIcon *icon;

    container = NAUTILUS_CANVAS_CONTAINER (data);
    icon = container->details->keyboard_icon_to_reveal;

    g_assert (icon != NULL);

    /* Only reveal the icon if it's still the keyboard focus or if
     * it's still selected. Someone originally thought we should
     * cancel this reveal if the user manages to sneak a direct
     * scroll in before the timeout fires, but we later realized
     * this wouldn't actually be an improvement
     * (see bugzilla.gnome.org 40612).
     */
    if (icon == container->details->focus
        || icon->is_selected)
    {
        reveal_icon (container, icon);
    }
    container->details->keyboard_icon_reveal_timer_id = 0;

    return FALSE;
}

static void
unschedule_keyboard_icon_reveal (NautilusCanvasContainer *container)
{
    NautilusCanvasContainerDetails *details;

    details = container->details;

    if (details->keyboard_icon_reveal_timer_id != 0)
    {
        g_source_remove (details->keyboard_icon_reveal_timer_id);
    }
}

static void
schedule_keyboard_icon_reveal (NautilusCanvasContainer *container,
                               NautilusCanvasIcon      *icon)
{
    NautilusCanvasContainerDetails *details;

    details = container->details;

    unschedule_keyboard_icon_reveal (container);

    details->keyboard_icon_to_reveal = icon;
    details->keyboard_icon_reveal_timer_id
        = g_timeout_add (KEYBOARD_ICON_REVEAL_TIMEOUT,
                         keyboard_icon_reveal_timeout_callback,
                         container);
}

static void inline
emit_atk_object_notify_focused (NautilusCanvasIcon *icon,
                                gboolean            focused)
{
    AtkObject *atk_object = atk_gobject_accessible_for_object (G_OBJECT (icon->item));
    atk_object_notify_state_change (atk_object, ATK_STATE_FOCUSED, focused);
}

static void
clear_focus (NautilusCanvasContainer *container)
{
    if (container->details->focus != NULL)
    {
        if (container->details->keyboard_focus)
        {
            eel_canvas_item_set (EEL_CANVAS_ITEM (container->details->focus->item),
                                 "highlighted_as_keyboard_focus", 0,
                                 NULL);
        }
        else
        {
            emit_atk_object_notify_focused (container->details->focus, FALSE);
        }
    }

    container->details->focus = NULL;
}

/* Set @icon as the icon currently focused for accessibility. */
static void
set_focus (NautilusCanvasContainer *container,
           NautilusCanvasIcon      *icon,
           gboolean                 keyboard_focus)
{
    g_assert (icon != NULL);

    if (icon == container->details->focus)
    {
        return;
    }

    clear_focus (container);

    container->details->focus = icon;
    container->details->keyboard_focus = keyboard_focus;

    if (keyboard_focus)
    {
        eel_canvas_item_set (EEL_CANVAS_ITEM (container->details->focus->item),
                             "highlighted_as_keyboard_focus", 1,
                             NULL);
    }
    else
    {
        emit_atk_object_notify_focused (container->details->focus, TRUE);
    }
}

static void
set_keyboard_rubberband_start (NautilusCanvasContainer *container,
                               NautilusCanvasIcon      *icon)
{
    container->details->keyboard_rubberband_start = icon;
}

static void
clear_keyboard_rubberband_start (NautilusCanvasContainer *container)
{
    container->details->keyboard_rubberband_start = NULL;
}

/* carbon-copy of eel_canvas_group_bounds(), but
 * for NautilusCanvasContainerItems it returns the
 * bounds for the “entire item”.
 */
static void
get_icon_bounds_for_canvas_bounds (EelCanvasGroup                *group,
                                   double                        *x1,
                                   double                        *y1,
                                   double                        *x2,
                                   double                        *y2,
                                   NautilusCanvasItemBoundsUsage  usage)
{
    EelCanvasItem *child;
    GList *list;
    double tx1, ty1, tx2, ty2;
    double minx, miny, maxx, maxy;
    int set;

    /* Get the bounds of the first visible item */

    child = NULL;     /* Unnecessary but eliminates a warning. */

    set = FALSE;

    for (list = group->item_list; list; list = list->next)
    {
        child = list->data;

        if (!NAUTILUS_IS_CANVAS_ITEM (child))
        {
            continue;
        }

        if (child->flags & EEL_CANVAS_ITEM_VISIBLE)
        {
            set = TRUE;
            if (!NAUTILUS_IS_CANVAS_ITEM (child) ||
                usage == BOUNDS_USAGE_FOR_DISPLAY)
            {
                eel_canvas_item_get_bounds (child, &minx, &miny, &maxx, &maxy);
            }
            else if (usage == BOUNDS_USAGE_FOR_LAYOUT)
            {
                nautilus_canvas_item_get_bounds_for_layout (NAUTILUS_CANVAS_ITEM (child),
                                                            &minx, &miny, &maxx, &maxy);
            }
            else if (usage == BOUNDS_USAGE_FOR_ENTIRE_ITEM)
            {
                nautilus_canvas_item_get_bounds_for_entire_item (NAUTILUS_CANVAS_ITEM (child),
                                                                 &minx, &miny, &maxx, &maxy);
            }
            else
            {
                g_assert_not_reached ();
            }
            break;
        }
    }

    /* If there were no visible items, return an empty bounding box */

    if (!set)
    {
        *x1 = *y1 = *x2 = *y2 = 0.0;
        return;
    }

    /* Now we can grow the bounds using the rest of the items */

    list = list->next;

    for (; list; list = list->next)
    {
        child = list->data;

        if (!NAUTILUS_IS_CANVAS_ITEM (child))
        {
            continue;
        }

        if (!(child->flags & EEL_CANVAS_ITEM_VISIBLE))
        {
            continue;
        }

        if (!NAUTILUS_IS_CANVAS_ITEM (child) ||
            usage == BOUNDS_USAGE_FOR_DISPLAY)
        {
            eel_canvas_item_get_bounds (child, &tx1, &ty1, &tx2, &ty2);
        }
        else if (usage == BOUNDS_USAGE_FOR_LAYOUT)
        {
            nautilus_canvas_item_get_bounds_for_layout (NAUTILUS_CANVAS_ITEM (child),
                                                        &tx1, &ty1, &tx2, &ty2);
        }
        else if (usage == BOUNDS_USAGE_FOR_ENTIRE_ITEM)
        {
            nautilus_canvas_item_get_bounds_for_entire_item (NAUTILUS_CANVAS_ITEM (child),
                                                             &tx1, &ty1, &tx2, &ty2);
        }
        else
        {
            g_assert_not_reached ();
        }

        if (tx1 < minx)
        {
            minx = tx1;
        }

        if (ty1 < miny)
        {
            miny = ty1;
        }

        if (tx2 > maxx)
        {
            maxx = tx2;
        }

        if (ty2 > maxy)
        {
            maxy = ty2;
        }
    }

    /* Make the bounds be relative to our parent's coordinate system */

    if (EEL_CANVAS_ITEM (group)->parent)
    {
        minx += group->xpos;
        miny += group->ypos;
        maxx += group->xpos;
        maxy += group->ypos;
    }

    if (x1 != NULL)
    {
        *x1 = minx;
    }

    if (y1 != NULL)
    {
        *y1 = miny;
    }

    if (x2 != NULL)
    {
        *x2 = maxx;
    }

    if (y2 != NULL)
    {
        *y2 = maxy;
    }
}

static void
get_all_icon_bounds (NautilusCanvasContainer       *container,
                     double                        *x1,
                     double                        *y1,
                     double                        *x2,
                     double                        *y2,
                     NautilusCanvasItemBoundsUsage  usage)
{
    /* FIXME bugzilla.gnome.org 42477: Do we have to do something about the rubberband
     * here? Any other non-icon items?
     */
    get_icon_bounds_for_canvas_bounds (EEL_CANVAS_GROUP (EEL_CANVAS (container)->root),
                                       x1, y1, x2, y2, usage);
}

void
nautilus_canvas_container_update_scroll_region (NautilusCanvasContainer *container)
{
    double x1, y1, x2, y2;
    double pixels_per_unit;
    GtkAdjustment *hadj, *vadj;
    float step_increment;
    GtkAllocation allocation;

    pixels_per_unit = EEL_CANVAS (container)->pixels_per_unit;

    get_all_icon_bounds (container, &x1, &y1, &x2, &y2, BOUNDS_USAGE_FOR_ENTIRE_ITEM);

    /* Add border at the "end"of the layout (i.e. after the icons), to
     * ensure we get some space when scrolled to the end.
     */
    y2 += ICON_PAD_BOTTOM + CONTAINER_PAD_BOTTOM;

    /* Auto-layout assumes a 0, 0 scroll origin and at least allocation->width.
     * Then we lay out to the right or to the left, so
     * x can be < 0 and > allocation */
    gtk_widget_get_allocation (GTK_WIDGET (container), &allocation);
    x1 = MIN (x1, 0);
    x2 = MAX (x2, allocation.width / pixels_per_unit);
    y1 = 0;

    x2 -= 1;
    x2 = MAX (x1, x2);

    y2 -= 1;
    y2 = MAX (y1, y2);

    eel_canvas_set_scroll_region (EEL_CANVAS (container), x1, y1, x2, y2);

    hadj = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container));
    vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container));

    /* Scroll by 1/4 icon each time you click. */
    step_increment = nautilus_canvas_container_get_icon_size_for_zoom_level
                         (container->details->zoom_level) / 4;
    if (gtk_adjustment_get_step_increment (hadj) != step_increment)
    {
        gtk_adjustment_set_step_increment (hadj, step_increment);
    }
    if (gtk_adjustment_get_step_increment (vadj) != step_increment)
    {
        gtk_adjustment_set_step_increment (vadj, step_increment);
    }
}

static void
cache_icon_positions (NautilusCanvasContainer *container)
{
    GList *l;
    gint idx;
    NautilusCanvasIcon *icon;

    for (l = container->details->icons, idx = 0; l != NULL; l = l->next)
    {
        icon = l->data;
        icon->position = idx++;
    }
}

static int
compare_icons_data (gconstpointer a,
                    gconstpointer b,
                    gpointer      canvas_container)
{
    NautilusCanvasContainerClass *klass;
    NautilusCanvasIconData *data_a, *data_b;

    data_a = (NautilusCanvasIconData *) a;
    data_b = (NautilusCanvasIconData *) b;
    klass = NAUTILUS_CANVAS_CONTAINER_GET_CLASS (canvas_container);

    return klass->compare_icons (canvas_container, data_a, data_b);
}

static int
compare_icons (gconstpointer a,
               gconstpointer b,
               gpointer      canvas_container)
{
    NautilusCanvasContainerClass *klass;
    const NautilusCanvasIcon *icon_a, *icon_b;

    icon_a = a;
    icon_b = b;
    klass = NAUTILUS_CANVAS_CONTAINER_GET_CLASS (canvas_container);

    return klass->compare_icons (canvas_container, icon_a->data, icon_b->data);
}

static void
sort_selection (NautilusCanvasContainer *container)
{
    container->details->selection = g_list_sort_with_data (container->details->selection,
                                                           compare_icons_data,
                                                           container);
    container->details->selection_needs_resort = FALSE;
}

static void
sort_icons (NautilusCanvasContainer  *container,
            GList                   **icons)
{
    NautilusCanvasContainerClass *klass;

    klass = NAUTILUS_CANVAS_CONTAINER_GET_CLASS (container);
    g_assert (klass->compare_icons != NULL);

    *icons = g_list_sort_with_data (*icons, compare_icons, container);
}

static void
resort (NautilusCanvasContainer *container)
{
    sort_icons (container, &container->details->icons);
    sort_selection (container);
    cache_icon_positions (container);
}

typedef struct
{
    double width;
    double height;
    double x_offset;
    double y_offset;
} IconPositions;

static void
lay_down_one_line (NautilusCanvasContainer *container,
                   GList                   *line_start,
                   GList                   *line_end,
                   double                   y,
                   double                   max_height,
                   GArray                  *positions,
                   gboolean                 whole_text)
{
    GList *p;
    NautilusCanvasIcon *icon;
    double x, ltr_icon_x, icon_x, y_offset;
    IconPositions *position;
    int i;
    gboolean is_rtl;

    is_rtl = nautilus_canvas_container_is_layout_rtl (container);

    /* Lay out the icons along the baseline. */
    x = ICON_PAD_LEFT;
    i = 0;
    for (p = line_start; p != line_end; p = p->next)
    {
        icon = p->data;

        position = &g_array_index (positions, IconPositions, i++);
        ltr_icon_x = x + position->x_offset;
        icon_x = is_rtl ? get_mirror_x_position (container, icon, ltr_icon_x) : ltr_icon_x;
        y_offset = position->y_offset;

        icon_set_position (icon, icon_x, y + y_offset);
        nautilus_canvas_item_set_entire_text (icon->item, whole_text);

        icon->saved_ltr_x = is_rtl ? ltr_icon_x : icon->x;

        x += position->width;
    }
}

static void
lay_down_icons_horizontal (NautilusCanvasContainer *container,
                           GList                   *icons,
                           double                   start_y)
{
    GList *p, *line_start;
    NautilusCanvasIcon *icon;
    double canvas_width, y;
    double available_width;
    GArray *positions;
    IconPositions *position;
    EelDRect bounds;
    EelDRect icon_bounds;
    double max_height_above, max_height_below;
    double height_above, height_below;
    double line_width;
    double min_grid_width;
    double grid_width;
    double num_columns;
    double icon_width, icon_size;
    int i;
    GtkAllocation allocation;

    g_assert (NAUTILUS_IS_CANVAS_CONTAINER (container));

    /* We can't get the right allocation if the size hasn't been allocated yet */
    g_return_if_fail (container->details->has_been_allocated);

    if (icons == NULL)
    {
        return;
    }

    positions = g_array_new (FALSE, FALSE, sizeof (IconPositions));
    gtk_widget_get_allocation (GTK_WIDGET (container), &allocation);

    /* Lay out icons a line at a time. */
    canvas_width = CANVAS_WIDTH (container, allocation);
    min_grid_width = nautilus_canvas_container_get_grid_size_for_zoom_level (container->details->zoom_level);
    icon_size = nautilus_canvas_container_get_icon_size_for_zoom_level (container->details->zoom_level);

    /* Subtracting 1.0 adds some room for error to prevent the jitter due to
     * the code not being able to decide how many columns should be there, as
     * "double" is not perfectly precise and increasing the size of the the
     * window by one pixel could well make it so that the space taken by the
     * icons and the padding is actually greater than the canvas with by like
     * 0.01, causing an entire column to be dropped unnecessarily. This fix is
     * adapted from Nemo.
     */
    available_width = MAX (1.0, canvas_width - ICON_PAD_LEFT - ICON_PAD_RIGHT - 1.0);
    num_columns = MAX (1.0, floor (available_width / min_grid_width));

    if (g_list_nth (icons, num_columns) != NULL)
    {
        grid_width = available_width / num_columns;
    }
    else
    {
        /* It does not look good when the icons jump around when new columns are
         * added or removed to the grid while there is only one line. It does
         * not look good either when the icons do not move at all when the
         * window is resized.
         *
         * To do this, we first compute the maximum extra fraction we can add to
         * the grid width. Adding this much, however, would simply distribute
         * the icons evenly, which looks bad when there's a wide window with
         * only a few icons.
         *
         * To fix this, we need to apply a function to the fraction which never
         * makes it larger and instead makes its growth slow down quickly but
         * smoothly as the window gets wider and wider. Here's the function used
         * by this code:
         *
         * f(x) = ∜(x + 1) - 1
         *
         * The +1 and -1 are there to skip the 0 to 1 part of ∜ where it makes
         * the number larger.
         */

        double num_icons = MAX (1.0, g_list_length (icons));

        double used_width = num_icons * min_grid_width;
        double unused_width = available_width - used_width;

        double max_extra_fraction = (unused_width / num_icons) / min_grid_width;
        double extra_fraction = pow (max_extra_fraction + 1.0, 1.0 / 4.0) - 1.0;

        grid_width = min_grid_width * (1 + extra_fraction);
    }

    grid_width = MAX (min_grid_width, grid_width);

    line_width = 0;
    line_start = icons;
    y = start_y + CONTAINER_PAD_TOP;
    i = 0;

    max_height_above = 0;
    max_height_below = 0;
    for (p = icons; p != NULL; p = p->next)
    {
        icon = p->data;

        /* Assume it's only one level hierarchy to avoid costly affine calculations */
        nautilus_canvas_item_get_bounds_for_layout (icon->item,
                                                    &bounds.x0, &bounds.y0,
                                                    &bounds.x1, &bounds.y1);

        /* Normalize the icon width to the grid unit.
         * Use the icon size for this zoom level too in the calculation, since
         * the actual bounds might be smaller - e.g. because we have a very
         * narrow thumbnail.
         */
        icon_width = ceil (MAX ((bounds.x1 - bounds.x0), icon_size) / grid_width) * grid_width;

        /* Calculate size above/below baseline */
        icon_bounds = nautilus_canvas_item_get_icon_rectangle (icon->item);
        height_above = icon_bounds.y1 - bounds.y0;
        height_below = bounds.y1 - icon_bounds.y1;

        /* If this icon doesn't fit, it's time to lay out the line that's queued up. */
        if (line_start != p && line_width + icon_width >= canvas_width)
        {
            /* Advance to the baseline. */
            y += ICON_PAD_TOP + max_height_above;

            lay_down_one_line (container, line_start, p, y, max_height_above, positions, FALSE);

            /* Advance to next line. */
            y += max_height_below + ICON_PAD_BOTTOM;

            line_width = 0;
            line_start = p;
            i = 0;

            max_height_above = height_above;
            max_height_below = height_below;
        }
        else
        {
            if (height_above > max_height_above)
            {
                max_height_above = height_above;
            }
            if (height_below > max_height_below)
            {
                max_height_below = height_below;
            }
        }

        g_array_set_size (positions, i + 1);
        position = &g_array_index (positions, IconPositions, i++);
        position->width = icon_width;
        position->height = icon_bounds.y1 - icon_bounds.y0;

        position->x_offset = (icon_width - (icon_bounds.x1 - icon_bounds.x0)) / 2;
        position->y_offset = icon_bounds.y0 - icon_bounds.y1;

        /* Add this icon. */
        line_width += icon_width;
    }

    /* Lay down that last line of icons. */
    if (line_start != NULL)
    {
        /* Advance to the baseline. */
        y += ICON_PAD_TOP + max_height_above;

        lay_down_one_line (container, line_start, NULL, y, max_height_above, positions, FALSE);
    }

    g_array_free (positions, TRUE);
}

static double
get_mirror_x_position (NautilusCanvasContainer *container,
                       NautilusCanvasIcon      *icon,
                       double                   x)
{
    EelDRect icon_bounds;
    GtkAllocation allocation;

    gtk_widget_get_allocation (GTK_WIDGET (container), &allocation);
    icon_bounds = nautilus_canvas_item_get_icon_rectangle (icon->item);

    return CANVAS_WIDTH (container, allocation) - x - (icon_bounds.x1 - icon_bounds.x0);
}

static void
nautilus_canvas_container_set_rtl_positions (NautilusCanvasContainer *container)
{
    GList *l;
    NautilusCanvasIcon *icon;
    double x;

    if (!container->details->icons)
    {
        return;
    }

    for (l = container->details->icons; l != NULL; l = l->next)
    {
        icon = l->data;
        x = get_mirror_x_position (container, icon, icon->saved_ltr_x);
        icon_set_position (icon, x, icon->y);
    }
}

static void
lay_down_icons (NautilusCanvasContainer *container,
                GList                   *icons,
                double                   start_y)
{
    lay_down_icons_horizontal (container, icons, start_y);
}

static void
redo_layout_internal (NautilusCanvasContainer *container)
{
    gboolean layout_possible;

    layout_possible = finish_adding_new_icons (container);
    if (!layout_possible)
    {
        schedule_redo_layout (container);
        return;
    }

    if (container->details->needs_resort)
    {
        resort (container);
        container->details->needs_resort = FALSE;
    }
    lay_down_icons (container, container->details->icons, 0);

    if (nautilus_canvas_container_is_layout_rtl (container))
    {
        nautilus_canvas_container_set_rtl_positions (container);
    }

    nautilus_canvas_container_update_scroll_region (container);

    process_pending_icon_to_reveal (container);
    nautilus_canvas_container_update_visible_icons (container);
}

static gboolean
redo_layout_callback (gpointer callback_data)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (callback_data);
    redo_layout_internal (container);
    container->details->idle_id = 0;

    return FALSE;
}

static void
unschedule_redo_layout (NautilusCanvasContainer *container)
{
    if (container->details->idle_id != 0)
    {
        g_source_remove (container->details->idle_id);
        container->details->idle_id = 0;
    }
}

static void
schedule_redo_layout (NautilusCanvasContainer *container)
{
    if (container->details->idle_id == 0
        && container->details->has_been_allocated)
    {
        container->details->idle_id = g_idle_add
                                          (redo_layout_callback, container);
    }
}

static void
redo_layout (NautilusCanvasContainer *container)
{
    unschedule_redo_layout (container);
    /* We can't lay out if the size hasn't been allocated yet; wait for it to
     * be and then we will be called again from size_allocate ()
     */
    if (container->details->has_been_allocated)
    {
        redo_layout_internal (container);
    }
}

/* Container-level icon handling functions.  */

static gboolean
button_event_modifies_selection (GdkEventButton *event)
{
    return (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) != 0;
}

/* invalidate the cached label sizes for all the icons */
static void
invalidate_label_sizes (NautilusCanvasContainer *container)
{
    GList *p;
    NautilusCanvasIcon *icon;

    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;

        nautilus_canvas_item_invalidate_label_size (icon->item);
    }
}

static gboolean
select_range (NautilusCanvasContainer *container,
              NautilusCanvasIcon      *icon1,
              NautilusCanvasIcon      *icon2,
              gboolean                 unselect_outside_range)
{
    gboolean selection_changed;
    GList *p;
    NautilusCanvasIcon *icon;
    NautilusCanvasIcon *unmatched_icon;
    gboolean select;

    selection_changed = FALSE;

    unmatched_icon = NULL;
    select = FALSE;
    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;

        if (unmatched_icon == NULL)
        {
            if (icon == icon1)
            {
                unmatched_icon = icon2;
                select = TRUE;
            }
            else if (icon == icon2)
            {
                unmatched_icon = icon1;
                select = TRUE;
            }
        }

        if (select || unselect_outside_range)
        {
            selection_changed |= icon_set_selected
                                     (container, icon, select);
        }

        if (unmatched_icon != NULL && icon == unmatched_icon)
        {
            select = FALSE;
        }
    }
    return selection_changed;
}


static gboolean
select_one_unselect_others (NautilusCanvasContainer *container,
                            NautilusCanvasIcon      *icon_to_select)
{
    gboolean selection_changed;
    GList *p;
    NautilusCanvasIcon *icon;

    selection_changed = FALSE;

    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;

        selection_changed |= icon_set_selected
                                 (container, icon, icon == icon_to_select);
    }

    if (selection_changed && icon_to_select != NULL)
    {
        reveal_icon (container, icon_to_select);
    }
    return selection_changed;
}

static gboolean
unselect_all (NautilusCanvasContainer *container)
{
    return select_one_unselect_others (container, NULL);
}

/* Implementation of rubberband selection.  */
static void
rubberband_select (NautilusCanvasContainer *container,
                   const EelDRect          *current_rect)
{
    GList *p;
    gboolean selection_changed, is_in, canvas_rect_calculated;
    NautilusCanvasIcon *icon;
    EelIRect canvas_rect;
    EelCanvas *canvas;

    selection_changed = FALSE;
    canvas_rect_calculated = FALSE;

    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;

        if (!canvas_rect_calculated)
        {
            /* Only do this calculation once, since all the canvas items
             * we are interating are in the same coordinate space
             */
            canvas = EEL_CANVAS_ITEM (icon->item)->canvas;
            eel_canvas_w2c (canvas,
                            current_rect->x0,
                            current_rect->y0,
                            &canvas_rect.x0,
                            &canvas_rect.y0);
            eel_canvas_w2c (canvas,
                            current_rect->x1,
                            current_rect->y1,
                            &canvas_rect.x1,
                            &canvas_rect.y1);
            canvas_rect_calculated = TRUE;
        }

        is_in = nautilus_canvas_item_hit_test_rectangle (icon->item, canvas_rect);

        selection_changed |= icon_set_selected
                                 (container, icon,
                                 is_in ^ icon->was_selected_before_rubberband);
    }

    if (selection_changed)
    {
        g_signal_emit (container,
                       signals[SELECTION_CHANGED], 0);
    }
}

static int
rubberband_timeout_callback (gpointer data)
{
    NautilusCanvasContainer *container;
    GtkWidget *widget;
    NautilusCanvasRubberbandInfo *band_info;
    int x, y;
    double x1, y1, x2, y2;
    double world_x, world_y;
    int x_scroll, y_scroll;
    int adj_x, adj_y;
    gboolean adj_changed;
    GtkAllocation allocation;

    EelDRect selection_rect;

    widget = GTK_WIDGET (data);
    container = NAUTILUS_CANVAS_CONTAINER (data);
    band_info = &container->details->rubberband_info;

    g_assert (band_info->timer_id != 0);

    adj_changed = FALSE;
    gtk_widget_get_allocation (widget, &allocation);

    adj_x = gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container)));
    if (adj_x != band_info->last_adj_x)
    {
        band_info->last_adj_x = adj_x;
        adj_changed = TRUE;
    }

    adj_y = gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container)));
    if (adj_y != band_info->last_adj_y)
    {
        band_info->last_adj_y = adj_y;
        adj_changed = TRUE;
    }

    gdk_window_get_device_position (gtk_widget_get_window (widget),
                                    band_info->device,
                                    &x, &y, NULL);

    if (x < RUBBERBAND_SCROLL_THRESHOLD)
    {
        x_scroll = x - RUBBERBAND_SCROLL_THRESHOLD;
        x = 0;
    }
    else if (x >= allocation.width - RUBBERBAND_SCROLL_THRESHOLD)
    {
        x_scroll = x - allocation.width + RUBBERBAND_SCROLL_THRESHOLD + 1;
        x = allocation.width - 1;
    }
    else
    {
        x_scroll = 0;
    }

    if (y < RUBBERBAND_SCROLL_THRESHOLD)
    {
        y_scroll = y - RUBBERBAND_SCROLL_THRESHOLD;
        y = 0;
    }
    else if (y >= allocation.height - RUBBERBAND_SCROLL_THRESHOLD)
    {
        y_scroll = y - allocation.height + RUBBERBAND_SCROLL_THRESHOLD + 1;
        y = allocation.height - 1;
    }
    else
    {
        y_scroll = 0;
    }

    if (y_scroll == 0 && x_scroll == 0
        && (int) band_info->prev_x == x && (int) band_info->prev_y == y && !adj_changed)
    {
        return TRUE;
    }

    nautilus_canvas_container_scroll (container, x_scroll, y_scroll);

    /* Remember to convert from widget to scrolled window coords */
    eel_canvas_window_to_world (EEL_CANVAS (container),
                                x + gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container))),
                                y + gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container))),
                                &world_x, &world_y);

    if (world_x < band_info->start_x)
    {
        x1 = world_x;
        x2 = band_info->start_x;
    }
    else
    {
        x1 = band_info->start_x;
        x2 = world_x;
    }

    if (world_y < band_info->start_y)
    {
        y1 = world_y;
        y2 = band_info->start_y;
    }
    else
    {
        y1 = band_info->start_y;
        y2 = world_y;
    }

    /* Don't let the area of the selection rectangle be empty.
     * Aside from the fact that it would be funny when the rectangle disappears,
     * this also works around a crash in libart that happens sometimes when a
     * zero height rectangle is passed.
     */
    x2 = MAX (x1 + 1, x2);
    y2 = MAX (y1 + 1, y2);

    eel_canvas_item_set
        (band_info->selection_rectangle,
        "x1", x1, "y1", y1,
        "x2", x2, "y2", y2,
        NULL);

    selection_rect.x0 = x1;
    selection_rect.y0 = y1;
    selection_rect.x1 = x2;
    selection_rect.y1 = y2;

    rubberband_select (container,
                       &selection_rect);

    band_info->prev_x = x;
    band_info->prev_y = y;

    return TRUE;
}

static void
stop_rubberbanding (NautilusCanvasContainer *container,
                    GdkEventButton          *event);

static void
start_rubberbanding (NautilusCanvasContainer *container,
                     GdkEventButton          *event)
{
    AtkObject *accessible;
    NautilusCanvasContainerDetails *details;
    NautilusCanvasRubberbandInfo *band_info;
    GList *p;
    NautilusCanvasIcon *icon;

    details = container->details;
    band_info = &details->rubberband_info;

    if (band_info->active)
    {
        g_debug ("Canceling active rubberband by device %s", gdk_device_get_name (band_info->device));
        stop_rubberbanding (container, NULL);
    }

    g_signal_emit (container,
                   signals[BAND_SELECT_STARTED], 0);

    band_info->device = event->device;

    for (p = details->icons; p != NULL; p = p->next)
    {
        icon = p->data;
        icon->was_selected_before_rubberband = icon->is_selected;
    }

    eel_canvas_window_to_world
        (EEL_CANVAS (container), event->x, event->y,
        &band_info->start_x, &band_info->start_y);

    band_info->selection_rectangle = eel_canvas_item_new
                                         (eel_canvas_root
                                             (EEL_CANVAS (container)),
                                         NAUTILUS_TYPE_SELECTION_CANVAS_ITEM,
                                         "x1", band_info->start_x,
                                         "y1", band_info->start_y,
                                         "x2", band_info->start_x,
                                         "y2", band_info->start_y,
                                         NULL);

    accessible = atk_gobject_accessible_for_object
                     (G_OBJECT (band_info->selection_rectangle));
    atk_object_set_name (accessible, "selection");
    atk_object_set_description (accessible, _("The selection rectangle"));

    band_info->prev_x = event->x - gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container)));
    band_info->prev_y = event->y - gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container)));

    band_info->active = TRUE;

    if (band_info->timer_id == 0)
    {
        band_info->timer_id = g_timeout_add
                                  (RUBBERBAND_TIMEOUT_INTERVAL,
                                  rubberband_timeout_callback,
                                  container);
    }

    eel_canvas_item_grab (band_info->selection_rectangle,
                          (GDK_POINTER_MOTION_MASK
                           | GDK_BUTTON_RELEASE_MASK
                           | GDK_SCROLL_MASK),
                          NULL,
                          (GdkEvent *) event);
}

static void
stop_rubberbanding (NautilusCanvasContainer *container,
                    GdkEventButton          *event)
{
    NautilusCanvasRubberbandInfo *band_info;
    GList *icons;
    gboolean enable_animation;

    band_info = &container->details->rubberband_info;

    if (event != NULL && event->device != band_info->device)
    {
        return;
    }

    g_assert (band_info->timer_id != 0);
    g_source_remove (band_info->timer_id);
    band_info->timer_id = 0;

    band_info->active = FALSE;

    band_info->device = NULL;

    g_object_get (gtk_settings_get_default (), "gtk-enable-animations", &enable_animation, NULL);

    /* Destroy this canvas item; the parent will unref it. */
    eel_canvas_item_ungrab (band_info->selection_rectangle);
    eel_canvas_item_lower_to_bottom (band_info->selection_rectangle);
    eel_canvas_item_destroy (band_info->selection_rectangle);
    band_info->selection_rectangle = NULL;

    /* if only one item has been selected, use it as range
     * selection base (cf. handle_icon_button_press) */
    icons = nautilus_canvas_container_get_selected_icons (container);
    if (g_list_length (icons) == 1)
    {
        container->details->range_selection_base_icon = icons->data;
    }
    g_list_free (icons);

    g_signal_emit (container,
                   signals[BAND_SELECT_ENDED], 0);
}

/* Keyboard navigation.  */

typedef gboolean (*IsBetterCanvasFunction) (NautilusCanvasContainer *container,
                                            NautilusCanvasIcon      *start_icon,
                                            NautilusCanvasIcon      *best_so_far,
                                            NautilusCanvasIcon      *candidate,
                                            void                    *data);

static NautilusCanvasIcon *
find_best_icon (NautilusCanvasContainer *container,
                NautilusCanvasIcon      *start_icon,
                IsBetterCanvasFunction   function,
                void                    *data)
{
    GList *p;
    NautilusCanvasIcon *best, *candidate;

    best = NULL;
    for (p = container->details->icons; p != NULL; p = p->next)
    {
        candidate = p->data;

        if (candidate != start_icon)
        {
            if ((*function)(container, start_icon, best, candidate, data))
            {
                best = candidate;
            }
        }
    }
    return best;
}

static NautilusCanvasIcon *
find_best_selected_icon (NautilusCanvasContainer *container,
                         NautilusCanvasIcon      *start_icon,
                         IsBetterCanvasFunction   function,
                         void                    *data)
{
    GList *p;
    NautilusCanvasIcon *best, *candidate;

    best = NULL;
    for (p = container->details->icons; p != NULL; p = p->next)
    {
        candidate = p->data;

        if (candidate != start_icon && candidate->is_selected)
        {
            if ((*function)(container, start_icon, best, candidate, data))
            {
                best = candidate;
            }
        }
    }
    return best;
}

static int
compare_icons_by_uri (NautilusCanvasContainer *container,
                      NautilusCanvasIcon      *icon_a,
                      NautilusCanvasIcon      *icon_b)
{
    char *uri_a, *uri_b;
    int result;

    g_assert (NAUTILUS_IS_CANVAS_CONTAINER (container));
    g_assert (icon_a != NULL);
    g_assert (icon_b != NULL);
    g_assert (icon_a != icon_b);

    uri_a = nautilus_canvas_container_get_icon_uri (container, icon_a);
    uri_b = nautilus_canvas_container_get_icon_uri (container, icon_b);
    result = strcmp (uri_a, uri_b);
    g_assert (result != 0);
    g_free (uri_a);
    g_free (uri_b);

    return result;
}

static int
get_cmp_point_x (NautilusCanvasContainer *container,
                 EelDRect                 icon_rect)
{
    return (icon_rect.x0 + icon_rect.x1) / 2;
}

static int
get_cmp_point_y (NautilusCanvasContainer *container,
                 EelDRect                 icon_rect)
{
    return icon_rect.y1;
}


static int
compare_icons_horizontal (NautilusCanvasContainer *container,
                          NautilusCanvasIcon      *icon_a,
                          NautilusCanvasIcon      *icon_b)
{
    EelDRect world_rect;
    int ax, bx;

    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_a->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &ax,
        NULL);
    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_b->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &bx,
        NULL);

    if (ax < bx)
    {
        return -1;
    }
    if (ax > bx)
    {
        return +1;
    }
    return 0;
}

static int
compare_icons_vertical (NautilusCanvasContainer *container,
                        NautilusCanvasIcon      *icon_a,
                        NautilusCanvasIcon      *icon_b)
{
    EelDRect world_rect;
    int ay, by;

    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_a->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        NULL,
        &ay);
    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_b->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        NULL,
        &by);

    if (ay < by)
    {
        return -1;
    }
    if (ay > by)
    {
        return +1;
    }
    return 0;
}

static int
compare_icons_horizontal_first (NautilusCanvasContainer *container,
                                NautilusCanvasIcon      *icon_a,
                                NautilusCanvasIcon      *icon_b)
{
    EelDRect world_rect;
    int ax, ay, bx, by;

    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_a->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &ax,
        &ay);
    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_b->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &bx,
        &by);

    if (ax < bx)
    {
        return -1;
    }
    if (ax > bx)
    {
        return +1;
    }
    if (ay < by)
    {
        return -1;
    }
    if (ay > by)
    {
        return +1;
    }
    return compare_icons_by_uri (container, icon_a, icon_b);
}

static int
compare_icons_vertical_first (NautilusCanvasContainer *container,
                              NautilusCanvasIcon      *icon_a,
                              NautilusCanvasIcon      *icon_b)
{
    EelDRect world_rect;
    int ax, ay, bx, by;

    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_a->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &ax,
        &ay);
    world_rect = nautilus_canvas_item_get_icon_rectangle (icon_b->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &bx,
        &by);

    if (ay < by)
    {
        return -1;
    }
    if (ay > by)
    {
        return +1;
    }
    if (ax < bx)
    {
        return -1;
    }
    if (ax > bx)
    {
        return +1;
    }
    return compare_icons_by_uri (container, icon_a, icon_b);
}

static gboolean
leftmost_in_top_row (NautilusCanvasContainer *container,
                     NautilusCanvasIcon      *start_icon,
                     NautilusCanvasIcon      *best_so_far,
                     NautilusCanvasIcon      *candidate,
                     void                    *data)
{
    if (best_so_far == NULL)
    {
        return TRUE;
    }
    return compare_icons_vertical_first (container, best_so_far, candidate) > 0;
}

static gboolean
rightmost_in_top_row (NautilusCanvasContainer *container,
                      NautilusCanvasIcon      *start_icon,
                      NautilusCanvasIcon      *best_so_far,
                      NautilusCanvasIcon      *candidate,
                      void                    *data)
{
    if (best_so_far == NULL)
    {
        return TRUE;
    }
    return compare_icons_vertical (container, best_so_far, candidate) > 0;
    return compare_icons_horizontal (container, best_so_far, candidate) < 0;
}

static gboolean
rightmost_in_bottom_row (NautilusCanvasContainer *container,
                         NautilusCanvasIcon      *start_icon,
                         NautilusCanvasIcon      *best_so_far,
                         NautilusCanvasIcon      *candidate,
                         void                    *data)
{
    if (best_so_far == NULL)
    {
        return TRUE;
    }
    return compare_icons_vertical_first (container, best_so_far, candidate) < 0;
}

static int
compare_with_start_row (NautilusCanvasContainer *container,
                        NautilusCanvasIcon      *icon)
{
    EelCanvasItem *item;

    item = EEL_CANVAS_ITEM (icon->item);

    if (container->details->arrow_key_start_y < item->y1)
    {
        return -1;
    }
    if (container->details->arrow_key_start_y > item->y2)
    {
        return +1;
    }
    return 0;
}

static int
compare_with_start_column (NautilusCanvasContainer *container,
                           NautilusCanvasIcon      *icon)
{
    EelCanvasItem *item;

    item = EEL_CANVAS_ITEM (icon->item);

    if (container->details->arrow_key_start_x < item->x1)
    {
        return -1;
    }
    if (container->details->arrow_key_start_x > item->x2)
    {
        return +1;
    }
    return 0;
}

static gboolean
same_row_right_side_leftmost (NautilusCanvasContainer *container,
                              NautilusCanvasIcon      *start_icon,
                              NautilusCanvasIcon      *best_so_far,
                              NautilusCanvasIcon      *candidate,
                              void                    *data)
{
    /* Candidates not on the start row do not qualify. */
    if (compare_with_start_row (container, candidate) != 0)
    {
        return FALSE;
    }

    /* Candidates that are farther right lose out. */
    if (best_so_far != NULL)
    {
        if (compare_icons_horizontal_first (container,
                                            best_so_far,
                                            candidate) < 0)
        {
            return FALSE;
        }
    }

    /* Candidate to the left of the start do not qualify. */
    if (compare_icons_horizontal_first (container,
                                        candidate,
                                        start_icon) <= 0)
    {
        return FALSE;
    }

    return TRUE;
}

static gboolean
same_row_left_side_rightmost (NautilusCanvasContainer *container,
                              NautilusCanvasIcon      *start_icon,
                              NautilusCanvasIcon      *best_so_far,
                              NautilusCanvasIcon      *candidate,
                              void                    *data)
{
    /* Candidates not on the start row do not qualify. */
    if (compare_with_start_row (container, candidate) != 0)
    {
        return FALSE;
    }

    /* Candidates that are farther left lose out. */
    if (best_so_far != NULL)
    {
        if (compare_icons_horizontal_first (container,
                                            best_so_far,
                                            candidate) > 0)
        {
            return FALSE;
        }
    }

    /* Candidate to the right of the start do not qualify. */
    if (compare_icons_horizontal_first (container,
                                        candidate,
                                        start_icon) >= 0)
    {
        return FALSE;
    }

    return TRUE;
}

static gboolean
next_row_leftmost (NautilusCanvasContainer *container,
                   NautilusCanvasIcon      *start_icon,
                   NautilusCanvasIcon      *best_so_far,
                   NautilusCanvasIcon      *candidate,
                   void                    *data)
{
    /* sort out icons that are not below the current row */
    if (compare_with_start_row (container, candidate) >= 0)
    {
        return FALSE;
    }

    if (best_so_far != NULL)
    {
        if (compare_icons_vertical_first (container,
                                          best_so_far,
                                          candidate) > 0)
        {
            /* candidate is above best choice, but below the current row */
            return TRUE;
        }

        if (compare_icons_horizontal_first (container,
                                            best_so_far,
                                            candidate) > 0)
        {
            return TRUE;
        }
    }

    return best_so_far == NULL;
}

static gboolean
next_row_rightmost (NautilusCanvasContainer *container,
                    NautilusCanvasIcon      *start_icon,
                    NautilusCanvasIcon      *best_so_far,
                    NautilusCanvasIcon      *candidate,
                    void                    *data)
{
    /* sort out icons that are not below the current row */
    if (compare_with_start_row (container, candidate) >= 0)
    {
        return FALSE;
    }

    if (best_so_far != NULL)
    {
        if (compare_icons_vertical_first (container,
                                          best_so_far,
                                          candidate) > 0)
        {
            /* candidate is above best choice, but below the current row */
            return TRUE;
        }

        if (compare_icons_horizontal_first (container,
                                            best_so_far,
                                            candidate) < 0)
        {
            return TRUE;
        }
    }

    return best_so_far == NULL;
}

static gboolean
previous_row_rightmost (NautilusCanvasContainer *container,
                        NautilusCanvasIcon      *start_icon,
                        NautilusCanvasIcon      *best_so_far,
                        NautilusCanvasIcon      *candidate,
                        void                    *data)
{
    /* sort out icons that are not above the current row */
    if (compare_with_start_row (container, candidate) <= 0)
    {
        return FALSE;
    }

    if (best_so_far != NULL)
    {
        if (compare_icons_vertical_first (container,
                                          best_so_far,
                                          candidate) < 0)
        {
            /* candidate is below the best choice, but above the current row */
            return TRUE;
        }

        if (compare_icons_horizontal_first (container,
                                            best_so_far,
                                            candidate) < 0)
        {
            return TRUE;
        }
    }

    return best_so_far == NULL;
}

static gboolean
same_column_above_lowest (NautilusCanvasContainer *container,
                          NautilusCanvasIcon      *start_icon,
                          NautilusCanvasIcon      *best_so_far,
                          NautilusCanvasIcon      *candidate,
                          void                    *data)
{
    /* Candidates not on the start column do not qualify. */
    if (compare_with_start_column (container, candidate) != 0)
    {
        return FALSE;
    }

    /* Candidates that are higher lose out. */
    if (best_so_far != NULL)
    {
        if (compare_icons_vertical_first (container,
                                          best_so_far,
                                          candidate) > 0)
        {
            return FALSE;
        }
    }

    /* Candidates below the start do not qualify. */
    if (compare_icons_vertical_first (container,
                                      candidate,
                                      start_icon) >= 0)
    {
        return FALSE;
    }

    return TRUE;
}

static gboolean
same_column_below_highest (NautilusCanvasContainer *container,
                           NautilusCanvasIcon      *start_icon,
                           NautilusCanvasIcon      *best_so_far,
                           NautilusCanvasIcon      *candidate,
                           void                    *data)
{
    /* Candidates not on the start column do not qualify. */
    if (compare_with_start_column (container, candidate) != 0)
    {
        return FALSE;
    }

    /* Candidates that are lower lose out. */
    if (best_so_far != NULL)
    {
        if (compare_icons_vertical_first (container,
                                          best_so_far,
                                          candidate) < 0)
        {
            return FALSE;
        }
    }

    /* Candidates above the start do not qualify. */
    if (compare_icons_vertical_first (container,
                                      candidate,
                                      start_icon) <= 0)
    {
        return FALSE;
    }

    return TRUE;
}

static gboolean
closest_in_90_degrees (NautilusCanvasContainer *container,
                       NautilusCanvasIcon      *start_icon,
                       NautilusCanvasIcon      *best_so_far,
                       NautilusCanvasIcon      *candidate,
                       void                    *data)
{
    EelDRect world_rect;
    int x, y;
    int dx, dy;
    int dist;
    int *best_dist;


    world_rect = nautilus_canvas_item_get_icon_rectangle (candidate->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &x,
        &y);

    dx = x - container->details->arrow_key_start_x;
    dy = y - container->details->arrow_key_start_y;

    switch (container->details->arrow_key_direction)
    {
        case GTK_DIR_UP:
        {
            if (dy > 0 ||
                ABS (dx) > ABS (dy))
            {
                return FALSE;
            }
        }
        break;

        case GTK_DIR_DOWN:
        {
            if (dy < 0 ||
                ABS (dx) > ABS (dy))
            {
                return FALSE;
            }
        }
        break;

        case GTK_DIR_LEFT:
        {
            if (dx > 0 ||
                ABS (dy) > ABS (dx))
            {
                return FALSE;
            }
        }
        break;

        case GTK_DIR_RIGHT:
        {
            if (dx < 0 ||
                ABS (dy) > ABS (dx))
            {
                return FALSE;
            }
        }
        break;

        default:
        {
            g_assert_not_reached ();
        }
        break;
    }

    dist = dx * dx + dy * dy;
    best_dist = data;

    if (best_so_far == NULL)
    {
        *best_dist = dist;
        return TRUE;
    }

    if (dist < *best_dist)
    {
        *best_dist = dist;
        return TRUE;
    }

    return FALSE;
}

static EelDRect
get_rubberband (NautilusCanvasIcon *icon1,
                NautilusCanvasIcon *icon2)
{
    EelDRect rect1;
    EelDRect rect2;
    EelDRect ret;

    eel_canvas_item_get_bounds (EEL_CANVAS_ITEM (icon1->item),
                                &rect1.x0, &rect1.y0,
                                &rect1.x1, &rect1.y1);
    eel_canvas_item_get_bounds (EEL_CANVAS_ITEM (icon2->item),
                                &rect2.x0, &rect2.y0,
                                &rect2.x1, &rect2.y1);

    eel_drect_union (&ret, &rect1, &rect2);

    return ret;
}

static void
keyboard_move_to (NautilusCanvasContainer *container,
                  NautilusCanvasIcon      *icon,
                  NautilusCanvasIcon      *from,
                  GdkEventKey             *event)
{
    if (icon == NULL)
    {
        return;
    }

    set_focus (container, icon, TRUE);

    if (event != NULL &&
        (event->state & GDK_CONTROL_MASK) != 0 &&
        (event->state & GDK_SHIFT_MASK) == 0)
    {
        clear_keyboard_rubberband_start (container);
    }
    else if (event != NULL &&
             (event->state & GDK_CONTROL_MASK) != 0 &&
             (event->state & GDK_SHIFT_MASK) != 0)
    {
        /* Do rubberband selection */
        EelDRect rect;

        if (from && !container->details->keyboard_rubberband_start)
        {
            set_keyboard_rubberband_start (container, from);
        }

        if (icon && container->details->keyboard_rubberband_start)
        {
            rect = get_rubberband (container->details->keyboard_rubberband_start,
                                   icon);
            rubberband_select (container, &rect);
        }
    }
    else if (event != NULL &&
             (event->state & GDK_CONTROL_MASK) == 0 &&
             (event->state & GDK_SHIFT_MASK) != 0)
    {
        /* Select range */
        NautilusCanvasIcon *start_icon;

        start_icon = container->details->range_selection_base_icon;
        if (start_icon == NULL || !start_icon->is_selected)
        {
            start_icon = icon;
            container->details->range_selection_base_icon = icon;
        }

        if (select_range (container, start_icon, icon, TRUE))
        {
            g_signal_emit (container,
                           signals[SELECTION_CHANGED], 0);
        }
    }
    else
    {
        /* Select icon. */
        clear_keyboard_rubberband_start (container);

        container->details->range_selection_base_icon = icon;
        if (select_one_unselect_others (container, icon))
        {
            g_signal_emit (container,
                           signals[SELECTION_CHANGED], 0);
        }
    }
    schedule_keyboard_icon_reveal (container, icon);
}

static void
keyboard_home (NautilusCanvasContainer *container,
               GdkEventKey             *event)
{
    NautilusCanvasIcon *from;
    NautilusCanvasIcon *to;

    /* Home selects the first canvas.
     * Control-Home sets the keyboard focus to the first canvas.
     */

    from = find_best_selected_icon (container, NULL,
                                    rightmost_in_bottom_row,
                                    NULL);
    to = find_best_icon (container, NULL, leftmost_in_top_row, NULL);

    keyboard_move_to (container, to, from, event);
}

static void
keyboard_end (NautilusCanvasContainer *container,
              GdkEventKey             *event)
{
    NautilusCanvasIcon *to;
    NautilusCanvasIcon *from;

    /* End selects the last canvas.
     * Control-End sets the keyboard focus to the last canvas.
     */
    from = find_best_selected_icon (container, NULL,
                                    leftmost_in_top_row,
                                    NULL);
    to = find_best_icon (container, NULL, rightmost_in_bottom_row, NULL);

    keyboard_move_to (container, to, from, event);
}

static void
record_arrow_key_start (NautilusCanvasContainer *container,
                        NautilusCanvasIcon      *icon,
                        GtkDirectionType         direction)
{
    EelDRect world_rect;

    world_rect = nautilus_canvas_item_get_icon_rectangle (icon->item);
    eel_canvas_w2c
        (EEL_CANVAS (container),
        get_cmp_point_x (container, world_rect),
        get_cmp_point_y (container, world_rect),
        &container->details->arrow_key_start_x,
        &container->details->arrow_key_start_y);
    container->details->arrow_key_direction = direction;
}

static void
keyboard_arrow_key (NautilusCanvasContainer *container,
                    GdkEventKey             *event,
                    GtkDirectionType         direction,
                    IsBetterCanvasFunction   better_start,
                    IsBetterCanvasFunction   empty_start,
                    IsBetterCanvasFunction   better_destination,
                    IsBetterCanvasFunction   better_destination_fallback,
                    IsBetterCanvasFunction   better_destination_fallback_fallback,
                    IsBetterCanvasFunction   better_destination_manual)
{
    NautilusCanvasIcon *from;
    NautilusCanvasIcon *to;
    int data;

    /* Chose the icon to start with.
     * If we have a keyboard focus, start with it.
     * Otherwise, use the single selected icon.
     * If there's multiple selection, use the icon farthest toward the end.
     */

    from = container->details->focus;

    if (from == NULL)
    {
        if (has_multiple_selection (container))
        {
            if (all_selected (container))
            {
                from = find_best_selected_icon
                           (container, NULL,
                           empty_start, NULL);
            }
            else
            {
                from = find_best_selected_icon
                           (container, NULL,
                           better_start, NULL);
            }
        }
        else
        {
            from = get_first_selected_icon (container);
        }
    }

    /* If there's no icon, select the icon farthest toward the end.
     * If there is an icon, select the next icon based on the arrow direction.
     */
    if (from == NULL)
    {
        to = from = find_best_icon
                        (container, NULL,
                        empty_start, NULL);
    }
    else
    {
        record_arrow_key_start (container, from, direction);

        to = find_best_icon
                 (container, from,
                 better_destination,
                 &data);

        /* Wrap around to next/previous row/column */
        if (to == NULL &&
            better_destination_fallback != NULL)
        {
            to = find_best_icon
                     (container, from,
                     better_destination_fallback,
                     &data);
        }

        /* With a layout like
         * 1 2 3
         * 4
         * (horizontal layout)
         *
         * or
         *
         * 1 4
         * 2
         * 3
         * (vertical layout)
         *
         * * pressing down for any of 1,2,3 (horizontal layout)
         * * pressing right for any of 1,2,3 (vertical layout)
         *
         * Should select 4.
         */
        if (to == NULL &&
            better_destination_fallback_fallback != NULL)
        {
            to = find_best_icon
                     (container, from,
                     better_destination_fallback_fallback,
                     &data);
        }

        if (to == NULL)
        {
            to = from;
        }
    }

    keyboard_move_to (container, to, from, event);
}

static gboolean
is_rectangle_selection_event (GdkEventKey *event)
{
    return event != NULL &&
           (event->state & GDK_CONTROL_MASK) != 0 &&
           (event->state & GDK_SHIFT_MASK) != 0;
}

static void
keyboard_right (NautilusCanvasContainer *container,
                GdkEventKey             *event)
{
    IsBetterCanvasFunction fallback;

    fallback = NULL;
    if (!is_rectangle_selection_event (event))
    {
        fallback = next_row_leftmost;
    }

    /* Right selects the next icon in the same row.
     * Control-Right sets the keyboard focus to the next icon in the same row.
     */
    keyboard_arrow_key (container,
                        event,
                        GTK_DIR_RIGHT,
                        rightmost_in_bottom_row,
                        nautilus_canvas_container_is_layout_rtl (container) ?
                        rightmost_in_top_row : leftmost_in_top_row,
                        same_row_right_side_leftmost,
                        fallback,
                        NULL,
                        closest_in_90_degrees);
}

static void
keyboard_left (NautilusCanvasContainer *container,
               GdkEventKey             *event)
{
    IsBetterCanvasFunction fallback;

    fallback = NULL;
    if (!is_rectangle_selection_event (event))
    {
        fallback = previous_row_rightmost;
    }

    /* Left selects the next icon in the same row.
     * Control-Left sets the keyboard focus to the next icon in the same row.
     */
    keyboard_arrow_key (container,
                        event,
                        GTK_DIR_LEFT,
                        rightmost_in_bottom_row,
                        nautilus_canvas_container_is_layout_rtl (container) ?
                        rightmost_in_top_row : leftmost_in_top_row,
                        same_row_left_side_rightmost,
                        fallback,
                        NULL,
                        closest_in_90_degrees);
}

static void
keyboard_down (NautilusCanvasContainer *container,
               GdkEventKey             *event)
{
    IsBetterCanvasFunction next_row_fallback;

    next_row_fallback = NULL;
    if (gtk_widget_get_direction (GTK_WIDGET (container)) == GTK_TEXT_DIR_RTL)
    {
        next_row_fallback = next_row_leftmost;
    }
    else
    {
        next_row_fallback = next_row_rightmost;
    }

    /* Down selects the next icon in the same column.
     * Control-Down sets the keyboard focus to the next icon in the same column.
     */
    keyboard_arrow_key (container,
                        event,
                        GTK_DIR_DOWN,
                        rightmost_in_bottom_row,
                        nautilus_canvas_container_is_layout_rtl (container) ?
                        rightmost_in_top_row : leftmost_in_top_row,
                        same_column_below_highest,
                        NULL,
                        next_row_fallback,
                        closest_in_90_degrees);
}

static void
keyboard_up (NautilusCanvasContainer *container,
             GdkEventKey             *event)
{
    /* Up selects the next icon in the same column.
     * Control-Up sets the keyboard focus to the next icon in the same column.
     */
    keyboard_arrow_key (container,
                        event,
                        GTK_DIR_UP,
                        rightmost_in_bottom_row,
                        nautilus_canvas_container_is_layout_rtl (container) ?
                        rightmost_in_top_row : leftmost_in_top_row,
                        same_column_above_lowest,
                        NULL,
                        NULL,
                        closest_in_90_degrees);
}

void
nautilus_canvas_container_preview_selection_event (NautilusCanvasContainer *container,
                                                   GtkDirectionType         direction)
{
    if (direction == GTK_DIR_UP)
    {
        keyboard_up (container, NULL);
    }
    else if (direction == GTK_DIR_DOWN)
    {
        keyboard_down (container, NULL);
    }
    else if (direction == GTK_DIR_LEFT)
    {
        keyboard_left (container, NULL);
    }
    else if (direction == GTK_DIR_RIGHT)
    {
        keyboard_right (container, NULL);
    }
}

static void
keyboard_space (NautilusCanvasContainer *container,
                GdkEventKey             *event)
{
    NautilusCanvasIcon *icon;

    if (!has_selection (container) &&
        container->details->focus != NULL)
    {
        keyboard_move_to (container,
                          container->details->focus,
                          NULL, NULL);
    }
    else if ((event->state & GDK_CONTROL_MASK) != 0 &&
             (event->state & GDK_SHIFT_MASK) == 0)
    {
        /* Control-space toggles the selection state of the current icon. */
        if (container->details->focus != NULL)
        {
            icon_toggle_selected (container, container->details->focus);
            g_signal_emit (container, signals[SELECTION_CHANGED], 0);
            if (container->details->focus->is_selected)
            {
                container->details->range_selection_base_icon = container->details->focus;
            }
        }
        else
        {
            icon = find_best_selected_icon (container,
                                            NULL,
                                            leftmost_in_top_row,
                                            NULL);
            if (icon == NULL)
            {
                icon = find_best_icon (container,
                                       NULL,
                                       leftmost_in_top_row,
                                       NULL);
            }
            if (icon != NULL)
            {
                set_focus (container, icon, TRUE);
            }
        }
    }
    else if ((event->state & GDK_SHIFT_MASK) != 0)
    {
        activate_selected_items_alternate (container, NULL);
    }
    else
    {
        preview_selected_items (container);
    }
}

static void
destroy (GtkWidget *object)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (object);

    nautilus_canvas_container_clear (container);

    if (container->details->rubberband_info.timer_id != 0)
    {
        g_source_remove (container->details->rubberband_info.timer_id);
        container->details->rubberband_info.timer_id = 0;
    }

    if (container->details->idle_id != 0)
    {
        g_source_remove (container->details->idle_id);
        container->details->idle_id = 0;
    }

    if (container->details->align_idle_id != 0)
    {
        g_source_remove (container->details->align_idle_id);
        container->details->align_idle_id = 0;
    }

    if (container->details->selection_changed_id != 0)
    {
        g_source_remove (container->details->selection_changed_id);
        container->details->selection_changed_id = 0;
    }

    if (container->details->size_allocation_count_id != 0)
    {
        g_source_remove (container->details->size_allocation_count_id);
        container->details->size_allocation_count_id = 0;
    }

    GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->destroy (object);
}

static void
finalize (GObject *object)
{
    NautilusCanvasContainerDetails *details;

    details = NAUTILUS_CANVAS_CONTAINER (object)->details;

    g_signal_handlers_disconnect_by_func (nautilus_icon_view_preferences,
                                          text_ellipsis_limit_changed_container_callback,
                                          object);

    g_hash_table_destroy (details->icon_set);
    details->icon_set = NULL;

    g_free (details->font);

    if (details->a11y_item_action_queue != NULL)
    {
        while (!g_queue_is_empty (details->a11y_item_action_queue))
        {
            g_free (g_queue_pop_head (details->a11y_item_action_queue));
        }
        g_queue_free (details->a11y_item_action_queue);
    }
    if (details->a11y_item_action_idle_handler != 0)
    {
        g_source_remove (details->a11y_item_action_idle_handler);
    }

    g_free (details);

    G_OBJECT_CLASS (nautilus_canvas_container_parent_class)->finalize (object);
}

/* GtkWidget methods.  */

static gboolean
clear_size_allocation_count (gpointer data)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (data);

    container->details->size_allocation_count_id = 0;
    container->details->size_allocation_count = 0;

    return FALSE;
}

static void
size_allocate (GtkWidget     *widget,
               GtkAllocation *allocation)
{
    NautilusCanvasContainer *container;
    gboolean need_layout_redone;
    GtkAllocation wid_allocation;

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    need_layout_redone = !container->details->has_been_allocated;
    gtk_widget_get_allocation (widget, &wid_allocation);

    if (allocation->width != wid_allocation.width)
    {
        need_layout_redone = TRUE;
    }

    if (allocation->height != wid_allocation.height)
    {
        need_layout_redone = TRUE;
    }

    /* Under some conditions we can end up in a loop when size allocating.
     * This happens when the icons don't fit without a scrollbar, but fits
     * when a scrollbar is added (bug #129963 for details).
     * We keep track of this looping by increasing a counter in size_allocate
     * and clearing it in a high-prio idle (the only way to detect the loop is
     * done).
     * When we've done at more than two iterations (with/without scrollbar)
     * we terminate this looping by not redoing the layout when the width
     * is wider than the current one (i.e when removing the scrollbar).
     */
    if (container->details->size_allocation_count_id == 0)
    {
        container->details->size_allocation_count_id =
            g_idle_add_full (G_PRIORITY_HIGH,
                             clear_size_allocation_count,
                             container, NULL);
    }
    container->details->size_allocation_count++;
    if (container->details->size_allocation_count > 2 &&
        allocation->width >= wid_allocation.width)
    {
        need_layout_redone = FALSE;
    }

    GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->size_allocate (widget, allocation);

    container->details->has_been_allocated = TRUE;

    if (need_layout_redone)
    {
        redo_layout (container);
    }
}

static GtkSizeRequestMode
get_request_mode (GtkWidget *widget)
{
    /* Don't trade size at all, since we get whatever we get anyway. */
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

/* We need to implement these since the GtkScrolledWindow uses them
 *  to guess whether to show scrollbars or not, and if we don't report
 *  anything it'll tend to get it wrong causing double calls
 *  to size_allocate (at different sizes) during its size allocation. */
static void
get_prefered_width (GtkWidget *widget,
                    gint      *minimum_size,
                    gint      *natural_size)
{
    EelCanvasGroup *root;
    double x1, x2;
    int cx1, cx2;
    int width;

    root = eel_canvas_root (EEL_CANVAS (widget));
    eel_canvas_item_get_bounds (EEL_CANVAS_ITEM (root),
                                &x1, NULL, &x2, NULL);
    eel_canvas_w2c (EEL_CANVAS (widget), x1, 0, &cx1, NULL);
    eel_canvas_w2c (EEL_CANVAS (widget), x2, 0, &cx2, NULL);

    width = cx2 - cx1;
    if (natural_size)
    {
        *natural_size = width;
    }
    if (minimum_size)
    {
        *minimum_size = width;
    }
}

static void
get_prefered_height (GtkWidget *widget,
                     gint      *minimum_size,
                     gint      *natural_size)
{
    EelCanvasGroup *root;
    double y1, y2;
    int cy1, cy2;
    int height;

    root = eel_canvas_root (EEL_CANVAS (widget));
    eel_canvas_item_get_bounds (EEL_CANVAS_ITEM (root),
                                NULL, &y1, NULL, &y2);
    eel_canvas_w2c (EEL_CANVAS (widget), 0, y1, NULL, &cy1);
    eel_canvas_w2c (EEL_CANVAS (widget), 0, y2, NULL, &cy2);

    height = cy2 - cy1;
    if (natural_size)
    {
        *natural_size = height;
    }
    if (minimum_size)
    {
        *minimum_size = height;
    }
}

static void
realize (GtkWidget *widget)
{
    GtkAdjustment *vadj, *hadj;
    NautilusCanvasContainer *container;

    GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->realize (widget);

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    /* Set up DnD.  */
    nautilus_canvas_dnd_init (container);

    hadj = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (widget));
    g_signal_connect (hadj, "value-changed",
                      G_CALLBACK (handle_hadjustment_changed), widget);

    vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (widget));
    g_signal_connect (vadj, "value-changed",
                      G_CALLBACK (handle_vadjustment_changed), widget);
}

static void
unrealize (GtkWidget *widget)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    nautilus_canvas_dnd_fini (container);

    GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->unrealize (widget);
}

static void
nautilus_canvas_container_request_update_all_internal (NautilusCanvasContainer *container,
                                                       gboolean                 invalidate_labels)
{
    GList *node;
    NautilusCanvasIcon *icon;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    for (node = container->details->icons; node != NULL; node = node->next)
    {
        icon = node->data;

        if (invalidate_labels)
        {
            nautilus_canvas_item_invalidate_label (icon->item);
        }

        nautilus_canvas_container_update_icon (container, icon);
    }

    container->details->needs_resort = TRUE;
    redo_layout (container);
}

static void
style_updated (GtkWidget *widget)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->style_updated (widget);

    if (gtk_widget_get_realized (widget))
    {
        nautilus_canvas_container_request_update_all_internal (container, TRUE);
    }
}

static gboolean
button_press_event (GtkWidget      *widget,
                    GdkEventButton *event)
{
    NautilusCanvasContainer *container;
    gboolean selection_changed;
    gboolean return_value;
    gboolean clicked_on_icon;

    container = NAUTILUS_CANVAS_CONTAINER (widget);
    container->details->button_down_time = event->time;

    /* Forget about the old keyboard selection now that we've started mousing. */
    clear_keyboard_rubberband_start (container);

    if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS)
    {
        /* We use our own double-click detection. */
        return TRUE;
    }

    /* Invoke the canvas event handler and see if an item picks up the event. */
    clicked_on_icon = GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->button_press_event (widget, event);

    if (!gtk_widget_has_focus (widget))
    {
        gtk_widget_grab_focus (widget);
    }

    if (clicked_on_icon)
    {
        return TRUE;
    }

    clear_focus (container);

    if (event->button == DRAG_BUTTON &&
        event->type == GDK_BUTTON_PRESS)
    {
        /* Clear the last click icon for double click */
        container->details->double_click_icon[1] = container->details->double_click_icon[0];
        container->details->double_click_icon[0] = NULL;
    }

    /* Button 1 does rubber banding. */
    if (event->button == RUBBERBAND_BUTTON)
    {
        if (!button_event_modifies_selection (event))
        {
            selection_changed = unselect_all (container);
            if (selection_changed)
            {
                g_signal_emit (container,
                               signals[SELECTION_CHANGED], 0);
            }
        }

        start_rubberbanding (container, event);
        return TRUE;
    }

    /* Prevent multi-button weirdness such as bug 6181 */
    if (container->details->rubberband_info.active)
    {
        return TRUE;
    }

    /* Button 2 may be passed to the window manager. */
    if (event->button == MIDDLE_BUTTON)
    {
        selection_changed = unselect_all (container);
        if (selection_changed)
        {
            g_signal_emit (container, signals[SELECTION_CHANGED], 0);
        }
        g_signal_emit (widget, signals[MIDDLE_CLICK], 0, event);
        return TRUE;
    }

    /* Button 3 does a contextual menu. */
    if (event->button == CONTEXTUAL_MENU_BUTTON)
    {
        selection_changed = unselect_all (container);
        if (selection_changed)
        {
            g_signal_emit (container, signals[SELECTION_CHANGED], 0);
        }
        g_signal_emit (widget, signals[CONTEXT_CLICK_BACKGROUND], 0, event);
        return TRUE;
    }

    /* Otherwise, we emit a button_press message. */
    g_signal_emit (widget,
                   signals[BUTTON_PRESS], 0, event,
                   &return_value);
    return return_value;
}

static void
nautilus_canvas_container_did_not_drag (NautilusCanvasContainer *container,
                                        GdkEventButton          *event)
{
    NautilusCanvasContainerDetails *details;
    gboolean selection_changed;
    static gint64 last_click_time = 0;
    static gint click_count = 0;
    gint double_click_time;
    gint64 current_time;

    details = container->details;

    if (details->icon_selected_on_button_down &&
        ((event->state & GDK_CONTROL_MASK) != 0 ||
         (event->state & GDK_SHIFT_MASK) == 0))
    {
        if (button_event_modifies_selection (event))
        {
            details->range_selection_base_icon = NULL;
            icon_toggle_selected (container, details->drag_icon);
            g_signal_emit (container,
                           signals[SELECTION_CHANGED], 0);
        }
        else
        {
            details->range_selection_base_icon = details->drag_icon;
            selection_changed = select_one_unselect_others
                                    (container, details->drag_icon);

            if (selection_changed)
            {
                g_signal_emit (container,
                               signals[SELECTION_CHANGED], 0);
            }
        }
    }

    if (details->drag_icon != NULL &&
        (details->single_click_mode ||
         event->button == MIDDLE_BUTTON))
    {
        /* Determine click count */
        g_object_get (G_OBJECT (gtk_widget_get_settings (GTK_WIDGET (container))),
                      "gtk-double-click-time", &double_click_time,
                      NULL);
        current_time = g_get_monotonic_time ();
        if (current_time - last_click_time < double_click_time * 1000)
        {
            click_count++;
        }
        else
        {
            click_count = 0;
        }

        /* Stash time for next compare */
        last_click_time = current_time;

        /* If single-click mode, activate the selected icons, unless modifying
         * the selection or pressing for a very long time, or double clicking.
         */


        if (click_count == 0 &&
            event->time - details->button_down_time < MAX_CLICK_TIME &&
            !button_event_modifies_selection (event))
        {
            /* It's a tricky UI issue whether this should activate
             * just the clicked item (as if it were a link), or all
             * the selected items (as if you were issuing an "activate
             * selection" command). For now, we're trying the activate
             * entire selection version to see how it feels. Note that
             * NautilusList goes the other way because its "links" seem
             * much more link-like.
             */
            if (event->button == MIDDLE_BUTTON)
            {
                activate_selected_items_alternate (container, NULL);
            }
            else
            {
                activate_selected_items (container);
            }
        }
    }
}

static gboolean
clicked_within_double_click_interval (NautilusCanvasContainer *container)
{
    static gint64 last_click_time = 0;
    static gint click_count = 0;
    gint double_click_time;
    gint64 current_time;

    /* Determine click count */
    g_object_get (G_OBJECT (gtk_widget_get_settings (GTK_WIDGET (container))),
                  "gtk-double-click-time", &double_click_time,
                  NULL);
    current_time = g_get_monotonic_time ();
    if (current_time - last_click_time < double_click_time * 1000)
    {
        click_count++;
    }
    else
    {
        click_count = 0;
    }

    /* Stash time for next compare */
    last_click_time = current_time;

    /* Only allow double click */
    if (click_count == 1)
    {
        click_count = 0;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static void
clear_drag_state (NautilusCanvasContainer *container)
{
    container->details->drag_icon = NULL;
    container->details->drag_state = DRAG_STATE_INITIAL;
}

static gboolean
button_release_event (GtkWidget      *widget,
                      GdkEventButton *event)
{
    NautilusCanvasContainer *container;
    NautilusCanvasContainerDetails *details;

    container = NAUTILUS_CANVAS_CONTAINER (widget);
    details = container->details;

    if (event->button == RUBBERBAND_BUTTON && details->rubberband_info.active)
    {
        stop_rubberbanding (container, event);
        return TRUE;
    }

    if (event->button == details->drag_button)
    {
        details->drag_button = 0;

        switch (details->drag_state)
        {
            case DRAG_STATE_MOVE_OR_COPY:
            {
                if (!details->drag_started)
                {
                    nautilus_canvas_container_did_not_drag (container, event);
                }
                else
                {
                    nautilus_canvas_dnd_end_drag (container);
                    DEBUG ("Ending drag from canvas container");
                }
            }
            break;

            default:
            {
            }
            break;
        }

        clear_drag_state (container);
        return TRUE;
    }

    return GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->button_release_event (widget, event);
}

static int
motion_notify_event (GtkWidget      *widget,
                     GdkEventMotion *event)
{
    NautilusCanvasContainer *container;
    NautilusCanvasContainerDetails *details;
    double world_x, world_y;
    int canvas_x, canvas_y;
    GdkDragAction actions;

    container = NAUTILUS_CANVAS_CONTAINER (widget);
    details = container->details;

    if (details->drag_button != 0)
    {
        switch (details->drag_state)
        {
            case DRAG_STATE_MOVE_OR_COPY:
            {
                if (details->drag_started)
                {
                    break;
                }

                eel_canvas_window_to_world
                    (EEL_CANVAS (container), event->x, event->y, &world_x, &world_y);

                if (gtk_drag_check_threshold (widget,
                                              details->drag_x,
                                              details->drag_y,
                                              world_x,
                                              world_y))
                {
                    details->drag_started = TRUE;
                    details->drag_state = DRAG_STATE_MOVE_OR_COPY;

                    eel_canvas_w2c (EEL_CANVAS (container),
                                    details->drag_x,
                                    details->drag_y,
                                    &canvas_x,
                                    &canvas_y);

                    actions = GDK_ACTION_COPY
                              | GDK_ACTION_MOVE
                              | GDK_ACTION_LINK
                              | GDK_ACTION_ASK;

                    nautilus_canvas_dnd_begin_drag (container,
                                                    actions,
                                                    details->drag_button,
                                                    event,
                                                    canvas_x,
                                                    canvas_y);
                    DEBUG ("Beginning drag from canvas container");
                }
            }
            break;

            default:
            {
            }
            break;
        }
    }

    return GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->motion_notify_event (widget, event);
}

static void
nautilus_canvas_container_get_icon_text (NautilusCanvasContainer  *container,
                                         NautilusCanvasIconData   *data,
                                         char                    **editable_text,
                                         char                    **additional_text,
                                         gboolean                  include_invisible)
{
    NautilusCanvasContainerClass *klass;

    klass = NAUTILUS_CANVAS_CONTAINER_GET_CLASS (container);
    g_assert (klass->get_icon_text != NULL);

    klass->get_icon_text (container, data, editable_text, additional_text, include_invisible);
}

static gboolean
handle_popups (NautilusCanvasContainer *container,
               GdkEvent                *event,
               const char              *signal)
{
    /* ensure we clear the drag state before showing the menu */
    clear_drag_state (container);

    g_signal_emit_by_name (container, signal, event);

    return TRUE;
}

static int
key_press_event (GtkWidget   *widget,
                 GdkEventKey *event)
{
    NautilusCanvasContainer *container;
    gboolean handled;

    container = NAUTILUS_CANVAS_CONTAINER (widget);
    handled = FALSE;

    switch (event->keyval)
    {
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
        {
            keyboard_home (container, event);
            handled = TRUE;
        }
        break;

        case GDK_KEY_End:
        case GDK_KEY_KP_End:
        {
            keyboard_end (container, event);
            handled = TRUE;
        }
        break;

        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
        {
            /* Don't eat Alt-Left, as that is used for history browsing */
            if ((event->state & GDK_MOD1_MASK) == 0)
            {
                keyboard_left (container, event);
                handled = TRUE;
            }
        }
        break;

        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
        {
            /* Don't eat Alt-Up, as that is used for alt-shift-Up */
            if ((event->state & GDK_MOD1_MASK) == 0)
            {
                keyboard_up (container, event);
                handled = TRUE;
            }
        }
        break;

        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
        {
            /* Don't eat Alt-Right, as that is used for history browsing */
            if ((event->state & GDK_MOD1_MASK) == 0)
            {
                keyboard_right (container, event);
                handled = TRUE;
            }
        }
        break;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
        {
            /* Don't eat Alt-Down, as that is used for Open */
            if ((event->state & GDK_MOD1_MASK) == 0)
            {
                keyboard_down (container, event);
                handled = TRUE;
            }
        }
        break;

        case GDK_KEY_space:
        {
            keyboard_space (container, event);
            handled = TRUE;
        }
        break;

        case GDK_KEY_F10:
        {
            /* handle Ctrl+F10 because we want to display the
             * background popup even if something is selected.
             * The other cases are handled by the "popup-menu" GtkWidget signal.
             */
            if (event->state & GDK_CONTROL_MASK)
            {
                handled = handle_popups (container, (GdkEvent *) event,
                                         "context_click_background");
            }
        }
        break;

        case GDK_KEY_v:
        {
            /* Eat Control + v to not enable type ahead */
            if ((event->state & GDK_CONTROL_MASK) != 0)
            {
                handled = TRUE;
            }
        }
        break;

        default:
        {
        }
        break;
    }

    if (!handled)
    {
        handled = GTK_WIDGET_CLASS (nautilus_canvas_container_parent_class)->key_press_event (widget, event);
    }

    return handled;
}

static void
grab_notify_cb  (GtkWidget *widget,
                 gboolean   was_grabbed)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    if (container->details->rubberband_info.active &&
        !was_grabbed)
    {
        /* we got a (un)grab-notify during rubberband.
         * This happens when a new modal dialog shows
         * up (e.g. authentication or an error). Stop
         * the rubberbanding so that we can handle the
         * dialog. */
        stop_rubberbanding (container, NULL);
    }
}

static void
text_ellipsis_limit_changed_container_callback (gpointer callback_data)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (callback_data);
    invalidate_label_sizes (container);
    schedule_redo_layout (container);
}

static GObject *
nautilus_canvas_container_constructor (GType                  type,
                                       guint                  n_construct_params,
                                       GObjectConstructParam *construct_params)
{
    NautilusCanvasContainer *container;
    GObject *object;

    object = G_OBJECT_CLASS (nautilus_canvas_container_parent_class)->constructor
                 (type,
                 n_construct_params,
                 construct_params);

    container = NAUTILUS_CANVAS_CONTAINER (object);
    g_signal_connect_swapped (nautilus_icon_view_preferences,
                              "changed::" NAUTILUS_PREFERENCES_ICON_VIEW_TEXT_ELLIPSIS_LIMIT,
                              G_CALLBACK (text_ellipsis_limit_changed_container_callback),
                              container);

    return object;
}

/* Initialization.  */

static void
nautilus_canvas_container_class_init (NautilusCanvasContainerClass *class)
{
    GtkWidgetClass *widget_class;

    G_OBJECT_CLASS (class)->constructor = nautilus_canvas_container_constructor;
    G_OBJECT_CLASS (class)->finalize = finalize;

    /* Signals.  */

    signals[SELECTION_CHANGED]
        = g_signal_new ("selection-changed",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         selection_changed),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0);
    signals[BUTTON_PRESS]
        = g_signal_new ("button-press",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         button_press),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_BOOLEAN, 1,
                        GDK_TYPE_EVENT);
    signals[ACTIVATE]
        = g_signal_new ("activate",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         activate),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__POINTER,
                        G_TYPE_NONE, 1,
                        G_TYPE_POINTER);
    signals[ACTIVATE_ALTERNATE]
        = g_signal_new ("activate-alternate",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         activate_alternate),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__POINTER,
                        G_TYPE_NONE, 1,
                        G_TYPE_POINTER);
    signals[ACTIVATE_PREVIEWER]
        = g_signal_new ("activate-previewer",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         activate_previewer),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_NONE, 2,
                        G_TYPE_POINTER, G_TYPE_POINTER);
    signals[CONTEXT_CLICK_SELECTION]
        = g_signal_new ("context-click-selection",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         context_click_selection),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__POINTER,
                        G_TYPE_NONE, 1,
                        G_TYPE_POINTER);
    signals[CONTEXT_CLICK_BACKGROUND]
        = g_signal_new ("context-click-background",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         context_click_background),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__POINTER,
                        G_TYPE_NONE, 1,
                        G_TYPE_POINTER);
    signals[MIDDLE_CLICK]
        = g_signal_new ("middle-click",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         middle_click),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__POINTER,
                        G_TYPE_NONE, 1,
                        G_TYPE_POINTER);
    signals[GET_ICON_URI]
        = g_signal_new ("get-icon-uri",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         get_icon_uri),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_STRING, 1,
                        G_TYPE_POINTER);
    signals[GET_ICON_ACTIVATION_URI]
        = g_signal_new ("get-icon-activation-uri",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         get_icon_activation_uri),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_STRING, 1,
                        G_TYPE_POINTER);
    signals[GET_ICON_DROP_TARGET_URI]
        = g_signal_new ("get-icon-drop-target-uri",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         get_icon_drop_target_uri),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_STRING, 1,
                        G_TYPE_POINTER);
    signals[MOVE_COPY_ITEMS]
        = g_signal_new ("move-copy-items",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         move_copy_items),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_NONE, 3,
                        G_TYPE_POINTER,
                        G_TYPE_POINTER,
                        GDK_TYPE_DRAG_ACTION);
    signals[HANDLE_NETSCAPE_URL]
        = g_signal_new ("handle-netscape-url",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         handle_netscape_url),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_NONE, 3,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        GDK_TYPE_DRAG_ACTION);
    signals[HANDLE_URI_LIST]
        = g_signal_new ("handle-uri-list",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         handle_uri_list),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_NONE, 3,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        GDK_TYPE_DRAG_ACTION);
    signals[HANDLE_TEXT]
        = g_signal_new ("handle-text",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         handle_text),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_NONE, 3,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        GDK_TYPE_DRAG_ACTION);
    signals[HANDLE_RAW]
        = g_signal_new ("handle-raw",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         handle_raw),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_NONE, 5,
                        G_TYPE_POINTER,
                        G_TYPE_INT,
                        G_TYPE_STRING,
                        G_TYPE_STRING,
                        GDK_TYPE_DRAG_ACTION);
    signals[HANDLE_HOVER] =
        g_signal_new ("handle-hover",
                      G_TYPE_FROM_CLASS (class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                       handle_hover),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
    signals[GET_CONTAINER_URI]
        = g_signal_new ("get-container-uri",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         get_container_uri),
                        NULL, NULL,
                        g_cclosure_marshal_generic,
                        G_TYPE_STRING, 0);
    signals[BAND_SELECT_STARTED]
        = g_signal_new ("band-select-started",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         band_select_started),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0);
    signals[BAND_SELECT_ENDED]
        = g_signal_new ("band-select-ended",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         band_select_ended),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0);
    signals[ICON_ADDED]
        = g_signal_new ("icon-added",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         icon_added),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__POINTER,
                        G_TYPE_NONE, 1, G_TYPE_POINTER);
    signals[ICON_REMOVED]
        = g_signal_new ("icon-removed",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         icon_removed),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__POINTER,
                        G_TYPE_NONE, 1, G_TYPE_POINTER);

    signals[CLEARED]
        = g_signal_new ("cleared",
                        G_TYPE_FROM_CLASS (class),
                        G_SIGNAL_RUN_LAST,
                        G_STRUCT_OFFSET (NautilusCanvasContainerClass,
                                         cleared),
                        NULL, NULL,
                        g_cclosure_marshal_VOID__VOID,
                        G_TYPE_NONE, 0);

    /* GtkWidget class.  */

    widget_class = GTK_WIDGET_CLASS (class);
    widget_class->destroy = destroy;
    widget_class->size_allocate = size_allocate;
    widget_class->get_request_mode = get_request_mode;
    widget_class->get_preferred_width = get_prefered_width;
    widget_class->get_preferred_height = get_prefered_height;
    widget_class->realize = realize;
    widget_class->unrealize = unrealize;
    widget_class->button_press_event = button_press_event;
    widget_class->button_release_event = button_release_event;
    widget_class->motion_notify_event = motion_notify_event;
    widget_class->key_press_event = key_press_event;
    widget_class->style_updated = style_updated;
    widget_class->grab_notify = grab_notify_cb;

    gtk_widget_class_set_accessible_type (widget_class, nautilus_canvas_container_accessible_get_type ());
}

static void
update_selected (NautilusCanvasContainer *container)
{
    GList *node;
    NautilusCanvasIcon *icon;

    for (node = container->details->icons; node != NULL; node = node->next)
    {
        icon = node->data;
        if (icon->is_selected)
        {
            eel_canvas_item_request_update (EEL_CANVAS_ITEM (icon->item));
        }
    }
}

static void
handle_has_focus_changed (GObject    *object,
                          GParamSpec *pspec,
                          gpointer    user_data)
{
    update_selected (NAUTILUS_CANVAS_CONTAINER (object));
}

static void
handle_scale_factor_changed (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
    nautilus_canvas_container_request_update_all_internal (NAUTILUS_CANVAS_CONTAINER (object),
                                                           TRUE);
}



static int text_ellipsis_limits[NAUTILUS_CANVAS_ZOOM_LEVEL_N_ENTRIES];

static gboolean
get_text_ellipsis_limit_for_zoom (char       **strs,
                                  const char  *zoom_level,
                                  int         *limit)
{
    char **p;
    char *str;
    gboolean success;

    success = FALSE;

    /* default */
    *limit = 3;

    if (zoom_level != NULL)
    {
        str = g_strdup_printf ("%s:%%d", zoom_level);
    }
    else
    {
        str = g_strdup ("%d");
    }

    if (strs != NULL)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        for (p = strs; *p != NULL; p++)
        {
            if (sscanf (*p, str, limit))
            {
                success = TRUE;
            }
        }
#pragma GCC diagnostic pop
    }

    g_free (str);

    return success;
}

static const char *zoom_level_names[] =
{
    "small",
    "standard",
    "large",
};

static void
text_ellipsis_limit_changed_callback (gpointer callback_data)
{
    char **pref;
    unsigned int i;
    int one_limit;

    pref = g_settings_get_strv (nautilus_icon_view_preferences,
                                NAUTILUS_PREFERENCES_ICON_VIEW_TEXT_ELLIPSIS_LIMIT);

    /* set default */
    get_text_ellipsis_limit_for_zoom (pref, NULL, &one_limit);
    for (i = 0; i < NAUTILUS_CANVAS_ZOOM_LEVEL_N_ENTRIES; i++)
    {
        text_ellipsis_limits[i] = one_limit;
    }

    /* override for each zoom level */
    for (i = 0; i < G_N_ELEMENTS (zoom_level_names); i++)
    {
        if (get_text_ellipsis_limit_for_zoom (pref,
                                              zoom_level_names[i],
                                              &one_limit))
        {
            text_ellipsis_limits[i] = one_limit;
        }
    }

    g_strfreev (pref);
}

static void
nautilus_canvas_container_init (NautilusCanvasContainer *container)
{
    NautilusCanvasContainerDetails *details;
    static gboolean setup_prefs = FALSE;

    details = g_new0 (NautilusCanvasContainerDetails, 1);

    details->icon_set = g_hash_table_new (g_direct_hash, g_direct_equal);
    details->zoom_level = NAUTILUS_CANVAS_ZOOM_LEVEL_STANDARD;

    container->details = details;

    g_signal_connect (container, "notify::has-focus",
                      G_CALLBACK (handle_has_focus_changed), NULL);
    g_signal_connect (container, "notify::scale-factor",
                      G_CALLBACK (handle_scale_factor_changed), NULL);

    if (!setup_prefs)
    {
        g_signal_connect_swapped (nautilus_icon_view_preferences,
                                  "changed::" NAUTILUS_PREFERENCES_ICON_VIEW_TEXT_ELLIPSIS_LIMIT,
                                  G_CALLBACK (text_ellipsis_limit_changed_callback),
                                  NULL);
        text_ellipsis_limit_changed_callback (NULL);

        setup_prefs = TRUE;
    }
}

typedef struct
{
    NautilusCanvasContainer *container;
    GdkEventButton *event;
} ContextMenuParameters;

static gboolean
handle_canvas_double_click (NautilusCanvasContainer *container,
                            NautilusCanvasIcon      *icon,
                            GdkEventButton          *event)
{
    NautilusCanvasContainerDetails *details;

    if (event->button != DRAG_BUTTON)
    {
        return FALSE;
    }

    details = container->details;

    if (!details->single_click_mode &&
        clicked_within_double_click_interval (container) &&
        details->double_click_icon[0] == details->double_click_icon[1] &&
        details->double_click_button[0] == details->double_click_button[1])
    {
        details->double_clicked = TRUE;
        return TRUE;
    }

    return FALSE;
}

/* NautilusCanvasIcon event handling.  */

/* Conceptually, pressing button 1 together with CTRL or SHIFT toggles
 * selection of a single icon without affecting the other icons;
 * without CTRL or SHIFT, it selects a single icon and un-selects all
 * the other icons.  But in this latter case, the de-selection should
 * only happen when the button is released if the icon is already
 * selected, because the user might select multiple icons and drag all
 * of them by doing a simple click-drag.
 */

static gboolean
handle_canvas_button_press (NautilusCanvasContainer *container,
                            NautilusCanvasIcon      *icon,
                            GdkEventButton          *event)
{
    NautilusCanvasContainerDetails *details;

    details = container->details;

    if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS)
    {
        return TRUE;
    }

    if (event->button != DRAG_BUTTON
        && event->button != CONTEXTUAL_MENU_BUTTON
        && event->button != DRAG_MENU_BUTTON)
    {
        return TRUE;
    }

    if ((event->button == DRAG_BUTTON) &&
        event->type == GDK_BUTTON_PRESS)
    {
        /* The next double click has to be on this icon */
        details->double_click_icon[1] = details->double_click_icon[0];
        details->double_click_icon[0] = icon;

        details->double_click_button[1] = details->double_click_button[0];
        details->double_click_button[0] = event->button;
    }

    if (handle_canvas_double_click (container, icon, event))
    {
        /* Double clicking does not trigger a D&D action. */
        details->drag_button = 0;
        details->drag_icon = NULL;
        return TRUE;
    }

    if (event->button == DRAG_BUTTON
        || event->button == DRAG_MENU_BUTTON)
    {
        details->drag_button = event->button;
        details->drag_icon = icon;
        details->drag_x = event->x;
        details->drag_y = event->y;
        details->drag_state = DRAG_STATE_MOVE_OR_COPY;
        details->drag_started = FALSE;
    }

    /* Modify the selection as appropriate. Selection is modified
     * the same way for contextual menu as it would be without.
     */
    details->icon_selected_on_button_down = icon->is_selected;

    if ((event->button == DRAG_BUTTON || event->button == MIDDLE_BUTTON) &&
        (event->state & GDK_SHIFT_MASK) != 0)
    {
        NautilusCanvasIcon *start_icon;

        set_focus (container, icon, FALSE);

        start_icon = details->range_selection_base_icon;
        if (start_icon == NULL || !start_icon->is_selected)
        {
            start_icon = icon;
            details->range_selection_base_icon = icon;
        }
        if (select_range (container, start_icon, icon,
                          (event->state & GDK_CONTROL_MASK) == 0))
        {
            g_signal_emit (container,
                           signals[SELECTION_CHANGED], 0);
        }
    }
    else if (!details->icon_selected_on_button_down)
    {
        set_focus (container, icon, FALSE);

        details->range_selection_base_icon = icon;
        if (button_event_modifies_selection (event))
        {
            icon_toggle_selected (container, icon);
            g_signal_emit (container,
                           signals[SELECTION_CHANGED], 0);
        }
        else
        {
            select_one_unselect_others (container, icon);
            g_signal_emit (container,
                           signals[SELECTION_CHANGED], 0);
        }
    }

    if (event->button == CONTEXTUAL_MENU_BUTTON)
    {
        clear_drag_state (container);

        g_signal_emit (container,
                       signals[CONTEXT_CLICK_SELECTION], 0,
                       event);
    }


    return TRUE;
}

static int
item_event_callback (EelCanvasItem *item,
                     GdkEvent      *event,
                     gpointer       data)
{
    NautilusCanvasContainer *container;
    NautilusCanvasIcon *icon;
    GdkEventButton *event_button;

    container = NAUTILUS_CANVAS_CONTAINER (data);

    icon = NAUTILUS_CANVAS_ITEM (item)->user_data;
    g_assert (icon != NULL);

    event_button = &event->button;

    switch (event->type)
    {
        case GDK_MOTION_NOTIFY:
        {
            return FALSE;
        }

        case GDK_BUTTON_PRESS:
        {
            container->details->double_clicked = FALSE;
            if (handle_canvas_button_press (container, icon, event_button))
            {
                /* Stop the event from being passed along further. Returning
                 * TRUE ain't enough.
                 */
                return TRUE;
            }
            return FALSE;
        }

        case GDK_BUTTON_RELEASE:
        {
            if (event_button->button == DRAG_BUTTON
                && container->details->double_clicked)
            {
                if (!button_event_modifies_selection (event_button))
                {
                    activate_selected_items (container);
                }
                else if ((event_button->state & GDK_CONTROL_MASK) == 0 &&
                         (event_button->state & GDK_SHIFT_MASK) != 0)
                {
                    activate_selected_items_alternate (container, icon);
                }
            }
            /* fall through */
        }

        default:
        {
            container->details->double_clicked = FALSE;
            return FALSE;
        }
        break;
    }
}

GtkWidget *
nautilus_canvas_container_new (void)
{
    return gtk_widget_new (NAUTILUS_TYPE_CANVAS_CONTAINER, NULL);
}

/* Clear all of the icons in the container. */
void
nautilus_canvas_container_clear (NautilusCanvasContainer *container)
{
    NautilusCanvasContainerDetails *details;
    GList *p;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    details = container->details;

    if (details->icons == NULL)
    {
        return;
    }

    clear_focus (container);
    clear_keyboard_rubberband_start (container);
    unschedule_keyboard_icon_reveal (container);
    set_pending_icon_to_reveal (container, NULL);
    details->drop_target = NULL;

    for (p = details->icons; p != NULL; p = p->next)
    {
        icon_free (p->data);
    }
    g_list_free (details->icons);
    details->icons = NULL;
    g_list_free (details->new_icons);
    details->new_icons = NULL;
    g_list_free (details->selection);
    details->selection = NULL;

    g_hash_table_destroy (details->icon_set);
    details->icon_set = g_hash_table_new (g_direct_hash, g_direct_equal);

    nautilus_canvas_container_update_scroll_region (container);
}

gboolean
nautilus_canvas_container_is_empty (NautilusCanvasContainer *container)
{
    return container->details->icons == NULL;
}

NautilusCanvasIconData *
nautilus_canvas_container_get_first_visible_icon (NautilusCanvasContainer *container)
{
    GList *l;
    NautilusCanvasIcon *icon, *best_icon;
    double x, y;
    double x1, y1, x2, y2;
    double *pos, best_pos;
    double hadj_v, vadj_v, h_page_size;
    gboolean better_icon;
    gboolean compare_lt;

    hadj_v = gtk_adjustment_get_value (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container)));
    vadj_v = gtk_adjustment_get_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container)));
    h_page_size = gtk_adjustment_get_page_size (gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container)));

    if (nautilus_canvas_container_is_layout_rtl (container))
    {
        x = hadj_v + h_page_size - ICON_PAD_LEFT - 1;
        y = vadj_v;
    }
    else
    {
        x = hadj_v;
        y = vadj_v;
    }

    eel_canvas_c2w (EEL_CANVAS (container),
                    x, y,
                    &x, &y);

    l = container->details->icons;
    best_icon = NULL;
    best_pos = 0;
    while (l != NULL)
    {
        icon = l->data;

        if (icon_is_positioned (icon))
        {
            eel_canvas_item_get_bounds (EEL_CANVAS_ITEM (icon->item),
                                        &x1, &y1, &x2, &y2);

            compare_lt = FALSE;
            pos = &y1;
            better_icon = y2 > y + ICON_PAD_TOP;
            if (better_icon)
            {
                if (best_icon == NULL)
                {
                    better_icon = TRUE;
                }
                else if (compare_lt)
                {
                    better_icon = best_pos < *pos;
                }
                else
                {
                    better_icon = best_pos > *pos;
                }

                if (better_icon)
                {
                    best_icon = icon;
                    best_pos = *pos;
                }
            }
        }

        l = l->next;
    }

    return best_icon ? best_icon->data : NULL;
}

NautilusCanvasIconData *
nautilus_canvas_container_get_focused_icon (NautilusCanvasContainer *container)
{
    NautilusCanvasIcon *icon;

    icon = container->details->focus;

    if (icon != NULL)
    {
        return icon->data;
    }

    return NULL;
}

/* puts the icon at the top of the screen */
void
nautilus_canvas_container_scroll_to_canvas (NautilusCanvasContainer *container,
                                            NautilusCanvasIconData  *data)
{
    GList *l;
    NautilusCanvasIcon *icon;
    GtkAdjustment *vadj;
    EelIRect bounds;
    GtkAllocation allocation;

    vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container));
    gtk_widget_get_allocation (GTK_WIDGET (container), &allocation);

    /* We need to force a relayout now if there are updates queued
     * since we need the final positions */
    nautilus_canvas_container_layout_now (container);

    l = container->details->icons;
    while (l != NULL)
    {
        icon = l->data;

        if (icon->data == data &&
            icon_is_positioned (icon))
        {
            /* ensure that we reveal the entire row/column */
            icon_get_row_and_column_bounds (container, icon, &bounds);

            gtk_adjustment_set_value (vadj, bounds.y0);
        }

        l = l->next;
    }
}

/* Call a function for all the icons. */
typedef struct
{
    NautilusCanvasCallback callback;
    gpointer callback_data;
} CallbackAndData;

static void
call_canvas_callback (gpointer data,
                      gpointer callback_data)
{
    NautilusCanvasIcon *icon;
    CallbackAndData *callback_and_data;

    icon = data;
    callback_and_data = callback_data;
    (*callback_and_data->callback)(icon->data, callback_and_data->callback_data);
}

void
nautilus_canvas_container_for_each (NautilusCanvasContainer *container,
                                    NautilusCanvasCallback   callback,
                                    gpointer                 callback_data)
{
    CallbackAndData callback_and_data;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    callback_and_data.callback = callback;
    callback_and_data.callback_data = callback_data;

    g_list_foreach (container->details->icons,
                    call_canvas_callback, &callback_and_data);
}

static int
selection_changed_at_idle_callback (gpointer data)
{
    NautilusCanvasContainer *container;

    container = NAUTILUS_CANVAS_CONTAINER (data);

    g_signal_emit (container,
                   signals[SELECTION_CHANGED], 0);

    container->details->selection_changed_id = 0;
    return FALSE;
}

/* utility routine to remove a single icon from the container */

static void
icon_destroy (NautilusCanvasContainer *container,
              NautilusCanvasIcon      *icon)
{
    NautilusCanvasContainerDetails *details;
    gboolean was_selected;
    NautilusCanvasIcon *icon_to_focus;
    GList *item;

    details = container->details;

    item = g_list_find (details->icons, icon);
    item = item->next ? item->next : item->prev;
    icon_to_focus = (item != NULL) ? item->data : NULL;

    details->icons = g_list_remove (details->icons, icon);
    details->new_icons = g_list_remove (details->new_icons, icon);
    details->selection = g_list_remove (details->selection, icon->data);
    g_hash_table_remove (details->icon_set, icon->data);

    was_selected = icon->is_selected;

    if (details->focus == icon ||
        details->focus == NULL)
    {
        if (icon_to_focus != NULL)
        {
            set_focus (container, icon_to_focus, TRUE);
        }
        else
        {
            clear_focus (container);
        }
    }

    if (details->keyboard_rubberband_start == icon)
    {
        clear_keyboard_rubberband_start (container);
    }

    if (details->keyboard_icon_to_reveal == icon)
    {
        unschedule_keyboard_icon_reveal (container);
    }
    if (details->drag_icon == icon)
    {
        clear_drag_state (container);
    }
    if (details->drop_target == icon)
    {
        details->drop_target = NULL;
    }
    if (details->range_selection_base_icon == icon)
    {
        details->range_selection_base_icon = NULL;
    }
    if (details->pending_icon_to_reveal == icon)
    {
        set_pending_icon_to_reveal (container, NULL);
    }

    icon_free (icon);

    if (was_selected)
    {
        /* Coalesce multiple removals causing multiple selection_changed events */
        details->selection_changed_id = g_idle_add (selection_changed_at_idle_callback, container);
    }
}

/* activate any selected items in the container */
static void
activate_selected_items (NautilusCanvasContainer *container)
{
    GList *selection;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    selection = nautilus_canvas_container_get_selection (container);
    if (selection != NULL)
    {
        g_signal_emit (container,
                       signals[ACTIVATE], 0,
                       selection);
    }
    g_list_free (selection);
}

static void
preview_selected_items (NautilusCanvasContainer *container)
{
    GList *selection;
    GArray *locations;
    gint idx;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    selection = nautilus_canvas_container_get_selection (container);
    locations = nautilus_canvas_container_get_selected_icon_locations (container);

    for (idx = 0; idx < locations->len; idx++)
    {
        GdkPoint *point = &(g_array_index (locations, GdkPoint, idx));
        gint scroll_x, scroll_y;

        eel_canvas_get_scroll_offsets (EEL_CANVAS (container),
                                       &scroll_x, &scroll_y);

        point->x -= scroll_x;
        point->y -= scroll_y;
    }

    if (selection != NULL)
    {
        g_signal_emit (container,
                       signals[ACTIVATE_PREVIEWER], 0,
                       selection, locations);
    }
    g_list_free (selection);
    g_array_unref (locations);
}

static void
activate_selected_items_alternate (NautilusCanvasContainer *container,
                                   NautilusCanvasIcon      *icon)
{
    GList *selection;

    g_assert (NAUTILUS_IS_CANVAS_CONTAINER (container));

    if (icon != NULL)
    {
        selection = g_list_prepend (NULL, icon->data);
    }
    else
    {
        selection = nautilus_canvas_container_get_selection (container);
    }
    if (selection != NULL)
    {
        g_signal_emit (container,
                       signals[ACTIVATE_ALTERNATE], 0,
                       selection);
    }
    g_list_free (selection);
}

static NautilusIconInfo *
nautilus_canvas_container_get_icon_images (NautilusCanvasContainer *container,
                                           NautilusCanvasIconData  *data,
                                           int                      size,
                                           gboolean                 for_drag_accept)
{
    NautilusCanvasContainerClass *klass;

    klass = NAUTILUS_CANVAS_CONTAINER_GET_CLASS (container);
    g_assert (klass->get_icon_images != NULL);

    return klass->get_icon_images (container, data, size, for_drag_accept);
}

static void
nautilus_canvas_container_prioritize_thumbnailing (NautilusCanvasContainer *container,
                                                   NautilusCanvasIcon      *icon)
{
    NautilusCanvasContainerClass *klass;

    klass = NAUTILUS_CANVAS_CONTAINER_GET_CLASS (container);
    g_assert (klass->prioritize_thumbnailing != NULL);

    klass->prioritize_thumbnailing (container, icon->data);
}

static void
nautilus_canvas_container_update_visible_icons (NautilusCanvasContainer *container)
{
    GtkAdjustment *vadj, *hadj;
    double min_y, max_y;
    double min_x, max_x;
    double x0, y0, x1, y1;
    GList *node;
    NautilusCanvasIcon *icon;
    gboolean visible;
    GtkAllocation allocation;

    hadj = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (container));
    vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (container));
    gtk_widget_get_allocation (GTK_WIDGET (container), &allocation);

    min_x = gtk_adjustment_get_value (hadj);
    max_x = min_x + allocation.width;

    min_y = gtk_adjustment_get_value (vadj);
    max_y = min_y + allocation.height;

    eel_canvas_c2w (EEL_CANVAS (container),
                    min_x, min_y, &min_x, &min_y);
    eel_canvas_c2w (EEL_CANVAS (container),
                    max_x, max_y, &max_x, &max_y);

    /* Do the iteration in reverse to get the render-order from top to
     * bottom for the prioritized thumbnails.
     */
    for (node = g_list_last (container->details->icons); node != NULL; node = node->prev)
    {
        icon = node->data;

        if (icon_is_positioned (icon))
        {
            eel_canvas_item_get_bounds (EEL_CANVAS_ITEM (icon->item),
                                        &x0,
                                        &y0,
                                        &x1,
                                        &y1);
            eel_canvas_item_i2w (EEL_CANVAS_ITEM (icon->item)->parent,
                                 &x0,
                                 &y0);
            eel_canvas_item_i2w (EEL_CANVAS_ITEM (icon->item)->parent,
                                 &x1,
                                 &y1);

            visible = y1 >= min_y && y0 <= max_y;

            if (visible)
            {
                nautilus_canvas_item_set_is_visible (icon->item, TRUE);
                nautilus_canvas_container_prioritize_thumbnailing (container,
                                                                   icon);
            }
            else
            {
                nautilus_canvas_item_set_is_visible (icon->item, FALSE);
            }
        }
    }
}

static void
handle_vadjustment_changed (GtkAdjustment           *adjustment,
                            NautilusCanvasContainer *container)
{
    nautilus_canvas_container_update_visible_icons (container);
}

static void
handle_hadjustment_changed (GtkAdjustment           *adjustment,
                            NautilusCanvasContainer *container)
{
    nautilus_canvas_container_update_visible_icons (container);
}


void
nautilus_canvas_container_update_icon (NautilusCanvasContainer *container,
                                       NautilusCanvasIcon      *icon)
{
    NautilusCanvasContainerDetails *details;
    guint icon_size;
    guint min_image_size, max_image_size;
    NautilusIconInfo *icon_info;
    GdkPixbuf *pixbuf;
    char *editable_text, *additional_text;

    if (icon == NULL)
    {
        return;
    }

    details = container->details;

    /* compute the maximum size based on the scale factor */
    min_image_size = MINIMUM_IMAGE_SIZE * EEL_CANVAS (container)->pixels_per_unit;
    max_image_size = MAX (MAXIMUM_IMAGE_SIZE * EEL_CANVAS (container)->pixels_per_unit, NAUTILUS_ICON_MAXIMUM_SIZE);

    /* Get the appropriate images for the file. */
    icon_get_size (container, icon, &icon_size);

    icon_size = MAX (icon_size, min_image_size);
    icon_size = MIN (icon_size, max_image_size);

    DEBUG ("Icon size, getting for size %d", icon_size);

    /* Get the icons. */
    icon_info = nautilus_canvas_container_get_icon_images (container, icon->data, icon_size,
                                                           icon == details->drop_target);

    pixbuf = nautilus_icon_info_get_pixbuf (icon_info);
    g_object_unref (icon_info);

    nautilus_canvas_container_get_icon_text (container,
                                             icon->data,
                                             &editable_text,
                                             &additional_text,
                                             FALSE);

    eel_canvas_item_set (EEL_CANVAS_ITEM (icon->item),
                         "editable_text", editable_text,
                         "additional_text", additional_text,
                         "highlighted_for_drop", icon == details->drop_target,
                         NULL);

    nautilus_canvas_item_set_image (icon->item, pixbuf);

    /* Let the pixbufs go. */
    g_object_unref (pixbuf);

    g_free (editable_text);
    g_free (additional_text);
}

static void
finish_adding_icon (NautilusCanvasContainer *container,
                    NautilusCanvasIcon      *icon)
{
    nautilus_canvas_container_update_icon (container, icon);
    eel_canvas_item_show (EEL_CANVAS_ITEM (icon->item));

    g_signal_connect_object (icon->item, "event",
                             G_CALLBACK (item_event_callback), container, 0);

    g_signal_emit (container, signals[ICON_ADDED], 0, icon->data);
}

static gboolean
finish_adding_new_icons (NautilusCanvasContainer *container)
{
    GList *p, *new_icons;

    new_icons = container->details->new_icons;
    container->details->new_icons = NULL;
    container->details->is_populating_container = g_list_length (new_icons) ==
                                                  g_hash_table_size (container->details->icon_set);

    /* Position most icons (not unpositioned manual-layout icons). */
    new_icons = g_list_reverse (new_icons);
    for (p = new_icons; p != NULL; p = p->next)
    {
        finish_adding_icon (container, p->data);
    }
    g_list_free (new_icons);

    return TRUE;
}

/**
 * nautilus_canvas_container_add:
 * @container: A NautilusCanvasContainer
 * @data: Icon data.
 *
 * Add icon to represent @data to container.
 * Returns FALSE if there was already such an icon.
 **/
gboolean
nautilus_canvas_container_add (NautilusCanvasContainer *container,
                               NautilusCanvasIconData  *data)
{
    NautilusCanvasContainerDetails *details;
    NautilusCanvasIcon *icon;
    EelCanvasItem *band, *item;

    g_return_val_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container), FALSE);
    g_return_val_if_fail (data != NULL, FALSE);

    details = container->details;

    if (g_hash_table_lookup (details->icon_set, data) != NULL)
    {
        return FALSE;
    }

    /* Create the new icon, including the canvas item. */
    icon = g_new0 (NautilusCanvasIcon, 1);
    icon->data = data;
    icon->x = ICON_UNPOSITIONED_VALUE;
    icon->y = ICON_UNPOSITIONED_VALUE;

    /* Whether the saved icon position should only be used
     * if the previous icon position is free. If the position
     * is occupied, another position near the last one will
     */
    icon->item = NAUTILUS_CANVAS_ITEM
                     (eel_canvas_item_new (EEL_CANVAS_GROUP (EEL_CANVAS (container)->root),
                                           nautilus_canvas_item_get_type (),
                                           "visible", FALSE,
                                           NULL));
    icon->item->user_data = icon;

    /* Make sure the icon is under the selection_rectangle */
    item = EEL_CANVAS_ITEM (icon->item);
    band = NAUTILUS_CANVAS_CONTAINER (item->canvas)->details->rubberband_info.selection_rectangle;
    if (band)
    {
        eel_canvas_item_send_behind (item, band);
    }

    /* Put it on both lists. */
    details->icons = g_list_prepend (details->icons, icon);
    details->new_icons = g_list_prepend (details->new_icons, icon);

    g_hash_table_insert (details->icon_set, data, icon);

    details->needs_resort = TRUE;

    /* Run an idle function to add the icons. */
    schedule_redo_layout (container);

    return TRUE;
}

void
nautilus_canvas_container_layout_now (NautilusCanvasContainer *container)
{
    container->details->in_layout_now = TRUE;
    if (container->details->idle_id != 0)
    {
        unschedule_redo_layout (container);
        redo_layout_internal (container);
    }

    /* Also need to make sure we're properly resized, for instance
     * newly added files may trigger a change in the size allocation and
     * thus toggle scrollbars on */
    gtk_container_check_resize (GTK_CONTAINER (gtk_widget_get_parent (GTK_WIDGET (container))));
    container->details->in_layout_now = FALSE;
}

/**
 * nautilus_canvas_container_remove:
 * @container: A NautilusCanvasContainer.
 * @data: Icon data.
 *
 * Remove the icon with this data.
 **/
gboolean
nautilus_canvas_container_remove (NautilusCanvasContainer *container,
                                  NautilusCanvasIconData  *data)
{
    NautilusCanvasIcon *icon;

    g_return_val_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container), FALSE);
    g_return_val_if_fail (data != NULL, FALSE);

    icon = g_hash_table_lookup (container->details->icon_set, data);

    if (icon == NULL)
    {
        return FALSE;
    }

    icon_destroy (container, icon);
    schedule_redo_layout (container);

    g_signal_emit (container, signals[ICON_REMOVED], 0, icon);

    return TRUE;
}

/**
 * nautilus_canvas_container_request_update:
 * @container: A NautilusCanvasContainer.
 * @data: Icon data.
 *
 * Update the icon with this data.
 **/
void
nautilus_canvas_container_request_update (NautilusCanvasContainer *container,
                                          NautilusCanvasIconData  *data)
{
    NautilusCanvasIcon *icon;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));
    g_return_if_fail (data != NULL);

    icon = g_hash_table_lookup (container->details->icon_set, data);

    if (icon != NULL)
    {
        nautilus_canvas_container_update_icon (container, icon);
        container->details->needs_resort = TRUE;
        schedule_redo_layout (container);
    }
}

/* zooming */

NautilusCanvasZoomLevel
nautilus_canvas_container_get_zoom_level (NautilusCanvasContainer *container)
{
    return container->details->zoom_level;
}

void
nautilus_canvas_container_set_zoom_level (NautilusCanvasContainer *container,
                                          int                      new_level)
{
    NautilusCanvasContainerDetails *details;
    int pinned_level;
    double pixels_per_unit;

    details = container->details;

    pinned_level = new_level;
    if (pinned_level < NAUTILUS_CANVAS_ZOOM_LEVEL_SMALL)
    {
        pinned_level = NAUTILUS_CANVAS_ZOOM_LEVEL_SMALL;
    }
    else if (pinned_level > NAUTILUS_CANVAS_ZOOM_LEVEL_LARGER)
    {
        pinned_level = NAUTILUS_CANVAS_ZOOM_LEVEL_LARGER;
    }

    if (pinned_level == details->zoom_level)
    {
        return;
    }

    details->zoom_level = pinned_level;

    pixels_per_unit = (double) nautilus_canvas_container_get_icon_size_for_zoom_level (pinned_level)
                      / NAUTILUS_CANVAS_ICON_SIZE_STANDARD;
    eel_canvas_set_pixels_per_unit (EEL_CANVAS (container), pixels_per_unit);

    nautilus_canvas_container_request_update_all_internal (container, TRUE);
}

/**
 * nautilus_canvas_container_request_update_all:
 * For each icon, synchronizes the displayed information (image, text) with the
 * information from the model.
 *
 * @container: An canvas container.
 **/
void
nautilus_canvas_container_request_update_all (NautilusCanvasContainer *container)
{
    nautilus_canvas_container_request_update_all_internal (container, FALSE);
}

/**
 * nautilus_canvas_container_reveal:
 * Change scroll position as necessary to reveal the specified item.
 */
void
nautilus_canvas_container_reveal (NautilusCanvasContainer *container,
                                  NautilusCanvasIconData  *data)
{
    NautilusCanvasIcon *icon;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));
    g_return_if_fail (data != NULL);

    icon = g_hash_table_lookup (container->details->icon_set, data);

    if (icon != NULL)
    {
        reveal_icon (container, icon);
    }
}

/**
 * nautilus_canvas_container_get_selection:
 * @container: An canvas container.
 *
 * Get a list of the icons currently selected in @container.
 *
 * Return value: A GList of the programmer-specified data associated to each
 * selected icon, or NULL if no canvas is selected.  The caller is expected to
 * free the list when it is not needed anymore.
 **/
GList *
nautilus_canvas_container_get_selection (NautilusCanvasContainer *container)
{
    g_return_val_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container), NULL);

    if (container->details->selection_needs_resort)
    {
        sort_selection (container);
    }

    return g_list_copy (container->details->selection);
}

static GList *
nautilus_canvas_container_get_selected_icons (NautilusCanvasContainer *container)
{
    GList *list, *p;

    g_return_val_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container), NULL);

    list = NULL;
    for (p = container->details->icons; p != NULL; p = p->next)
    {
        NautilusCanvasIcon *icon;

        icon = p->data;
        if (icon->is_selected)
        {
            list = g_list_prepend (list, icon);
        }
    }

    return g_list_reverse (list);
}

/**
 * nautilus_canvas_container_invert_selection:
 * @container: An canvas container.
 *
 * Inverts the selection in @container.
 *
 **/
void
nautilus_canvas_container_invert_selection (NautilusCanvasContainer *container)
{
    GList *p;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    for (p = container->details->icons; p != NULL; p = p->next)
    {
        NautilusCanvasIcon *icon;

        icon = p->data;
        icon_toggle_selected (container, icon);
    }

    g_signal_emit (container, signals[SELECTION_CHANGED], 0);
}


/* Returns an array of GdkPoints of locations of the icons. */
static GArray *
nautilus_canvas_container_get_icon_locations (NautilusCanvasContainer *container,
                                              GList                   *icons)
{
    GArray *result;
    GList *node;
    int index;

    result = g_array_new (FALSE, TRUE, sizeof (GdkPoint));
    result = g_array_set_size (result, g_list_length (icons));

    for (index = 0, node = icons; node != NULL; index++, node = node->next)
    {
        g_array_index (result, GdkPoint, index).x =
            ((NautilusCanvasIcon *) node->data)->x;
        g_array_index (result, GdkPoint, index).y =
            ((NautilusCanvasIcon *) node->data)->y;
    }

    return result;
}

/* Returns a GdkRectangle of the icon. The bounding box is adjusted with the
 * pixels_per_unit already, so they are the final positions on the canvas */
GdkRectangle *
nautilus_canvas_container_get_icon_bounding_box (NautilusCanvasContainer *container,
                                                 NautilusCanvasIconData  *data)
{
    NautilusCanvasIcon *icon;
    int x1, x2, y1, y2;
    GdkRectangle *bounding_box;

    g_return_val_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container), NULL);
    g_return_val_if_fail (data != NULL, NULL);

    icon = g_hash_table_lookup (container->details->icon_set, data);
    icon_get_bounding_box (icon,
                           &x1, &y1, &x2, &y2,
                           BOUNDS_USAGE_FOR_DISPLAY);
    bounding_box = g_malloc0 (sizeof (GdkRectangle));
    bounding_box->x = x1 * EEL_CANVAS (container)->pixels_per_unit;
    bounding_box->width = (x2 - x1) * EEL_CANVAS (container)->pixels_per_unit;
    bounding_box->y = y1 * EEL_CANVAS (container)->pixels_per_unit;
    bounding_box->height = (y2 - y1) * EEL_CANVAS (container)->pixels_per_unit;

    return bounding_box;
}

/**
 * nautilus_canvas_container_get_selected_icon_locations:
 * @container: An canvas container widget.
 *
 * Returns an array of GdkPoints of locations of the selected icons.
 **/
GArray *
nautilus_canvas_container_get_selected_icon_locations (NautilusCanvasContainer *container)
{
    GArray *result;
    GList *icons;

    g_return_val_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container), NULL);

    icons = nautilus_canvas_container_get_selected_icons (container);
    result = nautilus_canvas_container_get_icon_locations (container, icons);
    g_list_free (icons);

    return result;
}

/**
 * nautilus_canvas_container_select_all:
 * @container: An canvas container widget.
 *
 * Select all the icons in @container at once.
 **/
void
nautilus_canvas_container_select_all (NautilusCanvasContainer *container)
{
    gboolean selection_changed;
    GList *p;
    NautilusCanvasIcon *icon;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    selection_changed = FALSE;

    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;

        selection_changed |= icon_set_selected (container, icon, TRUE);
    }

    if (selection_changed)
    {
        g_signal_emit (container,
                       signals[SELECTION_CHANGED], 0);
    }
}

/**
 * nautilus_canvas_container_select_first:
 * @container: An canvas container widget.
 *
 * Select the first icon in @container.
 **/
void
nautilus_canvas_container_select_first (NautilusCanvasContainer *container)
{
    gboolean selection_changed;
    NautilusCanvasIcon *icon;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    selection_changed = FALSE;

    if (container->details->needs_resort)
    {
        resort (container);
        container->details->needs_resort = FALSE;
    }

    icon = g_list_nth_data (container->details->icons, 0);
    if (icon)
    {
        selection_changed |= icon_set_selected (container, icon, TRUE);
    }

    if (selection_changed)
    {
        g_signal_emit (container,
                       signals[SELECTION_CHANGED], 0);
    }
}

/**
 * nautilus_canvas_container_set_selection:
 * @container: An canvas container widget.
 * @selection: A list of NautilusCanvasIconData *.
 *
 * Set the selection to exactly the icons in @container which have
 * programmer data matching one of the items in @selection.
 **/
void
nautilus_canvas_container_set_selection (NautilusCanvasContainer *container,
                                         GList                   *selection)
{
    gboolean selection_changed;
    GHashTable *hash;
    GList *p;
    gboolean res;
    NautilusCanvasIcon *icon, *selected_icon;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    selection_changed = FALSE;
    selected_icon = NULL;

    hash = g_hash_table_new (NULL, NULL);
    for (p = selection; p != NULL; p = p->next)
    {
        g_hash_table_insert (hash, p->data, p->data);
    }
    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;

        res = icon_set_selected
                  (container, icon,
                  g_hash_table_lookup (hash, icon->data) != NULL);
        selection_changed |= res;

        if (res)
        {
            selected_icon = icon;
        }
    }
    g_hash_table_destroy (hash);

    if (selection_changed)
    {
        /* if only one item has been selected, use it as range
         * selection base (cf. handle_canvas_button_press) */
        if (g_list_length (selection) == 1)
        {
            container->details->range_selection_base_icon = selected_icon;
        }

        g_signal_emit (container,
                       signals[SELECTION_CHANGED], 0);
    }
}

/**
 * nautilus_canvas_container_select_list_unselect_others.
 * @container: An canvas container widget.
 * @selection: A list of NautilusCanvasIcon *.
 *
 * Set the selection to exactly the icons in @selection.
 **/
void
nautilus_canvas_container_select_list_unselect_others (NautilusCanvasContainer *container,
                                                       GList                   *selection)
{
    gboolean selection_changed;
    GHashTable *hash;
    GList *p;
    NautilusCanvasIcon *icon;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    selection_changed = FALSE;

    hash = g_hash_table_new (NULL, NULL);
    for (p = selection; p != NULL; p = p->next)
    {
        g_hash_table_insert (hash, p->data, p->data);
    }
    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;

        selection_changed |= icon_set_selected
                                 (container, icon,
                                 g_hash_table_lookup (hash, icon) != NULL);
    }
    g_hash_table_destroy (hash);

    if (selection_changed)
    {
        g_signal_emit (container,
                       signals[SELECTION_CHANGED], 0);
    }
}

/**
 * nautilus_canvas_container_unselect_all:
 * @container: An canvas container widget.
 *
 * Deselect all the icons in @container.
 **/
void
nautilus_canvas_container_unselect_all (NautilusCanvasContainer *container)
{
    if (unselect_all (container))
    {
        g_signal_emit (container,
                       signals[SELECTION_CHANGED], 0);
    }
}

/**
 * nautilus_canvas_container_get_icon_by_uri:
 * @container: An canvas container widget.
 * @uri: The uri of an canvas to find.
 *
 * Locate an icon, given the URI. The URI must match exactly.
 * Later we may have to have some way of figuring out if the
 * URI specifies the same object that does not require an exact match.
 **/
NautilusCanvasIcon *
nautilus_canvas_container_get_icon_by_uri (NautilusCanvasContainer *container,
                                           const char              *uri)
{
    NautilusCanvasContainerDetails *details;
    GList *p;

    /* Eventually, we must avoid searching the entire canvas list,
     *  but it's OK for now.
     *  A hash table mapping uri to canvas is one possibility.
     */

    details = container->details;

    for (p = details->icons; p != NULL; p = p->next)
    {
        NautilusCanvasIcon *icon;
        char *icon_uri;
        gboolean is_match;

        icon = p->data;

        icon_uri = nautilus_canvas_container_get_icon_uri
                       (container, icon);
        is_match = strcmp (uri, icon_uri) == 0;
        g_free (icon_uri);

        if (is_match)
        {
            return icon;
        }
    }

    return NULL;
}

static NautilusCanvasIcon *
get_nth_selected_icon (NautilusCanvasContainer *container,
                       int                      index)
{
    GList *p;
    NautilusCanvasIcon *icon;
    int selection_count;

    g_assert (index > 0);

    /* Find the nth selected icon. */
    selection_count = 0;
    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;
        if (icon->is_selected)
        {
            if (++selection_count == index)
            {
                return icon;
            }
        }
    }
    return NULL;
}

static NautilusCanvasIcon *
get_first_selected_icon (NautilusCanvasContainer *container)
{
    return get_nth_selected_icon (container, 1);
}

static gboolean
has_multiple_selection (NautilusCanvasContainer *container)
{
    return get_nth_selected_icon (container, 2) != NULL;
}

static gboolean
all_selected (NautilusCanvasContainer *container)
{
    GList *p;
    NautilusCanvasIcon *icon;

    for (p = container->details->icons; p != NULL; p = p->next)
    {
        icon = p->data;
        if (!icon->is_selected)
        {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean
has_selection (NautilusCanvasContainer *container)
{
    return get_nth_selected_icon (container, 1) != NULL;
}

char *
nautilus_canvas_container_get_icon_uri (NautilusCanvasContainer *container,
                                        NautilusCanvasIcon      *icon)
{
    char *uri;

    uri = NULL;
    g_signal_emit (container,
                   signals[GET_ICON_URI], 0,
                   icon->data,
                   &uri);
    return uri;
}

char *
nautilus_canvas_container_get_icon_activation_uri (NautilusCanvasContainer *container,
                                                   NautilusCanvasIcon      *icon)
{
    char *uri;

    uri = NULL;
    g_signal_emit (container,
                   signals[GET_ICON_ACTIVATION_URI], 0,
                   icon->data,
                   &uri);
    return uri;
}

char *
nautilus_canvas_container_get_icon_drop_target_uri (NautilusCanvasContainer *container,
                                                    NautilusCanvasIcon      *icon)
{
    char *uri;

    uri = NULL;
    g_signal_emit (container,
                   signals[GET_ICON_DROP_TARGET_URI], 0,
                   icon->data,
                   &uri);
    return uri;
}

/* Re-sort, switching to automatic layout if it was in manual layout. */
void
nautilus_canvas_container_sort (NautilusCanvasContainer *container)
{
    container->details->needs_resort = TRUE;
    redo_layout (container);
}

void
nautilus_canvas_container_set_single_click_mode (NautilusCanvasContainer *container,
                                                 gboolean                 single_click_mode)
{
    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    container->details->single_click_mode = single_click_mode;
}

/* handle theme changes */

void
nautilus_canvas_container_set_font (NautilusCanvasContainer *container,
                                    const char              *font)
{
    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    if (g_strcmp0 (container->details->font, font) == 0)
    {
        return;
    }

    g_free (container->details->font);
    container->details->font = g_strdup (font);

    nautilus_canvas_container_request_update_all_internal (container, TRUE);
    gtk_widget_queue_draw (GTK_WIDGET (container));
}

/**
 * nautilus_canvas_container_get_icon_description
 * @container: An canvas container widget.
 * @data: Icon data
 *
 * Gets the description for the icon. This function may return NULL.
 **/
char *
nautilus_canvas_container_get_icon_description (NautilusCanvasContainer *container,
                                                NautilusCanvasIconData  *data)
{
    NautilusCanvasContainerClass *klass;

    klass = NAUTILUS_CANVAS_CONTAINER_GET_CLASS (container);

    if (klass->get_icon_description)
    {
        return klass->get_icon_description (container, data);
    }
    else
    {
        return NULL;
    }
}

/**
 * nautilus_canvas_container_set_highlighted_for_clipboard
 * @container: An canvas container widget.
 * @data: Canvas Data associated with all icons that should be highlighted.
 *        Others will be unhighlighted.
 **/
void
nautilus_canvas_container_set_highlighted_for_clipboard (NautilusCanvasContainer *container,
                                                         GList                   *clipboard_canvas_data)
{
    GList *l;
    NautilusCanvasIcon *icon;
    gboolean highlighted_for_clipboard;

    g_return_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container));

    for (l = container->details->icons; l != NULL; l = l->next)
    {
        icon = l->data;
        highlighted_for_clipboard = (g_list_find (clipboard_canvas_data, icon->data) != NULL);

        eel_canvas_item_set (EEL_CANVAS_ITEM (icon->item),
                             "highlighted-for-clipboard", highlighted_for_clipboard,
                             NULL);
    }
}

/* NautilusCanvasContainerAccessible */
typedef struct
{
    EelCanvasAccessible parent;
    NautilusCanvasContainerAccessiblePrivate *priv;
} NautilusCanvasContainerAccessible;

typedef EelCanvasAccessibleClass NautilusCanvasContainerAccessibleClass;

#define GET_ACCESSIBLE_PRIV(o) ((NautilusCanvasContainerAccessible *) o)->priv

/* AtkAction interface */
static gboolean
nautilus_canvas_container_accessible_do_action (AtkAction *accessible,
                                                int        i)
{
    GtkWidget *widget;
    NautilusCanvasContainer *container;
    GList *selection;

    g_return_val_if_fail (i < LAST_ACTION, FALSE);

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);
    switch (i)
    {
        case ACTION_ACTIVATE:
        {
            selection = nautilus_canvas_container_get_selection (container);

            if (selection)
            {
                g_signal_emit_by_name (container, "activate", selection);
                g_list_free (selection);
            }
        }
        break;

        case ACTION_MENU:
        {
            handle_popups (container, NULL, "context_click_background");
        }
        break;

        default:
        {
            g_warning ("Invalid action passed to NautilusCanvasContainerAccessible::do_action");
            return FALSE;
        }
        break;
    }
    return TRUE;
}

static int
nautilus_canvas_container_accessible_get_n_actions (AtkAction *accessible)
{
    return LAST_ACTION;
}

static const char *
nautilus_canvas_container_accessible_action_get_description (AtkAction *accessible,
                                                             int        i)
{
    NautilusCanvasContainerAccessiblePrivate *priv;

    g_assert (i < LAST_ACTION);

    priv = GET_ACCESSIBLE_PRIV (accessible);

    if (priv->action_descriptions[i])
    {
        return priv->action_descriptions[i];
    }
    else
    {
        return nautilus_canvas_container_accessible_action_descriptions[i];
    }
}

static const char *
nautilus_canvas_container_accessible_action_get_name (AtkAction *accessible,
                                                      int        i)
{
    g_assert (i < LAST_ACTION);

    return nautilus_canvas_container_accessible_action_names[i];
}

static const char *
nautilus_canvas_container_accessible_action_get_keybinding (AtkAction *accessible,
                                                            int        i)
{
    g_assert (i < LAST_ACTION);

    return NULL;
}

static gboolean
nautilus_canvas_container_accessible_action_set_description (AtkAction  *accessible,
                                                             int         i,
                                                             const char *description)
{
    NautilusCanvasContainerAccessiblePrivate *priv;

    g_assert (i < LAST_ACTION);

    priv = GET_ACCESSIBLE_PRIV (accessible);

    if (priv->action_descriptions[i])
    {
        g_free (priv->action_descriptions[i]);
    }
    priv->action_descriptions[i] = g_strdup (description);

    return FALSE;
}

static void
nautilus_canvas_container_accessible_action_interface_init (AtkActionIface *iface)
{
    iface->do_action = nautilus_canvas_container_accessible_do_action;
    iface->get_n_actions = nautilus_canvas_container_accessible_get_n_actions;
    iface->get_description = nautilus_canvas_container_accessible_action_get_description;
    iface->get_name = nautilus_canvas_container_accessible_action_get_name;
    iface->get_keybinding = nautilus_canvas_container_accessible_action_get_keybinding;
    iface->set_description = nautilus_canvas_container_accessible_action_set_description;
}

/* AtkSelection interface */

static void
nautilus_canvas_container_accessible_update_selection (AtkObject *accessible)
{
    NautilusCanvasContainer *container;
    NautilusCanvasContainerAccessiblePrivate *priv;

    container = NAUTILUS_CANVAS_CONTAINER (gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible)));
    priv = GET_ACCESSIBLE_PRIV (accessible);

    if (priv->selection)
    {
        g_list_free (priv->selection);
        priv->selection = NULL;
    }

    priv->selection = nautilus_canvas_container_get_selected_icons (container);
}

static void
nautilus_canvas_container_accessible_selection_changed_cb (NautilusCanvasContainer *container,
                                                           gpointer                 data)
{
    g_signal_emit_by_name (data, "selection-changed");
}

static void
nautilus_canvas_container_accessible_icon_added_cb (NautilusCanvasContainer *container,
                                                    NautilusCanvasIconData  *icon_data,
                                                    gpointer                 data)
{
    NautilusCanvasIcon *icon;
    AtkObject *atk_parent;
    AtkObject *atk_child;

    /* We don't want to emit children_changed signals during any type of load. */
    if (!container->details->in_layout_now || container->details->is_populating_container)
    {
        return;
    }

    icon = g_hash_table_lookup (container->details->icon_set, icon_data);
    if (icon)
    {
        atk_parent = ATK_OBJECT (data);
        atk_child = atk_gobject_accessible_for_object
                        (G_OBJECT (icon->item));

        g_signal_emit_by_name (atk_parent, "children-changed::add",
                               icon->position, atk_child, NULL);
    }
}

static void
nautilus_canvas_container_accessible_icon_removed_cb (NautilusCanvasContainer *container,
                                                      NautilusCanvasIconData  *icon_data,
                                                      gpointer                 data)
{
    NautilusCanvasIcon *icon;
    AtkObject *atk_parent;
    AtkObject *atk_child;

    icon = g_hash_table_lookup (container->details->icon_set, icon_data);
    if (icon)
    {
        atk_parent = ATK_OBJECT (data);
        atk_child = atk_gobject_accessible_for_object
                        (G_OBJECT (icon->item));

        g_signal_emit_by_name (atk_parent, "children-changed::remove",
                               icon->position, atk_child, NULL);
    }
}

static void
nautilus_canvas_container_accessible_cleared_cb (NautilusCanvasContainer *container,
                                                 gpointer                 data)
{
    g_signal_emit_by_name (data, "children-changed", 0, NULL, NULL);
}

static gboolean
nautilus_canvas_container_accessible_add_selection (AtkSelection *accessible,
                                                    int           i)
{
    GtkWidget *widget;
    NautilusCanvasContainer *container;
    GList *l;
    GList *selection;
    NautilusCanvasIcon *icon;

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    l = g_list_nth (container->details->icons, i);
    if (l)
    {
        icon = l->data;

        selection = nautilus_canvas_container_get_selection (container);
        selection = g_list_prepend (selection,
                                    icon->data);
        nautilus_canvas_container_set_selection (container, selection);

        g_list_free (selection);
        return TRUE;
    }

    return FALSE;
}

static gboolean
nautilus_canvas_container_accessible_clear_selection (AtkSelection *accessible)
{
    GtkWidget *widget;
    NautilusCanvasContainer *container;

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    nautilus_canvas_container_unselect_all (container);

    return TRUE;
}

static AtkObject *
nautilus_canvas_container_accessible_ref_selection (AtkSelection *accessible,
                                                    int           i)
{
    NautilusCanvasContainerAccessiblePrivate *priv;
    AtkObject *atk_object;
    GList *item;
    NautilusCanvasIcon *icon;

    nautilus_canvas_container_accessible_update_selection (ATK_OBJECT (accessible));
    priv = GET_ACCESSIBLE_PRIV (accessible);

    item = (g_list_nth (priv->selection, i));

    if (item)
    {
        icon = item->data;
        atk_object = atk_gobject_accessible_for_object (G_OBJECT (icon->item));
        if (atk_object)
        {
            g_object_ref (atk_object);
        }

        return atk_object;
    }
    else
    {
        return NULL;
    }
}

static int
nautilus_canvas_container_accessible_get_selection_count (AtkSelection *accessible)
{
    NautilusCanvasContainerAccessiblePrivate *priv;
    int count;

    priv = GET_ACCESSIBLE_PRIV (accessible);
    nautilus_canvas_container_accessible_update_selection (ATK_OBJECT (accessible));
    count = g_list_length (priv->selection);

    return count;
}

static gboolean
nautilus_canvas_container_accessible_is_child_selected (AtkSelection *accessible,
                                                        int           i)
{
    NautilusCanvasContainer *container;
    GList *l;
    NautilusCanvasIcon *icon;
    GtkWidget *widget;

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    l = g_list_nth (container->details->icons, i);
    if (l)
    {
        icon = l->data;
        return icon->is_selected;
    }
    return FALSE;
}

static gboolean
nautilus_canvas_container_accessible_remove_selection (AtkSelection *accessible,
                                                       int           i)
{
    NautilusCanvasContainerAccessiblePrivate *priv;
    NautilusCanvasContainer *container;
    GList *l;
    GList *selection;
    NautilusCanvasIcon *icon;
    GtkWidget *widget;

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);
    nautilus_canvas_container_accessible_update_selection (ATK_OBJECT (accessible));

    priv = GET_ACCESSIBLE_PRIV (accessible);
    l = g_list_nth (priv->selection, i);
    if (l)
    {
        icon = l->data;

        selection = nautilus_canvas_container_get_selection (container);
        selection = g_list_remove (selection, icon->data);
        nautilus_canvas_container_set_selection (container, selection);

        g_list_free (selection);
        return TRUE;
    }

    return FALSE;
}

static gboolean
nautilus_canvas_container_accessible_select_all_selection (AtkSelection *accessible)
{
    NautilusCanvasContainer *container;
    GtkWidget *widget;

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    nautilus_canvas_container_select_all (container);

    return TRUE;
}

void
nautilus_canvas_container_widget_to_file_operation_position (NautilusCanvasContainer *container,
                                                             GdkPoint                *position)
{
    double x, y;

    g_return_if_fail (position != NULL);

    x = position->x;
    y = position->y;

    eel_canvas_window_to_world (EEL_CANVAS (container), x, y, &x, &y);

    position->x = (int) x;
    position->y = (int) y;

    /* ensure that we end up in the middle of the icon */
    position->x -= nautilus_canvas_container_get_icon_size_for_zoom_level (container->details->zoom_level) / 2;
    position->y -= nautilus_canvas_container_get_icon_size_for_zoom_level (container->details->zoom_level) / 2;
}

static void
nautilus_canvas_container_accessible_selection_interface_init (AtkSelectionIface *iface)
{
    iface->add_selection = nautilus_canvas_container_accessible_add_selection;
    iface->clear_selection = nautilus_canvas_container_accessible_clear_selection;
    iface->ref_selection = nautilus_canvas_container_accessible_ref_selection;
    iface->get_selection_count = nautilus_canvas_container_accessible_get_selection_count;
    iface->is_child_selected = nautilus_canvas_container_accessible_is_child_selected;
    iface->remove_selection = nautilus_canvas_container_accessible_remove_selection;
    iface->select_all_selection = nautilus_canvas_container_accessible_select_all_selection;
}


static gint
nautilus_canvas_container_accessible_get_n_children (AtkObject *accessible)
{
    NautilusCanvasContainer *container;
    GtkWidget *widget;
    gint i;

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return FALSE;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    i = g_hash_table_size (container->details->icon_set);

    return i;
}

static AtkObject *
nautilus_canvas_container_accessible_ref_child (AtkObject *accessible,
                                                int        i)
{
    AtkObject *atk_object;
    NautilusCanvasContainer *container;
    GList *item;
    NautilusCanvasIcon *icon;
    GtkWidget *widget;

    widget = gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible));
    if (!widget)
    {
        return NULL;
    }

    container = NAUTILUS_CANVAS_CONTAINER (widget);

    item = (g_list_nth (container->details->icons, i));

    if (item)
    {
        icon = item->data;

        atk_object = atk_gobject_accessible_for_object (G_OBJECT (icon->item));
        g_object_ref (atk_object);

        return atk_object;
    }
    return NULL;
}

G_DEFINE_TYPE_WITH_CODE (NautilusCanvasContainerAccessible, nautilus_canvas_container_accessible,
                         eel_canvas_accessible_get_type (),
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_ACTION, nautilus_canvas_container_accessible_action_interface_init)
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_SELECTION, nautilus_canvas_container_accessible_selection_interface_init))

static void
nautilus_canvas_container_accessible_initialize (AtkObject *accessible,
                                                 gpointer   data)
{
    NautilusCanvasContainer *container;

    if (ATK_OBJECT_CLASS (nautilus_canvas_container_accessible_parent_class)->initialize)
    {
        ATK_OBJECT_CLASS (nautilus_canvas_container_accessible_parent_class)->initialize (accessible, data);
    }

    if (GTK_IS_ACCESSIBLE (accessible))
    {
        nautilus_canvas_container_accessible_update_selection
            (ATK_OBJECT (accessible));

        container = NAUTILUS_CANVAS_CONTAINER (gtk_accessible_get_widget (GTK_ACCESSIBLE (accessible)));
        g_signal_connect (container, "selection-changed",
                          G_CALLBACK (nautilus_canvas_container_accessible_selection_changed_cb),
                          accessible);
        g_signal_connect (container, "icon-added",
                          G_CALLBACK (nautilus_canvas_container_accessible_icon_added_cb),
                          accessible);
        g_signal_connect (container, "icon-removed",
                          G_CALLBACK (nautilus_canvas_container_accessible_icon_removed_cb),
                          accessible);
        g_signal_connect (container, "cleared",
                          G_CALLBACK (nautilus_canvas_container_accessible_cleared_cb),
                          accessible);
    }
}

static void
nautilus_canvas_container_accessible_finalize (GObject *object)
{
    NautilusCanvasContainerAccessiblePrivate *priv;
    int i;

    priv = GET_ACCESSIBLE_PRIV (object);

    if (priv->selection)
    {
        g_list_free (priv->selection);
    }

    for (i = 0; i < LAST_ACTION; i++)
    {
        if (priv->action_descriptions[i])
        {
            g_free (priv->action_descriptions[i]);
        }
    }

    G_OBJECT_CLASS (nautilus_canvas_container_accessible_parent_class)->finalize (object);
}

static void
nautilus_canvas_container_accessible_init (NautilusCanvasContainerAccessible *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, nautilus_canvas_container_accessible_get_type (),
                                              NautilusCanvasContainerAccessiblePrivate);
}

static void
nautilus_canvas_container_accessible_class_init (NautilusCanvasContainerAccessibleClass *klass)
{
    AtkObjectClass *atk_class = ATK_OBJECT_CLASS (klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = nautilus_canvas_container_accessible_finalize;

    atk_class->get_n_children = nautilus_canvas_container_accessible_get_n_children;
    atk_class->ref_child = nautilus_canvas_container_accessible_ref_child;
    atk_class->initialize = nautilus_canvas_container_accessible_initialize;

    g_type_class_add_private (klass, sizeof (NautilusCanvasContainerAccessiblePrivate));
}

gboolean
nautilus_canvas_container_is_layout_rtl (NautilusCanvasContainer *container)
{
    g_return_val_if_fail (NAUTILUS_IS_CANVAS_CONTAINER (container), 0);

    return (gtk_widget_get_direction (GTK_WIDGET (container)) == GTK_TEXT_DIR_RTL);
}

int
nautilus_canvas_container_get_max_layout_lines_for_pango (NautilusCanvasContainer *container)
{
    int limit;

    limit = text_ellipsis_limits[container->details->zoom_level];

    if (limit <= 0)
    {
        return G_MININT;
    }

    return -limit;
}

int
nautilus_canvas_container_get_max_layout_lines (NautilusCanvasContainer *container)
{
    int limit;

    limit = text_ellipsis_limits[container->details->zoom_level];

    if (limit <= 0)
    {
        return G_MAXINT;
    }

    return limit;
}
