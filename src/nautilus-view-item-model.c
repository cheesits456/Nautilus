#include "nautilus-view-item-model.h"
#include "nautilus-file.h"

struct _NautilusViewItemModel
{
    GObject parent_instance;
    guint icon_size;
    NautilusFile *file;
    GtkLabel *label;
    GtkWidget *item_ui;
};

G_DEFINE_TYPE (NautilusViewItemModel, nautilus_view_item_model, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_FILE,
    PROP_ICON_SIZE,
    PROP_ITEM_UI,
    N_PROPS
};

static void
nautilus_view_item_model_finalize (GObject *object)
{
    G_OBJECT_CLASS (nautilus_view_item_model_parent_class)->finalize (object);
}

static void
nautilus_view_item_model_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    NautilusViewItemModel *self = NAUTILUS_VIEW_ITEM_MODEL (object);

    switch (prop_id)
    {
        case PROP_FILE:
        {
            g_value_set_object (value, self->file);
        }
        break;

        case PROP_ICON_SIZE:
        {
            g_value_set_int (value, self->icon_size);
        }
        break;

        case PROP_ITEM_UI:
        {
            g_value_set_object (value, self->item_ui);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_view_item_model_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    NautilusViewItemModel *self = NAUTILUS_VIEW_ITEM_MODEL (object);

    switch (prop_id)
    {
        case PROP_FILE:
        {
            nautilus_view_item_model_set_file (self, g_value_get_object (value));
        }
        break;

        case PROP_ICON_SIZE:
        {
            nautilus_view_item_model_set_icon_size (self, g_value_get_int (value));
        }
        break;

        case PROP_ITEM_UI:
        {
            nautilus_view_item_model_set_item_ui (self, g_value_get_object (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_view_item_model_init (NautilusViewItemModel *self)
{
}

static void
nautilus_view_item_model_class_init (NautilusViewItemModelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = nautilus_view_item_model_finalize;
    object_class->get_property = nautilus_view_item_model_get_property;
    object_class->set_property = nautilus_view_item_model_set_property;

    g_object_class_install_property (object_class,
                                     PROP_ICON_SIZE,
                                     g_param_spec_int ("icon-size",
                                                       "Icon size",
                                                       "The size in pixels of the icon",
                                                       NAUTILUS_CANVAS_ICON_SIZE_SMALL,
                                                       NAUTILUS_CANVAS_ICON_SIZE_LARGEST,
                                                       NAUTILUS_CANVAS_ICON_SIZE_LARGE,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
    g_object_class_install_property (object_class,
                                     PROP_FILE,
                                     g_param_spec_object ("file",
                                                          "File",
                                                          "The file the icon item represents",
                                                          NAUTILUS_TYPE_FILE,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

    g_object_class_install_property (object_class,
                                     PROP_ITEM_UI,
                                     g_param_spec_object ("item-ui",
                                                          "Item ui",
                                                          "The UI that reprensents the item model",
                                                          GTK_TYPE_WIDGET,
                                                          G_PARAM_READWRITE));
}

NautilusViewItemModel *
nautilus_view_item_model_new (NautilusFile *file,
                              guint         icon_size)
{
    return g_object_new (NAUTILUS_TYPE_VIEW_ITEM_MODEL,
                         "file", file,
                         "icon-size", icon_size,
                         NULL);
}

guint
nautilus_view_item_model_get_icon_size (NautilusViewItemModel *self)
{
    g_return_val_if_fail (NAUTILUS_IS_VIEW_ITEM_MODEL (self), -1);

    return self->icon_size;
}

void
nautilus_view_item_model_set_icon_size (NautilusViewItemModel *self,
                                        guint                  icon_size)
{
    g_return_if_fail (NAUTILUS_IS_VIEW_ITEM_MODEL (self));

    self->icon_size = icon_size;

    g_object_notify (G_OBJECT (self), "icon-size");
}

NautilusFile *
nautilus_view_item_model_get_file (NautilusViewItemModel *self)
{
    g_return_val_if_fail (NAUTILUS_IS_VIEW_ITEM_MODEL (self), NULL);

    return self->file;
}

void
nautilus_view_item_model_set_file (NautilusViewItemModel *self,
                                   NautilusFile          *file)
{
    g_return_if_fail (NAUTILUS_IS_VIEW_ITEM_MODEL (self));

    g_clear_object (&self->file);
    self->file = g_object_ref (file);

    g_object_notify (G_OBJECT (self), "file");
}

GtkWidget *
nautilus_view_item_model_get_item_ui (NautilusViewItemModel *self)
{
    g_return_val_if_fail (NAUTILUS_IS_VIEW_ITEM_MODEL (self), NULL);

    return self->item_ui;
}

void
nautilus_view_item_model_set_item_ui (NautilusViewItemModel *self,
                                      GtkWidget             *item_ui)
{
    g_return_if_fail (NAUTILUS_IS_VIEW_ITEM_MODEL (self));

    g_clear_object (&self->item_ui);
    self->item_ui = g_object_ref (item_ui);

    g_object_notify (G_OBJECT (self), "item-ui");
}
