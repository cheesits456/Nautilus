/* Nautilus - Canvas item for floating selection.
 *
 * Copyright (C) 1997, 1998, 1999, 2000 Free Software Foundation
 * Copyright (C) 2011 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Federico Mena <federico@nuclecu.unam.mx>
 *          Cosimo Cecchi <cosimoc@redhat.com>
 */

#include <config.h>

#include "nautilus-selection-canvas-item.h"

#include <math.h>

enum
{
    PROP_X1 = 1,
    PROP_Y1,
    PROP_X2,
    PROP_Y2,
    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL };

typedef struct
{
    /*< public >*/
    int x0, y0, x1, y1;
}  Rect;

struct _NautilusSelectionCanvasItemDetails
{
    Rect last_update_rect;
    Rect last_outline_update_rect;
    int last_outline_update_width;

    double x1, y1, x2, y2;              /* Corners of item */
};

G_DEFINE_TYPE (NautilusSelectionCanvasItem, nautilus_selection_canvas_item, EEL_TYPE_CANVAS_ITEM);

static void
nautilus_selection_canvas_item_draw (EelCanvasItem  *item,
                                     cairo_t        *cr,
                                     cairo_region_t *region)
{
    NautilusSelectionCanvasItem *self;
    double x1, y1, x2, y2;
    int cx1, cy1, cx2, cy2;
    double i2w_dx, i2w_dy;
    GtkStyleContext *context;

    self = NAUTILUS_SELECTION_CANVAS_ITEM (item);

    /* Get canvas pixel coordinates */
    i2w_dx = 0.0;
    i2w_dy = 0.0;
    eel_canvas_item_i2w (item, &i2w_dx, &i2w_dy);

    x1 = self->priv->x1 + i2w_dx;
    y1 = self->priv->y1 + i2w_dy;
    x2 = self->priv->x2 + i2w_dx;
    y2 = self->priv->y2 + i2w_dy;

    eel_canvas_w2c (item->canvas, x1, y1, &cx1, &cy1);
    eel_canvas_w2c (item->canvas, x2, y2, &cx2, &cy2);

    if (cx2 <= cx1 || cy2 <= cy1)
    {
        return;
    }

    context = gtk_widget_get_style_context (GTK_WIDGET (item->canvas));

    gtk_style_context_save (context);

    gtk_style_context_add_class (context, GTK_STYLE_CLASS_RUBBERBAND);

    cairo_save (cr);

    gtk_render_background (context, cr,
                           cx1, cy1,
                           cx2 - cx1,
                           cy2 - cy1);
    gtk_render_frame (context, cr,
                      cx1, cy1,
                      cx2 - cx1,
                      cy2 - cy1);

    cairo_restore (cr);

    gtk_style_context_restore (context);
}

static double
nautilus_selection_canvas_item_point (EelCanvasItem  *item,
                                      double          x,
                                      double          y,
                                      int             cx,
                                      int             cy,
                                      EelCanvasItem **actual_item)
{
    NautilusSelectionCanvasItem *self;
    double x1, y1, x2, y2;
    double hwidth;
    double dx, dy;

    self = NAUTILUS_SELECTION_CANVAS_ITEM (item);
    *actual_item = item;

    /* Find the bounds for the rectangle plus its outline width */

    x1 = self->priv->x1;
    y1 = self->priv->y1;
    x2 = self->priv->x2;
    y2 = self->priv->y2;

    hwidth = (1.0 / item->canvas->pixels_per_unit) / 2.0;

    x1 -= hwidth;
    y1 -= hwidth;
    x2 += hwidth;
    y2 += hwidth;

    /* Is point inside rectangle (which can be hollow if it has no fill set)? */

    if ((x >= x1) && (y >= y1) && (x <= x2) && (y <= y2))
    {
        return 0.0;
    }

    /* Point is outside rectangle */

    if (x < x1)
    {
        dx = x1 - x;
    }
    else if (x > x2)
    {
        dx = x - x2;
    }
    else
    {
        dx = 0.0;
    }

    if (y < y1)
    {
        dy = y1 - y;
    }
    else if (y > y2)
    {
        dy = y - y2;
    }
    else
    {
        dy = 0.0;
    }

    return sqrt (dx * dx + dy * dy);
}

static void
request_redraw_borders (EelCanvas *canvas,
                        Rect      *update_rect,
                        int        width)
{
    /* Top */
    eel_canvas_request_redraw (canvas,
                               update_rect->x0, update_rect->y0,
                               update_rect->x1, update_rect->y0 + width);
    /* Bottom */
    eel_canvas_request_redraw (canvas,
                               update_rect->x0, update_rect->y1 - width,
                               update_rect->x1, update_rect->y1);
    /* Left */
    eel_canvas_request_redraw (canvas,
                               update_rect->x0, update_rect->y0,
                               update_rect->x0 + width, update_rect->y1);
    /* Right */
    eel_canvas_request_redraw (canvas,
                               update_rect->x1 - width, update_rect->y0,
                               update_rect->x1, update_rect->y1);
}

static Rect make_rect (int x0,
                       int y0,
                       int x1,
                       int y1);

static int
rect_empty (const Rect *src)
{
    return (src->x1 <= src->x0 || src->y1 <= src->y0);
}

static gboolean
rects_intersect (Rect r1,
                 Rect r2)
{
    if (r1.x0 >= r2.x1)
    {
        return FALSE;
    }
    if (r2.x0 >= r1.x1)
    {
        return FALSE;
    }
    if (r1.y0 >= r2.y1)
    {
        return FALSE;
    }
    if (r2.y0 >= r1.y1)
    {
        return FALSE;
    }
    return TRUE;
}

static void
diff_rects_guts (Rect ra,
                 Rect rb,
                 int *count,
                 Rect result[4])
{
    if (ra.x0 < rb.x0)
    {
        result[(*count)++] = make_rect (ra.x0, ra.y0, rb.x0, ra.y1);
    }
    if (ra.y0 < rb.y0)
    {
        result[(*count)++] = make_rect (ra.x0, ra.y0, ra.x1, rb.y0);
    }
    if (ra.x1 < rb.x1)
    {
        result[(*count)++] = make_rect (ra.x1, rb.y0, rb.x1, rb.y1);
    }
    if (ra.y1 < rb.y1)
    {
        result[(*count)++] = make_rect (rb.x0, ra.y1, rb.x1, rb.y1);
    }
}

static void
diff_rects (Rect r1,
            Rect r2,
            int *count,
            Rect result[4])
{
    g_assert (count != NULL);
    g_assert (result != NULL);

    *count = 0;

    if (rects_intersect (r1, r2))
    {
        diff_rects_guts (r1, r2, count, result);
        diff_rects_guts (r2, r1, count, result);
    }
    else
    {
        if (!rect_empty (&r1))
        {
            result[(*count)++] = r1;
        }
        if (!rect_empty (&r2))
        {
            result[(*count)++] = r2;
        }
    }
}

static Rect
make_rect (int x0,
           int y0,
           int x1,
           int y1)
{
    Rect r;

    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1;
    r.y1 = y1;
    return r;
}

static void
nautilus_selection_canvas_item_update (EelCanvasItem *item,
                                       double         i2w_dx,
                                       double         i2w_dy,
                                       gint           flags)
{
    NautilusSelectionCanvasItem *self;
    NautilusSelectionCanvasItemDetails *priv;
    double x1, y1, x2, y2;
    int cx1, cy1, cx2, cy2;
    int repaint_rects_count, i;
    GtkStyleContext *context;
    GtkBorder border;
    Rect update_rect, repaint_rects[4];

    if (EEL_CANVAS_ITEM_CLASS (nautilus_selection_canvas_item_parent_class)->update)
    {
        (*EEL_CANVAS_ITEM_CLASS (nautilus_selection_canvas_item_parent_class)->update)(item, i2w_dx, i2w_dy, flags);
    }

    self = NAUTILUS_SELECTION_CANVAS_ITEM (item);
    priv = self->priv;

    x1 = priv->x1 + i2w_dx;
    y1 = priv->y1 + i2w_dy;
    x2 = priv->x2 + i2w_dx;
    y2 = priv->y2 + i2w_dy;

    eel_canvas_w2c (item->canvas, x1, y1, &cx1, &cy1);
    eel_canvas_w2c (item->canvas, x2, y2, &cx2, &cy2);

    update_rect = make_rect (cx1, cy1, cx2 + 1, cy2 + 1);
    diff_rects (update_rect, priv->last_update_rect,
                &repaint_rects_count, repaint_rects);
    for (i = 0; i < repaint_rects_count; i++)
    {
        eel_canvas_request_redraw (item->canvas,
                                   repaint_rects[i].x0, repaint_rects[i].y0,
                                   repaint_rects[i].x1, repaint_rects[i].y1);
    }

    priv->last_update_rect = update_rect;

    context = gtk_widget_get_style_context (GTK_WIDGET (item->canvas));

    gtk_style_context_save (context);
    gtk_style_context_add_class (context, GTK_STYLE_CLASS_RUBBERBAND);
    gtk_style_context_get_border (context, GTK_STATE_FLAG_NORMAL, &border);
    gtk_style_context_restore (context);

    cx1 -= border.left;
    cy1 -= border.top;
    cx2 += border.right;
    cy2 += border.bottom;

    update_rect = make_rect (cx1, cy1, cx2, cy2);
    request_redraw_borders (item->canvas, &update_rect,
                            border.left + border.top + border.right + border.bottom);
    request_redraw_borders (item->canvas, &priv->last_outline_update_rect,
                            priv->last_outline_update_width);
    priv->last_outline_update_rect = update_rect;
    priv->last_outline_update_width = border.left + border.top + border.right + border.bottom;

    item->x1 = cx1;
    item->y1 = cy1;
    item->x2 = cx2;
    item->y2 = cy2;
}

static void
nautilus_selection_canvas_item_translate (EelCanvasItem *item,
                                          double         dx,
                                          double         dy)
{
    NautilusSelectionCanvasItem *self;

    self = NAUTILUS_SELECTION_CANVAS_ITEM (item);

    self->priv->x1 += dx;
    self->priv->y1 += dy;
    self->priv->x2 += dx;
    self->priv->y2 += dy;
}

static void
nautilus_selection_canvas_item_bounds (EelCanvasItem *item,
                                       double        *x1,
                                       double        *y1,
                                       double        *x2,
                                       double        *y2)
{
    NautilusSelectionCanvasItem *self;
    GtkStyleContext *context;
    GtkBorder border;

    self = NAUTILUS_SELECTION_CANVAS_ITEM (item);
    context = gtk_widget_get_style_context (GTK_WIDGET (item->canvas));

    gtk_style_context_save (context);
    gtk_style_context_add_class (context, GTK_STYLE_CLASS_RUBBERBAND);
    gtk_style_context_get_border (context, GTK_STATE_FLAG_NORMAL, &border);
    gtk_style_context_restore (context);

    *x1 = self->priv->x1 - (border.left / item->canvas->pixels_per_unit) / 2.0;
    *y1 = self->priv->y1 - (border.top / item->canvas->pixels_per_unit) / 2.0;
    *x2 = self->priv->x2 + (border.right / item->canvas->pixels_per_unit) / 2.0;
    *y2 = self->priv->y2 + (border.bottom / item->canvas->pixels_per_unit) / 2.0;
}

static void
nautilus_selection_canvas_item_set_property (GObject      *object,
                                             guint         param_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
    EelCanvasItem *item;
    NautilusSelectionCanvasItem *self;

    self = NAUTILUS_SELECTION_CANVAS_ITEM (object);
    item = EEL_CANVAS_ITEM (object);

    switch (param_id)
    {
        case PROP_X1:
        {
            self->priv->x1 = g_value_get_double (value);

            eel_canvas_item_request_update (item);
        }
        break;

        case PROP_Y1:
        {
            self->priv->y1 = g_value_get_double (value);

            eel_canvas_item_request_update (item);
        }
        break;

        case PROP_X2:
        {
            self->priv->x2 = g_value_get_double (value);

            eel_canvas_item_request_update (item);
        }
        break;

        case PROP_Y2:
        {
            self->priv->y2 = g_value_get_double (value);

            eel_canvas_item_request_update (item);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        }
        break;
    }
}

static void
nautilus_selection_canvas_item_get_property (GObject    *object,
                                             guint       param_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
    NautilusSelectionCanvasItem *self;

    self = NAUTILUS_SELECTION_CANVAS_ITEM (object);

    switch (param_id)
    {
        case PROP_X1:
        {
            g_value_set_double (value, self->priv->x1);
        }
        break;

        case PROP_Y1:
        {
            g_value_set_double (value, self->priv->y1);
        }
        break;

        case PROP_X2:
        {
            g_value_set_double (value, self->priv->x2);
        }
        break;

        case PROP_Y2:
        {
            g_value_set_double (value, self->priv->y2);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        }
        break;
    }
}

static void
nautilus_selection_canvas_item_class_init (NautilusSelectionCanvasItemClass *klass)
{
    EelCanvasItemClass *item_class;
    GObjectClass *gobject_class;

    gobject_class = G_OBJECT_CLASS (klass);
    item_class = EEL_CANVAS_ITEM_CLASS (klass);

    gobject_class->set_property = nautilus_selection_canvas_item_set_property;
    gobject_class->get_property = nautilus_selection_canvas_item_get_property;

    item_class->draw = nautilus_selection_canvas_item_draw;
    item_class->point = nautilus_selection_canvas_item_point;
    item_class->update = nautilus_selection_canvas_item_update;
    item_class->bounds = nautilus_selection_canvas_item_bounds;
    item_class->translate = nautilus_selection_canvas_item_translate;

    properties[PROP_X1] =
        g_param_spec_double ("x1", NULL, NULL,
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                             G_PARAM_READWRITE);
    properties[PROP_Y1] =
        g_param_spec_double ("y1", NULL, NULL,
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                             G_PARAM_READWRITE);
    properties[PROP_X2] =
        g_param_spec_double ("x2", NULL, NULL,
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                             G_PARAM_READWRITE);
    properties[PROP_Y2] =
        g_param_spec_double ("y2", NULL, NULL,
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                             G_PARAM_READWRITE);

    g_object_class_install_properties (gobject_class, NUM_PROPERTIES, properties);
    g_type_class_add_private (klass, sizeof (NautilusSelectionCanvasItemDetails));
}

static void
nautilus_selection_canvas_item_init (NautilusSelectionCanvasItem *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NAUTILUS_TYPE_SELECTION_CANVAS_ITEM,
                                              NautilusSelectionCanvasItemDetails);
}
