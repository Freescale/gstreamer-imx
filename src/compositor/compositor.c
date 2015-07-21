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
	PROP_PAD_XPOS,
	PROP_PAD_YPOS,
	PROP_PAD_WIDTH,
	PROP_PAD_HEIGHT,
	PROP_PAD_LEFT_MARGIN,
	PROP_PAD_TOP_MARGIN,
	PROP_PAD_RIGHT_MARGIN,
	PROP_PAD_BOTTOM_MARGIN,
	PROP_PAD_ROTATION,
	PROP_PAD_KEEP_ASPECT_RATIO,
	PROP_PAD_ALPHA,
	PROP_PAD_FILL_COLOR
};

#define DEFAULT_PAD_XPOS 0
#define DEFAULT_PAD_YPOS 0
#define DEFAULT_PAD_WIDTH 0
#define DEFAULT_PAD_HEIGHT 0
#define DEFAULT_PAD_LEFT_MARGIN 0
#define DEFAULT_PAD_TOP_MARGIN 0
#define DEFAULT_PAD_RIGHT_MARGIN 0
#define DEFAULT_PAD_BOTTOM_MARGIN 0
#define DEFAULT_PAD_ROTATION GST_IMX_CANVAS_INNER_ROTATION_NONE
#define DEFAULT_PAD_KEEP_ASPECT_RATIO TRUE
#define DEFAULT_PAD_ALPHA 1.0
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
		PROP_PAD_XPOS,
		g_param_spec_int(
			"xpos",
			"X position",
			"Left X coordinate in pixels",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_XPOS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_YPOS,
		g_param_spec_int(
			"ypos",
			"Y position",
			"Top Y coordinate in pixels",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_YPOS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_WIDTH,
		g_param_spec_int(
			"width",
			"Width",
			"Width in pixels",
			0, G_MAXINT,
			DEFAULT_PAD_WIDTH,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_HEIGHT,
		g_param_spec_int(
			"height",
			"Height",
			"Height in pixels",
			0, G_MAXINT,
			DEFAULT_PAD_HEIGHT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
		g_param_spec_double(
			"alpha",
			"Alpha",
			"Alpha blending factor (range:  0.0 = fully transparent  1.0 = fully opaque)",
			0.0, 1.0,
			DEFAULT_PAD_ALPHA,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
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
	compositor_pad->xpos = DEFAULT_PAD_XPOS;
	compositor_pad->ypos = DEFAULT_PAD_YPOS;
	compositor_pad->width = DEFAULT_PAD_WIDTH;
	compositor_pad->height = DEFAULT_PAD_HEIGHT;
}


static void gst_imx_compositor_pad_check_outer_region(GstImxCompositorPad *compositor_pad)
{
	GstVideoInfo *info = &(GST_IMXBP_VIDEO_AGGREGATOR_PAD(compositor_pad)->info);

	if (compositor_pad->width == 0)
		compositor_pad->canvas.outer_region.x2 = compositor_pad->canvas.outer_region.x1 + GST_VIDEO_INFO_WIDTH(info);
	else
		compositor_pad->canvas.outer_region.x2 = compositor_pad->canvas.outer_region.x1 + compositor_pad->width;

	if (compositor_pad->height == 0)
		compositor_pad->canvas.outer_region.y2 = compositor_pad->canvas.outer_region.y1 + GST_VIDEO_INFO_HEIGHT(info);
	else
		compositor_pad->canvas.outer_region.y2 = compositor_pad->canvas.outer_region.y1 + compositor_pad->height;

	GST_DEBUG_OBJECT(compositor_pad, "computed outer region: %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&(compositor_pad->canvas.outer_region)));
}


static void gst_imx_compositor_pad_update_canvas(GstImxCompositorPad *compositor_pad)
{
	GstImxCompositor *compositor;
	GstVideoInfo *info = &(GST_IMXBP_VIDEO_AGGREGATOR_PAD(compositor_pad)->info);

	if (!(compositor_pad->canvas_needs_update))
		return;

	compositor = GST_IMX_COMPOSITOR(gst_pad_get_parent_element(GST_PAD(compositor_pad)));

	gst_imx_compositor_pad_check_outer_region(compositor_pad);

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
		case PROP_PAD_XPOS:
			compositor_pad->xpos = g_value_get_int(value);
			compositor_pad->canvas.outer_region.x1 = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_YPOS:
			compositor_pad->ypos = g_value_get_int(value);
			compositor_pad->canvas.outer_region.y1 = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_WIDTH:
			compositor_pad->width = g_value_get_int(value);
			compositor_pad->canvas.outer_region.x2 = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_HEIGHT:
			compositor_pad->height = g_value_get_int(value);
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
			compositor_pad->alpha = g_value_get_double(value);
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
		case PROP_PAD_XPOS:
			g_value_set_int(value, compositor_pad->xpos);
			break;

		case PROP_PAD_YPOS:
			g_value_set_int(value, compositor_pad->ypos);
			break;

		case PROP_PAD_WIDTH:
			g_value_set_int(value, compositor_pad->width);
			break;

		case PROP_PAD_HEIGHT:
			g_value_set_int(value, compositor_pad->height);
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
			g_value_set_double(value, compositor_pad->alpha);
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

static gboolean gst_imx_compositor_sink_query(GstImxBPAggregator *aggregator, GstImxBPAggregatorPad *pad, GstQuery *query);
static gboolean gst_imx_compositor_sink_event(GstImxBPAggregator *aggregator, GstImxBPAggregatorPad *pad, GstEvent *event);

static GstCaps* gst_imx_compositor_update_caps(GstImxBPVideoAggregator *videoaggregator, GstCaps *caps);
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

	aggregator_class->sink_query    = GST_DEBUG_FUNCPTR(gst_imx_compositor_sink_query);
	aggregator_class->sink_event    = GST_DEBUG_FUNCPTR(gst_imx_compositor_sink_event);
	aggregator_class->sinkpads_type = gst_imx_compositor_pad_get_type();

	video_aggregator_class->update_caps       = GST_DEBUG_FUNCPTR(gst_imx_compositor_update_caps);
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


static void gst_imx_compositor_init(GstImxCompositor *compositor)
{
	compositor->overall_width = DEFAULT_OVERALL_WIDTH;
	compositor->overall_height = DEFAULT_OVERALL_HEIGHT;
	compositor->dma_bufferpool = NULL;
	compositor->overall_region_valid = FALSE;
	compositor->region_fill_necessary = TRUE;
	compositor->background_color = DEFAULT_BACKGROUND_COLOR;
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


static gboolean gst_imx_compositor_sink_query(GstImxBPAggregator *aggregator, GstImxBPAggregatorPad *pad, GstQuery *query)
{
	switch (GST_QUERY_TYPE(query))
	{
		case GST_QUERY_CAPS:
		{
			GstCaps *filter, *caps;

			gst_query_parse_caps(query, &filter);
			caps = gst_pad_get_pad_template_caps(GST_PAD(pad));

			if (filter != NULL)
			{
				GstCaps *unfiltered_caps = gst_caps_make_writable(caps);
				caps = gst_caps_intersect(unfiltered_caps, filter);
				gst_caps_unref(unfiltered_caps);
			}

			GST_DEBUG_OBJECT(aggregator, "responding to CAPS query with caps %" GST_PTR_FORMAT, (gpointer)caps);

			gst_query_set_caps_result(query, caps);

			gst_caps_unref(caps);

			return TRUE;
		}

		case GST_QUERY_ACCEPT_CAPS:
		{
			GstCaps *accept_caps, *template_caps;
			gboolean ret;

			gst_query_parse_accept_caps(query, &accept_caps);
			template_caps = gst_pad_get_pad_template_caps(GST_PAD(pad));

			ret = gst_caps_is_subset(accept_caps, template_caps);
			GST_DEBUG_OBJECT(aggregator, "responding to ACCEPT_CAPS query with value %d  (acceptcaps: %" GST_PTR_FORMAT "  template caps %" GST_PTR_FORMAT ")", ret, (gpointer)accept_caps, (gpointer)template_caps);
			gst_query_set_accept_caps_result(query, ret);

			return TRUE;
		}

		default:
			return GST_IMXBP_AGGREGATOR_CLASS(gst_imx_compositor_parent_class)->sink_query(aggregator, pad, query);
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


static GstCaps* gst_imx_compositor_update_caps(GstImxBPVideoAggregator *videoaggregator, GstCaps *caps)
{
	GstCaps *out_caps;
	GstStructure *s;
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);

	gst_imx_compositor_update_overall_region(compositor);

	out_caps = gst_caps_copy(caps);
	s = gst_caps_get_structure(out_caps, 0);
	gst_structure_set(
		s,
		"width", G_TYPE_INT, (gint)(compositor->overall_region.x2 - compositor->overall_region.x1),
		"height", G_TYPE_INT, (gint)(compositor->overall_region.y2 - compositor->overall_region.y1),
		NULL
	);

	return out_caps;
}


static GstFlowReturn gst_imx_compositor_aggregate_frames(GstImxBPVideoAggregator *videoaggregator, GstBuffer *outbuffer)
{
	GstFlowReturn ret = GST_FLOW_OK;
	GList *walk;
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);
	GstImxCompositorClass *klass = GST_IMX_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(videoaggregator));

	g_assert(klass->set_output_frame != NULL);
	g_assert(klass->fill_region != NULL);
	g_assert(klass->draw_frame != NULL);

	if (!(klass->set_output_frame(compositor, outbuffer)))
	{
		GST_ERROR_OBJECT(compositor, "could not set the output frame");
		return GST_FLOW_ERROR;
	}

	gst_imx_compositor_update_overall_region(compositor);

	GST_LOG_OBJECT(compositor, "region_fill_necessary: %d", (gint)(compositor->region_fill_necessary));

	if (!(compositor->region_fill_necessary) || klass->fill_region(compositor, &(compositor->overall_region), compositor->background_color))
	{
		GST_OBJECT_LOCK(compositor);

		walk = GST_ELEMENT(videoaggregator)->sinkpads;
		while (walk != NULL)
		{
			GstImxBPVideoAggregatorPad *videoaggregator_pad = walk->data;
			GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(videoaggregator_pad);

			if (videoaggregator_pad->buffer == NULL)
				continue;

			gst_imx_compositor_pad_update_canvas(compositor_pad);

			if (!klass->draw_frame(
				compositor,
				&(videoaggregator_pad->info),
				&(compositor_pad->source_subset),
				&(compositor_pad->canvas),
				videoaggregator_pad->buffer,
				(guint8)(compositor_pad->alpha * 255.0)
			))
			{
				GST_ERROR_OBJECT(compositor, "error while drawing composition frame");
				ret = GST_FLOW_ERROR;
				break;
			}

			walk = g_list_next(walk);
		}

		GST_OBJECT_UNLOCK(compositor);
	}

	klass->set_output_frame(compositor, NULL);

	return ret;
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
			return klass->set_output_video_info(compositor, &info);
		else
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
		compositor->region_fill_necessary = TRUE;
		return;
	}

	if (compositor->overall_region_valid)
		return;

	/* Walk through all pads and merge their outer canvas regions together */
	walk = GST_ELEMENT(compositor)->sinkpads;
	while (walk != NULL)
	{
		GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(walk->data);
		GstImxRegion *outer_region = &(compositor_pad->canvas.outer_region);

		/* The outer region might not be defined. Check for this, and if necessary,
		 * set the outer region to be of the same size as the input frame. */
		gst_imx_compositor_pad_check_outer_region(compositor_pad);

		/* NOTE: *not* updating the pad canvas here, since the overall region is being
		 * constructed in these iterations, and the canvas updates require a valid
		 * overall region; furthermore, only the outer region is needed, which anyway
		 * needs to exist prior to any canvas updates */

		if (first)
		{
			compositor->overall_region = *outer_region;
			first = FALSE;
		}
		else
			gst_imx_region_merge(&(compositor->overall_region), &(compositor->overall_region), outer_region);

		walk = g_list_next(walk);
	}

	/* Make sure the overall region starts at (0,0), since any other topleft
	 * coordinate makes little sense */
	compositor->overall_region.x1 = 0;
	compositor->overall_region.y1 = 0;

	/* Now that the overall region is computed, walk through the individual
	 * outer regions, and check if any of them completely cover the overall region
	 * If so, the compositor does not have to clear the frame first, thus
	 * saving bandwidth */
	compositor->region_fill_necessary = TRUE;
	walk = GST_ELEMENT(compositor)->sinkpads;
	while (walk != NULL)
	{
		GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(walk->data);
		GstImxRegion *outer_region = &(compositor_pad->canvas.outer_region);

		/* Check if the outer region completely contains the overall region */
		if (gst_imx_region_contains(&(compositor->overall_region), outer_region) == GST_IMX_REGION_CONTAINS_FULL)
		{
			/* disable filling if this outer region is opaque
			 * (because it will completely cover the overall region) */
			compositor->region_fill_necessary = (compositor_pad->alpha < 1.0);
			break;
		}

		walk = g_list_next(walk);
	}

	compositor->overall_region_valid = TRUE;
}
