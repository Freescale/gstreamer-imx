/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2020  Carlos Rafael Giani
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
#include "gst/imx/common/gstimxdefaultallocator.h"
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimx2dvideotransform.h"
#include "gstimx2dmisc.h"


GST_DEBUG_CATEGORY_STATIC(imx_2d_video_transform_debug);
#define GST_CAT_DEFAULT imx_2d_video_transform_debug


enum
{
	PROP_0,
	PROP_INPUT_CROP,
	PROP_OUTPUT_ROTATION,
	PROP_OUTPUT_FLIP_MODE
};


#define DEFAULT_INPUT_CROP TRUE
#define DEFAULT_OUTPUT_ROTATION IMX_2D_ROTATION_NONE
#define DEFAULT_OUTPUT_FLIP_MODE IMX_2D_FLIP_MODE_NONE


/* Cached quark to avoid contention on the global quark table lock */
static GQuark meta_tag_video_quark;


G_DEFINE_ABSTRACT_TYPE(GstImx2dVideoTransform, gst_imx_2d_video_transform, GST_TYPE_BASE_TRANSFORM)


/* Base class function overloads. */

/* General element operations. */
static void gst_imx_2d_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_2d_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_2d_video_transform_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_imx_2d_video_transform_src_event(GstBaseTransform *transform, GstEvent *event);

/* Caps handling. */
static GstCaps* gst_imx_2d_video_transform_transform_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static GstCaps* gst_imx_2d_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static GstCaps* gst_imx_2d_video_transform_fixate_size_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static void gst_imx_2d_video_transform_fixate_format_caps(GstBaseTransform *transform, GstCaps *caps, GstCaps *othercaps);
static gboolean gst_imx_2d_video_transform_set_caps(GstBaseTransform *transform, GstCaps *input_caps, GstCaps *output_caps);

/* Allocator. */
static gboolean gst_imx_2d_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query);

/* Frame output. */
static GstFlowReturn gst_imx_2d_video_transform_prepare_output_buffer(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer **output_buffer);
static GstFlowReturn gst_imx_2d_video_transform_transform_frame(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer *output_buffer);
static gboolean gst_imx_2d_video_transform_transform_size(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, gsize size, GstCaps *othercaps, gsize *othersize);

/* Metadata and meta information. */
static gboolean gst_imx_2d_video_transform_transform_meta(GstBaseTransform *trans, GstBuffer *input_buffer, GstMeta *meta, GstBuffer *output_buffer);
static gboolean gst_imx_2d_video_transform_copy_metadata(GstBaseTransform *trans, GstBuffer *input_buffer, GstBuffer *output_buffer);


/* GstImx2dVideoTransform specific functions. */

static gboolean gst_imx_2d_video_transform_start(GstImx2dVideoTransform *self);
static void gst_imx_2d_video_transform_stop(GstImx2dVideoTransform *self);
static gboolean gst_imx_2d_video_transform_create_blitter(GstImx2dVideoTransform *self);




static void gst_imx_2d_video_transform_class_init(GstImx2dVideoTransformClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstBaseTransformClass *base_transform_class;

	GST_DEBUG_CATEGORY_INIT(imx_2d_video_transform_debug, "imx2dvideotransform", 0, "NXP i.MX 2D video transform");

	meta_tag_video_quark = g_quark_from_static_string(GST_META_TAG_VIDEO_STR);

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

	object_class->set_property                  = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_set_property);
	object_class->get_property                  = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_get_property);

	element_class->change_state                 = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_change_state);

	base_transform_class->src_event             = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_src_event);
	base_transform_class->transform_caps        = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_transform_caps);
	base_transform_class->fixate_caps           = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_fixate_caps);
	base_transform_class->set_caps              = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_set_caps);
	base_transform_class->decide_allocation     = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_decide_allocation);
	base_transform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_prepare_output_buffer);
	base_transform_class->transform             = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_transform_frame);
	base_transform_class->transform_size        = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_transform_size);
	base_transform_class->transform_meta        = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_transform_meta);
	base_transform_class->copy_metadata         = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_copy_metadata);

	/* We may have to process frames even if the caps are the same.
	 * This is because transformations like rotation produce frames
	 * with the same caps. */
	base_transform_class->passthrough_on_same_caps = FALSE;

	klass->start = NULL;
	klass->stop = NULL;
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
	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_ROTATION,
		g_param_spec_enum(
			"output-rotation",
			"Output rotation",
			"Output rotation in 90-degree steps",
			gst_imx_2d_rotation_get_type(),
			DEFAULT_OUTPUT_ROTATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_FLIP_MODE,
		g_param_spec_enum(
			"output-flip-mode",
			"Output flip mode",
			"Output flip mode",
			gst_imx_2d_flip_mode_get_type(),
			DEFAULT_OUTPUT_FLIP_MODE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_imx_2d_video_transform_init(GstImx2dVideoTransform *self)
{
	GstBaseTransform *base_transform = GST_BASE_TRANSFORM(self);

	self->blitter = NULL;

	self->inout_info_equal = FALSE;
	self->inout_info_set = FALSE;

	gst_video_info_init(&(self->input_video_info));
	gst_video_info_init(&(self->output_video_info));

	self->input_caps = NULL;

	self->input_surface = NULL;
	self->output_surface = NULL;

	self->input_crop = DEFAULT_INPUT_CROP;
	self->output_rotation = DEFAULT_OUTPUT_ROTATION;
	self->output_flip_mode = DEFAULT_OUTPUT_FLIP_MODE;

	/* Set passthrough initially to FALSE. Passthrough will
	 * be enabled/disabled on a per-frame basis in
	 * gst_imx_2d_video_transform_prepare_output_buffer(). */
	gst_base_transform_set_passthrough(base_transform, FALSE);
	gst_base_transform_set_qos_enabled(base_transform, TRUE);
	gst_base_transform_set_in_place(base_transform, FALSE);
}


static void gst_imx_2d_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_INPUT_CROP:
		{
			GST_OBJECT_LOCK(self);
			self->input_crop = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_OUTPUT_ROTATION:
		{
			GST_OBJECT_LOCK(self);
			self->output_rotation = g_value_get_enum(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_OUTPUT_FLIP_MODE:
		{
			GST_OBJECT_LOCK(self);
			self->output_flip_mode = g_value_get_enum(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_2d_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_INPUT_CROP:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->input_crop);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_OUTPUT_ROTATION:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_enum(value, self->output_rotation);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_OUTPUT_FLIP_MODE:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_enum(value, self->output_flip_mode);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_2d_video_transform_change_state(GstElement *element, GstStateChange transition)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(self != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if (!gst_imx_2d_video_transform_start(self))
				return GST_STATE_CHANGE_FAILURE;
			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_2d_video_transform_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
			gst_imx_2d_video_transform_stop(self);
			break;

		default:
			break;
	}

	return ret;
}


static gboolean gst_imx_2d_video_transform_src_event(GstBaseTransform *transform, GstEvent *event)
{
	gdouble a;
	GstStructure *structure;
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_NAVIGATION:
		{
			/* Scale pointer_x/y values in the event if
			 * in- and output have different width/height. */

			gint in_w = GST_VIDEO_INFO_WIDTH(&(self->input_video_info));
			gint in_h = GST_VIDEO_INFO_HEIGHT(&(self->input_video_info));
			gint out_w = GST_VIDEO_INFO_WIDTH(&(self->output_video_info));
			gint out_h = GST_VIDEO_INFO_HEIGHT(&(self->output_video_info));

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

	return GST_BASE_TRANSFORM_CLASS(gst_imx_2d_video_transform_parent_class)->src_event(transform, event);
}


static GstCaps* gst_imx_2d_video_transform_transform_caps(GstBaseTransform *transform, G_GNUC_UNUSED GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
	GstCaps *tmpcaps, *result;
	GstStructure *structure;
	gint i, n;

	/* Process each structure from the caps, copy them, and modify them if necessary. */
	tmpcaps = gst_caps_new_empty();
	n = gst_caps_get_size(caps);
	for (i = 0; i < n; i++)
	{
		structure = gst_caps_get_structure(caps, i);

		/* If this is already expressed by the existing caps, skip this structure. */
		if ((i > 0) && gst_caps_is_subset_structure(tmpcaps, structure))
			continue;

		/* Make the copy. */
		structure = gst_structure_copy(structure);

		/* Since the blitter can perform scaling, don't restrict width / height. */
		gst_structure_set(
			structure,
			"width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
			"height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
			NULL
		);

		/* Colorimetry is not supported by the videotransform element. */
		gst_structure_remove_fields(structure, "format", "colorimetry", "chroma-site", NULL);

		/* If there is a pixel aspect ratio in the structure, turn that field into
		 * a range, since this element does not restrict the pixel aspect ratio to
		 * any specific values. */
		if (gst_structure_has_field(structure, "pixel-aspect-ratio"))
		{
			gst_structure_set(
				structure,
				"pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1,
				NULL
			);
		}

		gst_caps_append_structure(tmpcaps, structure);
	}

	if (filter != NULL)
	{
		GstCaps *unfiltered_result = tmpcaps;
		result = gst_caps_intersect_full(unfiltered_result, filter, GST_CAPS_INTERSECT_FIRST);
		GST_DEBUG(
			"applied filter %" GST_PTR_FORMAT "; resulting transformed and filtered caps: %" GST_PTR_FORMAT,
			(gpointer)filter,
			(gpointer)result
		);
		gst_caps_unref(unfiltered_result);
	}
	else
		result = tmpcaps;

	GST_DEBUG_OBJECT(transform, "transformed caps %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, (gpointer)caps, (gpointer)result);

	return result;
}


/* NOTE: The following functions are taken almost 1:1 from the upstream videoconvert element:
 * gst_imx_2d_video_transform_fixate_caps
 * gst_imx_2d_video_transform_fixate_size_caps
 * score_value
 * gst_imx_2d_video_transform_fixate_format_caps
 * */


static GstCaps* gst_imx_2d_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
	GST_DEBUG_OBJECT(transform, "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, (gpointer)othercaps, (gpointer)caps);

	othercaps = gst_caps_truncate(othercaps);
	othercaps = gst_caps_make_writable(othercaps);

	othercaps = gst_imx_2d_video_transform_fixate_size_caps(transform, direction, caps, othercaps);
	gst_imx_2d_video_transform_fixate_format_caps(transform, caps, othercaps);

	return othercaps;
}


static GstCaps* gst_imx_2d_video_transform_fixate_size_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
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
	gst_imx_2d_video_transform_fixate_format_caps(transform, caps, othercaps);

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



static void gst_imx_2d_video_transform_fixate_format_caps(GstBaseTransform *transform, GstCaps *caps, GstCaps *othercaps)
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


static gboolean gst_imx_2d_video_transform_set_caps(GstBaseTransform *transform, GstCaps *input_caps, GstCaps *output_caps)
{
	guint i;
	GstVideoInfo input_video_info;
	GstVideoInfo output_video_info;
	Imx2dSurfaceDesc output_surface_desc;
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);

	g_assert(self->blitter != NULL);

	/* Convert the caps to video info structures for easier acess. */

	GST_DEBUG_OBJECT(self, "setting caps: input caps: %" GST_PTR_FORMAT "  output caps: %" GST_PTR_FORMAT, (gpointer)input_caps, (gpointer)output_caps);

	if (!gst_video_info_from_caps(&input_video_info, input_caps))
	{
		GST_ERROR_OBJECT(self, "cannot convert input caps to video info; input caps: %" GST_PTR_FORMAT, (gpointer)input_caps);
		self->inout_info_set = FALSE;
		return FALSE;
	}

	if (!gst_video_info_from_caps(&output_video_info, output_caps))
	{
		GST_ERROR_OBJECT(self, "cannot convert output caps to video info; output caps: %" GST_PTR_FORMAT, (gpointer)output_caps);
		self->inout_info_set = FALSE;
		return FALSE;
	}

	/* Check if the input and output video are equal. This will be needed
	 * in gst_imx_2d_video_transform_prepare_output_buffer() to decide
	 * whether or not the input buffer needs to be passed through. */
	// TODO: Once deinterlacing is introduced, also check
	// for interlacing flags if deinterlacing is enabled.
	self->inout_info_equal = (GST_VIDEO_INFO_WIDTH(&input_video_info) == GST_VIDEO_INFO_WIDTH(&output_video_info))
	                      && (GST_VIDEO_INFO_HEIGHT(&input_video_info) == GST_VIDEO_INFO_HEIGHT(&output_video_info))
	                      && (GST_VIDEO_INFO_FORMAT(&input_video_info) == GST_VIDEO_INFO_FORMAT(&output_video_info));

	if (self->inout_info_equal)
		GST_DEBUG_OBJECT(self, "input and output caps are equal");
	else
		GST_DEBUG_OBJECT(self, "input and output caps are not equal");

	/* Fill the input surface description with values that can't change
	 * in between buffers. (Plane stride and offset values can change.
	 * This is unlikely to happen, but it is not impossible.) */
	self->input_surface_desc.width = GST_VIDEO_INFO_WIDTH(&input_video_info);
	self->input_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&input_video_info);
	self->input_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&input_video_info));

	/* Fill the output surface description. None of its values can change
	 * in between buffers, since we allocate the output buffers ourselves.
	 * In gst_imx_2d_video_transform_decide_allocation(), we set up the
	 * buffer pool that will be used for acquiring output buffers, and
	 * those buffers will always use the same plane stride and plane
	 * offset values. */
	memset(&output_surface_desc, 0, sizeof(output_surface_desc));
	output_surface_desc.width = GST_VIDEO_INFO_WIDTH(&output_video_info);
	output_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&output_video_info);
	output_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&output_video_info));

	/* As said above, we allocate the output buffers ourselves, so we can
	 * define what the plane stride and offset values should be. Do that
	 * by using this utility function to calculate the strides and offsets. */
	imx_2d_surface_desc_calculate_strides_and_offsets(&output_surface_desc, imx_2d_blitter_get_hardware_capabilities(self->blitter));

	imx_2d_surface_set_desc(self->output_surface, &output_surface_desc);

	/* Copy the calculated strides and offsets into the output_video_info
	 * structure so that its values and those in output_surface_desc match. */
	for (i = 0; i < GST_VIDEO_INFO_N_PLANES(&output_video_info); ++i)
	{
		GST_VIDEO_INFO_PLANE_STRIDE(&output_video_info, i) = output_surface_desc.plane_stride[i];
		GST_VIDEO_INFO_PLANE_OFFSET(&output_video_info, i) = output_surface_desc.plane_offset[i];
	}

	/* Also set the output_video_info size to the one that results from the
	 * values in output_surface_desc. This is particularly important for
	 * gst_imx_2d_video_transform_decide_allocation(), since that function
	 * will be called once this set_caps() function is done, and it will
	 * use the output_video_info values we set here. */
	GST_VIDEO_INFO_SIZE(&output_video_info) = imx_2d_surface_desc_calculate_framesize(&output_surface_desc);

	gst_caps_replace(&(self->input_caps), input_caps);

	self->input_video_info = input_video_info;
	self->output_video_info = output_video_info;
	self->inout_info_set = TRUE;

	return TRUE;
}


static gboolean gst_imx_2d_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);
	GstStructure *pool_config;
	guint i;
	GstCaps *negotiated_caps;
	GstAllocator *selected_allocator = NULL;
	GstAllocationParams allocation_params;
	GstBufferPool *new_buffer_pool = NULL;
	guint buffer_size;

	GST_TRACE_OBJECT(self, "attempting to decide what buffer pool and allocator to use");

	gst_query_parse_allocation(query, &negotiated_caps, NULL);

	for (i = 0; i < gst_query_get_n_allocation_params(query); ++i)
	{
		GstAllocator *allocator = NULL;

		gst_query_parse_nth_allocation_param(query, i, &allocator, &allocation_params);
		if (allocator == NULL)
			continue;

		if (GST_IS_IMX_DMA_BUFFER_ALLOCATOR(allocator))
		{
			GST_DEBUG_OBJECT(self, "allocator #%u in allocation query can allocate DMA memory", i);
			selected_allocator = allocator;
			break;
		}
		else
			gst_object_unref(GST_OBJECT_CAST(allocator));
	}

	/* If no suitable allocator was found, use our own. */
	if (selected_allocator == NULL)
	{
		GST_DEBUG_OBJECT(self, "found no allocator in query that can allocate DMA memory, using our own");
		gst_allocation_params_init(&allocation_params);
		selected_allocator = gst_object_ref(self->imx_dma_buffer_allocator);
	}

	/* Create our own buffer pool, and use the output video info size as
	 * its buffer size. We do not look at the pools in the query, because
	 * we want to make sure that the pool uses our selected allocator.
	 * Buffer pools may ignore allocators that we pass to them, but for
	 * this element, it is essential that the buffer pool uses the selected
	 * ImxDmaBuffer allocator. */
	GST_DEBUG_OBJECT(self, "creating new buffer pool");
	new_buffer_pool = gst_video_buffer_pool_new();
	/* decide_allocation() is called after set_caps(), so
	 * it is safe to use self->output_video_info here. */
	buffer_size = GST_VIDEO_INFO_SIZE(&(self->output_video_info));

	/* Make sure the selected allocator is picked by setting
	 * it as the first entry in the allocation param list. */
	if (gst_query_get_n_allocation_params(query) == 0)
	{
		GST_DEBUG_OBJECT(self, "there are no allocation pools in the allocation query; adding our buffer pool to it");
		gst_query_add_allocation_param(query, selected_allocator, &allocation_params);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "there are allocation pools in the allocation query; setting our buffer pool as the first one in the query");
		gst_query_set_nth_allocation_param(query, 0, selected_allocator, &allocation_params);
	}

	/* Make sure the selected buffer pool is picked by setting
	 * it as the first entry in the allocation pool list. */
	if (gst_query_get_n_allocation_pools(query) == 0)
	{
		GST_DEBUG_OBJECT(self, "there are no allocation pools in the allocation query; adding our buffer pool to it");
		gst_query_add_allocation_pool(query, new_buffer_pool, buffer_size, 0, 0);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "there are allocation pools in the allocation query; setting our buffer pool as the first one in the query");
		gst_query_set_nth_allocation_pool(query, 0, new_buffer_pool, buffer_size, 0, 0);
	}

	/* Enable the videometa option in the buffer pool to make
	 * sure it gets added to newly created buffers. */
	pool_config = gst_buffer_pool_get_config(new_buffer_pool);
	gst_buffer_pool_config_set_params(pool_config, negotiated_caps, buffer_size, 0, 0);
	gst_buffer_pool_config_add_option(pool_config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(new_buffer_pool, pool_config);

	/* Unref these, since we passed them to the query. */
	gst_object_unref(GST_OBJECT(selected_allocator));
	gst_object_unref(GST_OBJECT(new_buffer_pool));

	/* Chain up to the base class. */
	return GST_BASE_TRANSFORM_CLASS(gst_imx_2d_video_transform_parent_class)->decide_allocation(transform, query);
}


static GstFlowReturn gst_imx_2d_video_transform_prepare_output_buffer(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);
	GstVideoCropMeta *video_crop_meta = NULL;
	gboolean passthrough;
	Imx2dRotation output_rotation;
	Imx2dFlipMode output_flip_mode;
	gboolean input_crop;
	gboolean has_crop_meta = FALSE;

	/* The code in here has one single purpose: to decide whether or not the input buffer
	 * is to be passed through. Passthrough is done by setting *output_buffer to input_buffer.
	 *
	 * Passthrough is done if and only if all of these conditions are met:
	 *
	 * - Inout buffer is not NULL
	 * - Input and output caps (or rather, video infos) are equal
	 * - Input crop is disabled, or it is enabled & the input buffer's video
	 *   crop meta defines a rectangle that contains the entire frame
	 * - Output rotation is disabled (= set to IMX_2D_ROTATION_NONE)
	 * - Flip mode is disabled (= set to IMX_2D_FLIP_MODE_NONE)
	 */

	g_assert(self->uploader != NULL);

	GST_OBJECT_LOCK(self);
	output_rotation = self->output_rotation;
	output_flip_mode = self->output_flip_mode;
	input_crop = self->input_crop;
	GST_OBJECT_UNLOCK(self);

	{
		gboolean has_input_buffer = (input_buffer != NULL);
		gboolean no_output_rotation = (output_rotation == IMX_2D_ROTATION_NONE);
		gboolean no_output_flip_mode = (output_flip_mode == IMX_2D_FLIP_MODE_NONE);

		if (input_crop)
		{
			video_crop_meta = gst_buffer_get_video_crop_meta(input_buffer);
			has_crop_meta = (video_crop_meta != NULL);
		}

		GST_LOG_OBJECT(
			self,
			"has input: %d  input&output video info equal: %d  no output rotation: %d  no flip mode: %d  input crop: %d  has crop meta: %d",
			has_input_buffer,
			self->inout_info_equal,
			no_output_rotation, no_output_flip_mode,
			input_crop, has_crop_meta
		);

		passthrough = (input_buffer != NULL)
		           && self->inout_info_equal
		           && (output_rotation == IMX_2D_ROTATION_NONE)
		           && (output_flip_mode == IMX_2D_FLIP_MODE_NONE);

		if (passthrough && input_crop && has_crop_meta)
		{
			guint in_width, in_height;
			gboolean crop_rect_contains_entire_frame;

			in_width = GST_VIDEO_INFO_WIDTH(&(self->input_video_info));
			in_height = GST_VIDEO_INFO_HEIGHT(&(self->input_video_info));

			crop_rect_contains_entire_frame = (video_crop_meta->x == 0)
			                               && (video_crop_meta->y == 0)
			                               && (video_crop_meta->width == in_width)
			                               && (video_crop_meta->height == in_height);

			GST_LOG_OBJECT(self, "crop rectangle contains whole input frame: %d", crop_rect_contains_entire_frame);

			passthrough = crop_rect_contains_entire_frame;
		}

		GST_LOG_OBJECT(self, "=> passthrough: %s", passthrough ? "yes" : "no");
	}

	if (passthrough)
	{
		*output_buffer = input_buffer;
		return GST_FLOW_OK;
	}

	return GST_BASE_TRANSFORM_CLASS(gst_imx_2d_video_transform_parent_class)->prepare_output_buffer(transform, input_buffer, output_buffer);
}


static GstFlowReturn gst_imx_2d_video_transform_transform_frame(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer *output_buffer)
{
	Imx2dBlitParams blit_params;
	GstFlowReturn flow_ret;
	ImxDmaBuffer *in_dma_buffer;
	ImxDmaBuffer *out_dma_buffer;
	gboolean input_crop;
	Imx2dRegion crop_rectangle;
	Imx2dRotation output_rotation;
	Imx2dFlipMode output_flip_mode;
	GstVideoMeta *videometa;
	guint plane_index;
	GstBuffer *uploading_result;
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);

	if (G_UNLIKELY(!self->inout_info_set))
	{
		GST_ELEMENT_ERROR(transform, CORE, NEGOTIATION, (NULL), ("unknown format"));
		return GST_FLOW_NOT_NEGOTIATED;
	}

	if (input_buffer == output_buffer)
	{
		GST_LOG_OBJECT(self, "passing buffer through");
		return GST_FLOW_OK;
	}

	GST_LOG_OBJECT(self, "beginning frame transform by uploading input buffer");

	/* Begin by uploading the buffer. This may actually be a secondary form
	 * of "passthrough". If the input buffer already uses an ImxDmaBuffer, then
	 * there is no point in doing a proper upload (which typically would imply
	 * a CPU based frame copy). Instead, we can then just use the input buffer
	 * as-is. The uploader can configure itself automatically based on the input
	 * caps, and has a method for this case, where the input buffer is ref'd, but
	 * otherwise just passed through. In other cases, such as when an upstream
	 * element outputs buffers that have their memory in sysmem, the uploader
	 * will have chosen a method that does copy the buffer contents. */
	flow_ret = gst_imx_dma_buffer_uploader_perform(self->uploader, input_buffer, &uploading_result);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		return flow_ret;

	/* From this moment on, use the uploaded version as our "input buffer".
	 * As explained above, depending on the caps, this may really be still
	 * our original input buffer. */
	input_buffer = uploading_result;

	/* The rest of the code in this function expects buffers with ImxDmaBuffer inside. */
	g_assert(gst_imx_has_imx_dma_buffer_memory(input_buffer));
	g_assert(gst_imx_has_imx_dma_buffer_memory(output_buffer));
	in_dma_buffer = gst_imx_get_dma_buffer_from_buffer(input_buffer);
	out_dma_buffer = gst_imx_get_dma_buffer_from_buffer(output_buffer);
	g_assert(in_dma_buffer != NULL);
	g_assert(out_dma_buffer != NULL);

	/* Create local copies of the property values so that we can use them
	 * without risking race conditions if another thread is setting new
	 * values while this function is running. */
	GST_OBJECT_LOCK(self);
	input_crop = self->input_crop;
	output_rotation = self->output_rotation;
	output_flip_mode = self->output_flip_mode;
	GST_OBJECT_UNLOCK(self);

	GST_LOG_OBJECT(self, "filling input surface description with input buffer plane stride and -info");

	/* Fill plane offset and stride values into input_surface_desc. As explained
	 * in gst_imx_2d_video_transform_set_caps(), these values _can_ in theory
	 * change between incoming buffers. Prefer getting them from a videometa,
	 * because those can carry values with them that deviate from what could
	 * be calculated out of the caps. For example, if width = 100 and bytes
	 * per pixel = 3, then one could calculate a stride value of 100*3 = 300 byte.
	 * But the underlying hardware may require alignment to 16-byte increments,
	 * and the actual stride value is then 304 bytes - impossible to determine
	 * with the caps alone. The videometa would then contain this stride value
	 * of 304 bytes. Consequently, it is better to look at the videometa and
	 * use its values instead of relying on computed ones. */
	videometa = gst_buffer_get_video_meta(input_buffer);
	if (videometa != NULL)
	{
		for (plane_index = 0; plane_index < videometa->n_planes; ++plane_index)
		{
			self->input_surface_desc.plane_stride[plane_index] = videometa->stride[plane_index];
			self->input_surface_desc.plane_offset[plane_index] = videometa->offset[plane_index];

			GST_LOG_OBJECT(
				self,
				"input plane #%u info from videometa:  stride: %d  offset: %d",
				plane_index,
				self->input_surface_desc.plane_stride[plane_index],
				self->input_surface_desc.plane_offset[plane_index]
			);
		}
	}
	else
	{
		GstVideoInfo *in_info = &(self->input_video_info);

		for (plane_index = 0; plane_index < GST_VIDEO_INFO_N_PLANES(in_info); ++plane_index)
		{
			self->input_surface_desc.plane_stride[plane_index] = GST_VIDEO_INFO_PLANE_STRIDE(in_info, plane_index);
			self->input_surface_desc.plane_offset[plane_index] = GST_VIDEO_INFO_PLANE_OFFSET(in_info, plane_index);

			GST_LOG_OBJECT(
				self,
				"input plane #%u info from videoinfo:  stride: %d  offset: %d",
				plane_index,
				self->input_surface_desc.plane_stride[plane_index],
				self->input_surface_desc.plane_offset[plane_index]
			);
		}
	}

	imx_2d_surface_set_desc(self->input_surface, &(self->input_surface_desc));

	GST_LOG_OBJECT(self, "setting output buffer videometa's plane stride and -offset");

	/* Now fill the videometa of the output buffer. Since we allocate
	 * these buffers, we know they always must contain a videometa.
	 * That meta needs to be filled with valid values though. */

	videometa = gst_buffer_get_video_meta(output_buffer);
	g_assert(videometa != NULL);

	{
		GstVideoInfo *out_info = &(self->output_video_info);

		for (plane_index = 0; plane_index < GST_VIDEO_INFO_N_PLANES(out_info); ++plane_index)
		{
			videometa->stride[plane_index] = GST_VIDEO_INFO_PLANE_STRIDE(out_info, plane_index);
			videometa->offset[plane_index] = GST_VIDEO_INFO_PLANE_OFFSET(out_info, plane_index);

			GST_LOG_OBJECT(
				self,
				"output plane #%u info:  stride: %d  offset: %d",
				plane_index,
				self->input_surface_desc.plane_stride[plane_index],
				self->input_surface_desc.plane_offset[plane_index]
			);
		}
	}

	GST_LOG_OBJECT(self, "setting ImxDmaBuffer %p as input DMA buffer", (gpointer)(in_dma_buffer));
	GST_LOG_OBJECT(self, "setting ImxDmaBuffer %p as output DMA buffer", (gpointer)(out_dma_buffer));

	imx_2d_surface_set_dma_buffer(self->input_surface, in_dma_buffer);
	imx_2d_surface_set_dma_buffer(self->output_surface, out_dma_buffer);

	blit_params.source_region = NULL;
	blit_params.dest_region = NULL;
	blit_params.flip_mode = output_flip_mode;
	blit_params.rotation = output_rotation;
	blit_params.alpha = 255;

	if (input_crop)
	{
		GstVideoCropMeta *crop_meta = gst_buffer_get_video_crop_meta(input_buffer);

		if (crop_meta != NULL)
		{
			crop_rectangle.x1 = crop_meta->x;
			crop_rectangle.y1 = crop_meta->y;
			crop_rectangle.x2 = crop_meta->x + crop_meta->width;
			crop_rectangle.y2 = crop_meta->y + crop_meta->height;

			blit_params.source_region = &crop_rectangle;

			GST_LOG_OBJECT(
				self,
				"using crop rectangle (%d, %d) - (%d, %d)",
				crop_rectangle.x1, crop_rectangle.y1,
				crop_rectangle.x2, crop_rectangle.y2
			);
		}
	}

	GST_LOG_OBJECT(self, "beginning blitting procedure to transform the frame");

	if (!imx_2d_blitter_start(self->blitter))
	{
		GST_ERROR_OBJECT(self, "starting blitter failed");
		goto error;
	}

	if (!imx_2d_blitter_do_blit(self->blitter, self->input_surface, self->output_surface, &blit_params))
	{
		GST_ERROR_OBJECT(self, "blitting failed");
		goto error;
	}

	if (!imx_2d_blitter_finish(self->blitter))
	{
		GST_ERROR_OBJECT(self, "finishing blitter failed");
		goto error;
	}

	GST_LOG_OBJECT(self, "blitting procedure finished successfully; frame transform complete");

finish:
	gst_buffer_unref(input_buffer);
	return flow_ret;

error:
	if (flow_ret != GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


static gboolean gst_imx_2d_video_transform_transform_size(GstBaseTransform *transform, G_GNUC_UNUSED GstPadDirection direction, G_GNUC_UNUSED GstCaps *caps, G_GNUC_UNUSED gsize size, GstCaps *othercaps, gsize *othersize)
{
	/* We use transform_size instead of get_unit_size because due to
	 * padding rows/columns in a frame / imx2d surface, we may not
	 * be able to provide an integer multiple of units to the default
	 * transform_size implementation. */

	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);
	GstVideoInfo video_info;
	Imx2dSurfaceDesc surface_desc;

	if (G_UNLIKELY(self->blitter == NULL))
		return FALSE;

	if (G_UNLIKELY(!gst_video_info_from_caps(&video_info, othercaps)))
		return FALSE;

	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.width = GST_VIDEO_INFO_WIDTH(&video_info);
	surface_desc.height = GST_VIDEO_INFO_HEIGHT(&video_info);
	surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&video_info));

	imx_2d_surface_desc_calculate_strides_and_offsets(&surface_desc, imx_2d_blitter_get_hardware_capabilities(self->blitter));

	*othersize = imx_2d_surface_desc_calculate_framesize(&surface_desc);

	return TRUE;
}


static gboolean gst_imx_2d_video_transform_transform_meta(GstBaseTransform *trans, GstBuffer *input_buffer, GstMeta *meta, GstBuffer *output_buffer)
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

	return GST_BASE_TRANSFORM_CLASS(gst_imx_2d_video_transform_parent_class)->transform_meta(trans, input_buffer, meta, output_buffer);
}


static gboolean gst_imx_2d_video_transform_copy_metadata(G_GNUC_UNUSED GstBaseTransform *trans, GstBuffer *input_buffer, GstBuffer *output_buffer)
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


static gboolean gst_imx_2d_video_transform_start(GstImx2dVideoTransform *self)
{
	GstImx2dVideoTransformClass *klass = GST_IMX_2D_VIDEO_TRANSFORM_CLASS(G_OBJECT_GET_CLASS(self));

	self->inout_info_equal = FALSE;
	self->inout_info_set = FALSE;

	self->imx_dma_buffer_allocator = gst_imx_allocator_new();
	self->uploader = gst_imx_dma_buffer_uploader_new(self->imx_dma_buffer_allocator);

	if ((klass->start != NULL) && !(klass->start(self)))
	{
		GST_ERROR_OBJECT(self, "start() failed");
		goto error;
	}

	if (!gst_imx_2d_video_transform_create_blitter(self))
	{
		GST_ERROR_OBJECT(self, "creating blitter failed");
		goto error;
	}

	self->input_surface = imx_2d_surface_create(NULL, NULL);
	if (self->input_surface == NULL)
	{
		GST_ERROR_OBJECT(self, "creating input surface failed");
		goto error;
	}

	self->output_surface = imx_2d_surface_create(NULL, NULL);
	if (self->output_surface == NULL)
	{
		GST_ERROR_OBJECT(self, "creating output surface failed");
		goto error;
	}

	return TRUE;

error:
	gst_imx_2d_video_transform_stop(self);
	return FALSE;
}


static void gst_imx_2d_video_transform_stop(GstImx2dVideoTransform *self)
{
	GstImx2dVideoTransformClass *klass = GST_IMX_2D_VIDEO_TRANSFORM_CLASS(G_OBJECT_GET_CLASS(self));

	if ((klass->stop != NULL) && !(klass->stop(self)))
		GST_ERROR_OBJECT(self, "stop() failed");

	gst_caps_replace(&(self->input_caps), NULL);

	if (self->input_surface != NULL)
	{
		imx_2d_surface_destroy(self->input_surface);
		self->input_surface = NULL;
	}

	if (self->output_surface != NULL)
	{
		imx_2d_surface_destroy(self->output_surface);
		self->output_surface = NULL;
	}

	if (self->blitter != NULL)
	{
		imx_2d_blitter_destroy(self->blitter);
		self->blitter = NULL;
	}

	if (self->uploader != NULL)
	{
		gst_object_unref(GST_OBJECT(self->uploader));
		self->uploader = NULL;
	}

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}
}


static gboolean gst_imx_2d_video_transform_create_blitter(GstImx2dVideoTransform *self)
{
	GstImx2dVideoTransformClass *klass = GST_IMX_2D_VIDEO_TRANSFORM_CLASS(G_OBJECT_GET_CLASS(self));

	g_assert(klass->create_blitter != NULL);
	g_assert(self->blitter == NULL);

	if (G_UNLIKELY((self->blitter = klass->create_blitter(self)) == NULL))
	{
		GST_ERROR_OBJECT(self, "could not create blitter");
		return FALSE;
	}

	GST_DEBUG_OBJECT(self, "created new blitter %" GST_PTR_FORMAT, (gpointer)(self->blitter));

	return TRUE;
}


void gst_imx_2d_video_transform_common_class_init(GstImx2dVideoTransformClass *klass, Imx2dHardwareCapabilities const *capabilities)
{
	GstElementClass *element_class;
	GstCaps *sink_template_caps;
	GstCaps *src_template_caps;
	GstPadTemplate *sink_template;
	GstPadTemplate *src_template;

	element_class = GST_ELEMENT_CLASS(klass);

	sink_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities(capabilities, GST_PAD_SINK);
	src_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities(capabilities, GST_PAD_SRC);

	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_template_caps);

	gst_element_class_add_pad_template(element_class, sink_template);
	gst_element_class_add_pad_template(element_class, src_template);
}
