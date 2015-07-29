/* GStreamer base class for i.MX blitter based video transform elements
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


#ifndef GST_IMX_BLITTER_VIDEO_TRANSFORM_H
#define GST_IMX_BLITTER_VIDEO_TRANSFORM_H

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "blitter.h"


G_BEGIN_DECLS


typedef struct _GstImxBlitterVideoTransform GstImxBlitterVideoTransform;
typedef struct _GstImxBlitterVideoTransformClass GstImxBlitterVideoTransformClass;
typedef struct _GstImxBlitterVideoTransformPrivate GstImxBlitterVideoTransformPrivate;


#define GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM             (gst_imx_blitter_video_transform_get_type())
#define GST_IMX_BLITTER_VIDEO_TRANSFORM(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM, GstImxBlitterVideoTransform))
#define GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM, GstImxBlitterVideoTransformClass))
#define GST_IS_IMX_BLITTER_VIDEO_TRANSFORM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM))
#define GST_IS_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM))
#define GST_IMX_BLITTER_VIDEO_TRANSFORM_CAST(obj)        ((GstImxBlitterVideoTransform*)(obj))


/* Macros for locking/unlocking the blitter video transform's mutex.
 * These should always be used when a property is set that affects
 * the blit operation. */
#define GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(obj) do { g_mutex_lock(&(((GstImxBlitterVideoTransform*)(obj))->mutex)); } while (0)
#define GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(obj) do { g_mutex_unlock(&(((GstImxBlitterVideoTransform*)(obj))->mutex)); } while (0)


#define GST_IMX_BLITTER_VIDEO_TRANSFORM_INPUT_INFO(obj)  (&(((GstImxBlitterVideoTransform*)(obj))->input_video_info))
#define GST_IMX_BLITTER_VIDEO_TRANSFORM_OUTPUT_INFO(obj) (&(((GstImxBlitterVideoTransform*)(obj))->output_video_info))


/**
 * GstImxBlitterVideoTransform:
 *
 * The opaque #GstImxBlitterVideoTransform data structure.
 */
struct _GstImxBlitterVideoTransform
{
	GstBaseTransform parent;

	/*< protected >*/

	/* Mutex protecting the set input frame - set output frame - blit
	 * sequence inside transform_frame */
	GMutex mutex;

	/* The blitter to be used; is unref'd in the READY->NULL state change */
	GstImxBlitter *blitter;

	/* Flag to indicate initialization status; FALSE if
	 * the transform element is in the NULL state, TRUE otherwise */
	gboolean initialized;

	/* Flags for equality check and for checking if input & output info
	 * (based on input & output caps) have been set */
	gboolean inout_info_equal, inout_info_set;

	/* Copy of the GstVideoInfo structure generated from the input caps */
	GstVideoInfo input_video_info;
	/* Copy of the GstVideoInfo structure generated from the output caps */
	GstVideoInfo output_video_info;

	/* Output canvas. Aspect ratio is *not* kept. */
	GstImxCanvas canvas;

	/* If true, crop rectangles in videocrop metas will be supported */
	gboolean input_crop;
	/* If true, then the last frame contained crop metadata */
	gboolean last_frame_with_cropdata;

	/* Last seen source region (used for cropping) */
	GstImxRegion last_source_region;
};


/**
 * GstImxBlitterVideoTransformClass
 * @parent_class:             The parent class structure
 * @start:                    Optional.
 *                            Called during the NULL->READY state change. Note that this is
 *                            called before @create_blitter.
 *                            If this returns FALSE, then the state change is considered to
 *                            have failed, and the transform's change_state function will return
 *                            GST_STATE_CHANGE_FAILURE.
 * @stop:                     Optional.
 *                            Called during the READY->NULL state change.
 *                            Returns FALSE if stopping failed, TRUE otherwise.
 * @are_video_infos_equal:    Required.
 *                            Checks if in_info and out_info are equal. If they are,
 *                            return TRUE, otherwise FALSE.
 * @are_transforms_necessary: Optional.
 *                            Checks if the blit must happen, even if in- and output
 *                            have the exact same format. This is necessary for
 *                            example when rotations are enabled, or deinterlacing etc.
 *                            Returns TRUE if the frame shall always be copied by blitting,
 *                            FALSE if the frame copy shall be done only if the in- and
 *                            output formats are different.
 *                            If this is NULL, the element behaves as if this function
 *                            always returned FALSE.
 * @create_blitter:           Required.
 *                            Instructs the subclass to create a new blitter instance and
 *                            return it. If the subclass is supposed to create the blitter
 *                            only once, then create it in @start, ref it here, and then
 *                            return it. It will be unref'd in the READY->NULL state change.
 *
 * The blitter video transform is an abstract base class for defining blitter-based video transform
 * elements (for colorspace conversion, rotation, deinterlacing etc.)
 * It uses a blitter that is created by @create_blitter. Derived classes must implement at least the
 * @create_blitter, the @are_video_infos_equal, and the @are_transforms_necessary functions.
 *
 * If derived classes implement @set_property/@get_property functions, and these modify states
 * related to the blitter, these must surround the modifications with mutex locks. Use
 * @GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK and @GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK for this.
 */
struct _GstImxBlitterVideoTransformClass
{
	GstBaseTransformClass parent_class;

	gboolean (*start)(GstImxBlitterVideoTransform *blitter_video_transform);
	gboolean (*stop)(GstImxBlitterVideoTransform *blitter_video_transform);

	gboolean (*are_video_infos_equal)(GstImxBlitterVideoTransform *blitter_video_transform, GstVideoInfo const *in_info, GstVideoInfo const *out_info);

	gboolean (*are_transforms_necessary)(GstImxBlitterVideoTransform *blitter_video_transform, GstBuffer *input);

	GstImxBlitter* (*create_blitter)(GstImxBlitterVideoTransform *blitter_video_transform);
};


GType gst_imx_blitter_video_transform_get_type(void);


G_END_DECLS


#endif
