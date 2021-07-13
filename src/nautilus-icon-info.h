#pragma once

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_LIST_ZOOM_LEVEL_N_ENTRIES (NAUTILUS_LIST_ZOOM_LEVEL_LARGER + 1)
#define NAUTILUS_CANVAS_ZOOM_LEVEL_N_ENTRIES (NAUTILUS_CANVAS_ZOOM_LEVEL_LARGEST + 1)

/* Maximum size of an icon that the icon factory will ever produce */
#define NAUTILUS_ICON_MAXIMUM_SIZE     320

#define NAUTILUS_TYPE_ICON_INFO (nautilus_icon_info_get_type ())
G_DECLARE_FINAL_TYPE (NautilusIconInfo, nautilus_icon_info, NAUTILUS, ICON_INFO, GObject)

NautilusIconInfo *    nautilus_icon_info_new_for_pixbuf               (GdkPixbuf         *pixbuf,
								       int                scale);
NautilusIconInfo *    nautilus_icon_info_lookup                       (GIcon             *icon,
								       int                size,
								       int                scale);
NautilusIconInfo *    nautilus_icon_info_lookup_from_name             (const char        *name,
								       int                size,
								       int                scale);
NautilusIconInfo *    nautilus_icon_info_lookup_from_path             (const char        *path,
								       int                size,
								       int                scale);
gboolean              nautilus_icon_info_is_fallback                  (NautilusIconInfo  *icon);
GdkPixbuf *           nautilus_icon_info_get_pixbuf                   (NautilusIconInfo  *icon);
GdkPixbuf *           nautilus_icon_info_get_pixbuf_nodefault         (NautilusIconInfo  *icon);
GdkPixbuf *           nautilus_icon_info_get_pixbuf_nodefault_at_size (NautilusIconInfo  *icon,
								       gsize              forced_size);
GdkPixbuf *           nautilus_icon_info_get_pixbuf_at_size           (NautilusIconInfo  *icon,
								       gsize              forced_size);
const char *          nautilus_icon_info_get_used_name                (NautilusIconInfo  *icon);

void                  nautilus_icon_info_clear_caches                 (void);

gint  nautilus_get_icon_size_for_stock_size          (GtkIconSize        size);

G_END_DECLS
