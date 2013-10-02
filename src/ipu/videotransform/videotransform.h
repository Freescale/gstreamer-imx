/* Video transform element using the Freescale IPU
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


#ifndef GST_IMX_IPU_VIDEOTRANSFORM_H
#define GST_IMX_IPU_VIDEOTRANSFORM_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>


G_BEGIN_DECLS


typedef struct _GstImxIpuVideoTransform GstImxIpuVideoTransform;
typedef struct _GstImxIpuVideoTransformClass GstImxIpuVideoTransformClass;
typedef struct _GstImxIpuVideoTransformPrivate GstImxIpuVideoTransformPrivate;


#define GST_TYPE_IMX_IPU_VIDEO_TRANSFORM             (gst_imx_ipu_video_transform_get_type())
#define GST_IMX_IPU_VIDEO_TRANSFORM(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_IPU_VIDEO_TRANSFORM, GstImxIpuVideoTransform))
#define GST_IMX_IPU_VIDEO_TRANSFORM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_IPU_VIDEO_TRANSFORM, GstImxIpuVideoTransformClass))
#define GST_IS_IMX_IPU_VIDEO_TRANSFORM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_IPU_VIDEO_TRANSFORM))
#define GST_IS_IMX_IPU_VIDEO_TRANSFORM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_IPU_VIDEO_TRANSFORM))
#define GST_IMX_IPU_VIDEO_TRANSFORM_CAST(obj)        ((GstImxIpuVideoTransform*)(obj))


struct _GstImxIpuVideoTransform
{
	GstVideoFilter parent;
	GstImxIpuVideoTransformPrivate *priv;
};


struct _GstImxIpuVideoTransformClass
{
	GstVideoFilterClass parent_class;
};


GType gst_imx_ipu_video_transform_get_type(void);


G_END_DECLS


#endif
