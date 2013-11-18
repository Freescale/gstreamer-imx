/* GStreamer video decoder using the Freescale VPU hardware video engine
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


#ifndef GST_IMX_VPU_DECODER_H
#define GST_IMX_VPU_DECODER_H

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

#include <vpu_wrapper.h>

#include "../framebuffers.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuDec GstImxVpuDec;
typedef struct _GstImxVpuDecClass GstImxVpuDecClass;


#define GST_TYPE_IMX_VPU_DEC             (gst_imx_vpu_dec_get_type())
#define GST_IMX_VPU_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_DEC, GstImxVpuDec))
#define GST_IMX_VPU_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_DEC, GstImxVpuDecClass))
#define GST_IS_IMX_VPU_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_DEC))
#define GST_IS_IMX_VPU_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_DEC))


struct _GstImxVpuDec
{
	GstVideoDecoder parent;

	VpuDecHandle handle;

	VpuDecInitInfo init_info;
	VpuMemInfo mem_info;

	gboolean vpu_inst_opened, is_mjpeg;

	GstBuffer *codec_data;

	GstImxVpuFramebuffers *current_framebuffers;
	guint min_num_framebuffers;

	GstVideoCodecState *current_output_state;

	GSList *virt_dec_mem_blocks, *phys_dec_mem_blocks;

	GHashTable *frame_table;
};


struct _GstImxVpuDecClass
{
	GstVideoDecoderClass parent_class;
	gint inst_counter;
};


GType gst_imx_vpu_dec_get_type(void);


G_END_DECLS


#endif

