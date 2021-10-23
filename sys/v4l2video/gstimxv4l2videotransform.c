/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2021  Carlos Rafael Giani
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

#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <linux/videodev2.h>

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gst/imx/common/gstimxionallocator.h"
#include "gst/imx/video/gstimxvideodmabufferpool.h"
#include "gstimxv4l2videoformat.h"
#include "gstimxv4l2videotransform.h"


GST_DEBUG_CATEGORY_STATIC(imx_v4l2_video_transform_debug);
#define GST_CAT_DEFAULT imx_v4l2_video_transform_debug


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL))
);


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL))
);


enum
{
	PROP_0,
	PROP_DEVICE
};


#define DEFAULT_DEVICE "/dev/video1"


typedef struct
{
	enum v4l2_buf_type buf_type;
	gchar const *name;

	GstBuffer **queued_gstbuffers;
	gint *unqueued_buffer_indices;
	gint num_buffers;
	gint num_queued_buffers;

	gsize driver_plane_sizes[3];

	GstVideoInfo video_info;

	GstCaps *available_caps;

	gint min_num_required_buffers;

	gboolean initialized;
	gboolean stream_enabled;
}
GstImxV4L2VideoTransformQueue;


#define INIT_V4L2_QUEUE(QUEUE, BUFTYPE) \
	G_STMT_START { \
		(QUEUE)->buf_type = (BUFTYPE); \
		(QUEUE)->name = ((BUFTYPE) == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? "output" : "capture"; \
		(QUEUE)->initialized = FALSE; \
	} G_STMT_END


struct _GstImxV4L2VideoTransform
{
	GstBaseTransform parent;

	GstAllocator *imx_dma_buffer_allocator;
	GstBufferPool *input_buffer_pool, *output_buffer_pool;

	gchar *device;

	int v4l2_fd;

	GstImxV4L2VideoTransformQueue v4l2_output_queue, v4l2_capture_queue;
};


struct _GstImxV4L2VideoTransformClass
{
	GstBaseTransformClass parent_class;
};


/* Cached quark to avoid contention on the global quark table lock */
static GQuark meta_tag_video_quark;


G_DEFINE_TYPE(GstImxV4L2VideoTransform, gst_imx_v4l2_video_transform, GST_TYPE_BASE_TRANSFORM)

/* General element operations. */
static void gst_imx_v4l2_video_transform_dispose(GObject *object);
static void gst_imx_v4l2_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_v4l2_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_v4l2_video_transform_change_state(GstElement *element, GstStateChange transition);

/* Caps handling. */
static GstCaps* gst_imx_v4l2_video_transform_transform_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *input_caps, GstCaps *filter);
static GstCaps* gst_imx_v4l2_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static GstCaps* gst_imx_v4l2_video_transform_fixate_size_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static void gst_imx_v4l2_video_transform_fixate_format_caps(GstBaseTransform *transform, GstCaps *caps, GstCaps *othercaps);
static gboolean gst_imx_v4l2_video_transform_set_caps(GstBaseTransform *transform, GstCaps *input_caps, GstCaps *output_caps);

/* Allocator. */
static gboolean gst_imx_v4l2_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query);

/* Frame output. */
static GstFlowReturn gst_imx_v4l2_video_transform_prepare_output_buffer(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer **output_buffer);
static GstFlowReturn gst_imx_v4l2_video_transform_transform_frame(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer *output_buffer);
static gboolean gst_imx_v4l2_video_transform_transform_size(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, gsize size, GstCaps *othercaps, gsize *othersize);

/* Metadata and meta information. */
static gboolean gst_imx_v4l2_video_transform_transform_meta(GstBaseTransform *trans, GstBuffer *input_buffer, GstMeta *meta, GstBuffer *output_buffer);
static gboolean gst_imx_v4l2_video_transform_copy_metadata(GstBaseTransform *trans, GstBuffer *input_buffer, GstBuffer *output_buffer);

/* GstImxV4L2VideoTransform specific functions. */

static gboolean gst_imx_v4l2_video_transform_open(GstImxV4L2VideoTransform *self);
static void gst_imx_v4l2_video_transform_close(GstImxV4L2VideoTransform *self);

static gboolean gst_imx_v4l2_video_transform_probe_available_caps(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue);

static gboolean gst_imx_v4l2_video_transform_setup_v4l2_queue(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue, GstVideoInfo const *video_info);
static void gst_imx_v4l2_video_transform_teardown_v4l2_queue(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue);
static gboolean gst_imx_v4l2_video_transform_queue_buffer(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue, GstBuffer *gstbuffer);
static GstBuffer* gst_imx_v4l2_video_transform_dequeue_buffer(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue);
static gboolean gst_imx_v4l2_video_transform_enable_stream(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue, gboolean do_enable);


static void gst_imx_v4l2_video_transform_class_init(GstImxV4L2VideoTransformClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstBaseTransformClass *base_transform_class;

	GST_DEBUG_CATEGORY_INIT(imx_v4l2_video_transform_debug, "imxv4l2videotransform", 0, "NXP i.MX V4L2 video convert element");

	meta_tag_video_quark = g_quark_from_static_string(GST_META_TAG_VIDEO_STR);

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->dispose      = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_dispose);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_get_property);

	element_class->change_state = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_change_state);

	base_transform_class->transform_caps        = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_transform_caps);
	base_transform_class->fixate_caps           = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_fixate_caps);
	base_transform_class->set_caps              = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_set_caps);
	base_transform_class->decide_allocation     = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_decide_allocation);
	base_transform_class->transform             = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_transform_frame);
	base_transform_class->transform_size        = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_transform_size);
	base_transform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_prepare_output_buffer);
	base_transform_class->transform_meta        = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_transform_meta);
	base_transform_class->copy_metadata         = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_transform_copy_metadata);

	base_transform_class->passthrough_on_same_caps = TRUE;

	g_object_class_install_property(
		object_class,
		PROP_DEVICE,
		g_param_spec_string(
			"device",
			"Device",
			"Device location",
			DEFAULT_DEVICE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"i.MX V4L2 video transform",
		"Filter/Converter/Video/Scaler/Transform/Effect/Hardware",
		"Video transformation using V4L2 mem2mem",
		"Carlos Rafael Giani <crg7475@mailbox.org>"
	);
}


void gst_imx_v4l2_video_transform_init(GstImxV4L2VideoTransform *self)
{
	GstBaseTransform *base_transform = GST_BASE_TRANSFORM(self);

	self->imx_dma_buffer_allocator = NULL;
	self->input_buffer_pool = NULL;
	self->output_buffer_pool = NULL;

	self->device = g_strdup(DEFAULT_DEVICE);

	self->v4l2_fd = -1;

	INIT_V4L2_QUEUE(&(self->v4l2_output_queue), V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	INIT_V4L2_QUEUE(&(self->v4l2_capture_queue), V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	gst_base_transform_set_qos_enabled(base_transform, TRUE);
}


static void gst_imx_v4l2_video_transform_dispose(GObject *object)
{
	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(object);

	g_free(self->device);
	gst_caps_replace(&(self->v4l2_output_queue.available_caps), NULL);
	gst_caps_replace(&(self->v4l2_capture_queue.available_caps), NULL);

	G_OBJECT_CLASS(gst_imx_v4l2_video_transform_parent_class)->dispose(object);
}


static void gst_imx_v4l2_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_DEVICE:
			GST_OBJECT_LOCK(self);
			g_free(self->device);
			self->device = g_value_dup_string(value);
			GST_OBJECT_UNLOCK(self);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_v4l2_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_DEVICE:
			GST_OBJECT_LOCK(self);
			g_value_set_string(value, self->device);
			GST_OBJECT_UNLOCK(self);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_v4l2_video_transform_change_state(GstElement *element, GstStateChange transition)
{
	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(self != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if (!gst_imx_v4l2_video_transform_open(self))
				return GST_STATE_CHANGE_FAILURE;
			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_v4l2_video_transform_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
			gst_imx_v4l2_video_transform_close(self);
			break;

		default:
			break;
	}

	return ret;
}


static GstCaps* gst_imx_v4l2_video_transform_transform_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *input_caps, GstCaps *filter)
{
	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(transform);
	GstCaps *stripped_input_caps, *unfiltered_caps, *transformed_caps;
	GstImxV4L2VideoTransformQueue *v4l2_transform_queue;
	GstStructure *structure;
	GstCapsFeatures *features;
	gint caps_idx, num_caps;

	GST_DEBUG_OBJECT(
		self,
		"about to transform %s input caps %" GST_PTR_FORMAT " (filter caps %" GST_PTR_FORMAT ")",
		(direction == GST_PAD_SINK) ? "sink" : "src",
		(gpointer)input_caps,
		(gpointer)filter
	);

	v4l2_transform_queue = (direction == GST_PAD_SINK) ? &(self->v4l2_capture_queue) : &(self->v4l2_output_queue);
	g_assert(v4l2_transform_queue != NULL);

	/* Strip the format, chroma-site, and colorimetry fields from the caps.
	 * chroma-site, and colorimetry are not supported by this converter.
	 * As for the formats, the list of formats that this element can convert
	 * to is _not_ depending on the formats in the input caps. So, just
	 * remove the format field from the input caps, then intersect those
	 * with the available_caps from the v4l2_transform_queue to insert
	 * the formats the caps can be transformed to. */

	/* First, strip the input caps. */

	stripped_input_caps = gst_caps_new_empty();
	num_caps = gst_caps_get_size(input_caps);

	for (caps_idx = 0; caps_idx < num_caps; ++caps_idx)
	{
		structure = gst_caps_get_structure(input_caps, caps_idx);
		features = gst_caps_get_features(input_caps, caps_idx);

		/* Copy the caps features since the gst_caps_append_structure_full()
		 * call below takes ownership over the caps features object. */
		features = gst_caps_features_copy(features);

		/* If this is already expressed by the existing stripped_input_caps, skip this structure. */
		if ((caps_idx > 0) && gst_caps_is_subset_structure_full(stripped_input_caps, structure, features))
		{
			gst_caps_features_free(features);
			continue;
		}

		/* Make the stripped copy. */
		structure = gst_structure_copy(structure);
		gst_structure_remove_fields(structure, "colorimetry", "chroma-site", "format", NULL);
		gst_caps_append_structure_full(stripped_input_caps, structure, features);
	}

	GST_DEBUG_OBJECT(transform, "got stripped input caps: %" GST_PTR_FORMAT, (gpointer)stripped_input_caps);

	/* Next, intersect stripped_input_caps with the available_caps from the V4L2 queue
	 * that corresponds to the _opposite_ side. So, if for example "direction" indicates
	 * that the input_caps are associated with the sink caps, then we intersect
	 * stripped_input_caps with the V4L2 _capture_ queue (since in V4L2 jargon, "capture"
	 * corresponds to the srcpad), and vice versa.
	 * If there are no available_caps (because transform_caps is called before these
	 * caps were probed), just continue using the stripped_input_caps. */

	if (v4l2_transform_queue->available_caps != NULL)
	{
		GST_DEBUG_OBJECT(
			self,
			"intersecting stripped input caps with available V4L2 %s queue caps %" GST_PTR_FORMAT,
			v4l2_transform_queue->name,
			(gpointer)(v4l2_transform_queue->available_caps)
		);
		unfiltered_caps = gst_caps_intersect_full(stripped_input_caps, v4l2_transform_queue->available_caps, GST_CAPS_INTERSECT_FIRST);
		gst_caps_unref(stripped_input_caps);
	}
	else
		unfiltered_caps = stripped_input_caps;

	GST_DEBUG_OBJECT(transform, "got unfiltered caps: %" GST_PTR_FORMAT, (gpointer)unfiltered_caps);

	/* Apply the filter on the unfiltered caps. */

	if (filter != NULL)
	{
		transformed_caps = gst_caps_intersect_full(unfiltered_caps, filter, GST_CAPS_INTERSECT_FIRST);
		GST_DEBUG_OBJECT(
			transform,
			"applied filter %" GST_PTR_FORMAT " -> filtered caps are the transformed caps: %" GST_PTR_FORMAT,
			(gpointer)filter,
			(gpointer)transformed_caps
		);
		gst_caps_unref(unfiltered_caps);
	}
	else
	{
		transformed_caps = unfiltered_caps;
		GST_DEBUG_OBJECT(transform, "no filter specified -> unfiltered caps are the transformed caps: %" GST_PTR_FORMAT, (gpointer)transformed_caps);
	}

	return transformed_caps;
}


/* NOTE: The following functions are taken almost 1:1 from the upstream videoconvert element:
 * gst_imx_v4l2_video_transform_fixate_caps
 * gst_imx_v4l2_video_transform_fixate_size_caps
 * score_value
 * gst_imx_v4l2_video_transform_fixate_format_caps
 * */


static GstCaps* gst_imx_v4l2_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
	GstCaps *result;

	GST_DEBUG_OBJECT(transform, "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, (gpointer)othercaps, (gpointer)caps);

	result = gst_caps_truncate(othercaps);
	result = gst_caps_make_writable(result);
	GST_DEBUG_OBJECT(transform, "truncated caps to: %" GST_PTR_FORMAT, (gpointer)result);

	result = gst_imx_v4l2_video_transform_fixate_size_caps(transform, direction, caps, result);
	GST_DEBUG_OBJECT(transform, "fixated size to: %" GST_PTR_FORMAT, (gpointer)result);

	gst_imx_v4l2_video_transform_fixate_format_caps(transform, caps, result);
	GST_DEBUG_OBJECT(transform, "fixated format to: %" GST_PTR_FORMAT, (gpointer)result);

	result = gst_caps_fixate(result);
	GST_DEBUG_OBJECT(transform, "fixated remaining fields to: %" GST_PTR_FORMAT, (gpointer)result);

	if (direction == GST_PAD_SINK)
	{
		if (gst_caps_is_subset(caps, result))
		{
			GST_DEBUG_OBJECT(transform, "sink caps %" GST_PTR_FORMAT " are a subset of the fixated caps; using original sink caps as result instead", (gpointer)caps);
			gst_caps_replace(&result, caps);
		}
	}

	return result;
}


static GstCaps* gst_imx_v4l2_video_transform_fixate_size_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
	GstStructure *ins, *outs;
	GValue const *from_par, *to_par;
	GValue fpar = { 0, }, tpar = { 0, };

	ins = gst_caps_get_structure(caps, 0);
	outs = gst_caps_get_structure(othercaps, 0);

	from_par = gst_structure_get_value(ins, "pixel-aspect-ratio");
	to_par = gst_structure_get_value(outs, "pixel-aspect-ratio");

	/* If we're fixating from the sinkpad we always set the PAR and
	 * assume that missing PAR on the sinkpad means 1/1 and
	 * missing PAR on the srcpad means undefined
	 */
	if (direction == GST_PAD_SINK)
	{
		if (!from_par)
		{
			g_value_init(&fpar, GST_TYPE_FRACTION);
			gst_value_set_fraction(&fpar, 1, 1);
			from_par = &fpar;
		}
		if (!to_par)
		{
			g_value_init(&tpar, GST_TYPE_FRACTION_RANGE);
			gst_value_set_fraction_range_full(&tpar, 1, G_MAXINT, G_MAXINT, 1);
			to_par = &tpar;
		}
	} else
	{
		if (!to_par)
		{
			g_value_init(&tpar, GST_TYPE_FRACTION);
			gst_value_set_fraction(&tpar, 1, 1);
			to_par = &tpar;

			gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
		}
		if (!from_par)
		{
			g_value_init(&fpar, GST_TYPE_FRACTION);
			gst_value_set_fraction (&fpar, 1, 1);
			from_par = &fpar;
		}
	}

	/* we have both PAR but they might not be fixated */
	{
		gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
		gint w = 0, h = 0;
		gint from_dar_n, from_dar_d;
		gint num, den;

		/* from_par should be fixed */
		g_return_val_if_fail(gst_value_is_fixed(from_par), othercaps);

		from_par_n = gst_value_get_fraction_numerator(from_par);
		from_par_d = gst_value_get_fraction_denominator(from_par);

		gst_structure_get_int(ins, "width", &from_w);
		gst_structure_get_int(ins, "height", &from_h);

		gst_structure_get_int(outs, "width", &w);
		gst_structure_get_int(outs, "height", &h);

		/* if both width and height are already fixed, we can't do anything
		 * about it anymore */
		if (w && h) {
			guint n, d;

			GST_DEBUG_OBJECT(transform, "dimensions already set to %dx%d, not fixating", w, h);
			if (!gst_value_is_fixed(to_par))
			{
				if (gst_video_calculate_display_ratio(&n, &d, from_w, from_h, from_par_n, from_par_d, w, h))
				{
					GST_DEBUG_OBJECT(transform, "fixating to_par to %dx%d", n, d);
					if (gst_structure_has_field(outs, "pixel-aspect-ratio"))
						gst_structure_fixate_field_nearest_fraction(outs, "pixel-aspect-ratio", n, d);
					else if (n != d)
						gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, n, d, NULL);
				}
			}
			goto done;
		}

		/* Calculate input DAR */
		if (!gst_util_fraction_multiply(from_w, from_h, from_par_n, from_par_d, &from_dar_n, &from_dar_d))
		{
			GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL),
					("Error calculating the output scaled size - integer overflow"));
			goto done;
		}

		GST_DEBUG_OBJECT(transform, "Input DAR is %d/%d", from_dar_n, from_dar_d);

		/* If either width or height are fixed there's not much we
		 * can do either except choosing a height or width and PAR
		 * that matches the DAR as good as possible
		 */
		if (h)
		{
			GstStructure *tmp;
			gint set_w, set_par_n, set_par_d;

			GST_DEBUG_OBJECT(transform, "height is fixed (%d)", h);

			/* If the PAR is fixed too, there's not much to do
			 * except choosing the width that is nearest to the
			 * width with the same DAR */
			if (gst_value_is_fixed(to_par))
			{
				to_par_n = gst_value_get_fraction_numerator(to_par);
				to_par_d = gst_value_get_fraction_denominator(to_par);

				GST_DEBUG_OBJECT(transform, "PAR is fixed %d/%d", to_par_n, to_par_d);

				if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_d, to_par_n, &num, &den))
				{
					GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
					goto done;
				}

				w = (guint) gst_util_uint64_scale_int(h, num, den);
				gst_structure_fixate_field_nearest_int(outs, "width", w);

				goto done;
			}

			/* The PAR is not fixed and it's quite likely that we can set
			 * an arbitrary PAR. */

			/* Check if we can keep the input width */
			tmp = gst_structure_copy(outs);
			gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
			gst_structure_get_int(tmp, "width", &set_w);

			/* Might have failed but try to keep the DAR nonetheless by
			 * adjusting the PAR */
			if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, h, set_w, &to_par_n, &to_par_d))
			{
				GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
				gst_structure_free(tmp);
				goto done;
			}

			if (!gst_structure_has_field(tmp, "pixel-aspect-ratio"))
				gst_structure_set_value(tmp, "pixel-aspect-ratio", to_par);
			gst_structure_fixate_field_nearest_fraction(tmp, "pixel-aspect-ratio", to_par_n, to_par_d);
			gst_structure_get_fraction(tmp, "pixel-aspect-ratio", &set_par_n, &set_par_d);
			gst_structure_free(tmp);

			/* Check if the adjusted PAR is accepted */
			if (set_par_n == to_par_n && set_par_d == to_par_d)
			{
				if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
					gst_structure_set(outs, "width", G_TYPE_INT, set_w, "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);
				goto done;
			}

			/* Otherwise scale the width to the new PAR and check if the
			 * adjusted with is accepted. If all that fails we can't keep
			 * the DAR */
			if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, set_par_d, set_par_n, &num, &den))
			{
				GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
				goto done;
			}

			w = (guint) gst_util_uint64_scale_int(h, num, den);
			gst_structure_fixate_field_nearest_int(outs, "width", w);
			if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
				gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);

			goto done;
		}
		else if (w)
		{
			GstStructure *tmp;
			gint set_h, set_par_n, set_par_d;

			GST_DEBUG_OBJECT(transform, "width is fixed (%d)", w);

			/* If the PAR is fixed too, there's not much to do
			 * except choosing the height that is nearest to the
			 * height with the same DAR */
			if (gst_value_is_fixed(to_par))
			{
				to_par_n = gst_value_get_fraction_numerator(to_par);
				to_par_d = gst_value_get_fraction_denominator(to_par);

				GST_DEBUG_OBJECT(transform, "PAR is fixed %d/%d", to_par_n, to_par_d);

				if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_d, to_par_n, &num, &den))
				{
					GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
					goto done;
				}

				h = (guint) gst_util_uint64_scale_int(w, den, num);
				gst_structure_fixate_field_nearest_int(outs, "height", h);

				goto done;
			}

			/* The PAR is not fixed and it's quite likely that we can set
			 * an arbitrary PAR. */

			/* Check if we can keep the input height */
			tmp = gst_structure_copy(outs);
			gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
			gst_structure_get_int(tmp, "height", &set_h);

			/* Might have failed but try to keep the DAR nonetheless by
			 * adjusting the PAR */
			if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, set_h, w, &to_par_n, &to_par_d))
			{
				GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
				gst_structure_free(tmp);
				goto done;
			}
			if (!gst_structure_has_field(tmp, "pixel-aspect-ratio"))
				gst_structure_set_value(tmp, "pixel-aspect-ratio", to_par);
			gst_structure_fixate_field_nearest_fraction(tmp, "pixel-aspect-ratio", to_par_n, to_par_d);
			gst_structure_get_fraction(tmp, "pixel-aspect-ratio", &set_par_n, &set_par_d);
			gst_structure_free(tmp);

			/* Check if the adjusted PAR is accepted */
			if (set_par_n == to_par_n && set_par_d == to_par_d)
			{
				if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
					gst_structure_set(outs, "height", G_TYPE_INT, set_h,
							"pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
							NULL);
				goto done;
			}

			/* Otherwise scale the height to the new PAR and check if the
			 * adjusted with is accepted. If all that fails we can't keep
			 * the DAR */
			if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, set_par_d, set_par_n, &num, &den))
			{
				GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
				goto done;
			}

			h = (guint) gst_util_uint64_scale_int(w, den, num);
			gst_structure_fixate_field_nearest_int(outs, "height", h);
			if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
				gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);

			goto done;
		}
		else if (gst_value_is_fixed(to_par))
		{
			GstStructure *tmp;
			gint set_h, set_w, f_h, f_w;

			to_par_n = gst_value_get_fraction_numerator(to_par);
			to_par_d = gst_value_get_fraction_denominator(to_par);

			/* Calculate scale factor for the PAR change */
			if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_n, to_par_d, &num, &den))
			{
				GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
				goto done;
			}

			/* Try to keep the input height (because of interlacing) */
			tmp = gst_structure_copy(outs);
			gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
			gst_structure_get_int(tmp, "height", &set_h);

			/* This might have failed but try to scale the width
			 * to keep the DAR nonetheless */
			w = (guint) gst_util_uint64_scale_int(set_h, num, den);
			gst_structure_fixate_field_nearest_int(tmp, "width", w);
			gst_structure_get_int(tmp, "width", &set_w);
			gst_structure_free(tmp);

			/* We kept the DAR and the height is nearest to the original height */
			if (set_w == w)
			{
				gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height", G_TYPE_INT, set_h, NULL);
				goto done;
			}

			f_h = set_h;
			f_w = set_w;

			/* If the former failed, try to keep the input width at least */
			tmp = gst_structure_copy(outs);
			gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
			gst_structure_get_int(tmp, "width", &set_w);

			/* This might have failed but try to scale the width
			 * to keep the DAR nonetheless */
			h = (guint) gst_util_uint64_scale_int(set_w, den, num);
			gst_structure_fixate_field_nearest_int(tmp, "height", h);
			gst_structure_get_int(tmp, "height", &set_h);
			gst_structure_free(tmp);

			/* We kept the DAR and the width is nearest to the original width */
			if (set_h == h)
			{
				gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height", G_TYPE_INT, set_h, NULL);
				goto done;
			}

			/* If all this failed, keep the height that was nearest to the orignal
			 * height and the nearest possible width. This changes the DAR but
			 * there's not much else to do here.
			 */
			gst_structure_set(outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT, f_h, NULL);
			goto done;
		}
		else
		{
			GstStructure *tmp;
			gint set_h, set_w, set_par_n, set_par_d, tmp2;

			/* width, height and PAR are not fixed but passthrough is not possible */

			/* First try to keep the height and width as good as possible
			 * and scale PAR */
			tmp = gst_structure_copy(outs);
			gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
			gst_structure_get_int(tmp, "height", &set_h);
			gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
			gst_structure_get_int(tmp, "width", &set_w);

			if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, set_h, set_w, &to_par_n, &to_par_d))
			{
				GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
				goto done;
			}

			if (!gst_structure_has_field(tmp, "pixel-aspect-ratio"))
				gst_structure_set_value(tmp, "pixel-aspect-ratio", to_par);
			gst_structure_fixate_field_nearest_fraction(tmp, "pixel-aspect-ratio", to_par_n, to_par_d);
			gst_structure_get_fraction(tmp, "pixel-aspect-ratio", &set_par_n, &set_par_d);
			gst_structure_free(tmp);

			if (set_par_n == to_par_n && set_par_d == to_par_d)
			{
				gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height", G_TYPE_INT, set_h, NULL);

				if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
					gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);
				goto done;
			}

			/* Otherwise try to scale width to keep the DAR with the set
			 * PAR and height */
			if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, set_par_d, set_par_n, &num, &den))
			{
				GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("Error calculating the output scaled size - integer overflow"));
				goto done;
			}

			w = (guint) gst_util_uint64_scale_int(set_h, num, den);
			tmp = gst_structure_copy(outs);
			gst_structure_fixate_field_nearest_int(tmp, "width", w);
			gst_structure_get_int(tmp, "width", &tmp2);
			gst_structure_free(tmp);

			if (tmp2 == w)
			{
				gst_structure_set(outs, "width", G_TYPE_INT, tmp2, "height", G_TYPE_INT, set_h, NULL);
				if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
					gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);
				goto done;
			}

			/* ... or try the same with the height */
			h = (guint) gst_util_uint64_scale_int(set_w, den, num);
			tmp = gst_structure_copy(outs);
			gst_structure_fixate_field_nearest_int(tmp, "height", h);
			gst_structure_get_int(tmp, "height", &tmp2);
			gst_structure_free(tmp);

			if (tmp2 == h)
			{
				gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height", G_TYPE_INT, tmp2, NULL);
				if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
					gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);
				goto done;
			}

			/* If all fails we can't keep the DAR and take the nearest values
			 * for everything from the first try */
			gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height", G_TYPE_INT, set_h, NULL);
			if (gst_structure_has_field(outs, "pixel-aspect-ratio") || set_par_n != set_par_d)
				gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d, NULL);
		}
	}

done:
	gst_imx_v4l2_video_transform_fixate_format_caps(transform, caps, othercaps);

	GST_DEBUG_OBJECT(transform, "fixated othercaps to %" GST_PTR_FORMAT, (gpointer)othercaps);

	if (from_par == &fpar)
		g_value_unset(&fpar);
	if (to_par == &tpar)
		g_value_unset(&tpar);

	return othercaps;
}


/*
 * This is an incomplete matrix of in formats and a score for the preferred output
 * format.
 *
 *         out: RGB24   RGB16  ARGB  AYUV  YUV444  YUV422 YUV420 YUV411 YUV410  PAL  GRAY
 *  in
 * RGB24          0      2       1     2     2       3      4      5      6      7    8
 * RGB16          1      0       1     2     2       3      4      5      6      7    8
 * ARGB           2      3       0     1     4       5      6      7      8      9    10
 * AYUV           3      4       1     0     2       5      6      7      8      9    10
 * YUV444         2      4       3     1     0       5      6      7      8      9    10
 * YUV422         3      5       4     2     1       0      6      7      8      9    10
 * YUV420         4      6       5     3     2       1      0      7      8      9    10
 * YUV411         4      6       5     3     2       1      7      0      8      9    10
 * YUV410         6      8       7     5     4       3      2      1      0      9    10
 * PAL            1      3       2     6     4       6      7      8      9      0    10
 * GRAY           1      4       3     2     1       5      6      7      8      9    0
 *
 * PAL or GRAY are never preferred, if we can we would convert to PAL instead
 * of GRAY, though
 * less subsampling is preferred and if any, preferably horizontal
 * We would like to keep the alpha, even if we would need to to colorspace conversion
 * or lose depth.
 */
#define SCORE_FORMAT_CHANGE       1
#define SCORE_DEPTH_CHANGE        1
#define SCORE_ALPHA_CHANGE        1
#define SCORE_CHROMA_W_CHANGE     1
#define SCORE_CHROMA_H_CHANGE     1
#define SCORE_PALETTE_CHANGE      1

#define SCORE_COLORSPACE_LOSS     2     /* RGB <-> YUV */
#define SCORE_DEPTH_LOSS          4     /* change bit depth */
#define SCORE_ALPHA_LOSS          8     /* lose the alpha channel */
#define SCORE_CHROMA_W_LOSS      16     /* vertical subsample */
#define SCORE_CHROMA_H_LOSS      32     /* horizontal subsample */
#define SCORE_PALETTE_LOSS       64     /* convert to palette format */
#define SCORE_COLOR_LOSS        128     /* convert to GRAY */

#define COLORSPACE_MASK (GST_VIDEO_FORMAT_FLAG_YUV | \
                         GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK      (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK    (GST_VIDEO_FORMAT_FLAG_PALETTE)

/* calculate how much loss a conversion would be */
static void score_value(GstBaseTransform * base, const GstVideoFormatInfo * in_info, const GValue * val, gint * min_loss, const GstVideoFormatInfo ** out_info)
{
	const gchar *fname;
	const GstVideoFormatInfo *t_info;
	GstVideoFormatFlags in_flags, t_flags;
	gint loss;

	fname = g_value_get_string(val);
	t_info = gst_video_format_get_info(gst_video_format_from_string(fname));
	if (!t_info)
		return;

	/* accept input format immediately without loss */
	if (in_info == t_info)
	{
		*min_loss = 0;
		*out_info = t_info;
		return;
	}

	loss = SCORE_FORMAT_CHANGE;

	in_flags = GST_VIDEO_FORMAT_INFO_FLAGS(in_info);
	in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
	in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
	in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

	t_flags = GST_VIDEO_FORMAT_INFO_FLAGS(t_info);
	t_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
	t_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
	t_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

	if ((t_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK))
	{
		loss += SCORE_PALETTE_CHANGE;
		if (t_flags & PALETTE_MASK)
			loss += SCORE_PALETTE_LOSS;
	}

	if ((t_flags & COLORSPACE_MASK) != (in_flags & COLORSPACE_MASK))
	{
		loss += SCORE_COLORSPACE_LOSS;
		if (t_flags & GST_VIDEO_FORMAT_FLAG_GRAY)
			loss += SCORE_COLOR_LOSS;
	}

	if ((t_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK))
	{
		loss += SCORE_ALPHA_CHANGE;
		if (in_flags & ALPHA_MASK)
			loss += SCORE_ALPHA_LOSS;
	}

	if ((in_info->h_sub[1]) != (t_info->h_sub[1]))
	{
		loss += SCORE_CHROMA_H_CHANGE;
		if ((in_info->h_sub[1]) < (t_info->h_sub[1]))
			loss += SCORE_CHROMA_H_LOSS;
	}
	if ((in_info->w_sub[1]) != (t_info->w_sub[1]))
	{
		loss += SCORE_CHROMA_W_CHANGE;
		if ((in_info->w_sub[1]) < (t_info->w_sub[1]))
			loss += SCORE_CHROMA_W_LOSS;
	}

	if ((in_info->bits) != (t_info->bits))
	{
		loss += SCORE_DEPTH_CHANGE;
		if ((in_info->bits) > (t_info->bits))
			loss += SCORE_DEPTH_LOSS;
	}

	GST_DEBUG_OBJECT(base, "score %s -> %s = %d", GST_VIDEO_FORMAT_INFO_NAME(in_info), GST_VIDEO_FORMAT_INFO_NAME(t_info), loss);

	if (loss < *min_loss)
	{
		GST_DEBUG_OBJECT(base, "found new best %d", loss);
		*out_info = t_info;
		*min_loss = loss;
	}
}


static void gst_imx_v4l2_video_transform_fixate_format_caps(GstBaseTransform *transform, GstCaps *caps, GstCaps *othercaps)
{
	GstStructure *ins, *outs;
	const gchar *in_format;
	const GstVideoFormatInfo *in_info, *out_info = NULL;
	gint min_loss = G_MAXINT;
	guint i, capslen;

	ins = gst_caps_get_structure(caps, 0);
	in_format = gst_structure_get_string(ins, "format");
	if (!in_format)
		return;

	GST_DEBUG_OBJECT(transform, "source format %s", in_format);

	in_info = gst_video_format_get_info(gst_video_format_from_string(in_format));
	if (!in_info)
		return;

	outs = gst_caps_get_structure(othercaps, 0);

	capslen = gst_caps_get_size(othercaps);
	GST_DEBUG_OBJECT(transform, "iterate %d structures", capslen);
	for (i = 0; i < capslen; i++)
	{
		GstStructure *tests;
		const GValue *format;

		tests = gst_caps_get_structure(othercaps, i);
		format = gst_structure_get_value(tests, "format");
		/* should not happen */
		if (format == NULL)
			continue;

		if (GST_VALUE_HOLDS_LIST(format))
		{
			gint j, len;

			len = gst_value_list_get_size(format);
			GST_DEBUG_OBJECT(transform, "have %d formats", len);
			for (j = 0; j < len; j++)
			{
				const GValue *val;

				val = gst_value_list_get_value(format, j);
				if (G_VALUE_HOLDS_STRING(val))
				{
					score_value(transform, in_info, val, &min_loss, &out_info);
					if (min_loss == 0)
						break;
				}
			}
		} else if (G_VALUE_HOLDS_STRING(format))
			score_value(transform, in_info, format, &min_loss, &out_info);
	}
	if (out_info)
		gst_structure_set(outs, "format", G_TYPE_STRING, GST_VIDEO_FORMAT_INFO_NAME(out_info), NULL);
}


static gboolean gst_imx_v4l2_video_transform_set_caps(GstBaseTransform *transform, GstCaps *input_caps, GstCaps *output_caps)
{
	GstVideoInfo video_info;
	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(transform);

	/* We do not drain here, since the mem2mem device does not _actually_
	 * queue frames. As soon as a frame is pushed into the output queue,
	 * the device begins processing, and eventually puts the converted
	 * frame into the capture queue. */

	GST_DEBUG_OBJECT(self, "setting caps:  input: %" GST_PTR_FORMAT "  output: %" GST_PTR_FORMAT, (gpointer)input_caps, (gpointer)output_caps);

	gst_imx_v4l2_video_transform_teardown_v4l2_queue(self, &(self->v4l2_output_queue));
	gst_imx_v4l2_video_transform_teardown_v4l2_queue(self, &(self->v4l2_capture_queue));

	if (!gst_video_info_from_caps(&video_info, input_caps))
	{
		GST_ERROR_OBJECT(self, "could not convert input caps to video info; caps: %" GST_PTR_FORMAT, (gpointer)input_caps);
	}

	if (!gst_imx_v4l2_video_transform_setup_v4l2_queue(self, &(self->v4l2_output_queue), &video_info))
		return FALSE;

	if (!gst_video_info_from_caps(&video_info, output_caps))
	{
		GST_ERROR_OBJECT(self, "could not convert output caps to video info; caps: %" GST_PTR_FORMAT, (gpointer)output_caps);
	}

	if (!gst_imx_v4l2_video_transform_setup_v4l2_queue(self, &(self->v4l2_capture_queue), &video_info))
		return FALSE;

	if (self->input_buffer_pool != NULL)
	{
		gst_buffer_pool_set_active(self->input_buffer_pool, FALSE);
		gst_object_unref(GST_OBJECT(self->input_buffer_pool));
		self->input_buffer_pool = NULL;
	}

	if (self->output_buffer_pool != NULL)
	{
		gst_buffer_pool_set_active(self->output_buffer_pool, FALSE);
		gst_object_unref(GST_OBJECT(self->output_buffer_pool));
		self->output_buffer_pool = NULL;
	}

	self->input_buffer_pool = gst_imx_video_dma_buffer_pool_new(
		self->imx_dma_buffer_allocator,
		&(self->v4l2_output_queue.video_info),
		TRUE,
		self->v4l2_output_queue.driver_plane_sizes
	);
	gst_buffer_pool_set_active(self->input_buffer_pool, TRUE);

	self->output_buffer_pool = gst_imx_video_dma_buffer_pool_new(
		self->imx_dma_buffer_allocator,
		&(self->v4l2_capture_queue.video_info),
		TRUE,
		self->v4l2_capture_queue.driver_plane_sizes
	);
	gst_buffer_pool_set_active(self->output_buffer_pool, TRUE);

	return TRUE;
}


static gboolean gst_imx_v4l2_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query)
{
	/* NOTE: This actually amounts to a no-op, since we install our
	 * own prepare_output_buffer vfunc. That one does not chain up
	 * to the base class, and the vfunc of that base class is the
	 * one that uses the buffer pool and allocator that are picked
	 * by decide_allocation. Our prepare_output_buffer _doesn't_
	 * use the contents of the allocation query. */

	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(transform);
	guint buffer_size;

	buffer_size = GST_VIDEO_INFO_SIZE(gst_imx_video_dma_buffer_pool_get_video_info(self->output_buffer_pool));

	if (gst_query_get_n_allocation_params(query) > 0)
		gst_query_set_nth_allocation_param(query, 0, self->imx_dma_buffer_allocator, NULL);
	else
		gst_query_add_allocation_param(query, self->imx_dma_buffer_allocator, NULL);

	if (gst_query_get_n_allocation_pools(query) > 0)
		gst_query_set_nth_allocation_pool(query, 0, self->output_buffer_pool, buffer_size, 0, 0);
	else
		gst_query_add_allocation_pool(query, self->output_buffer_pool, buffer_size, 0, 0);

	return GST_BASE_TRANSFORM_CLASS(gst_imx_v4l2_video_transform_parent_class)->decide_allocation(transform, query);
}


static GstFlowReturn gst_imx_v4l2_video_transform_prepare_output_buffer(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	gint i;
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstImxV4L2VideoTransform *self = GST_IMX_V4L2_VIDEO_TRANSFORM(transform);
	GstBuffer *original_input_buffer = input_buffer;

	*output_buffer = NULL;

	g_assert(self->v4l2_capture_queue.initialized);

	if (gst_is_dmabuf_memory(gst_buffer_peek_memory(input_buffer, 0)))
	{
		gst_buffer_ref(input_buffer);
	}
	else
	{
		gint y, width, height, plane_index;
		GstBuffer *uploaded_input_buffer;
		GstVideoInfo *video_info;
		GstVideoFrame video_frame;

		video_info = &(self->v4l2_output_queue.video_info);

		flow_ret = gst_buffer_pool_acquire_buffer(self->input_buffer_pool, &uploaded_input_buffer, NULL);
		if (G_UNLIKELY(flow_ret) != GST_FLOW_OK)
			goto error;

		width = GST_VIDEO_INFO_WIDTH(video_info);
		height = GST_VIDEO_INFO_HEIGHT(video_info);

		if (gst_video_frame_map(&video_frame, video_info, input_buffer, GST_MAP_READ))
		{
			for (plane_index = 0; plane_index < (gint)GST_VIDEO_INFO_N_PLANES(video_info); ++plane_index)
			{
				GstMemory *memory;
				GstMapInfo map_info;
				guint8 const *src_pixels;
				guint8 *dest_pixels;

				memory = gst_buffer_peek_memory(uploaded_input_buffer, plane_index);
				g_assert(memory != NULL);

				gst_memory_map(memory, &map_info, GST_MAP_WRITE);

				src_pixels = GST_VIDEO_FRAME_PLANE_DATA(&video_frame, plane_index);
				dest_pixels = map_info.data;

				for (y = 0; y < height; ++y)
				{
					memcpy(
						dest_pixels + y * GST_VIDEO_INFO_PLANE_STRIDE(video_info, plane_index),
						src_pixels + y * GST_VIDEO_FRAME_PLANE_STRIDE(&video_frame, plane_index),
						width * GST_VIDEO_FRAME_COMP_PSTRIDE(&video_frame, plane_index)
					);
				}

				gst_memory_unmap(memory, &map_info);
			}

			gst_video_frame_unmap(&video_frame);
		}
		else
		{
			GST_ERROR_OBJECT(self, "could not map input buffer");
			gst_buffer_unref(uploaded_input_buffer);
			goto error;
		}

		input_buffer = uploaded_input_buffer;
	}

	if (!(self->v4l2_capture_queue.stream_enabled))
	{
		for (i = 0; i < self->v4l2_capture_queue.num_buffers; ++i)
		{
			GstBuffer *gstbuffer;
			gboolean ret;
			flow_ret = gst_buffer_pool_acquire_buffer(self->output_buffer_pool, &gstbuffer, NULL);
			if (G_UNLIKELY(flow_ret) != GST_FLOW_OK)
				goto error;

			GST_LOG_OBJECT(self, "queuing V4L2 capture buffer with index %d", i);
			ret = gst_imx_v4l2_video_transform_queue_buffer(self, &(self->v4l2_capture_queue), gstbuffer);

			gst_buffer_unref(gstbuffer);

			if (!ret)
				goto error;
		}

		if (!gst_imx_v4l2_video_transform_enable_stream(self, &(self->v4l2_capture_queue), TRUE))
			goto error;
	}
	else
	{
		GstBuffer *gstbuffer;
		gboolean ret;

		GST_LOG_OBJECT(self, "acquiring new buffer to queue it in the V4L2 capture queue");

		flow_ret = gst_buffer_pool_acquire_buffer(self->output_buffer_pool, &gstbuffer, NULL);
		if (G_UNLIKELY(flow_ret) != GST_FLOW_OK)
			goto error;

		ret = gst_imx_v4l2_video_transform_queue_buffer(self, &(self->v4l2_capture_queue), gstbuffer);

		gst_buffer_unref(gstbuffer);

		if (!ret)
			goto error;
	}

	if (self->v4l2_output_queue.stream_enabled)
	{
		GstBuffer *previous_input_buffer;
		GST_LOG_OBJECT(self, "dequeuing previously queued V4L2 output buffer since associated upstream frame was already processed");
		previous_input_buffer = gst_imx_v4l2_video_transform_dequeue_buffer(self, &(self->v4l2_output_queue));
		gst_buffer_unref(previous_input_buffer);
	}

	GST_LOG_OBJECT(self, "queuing new V4L2 output buffer to process upstream frame");
	if (!gst_imx_v4l2_video_transform_queue_buffer(self, &(self->v4l2_output_queue), input_buffer))
		goto error;

	if (!(self->v4l2_output_queue.stream_enabled))
	{
		if (!gst_imx_v4l2_video_transform_enable_stream(self, &(self->v4l2_output_queue), TRUE))
			goto error;		
	}

	GST_LOG_OBJECT(self, "dequeuing V4L2 capture buffer to retrieve converted frame");
	*output_buffer = gst_imx_v4l2_video_transform_dequeue_buffer(self, &(self->v4l2_capture_queue));
	if (G_UNLIKELY(*output_buffer == NULL))
		goto error;

	gst_imx_v4l2_video_transform_copy_metadata(transform, original_input_buffer, *output_buffer);

finish:
	if ((flow_ret != GST_FLOW_OK) && (*output_buffer != NULL))
	{
		gst_buffer_unref(*output_buffer);
		*output_buffer = NULL;
	}

	gst_buffer_unref(input_buffer);

	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


static GstFlowReturn gst_imx_v4l2_video_transform_transform_frame(G_GNUC_UNUSED GstBaseTransform *transform, G_GNUC_UNUSED GstBuffer *input_buffer, G_GNUC_UNUSED GstBuffer *output_buffer)
{
	/* Nothing to do here; processing is done in prepare_output_buffer. */
	return GST_FLOW_OK;
}


static gboolean gst_imx_v4l2_video_transform_transform_size(G_GNUC_UNUSED GstBaseTransform *transform, G_GNUC_UNUSED GstPadDirection direction, G_GNUC_UNUSED GstCaps *caps, G_GNUC_UNUSED gsize size, GstCaps *othercaps, gsize *othersize)
{
	GstVideoInfo video_info;

	if (G_UNLIKELY(!gst_video_info_from_caps(&video_info, othercaps)))
		return FALSE;

	*othersize = GST_VIDEO_INFO_SIZE(&video_info);

	return TRUE;
}


static gboolean gst_imx_v4l2_video_transform_transform_meta(GstBaseTransform *trans, GstBuffer *input_buffer, GstMeta *meta, GstBuffer *output_buffer)
{
	GstMetaInfo const *info = meta->info;
	gchar const * const *tags;

	tags = gst_meta_api_type_get_tags(info->api);

	/* If there is only one meta tag, and it is the video one,
	 * we can safely instruct the base class to copy the meta.
	 * Otherwise, we let the base class deal with the meta. */
	if ((tags != NULL)
	 && (g_strv_length((gchar **)tags) == 1)
	 && gst_meta_api_type_has_tag(info->api, meta_tag_video_quark)
	)
		return TRUE;

	return GST_BASE_TRANSFORM_CLASS(gst_imx_v4l2_video_transform_parent_class)->transform_meta(trans, input_buffer, meta, output_buffer);
}


static gboolean gst_imx_v4l2_video_transform_copy_metadata(G_GNUC_UNUSED GstBaseTransform *trans, GstBuffer *input_buffer, GstBuffer *output_buffer)
{
	/* Copy PTS, DTS, duration, offset, offset-end
	 * These do not change in the videotransform operation */
	GST_BUFFER_DTS(output_buffer) = GST_BUFFER_DTS(input_buffer);
	GST_BUFFER_PTS(output_buffer) = GST_BUFFER_PTS(input_buffer);
	GST_BUFFER_DURATION(output_buffer) = GST_BUFFER_DURATION(input_buffer);
	GST_BUFFER_OFFSET(output_buffer) = GST_BUFFER_OFFSET(input_buffer);
	GST_BUFFER_OFFSET_END(output_buffer) = GST_BUFFER_OFFSET_END(input_buffer);

	/* Make sure the GST_BUFFER_FLAG_TAG_MEMORY flag isn't copied,
	 * otherwise the output buffer will be reallocated all the time */
	GST_BUFFER_FLAGS(output_buffer) = GST_BUFFER_FLAGS(input_buffer);
	GST_BUFFER_FLAG_UNSET(output_buffer, GST_BUFFER_FLAG_TAG_MEMORY);

	return TRUE;
}


static gboolean gst_imx_v4l2_video_transform_open(GstImxV4L2VideoTransform *self)
{
	gchar *device = NULL;
	gboolean ret = TRUE;

	GST_OBJECT_LOCK(self);
	device = g_strdup(self->device);
	GST_OBJECT_UNLOCK(self);

	self->imx_dma_buffer_allocator = gst_imx_ion_allocator_new();
	if (self->imx_dma_buffer_allocator == NULL)
	{
		GST_ERROR_OBJECT(self, "creating ION DMA buffer allocator failed");
		goto error;
	}

	self->v4l2_fd = open(self->device, O_RDWR);
	if (self->v4l2_fd < 0)
	{
		GST_ERROR_OBJECT(self, "could not open V4L2 device: %s (%d)", strerror(errno), errno);
		goto error;
	}

	if (!gst_imx_v4l2_video_transform_probe_available_caps(self, &(self->v4l2_output_queue)))
	{
		GST_ERROR_OBJECT(self, "could probe caps for V4L2 output queue");
		goto error;
	}
	if (!gst_imx_v4l2_video_transform_probe_available_caps(self, &(self->v4l2_capture_queue)))
	{
		GST_ERROR_OBJECT(self, "could probe caps for V4L2 capture queue");
		goto error;
	}

finish:
	g_free(device);
	return ret;

error:
	gst_imx_v4l2_video_transform_close(self);
	ret = FALSE;
	goto finish;
}


static void gst_imx_v4l2_video_transform_close(GstImxV4L2VideoTransform *self)
{
	gst_imx_v4l2_video_transform_teardown_v4l2_queue(self, &(self->v4l2_output_queue));
	gst_imx_v4l2_video_transform_teardown_v4l2_queue(self, &(self->v4l2_capture_queue));

	if (self->v4l2_fd > 0)
	{
		close(self->v4l2_fd);
		self->v4l2_fd = -1;
	}

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}

	if (self->input_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->input_buffer_pool));
		self->input_buffer_pool = NULL;
	}

	if (self->output_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->output_buffer_pool));
		self->output_buffer_pool = NULL;
	}
}


static gboolean gst_imx_v4l2_video_transform_probe_available_caps(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue)
{
	guint format_index;
	struct v4l2_fmtdesc format_desc;
	GValue format_gvalue = G_VALUE_INIT;
	GValue formats_gvalue = G_VALUE_INIT;
	gboolean ret = TRUE;
	GstStructure *structure;

	g_value_init(&format_gvalue, G_TYPE_STRING);
	g_value_init(&formats_gvalue, GST_TYPE_LIST);

	memset(&format_desc, 0, sizeof(format_desc));

	GST_DEBUG_OBJECT(self, "enumerating supported V4L2 mem2mem %s queue pixel formats", queue->name);

	for (format_index = 0; ; ++format_index)
	{
		GstImxV4L2VideoFormat const *v4l2_video_format;
		GstVideoFormat gst_format;

		format_desc.type = queue->buf_type;
		format_desc.index = format_index;

		if (ioctl(self->v4l2_fd, VIDIOC_ENUM_FMT, &format_desc) < 0)
		{
			if (errno == EINVAL)
			{
				GST_DEBUG_OBJECT(self, "reached the end of the list of supported formats");
				break;
			}
			else
			{
				GST_DEBUG_OBJECT(
					self,
					"error while enumerating supported %s query pixel format #%u: %s (%d)",
					queue->name,
					format_index,
					strerror(errno), errno
				);
				goto error;
			}
		}

		GST_DEBUG_OBJECT(
			self,
			"enumerated V4L2 format #%u: fourCC \"%" GST_FOURCC_FORMAT "\" \"%s\"",
			format_index,
			GST_FOURCC_ARGS(format_desc.pixelformat),
			format_desc.description
		);

		v4l2_video_format = gst_imx_v4l2_get_by_v4l2_pixelformat(format_desc.pixelformat);
		if (G_UNLIKELY((v4l2_video_format == NULL) || (v4l2_video_format->type != GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW)))
		{
			GST_DEBUG_OBJECT(self, "could not convert V4L2 pixelformat to anything we support; skipping");
			continue;
		}

		gst_format = v4l2_video_format->format.gst_format;
		g_value_set_string(&format_gvalue, gst_video_format_to_string(gst_format));
		gst_value_list_append_value(&formats_gvalue, &format_gvalue);
	}

	if (queue->available_caps != NULL)
		gst_caps_unref(queue->available_caps);

	structure = gst_structure_new(
		"video/x-raw",
		"width", GST_TYPE_INT_RANGE, (gint)1, G_MAXINT,
		"height", GST_TYPE_INT_RANGE, (gint)1, G_MAXINT,
		"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
		NULL
	);
	gst_structure_take_value(structure, "format", &formats_gvalue);

	queue->available_caps = gst_caps_new_full(structure, NULL);

	GST_DEBUG_OBJECT(
		self,
		"probed V4L2 %s queue caps: %" GST_PTR_FORMAT,
		queue->name,
		(gpointer)(queue->available_caps)
	);

finish:
	g_value_unset(&format_gvalue);
	return ret;

error:
	ret = FALSE;
	g_value_unset(&formats_gvalue);
	goto finish;
}


static gboolean gst_imx_v4l2_video_transform_setup_v4l2_queue(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue, GstVideoInfo const *video_info)
{
	gint buffer_index, plane_index, num_planes;
	struct v4l2_format v4l2_fmt;
	struct v4l2_control v4l2_ctrl;
	struct v4l2_requestbuffers v4l2_reqbuf;
	GstImxV4L2VideoFormat const *gst_imx_format;
	GstVideoFormat gst_format;

	g_assert(!queue->initialized);

	GST_DEBUG_OBJECT(self, "setting up V4L2 %s queue", queue->name);

	gst_format = GST_VIDEO_INFO_FORMAT(video_info);
	gst_imx_format = gst_imx_v4l2_get_by_gst_video_format(gst_format);
	if (G_UNLIKELY(gst_imx_format == NULL))
	{
		GST_ERROR_OBJECT(self, "cannot handle video format %s for V4L2 %s queue", gst_video_format_to_string(gst_format), queue->name);
		return FALSE;
	}
	g_assert(gst_imx_format->type == GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW);

	num_planes = (gint)(GST_VIDEO_INFO_N_PLANES(video_info));
	memcpy(&(queue->video_info), video_info, sizeof(GstVideoInfo));

	memset(&v4l2_fmt, 0, sizeof(v4l2_fmt));
	v4l2_fmt.type = queue->buf_type;
	v4l2_fmt.fmt.pix_mp.width = GST_VIDEO_INFO_WIDTH(video_info);
	v4l2_fmt.fmt.pix_mp.height = GST_VIDEO_INFO_HEIGHT(video_info);
	v4l2_fmt.fmt.pix_mp.pixelformat = gst_imx_format->v4l2_pixelformat;
	v4l2_fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	v4l2_fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	v4l2_fmt.fmt.pix_mp.flags = 0;
	v4l2_fmt.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	v4l2_fmt.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	v4l2_fmt.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	for (plane_index = 0; plane_index < num_planes; ++plane_index)
	{
		struct v4l2_plane_pix_format *plane_fmt = &(v4l2_fmt.fmt.pix_mp.plane_fmt[plane_index]);

		plane_fmt->bytesperline = GST_VIDEO_INFO_PLANE_STRIDE(video_info, plane_index);
		plane_fmt->sizeimage = plane_fmt->bytesperline * GST_VIDEO_INFO_HEIGHT(video_info);
	}

	if (ioctl(self->v4l2_fd, VIDIOC_S_FMT, &v4l2_fmt) < 0)
	{
		GST_ERROR_OBJECT(self, "could not set V4L2 pixel format for V4L2 %s queue: %s (%d)", queue->name, strerror(errno), errno);
		return FALSE;
	}

	GST_DEBUG_OBJECT(self, "configured format for V4L2 %s queue", queue->name);

	memset(&v4l2_ctrl, 0, sizeof(v4l2_ctrl));
	v4l2_ctrl.id = (queue->buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? V4L2_CID_MIN_BUFFERS_FOR_OUTPUT
                                                                        : V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
	if (ioctl(self->v4l2_fd, VIDIOC_G_CTRL, &v4l2_ctrl) < 0)
	{
		GST_ERROR_OBJECT(self, "could not query min required number of V4L2 %s buffers: %s (%d)", queue->name, strerror(errno), errno);
		return FALSE;
	}

	queue->min_num_required_buffers = v4l2_ctrl.value;
	g_assert(queue->min_num_required_buffers > 0);

	GST_DEBUG_OBJECT(self, "V4L2 %s queue requires a minimum of %d buffer(s)", queue->name, queue->min_num_required_buffers);

	memset(&v4l2_reqbuf, 0, sizeof(v4l2_reqbuf));
	v4l2_reqbuf.type = queue->buf_type;
	v4l2_reqbuf.memory = V4L2_MEMORY_DMABUF;
	v4l2_reqbuf.count = queue->min_num_required_buffers;
	if (ioctl(self->v4l2_fd, VIDIOC_REQBUFS, &v4l2_reqbuf) < 0)
	{
		GST_ERROR_OBJECT(self, "could not request %d V4L2 %s buffers: %s (%d)", queue->min_num_required_buffers, queue->name, strerror(errno), errno);
		return FALSE;
	}
	queue->num_buffers = v4l2_reqbuf.count;
	queue->num_queued_buffers = 0;

	GST_DEBUG_OBJECT(self, "actual number of requested %s buffers: %d", queue->name, queue->num_buffers);

	for (buffer_index = 0; buffer_index < queue->num_buffers; ++buffer_index)
	{
		struct v4l2_buffer buffer;
		struct v4l2_plane planes[VIDEO_MAX_PLANES];

		memset(&buffer, 0, sizeof(buffer));
		memset(planes, 0, sizeof(planes));

		buffer.index = buffer_index;
		buffer.type = queue->buf_type;
		buffer.length = num_planes;
		buffer.m.planes = planes;

		if (ioctl(self->v4l2_fd, VIDIOC_QUERYBUF, &buffer) < 0)
		{
			GST_ERROR_OBJECT(
				self,
				"could not query requested V4L2 %s buffer with index %d: %s (%d)",
				queue->name,
				buffer_index,
				strerror(errno), errno
			);
			return FALSE;
		}

		for (plane_index = 0; plane_index < num_planes; ++plane_index)
		{
			queue->driver_plane_sizes[plane_index] = planes[plane_index].length;
			GST_DEBUG_OBJECT(
				self,
				"driver query result: buffer with index %d has plane %d with size %" G_GSIZE_FORMAT,
				buffer_index,
				plane_index,
				queue->driver_plane_sizes[plane_index]
			);
		}
	}

	queue->unqueued_buffer_indices = g_malloc_n(queue->num_buffers, sizeof(gint));
	for (buffer_index = 0; buffer_index < queue->num_buffers; ++buffer_index)
		queue->unqueued_buffer_indices[buffer_index] = buffer_index;

	queue->queued_gstbuffers = g_malloc0_n(queue->num_buffers, sizeof(GstBuffer*));

	queue->initialized = TRUE;

	return TRUE;
}


static void gst_imx_v4l2_video_transform_teardown_v4l2_queue(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue)
{
	gint i;
	struct v4l2_requestbuffers v4l2_reqbuf;

	if (!queue->initialized)
		return;

	gst_imx_v4l2_video_transform_enable_stream(self, queue, FALSE);

	memset(&v4l2_reqbuf, 0, sizeof(v4l2_reqbuf));
	v4l2_reqbuf.type = queue->buf_type;
	v4l2_reqbuf.memory = V4L2_MEMORY_DMABUF;
	v4l2_reqbuf.count = 0;
	if (ioctl(self->v4l2_fd, VIDIOC_REQBUFS, &v4l2_reqbuf) < 0)
	{
		GST_ERROR_OBJECT(self, "error while deallocating V4L2 %s buffers: %s (%d)", queue->name, strerror(errno), errno);
	}

	for (i = 0; i < queue->num_buffers; ++i)
		gst_buffer_replace(&(queue->queued_gstbuffers[i]), NULL);
	g_free(queue->queued_gstbuffers);
	queue->queued_gstbuffers = NULL;

	g_free(queue->unqueued_buffer_indices);
	queue->unqueued_buffer_indices = NULL;
	queue->num_buffers = 0;

	queue->initialized = FALSE;
}


static gboolean gst_imx_v4l2_video_transform_queue_buffer(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue, GstBuffer *gstbuffer)
{
	gint buffer_index, i, num_planes, num_memory_blocks;
	struct v4l2_buffer buffer;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	GstVideoInfo const *video_info = &(queue->video_info);
	GstBufferPool *buffer_pool;

	if (queue->num_queued_buffers == queue->num_buffers)
	{
		GST_ERROR_OBJECT(self, "all %s buffers are already queued; cannot queue anything", queue->name);
		return FALSE;
	}

	buffer_pool = (queue->buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
	            ? self->input_buffer_pool
	            : self->output_buffer_pool;

	buffer_index = queue->unqueued_buffer_indices[queue->num_queued_buffers];

	memset(&buffer, 0, sizeof(buffer));
	memset(planes, 0, sizeof(planes));

	num_planes = GST_VIDEO_INFO_N_PLANES(video_info);
	num_memory_blocks = gst_buffer_n_memory(gstbuffer);

	GST_LOG_OBJECT(
		self,
		"queuing V4L2 %s buffer with index %d; multi-memory buffer: %d; GstBuffer: %" GST_PTR_FORMAT,
		queue->name,
		buffer_index,
		(num_memory_blocks != 1),
		(gpointer)gstbuffer
	);

	if (num_memory_blocks == 1)
	{
		GstMemory *memory = gst_buffer_peek_memory(gstbuffer, 0);
		g_assert(gst_is_dmabuf_memory(memory));

		int fd = gst_dmabuf_memory_get_fd(memory);

		for (i = 0; i < num_planes; ++i)
		{
			gsize plane_offset = gst_imx_video_dma_buffer_pool_get_plane_offset(buffer_pool, i);
			gsize plane_size = gst_imx_video_dma_buffer_pool_get_plane_size(buffer_pool, i);

			planes[i].data_offset = plane_offset;
			planes[i].length = gst_memory_get_sizes(memory, NULL, NULL);
			planes[i].bytesused = plane_size + plane_offset;
			planes[i].m.fd = fd;

			GST_LOG_OBJECT(
				self,
				"  plane %d:  offset %u  total length %u  bytesused %u  FD %d",
				i,
				(guint)(planes[i].data_offset),
				(guint)(planes[i].length),
				(guint)(planes[i].bytesused),
				planes[i].m.fd
			);
		}
	}
	else
	{
		g_assert(num_planes <= (gint)gst_buffer_n_memory(gstbuffer));

		for (i = 0; i < num_planes; ++i)
		{
			gsize maxsize;
			GstMemory *memory = gst_buffer_peek_memory(gstbuffer, i);
			g_assert(gst_is_dmabuf_memory(memory));

			planes[i].length = gst_memory_get_sizes(memory, NULL, &maxsize);
			planes[i].m.fd = gst_dmabuf_memory_get_fd(memory);

			GST_LOG_OBJECT(
				self,
				"  plane %d:  total length %u  FD %d  maxsize %" G_GSIZE_FORMAT,
				i,
				(guint)(planes[i].length),
				planes[i].m.fd,
				maxsize
			);
		}
	}

	buffer.index = buffer_index;
	buffer.memory = V4L2_MEMORY_DMABUF;
	buffer.type = queue->buf_type;
	buffer.flags = 0;
	buffer.field = V4L2_FIELD_NONE;
	buffer.length = num_planes;
	buffer.m.planes = planes;

	if (ioctl(self->v4l2_fd, VIDIOC_QBUF, &buffer) < 0)
	{
		GST_ERROR_OBJECT(self, "could not queue %s buffer with index %d: %s (%d)", queue->name, buffer_index, strerror(errno), errno);
		return FALSE;
	}

	queue->queued_gstbuffers[buffer_index] = gstbuffer;
	gst_buffer_ref(gstbuffer);

	queue->num_queued_buffers++;

	return TRUE;
}


static GstBuffer* gst_imx_v4l2_video_transform_dequeue_buffer(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue)
{
	GstBuffer *gstbuffer;
	gint buffer_index, num_planes;
	struct v4l2_buffer buffer;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];

	if (queue->num_queued_buffers == 0)
	{
		GST_ERROR_OBJECT(self, "no %s buffer is queued; cannot dequeue anything", queue->name);
		return FALSE;
	}

	memset(&buffer, 0, sizeof(buffer));
	memset(planes, 0, sizeof(planes));

	num_planes = GST_VIDEO_INFO_N_PLANES(&(queue->video_info));

	buffer.type = queue->buf_type;
	buffer.length = num_planes;
	buffer.m.planes = planes;

	if (ioctl(self->v4l2_fd, VIDIOC_DQBUF, &buffer) < 0)
	{
		GST_ERROR_OBJECT(self, "could not dequeue %s buffer: %s (%d)", queue->name, strerror(errno), errno);
		return FALSE;
	}

	buffer_index = buffer.index;
	g_assert(buffer_index < queue->num_buffers);

	GST_LOG_OBJECT(self, "dequeuing V4L2 %s buffer with index %d", queue->name, buffer_index);

	gstbuffer = queue->queued_gstbuffers[buffer_index];
	queue->queued_gstbuffers[buffer_index] = NULL;

	queue->num_queued_buffers--;
	queue->unqueued_buffer_indices[queue->num_queued_buffers] = buffer_index;

	return gstbuffer;
}


static gboolean gst_imx_v4l2_video_transform_enable_stream(GstImxV4L2VideoTransform *self, GstImxV4L2VideoTransformQueue *queue, gboolean do_enable)
{
	gchar const *stream_name;
	enum v4l2_buf_type type = queue->buf_type;

	switch (type)
	{
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			stream_name = "output (= encoded data)";
			break;

		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
			stream_name = "capture (= decoded data)";
			break;

		default:
			g_assert_not_reached();
			break;
	}

	if (queue->stream_enabled == do_enable)
		return TRUE;

	if (ioctl(self->v4l2_fd, do_enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type) < 0)
	{
		GST_ERROR_OBJECT(self, "could not %s %s stream: %s (%d)", (do_enable ? "enable" : "disable"), stream_name, strerror(errno), errno);
		return FALSE;
	}
	else
	{
		GST_DEBUG_OBJECT(self, "%s stream %s", stream_name, (do_enable ? "enabled" : "disabled"));
		queue->stream_enabled = do_enable;
		return TRUE;
	}
}
