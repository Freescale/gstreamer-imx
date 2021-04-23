/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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

#ifndef GST_IMX_DEFAULT_ALLOCATOR_H
#define GST_IMX_DEFAULT_ALLOCATOR_H

#include <gst/gst.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_DEFAULT_ALLOCATOR             (gst_imx_default_allocator_get_type())
#define GST_IMX_DEFAULT_ALLOCATOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_DEFAULT_ALLOCATOR, GstImxDefaultAllocator))
#define GST_IMX_DEFAULT_ALLOCATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_DEFAULT_ALLOCATOR, GstImxDefaultAllocatorClass))
#define GST_IMX_DEFAULT_ALLOCATOR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_DEFAULT_ALLOCATOR, GstImxDefaultAllocatorClass))
#define GST_IMX_DEFAULT_ALLOCATOR_CAST(obj)        ((GstImxDefaultAllocator *)(obj))
#define GST_IS_IMX_DEFAULT_ALLOCATOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_DEFAULT_ALLOCATOR))
#define GST_IS_IMX_DEFAULT_ALLOCATOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_DEFAULT_ALLOCATOR))


typedef struct _GstImxDefaultAllocator GstImxDefaultAllocator;
typedef struct _GstImxDefaultAllocatorClass GstImxDefaultAllocatorClass;


GType gst_imx_default_allocator_get_type(void);

GstAllocator* gst_imx_default_allocator_new(void);


G_END_DECLS


#endif /* GST_IMX_DEFAULT_ALLOCATOR_H */
