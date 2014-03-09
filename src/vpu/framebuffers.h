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


#ifndef GST_IMX_VPU_FRAMEBUFFERS_H
#define GST_IMX_VPU_FRAMEBUFFERS_H

#include <glib.h>
#include <gst/gst.h>
#include <vpu_wrapper.h>


G_BEGIN_DECLS


typedef struct _GstImxVpuFramebuffers GstImxVpuFramebuffers;
typedef struct _GstImxVpuFramebuffersClass GstImxVpuFramebuffersClass;


#define GST_TYPE_IMX_VPU_FRAMEBUFFERS             (gst_imx_vpu_framebuffers_get_type())
#define GST_IMX_VPU_FRAMEBUFFERS(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_FRAMEBUFFERS, GstImxVpuFramebuffers))
#define GST_IMX_VPU_FRAMEBUFFERS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_FRAMEBUFFERS, GstImxVpuFramebuffersClass))
#define GST_IMX_VPU_FRAMEBUFFERS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_FRAMEBUFFERS, GstImxVpuFramebuffersClass))
#define GST_IS_IMX_VPU_FRAMEBUFFERS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_FRAMEBUFFERS))
#define GST_IS_IMX_VPU_FRAMEBUFFERS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_FRAMEBUFFERS))

#define GST_IMX_VPU_MIN_NUM_FREE_FRAMEBUFFERS 6


typedef enum
{
	GST_IMX_VPU_FRAMEBUFFERS_UNREGISTERED,
	GST_IMX_VPU_FRAMEBUFFERS_DECODER_REGISTERED,
	GST_IMX_VPU_FRAMEBUFFERS_ENCODER_REGISTERED
} GstImxVpuFramebuffersRegistrationState;


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
GstImxVpuFramebuffersDecEncStates;


struct _GstImxVpuFramebuffers
{
	GstObject parent;

	GstImxVpuFramebuffersDecEncStates decenc_states;

	GstImxVpuFramebuffersRegistrationState registration_state;

	GstAllocator *allocator;

	VpuFrameBuffer *framebuffers;
	guint num_framebuffers;
	gint num_available_framebuffers, decremented_availbuf_counter;
	GSList *fb_mem_blocks;
	GMutex available_fb_mutex;
	GCond cond;
	gboolean flushing, exit_loop;

	int y_stride, uv_stride;
	int y_size, u_size, v_size, mv_size;
	int total_size;

	guint pic_width, pic_height;
};


struct _GstImxVpuFramebuffersClass
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
GstImxVpuFramebufferParams;


#define GST_IMX_VPU_FRAMEBUFFERS_LOCK(framebuffers)   (g_mutex_lock(&(((GstImxVpuFramebuffers*)(framebuffers))->available_fb_mutex)))
#define GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(framebuffers) (g_mutex_unlock(&(((GstImxVpuFramebuffers*)(framebuffers))->available_fb_mutex)))


GType gst_imx_vpu_framebuffers_get_type(void);

GstImxVpuFramebuffers * gst_imx_vpu_framebuffers_new(GstImxVpuFramebufferParams *params, GstAllocator *allocator);

gboolean gst_imx_vpu_framebuffers_register_with_decoder(GstImxVpuFramebuffers *framebuffers, VpuDecHandle handle);
gboolean gst_imx_vpu_framebuffers_register_with_encoder(GstImxVpuFramebuffers *framebuffers, VpuEncHandle handle, guint src_stride);

void gst_imx_vpu_framebuffers_dec_init_info_to_params(VpuDecInitInfo *init_info, GstImxVpuFramebufferParams *params);
void gst_imx_vpu_framebuffers_enc_init_info_to_params(VpuEncInitInfo *init_info, GstImxVpuFramebufferParams *params);

/* NOTE: the two functions below must be called with a lock held on framebuffers! */
void gst_imx_vpu_framebuffers_set_flushing(GstImxVpuFramebuffers *framebuffers, gboolean flushing);
void gst_imx_vpu_framebuffers_wait_until_frames_available(GstImxVpuFramebuffers *framebuffers);
void gst_imx_vpu_framebuffers_exit_wait_loop(GstImxVpuFramebuffers *framebuffers);


G_END_DECLS


#endif

