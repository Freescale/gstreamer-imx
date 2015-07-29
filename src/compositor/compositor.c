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
	PROP_PAD_INPUT_CROP,
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
#define DEFAULT_PAD_INPUT_CROP TRUE
#define DEFAULT_PAD_ALPHA 1.0
#define DEFAULT_PAD_FILL_COLOR (0xFF000000)


G_DEFINE_TYPE(GstImxCompositorPad, gst_imx_compositor_pad, GST_TYPE_VIDEO_AGGREGATOR_PAD)


static void gst_imx_compositor_pad_compute_outer_region(GstImxCompositorPad *compositor_pad);
static void gst_imx_compositor_pad_update_canvas(GstImxCompositorPad *compositor_pad, GstImxRegion const *source_region);

static gboolean gst_imx_compositor_pad_flush(GstImxBPAggregatorPad *aggpad, GstImxBPAggregator *aggregator);

static void gst_imx_compositor_pad_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_compositor_pad_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);


static void gst_imx_compositor_pad_class_init(GstImxCompositorPadClass *klass)
{
	GObjectClass *object_class;
	GstImxBPAggregatorPadClass *aggregator_pad_class;
	GstImxBPVideoAggregatorPadClass *videoaggregator_pad_class;

	object_class = G_OBJECT_CLASS(klass);
	aggregator_pad_class = GST_IMXBP_AGGREGATOR_PAD_CLASS(klass);
	videoaggregator_pad_class = GST_IMXBP_VIDEO_AGGREGATOR_PAD_CLASS(klass);

	object_class->set_property  = GST_DEBUG_FUNCPTR(gst_imx_compositor_pad_set_property);
	object_class->get_property  = GST_DEBUG_FUNCPTR(gst_imx_compositor_pad_get_property);

	aggregator_pad_class->flush = GST_DEBUG_FUNCPTR(gst_imx_compositor_pad_flush);

	/* Explicitely set these to NULL to force the base class
	 * to not try any software-based colorspace conversions
	 * Subclasses use i.MX blitters, which are capable of
	 * hardware-accelerated colorspace conversions */
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
		PROP_PAD_INPUT_CROP,
		g_param_spec_boolean(
			"input-crop",
			"Input crop",
			"Whether or not to crop input frames based on their video crop metadata",
			DEFAULT_PAD_INPUT_CROP,
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
			"Fill color (format: 0xAABBGGRR)",
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
	compositor_pad->input_crop = DEFAULT_PAD_INPUT_CROP;
	compositor_pad->last_frame_with_cropdata = FALSE;
	compositor_pad->pad_is_new = TRUE;
}


static void gst_imx_compositor_pad_compute_outer_region(GstImxCompositorPad *compositor_pad)
{
	GstVideoInfo *info = &(GST_IMXBP_VIDEO_AGGREGATOR_PAD(compositor_pad)->info);

	/* Set the outer region's top left corner */

	compositor_pad->canvas.outer_region.x1 = compositor_pad->xpos;
	compositor_pad->canvas.outer_region.y1 = compositor_pad->ypos;

	/* Check if width and/or height are 0. 0 means "use the video width/height". */

	if (compositor_pad->width == 0)
		compositor_pad->canvas.outer_region.x2 = compositor_pad->xpos + GST_VIDEO_INFO_WIDTH(info);
	else
		compositor_pad->canvas.outer_region.x2 = compositor_pad->xpos + compositor_pad->width;

	if (compositor_pad->height == 0)
		compositor_pad->canvas.outer_region.y2 = compositor_pad->ypos + GST_VIDEO_INFO_HEIGHT(info);
	else
		compositor_pad->canvas.outer_region.y2 = compositor_pad->ypos + compositor_pad->height;

	GST_DEBUG_OBJECT(compositor_pad, "computed outer region: %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&(compositor_pad->canvas.outer_region)));
}


static void gst_imx_compositor_pad_update_canvas(GstImxCompositorPad *compositor_pad, GstImxRegion const *source_region)
{
	GstImxCompositor *compositor;
	GstVideoInfo *info = &(GST_IMXBP_VIDEO_AGGREGATOR_PAD(compositor_pad)->info);

	/* Catch redundant calls */
	if (!(compositor_pad->canvas_needs_update))
		return;

	compositor = GST_IMX_COMPOSITOR(gst_pad_get_parent_element(GST_PAD(compositor_pad)));

	/* (Re)compute the outer region */
	gst_imx_compositor_pad_compute_outer_region(compositor_pad);

	/* (Re)computer the inner region */
	gst_imx_canvas_calculate_inner_region(&(compositor_pad->canvas), info);

	/* Next, clip the canvas against the overall region,
	 * which describes the output frame's size
	 * This way, it is ensured that only the parts that are "within"
	 * the output frame are blit */
	gst_imx_canvas_clip(
		&(compositor_pad->canvas),
		&(compositor->overall_region),
		info,
		source_region,
		&(compositor_pad->source_subset)
	);

	/* Canvas updated, mark it as such */
	compositor_pad->canvas_needs_update = FALSE;

	gst_object_unref(GST_OBJECT(compositor));
}


static gboolean gst_imx_compositor_pad_flush(GstImxBPAggregatorPad *aggpad, GstImxBPAggregator *aggregator)
{
	GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD(aggpad);
	GST_DEBUG_OBJECT(aggregator, "resetting internal compositor pad flags");
	compositor_pad->last_frame_with_cropdata = TRUE;
	compositor_pad->canvas_needs_update = TRUE;
	return TRUE;
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
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_YPOS:
			compositor_pad->ypos = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_WIDTH:
			compositor_pad->width = g_value_get_int(value);
			compositor_pad->canvas_needs_update = TRUE;
			compositor->overall_region_valid = FALSE;
			break;

		case PROP_PAD_HEIGHT:
			compositor_pad->height = g_value_get_int(value);
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

		case PROP_PAD_INPUT_CROP:
			compositor_pad->input_crop = g_value_get_boolean(value);
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

		case PROP_PAD_INPUT_CROP:
			g_value_set_boolean(value, compositor_pad->input_crop);
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
	PROP_BACKGROUND_COLOR
};

#define DEFAULT_BACKGROUND_COLOR 0x00000000




G_DEFINE_ABSTRACT_TYPE(GstImxCompositor, gst_imx_compositor, GST_TYPE_VIDEO_AGGREGATOR)


static void gst_imx_compositor_dispose(GObject *object);
static void gst_imx_compositor_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_compositor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_compositor_sink_query(GstImxBPAggregator *aggregator, GstImxBPAggregatorPad *pad, GstQuery *query);
static gboolean gst_imx_compositor_sink_event(GstImxBPAggregator *aggregator, GstImxBPAggregatorPad *pad, GstEvent *event);
//static gboolean gst_imx_compositor_stop(GstImxBPAggregator *aggregator);

static GstFlowReturn gst_imx_compositor_aggregate_frames(GstImxBPVideoAggregator *videoaggregator, GstBuffer *outbuffer);
static GstFlowReturn gst_imx_compositor_get_output_buffer(GstImxBPVideoAggregator *videoaggregator, GstBuffer **outbuffer);
static gboolean gst_imx_compositor_negotiated_caps(GstImxBPVideoAggregator *videoaggregator, GstCaps *caps);

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
//	aggregator_class->stop          = GST_DEBUG_FUNCPTR(gst_imx_compositor_stop);
	aggregator_class->sinkpads_type = gst_imx_compositor_pad_get_type();

	video_aggregator_class->aggregate_frames  = GST_DEBUG_FUNCPTR(gst_imx_compositor_aggregate_frames);
	video_aggregator_class->get_output_buffer = GST_DEBUG_FUNCPTR(gst_imx_compositor_get_output_buffer);
	video_aggregator_class->negotiated_caps   = GST_DEBUG_FUNCPTR(gst_imx_compositor_negotiated_caps);
	video_aggregator_class->preserve_update_caps_result = FALSE;

	klass->get_phys_mem_allocator = NULL;
	klass->set_output_video_info = NULL;
	klass->draw_frame = NULL;

	g_object_class_install_property(
		object_class,
		PROP_BACKGROUND_COLOR,
		g_param_spec_uint(
			"background-color",
			"Background color",
			"Background color (format: 0xBBGGRR)",
			0,
			0xFFFFFF,
			DEFAULT_BACKGROUND_COLOR,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_compositor_init(GstImxCompositor *compositor)
{
	compositor->overall_width = 0;
	compositor->overall_height = 0;
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
			/* Custom caps query response. Take the sinkpad template caps,
			 * optionally filter them, and return them as the result.
			 * This ensures that the caps that the derived class supports
			 * for input data are actually used (by default, the aggregator
			 * base classes try to keep input and output caps equal) */

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
			/* Custom accept_caps query response. Simply check if
			 * the supplied caps are a valid subset of the sinkpad's
			 * template caps. This is done for the same reasons
			 * as the caps query response above. */

			GstCaps *accept_caps = NULL, *template_caps = NULL;
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

	/* If new caps came in one of the sinkpads, this pad's canvas might
	 * need to be changed now (for example, if the new caps have different
	 * width or height). Request an update by raising the canvas_needs_update
	 * flag. This is done *after* the base class handled events, to make
	 * sure the flag is only raised if the base class didn't have problems
	 * processing the event (ret is FALSE in that case). */
	if (ret && (GST_EVENT_TYPE(event) == GST_EVENT_CAPS))
	{
		GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD(pad);
		compositor_pad->canvas_needs_update = TRUE;
	}

	return ret;
}


#if 0
static gboolean gst_imx_compositor_stop(GstImxBPAggregator *aggregator)
{
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);

	walk = GST_ELEMENT(videoaggregator)->sinkpads;
	while (walk != NULL)
	{
		GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(walk->data);
		compositor_pad->last_frame_with_cropdata = TRUE;
		compositor_pad->canvas_needs_update = TRUE;
		walk = g_list_next(walk);
	}
}
#endif


static GstFlowReturn gst_imx_compositor_aggregate_frames(GstImxBPVideoAggregator *videoaggregator, GstBuffer *outbuffer)
{
	GstFlowReturn ret = GST_FLOW_OK;
	GList *walk;
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);
	GstImxCompositorClass *klass = GST_IMX_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(videoaggregator));

	g_assert(klass->set_output_frame != NULL);
	g_assert(klass->fill_region != NULL);
	g_assert(klass->draw_frame != NULL);

	/* This function is the heart of the compositor. Here, input frames
	 * are drawn on the output frame, with their specific parameters. */

	/* Set the output buffer */
	if (!(klass->set_output_frame(compositor, outbuffer)))
	{
		GST_ERROR_OBJECT(compositor, "could not set the output frame");
		return GST_FLOW_ERROR;
	}

	/* TODO: are the update_overall_region calls here necessary?
	 * If the video aggregator calls update_caps when a pad is added/removed,
	 * there is no need for these calls */

	/* Update the overall region first if necessary to ensure that it is valid
	 * and that the region_fill_necessary flag is set to the proper value */
	gst_imx_compositor_update_overall_region(compositor);

	GST_LOG_OBJECT(compositor, "aggregating frames, region_fill_necessary: %d", (gint)(compositor->region_fill_necessary));

	/* Check if the overall region needs to be filled. This is the case if none
	 * of the input frames completely cover the overall region with 100% alpha
	 * (this is determined by gst_imx_compositor_update_overall_region() ) */
	if (!(compositor->region_fill_necessary) || klass->fill_region(compositor, &(compositor->overall_region), compositor->background_color))
	{
		/* Lock object to ensure nothing is changed during composition */
		GST_OBJECT_LOCK(compositor);

		/* First walk: check if there is a new pad. If so, recompute the
		 * overall region, since it might need to be expanded to encompass
		 * the new additional input frames */
		walk = GST_ELEMENT(videoaggregator)->sinkpads;
		while (walk != NULL)
		{
			GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(walk->data);

			if (compositor_pad->pad_is_new)
			{
				GST_DEBUG_OBJECT(compositor, "there is a new pad; invalidate overall region");

				compositor_pad->pad_is_new = FALSE;
				compositor->overall_region_valid = FALSE;

				/* While this call might seem redundant, there is one
				 * benefit in calling this function apparently twice
				 * (once above, and once here): the earlier call
				 * happens outside of the object lock. New pads are less
				 * common than overall region changes, so it is good
				 * if most update calls happen outside of the object
				 * lock (the overall_region_valid flag ensures redundant
				 * calls don't compute anything). */
				gst_imx_compositor_update_overall_region(compositor);

				break;
			}

			/* Move to next pad */
			walk = g_list_next(walk);
		}

		/* Second walk: draw the input frames on the output frame */
		walk = GST_ELEMENT(videoaggregator)->sinkpads;
		while (walk != NULL)
		{
			GstImxBPVideoAggregatorPad *videoaggregator_pad = walk->data;
			GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(videoaggregator_pad);

			/* If there actually is a buffer, draw it
			 * Sometimes, pads don't deliver data right from the start;
			 * in these cases, their buffers will be NULL
			 * Just skip to the next pad in that case */
			if (videoaggregator_pad->buffer != NULL)
			{
				GstVideoCropMeta *video_crop_meta;
				if (compositor_pad->input_crop && ((video_crop_meta = gst_buffer_get_video_crop_meta(videoaggregator_pad->buffer)) != NULL))
				{
					/* Crop metadata present. Reconfigure canvas. */

					GstVideoInfo *info = &(videoaggregator_pad->info);

					GstImxRegion source_region;
					source_region.x1 = video_crop_meta->x;
					source_region.y1 = video_crop_meta->y;
					source_region.x2 = video_crop_meta->x + video_crop_meta->width;
					source_region.y2 = video_crop_meta->y + video_crop_meta->height;

					/* Make sure the source region does not exceed valid bounds */
					source_region.x1 = MAX(0, source_region.x1);
					source_region.y1 = MAX(0, source_region.y1);
					source_region.x2 = MIN(GST_VIDEO_INFO_WIDTH(info), source_region.x2);
					source_region.y2 = MIN(GST_VIDEO_INFO_HEIGHT(info), source_region.y2);

					GST_LOG_OBJECT(compositor, "retrieved crop rectangle %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&source_region));


					/* Canvas needs to be updated if either one of these applies:
					 * - the current frame has crop metadata, the last one didn't
					 * - the new crop rectangle and the last are different */
					if (!(compositor_pad->last_frame_with_cropdata) || !gst_imx_region_equal(&source_region, &(compositor_pad->last_source_region)))
					{
						GST_LOG_OBJECT(compositor, "using new crop rectangle %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&source_region));
						compositor_pad->last_source_region = source_region;
						compositor_pad->canvas_needs_update = TRUE;
					}

					compositor_pad->last_frame_with_cropdata = TRUE;

					/* Update canvas and input region if necessary */
					if (compositor_pad->canvas_needs_update)
						gst_imx_compositor_pad_update_canvas(compositor_pad, &(compositor_pad->last_source_region));
				}
				else
				{
					/* Force an update if this frame has no crop metadata but the last one did */
					if (compositor_pad->last_frame_with_cropdata)
						compositor_pad->canvas_needs_update = TRUE;
					compositor_pad->last_frame_with_cropdata = FALSE;

					/* Update the pad's canvas if necessary,
					 * to ensure there is a valid canvas to draw to */
					gst_imx_compositor_pad_update_canvas(compositor_pad, NULL);
				}

				GST_LOG_OBJECT(
					compositor,
					"pad %p  frame %p  format: %s  width/height: %d/%d  regions: outer %" GST_IMX_REGION_FORMAT "  inner %" GST_IMX_REGION_FORMAT "  source subset %" GST_IMX_REGION_FORMAT,
					(gpointer)(videoaggregator_pad),
					(gpointer)(videoaggregator_pad->buffer),
					gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&(videoaggregator_pad->info))),
					GST_VIDEO_INFO_WIDTH(&(videoaggregator_pad->info)),
					GST_VIDEO_INFO_HEIGHT(&(videoaggregator_pad->info)),
					GST_IMX_REGION_ARGS(&(compositor_pad->canvas.outer_region)),
					GST_IMX_REGION_ARGS(&(compositor_pad->canvas.inner_region)),
					GST_IMX_REGION_ARGS(&(compositor_pad->source_subset))
				);

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
			}
			else
			{
				GST_LOG_OBJECT(compositor, "pad %p  buffer is NULL, no frame to aggregate - skipping to next pad", (gpointer)(videoaggregator_pad));
			}

			/* Move to next pad */
			walk = g_list_next(walk);
		}

		GST_OBJECT_UNLOCK(compositor);
	}

	/* Release the output buffer, since we don't need it anymore, and
	 * there is no reason to retain it */
	klass->set_output_frame(compositor, NULL);

	return ret;
}


static GstFlowReturn gst_imx_compositor_get_output_buffer(GstImxBPVideoAggregator *videoaggregator, GstBuffer **outbuffer)
{
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);
	GstBufferPool *pool = compositor->dma_bufferpool;

	/* Return a DMA buffer from the pool. The output buffers produced by
	 * the video aggregator base class will use this function to allocate. */

	if (!gst_buffer_pool_is_active(pool))
		gst_buffer_pool_set_active(pool, TRUE);

	return gst_buffer_pool_acquire_buffer(pool, outbuffer, NULL);
}


static gboolean gst_imx_compositor_negotiated_caps(GstImxBPVideoAggregator *videoaggregator, GstCaps *caps)
{
	GstVideoInfo info;
	GstImxCompositor *compositor = GST_IMX_COMPOSITOR(videoaggregator);

	/* Output caps have been negotiated. Set up a suitable DMA buffer pool
	 * (cleaning up any old buffer pool first) and inform subclass about
	 * the new output caps. */

	if (!gst_video_info_from_caps(&info, caps))
	{
		GST_ERROR_OBJECT(compositor, "could not get video info from negotiated caps");
		return FALSE;
	}

	/* Get the new overall width/height from video info */
	compositor->overall_width = GST_VIDEO_INFO_WIDTH(&info);
	compositor->overall_height = GST_VIDEO_INFO_HEIGHT(&info);

	GST_DEBUG_OBJECT(videoaggregator, "negotiated width/height: %u/%u", compositor->overall_width, compositor->overall_height);

	/* Update the overall region based on the new overall width/height */
	gst_imx_compositor_update_overall_region(compositor);

	/* Cleanup old buffer pool */
	if (compositor->dma_bufferpool != NULL)
		gst_object_unref(GST_OBJECT(compositor->dma_bufferpool));

	/* And get the new one */
	compositor->dma_bufferpool = gst_imx_compositor_create_bufferpool(compositor, caps, 0, 0, 0, NULL, NULL);

	if (compositor->dma_bufferpool != NULL)
	{
		GstImxCompositorClass *klass = GST_IMX_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(compositor));

		/* Inform subclass about the new output video info */
		if (klass->set_output_video_info != NULL)
			return klass->set_output_video_info(compositor, &info);
		else
			return TRUE;
	}
	else
		return FALSE;
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

	/* Catch redundant calls */
	if (compositor->overall_region_valid)
		return;

	if ((compositor->overall_width != 0) && (compositor->overall_height != 0))
	{
		/* If the width and height of the overall region are fixed to specific
		 * values by the caller, use these, and don't look at the canvases
		 * in the input pads. */

		compositor->overall_region.x2 = compositor->overall_width;
		compositor->overall_region.y2 = compositor->overall_height;
	}
	else
	{
		/* Overall width and/or height are set to 0. This means the caller wants
		 * the overall region to adapt to the sizes of the input canvases. The
		 * overall region must encompass and show all of them (exception:
		 * pads with negative xpos/ypos coordinates can have their canvas lie
		 * either partially or fully outside of the overall region).
		 * To compute this overall region, walk through all pads and merge their
		 * outer canvas regions together. */

		walk = GST_ELEMENT(compositor)->sinkpads;
		while (walk != NULL)
		{
			GstImxCompositorPad *compositor_pad = GST_IMX_COMPOSITOR_PAD_CAST(walk->data);
			GstImxRegion *outer_region = &(compositor_pad->canvas.outer_region);

			/* Update the outer region, since the xpos/ypos/width/height pad properties
			 * might have changed */
			gst_imx_compositor_pad_compute_outer_region(compositor_pad);

			/* The pad canvasses are *not* updated here. This is because in order for
			 * these updates to be done, a valid overall region needs to exist first.
			 * And the whole point of this loop is to compute said region.
			 * Furthermore, canvas updates anyway are unnecessary here. They'll be
			 * done later, during frame aggregation, when necessary. The only
			 * value that is needed here from the canvas is the outer region, and
			 * this one is already computed above. */

			if (first)
			{
				/* This is the first visited pad, so just copy its outer region */
				compositor->overall_region = *outer_region;
				first = FALSE;
			}
			else
				gst_imx_region_merge(&(compositor->overall_region), &(compositor->overall_region), outer_region);

			GST_DEBUG_OBJECT(compositor, "current outer region: %" GST_IMX_REGION_FORMAT "  merged overall region: %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(outer_region), GST_IMX_REGION_ARGS(&(compositor->overall_region)));

			/* Move to next pad */
			walk = g_list_next(walk);
		}
	}

	/* Make sure the overall region starts at (0,0), since any other topleft
	 * coordinates make little sense */
	compositor->overall_region.x1 = 0;
	compositor->overall_region.y1 = 0;

	/* Now that the overall region is computed, walk through the individual
	 * outer regions, and check if any of them completely cover the overall region
	 * If so, the compositor does not have to clear the frame first (= filling
	 * the overall region with fill_region), thus saving bandwidth */
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
