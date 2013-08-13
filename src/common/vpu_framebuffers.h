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


#ifndef VPU_FRAMEBUFFERS_H
#define VPU_FRAMEBUFFERS_H

#include <glib.h>
#include <gst/gst.h>
#include <vpu_wrapper.h>


G_BEGIN_DECLS


typedef struct _GstFslVpuFramebuffers GstFslVpuFramebuffers;
typedef struct _GstFslVpuFramebuffersClass GstFslVpuFramebuffersClass;


#define GST_TYPE_FSL_VPU_FRAMEBUFFERS             (gst_fsl_vpu_framebuffers_get_type())
#define GST_FSL_VPU_FRAMEBUFFERS(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_VPU_FRAMEBUFFERS, GstFslVpuFramebuffers))
#define GST_FSL_VPU_FRAMEBUFFERS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_VPU_FRAMEBUFFERS, GstFslVpuFramebuffersClass))
#define GST_FSL_VPU_FRAMEBUFFERS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_FSL_VPU_FRAMEBUFFERS, GstFslVpuFramebuffersClass))
#define GST_IS_FSL_VPU_FRAMEBUFFERS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FSL_VPU_FRAMEBUFFERS))
#define GST_IS_FSL_VPU_FRAMEBUFFERS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FSL_VPU_FRAMEBUFFERS))


struct _GstFslVpuFramebuffers
{
	GstObject parent;

	VpuDecHandle handle;
	gboolean decoder_open;

	VpuFrameBuffer *framebuffers;
	guint num_framebuffers;
	guint num_reserve_framebuffers;
	gint num_available_framebuffers;
	GSList *fb_mem_blocks;
	GMutex available_fb_mutex;

	int y_stride, uv_stride;
	int y_size, u_size, v_size, mv_size;
	int total_size;
};


struct _GstFslVpuFramebuffersClass
{
	GstObjectClass parent_class;
};


GType gst_fsl_vpu_framebuffers_get_type(void);
GstFslVpuFramebuffers * gst_fsl_vpu_framebuffers_new(VpuDecHandle handle, VpuDecInitInfo *init_info);


G_END_DECLS


#endif

