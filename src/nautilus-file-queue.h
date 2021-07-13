/*
   Copyright (C) 2001 Maciej Stachowiak
  
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

   Author: Maciej Stachowiak <mjs@noisehavoc.org>
*/

#pragma once

#include "nautilus-file.h"

typedef struct NautilusFileQueue NautilusFileQueue;

NautilusFileQueue *nautilus_file_queue_new      (void);
void               nautilus_file_queue_destroy  (NautilusFileQueue *queue);

/* Add a file to the tail of the queue, unless it's already in the queue */
void               nautilus_file_queue_enqueue  (NautilusFileQueue *queue,
						 NautilusFile      *file);

/* Return the file at the head of the queue after removing it from the
 * queue. This is dangerous unless you have another ref to the file,
 * since it will unref it.  
 */
NautilusFile *     nautilus_file_queue_dequeue  (NautilusFileQueue *queue);

/* Remove a file from an arbitrary point in the queue in constant time. */
void               nautilus_file_queue_remove   (NautilusFileQueue *queue,
						 NautilusFile      *file);

/* Get the file at the head of the queue without removing or unrefing it. */
NautilusFile *     nautilus_file_queue_head     (NautilusFileQueue *queue);

gboolean           nautilus_file_queue_is_empty (NautilusFileQueue *queue);
