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


#include <gst/gst.h>
#include <gst/video/video.h>

#include "video_transform.h"
#include "../common/phys_mem_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_blitter_video_transform_debug);
#define GST_CAT_DEFAULT imx_blitter_video_transform_debug


enum
{
	PROP_0,
	PROP_INPUT_CROP
};


#define DEFAULT_INPUT_CROP TRUE


G_DEFINE_ABSTRACT_TYPE(GstImxBlitterVideoTransform, gst_imx_blitter_video_transform, GST_TYPE_BASE_TRANSFORM)


/* general element operations */
static void gst_imx_blitter_video_transform_finalize(GObject *object);
static void gst_imx_blitter_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_blitter_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_blitter_video_transform_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_imx_blitter_video_transform_sink_event(GstBaseTransform *transform, GstEvent *event);
static gboolean gst_imx_blitter_video_transform_src_event(GstBaseTransform *transform, GstEvent *event);

/* caps handling */
static GstCaps* gst_imx_blitter_video_transform_transform_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static GstCaps* gst_imx_blitter_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static GstCaps* gst_imx_blitter_video_transform_fixate_size_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static void gst_imx_blitter_video_transform_fixate_format_caps(GstBaseTransform *transform, GstCaps *caps, GstCaps *othercaps);
static gboolean gst_imx_blitter_video_transform_set_caps(GstBaseTransform *transform, GstCaps *in, GstCaps *out);

/* allocator */
static gboolean gst_imx_blitter_video_transform_propose_allocation(GstBaseTransform *transform, GstQuery *decide_query, GstQuery *query);
static gboolean gst_imx_blitter_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query);

/* frame output */
static GstFlowReturn gst_imx_blitter_video_transform_prepare_output_buffer(GstBaseTransform *transform, GstBuffer *input, GstBuffer **outbuf);
static GstFlowReturn gst_imx_blitter_video_transform_transform_frame(GstBaseTransform *transform, GstBuffer *in, GstBuffer *out);
static gboolean gst_imx_blitter_video_transform_transform_size(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, gsize size, GstCaps *othercaps, gsize *othersize);

/* metadata and meta information */
static gboolean gst_imx_blitter_video_transform_transform_meta(GstBaseTransform *trans, GstBuffer *inbuf, GstMeta *meta, GstBuffer *outbuf);
static gboolean gst_imx_blitter_video_transform_get_unit_size(GstBaseTransform *transform, GstCaps *caps, gsize *size);
static gboolean gst_imx_blitter_video_transform_copy_metadata(GstBaseTransform *trans, GstBuffer *input, GstBuffer *outbuf);

/* misc */
static gboolean gst_imx_blitter_video_transform_acquire_blitter(GstImxBlitterVideoTransform *blitter_video_transform);




/* functions declared by G_DEFINE_ABSTRACT_TYPE */

void gst_imx_blitter_video_transform_class_init(GstImxBlitterVideoTransformClass *klass)
{
	GObjectClass *object_class;
	GstBaseTransformClass *base_transform_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_blitter_video_transform_debug, "imxblittervideotransform", 0, "Freescale i.MX blitter video transform base class");

	object_class = G_OBJECT_CLASS(klass);
	base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	element_class->change_state                 = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_change_state);
	object_class->finalize                      = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_finalize);
	object_class->set_property                  = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_set_property);
	object_class->get_property                  = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_get_property);
	base_transform_class->sink_event            = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_sink_event);
	base_transform_class->src_event             = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_src_event);
	base_transform_class->transform_caps        = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_transform_caps);
	base_transform_class->fixate_caps           = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_fixate_caps);
	base_transform_class->propose_allocation    = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_propose_allocation);
	base_transform_class->decide_allocation     = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_decide_allocation);
	base_transform_class->set_caps              = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_set_caps);
	base_transform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_prepare_output_buffer);
	base_transform_class->transform             = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_transform_frame);
	base_transform_class->transform_size        = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_transform_size);
	base_transform_class->transform_meta        = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_transform_meta);
	base_transform_class->get_unit_size         = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_get_unit_size);
	base_transform_class->copy_metadata         = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_transform_copy_metadata);

	base_transform_class->passthrough_on_same_caps = FALSE;

	klass->start = NULL;
	klass->stop = NULL;
	klass->are_video_infos_equal = NULL;
	klass->are_transforms_necessary = NULL;
	klass->create_blitter = NULL;

	g_object_class_install_property(
		object_class,
		PROP_INPUT_CROP,
		g_param_spec_boolean(
			"input-crop",
			"Input crop",
			"Whether or not to crop input frames based on their video crop metadata",
			DEFAULT_INPUT_CROP,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_imx_blitter_video_transform_init(GstImxBlitterVideoTransform *blitter_video_transform)
{
	GstBaseTransform *base_transform = GST_BASE_TRANSFORM(blitter_video_transform);

	blitter_video_transform->initialized = FALSE;

	blitter_video_transform->inout_info_equal = FALSE;
	blitter_video_transform->inout_info_set = FALSE;
	gst_video_info_init(&(blitter_video_transform->input_video_info));
	gst_video_info_init(&(blitter_video_transform->output_video_info));

	blitter_video_transform->input_crop = DEFAULT_INPUT_CROP;
	blitter_video_transform->last_frame_with_cropdata = FALSE;

	blitter_video_transform->blitter = NULL;

	g_mutex_init(&(blitter_video_transform->mutex));

	/* Set passthrough initially to FALSE ; passthrough will later be
	 * enabled/disabled on a per-frame basis */
	gst_base_transform_set_passthrough(base_transform, FALSE);
	gst_base_transform_set_qos_enabled(base_transform, TRUE);
	gst_base_transform_set_in_place(base_transform, FALSE);
}




/* general element operations */

static void gst_imx_blitter_video_transform_finalize(GObject *object)
{
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(object);

	g_mutex_clear(&(blitter_video_transform->mutex));

	G_OBJECT_CLASS(gst_imx_blitter_video_transform_parent_class)->finalize(object);
}


static void gst_imx_blitter_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_INPUT_CROP:
		{
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);
			blitter_video_transform->input_crop = g_value_get_boolean(value);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_blitter_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_INPUT_CROP:
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);
			g_value_set_boolean(value, blitter_video_transform->input_crop);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_blitter_video_transform_change_state(GstElement *element, GstStateChange transition)
{
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(element);
	GstImxBlitterVideoTransformClass *klass = GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(G_OBJECT_GET_CLASS(element));
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(blitter_video_transform != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);

			blitter_video_transform->initialized = TRUE;

			if ((klass->start != NULL) && !(klass->start(blitter_video_transform)))
			{
				GST_ERROR_OBJECT(blitter_video_transform, "start() failed");
				blitter_video_transform->initialized = FALSE;
				GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);
				return GST_STATE_CHANGE_FAILURE;
			}

			if (!gst_imx_blitter_video_transform_acquire_blitter(blitter_video_transform))
			{
				GST_ERROR_OBJECT(blitter_video_transform, "acquiring blitter failed");
				GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);
				return GST_STATE_CHANGE_FAILURE;
			}

			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);

			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_blitter_video_transform_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_PAUSED_TO_READY:
		{
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);
			blitter_video_transform->last_frame_with_cropdata = FALSE;
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);
			break;
		}

		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);

			blitter_video_transform->initialized = FALSE;

			if ((klass->stop != NULL) && !(klass->stop(blitter_video_transform)))
				GST_ERROR_OBJECT(blitter_video_transform, "stop() failed");

			if (blitter_video_transform->blitter != NULL)
			{
				gst_object_unref(GST_OBJECT(blitter_video_transform->blitter));
				blitter_video_transform->blitter = NULL;
			}

			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);

			break;
		}

		default:
			break;
	}

	return ret;
}


static gboolean gst_imx_blitter_video_transform_sink_event(GstBaseTransform *transform, GstEvent *event)
{
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(transform);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_FLUSH_STOP:
		{
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);
			if (blitter_video_transform->blitter != NULL)
				gst_imx_blitter_flush(blitter_video_transform->blitter);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);

			break;
		}

		default:
			break;
	}

	return GST_BASE_TRANSFORM_CLASS(gst_imx_blitter_video_transform_parent_class)->sink_event(transform, event);
}


static gboolean gst_imx_blitter_video_transform_src_event(GstBaseTransform *transform, GstEvent *event)
{
	gdouble a;
	GstStructure *structure;
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(transform);

	GST_DEBUG_OBJECT(transform, "handling %s event", GST_EVENT_TYPE_NAME(event));

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_NAVIGATION:
		{
			/* scale pointer_x/y values in the event if in- and output have different width/height */

			gint in_w = GST_VIDEO_INFO_WIDTH(&(blitter_video_transform->input_video_info));
			gint in_h = GST_VIDEO_INFO_HEIGHT(&(blitter_video_transform->input_video_info));
			gint out_w = GST_VIDEO_INFO_WIDTH(&(blitter_video_transform->output_video_info));
			gint out_h = GST_VIDEO_INFO_HEIGHT(&(blitter_video_transform->output_video_info));
			if ((in_w != out_w) || (in_h != out_h))
			{
				event = GST_EVENT(gst_mini_object_make_writable(GST_MINI_OBJECT(event)));

				structure = (GstStructure *)gst_event_get_structure(event);
				if (gst_structure_get_double(structure, "pointer_x", &a))
				{
					gst_structure_set(
						structure,
						"pointer_x",
						G_TYPE_DOUBLE,
						a * in_w / out_w,
						NULL
					);
				}
				if (gst_structure_get_double(structure, "pointer_y", &a))
				{
					gst_structure_set(
						structure,
						"pointer_y",
						G_TYPE_DOUBLE,
						a * in_h / out_h,
						NULL
					);
				}
			}
			break;
		}

		default:
			break;
	}

	return GST_BASE_TRANSFORM_CLASS(gst_imx_blitter_video_transform_parent_class)->src_event(transform, event);
}




/* caps handling */

static GstCaps* gst_imx_blitter_video_transform_transform_caps(GstBaseTransform *transform, G_GNUC_UNUSED GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
	GstCaps *tmpcaps1, *tmpcaps2, *result;
	GstStructure *structure;
	gint i, n;

	tmpcaps1 = gst_caps_new_empty();
	n = gst_caps_get_size(caps);
	for (i = 0; i < n; i++)
	{
		structure = gst_caps_get_structure(caps, i);

		/* If this is already expressed by the existing caps
		 * skip this structure */
		if ((i > 0) && gst_caps_is_subset_structure(tmpcaps1, structure))
			continue;

		/* make copy */
		structure = gst_structure_copy(structure);
		gst_structure_set(
			structure,
			"width", GST_TYPE_INT_RANGE, 64, G_MAXINT,
			"height", GST_TYPE_INT_RANGE, 64, G_MAXINT,
			NULL
		);

		/* colorimetry is not supported by the videotransform element */
		gst_structure_remove_fields(structure, "format", "colorimetry", "chroma-site", NULL);

		/* if pixel aspect ratio, make a range of it */
		if (gst_structure_has_field(structure, "pixel-aspect-ratio"))
		{
			gst_structure_set(
				structure,
				"pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
				NULL
			);
		}
		gst_caps_append_structure(tmpcaps1, structure);
	}

	/* filter the resulting caps if necessary */
	if (filter != NULL)
	{
		tmpcaps2 = gst_caps_intersect_full(filter, tmpcaps1, GST_CAPS_INTERSECT_FIRST);
		gst_caps_unref(tmpcaps1);
		tmpcaps1 = tmpcaps2;
	}

	result = tmpcaps1;

	GST_DEBUG_OBJECT(transform, "transformed %" GST_PTR_FORMAT " into %" GST_PTR_FORMAT, (gpointer)caps, (gpointer)result);

	return result;
}


/* NOTE: these following functions are taken almost 1:1 from the upstream videoconvert element:
 * gst_imx_blitter_video_transform_fixate_caps
 * gst_imx_blitter_video_transform_fixate_size_caps
 * score_value
 * gst_imx_blitter_video_transform_fixate_format_caps
 * */


static GstCaps* gst_imx_blitter_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
	othercaps = gst_caps_truncate(othercaps);
	othercaps = gst_caps_make_writable(othercaps);

	GST_DEBUG_OBJECT(transform, "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, (gpointer)othercaps, (gpointer)caps);

	othercaps = gst_imx_blitter_video_transform_fixate_size_caps(transform, direction, caps, othercaps);
	gst_imx_blitter_video_transform_fixate_format_caps(transform, caps, othercaps);

	return othercaps;
}


static GstCaps* gst_imx_blitter_video_transform_fixate_size_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
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
	gst_imx_blitter_video_transform_fixate_format_caps(transform, caps, othercaps);

	GST_DEBUG_OBJECT(transform, "fixated othercaps to %" GST_PTR_FORMAT, (gpointer)othercaps);

	if (from_par == &fpar)
		g_value_unset(&fpar);
	if (to_par == &tpar)
		g_value_unset(&tpar);

	return othercaps;
}


#define SCORE_PALETTE_LOSS        1
#define SCORE_COLOR_LOSS          2
#define SCORE_ALPHA_LOSS          4
#define SCORE_CHROMA_W_LOSS       8
#define SCORE_CHROMA_H_LOSS      16
#define SCORE_DEPTH_LOSS         32

#define COLOR_MASK   (GST_VIDEO_FORMAT_FLAG_YUV | \
                      GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK   (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK (GST_VIDEO_FORMAT_FLAG_PALETTE)


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

	loss = 1;

	in_flags = GST_VIDEO_FORMAT_INFO_FLAGS(in_info);
	in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
	in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
	in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

	t_flags = GST_VIDEO_FORMAT_INFO_FLAGS(t_info);
	t_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
	t_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
	t_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

	if ((t_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK))
		loss += SCORE_PALETTE_LOSS;

	if ((t_flags & COLOR_MASK) != (in_flags & COLOR_MASK))
		loss += SCORE_COLOR_LOSS;

	if ((t_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK))
		loss += SCORE_ALPHA_LOSS;

	if ((in_info->h_sub[1]) < (t_info->h_sub[1]))
		loss += SCORE_CHROMA_H_LOSS;
	if ((in_info->w_sub[1]) < (t_info->w_sub[1]))
		loss += SCORE_CHROMA_W_LOSS;

	if ((in_info->bits) > (t_info->bits))
		loss += SCORE_DEPTH_LOSS;

	GST_DEBUG_OBJECT(base, "score %s -> %s = %d",
			GST_VIDEO_FORMAT_INFO_NAME(in_info),
			GST_VIDEO_FORMAT_INFO_NAME(t_info), loss);

	if (loss < *min_loss)
	{
		GST_DEBUG_OBJECT(base, "found new best %d", loss);
		*out_info = t_info;
		*min_loss = loss;
	}
}


static void gst_imx_blitter_video_transform_fixate_format_caps(GstBaseTransform *transform, GstCaps *caps, GstCaps *othercaps)
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


static gboolean gst_imx_blitter_video_transform_set_caps(GstBaseTransform *transform, GstCaps *in, GstCaps *out)
{
	gboolean inout_info_equal;
	GstVideoInfo in_info, out_info;
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(transform);
	GstImxBlitterVideoTransformClass *klass = GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(G_OBJECT_GET_CLASS(transform));
	GstImxCanvas *canvas = &(blitter_video_transform->canvas);
	GstImxRegion source_subset;

	g_assert(klass->are_video_infos_equal != NULL);
	g_assert(blitter_video_transform->blitter != NULL);

	if (!gst_video_info_from_caps(&in_info, in) || !gst_video_info_from_caps(&out_info, out))
	{
		GST_ERROR_OBJECT(transform, "caps are invalid");
		blitter_video_transform->inout_info_set = FALSE;
		return FALSE;
	}

	inout_info_equal = klass->are_video_infos_equal(blitter_video_transform, &in_info, &out_info);

	if (inout_info_equal)
		GST_DEBUG_OBJECT(transform, "input and output caps are equal");
	else
		GST_DEBUG_OBJECT(transform, "input and output caps are not equal:  input: %" GST_PTR_FORMAT "  output: %" GST_PTR_FORMAT, (gpointer)in, (gpointer)out);

	gst_imx_blitter_set_input_video_info(blitter_video_transform->blitter, &in_info);
	gst_imx_blitter_set_output_video_info(blitter_video_transform->blitter, &out_info);

	/* setting new caps changes the canvas, so recalculate it
	 * the recalculation here is done without any input cropping, so set
	 * last_frame_with_cropdata to FALSE, in case subsequent frames do
	 * contain crop metadata */

	blitter_video_transform->last_frame_with_cropdata = FALSE;

	/* the canvas always encompasses the entire output frame */
	canvas->outer_region.x1 = 0;
	canvas->outer_region.y1 = 0;
	canvas->outer_region.x2 = GST_VIDEO_INFO_WIDTH(&out_info);
	canvas->outer_region.y2 = GST_VIDEO_INFO_HEIGHT(&out_info);

	gst_imx_canvas_calculate_inner_region(canvas, &in_info);
	gst_imx_canvas_clip(canvas, &(canvas->outer_region), &in_info, NULL, &source_subset);

	gst_imx_blitter_set_input_region(blitter_video_transform->blitter, &source_subset);
	gst_imx_blitter_set_output_canvas(blitter_video_transform->blitter, canvas);

	blitter_video_transform->input_video_info = in_info;
	blitter_video_transform->output_video_info = out_info;
	blitter_video_transform->inout_info_equal = inout_info_equal;
	blitter_video_transform->inout_info_set = TRUE;

	return TRUE;
}




/* allocator */

static gboolean gst_imx_blitter_video_transform_propose_allocation(GstBaseTransform *transform, G_GNUC_UNUSED GstQuery *decide_query, GstQuery *query)
{
	return gst_pad_peer_query(GST_BASE_TRANSFORM_SRC_PAD(transform), query);
}


static gboolean gst_imx_blitter_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query)
{
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(transform);
	GstCaps *outcaps;
	GstBufferPool *pool = NULL;
	guint size, min = 0, max = 0;
	GstStructure *config;
	GstVideoInfo vinfo;
	gboolean update_pool;

	g_assert(blitter_video_transform->blitter != NULL);

	gst_query_parse_allocation(query, &outcaps, NULL);
	gst_video_info_init(&vinfo);
	gst_video_info_from_caps(&vinfo, outcaps);

	GST_DEBUG_OBJECT(blitter_video_transform, "num allocation pools: %d", gst_query_get_n_allocation_pools(query));

	/* Look for an allocator which can allocate physical memory buffers */
	if (gst_query_get_n_allocation_pools(query) > 0)
	{
		for (guint i = 0; i < gst_query_get_n_allocation_pools(query); ++i)
		{
			gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min, &max);
			if (gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM))
				break;
		}

		size = MAX(size, vinfo.size);
		update_pool = TRUE;
	}
	else
	{
		pool = NULL;
		size = vinfo.size;
		min = max = 0;
		update_pool = FALSE;
	}

	/* Either no pool or no pool with the ability to allocate physical memory buffers
	 * has been found -> create a new pool */
	if ((pool == NULL) || !gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM))
	{
		if (pool == NULL)
			GST_DEBUG_OBJECT(blitter_video_transform, "no pool present; creating new pool");
		else
			GST_DEBUG_OBJECT(blitter_video_transform, "no pool supports physical memory buffers; creating new pool");
		pool = gst_imx_blitter_create_bufferpool(blitter_video_transform->blitter, outcaps, size, min, max, NULL, NULL);
	}
	else
	{
		config = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_set_params(config, outcaps, size, min, max);
		gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
		gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
		gst_buffer_pool_set_config(pool, config);
	}

	GST_DEBUG_OBJECT(
		blitter_video_transform,
		"pool config:  outcaps: %" GST_PTR_FORMAT "  size: %u  min buffers: %u  max buffers: %u",
		(gpointer)outcaps,
		size,
		min,
		max
	);

	if (update_pool)
		gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
	else
		gst_query_add_allocation_pool(query, pool, size, min, max);

	if (pool != NULL)
		gst_object_unref(pool);

	return TRUE;
}




/* frame output */

static GstFlowReturn gst_imx_blitter_video_transform_prepare_output_buffer(GstBaseTransform *transform, GstBuffer *input, GstBuffer **outbuf)
{
	gboolean passthrough;
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(transform);
	GstImxBlitterVideoTransformClass *klass = GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(G_OBJECT_GET_CLASS(transform));
	GstVideoCropMeta *video_crop_meta;
	gboolean update_canvas = FALSE;

	/* If either there is no input buffer or in- and output info are not equal,
	 * it is clear there can be no passthrough mode */
	passthrough = (input != NULL) && blitter_video_transform->inout_info_equal;

	GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);

	/* Check if cropping needs to be done */
	if ((input != NULL) && blitter_video_transform->input_crop && ((video_crop_meta = gst_buffer_get_video_crop_meta(input)) != NULL))
	{
		GstImxRegion source_region;
		gint in_width, in_height;

		source_region.x1 = video_crop_meta->x;
		source_region.y1 = video_crop_meta->y;
		source_region.x2 = video_crop_meta->x + video_crop_meta->width;
		source_region.y2 = video_crop_meta->y + video_crop_meta->height;

		in_width = GST_VIDEO_INFO_WIDTH(&(blitter_video_transform->input_video_info));
		in_height = GST_VIDEO_INFO_HEIGHT(&(blitter_video_transform->input_video_info));

		/* Make sure the source region does not exceed valid bounds */
		source_region.x1 = MAX(0, source_region.x1);
		source_region.y1 = MAX(0, source_region.y1);
		source_region.x2 = MIN(in_width, source_region.x2);
		source_region.y2 = MIN(in_height, source_region.y2);

		/* If the crop rectangle encompasses the entire frame, cropping is
		 * effectively a no-op, so make it passthrough in that case,
		 * unless passthrough is already FALSE */
		passthrough = passthrough && (source_region.x1 == 0) && (source_region.y1 == 0) && (source_region.x2 == in_width) && (source_region.y2 == in_height);

		GST_LOG_OBJECT(blitter_video_transform, "retrieved crop rectangle %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&source_region));

		/* Canvas needs to be updated if either one of these applies:
		 * - the current frame has crop metadata, the last one didn't
		 * - the new crop rectangle and the last are different */
		if (!(blitter_video_transform->last_frame_with_cropdata) || !gst_imx_region_equal(&source_region, &(blitter_video_transform->last_source_region)))
		{
			GST_LOG_OBJECT(blitter_video_transform, "using new crop rectangle %" GST_IMX_REGION_FORMAT, GST_IMX_REGION_ARGS(&source_region));
			blitter_video_transform->last_source_region = source_region;
			update_canvas = TRUE;
		}

		blitter_video_transform->last_frame_with_cropdata = TRUE;
	}
	else
	{
		/* Force a canvas update if this frame has no crop metadata but the last one did */
		if (blitter_video_transform->last_frame_with_cropdata)
			update_canvas = TRUE;
		blitter_video_transform->last_frame_with_cropdata = FALSE;
	}

	if (update_canvas)
	{
		GstImxRegion source_subset;
		GstImxCanvas *canvas = &(blitter_video_transform->canvas);

		gst_imx_canvas_clip(
			canvas,
			&(canvas->outer_region),
			&(blitter_video_transform->input_video_info),
			blitter_video_transform->last_frame_with_cropdata ? &(blitter_video_transform->last_source_region) : NULL,
			&source_subset
		);

		gst_imx_blitter_set_input_region(blitter_video_transform->blitter, &source_subset);
		gst_imx_blitter_set_output_canvas(blitter_video_transform->blitter, canvas);
	}

	if ((input != NULL) && passthrough)
	{
		/* test for additional special cases for passthrough must not be enabled
		 * such case are transforms like rotation, deinterlacing ... */
		passthrough = passthrough && (blitter_video_transform->canvas.inner_rotation == GST_IMX_CANVAS_INNER_ROTATION_NONE) &&
		              (klass->are_transforms_necessary != NULL) &&
		              !(klass->are_transforms_necessary(blitter_video_transform, input));
	}
	else if (!blitter_video_transform->inout_info_equal)
		GST_LOG_OBJECT(transform, "input and output caps are not equal");
	else if (blitter_video_transform->last_frame_with_cropdata && !passthrough)
		GST_LOG_OBJECT(transform, "cropping is performed");
	else if (input == NULL)
		GST_LOG_OBJECT(transform, "input buffer is NULL");

	GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);

	GST_LOG_OBJECT(transform, "passthrough: %s", passthrough ? "yes" : "no");

	if (passthrough)
	{
		/* This instructs the base class to not allocate a new buffer for
		 * the output, and instead pass the input buffer as the output
		 * (this is used in the fransform_frame function below) */
		*outbuf = input;
		return GST_FLOW_OK;
	}
	else
		return GST_BASE_TRANSFORM_CLASS(gst_imx_blitter_video_transform_parent_class)->prepare_output_buffer(transform, input, outbuf);
}


static GstFlowReturn gst_imx_blitter_video_transform_transform_frame(GstBaseTransform *transform, GstBuffer *in, GstBuffer *out)
{
	GstImxBlitterVideoTransform *blitter_video_transform = GST_IMX_BLITTER_VIDEO_TRANSFORM(transform);

	g_assert(blitter_video_transform->blitter != NULL);

	if (!blitter_video_transform->inout_info_set)
	{
		GST_ELEMENT_ERROR(transform, CORE, NOT_IMPLEMENTED, (NULL), ("unknown format"));
		return GST_FLOW_NOT_NEGOTIATED;
	}

	if (in == out)
	{
		GST_LOG_OBJECT(transform, "passing buffer through");
		return GST_FLOW_OK;
	}

	GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(blitter_video_transform);

	gst_imx_blitter_set_input_frame(blitter_video_transform->blitter, in);
	gst_imx_blitter_set_output_frame(blitter_video_transform->blitter, out);
	gst_imx_blitter_blit(blitter_video_transform->blitter, 255);
	gst_imx_blitter_set_output_frame(blitter_video_transform->blitter, NULL);

	GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(blitter_video_transform);

	return GST_FLOW_OK;
}


static gboolean gst_imx_blitter_video_transform_transform_size(G_GNUC_UNUSED GstBaseTransform *transform, G_GNUC_UNUSED GstPadDirection direction, G_GNUC_UNUSED GstCaps *caps, gsize size, GstCaps *othercaps, gsize *othersize)
{
	gboolean ret = TRUE;
	GstVideoInfo info;

	g_assert(size != 0);

	ret = gst_video_info_from_caps(&info, othercaps);
	if (ret)
		*othersize = info.size;

	return ret;
}




/* metadata and meta information */

static gboolean gst_imx_blitter_video_transform_transform_meta(GstBaseTransform *trans, GstBuffer *inbuf, GstMeta *meta, GstBuffer *outbuf)
{
	GstMetaInfo const *info = meta->info;
	gchar const * const *tags;

	tags = gst_meta_api_type_get_tags(info->api);

	if (
		(tags != NULL) &&
		(g_strv_length((gchar **)tags) == 1) &&
		gst_meta_api_type_has_tag(info->api, g_quark_from_string(GST_META_TAG_VIDEO_STR))
	)
		return TRUE;

	return GST_BASE_TRANSFORM_CLASS(gst_imx_blitter_video_transform_parent_class)->transform_meta(trans, inbuf, meta, outbuf);
}


static gboolean gst_imx_blitter_video_transform_get_unit_size(GstBaseTransform *transform, GstCaps *caps, gsize *size)
{
	GstVideoInfo info;

	if (!gst_video_info_from_caps(&info, caps))
	{
		GST_WARNING_OBJECT(transform, "Failed to parse caps %" GST_PTR_FORMAT, caps);
		return FALSE;
	}

	*size = info.size;

	GST_DEBUG_OBJECT(transform, "Returning size %" G_GSIZE_FORMAT " bytes for caps %" GST_PTR_FORMAT, *size, caps);

	return TRUE;
}


static gboolean gst_imx_blitter_video_transform_copy_metadata(G_GNUC_UNUSED GstBaseTransform *trans, GstBuffer *input, GstBuffer *outbuf)
{
	/* Copy PTS, DTS, duration, offset, offset-end
	 * These do not change in the videotransform operation */
	GST_BUFFER_DTS(outbuf) = GST_BUFFER_DTS(input);
	GST_BUFFER_PTS(outbuf) = GST_BUFFER_PTS(input);
	GST_BUFFER_DURATION(outbuf) = GST_BUFFER_DURATION(input);
	GST_BUFFER_OFFSET(outbuf) = GST_BUFFER_OFFSET(input);
	GST_BUFFER_OFFSET_END(outbuf) = GST_BUFFER_OFFSET_END(input);

	/* For GStreamer 1.3.1 and newer, make sure the GST_BUFFER_FLAG_TAG_MEMORY flag
	 * isn't copied, otherwise the output buffer will be reallocated all the time */
	GST_BUFFER_FLAGS(outbuf) = GST_BUFFER_FLAGS(input);
#if GST_CHECK_VERSION(1, 3, 1)
	GST_BUFFER_FLAG_UNSET(outbuf, GST_BUFFER_FLAG_TAG_MEMORY);
#endif

	return TRUE;
}




static gboolean gst_imx_blitter_video_transform_acquire_blitter(GstImxBlitterVideoTransform *blitter_video_transform)
{
	/* must be called with lock held */

	GstImxBlitterVideoTransformClass *klass = GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(G_OBJECT_GET_CLASS(blitter_video_transform));

	g_assert(blitter_video_transform != NULL);
	g_assert(klass->create_blitter != NULL);

	/* Do nothing if the blitter is already acquired */
	if (blitter_video_transform->blitter != NULL)
		return TRUE;

	if ((blitter_video_transform->blitter = klass->create_blitter(blitter_video_transform)) == NULL)
	{
		GST_ERROR_OBJECT(blitter_video_transform, "could not acquire blitter");
		return FALSE;
	}

	return TRUE;
}
