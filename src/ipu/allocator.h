/* Allocation functions for physical memory
 * Copyright (C) 2013  Carlos Rafael Giani
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifndef GST_FSL_IPU_ALLOCATOR_H
#define GST_FSL_IPU_ALLOCATOR_H

#include <gst/gst.h>
#include <gst/gstallocator.h>


G_BEGIN_DECLS


gpointer gst_fsl_ipu_alloc_phys_mem(int ipu_fd, gsize size);
gboolean gst_fsl_ipu_free_phys_mem(int ipu_fd, gpointer mem);


G_END_DECLS


#endif

