#include "compositor.h"




GST_DEBUG_CATEGORY_STATIC(imx_blitter_compositor_debug);
#define GST_CAT_DEFAULT imx_blitter_compositor_debug


G_DEFINE_ABSTRACT_TYPE(GstImxBlitterCompositor, gst_imx_blitter_compositor, GST_TYPE_IMX_COMPOSITOR)


static void gst_imx_blitter_compositor_dispose(GObject *object);

static GstStateChangeReturn gst_imx_blitter_compositor_change_state(GstElement *element, GstStateChange transition);

GstAllocator* gst_imx_blitter_compositor_get_phys_mem_allocator(GstImxCompositor *compositor);
gboolean gst_imx_blitter_compositor_set_output_frame(GstImxCompositor *compositor, GstBuffer *output_frame);
gboolean gst_imx_blitter_compositor_set_output_video_info(GstImxCompositor *compositor, GstVideoInfo const *info);
gboolean gst_imx_blitter_compositor_fill_region(GstImxCompositor *compositor, GstImxRegion const *region, guint32 color);
gboolean gst_imx_blitter_compositor_draw_frame(GstImxCompositor *compositor, GstVideoInfo const *input_info, GstImxRegion const *input_region, GstImxCanvas const *output_canvas, GstBuffer *input_frame, guint8 alpha);




/* functions declared by G_DEFINE_ABSTRACT_TYPE */

static void gst_imx_blitter_compositor_class_init(GstImxBlitterCompositorClass *klass)
{
	GObjectClass *object_class;
	GstImxCompositorClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_blitter_compositor_debug, "imxblittercompositor", 0, "Freescale i.MX blitter compositor base class");

	object_class = G_OBJECT_CLASS(klass);
	parent_class = GST_IMX_COMPOSITOR_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_dispose);

	element_class->change_state = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_change_state);

	parent_class->get_phys_mem_allocator = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_get_phys_mem_allocator);
	parent_class->set_output_frame       = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_set_output_frame);
	parent_class->set_output_video_info  = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_set_output_video_info);
	parent_class->fill_region            = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_fill_region);
	parent_class->draw_frame             = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_draw_frame);

	klass->start          = NULL;
	klass->stop           = NULL;
	klass->create_blitter = NULL;
}


static void gst_imx_blitter_compositor_init(GstImxBlitterCompositor *blitter_compositor)
{
	blitter_compositor->blitter = NULL;
}


static void gst_imx_blitter_compositor_dispose(GObject *object)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(object);

	if (blitter_compositor->blitter != NULL)
	{
		gst_object_unref(blitter_compositor->blitter);
		blitter_compositor->blitter = NULL;
	}

	G_OBJECT_CLASS(gst_imx_blitter_compositor_parent_class)->dispose(object);
}


static GstStateChangeReturn gst_imx_blitter_compositor_change_state(GstElement *element, GstStateChange transition)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(element);
	GstImxBlitterCompositorClass *klass = GST_IMX_BLITTER_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(element));
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(klass->create_blitter != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if ((klass->start != NULL) && !(klass->start(blitter_compositor)))
			{
				GST_ERROR_OBJECT(blitter_compositor, "start() failed");
				return GST_STATE_CHANGE_FAILURE;
			}

			if ((blitter_compositor->blitter = klass->create_blitter(blitter_compositor)) == NULL)
			{
				GST_ERROR_OBJECT(blitter_compositor, "could not get blitter");
				return GST_STATE_CHANGE_FAILURE;
			}

			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_blitter_compositor_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			if ((klass->stop != NULL) && !(klass->stop(blitter_compositor)))
				GST_ERROR_OBJECT(blitter_compositor, "stop() failed");

			if (blitter_compositor->blitter != NULL)
			{
				gst_object_unref(blitter_compositor->blitter);
				blitter_compositor->blitter = NULL;
			}

			break;
		}

		default:
			break;
	}

	return ret;
}


GstAllocator* gst_imx_blitter_compositor_get_phys_mem_allocator(GstImxCompositor *compositor)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_get_phys_mem_allocator(blitter_compositor->blitter);
}


gboolean gst_imx_blitter_compositor_set_output_frame(GstImxCompositor *compositor, GstBuffer *output_frame)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_set_output_frame(blitter_compositor->blitter, output_frame);
}


gboolean gst_imx_blitter_compositor_set_output_video_info(GstImxCompositor *compositor, GstVideoInfo const *info)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_set_output_video_info(blitter_compositor->blitter, info);
}


gboolean gst_imx_blitter_compositor_fill_region(GstImxCompositor *compositor, GstImxRegion const *region, guint32 color)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_fill_region(blitter_compositor->blitter, region, color);
}


gboolean gst_imx_blitter_compositor_draw_frame(GstImxCompositor *compositor, GstVideoInfo const *input_info, GstImxRegion const *input_region, GstImxCanvas const *output_canvas, GstBuffer *input_frame, guint8 alpha)
{
	gboolean ret = TRUE;
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	ret = ret && gst_imx_blitter_set_input_video_info(blitter_compositor->blitter, input_info);
	ret = ret && gst_imx_blitter_set_input_region(blitter_compositor->blitter, input_region);
	ret = ret && gst_imx_blitter_set_input_frame(blitter_compositor->blitter, input_frame);
	ret = ret && gst_imx_blitter_set_output_canvas(blitter_compositor->blitter, output_canvas);

	ret = ret && gst_imx_blitter_blit(blitter_compositor->blitter, alpha);

	return ret;
}
