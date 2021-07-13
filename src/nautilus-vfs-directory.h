/*
   nautilus-vfs-directory.h: Subclass of NautilusDirectory to implement the
   the case of a VFS directory.
 
   Copyright (C) 1999, 2000 Eazel, Inc.
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public
   License along with this program; if not, see <http://www.gnu.org/licenses/>.
  
   Author: Darin Adler <darin@bentspoon.com>
*/

#pragma once

#include "nautilus-directory.h"

#define NAUTILUS_TYPE_VFS_DIRECTORY nautilus_vfs_directory_get_type()
#define NAUTILUS_VFS_DIRECTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), NAUTILUS_TYPE_VFS_DIRECTORY, NautilusVFSDirectory))
#define NAUTILUS_VFS_DIRECTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), NAUTILUS_TYPE_VFS_DIRECTORY, NautilusVFSDirectoryClass))
#define NAUTILUS_IS_VFS_DIRECTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NAUTILUS_TYPE_VFS_DIRECTORY))
#define NAUTILUS_IS_VFS_DIRECTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), NAUTILUS_TYPE_VFS_DIRECTORY))
#define NAUTILUS_VFS_DIRECTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), NAUTILUS_TYPE_VFS_DIRECTORY, NautilusVFSDirectoryClass))

typedef struct NautilusVFSDirectoryDetails NautilusVFSDirectoryDetails;

typedef struct {
	NautilusDirectory parent_slot;
} NautilusVFSDirectory;

typedef struct {
	NautilusDirectoryClass parent_slot;
} NautilusVFSDirectoryClass;

GType   nautilus_vfs_directory_get_type (void);