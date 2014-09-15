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

#include "base_blitter.h"


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
	GstImxBaseBlitter *blitter;

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
};


/**
 * GstImxBlitterVideoTransformClass
 * @parent_class:             The parent class structure
 * @start:                    Required.
 *                            Called during the NULL->READY state change. Call
 *                            @gst_imx_blitter_video_transform_set_blitter in here.
 *                            (This function must be called in @start.)
 * @stop:                     Optional.
 *                            Called during the READY->NULL state change.
 * @are_video_infos_equal:    Required.
 *                            Checks if in_info and out_info are equal. If they are,
 *                            return TRUE, otherwise FALSE.
 * @are_transforms_necessary: Required.
 *                            Checks if the blit must happen, even if in- and output
 *                            have the exact same format. This is necessary for
 *                            example when rotations are enabled, or deinterlacing etc.
 *                            Returns TRUE if the frame shall always be copied by blitting,
 *                            FALSE if the frame copy shall be done only if the in- and
 *                            output formats are different.
 *
 * The blitter video transform is an abstract base class for defining blitter-based video transform
 * elements (for colorspace conversion, rotation, deinterlacing etc.)
 * It uses a blitter specified with @gst_imx_blitter_video_transform_set_blitter. Derived classes
 * must implement at the @start, the @are_video_infos_equal, and the @are_transforms_necessary
 * functions. @start must internally call @gst_imx_blitter_video_transform_set_blitter.
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
};


GType gst_imx_blitter_video_transform_get_type(void);


/* Sets the blitter the video transform uses for blitting video frames to the
 * output buffer. The blitter is ref'd. If a pointer to another blitter
 * was set previously, this older blitter is first unref'd. If the new and
 * the old blitter pointer are the same, this function does nothing. This
 * function can be called anytime, but must be called at least once inside @start.
 * If something goes wrong, it returns FALSE, otherwise TRUE. (The blitter is
 * ref'd even if it returns FALSE.)
 * NOTE: This function must be called with a mutex lock. Surround this call
 * with GST_IS_IMX_BLITTER_VIDEO_TRANSFORM_LOCK/UNLOCK calls.
 */
gboolean gst_imx_blitter_video_transform_set_blitter(GstImxBlitterVideoTransform *blitter_video_transform, GstImxBaseBlitter *blitter);


G_END_DECLS


#endif
