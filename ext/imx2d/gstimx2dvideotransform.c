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
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimx2dvideotransform.h"
#include "gstimx2dvideobufferpool.h"
#include "gstimx2dmisc.h"


GST_DEBUG_CATEGORY_STATIC(imx_2d_video_transform_debug);
#define GST_CAT_DEFAULT imx_2d_video_transform_debug


enum
{
	PROP_0,
	PROP_INPUT_CROP,
	PROP_OUTPUT_ROTATION
};


#define DEFAULT_INPUT_CROP TRUE
#define DEFAULT_OUTPUT_ROTATION IMX_2D_ROTATION_NONE


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
static gboolean gst_imx_2d_video_transform_propose_allocation(GstBaseTransform *transform, GstQuery *decide_query, GstQuery *query);
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

	gst_imx_2d_setup_logging();

	GST_DEBUG_CATEGORY_INIT(imx_2d_video_transform_debug, "imx2dvideotransform", 0, "NXP i.MX 2D video transform base class");

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
	base_transform_class->propose_allocation    = GST_DEBUG_FUNCPTR(gst_imx_2d_video_transform_propose_allocation);
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
}


void gst_imx_2d_video_transform_init(GstImx2dVideoTransform *self)
{
	GstBaseTransform *base_transform = GST_BASE_TRANSFORM(self);

	self->blitter = NULL;

	self->inout_info_equal = FALSE;
	self->inout_info_set = FALSE;

	self->passing_through_overlay_meta = FALSE;

	gst_video_info_init(&(self->input_video_info));
	gst_video_info_init(&(self->output_video_info));

	self->input_caps = NULL;

	self->input_surface = NULL;
	self->output_surface = NULL;

	self->overlay_handler = NULL;

	self->input_crop = DEFAULT_INPUT_CROP;
	self->output_rotation = DEFAULT_OUTPUT_ROTATION;

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


static GstCaps* gst_imx_2d_video_transform_transform_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
	GstCaps *unfiltered_caps, *transformed_caps;
	GstStructure *structure;
	GstCapsFeatures *features;
	gint caps_idx, num_caps;


	/* Process each structure from the caps, copy them, and modify them if necessary. */

	unfiltered_caps = gst_caps_new_empty();
	num_caps = gst_caps_get_size(caps);

	for (caps_idx = 0; caps_idx < num_caps; ++caps_idx)
	{
		structure = gst_caps_get_structure(caps, caps_idx);
		features = gst_caps_get_features(caps, caps_idx);

		/* Copy the caps features since the gst_caps_append_structure_full()
		 * call below takes ownership over the caps features object. */
		features = gst_caps_features_copy(features);

		/* If this is already expressed by the existing caps, skip this structure. */
		if ((caps_idx > 0) && gst_caps_is_subset_structure_full(unfiltered_caps, structure, features))
		{
			gst_caps_features_free(features);
			continue;
		}

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

		gst_caps_append_structure_full(unfiltered_caps, structure, features);
	}

	GST_DEBUG_OBJECT(transform, "got unfiltered caps: %" GST_PTR_FORMAT, (gpointer)unfiltered_caps);


	/* Create a copy of the unfiltered caps, and for each structure in the copy,
	 * add/remove the overlay composition meta caps feature. The intent here
	 * is to make sure the caps always contain versions of these structure with
	 * and without that caps feature.
	 *
	 * If the direction is GST_PAD_SRC, it means that "caps" is the srcpad, and
	 * we are figuring out what the corresponding sink caps are. In this case,
	 * we want to _add_ the caps feature from the copy, since the srcpad may
	 * have caps that don't have that caps feature (even though this transform
	 * element _can_ handle that caps feature, therefore we do want it present
	 * in the sink pad caps).
	 *
	 * If the direction is GST_PAD_SINK, it means that "caps" is the sinkpad, and
	 * we are figuring out what the corresponding src caps are. In this case,
	 * we want to _remove_ the caps feature from the copy, since the sinkpad
	 * may have caps that contain that caps features while downstream doesn't
	 * support it (leading to incompatible caps if our src pad caps only contain
	 * structures that have that caps feature attached).
	 *
	 * The copy is then merged with the original to provide a result that contains
	 * structures with and without that caps feature. Note tthat when merging,
	 * we place the copy _first_. This causes caps with the caps feature to
	 * come first, and those without to come after. This is important to make
	 * sure that when gst_imx_2d_video_transform_fixate_caps() is called, the
	 * very first caps contain the caps feature. In cases where downstream cannot
	 * handle that caps feature, only caps without that feature will make it
	 * through the filter below, so this case is also covered. */

	num_caps = gst_caps_get_size(unfiltered_caps);

	if (direction == GST_PAD_SRC)
	{
		GstCaps *unfiltered_caps_with_composition = gst_caps_copy(unfiltered_caps);

		for (caps_idx = 0; caps_idx < num_caps; ++caps_idx)
		{
			features = gst_caps_get_features(unfiltered_caps_with_composition, caps_idx);
			if (!gst_caps_features_is_any(features))
				gst_caps_features_add(features, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
		}

		GST_DEBUG_OBJECT(
			transform,
			"created copy of unfiltered caps with overlay composition feature added: %" GST_PTR_FORMAT,
			(gpointer)unfiltered_caps_with_composition
		);

		unfiltered_caps = gst_caps_merge(unfiltered_caps_with_composition, unfiltered_caps);
	}
	else
	{
		GstCaps *unfiltered_caps_without_composition = gst_caps_copy(unfiltered_caps);

		for (caps_idx = 0; caps_idx < num_caps; ++caps_idx)
		{
			features = gst_caps_get_features(unfiltered_caps_without_composition, caps_idx);
			if (gst_caps_features_contains(features, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION))
				gst_caps_features_remove(features, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
		}

		GST_DEBUG_OBJECT(
			transform,
			"created copy of unfiltered caps with overlay composition feature removed: %" GST_PTR_FORMAT,
			(gpointer)unfiltered_caps_without_composition
		);

		unfiltered_caps = gst_caps_merge(unfiltered_caps, unfiltered_caps_without_composition);
	}

	GST_DEBUG_OBJECT(transform, "merged the copy into the unfiltered caps: %" GST_PTR_FORMAT, (gpointer)unfiltered_caps);


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
 * gst_imx_2d_video_transform_fixate_caps
 * gst_imx_2d_video_transform_fixate_size_caps
 * score_value
 * gst_imx_2d_video_transform_fixate_format_caps
 * */


static GstCaps* gst_imx_2d_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
	GstCaps *result;

	GST_DEBUG_OBJECT(transform, "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT, (gpointer)othercaps, (gpointer)caps);

	result = gst_caps_truncate(othercaps);
	result = gst_caps_make_writable(result);
	GST_DEBUG_OBJECT(transform, "truncated caps to: %" GST_PTR_FORMAT, (gpointer)result);

	result = gst_imx_2d_video_transform_fixate_size_caps(transform, direction, caps, result);
	GST_DEBUG_OBJECT(transform, "fixated size to: %" GST_PTR_FORMAT, (gpointer)result);

	gst_imx_2d_video_transform_fixate_format_caps(transform, caps, result);
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
	GstImx2dTileLayout input_video_tile_layout;
	GstVideoInfo output_video_info;
	GstCapsFeatures *input_caps_features;
	GstCapsFeatures *output_caps_features;
	gboolean input_has_overlay_meta;
	gboolean output_has_overlay_meta;
	Imx2dSurfaceDesc output_surface_desc;
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);

	g_assert(self->blitter != NULL);

	/* Convert the caps to video info structures for easier acess. */

	GST_DEBUG_OBJECT(self, "setting caps: input caps: %" GST_PTR_FORMAT "  output caps: %" GST_PTR_FORMAT, (gpointer)input_caps, (gpointer)output_caps);

	if (!gst_imx_video_info_from_caps(&input_video_info, input_caps, &input_video_tile_layout, NULL))
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

	/* The stride values may require alignment according to the blitter's
	 * capabilities. Adjust the output video's fields to match those. */
	{
		GstVideoInfo original_output_video_info;
		GstVideoAlignment video_alignment;
		Imx2dHardwareCapabilities const *capabilities = imx_2d_blitter_get_hardware_capabilities(self->blitter);

		gst_video_alignment_reset(&video_alignment);

		GST_DEBUG_OBJECT(self, "aligning output video info stride; blitter's stride alignment = %d", capabilities->stride_alignment);

		for (i = 0; i < GST_VIDEO_INFO_N_PLANES(&output_video_info); ++i)
			video_alignment.stride_align[i] = capabilities->stride_alignment - 1;

		memcpy(&original_output_video_info, &output_video_info, sizeof(GstVideoInfo));
		gst_video_info_align(&output_video_info, &video_alignment);

		for (i = 0; i < GST_VIDEO_INFO_N_PLANES(&output_video_info); ++i)
		{
			GST_DEBUG_OBJECT(
				self,
				"plane %u of output video info: original stride %d aligned stride %d",
				i,
				GST_VIDEO_INFO_PLANE_STRIDE(&original_output_video_info, i),
				GST_VIDEO_INFO_PLANE_STRIDE(&output_video_info, i)
			);
		}
	}

	/* Check if input and output caps have the overlay meta caps feature.
	 * If both have it, or neither have it, we can pass through the data.
	 * In the former case, it means that downstream can handle this meta,
	 * so we can pass the overlay composition data downstream unchanged.
	 * If neither have it, then there's no meta to concern ourselves with.
	 * But if the input caps have the meta and the output caps don't,
	 * then we _have_ to render the caps into the frame, even if there
	 * is no actual transformation going on. (The case where input caps
	 * have no meta but output caps do does not exist.) */
	input_caps_features = gst_caps_get_features(input_caps, 0);
	input_has_overlay_meta = gst_caps_features_contains(input_caps_features, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
	output_caps_features = gst_caps_get_features(output_caps, 0);
	output_has_overlay_meta = gst_caps_features_contains(output_caps_features, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
	GST_DEBUG_OBJECT(self, "input/output caps have overlay meta: %d/%d", input_has_overlay_meta, output_has_overlay_meta);

	/* If there is an overlay meta, and downstream supports that meta,
	 * remember this in case there are other transformations that need
	 * to be performed. That's because in this case, while rendering the
	 * frame, we have to make sure that we don't also render the overlays,
	 * since downstream can handle those. */
	self->passing_through_overlay_meta = input_has_overlay_meta && output_has_overlay_meta;
	if (self->passing_through_overlay_meta)
		GST_DEBUG_OBJECT(self, "there is overlay meta and downstream can handle it -> will pass through that meta");

	/* Check if the input and output video are equal. This will be needed
	 * in gst_imx_2d_video_transform_prepare_output_buffer() to decide
	 * whether or not the input buffer needs to be passed through. */
	// TODO: Once deinterlacing is introduced, also check
	// for interlacing flags if deinterlacing is enabled.
	self->inout_info_equal = (GST_VIDEO_INFO_WIDTH(&input_video_info) == GST_VIDEO_INFO_WIDTH(&output_video_info))
	                      && (GST_VIDEO_INFO_HEIGHT(&input_video_info) == GST_VIDEO_INFO_HEIGHT(&output_video_info))
	                      && (GST_VIDEO_INFO_FORMAT(&input_video_info) == GST_VIDEO_INFO_FORMAT(&output_video_info))
	                      && (input_video_tile_layout == GST_IMX_2D_TILE_LAYOUT_NONE)
	                      && (input_has_overlay_meta == output_has_overlay_meta);

	if (self->inout_info_equal)
		GST_DEBUG_OBJECT(self, "input and output caps are equal");
	else
		GST_DEBUG_OBJECT(self, "input and output caps are not equal");

	/* Fill the input surface description with values that can't change
	 * in between buffers. (Plane stride and offset values can change.
	 * This is unlikely to happen, but it is not impossible.) */
	self->input_surface_desc.width = GST_VIDEO_INFO_WIDTH(&input_video_info);
	self->input_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&input_video_info);
	self->input_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&input_video_info), &input_video_tile_layout);

	/* Fill the output surface description. None of its values can change
	 * in between buffers, since we allocate the output buffers ourselves.
	 * In gst_imx_2d_video_transform_decide_allocation(), we set up the
	 * buffer pool that will be used for acquiring output buffers, and
	 * those buffers will always use the same plane stride and plane
	 * offset values. */
	memset(&output_surface_desc, 0, sizeof(output_surface_desc));
	output_surface_desc.width = GST_VIDEO_INFO_WIDTH(&output_video_info);
	output_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&output_video_info);
	output_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&output_video_info), NULL);

	for (i = 0; i < GST_VIDEO_INFO_N_PLANES(&output_video_info); ++i)
		output_surface_desc.plane_strides[i] = GST_VIDEO_INFO_PLANE_STRIDE(&output_video_info, i);

	imx_2d_surface_set_desc(self->output_surface, &output_surface_desc);

	gst_caps_replace(&(self->input_caps), input_caps);

	gst_imx_2d_video_overlay_handler_clear_cached_overlays(self->overlay_handler);

	GST_OBJECT_LOCK(self);
	self->input_video_info = input_video_info;
	self->output_video_info = output_video_info;
	self->inout_info_set = TRUE;
	GST_OBJECT_UNLOCK(self);

	return TRUE;
}


static gboolean gst_imx_2d_video_transform_propose_allocation(GstBaseTransform *transform, GstQuery *decide_query, GstQuery *query)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);
	GstStructure *allocation_meta_structure = NULL;
	guint output_width, output_height;
	gboolean already_has_overlay_meta;
	gboolean add_overlay_meta;
	guint meta_index;

	if (!GST_BASE_TRANSFORM_CLASS(gst_imx_2d_video_transform_parent_class)->propose_allocation(transform, decide_query, query))
		return FALSE;

	/* Passthrough; we are not supposed to do anything. */
	if (decide_query == NULL)
		return TRUE;

	/* Check if we need to add the overlay meta to the query.
	 * We have to add it in these cases:
	 *
	 * 1. The query does not already contain the overlay meta.
	 *    This occurs when downstream cannot handle such an
	 *    overlay meta.
	 * 2. This transform element is not passing through the
	 *    meta. In such a case, the overlays ar rendered by
	 *    the transform element. Therefore, the window size
	 *    used by upstream to compute the overlay rectangles
	 *    must fit the size of this transform element's
	 *    output frame. */
	already_has_overlay_meta = gst_query_find_allocation_meta(query, GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, &meta_index);
	add_overlay_meta = !already_has_overlay_meta || !(self->passing_through_overlay_meta);

	if (add_overlay_meta)
	{
		GST_OBJECT_LOCK(self);
		output_width = GST_VIDEO_INFO_WIDTH(&(self->output_video_info));
		output_height = GST_VIDEO_INFO_HEIGHT(&(self->output_video_info));
		GST_OBJECT_UNLOCK(self);

		GST_DEBUG_OBJECT(self, "proposing overlay composition meta to allocation query with output video size %ux%u", output_width, output_height);

		allocation_meta_structure = gst_structure_new(
			"GstVideoOverlayCompositionMeta",
			"width", G_TYPE_UINT, output_width,
			"height", G_TYPE_UINT, output_height,
			NULL
		);

		if (already_has_overlay_meta)
			gst_query_remove_nth_allocation_meta(query, meta_index);

		gst_query_add_allocation_meta(query, GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, allocation_meta_structure);

		gst_structure_free(allocation_meta_structure);
	}

	gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);

	return TRUE;
}


static gboolean gst_imx_2d_video_transform_decide_allocation(GstBaseTransform *transform, GstQuery *query)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);

	/* We are not chaining up to the base class since the default
	 * basetransform decide_allocation method is very cautious
	 * and throws away too much from the allocation query, including
	 * memory-tagged metas like the videometa, while not providing
	 * any work we aren't performing here anyway. */

	GST_TRACE_OBJECT(self, "attempting to decide what buffer pool and allocator to use");

	/* Discard any previously created buffer pool before creating a new one. */
	if (self->video_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->video_buffer_pool));
		self->video_buffer_pool = NULL;
	}

	self->video_buffer_pool = gst_imx_2d_video_buffer_pool_new(
		self->imx_dma_buffer_allocator,
		query,
		&(self->output_video_info)
	);

	return (self->video_buffer_pool != NULL);
}


static GstFlowReturn gst_imx_2d_video_transform_prepare_output_buffer(GstBaseTransform *transform, GstBuffer *input_buffer, GstBuffer **output_buffer)
{
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);
	GstVideoCropMeta *video_crop_meta = NULL;
	gboolean passthrough;
	Imx2dRotation output_rotation;
	gboolean input_crop;
	gboolean has_crop_meta = FALSE;

	/* The code in here has one single purpose: to decide whether or not the input buffer
	 * is to be passed through. Passthrough is done by setting *output_buffer to input_buffer.
	 *
	 * Passthrough is done if and only if all of these conditions are met:
	 *
	 * - Inout buffer is not NULL
	 * - Input and output caps (or rather, video infos) are equal
	 * - gst_imx_video_buffer_pool_are_both_pools_same() returns TRUE
	 * - Input crop is disabled, or it is enabled & the input buffer's video
	 *   crop meta defines a rectangle that contains the entire frame
	 * - Output rotation is disabled (= set to IMX_2D_ROTATION_NONE)
	 */

	g_assert(self->uploader != NULL);

	GST_OBJECT_LOCK(self);
	output_rotation = self->output_rotation;
	input_crop = self->input_crop;
	GST_OBJECT_UNLOCK(self);

	{
		gboolean has_input_buffer = (input_buffer != NULL);
		gboolean no_output_rotation = (output_rotation == IMX_2D_ROTATION_NONE);
		gboolean are_both_pools_same = gst_imx_2d_video_buffer_pool_are_both_pools_same(self->video_buffer_pool);

		if (input_crop)
		{
			video_crop_meta = gst_buffer_get_video_crop_meta(input_buffer);
			has_crop_meta = (video_crop_meta != NULL);
		}

		GST_LOG_OBJECT(
			self,
			"has input: %d  "
			"input&output video info equal: %d  "
			"no output rotation: %d  "
			"input crop: %d  "
			"has crop meta: %d"
			"are both pools same: %d",
			has_input_buffer,
			self->inout_info_equal,
			no_output_rotation,
			input_crop,
			has_crop_meta,
			are_both_pools_same
		);

		passthrough = (input_buffer != NULL)
		           && self->inout_info_equal
		           && (output_rotation == IMX_2D_ROTATION_NONE)
		           && are_both_pools_same;

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
	GstFlowReturn flow_ret = GST_FLOW_OK;
	gboolean input_crop;
	Imx2dRegion crop_rectangle;
	Imx2dRotation output_rotation;
	GstBuffer *uploaded_input_buffer = NULL;
	GstBuffer *intermediate_buffer = NULL;
	GstImx2dVideoTransform *self = GST_IMX_2D_VIDEO_TRANSFORM(transform);

	/* Initial checks. */

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

	if (!gst_imx_2d_check_input_buffer_structure(input_buffer, GST_VIDEO_INFO_N_PLANES(&(self->input_video_info))))
		return GST_FLOW_ERROR;


	/* Create local copies of the property values so that we can use them
	 * without risking race conditions if another thread is setting new
	 * values while this function is running. */
	GST_OBJECT_LOCK(self);
	input_crop = self->input_crop;
	output_rotation = self->output_rotation;
	GST_OBJECT_UNLOCK(self);


	GST_LOG_OBJECT(self, "beginning frame transform by uploading input buffer");

	/* Upload the input buffer. The uploader creates a deep
	 * copy if necessary, but tries to avoid that if possible
	 * by passing through the buffer (if it consists purely
	 * of imxdmabuffer backeed gstmemory blocks) or by
	 * duplicating DMA-BUF FDs with dup(). */
	flow_ret = gst_imx_dma_buffer_uploader_perform(self->uploader, input_buffer, &uploaded_input_buffer);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		goto error;


	/* Acquire an intermediate buffer from the internal DMA buffer pool.
	 * If the internal DMA buffer pool and the output video buffer pool
	 * are one and the same, this simply ref output_buffer and returns
	 * it as the intermediate_buffer. All blitter operations are performed
	 * on the intermediate_buffer. */

	flow_ret = gst_imx_2d_video_buffer_pool_acquire_intermediate_buffer(self->video_buffer_pool, output_buffer, &intermediate_buffer);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		goto error;


	/* Set up the input and output surfaces. */

	gst_imx_2d_assign_input_buffer_to_surface(
		input_buffer, uploaded_input_buffer,
		self->input_surface,
		&(self->input_surface_desc),
		&(self->input_video_info)
	);

	imx_2d_surface_set_desc(self->input_surface, &(self->input_surface_desc));

	gst_imx_2d_assign_output_buffer_to_surface(self->output_surface, intermediate_buffer, &(self->output_video_info));


	/* Fill the blit parameters. */

	memset(&blit_params, 0, sizeof(blit_params));
	blit_params.source_region = NULL;
	blit_params.dest_region = NULL;
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


	/* Now perform the actual blit. */

	GST_LOG_OBJECT(self, "beginning blitting procedure to transform the frame");

	if (!imx_2d_blitter_start(self->blitter, self->output_surface))
	{
		GST_ERROR_OBJECT(self, "starting blitter failed");
		goto error;
	}

	if (!imx_2d_blitter_do_blit(self->blitter, self->input_surface, &blit_params))
	{
		GST_ERROR_OBJECT(self, "blitting failed");
		goto error;
	}

	if (!self->passing_through_overlay_meta)
	{
		GST_LOG_OBJECT(self, "will render overlays into frame");
		if (!gst_imx_2d_video_overlay_handler_render(self->overlay_handler, input_buffer))
		{
			GST_ERROR_OBJECT(self, "rendering overlay(s) failed");
			goto error;
		}
	}

	if (!imx_2d_blitter_finish(self->blitter))
	{
		GST_ERROR_OBJECT(self, "finishing blitter failed");
		goto error;
	}


	/* The blitter is done. Transfer the resulting pixels to the output buffer.
	 * If the internal DMA buffer pool and the output video buffer pool are
	 * one and the same, this implies that intermediate_buffer and output_buffer
	 * are the same. Just unref it in that case. Otherwise, if these two pools
	 * are not the same one, then neither are these buffers. Pixels are then
	 * copied from intermediate_buffer to output_buffer. These two pools are
	 * different if downstream can't handle video meta and the blitter requires
	 * stride values / plane offsets that aren't tightly packed. See the
	 * GstImx2dVideoBufferPool documentation for details. */

	if (!gst_imx_2d_video_buffer_pool_transfer_to_output_buffer(self->video_buffer_pool, intermediate_buffer, output_buffer))
	{
		GST_ERROR_OBJECT(self, "could not transfer intermediate buffer contents to output buffer");
		goto error;
	}


	/* Pass through the overlay meta if necessary. */

	if (self->passing_through_overlay_meta)
	{
		GstVideoOverlayCompositionMeta *composition_meta;
		GstVideoOverlayComposition *composition;

		GST_LOG_OBJECT(self, "passing through overlay meta");

		composition_meta = gst_buffer_get_video_overlay_composition_meta(input_buffer);
		composition = composition_meta->overlay;

		gst_buffer_add_video_overlay_composition_meta(output_buffer, composition);
	}


	GST_LOG_OBJECT(self, "blitting procedure finished successfully; frame transform complete");


finish:
	/* Discard the uploaded version of the input buffer. */
	if (uploaded_input_buffer != NULL)
		gst_buffer_unref(uploaded_input_buffer);
	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
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

	if (G_UNLIKELY(self->blitter == NULL))
		return FALSE;

	if (G_UNLIKELY(!gst_imx_video_info_from_caps(&video_info, othercaps, NULL, NULL)))
		return FALSE;

	*othersize = GST_VIDEO_INFO_SIZE(&video_info);

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

	self->passing_through_overlay_meta = FALSE;

	self->video_buffer_pool = NULL;

	self->imx_dma_buffer_allocator = gst_imx_allocator_new();
	if (self->imx_dma_buffer_allocator == NULL)
	{
		GST_ERROR_OBJECT(self, "creating DMA buffer allocator failed");
		goto error;
	}

	self->uploader = gst_imx_dma_buffer_uploader_new(self->imx_dma_buffer_allocator);
	if (self->uploader == NULL)
	{
		GST_ERROR_OBJECT(self, "creating DMA buffer uploader failed");
		goto error;
	}

	/* We call start _after_ the allocator & uploader were
	 * set up in case these might be needed. Currently,
	 * this is not the case, but it may be in the future. */
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

	self->input_surface = imx_2d_surface_create(NULL);
	if (self->input_surface == NULL)
	{
		GST_ERROR_OBJECT(self, "creating input surface failed");
		goto error;
	}

	self->output_surface = imx_2d_surface_create(NULL);
	if (self->output_surface == NULL)
	{
		GST_ERROR_OBJECT(self, "creating output surface failed");
		goto error;
	}

	self->overlay_handler = gst_imx_2d_video_overlay_handler_new(self->uploader, self->blitter);
	if (self->overlay_handler == NULL)
	{
		GST_ERROR_OBJECT(self, "creating overlay handler failed");
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

	if (self->overlay_handler != NULL)
	{
		gst_object_unref(GST_OBJECT(self->overlay_handler));
		self->overlay_handler = NULL;
	}

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

	if (self->video_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->video_buffer_pool));
		self->video_buffer_pool = NULL;
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

	sink_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities_full(capabilities, GST_PAD_SINK, TRUE);
	src_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities_full(capabilities, GST_PAD_SRC, TRUE);

	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_template_caps);

	gst_element_class_add_pad_template(element_class, sink_template);
	gst_element_class_add_pad_template(element_class, src_template);
}
