#ifndef GST_IMX_COMPOSITOR_H
#define GST_IMX_COMPOSITOR_H

#include <gst/gst.h>
#include "gst-backport/gstimxbpvideoaggregator.h"
#include "gst-backport/gstimxbpvideoaggregatorpad.h"
#include "../common/canvas.h"


G_BEGIN_DECLS


typedef struct _GstImxCompositor GstImxCompositor;
typedef struct _GstImxCompositorClass GstImxCompositorClass;
typedef struct _GstImxCompositorPad GstImxCompositorPad;
typedef struct _GstImxCompositorPadClass GstImxCompositorPadClass;


#define GST_TYPE_IMX_COMPOSITOR             (gst_imx_compositor_get_type())
#define GST_IMX_COMPOSITOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_COMPOSITOR, GstImxCompositor))
#define GST_IMX_COMPOSITOR_CAST(obj)        ((GstImxCompositor *)(obj))
#define GST_IMX_COMPOSITOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_COMPOSITOR, GstImxCompositorClass))
#define GST_IS_IMX_COMPOSITOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_COMPOSITOR))
#define GST_IS_IMX_COMPOSITOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_COMPOSITOR))

#define GST_TYPE_IMX_COMPOSITOR_PAD             (gst_imx_compositor_pad_get_type())
#define GST_IMX_COMPOSITOR_PAD(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_COMPOSITOR_PAD, GstImxCompositorPad))
#define GST_IMX_COMPOSITOR_PAD_CAST(obj)        ((GstImxCompositorPad *)(obj))
#define GST_IMX_COMPOSITOR_PAD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_COMPOSITOR_PAD, GstImxCompositorPadClass))
#define GST_IS_IMX_COMPOSITOR_PAD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_COMPOSITOR_PAD))
#define GST_IS_IMX_COMPOSITOR_PAD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_COMPOSITOR_PAD))


struct _GstImxCompositor
{
	GstImxBPVideoAggregator parent;
	guint overall_width, overall_height;
	GstBufferPool *dma_bufferpool;
	GstImxRegion overall_region;
	gboolean overall_region_valid;
};


struct _GstImxCompositorClass
{
	GstImxBPVideoAggregatorClass parent_class;

	GstAllocator* (*get_phys_mem_allocator)(GstImxCompositor *compositor);
	void (*set_output_frame)(GstImxCompositor *compositor, GstBuffer *output_frame);
	void (*set_output_video_info)(GstImxCompositor *compositor, GstVideoInfo const *info);
	void (*draw_frame)(GstImxCompositor *compositor, GstVideoInfo const *input_info, GstImxRegion const *input_region, GstImxCanvas const *output_canvas, GstBuffer *input_frame, guint8 alpha);
};


struct _GstImxCompositorPad
{
	GstImxBPVideoAggregatorPad parent;
	gboolean canvas_needs_update;
	GstImxCanvas canvas;
	GstImxRegion source_subset;
	guint8 alpha;
};


struct _GstImxCompositorPadClass
{
	GstImxBPVideoAggregatorPadClass parent_class;
};


GType gst_imx_compositor_get_type(void);
GType gst_imx_compositor_pad_get_type(void);


G_END_DECLS


#endif
