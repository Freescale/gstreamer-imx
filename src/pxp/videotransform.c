/* PxP-based i.MX video transform class
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


#include "videotransform.h"
#include "blitter.h"




GST_DEBUG_CATEGORY_STATIC(imx_pxp_video_transform_debug);
#define GST_CAT_DEFAULT imx_pxp_video_transform_debug


enum
{
	PROP_0,
	PROP_OUTPUT_ROTATION,
	PROP_INPUT_CROP
};


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_IMX_PXP_BLITTER_SINK_CAPS
);


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_IMX_PXP_BLITTER_SRC_CAPS
);


G_DEFINE_TYPE(GstImxPxPVideoTransform, gst_imx_pxp_video_transform, GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM)


static void gst_imx_pxp_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_pxp_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

gboolean gst_imx_pxp_video_transform_start(GstImxBlitterVideoTransform *blitter_video_transform);
gboolean gst_imx_pxp_video_transform_stop(GstImxBlitterVideoTransform *blitter_video_transform);

gboolean gst_imx_pxp_video_transform_are_video_infos_equal(GstImxBlitterVideoTransform *blitter_video_transform, GstVideoInfo const *in_info, GstVideoInfo const *out_info);

gboolean gst_imx_pxp_video_transform_are_transforms_necessary(GstImxBlitterVideoTransform *blitter_video_transform, GstBuffer *input);




/* required functions declared by G_DEFINE_TYPE */

void gst_imx_pxp_video_transform_class_init(GstImxPxPVideoTransformClass *klass)
{
	GObjectClass *object_class;
	GstImxBlitterVideoTransformClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_pxp_video_transform_debug, "imxpxpvideotransform", 0, "Freescale i.MX PxP video transform");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale PxP video transform",
		"Filter/Converter/Video/Scaler",
		"Video transformation using the PxP API",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_get_property);

	base_class->start = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_start);
	base_class->stop  = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_stop);

	base_class->are_video_infos_equal = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_are_video_infos_equal);
	
	base_class->are_transforms_necessary = GST_DEBUG_FUNCPTR(gst_imx_pxp_video_transform_are_transforms_necessary);

	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_ROTATION,
		g_param_spec_enum(
			"output-rotation",
			"Output rotation",
			"Rotation that shall be applied to output frames",
			gst_imx_pxp_blitter_rotation_mode_get_type(),
			GST_IMX_PXP_BLITTER_OUTPUT_ROTATION_DEFAULT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_INPUT_CROP,
		g_param_spec_boolean(
			"enable-crop",
			"Enable input frame cropping",
			"Whether or not to crop input frames based on their video crop metadata",
			GST_IMX_PXP_BLITTER_CROP_DEFAULT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_imx_pxp_video_transform_init(GstImxPxPVideoTransform *pxp_video_transform)
{
	pxp_video_transform->output_rotation = GST_IMX_PXP_BLITTER_OUTPUT_ROTATION_DEFAULT;
	pxp_video_transform->input_crop = GST_IMX_PXP_BLITTER_CROP_DEFAULT;
}




static void gst_imx_pxp_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxPxPVideoTransform *pxp_video_transform = GST_IMX_PXP_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_OUTPUT_ROTATION:
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(pxp_video_transform);
			pxp_video_transform->output_rotation = g_value_get_enum(value);
			if (pxp_video_transform->blitter != NULL)
				gst_imx_pxp_blitter_set_output_rotation(pxp_video_transform->blitter, pxp_video_transform->output_rotation);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(pxp_video_transform);
			break;

		case PROP_INPUT_CROP:
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(pxp_video_transform);
			pxp_video_transform->input_crop = g_value_get_boolean(value);
			if (pxp_video_transform->blitter != NULL)
				gst_imx_pxp_blitter_enable_crop(pxp_video_transform->blitter, pxp_video_transform->input_crop);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(pxp_video_transform);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_pxp_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxPxPVideoTransform *pxp_video_transform = GST_IMX_PXP_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_OUTPUT_ROTATION:
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(pxp_video_transform);
			g_value_set_enum(value, pxp_video_transform->output_rotation);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(pxp_video_transform);
			break;

		case PROP_INPUT_CROP:
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(pxp_video_transform);
			g_value_set_boolean(value, pxp_video_transform->input_crop);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(pxp_video_transform);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


gboolean gst_imx_pxp_video_transform_start(GstImxBlitterVideoTransform *blitter_video_transform)
{
	GstImxPxPVideoTransform *pxp_video_transform = GST_IMX_PXP_VIDEO_TRANSFORM(blitter_video_transform);
	
	GstImxPxPBlitter *blitter = gst_imx_pxp_blitter_new();
	if (blitter == NULL)
	{
		GST_ERROR_OBJECT(blitter_video_transform, "could not create PxP blitter");
		return FALSE;
	}

	gst_imx_pxp_blitter_set_output_rotation(blitter, pxp_video_transform->output_rotation);
	gst_imx_pxp_blitter_enable_crop(blitter, pxp_video_transform->input_crop);

	gst_imx_blitter_video_transform_set_blitter(blitter_video_transform, GST_IMX_BASE_BLITTER(blitter));

	gst_object_unref(GST_OBJECT(blitter));

	/* no ref necessary, since the base class will clean up *after* any
	 * activity that might use the blitter has been shut down at that point */
	pxp_video_transform->blitter = blitter;

	return TRUE;
}


gboolean gst_imx_pxp_video_transform_stop(G_GNUC_UNUSED GstImxBlitterVideoTransform *blitter_video_transform)
{
	return TRUE;
}


gboolean gst_imx_pxp_video_transform_are_video_infos_equal(G_GNUC_UNUSED GstImxBlitterVideoTransform *blitter_video_transform, GstVideoInfo const *in_info, GstVideoInfo const *out_info)
{
	return
		(GST_VIDEO_INFO_WIDTH(in_info) == GST_VIDEO_INFO_WIDTH(out_info)) &&
		(GST_VIDEO_INFO_HEIGHT(in_info) == GST_VIDEO_INFO_HEIGHT(out_info)) &&
		(GST_VIDEO_INFO_FORMAT(in_info) == GST_VIDEO_INFO_FORMAT(out_info))
		;
}


gboolean gst_imx_pxp_video_transform_are_transforms_necessary(GstImxBlitterVideoTransform *blitter_video_transform, G_GNUC_UNUSED GstBuffer *input)
{
	GstImxPxPVideoTransform *pxp_video_transform = GST_IMX_PXP_VIDEO_TRANSFORM(blitter_video_transform);

	return gst_imx_pxp_blitter_get_output_rotation(pxp_video_transform->blitter) == GST_IMX_PXP_BLITTER_ROTATION_NONE;
}
