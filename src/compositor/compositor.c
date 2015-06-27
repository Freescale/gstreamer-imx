#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "compositor.h"
#include "../common/phys_mem_meta.h"
#include "../common/phys_mem_buffer_pool.h"


GST_DEBUG_CATEGORY_STATIC(imx_compositor_debug);
#define GST_CAT_DEFAULT imx_compositor_debug




/********** GstImxCompositorPad **********/


enum
{
	PROP_PAD_0,
	PROP_PAD_X1,
	PROP_PAD_X2,
	PROP_PAD_Y1,
	PROP_PAD_Y2,
	PROP_PAD_LEFT_MARGIN,
	PROP_PAD_TOP_MARGIN,
	PROP_PAD_RIGHT_MARGIN,
	PROP_PAD_BOTTOM_MARGIN,
	PROP_PAD_ROTATION,
	PROP_PAD_KEEP_ASPECT_RATIO,
	PROP_PAD_ALPHA,
	PROP_PAD_FILL_COLOR
};

#define DEFAULT_PAD_X1 0
#define DEFAULT_PAD_X2 0
#define DEFAULT_PAD_Y1 0
#define DEFAULT_PAD_Y2 0
#define DEFAULT_PAD_LEFT_MARGIN 0
#define DEFAULT_PAD_TOP_MARGIN 0
#define DEFAULT_PAD_RIGHT_MARGIN 0
#define DEFAULT_PAD_BOTTOM_MARGIN 0
#define DEFAULT_PAD_ROTATION GST_IMX_CANVAS_INNER_ROTATION_NONE
#define DEFAULT_PAD_KEEP_ASPECT_RATIO TRUE
#define DEFAULT_PAD_ALPHA 255
#define DEFAULT_PAD_FILL_COLOR (0xFF000000)


G_DEFINE_TYPE(GstImxCompositorPad, gst_imx_compositor_pad, GST_TYPE_VIDEO_AGGREGATOR_PAD)


static void gst_imx_compositor_pad_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_compositor_pad_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);


static void gst_imx_compositor_pad_class_init(GstImxCompositorPadClass *klass)
{
	GObjectClass *object_class;
	GstImxBPVideoAggregatorPadClass *videoaggregator_pad_class;

	object_class = G_OBJECT_CLASS(klass);
	videoaggregator_pad_class = GST_IMXBP_VIDEO_AGGREGATOR_PAD_CLASS(klass);

	object_class->set_property  = GST_DEBUG_FUNCPTR(gst_imx_compositor_pad_set_property);
	object_class->get_property  = GST_DEBUG_FUNCPTR(gst_imx_compositor_pad_get_property);

	videoaggregator_pad_class->set_info      = NULL;
	videoaggregator_pad_class->prepare_frame = NULL;
	videoaggregator_pad_class->clean_frame   = NULL;

	g_object_class_install_property(
		object_class,
		PROP_PAD_X1,
		g_param_spec_int(
			"x1",
			"X1",
			"Left X coordinate",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_X1,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_X2,
		g_param_spec_int(
			"x2",
			"X2",
			"Right X coordinate",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_X2,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_Y1,
		g_param_spec_int(
			"y1",
			"Y1",
			"Top Y coordinate",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_Y1,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_Y2,
		g_param_spec_int(
			"y2",
			"Y2",
			"Bottom Y coordinate",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_Y2,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_LEFT_MARGIN,
		g_param_spec_uint(
			"left-margin",
			"Left margin",
			"Left margin",
			0, G_MAXUINT,
			DEFAULT_PAD_LEFT_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_TOP_MARGIN,
		g_param_spec_uint(
			"top-margin",
			"Top margin",
			"Top margin",
			0, G_MAXUINT,
			DEFAULT_PAD_TOP_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_RIGHT_MARGIN,
		g_param_spec_uint(
			"right-margin",
			"Right margin",
			"Right margin",
			0, G_MAXUINT,
			DEFAULT_PAD_RIGHT_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_BOTTOM_MARGIN,
		g_param_spec_uint(
			"bottom-margin",
			"Bottom margin",
			"Bottom margin",
			0, G_MAXUINT,
			DEFAULT_PAD_BOTTOM_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_ROTATION,
		g_param_spec_enum(
			"rotation",
			"Rotation",
			"Rotation that shall be applied to output frames",
			gst_imx_canvas_inner_rotation_get_type(),
			DEFAULT_PAD_ROTATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_KEEP_ASPECT_RATIO,
		g_param_spec_boolean(
			"keep-aspect-ratio",
			"Keep aspect ratio",
			"Keep aspect ratio",
			DEFAULT_PAD_KEEP_ASPECT_RATIO,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_ALPHA,
		g_param_spec_uint(
			"alpha",
			"Alpha",
			"Alpha blending factor (range:  0 = fully transparent  255 = fully opaque)",
			0, 255,
			DEFAULT_PAD_ALPHA,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_FILL_COLOR,
		g_param_spec_uint(
			"fill-color",
			"Fill color",
			"Fill color (format: 0xRRGGBBAA)",
			0, 0xFFFFFFFF,
			DEFAULT_PAD_FILL_COLOR,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_compositor_pad_init(GstImxCompositorPad *compositor_pad)
{
	memset(&(compositor_pad->canvas), 0, sizeof(GstImxCanvas));
	compositor_pad->canvas.inner_rotation = DEFAULT_PAD_ROTATION;
	compositor_pad->canvas.keep_aspect_ratio = DEFAULT_PAD_KEEP_ASPECT_RATIO;
	compositor_pad->canvas.fill_color = DEFAULT_PAD_FILL_COLOR;
	compositor_pad->canvas_needs_update = TRUE;
	compositor_pad->alpha = DEFAULT_PAD_ALPHA;
}


static void gst_imx_compositor_pad_update_canvas(GstImxCompositorPad *compositor_pad)
{
	GstImxCompositor *compositor;
	GstVideoInfo *info = &(GST_IMXBP_VIDEO_AGGREGATOR_PAD(compositor_pad)->info);

	if (!(compositor_pad->canvas_needs_update))
		return;

	compositor = GST_IMX_COMPOSITOR(gst_pad_get_parent_element(GST_PAD(compositor_pad)));

	gst_imx_canvas_calculate_inner_region(&(compositor_pad->canvas), info);
	gst_imx_canvas_clip(
		&(compositor_pad->canvas),
		&(compositor->overall_region),
		info,
		&(compositor_pad->source_subset)
	);

	compositor_pad->canvas_needs_update = FALSE;

	gst_object_unref(GST_OBJECT(compositor));
}


static void gst_imx_compositor_pad_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxCompositor *compositor;
	GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD(object);

	compositor = GST_IMX_COMPOSITOR(gst_pad_get_parent_element(GST_PAD(compositor_pad)));

	switch (prop_id)
	{
		case PROP_PAD_X1:
			compositor_pad->canvas.outer_region.x1 = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_X2:
			compositor_pad->canvas.outer_region.x2 = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_Y1:
			compositor_pad->canvas.outer_region.y1 = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_Y2:
			compositor_pad->canvas.outer_region.y2 = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_LEFT_MARGIN:
			compositor_pad->canvas.margin_left = g_value_get_uint(value);
			compositor_pad->canvas_needs_update = TRUE;
			break;

		case PROP_PAD_TOP_MARGIN:
			compositor_pad->canvas.margin_top = g_value_get_uint(value);
			compositor_pad->canvas_needs_update = TRUE;
			break;

		case PROP_PAD_RIGHT_MARGIN:
			compositor_pad->canvas.margin_right = g_value_get_uint(value);
			compositor_pad->canvas_needs_update = TRUE;
			break;

		case PROP_PAD_BOTTOM_MARGIN:
			compositor_pad->canvas.margin_bottom = g_value_get_uint(value);
			compositor_pad->canvas_needs_update = TRUE;
			break;

		case PROP_PAD_ROTATION:
			compositor_pad->canvas.inner_rotation = g_value_get_enum(value);
			compositor_pad->canvas_needs_update = TRUE;
			break;

		case PROP_PAD_KEEP_ASPECT_RATIO:
			compositor_pad->canvas.keep_aspect_ratio = g_value_get_boolean(value);
			compositor_pad->canvas_needs_update = TRUE;
			break;

		case PROP_PAD_ALPHA:
			compositor_pad->alpha = g_value_get_uint(value);
			break;

		case PROP_PAD_FILL_COLOR:
			compositor_pad->canvas.fill_color = g_value_get_uint(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}

	gst_object_unref(GST_OBJECT(compositor));
}


static void gst_imx_compositor_pad_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD(object);

	switch (prop_id)
	{
		case PROP_PAD_X1:
			g_value_set_int(value, compositor_pad->canvas.outer_region.x1);
			break;

		case PROP_PAD_X2:
			g_value_set_int(value, compositor_pad->canvas.outer_region.x2);
			break;

		case PROP_PAD_Y1:
			g_value_set_int(value, compositor_pad->canvas.outer_region.y1);
			break;

		case PROP_PAD_Y2:
			g_value_set_int(value, compositor_pad->canvas.outer_region.y2);
			break;

		case PROP_PAD_LEFT_MARGIN:
			g_value_set_uint(value, compositor_pad->canvas.margin_left);
			break;

		case PROP_PAD_TOP_MARGIN:
			g_value_set_uint(value, compositor_pad->canvas.margin_top);
			break;

		case PROP_PAD_RIGHT_MARGIN:
			g_value_set_uint(value, compositor_pad->canvas.margin_right);
			break;

		case PROP_PAD_BOTTOM_MARGIN:
			g_value_set_uint(value, compositor_pad->canvas.margin_bottom);
			break;

		case PROP_PAD_ROTATION:
			g_value_set_enum(value, compositor_pad->canvas.inner_rotation);
			break;

		case PROP_PAD_KEEP_ASPECT_RATIO:
			g_value_set_boolean(value, compositor_pad->canvas.keep_aspect_ratio);
			break;

		case PROP_PAD_ALPHA:
			g_value_set_uint(value, compositor_pad->alpha);
			break;

		case PROP_PAD_FILL_COLOR:
			g_value_set_uint(value, compositor_pad->canvas.fill_color);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}




/********** GstImxCompositor **********/

enum
{
	PROP_0,
	PROP_OVERALL_WIDTH,
	PROP_OVERALL_HEIGHT,
	PROP_BACKGROUND_COLOR
};

#define DEFAULT_OVERALL_WIDTH 0
#define DEFAULT_OVERALL_HEIGHT 0
#define DEFAULT_BACKGROUND_COLOR 0x00000000




G_DEFINE_ABSTRACT_TYPE(GstImxCompositor, gst_imx_compositor, GST_TYPE_VIDEO_AGGREGATOR)


static void gst_imx_compositor_dispose(GObject *object);
static void gst_imx_compositor_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_compositor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_compositor_sink_event(GstImxBPAggregator *aggregator, GstImxBPAggregatorPad *pad, GstEvent *event);

static GstFlowReturn gst_imx_compositor_aggregate_frames(GstImxBPVideoAggregator *videoaggregator, GstBuffer *outbuffer);
static GstFlowReturn gst_imx_compositor_get_output_buffer(GstImxBPVideoAggregator *videoaggregator, GstBuffer **outbuffer);
static gboolean gst_imx_compositor_negotiated_caps(GstImxBPVideoAggregator *videoaggregator, GstCaps *caps);
static void gst_imx_compositor_find_best_format(GstImxBPVideoAggregator *videoaggregator, GstCaps *downstream_caps, GstVideoInfo *best_info, gboolean *at_least_one_alpha);

static GstBufferPool* gst_imx_compositor_create_bufferpool(GstImxCompositor *compositor, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params);
static void gst_imx_compositor_update_overall_region(GstImxCompositor *compositor);


static void gst_imx_compositor_class_init(GstImxCompositorClass *klass)
{
	GObjectClass *object_class;
	GstImxBPAggregatorClass *aggregator_class;
	GstImxBPVideoAggregatorClass *video_aggregator_class;

	GST_DEBUG_CATEGORY_INIT(imx_compositor_debug, "imxvideocompositor", 0, "i.MX Video compositor");

	object_class = G_OBJECT_CLASS(klass);
	aggregator_class = GST_IMXBP_AGGREGATOR_CLASS(klass);
	video_aggregator_class = GST_IMXBP_VIDEO_AGGREGATOR_CLASS(klass);

	object_class->dispose       = GST_DEBUG_FUNCPTR(gst_imx_compositor_dispose);
	object_class->set_property  = GST_DEBUG_FUNCPTR(gst_imx_compositor_set_property);
	object_class->get_property  = GST_DEBUG_FUNCPTR(gst_imx_compositor_get_property);

	aggregator_class->sink_event    = GST_DEBUG_FUNCPTR(gst_imx_compositor_sink_event);
	aggregator_class->sinkpads_type = gst_imx_compositor_pad_get_type();

	video_aggregator_class->aggregate_frames  = GST_DEBUG_FUNCPTR(gst_imx_compositor_aggregate_frames);
	video_aggregator_class->get_output_buffer = GST_DEBUG_FUNCPTR(gst_imx_compositor_get_output_buffer);
	video_aggregator_class->negotiated_caps   = GST_DEBUG_FUNCPTR(gst_imx_compositor_negotiated_caps);
	video_aggregator_class->find_best_format  = GST_DEBUG_FUNCPTR(gst_imx_compositor_find_best_format);
	video_aggregator_class->preserve_update_caps_result = FALSE;

	klass->get_phys_mem_allocator = NULL;
	klass->set_output_video_info = NULL;
	klass->draw_frame = NULL;

	g_object_class_install_property(
		object_class,
		PROP_OVERALL_WIDTH,
		g_param_spec_uint(
			"overall-width",
			"Overall width",
			"Width of the overall frame (0 = adapts to width of input frames)",
			0,
			G_MAXUINT,
			DEFAULT_OVERALL_WIDTH,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_OVERALL_HEIGHT,
		g_param_spec_uint(
			"overall-height",
			"Overall height",
			"Height of the overall frame (0 = adapts to height of input frames)",
			0,
			G_MAXUINT,
			DEFAULT_OVERALL_HEIGHT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_BACKGROUND_COLOR,
		g_param_spec_uint(
			"background-color",
			"Background color",
			"Background color (format: 0xRRGGBB)",
			0,
			0xFFFFFF,
			DEFAULT_BACKGROUND_COLOR,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_compositor_init(GstImxCompositor *aggregator)
{
	aggregator->overall_width = DEFAULT_OVERALL_WIDTH;
	aggregator->overall_height = DEFAULT_OVERALL_HEIGHT;
	aggregator->dma_bufferpool = NULL;
	aggregator->overall_region_valid = FALSE;
	aggregator->background_color = DEFAULT_BACKGROUND_COLOR;
}


static void gst_imx_compositor_dispose(GObject *object)
{
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(object);

	if (compositor->dma_bufferpool != NULL)
	{
		gst_object_unref(GST_OBJECT(compositor->dma_bufferpool));
		compositor->dma_bufferpool = NULL;
	}

	G_OBJECT_CLASS(gst_imx_compositor_parent_class)->dispose(object);
}


static void gst_imx_compositor_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(object);

	switch (prop_id)
	{
		case PROP_OVERALL_WIDTH:
			compositor->overall_width = g_value_get_uint(value);
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_OVERALL_HEIGHT:
			compositor->overall_height = g_value_get_uint(value);
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_BACKGROUND_COLOR:
			compositor->background_color = g_value_get_uint(value);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_compositor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(object);

	switch (prop_id)
	{
		case PROP_OVERALL_WIDTH:
			g_value_set_uint(value, compositor->overall_width);
			break;

		case PROP_OVERALL_HEIGHT:
			g_value_set_uint(value, compositor->overall_height);
			break;

		case PROP_BACKGROUND_COLOR:
			g_value_set_uint(value, compositor->background_color);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_compositor_sink_event(GstImxBPAggregator *aggregator, GstImxBPAggregatorPad *pad, GstEvent *event)
{
	gboolean ret = GST_IMXBP_AGGREGATOR_CLASS(gst_imx_compositor_parent_class)->sink_event(aggregator, pad, event);

	if (ret && (GST_EVENT_TYPE(event) == GST_EVENT_CAPS))
	{
		GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD(pad);
		compositor_pad->canvas_needs_update = TRUE;
	}

	return ret;
}


static GstFlowReturn gst_imx_compositor_aggregate_frames(GstImxBPVideoAggregator *videoaggregator, GstBuffer *outbuffer)
{
	GList *walk;
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);
	GstImxCompositorClass *klass = GST_IMX_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(videoaggregator));

	g_assert(klass->set_output_frame != NULL);
	g_assert(klass->fill_region != NULL);
	g_assert(klass->draw_frame != NULL);

	klass->set_output_frame(compositor, outbuffer);
	gst_imx_compositor_update_overall_region(compositor);

	klass->fill_region(compositor, &(compositor->overall_region), compositor->background_color);

	walk = GST_ELEMENT(videoaggregator)->sinkpads;
	while (walk != NULL)
	{
		GstImxBPVideoAggregatorPad *videoaggregator_pad = walk->data;
		GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(videoaggregator_pad);

		gst_imx_compositor_pad_update_canvas(compositor_pad);

		klass->draw_frame(
			compositor,
			&(videoaggregator_pad->info),
			&(compositor_pad->source_subset),
			&(compositor_pad->canvas),
			videoaggregator_pad->buffer,
			compositor_pad->alpha
		);

		walk = g_list_next(walk);
	}

	klass->set_output_frame(compositor, NULL);

	return GST_FLOW_OK;
}


static GstFlowReturn gst_imx_compositor_get_output_buffer(GstImxBPVideoAggregator *videoaggregator, GstBuffer **outbuffer)
{
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);
	GstBufferPool *pool = compositor->dma_bufferpool;

	if (!gst_buffer_pool_is_active(pool))
		gst_buffer_pool_set_active(pool, TRUE);

	return gst_buffer_pool_acquire_buffer(pool, outbuffer, NULL);
}


static gboolean gst_imx_compositor_negotiated_caps(GstImxBPVideoAggregator *videoaggregator, GstCaps *caps)
{
	GstVideoInfo info;
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);

	if (!gst_video_info_from_caps(&info, caps))
	{
		GST_ERROR_OBJECT(compositor, "could not get video info from negotiated caps");
		return FALSE;
	}

	if (compositor->dma_bufferpool != NULL)
		gst_object_unref(GST_OBJECT(compositor->dma_bufferpool));

	compositor->dma_bufferpool = gst_imx_compositor_create_bufferpool(compositor, caps, 0, 0, 0, NULL, NULL);

	if (compositor->dma_bufferpool != NULL)
	{
		GstImxCompositorClass *klass = GST_IMX_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(compositor));

		if (klass->set_output_video_info != NULL)
			klass->set_output_video_info(compositor, &info);

		return TRUE;
	}
	else
		return FALSE;
}


static void gst_imx_compositor_find_best_format(GstImxBPVideoAggregator *videoaggregator, GstCaps *downstream_caps, GstVideoInfo *best_info, gboolean *at_least_one_alpha)
{
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);

	downstream_caps = gst_caps_fixate(downstream_caps);
	gst_video_info_from_caps(best_info, downstream_caps);

	gst_imx_compositor_update_overall_region(compositor);

	GST_VIDEO_INFO_WIDTH(best_info) = compositor->overall_region.x2 - compositor->overall_region.x1;
	GST_VIDEO_INFO_HEIGHT(best_info) = compositor->overall_region.y2 - compositor->overall_region.y1;

	*at_least_one_alpha = FALSE;
}


static GstBufferPool* gst_imx_compositor_create_bufferpool(GstImxCompositor *compositor, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params)
{
	GstBufferPool *pool;
	GstStructure *config;
	GstImxCompositorClass *klass;

	g_assert(compositor != NULL);
	klass = GST_IMX_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(compositor));

	g_assert(klass->get_phys_mem_allocator != NULL);

	if (size == 0)
	{
		GstVideoInfo info;
		if (!gst_video_info_from_caps(&info, caps))
		{
			GST_ERROR_OBJECT(compositor, "could not parse caps for dma bufferpool");
			return NULL;
		}
	}

	pool = gst_imx_phys_mem_buffer_pool_new(FALSE);

	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, caps, size, min_buffers, max_buffers);

	/* If the allocator value is NULL, get an allocator
	 * it is unref'd by the buffer pool when it is unref'd */
	if (allocator == NULL)
		allocator = klass->get_phys_mem_allocator(compositor);
	if (allocator == NULL)
	{
		GST_ERROR_OBJECT(compositor, "could not create physical memory bufferpool allocator");
		return NULL;
	}

	gst_buffer_pool_config_set_allocator(config, allocator, alloc_params);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(pool, config);

	return pool;
}


static void gst_imx_compositor_update_overall_region(GstImxCompositor *compositor)
{
	GList *walk;
	gboolean first = TRUE;

	if ((compositor->overall_width != 0) && (compositor->overall_height != 0))
	{
		compositor->overall_region.x1 = 0;
		compositor->overall_region.y1 = 0;
		compositor->overall_region.x2 = compositor->overall_width;
		compositor->overall_region.y2 = compositor->overall_height;
		return;
	}

	if (compositor->overall_region_valid)
		return;

	walk = GST_ELEMENT(compositor)->sinkpads;
	while (walk != NULL)
	{
		GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(walk->data);
		GstImxRegion *outer_region = &(compositor_pad->canvas.outer_region);

		/* NOTE: *not* updating the pad canvas here, since the overall region is being
		 * constructed in these iterations, and the canvas updates require a valid
		 * overall region; furthermore, only the outer region is needed, which anyway
		 * needs to exist prior to any canvas updates */

		if (first)
			compositor->overall_region = *outer_region;
		else
			gst_imx_region_merge(&(compositor->overall_region), &(compositor->overall_region), outer_region);

		first = FALSE;
		walk = g_list_next(walk);
	}

	compositor->overall_region_valid = TRUE;
}
