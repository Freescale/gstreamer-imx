/* GStreamer meta data structure for physical memory information
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


#ifndef GST_IMX_COMMON_PHYS_MEM_META_H
#define GST_IMX_COMMON_PHYS_MEM_META_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>


G_BEGIN_DECLS


typedef struct _GstImxPhysMemMeta GstImxPhysMemMeta;


#define GST_IMX_PHYS_MEM_META_GET(buffer)      ((GstImxPhysMemMeta *)gst_buffer_get_meta((buffer), gst_imx_phys_mem_meta_api_get_type()))
#define GST_IMX_PHYS_MEM_META_ADD(buffer)      ((GstImxPhysMemMeta *)gst_buffer_add_meta((buffer), gst_imx_phys_mem_meta_get_info(), NULL))
#define GST_IMX_PHYS_MEM_META_DEL(buffer)      (gst_buffer_remove_meta((buffer), gst_buffer_get_meta((buffer), gst_imx_phys_mem_meta_api_get_type())))


#define GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM "GstBufferPoolOptionImxPhysMem"


struct _GstImxPhysMemMeta
{
	GstMeta meta;

	guintptr phys_addr;
	gsize x_padding, y_padding;
	GstBuffer *parent;
};


GType gst_imx_phys_mem_meta_api_get_type(void);
GstMetaInfo const * gst_imx_phys_mem_meta_get_info(void);


G_END_DECLS


#endif


