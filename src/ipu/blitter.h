/* Common Freescale IPU blitter code for GStreamer
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


#ifndef GST_FSL_IPU_BLITTER_H
#define GST_FSL_IPU_BLITTER_H

#include <gst/gst.h>
#include <gst/video/video.h>


G_BEGIN_DECLS


typedef struct _GstFslIpuBlitter GstFslIpuBlitter;
typedef struct _GstFslIpuBlitterClass GstFslIpuBlitterClass;
typedef struct _GstFslIpuBlitterPrivate GstFslIpuBlitterPrivate;


#define GST_TYPE_FSL_IPU_BLITTER             (gst_fsl_ipu_blitter_get_type())
#define GST_FSL_IPU_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_IPU_BLITTER, GstFslIpuBlitter))
#define GST_FSL_IPU_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_IPU_BLITTER, GstFslIpuBlitterClass))
#define GST_FSL_IPU_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_FSL_IPU_BLITTER, GstFslIpuBlitterClass))
#define GST_IS_FSL_IPU_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FSL_IPU_BLITTER))
#define GST_IS_FSL_IPU_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FSL_IPU_BLITTER))


#define GST_FSL_IPU_VIDEO_FORMATS \
	" { " \
	"   RGB16 " \
	" , BGR " \
	" , RGB " \
	" , BGRx " \
	" , BGRA " \
	" , RGBx " \
	" , RGBA " \
	" , ABGR " \
	" , UYVY " \
	" , v308 " \
	" , NV12 " \
	" , YV12 " \
	" , I420 " \
	" , Y42B " \
	" , Y444 " \
	" } "

#define GST_FSL_IPU_BLITTER_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_FSL_IPU_VIDEO_FORMATS ", " \
		"width = (int) [ 64, MAX ], " \
		"height = (int) [ 64, MAX ], " \
		"framerate = (fraction) [ 0, MAX ]; " \
	)


struct _GstFslIpuBlitter
{
	GstObject parent;
	GstFslIpuBlitterPrivate *priv;

	GstBufferPool *internal_bufferpool;
	GstBuffer *internal_input_buffer;
	GstVideoFrame temp_input_video_frame;
	GstVideoInfo input_video_info;
};


struct _GstFslIpuBlitterClass
{
	GstObjectClass parent_class;
};


GType gst_fsl_ipu_blitter_get_type(void);

gboolean gst_fsl_ipu_blitter_set_input_frame(GstFslIpuBlitter *ipu_blitter, GstVideoFrame *input_frame);
gboolean gst_fsl_ipu_blitter_set_output_frame(GstFslIpuBlitter *ipu_blitter, GstVideoFrame *output_frame);
gboolean gst_fsl_ipu_blitter_set_incoming_frame(GstFslIpuBlitter *ipu_blitter, GstVideoFrame *incoming_frame);
void gst_fsl_ipu_blitter_set_input_info(GstFslIpuBlitter *ipu_blitter, GstVideoInfo *info);
gboolean gst_fsl_ipu_blitter_blit(GstFslIpuBlitter *ipu_blitter);
GstBufferPool* gst_fsl_ipu_blitter_create_bufferpool(GstFslIpuBlitter *ipu_blitter, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params);
GstBufferPool* gst_fsl_ipu_blitter_get_internal_bufferpool(GstFslIpuBlitter *ipu_blitter);
GstBuffer* gst_fsl_ipu_blitter_wrap_framebuffer(GstFslIpuBlitter *ipu_blitter, int framebuffer_fd, guint x, guint y, guint width, guint height);


G_END_DECLS


#endif

