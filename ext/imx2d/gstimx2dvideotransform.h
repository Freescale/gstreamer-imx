/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2020  Carlos Rafael Giani
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

#ifndef GST_IMX_2D_VIDEO_TRANSFORM_H
#define GST_IMX_2D_VIDEO_TRANSFORM_H

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include "gst/imx/video/gstimxvideobufferpool.h"
#include "gst/imx/video/gstimxvideouploader.h"
#include "imx2d/imx2d.h"
#include "gstimx2dmisc.h"
#include "gstimx2dvideooverlayhandler.h"


G_BEGIN_DECLS


#define GST_TYPE_IMX_2D_VIDEO_TRANSFORM             (gst_imx_2d_video_transform_get_type())
#define GST_IMX_2D_VIDEO_TRANSFORM(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_2D_VIDEO_TRANSFORM, GstImx2dVideoTransform))
#define GST_IMX_2D_VIDEO_TRANSFORM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_2D_VIDEO_TRANSFORM, GstImx2dVideoTransformClass))
#define GST_IMX_2D_VIDEO_TRANSFORM_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_2D_VIDEO_TRANSFORM, GstImx2dVideoTransformClass))
#define GST_IMX_2D_VIDEO_TRANSFORM_CAST(obj)        ((GstImx2dVideoTransform *)(obj))
#define GST_IS_IMX_2D_VIDEO_TRANSFORM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_2D_VIDEO_TRANSFORM))
#define GST_IS_IMX_2D_VIDEO_TRANSFORM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_2D_VIDEO_TRANSFORM))


typedef struct _GstImx2dVideoTransform GstImx2dVideoTransform;
typedef struct _GstImx2dVideoTransformClass GstImx2dVideoTransformClass;


struct _GstImx2dVideoTransform
{
	GstBaseTransform parent;

	/*< private >*/

	GstImxVideoUploader *uploader;
	GstAllocator *imx_dma_buffer_allocator;

	GstImxVideoBufferPool *video_buffer_pool;

	Imx2dBlitter *blitter;

	gboolean inout_info_equal;
	gboolean inout_info_set;

	gboolean passing_through_overlay_meta;

	GstVideoInfo input_video_info;
	GstVideoInfo output_video_info;

	GstCaps *input_caps;

	Imx2dSurface *input_surface;
	Imx2dSurface *output_surface;

	Imx2dSurfaceDesc input_surface_desc;

	GstImx2dVideoOverlayHandler *overlay_handler;

	gboolean input_crop;
	GstVideoOrientationMethod video_direction;
	gboolean disable_passthrough;

	GstVideoOrientationMethod tag_video_direction;
};


struct _GstImx2dVideoTransformClass
{
	GstBaseTransformClass parent_class;

	gboolean (*start)(GstImx2dVideoTransform *imx_2d_video_transform);
	gboolean (*stop)(GstImx2dVideoTransform *imx_2d_video_transform);

	Imx2dBlitter* (*create_blitter)(GstImx2dVideoTransform *imx_2d_video_transform);

	Imx2dHardwareCapabilities const *hardware_capabilities;
};


GType gst_imx_2d_video_transform_get_type(void);


void gst_imx_2d_video_transform_common_class_init(GstImx2dVideoTransformClass *klass, Imx2dHardwareCapabilities const *capabilities);


G_END_DECLS


#endif /* GST_IMX_2D_VIDEO_TRANSFORM_H */
