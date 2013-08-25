/* Freescale i.MX specific GStreamer meta data structures
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


#ifndef FSL_GSTMETA_H
#define FSL_GSTMETA_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <vpu_wrapper.h>


G_BEGIN_DECLS


typedef struct _GstFslVpuBufferMeta GstFslVpuBufferMeta;
typedef struct _GstFslPhysMemMeta GstFslPhysMemMeta;


#define GST_FSL_VPU_BUFFER_META_GET(buffer)      ((GstFslVpuBufferMeta *)gst_buffer_get_meta((buffer), gst_fsl_vpu_buffer_meta_api_get_type()))
#define GST_FSL_VPU_BUFFER_META_ADD(buffer)      (gst_buffer_add_meta((buffer), gst_fsl_vpu_buffer_meta_get_info(), NULL))
#define GST_FSL_VPU_BUFFER_META_DEL(buffer)      (gst_buffer_remove_meta((buffer), gst_buffer_get_meta((buffer), gst_fsl_vpu_buffer_meta_api_get_type())))


#define GST_FSL_PHYS_MEM_META_GET(buffer)      ((GstFslPhysMemMeta *)gst_buffer_get_meta((buffer), gst_fsl_phys_mem_meta_api_get_type()))
#define GST_FSL_PHYS_MEM_META_ADD(buffer)      (gst_buffer_add_meta((buffer), gst_fsl_phys_mem_meta_get_info(), NULL))
#define GST_FSL_PHYS_MEM_META_DEL(buffer)      (gst_buffer_remove_meta((buffer), gst_buffer_get_meta((buffer), gst_fsl_phys_mem_meta_api_get_type())))


struct _GstFslVpuBufferMeta
{
	GstMeta meta;

	VpuFrameBuffer *framebuffer;
	gboolean not_displayed_yet;
};


struct _GstFslPhysMemMeta
{
	GstMeta meta;

	gpointer virt_addr, phys_addr;
	gsize padding;
};


GType gst_fsl_vpu_buffer_meta_api_get_type(void);
GstMetaInfo const * gst_fsl_vpu_buffer_meta_get_info(void);

GType gst_fsl_phys_mem_meta_api_get_type(void);
GstMetaInfo const * gst_fsl_phys_mem_meta_get_info(void);


G_END_DECLS


#endif


