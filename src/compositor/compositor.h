#ifndef GST_IMX_COMPOSITOR_H
#define GST_IMX_COMPOSITOR_H

#include <gst/gst.h>
#include "gst-backport/gstimxbpvideoaggregator.h"
#include "gst-backport/gstimxbpvideoaggregatorpad.h"
#include "../common/canvas.h"


G_BEGIN_DECLS


typedef struct _GstImxVideoCompositor GstImxVideoCompositor;
typedef struct _GstImxVideoCompositorClass GstImxVideoCompositorClass;
typedef struct _GstImxVideoCompositorPad GstImxVideoCompositorPad;
typedef struct _GstImxVideoCompositorPadClass GstImxVideoCompositorPadClass;


#define GST_TYPE_IMX_VIDEO_COMPOSITOR             (gst_imx_video_compositor_get_type())
#define GST_IMX_VIDEO_COMPOSITOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VIDEO_COMPOSITOR, GstImxVideoCompositor))
#define GST_IMX_VIDEO_COMPOSITOR_CAST(obj)        ((GstImxVideoCompositor *)(obj))
#define GST_IMX_VIDEO_COMPOSITOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VIDEO_COMPOSITOR, GstImxVideoCompositorClass))
#define GST_IS_IMX_VIDEO_COMPOSITOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VIDEO_COMPOSITOR))
#define GST_IS_IMX_VIDEO_COMPOSITOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VIDEO_COMPOSITOR))

#define GST_TYPE_IMX_VIDEO_COMPOSITOR_PAD             (gst_imx_video_compositor_pad_get_type())
#define GST_IMX_VIDEO_COMPOSITOR_PAD(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VIDEO_COMPOSITOR_PAD, GstImxVideoCompositorPad))
#define GST_IMX_VIDEO_COMPOSITOR_PAD_CAST(obj)        ((GstImxVideoCompositorPad *)(obj))
#define GST_IMX_VIDEO_COMPOSITOR_PAD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VIDEO_COMPOSITOR_PAD, GstImxVideoCompositorPadClass))
#define GST_IS_IMX_VIDEO_COMPOSITOR_PAD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VIDEO_COMPOSITOR_PAD))
#define GST_IS_IMX_VIDEO_COMPOSITOR_PAD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VIDEO_COMPOSITOR_PAD))


struct _GstImxVideoCompositor
{
	GstImxBPVideoAggregator parent;
	guint overall_width, overall_height;
	GstBufferPool *dma_bufferpool;
	GstImxRegion overall_region;
	gboolean overall_region_valid;
	gboolean region_fill_necessary;
	guint32 background_color;
};


/**
 * GstImxVideoCompositorClass:
 *
 * The compositor base class takes N input video streams and composes them into
 * one output video stream. Input video streams can have different sizes, formats,
 * framerates etc. The output video stream's caps are determined by the srcpad
 * template caps and by what downstream supports. Since all i.MX blitters support
 * color space conversion, scaling, rotation etc. in one step, it is possible
 * for the compositor to compose such streams without having to rely on explicit
 * conversion elements.
 *
 * The compositor also supports alpha blending and filling regions with one solid color.
 *
 * @parent_class: The parent class structure
 *
 * @get_phys_mem_allocator: Required.
 *                          Returns a GstAllocator which allocates physically contiguous memory.
 *                          Which allocator to use is up to the subclass.
 *                          This vfunc increases the allocator's refcount.
 *                          If something went wrong, it returns NULL.
 * @set_output_frame:       Required.
 *                          Sets the frame that will contain the composed video.
 *                          If the frame is non-NULL, the subclass must ref this frame and keep
 *                          a pointer to it internally. All @draw_frame and @fill_region will
 *                          target this output frame until a different one is set.
 *                          If the frame is NULL, it instructs the subclass to unref any previously
 *                          ref'd output frame. @draw_frame and @fill_region cannot be called
 *                          afterwards unless a non-NULL frame is set again.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 *                          If this returns FALSE, the given frame is *not* ref'd inside.
 *                          In other words, if it returns FALSE, it is not necessary to explicitely
 *                          call @set_output_frame with a NULL frame afterwards.
 * @set_output_video_info:  Optional.
 *                          This gives derived blitters the chance to update any internal state
 *                          related to the video info.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @fill_region:            Required.
 *                          Fills a given region in the output frame with the given color.
 *                          The color is specified as an unsigned 32-bit integer in format: 0x00BBGGRR.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 * @draw_frame:             Required.
 *                          Draws a given input frame on the output frame, using the given
 *                          input info, input region, and output canvas.
 *                          alpha 255 equals 100% opacity; alpha 0 means 100% transparency.
 *                          Returns TRUE if it successfully completed, FALSE otherwise.
 */
struct _GstImxVideoCompositorClass
{
	GstImxBPVideoAggregatorClass parent_class;

	GstAllocator* (*get_phys_mem_allocator)(GstImxVideoCompositor *compositor);
	gboolean (*set_output_frame)(GstImxVideoCompositor *compositor, GstBuffer *output_frame);
	gboolean (*set_output_video_info)(GstImxVideoCompositor *compositor, GstVideoInfo const *info);
	gboolean (*fill_region)(GstImxVideoCompositor *compositor, GstImxRegion const *region, guint32 color);
	gboolean (*draw_frame)(GstImxVideoCompositor *compositor, GstVideoInfo const *input_info, GstImxRegion const *input_region, GstImxCanvas const *output_canvas, GstBuffer *input_frame, guint8 alpha);
};


struct _GstImxVideoCompositorPad
{
	GstImxBPVideoAggregatorPad parent;

	gboolean canvas_needs_update;
	gboolean pad_is_new;
	GstImxCanvas canvas;
	GstImxRegion source_subset;
	gdouble alpha;
	gboolean input_crop;
	gboolean last_frame_with_cropdata;
	GstImxRegion last_source_region;
	gint xpos, ypos, width, height;
};


struct _GstImxVideoCompositorPadClass
{
	GstImxBPVideoAggregatorPadClass parent_class;
};


GType gst_imx_video_compositor_get_type(void);
GType gst_imx_video_compositor_pad_get_type(void);


G_END_DECLS


#endif
