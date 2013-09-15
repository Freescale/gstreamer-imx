/* VPU registered framebuffers structure
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


#ifndef GST_FSL_VPU_FRAMEBUFFERS_H
#define GST_FSL_VPU_FRAMEBUFFERS_H

#include <glib.h>
#include <gst/gst.h>
#include <vpu_wrapper.h>
#include "../common/alloc.h"


G_BEGIN_DECLS


typedef struct _GstFslVpuFramebuffers GstFslVpuFramebuffers;
typedef struct _GstFslVpuFramebuffersClass GstFslVpuFramebuffersClass;


#define GST_TYPE_FSL_VPU_FRAMEBUFFERS             (gst_fsl_vpu_framebuffers_get_type())
#define GST_FSL_VPU_FRAMEBUFFERS(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_VPU_FRAMEBUFFERS, GstFslVpuFramebuffers))
#define GST_FSL_VPU_FRAMEBUFFERS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_VPU_FRAMEBUFFERS, GstFslVpuFramebuffersClass))
#define GST_FSL_VPU_FRAMEBUFFERS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_FSL_VPU_FRAMEBUFFERS, GstFslVpuFramebuffersClass))
#define GST_IS_FSL_VPU_FRAMEBUFFERS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FSL_VPU_FRAMEBUFFERS))
#define GST_IS_FSL_VPU_FRAMEBUFFERS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FSL_VPU_FRAMEBUFFERS))


typedef enum
{
	GST_FSL_VPU_FRAMEBUFFERS_UNREGISTERED,
	GST_FSL_VPU_FRAMEBUFFERS_DECODER_REGISTERED,
	GST_FSL_VPU_FRAMEBUFFERS_ENCODER_REGISTERED
} GstFslVpuFramebuffersRegistrationState;


typedef union
{
	struct
	{
		VpuDecHandle handle;
		gboolean decoder_open;
	} dec;
	struct
	{
		VpuEncHandle handle;
		gboolean encoder_open;
	} enc;
}
GstFslVpuFramebuffersDecEncStates;


struct _GstFslVpuFramebuffers
{
	GstObject parent;

	GstFslVpuFramebuffersDecEncStates decenc_states;

	GstFslVpuFramebuffersRegistrationState registration_state;

	gst_fsl_phys_mem_allocator *phys_mem_alloc;

	VpuFrameBuffer *framebuffers;
	guint num_framebuffers;
	guint num_reserve_framebuffers;
	gint num_available_framebuffers;
	GSList *fb_mem_blocks;
	GMutex available_fb_mutex;

	int y_stride, uv_stride;
	int y_size, u_size, v_size, mv_size;
	int total_size;

	guint pic_width, pic_height;
};


struct _GstFslVpuFramebuffersClass
{
	GstObjectClass parent_class;
};


typedef struct
{
	gint
		pic_width,
		pic_height,
		min_framebuffer_count,
		mjpeg_source_format,
		interlace,
		address_alignment;
}
GstFslVpuFramebufferParams;


GType gst_fsl_vpu_framebuffers_get_type(void);
GstFslVpuFramebuffers * gst_fsl_vpu_framebuffers_new(GstFslVpuFramebufferParams *params, gst_fsl_phys_mem_allocator *phys_mem_alloc);
gboolean gst_fsl_vpu_framebuffers_register_with_decoder(GstFslVpuFramebuffers *framebuffers, VpuDecHandle handle);
gboolean gst_fsl_vpu_framebuffers_register_with_encoder(GstFslVpuFramebuffers *framebuffers, VpuEncHandle handle, guint src_stride);
void gst_fsl_vpu_dec_init_info_to_params(VpuDecInitInfo *init_info, GstFslVpuFramebufferParams *params);
void gst_fsl_vpu_enc_init_info_to_params(VpuEncInitInfo *init_info, GstFslVpuFramebufferParams *params);


G_END_DECLS


#endif

