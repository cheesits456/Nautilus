/*
 *  Nautilus SendTo
 *
 *  Copyright (C) 2005 Roberto Majadas
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Author: Roberto Majadas <roberto.majadas@openshine.com>
 *
 */

#include <config.h>
#include <nautilus-extension.h>
#include <glib/gi18n-lib.h>
#include "nautilus-nste.h"


void
nautilus_module_initialize (GTypeModule *module)
{
    nautilus_nste_load (module);

    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
}

void
nautilus_module_shutdown (void)
{
}

void
nautilus_module_list_types (const GType **types,
                            int          *num_types)
{
    static GType type_list[1];

    type_list[0] = NAUTILUS_TYPE_NSTE;
    *types = type_list;

    *num_types = 1;
}
