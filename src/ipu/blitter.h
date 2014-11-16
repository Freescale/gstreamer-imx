/* Common Freescale IPU blitter code for GStreamer
 * Copyright (C) 2014  Carlos Rafael Giani
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


#ifndef GST_IMX_IPU_BLITTER_H
#define GST_IMX_IPU_BLITTER_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "../common/base_blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxIpuBlitter GstImxIpuBlitter;
typedef struct _GstImxIpuBlitterClass GstImxIpuBlitterClass;
typedef struct _GstImxIpuBlitterPrivate GstImxIpuBlitterPrivate;


#define GST_TYPE_IMX_IPU_BLITTER             (gst_imx_ipu_blitter_get_type())
#define GST_IMX_IPU_BLITTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_IPU_BLITTER, GstImxIpuBlitter))
#define GST_IMX_IPU_BLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_IPU_BLITTER, GstImxIpuBlitterClass))
#define GST_IMX_IPU_BLITTER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_IPU_BLITTER, GstImxIpuBlitterClass))
#define GST_IS_IMX_IPU_BLITTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_IPU_BLITTER))
#define GST_IS_IMX_IPU_BLITTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_IPU_BLITTER))


#define GST_IMX_IPU_VIDEO_FORMATS \
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

#define GST_IMX_IPU_BLITTER_SINK_CAPS \
	GST_STATIC_CAPS( \
		"video/x-raw, " \
		"format = (string) " GST_IMX_IPU_VIDEO_FORMATS ", " \
		"width = (int) [ 64, MAX ], " \
		"height = (int) [ 64, MAX ], " \
		"framerate = (fraction) [ 0, MAX ], " \
		"interlace-mode = (string) { progressive, mixed, interleaved }; " \
	)

#define GST_IMX_IPU_BLITTER_SRC_CAPS GST_IMX_IPU_BLITTER_SINK_CAPS


typedef enum
{
	GST_IMX_IPU_BLITTER_ROTATION_NONE,
	GST_IMX_IPU_BLITTER_ROTATION_HFLIP,
	GST_IMX_IPU_BLITTER_ROTATION_VFLIP,
	GST_IMX_IPU_BLITTER_ROTATION_180,
	GST_IMX_IPU_BLITTER_ROTATION_90CW,
	GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP,
	GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP,
	GST_IMX_IPU_BLITTER_ROTATION_90CCW
}
GstImxIpuBlitterRotationMode;


typedef enum
{
	GST_IMX_IPU_BLITTER_DEINTERLACE_NONE,
	GST_IMX_IPU_BLITTER_DEINTERLACE_SLOW_MOTION,
	GST_IMX_IPU_BLITTER_DEINTERLACE_FAST_MOTION
}
GstImxIpuBlitterDeinterlaceMode;


#define GST_IMX_IPU_BLITTER_OUTPUT_ROTATION_DEFAULT  GST_IMX_IPU_BLITTER_ROTATION_NONE
#define GST_IMX_IPU_BLITTER_CROP_DEFAULT  FALSE
#define GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT  GST_IMX_IPU_BLITTER_DEINTERLACE_NONE


struct _GstImxIpuBlitter
{
	GstImxBaseBlitter parent;
	GstImxIpuBlitterPrivate *priv;

	GstVideoInfo input_video_info;

	GstBuffer *current_frame, *previous_frame; /* used for deinterlacing */

	GstImxIpuBlitterDeinterlaceMode deinterlace_mode;

	GstImxBaseBlitterRegion output_region;

	gboolean output_region_uptodate;
	GstBuffer *dummy_black_buffer;

	GstAllocator *allocator;
};


struct _GstImxIpuBlitterClass
{
	GstImxBaseBlitterClass parent_class;
};


GType gst_imx_ipu_blitter_rotation_mode_get_type(void);
GType gst_imx_ipu_blitter_deinterlace_mode_get_type(void);


GType gst_imx_ipu_blitter_get_type(void);


GstImxIpuBlitter* gst_imx_ipu_blitter_new(void);

void gst_imx_ipu_blitter_set_output_rotation_mode(GstImxIpuBlitter *ipu_blitter, GstImxIpuBlitterRotationMode rotation_mode);
GstImxIpuBlitterRotationMode gst_imx_ipu_blitter_get_output_rotation_mode(GstImxIpuBlitter *ipu_blitter);

void gst_imx_ipu_blitter_set_deinterlace_mode(GstImxIpuBlitter *ipu_blitter, GstImxIpuBlitterDeinterlaceMode deinterlace_mode);
GstImxIpuBlitterDeinterlaceMode gst_imx_ipu_blitter_get_deinterlace_mode(GstImxIpuBlitter *ipu_blitter);


G_END_DECLS


#endif
