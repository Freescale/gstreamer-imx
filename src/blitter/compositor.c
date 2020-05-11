#include "compositor.h"
#include "../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(imx_blitter_compositor_debug);
#define GST_CAT_DEFAULT imx_blitter_compositor_debug


G_DEFINE_ABSTRACT_TYPE(GstImxBlitterCompositor, gst_imx_blitter_compositor, GST_TYPE_IMX_VIDEO_COMPOSITOR)


static void gst_imx_blitter_compositor_dispose(GObject *object);

static GstStateChangeReturn gst_imx_blitter_compositor_change_state(GstElement *element, GstStateChange transition);

static void gst_imx_blitter_compositor_release_pad(GstElement *element, GstPad *pad);

GstAllocator* gst_imx_blitter_compositor_get_phys_mem_allocator(GstImxVideoCompositor *compositor);
gboolean gst_imx_blitter_compositor_set_output_frame(GstImxVideoCompositor *compositor, GstBuffer *output_frame);
gboolean gst_imx_blitter_compositor_set_output_video_info(GstImxVideoCompositor *compositor, GstVideoInfo const *info);
gboolean gst_imx_blitter_compositor_fill_region(GstImxVideoCompositor *compositor, GstImxRegion const *region, guint32 color);
gboolean gst_imx_blitter_compositor_draw_frame(GstImxVideoCompositor *compositor, GstImxVideoCompositorPad *pad, GstVideoInfo const *input_info, GstImxRegion const *input_region, GstImxCanvas const *output_canvas, GstBuffer **input_frame, guint8 alpha);




/* functions declared by G_DEFINE_ABSTRACT_TYPE */

static void gst_imx_blitter_compositor_class_init(GstImxBlitterCompositorClass *klass)
{
	GObjectClass *object_class;
	GstImxVideoCompositorClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_blitter_compositor_debug, "imxblittercompositor", 0, "Freescale i.MX blitter compositor base class");

	object_class = G_OBJECT_CLASS(klass);
	parent_class = GST_IMX_VIDEO_COMPOSITOR_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_dispose);

	element_class->change_state = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_change_state);
	element_class->release_pad = GST_DEBUG_FUNCPTR(gst_imx_blitter_compositor_release_pad);

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
	blitter_compositor->dma_bufferpools = g_hash_table_new_full (g_direct_hash, g_direct_equal, (GDestroyNotify) gst_object_unref, (GDestroyNotify) gst_object_unref);
}


static void gst_imx_blitter_compositor_dispose(GObject *object)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(object);

	if (blitter_compositor->blitter != NULL)
	{
		gst_object_unref(blitter_compositor->blitter);
		blitter_compositor->blitter = NULL;
	}

	if (blitter_compositor->dma_bufferpools != NULL)
	{
		g_hash_table_unref(blitter_compositor->dma_bufferpools);
		blitter_compositor->dma_bufferpools = NULL;
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

static void gst_imx_blitter_compositor_release_pad(GstElement *element, GstPad *pad)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(element);

	if (blitter_compositor->dma_bufferpools)
		g_hash_table_remove(blitter_compositor->dma_bufferpools, pad);

	GST_ELEMENT_CLASS(gst_imx_blitter_compositor_parent_class)->release_pad(element, pad);
}

GstAllocator* gst_imx_blitter_compositor_get_phys_mem_allocator(GstImxVideoCompositor *compositor)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_get_phys_mem_allocator(blitter_compositor->blitter);
}


gboolean gst_imx_blitter_compositor_set_output_frame(GstImxVideoCompositor *compositor, GstBuffer *output_frame)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_set_output_frame(blitter_compositor->blitter, output_frame);
}


gboolean gst_imx_blitter_compositor_set_output_video_info(GstImxVideoCompositor *compositor, GstVideoInfo const *info)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_set_output_video_info(blitter_compositor->blitter, info);
}


gboolean gst_imx_blitter_compositor_fill_region(GstImxVideoCompositor *compositor, GstImxRegion const *region, guint32 color)
{
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	return gst_imx_blitter_fill_region(blitter_compositor->blitter, region, color);
}


gboolean gst_imx_blitter_compositor_draw_frame(GstImxVideoCompositor *compositor, GstImxVideoCompositorPad *pad, GstVideoInfo const *input_info, GstImxRegion const *input_region, GstImxCanvas const *output_canvas, GstBuffer **input_frame, guint8 alpha)
{
	gboolean ret = TRUE;
	GstImxBlitterCompositor *blitter_compositor = GST_IMX_BLITTER_COMPOSITOR(compositor);
	g_assert(blitter_compositor->blitter != NULL);

	GstImxPhysMemMeta *phys_mem_meta;
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(*input_frame);

	ret = ret && gst_imx_blitter_set_input_video_info(blitter_compositor->blitter, input_info);

	if ((phys_mem_meta == NULL) || (phys_mem_meta->phys_addr == 0))
	{
		GstBufferPool *dma_bufferpool;
		GstCaps *current_caps = gst_video_info_to_caps(input_info);

		GST_TRACE_OBJECT(blitter_compositor, "need an internal bufferpool");

		dma_bufferpool = g_hash_table_lookup(blitter_compositor->dma_bufferpools, pad);
		if (dma_bufferpool)
		{
			GstStructure *buffer_pool_config = gst_buffer_pool_get_config(dma_bufferpool);
			GstCaps *previous_caps = NULL;

			GST_TRACE_OBJECT(blitter_compositor, "found an internal bufferpool");

			gst_buffer_pool_config_get_params(buffer_pool_config, &previous_caps, NULL, NULL, NULL);

			if (!previous_caps || !gst_caps_is_equal(current_caps, previous_caps))
			{
				g_hash_table_remove(blitter_compositor->dma_bufferpools, pad);
				dma_bufferpool = NULL;
			}

			gst_structure_free(buffer_pool_config);
		}

		if (dma_bufferpool == NULL)
		{
			GST_TRACE_OBJECT(blitter_compositor, "need to create internal bufferpool");
			dma_bufferpool = gst_imx_blitter_create_bufferpool(
				blitter_compositor->blitter,
				current_caps,
				input_info->size,
				0, 0,
				NULL,
				NULL
			);

			if (dma_bufferpool == NULL)
			{
				GST_ERROR_OBJECT(blitter_compositor, "failed to create internal bufferpool");
				return FALSE;
			}

			g_hash_table_insert(blitter_compositor->dma_bufferpools, gst_object_ref(pad), dma_bufferpool);
		}

		gst_caps_unref(current_caps);

		ret = ret && gst_imx_blitter_set_input_dma_bufferpool(blitter_compositor->blitter, dma_bufferpool);
	} else {
		ret = ret && gst_imx_blitter_set_input_dma_bufferpool(blitter_compositor->blitter, NULL);
	}

	ret = ret && gst_imx_blitter_set_input_region(blitter_compositor->blitter, input_region);
	ret = ret && gst_imx_blitter_set_input_frame_and_cache(blitter_compositor->blitter, input_frame);
	ret = ret && gst_imx_blitter_set_output_canvas(blitter_compositor->blitter, output_canvas);

	ret = ret && gst_imx_blitter_blit(blitter_compositor->blitter, alpha);

	return ret;
}
