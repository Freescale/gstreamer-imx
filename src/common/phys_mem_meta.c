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


#include "phys_mem_meta.h"


static gboolean gst_imx_phys_mem_meta_init(GstMeta *meta, G_GNUC_UNUSED gpointer params, G_GNUC_UNUSED GstBuffer *buffer)
{
	GstImxPhysMemMeta *imx_phys_mem_meta = (GstImxPhysMemMeta *)meta;
	imx_phys_mem_meta->phys_addr = 0;
	imx_phys_mem_meta->padding = 0;
	return TRUE;
}


GType gst_imx_phys_mem_meta_api_get_type(void)
{
	static volatile GType type;
	static gchar const *tags[] = { "memory", "phys_mem", NULL };

	if (g_once_init_enter(&type))
	{
		GType _type = gst_meta_api_type_register("GstImxPhysMemMetaAPI", tags);
		g_once_init_leave(&type, _type);
	}

	return type;
}


GstMetaInfo const * gst_imx_phys_mem_meta_get_info(void)
{
	static GstMetaInfo const *gst_imx_phys_mem_meta_info = NULL;

	if (g_once_init_enter(&gst_imx_phys_mem_meta_info))
	{
		GstMetaInfo const *meta = gst_meta_register(
			gst_imx_phys_mem_meta_api_get_type(),
			"GstImxPhysMemMeta",
			sizeof(GstImxPhysMemMeta),
			GST_DEBUG_FUNCPTR(gst_imx_phys_mem_meta_init),
			(GstMetaFreeFunction)NULL,
			(GstMetaTransformFunction)NULL
		);
		g_once_init_leave(&gst_imx_phys_mem_meta_info, meta);
	}

	return gst_imx_phys_mem_meta_info;
}

