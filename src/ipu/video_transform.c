/* IPU-based i.MX video transform class
 * Copyright (C) 2015  Carlos Rafael Giani
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


#include "video_transform.h"
#include "blitter.h"




GST_DEBUG_CATEGORY_STATIC(imx_ipu_video_transform_debug);
#define GST_CAT_DEFAULT imx_ipu_video_transform_debug


enum
{
	PROP_0,
	PROP_DEINTERLACE
};


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_IMX_IPU_BLITTER_SINK_CAPS
);


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_IMX_IPU_BLITTER_SRC_CAPS
);


G_DEFINE_TYPE(GstImxIpuVideoTransform, gst_imx_ipu_video_transform, GST_TYPE_IMX_BLITTER_VIDEO_TRANSFORM)


static void gst_imx_ipu_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_ipu_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstCaps* gst_imx_ipu_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);

static gboolean gst_imx_ipu_video_transform_start(GstImxBlitterVideoTransform *blitter_video_transform);
static gboolean gst_imx_ipu_video_transform_stop(GstImxBlitterVideoTransform *blitter_video_transform);
static GstImxBlitter* gst_imx_ipu_video_transform_create_blitter(GstImxBlitterVideoTransform *blitter_video_transform);
gboolean gst_imx_ipu_video_transform_are_video_infos_equal(GstImxBlitterVideoTransform *blitter_video_transform, GstVideoInfo const *in_info, GstVideoInfo const *out_info);
gboolean gst_imx_ipu_video_transform_are_transforms_necessary(GstImxBlitterVideoTransform *blitter_video_transform, GstBuffer *input);




/* required functions declared by G_DEFINE_TYPE */

static void gst_imx_ipu_video_transform_class_init(GstImxIpuVideoTransformClass *klass)
{
	GObjectClass *object_class;
	GstBaseTransformClass *base_transform_class;
	GstImxBlitterVideoTransformClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_ipu_video_transform_debug, "imxipuvideotransform", 0, "Freescale i.MX IPU video transform");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BLITTER_VIDEO_TRANSFORM_CLASS(klass);
	base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video transform",
		"Filter/Converter/Video/Scaler",
		"Video transformation using the IPU API",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->set_property           = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_set_property);
	object_class->get_property           = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_get_property);

	base_transform_class->fixate_caps    = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_fixate_caps);

	base_class->start                    = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_start);
	base_class->stop                     = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_stop);
	base_class->create_blitter           = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_create_blitter);
	base_class->are_video_infos_equal    = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_are_video_infos_equal);
	base_class->are_transforms_necessary = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_transform_are_transforms_necessary);

	g_object_class_install_property(
		object_class,
		PROP_DEINTERLACE,
		g_param_spec_boolean(
			"deinterlace",
			"Deinterlace",
			"Whether or not to enable deinterlacing",
			GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_ipu_video_transform_init(GstImxIpuVideoTransform *ipu_video_transform)
{
	ipu_video_transform->blitter = NULL;
	ipu_video_transform->deinterlacing_enabled = GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT;
}



static void gst_imx_ipu_video_transform_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxIpuVideoTransform *ipu_video_transform = GST_IMX_IPU_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_DEINTERLACE:
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(ipu_video_transform);
			ipu_video_transform->deinterlacing_enabled = g_value_get_boolean(value);
			if (ipu_video_transform->blitter != NULL)
				gst_imx_ipu_blitter_enable_deinterlacing((GstImxIpuBlitter *)(ipu_video_transform->blitter), ipu_video_transform->deinterlacing_enabled);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(ipu_video_transform);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_ipu_video_transform_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxIpuVideoTransform *ipu_video_transform = GST_IMX_IPU_VIDEO_TRANSFORM(object);

	switch (prop_id)
	{
		case PROP_DEINTERLACE:
			GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(ipu_video_transform);
			g_value_set_boolean(value, ipu_video_transform->deinterlacing_enabled);
			GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(ipu_video_transform);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_ipu_video_transform_start(GstImxBlitterVideoTransform *blitter_video_transform)
{
	GstImxIpuVideoTransform *ipu_video_transform = GST_IMX_IPU_VIDEO_TRANSFORM(blitter_video_transform);
	GstImxIpuBlitter *blitter;

	if ((blitter = gst_imx_ipu_blitter_new()) == NULL)
	{
		GST_ERROR_OBJECT(blitter_video_transform, "could not create IPU blitter");
		return FALSE;
	}

	ipu_video_transform->blitter = (GstImxBlitter *)gst_object_ref(blitter);
	gst_imx_ipu_blitter_enable_deinterlacing((GstImxIpuBlitter *)(ipu_video_transform->blitter), ipu_video_transform->deinterlacing_enabled);

	return TRUE;
}


static gboolean gst_imx_ipu_video_transform_stop(GstImxBlitterVideoTransform *blitter_video_transform)
{
	GstImxIpuVideoTransform *ipu_video_transform = GST_IMX_IPU_VIDEO_TRANSFORM(blitter_video_transform);
	gst_object_unref(ipu_video_transform->blitter);
	return TRUE;
}


static GstImxBlitter* gst_imx_ipu_video_transform_create_blitter(GstImxBlitterVideoTransform *blitter_video_transform)
{
	GstImxIpuVideoTransform *ipu_video_transform = GST_IMX_IPU_VIDEO_TRANSFORM(blitter_video_transform);
	return ipu_video_transform->blitter;
}


static GstCaps* gst_imx_ipu_video_transform_fixate_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
	gint i, n;
	gboolean no_interlacing;
	GstImxIpuVideoTransform *ipu_video_transform = GST_IMX_IPU_VIDEO_TRANSFORM(transform);
	GstCaps *result_caps = GST_BASE_TRANSFORM_CLASS(gst_imx_ipu_video_transform_parent_class)->fixate_caps(transform, direction, caps, othercaps);

	if (direction == GST_PAD_SINK)
	{
		GST_IMX_BLITTER_VIDEO_TRANSFORM_LOCK(ipu_video_transform);
		no_interlacing = !(ipu_video_transform->deinterlacing_enabled);
		GST_IMX_BLITTER_VIDEO_TRANSFORM_UNLOCK(ipu_video_transform);

		if (no_interlacing)
			return result_caps;

		GST_LOG_OBJECT(transform, "deinterlacing enabled -> adjusting interlace-mode in fixated src caps to \"progressive\"");

		n = gst_caps_get_size(result_caps);
		for (i = 0; i < n; i++)
		{
			GstStructure *structure = gst_caps_get_structure(result_caps, i);
			gst_structure_set(
				structure,
				"interlace-mode", G_TYPE_STRING, "progressive",
				NULL
			);
		}
	}

	return result_caps;
}


gboolean gst_imx_ipu_video_transform_are_video_infos_equal(G_GNUC_UNUSED GstImxBlitterVideoTransform *blitter_video_transform, GstVideoInfo const *in_info, GstVideoInfo const *out_info)
{
	return
		(GST_VIDEO_INFO_WIDTH(in_info) == GST_VIDEO_INFO_WIDTH(out_info)) &&
		(GST_VIDEO_INFO_HEIGHT(in_info) == GST_VIDEO_INFO_HEIGHT(out_info)) &&
		(GST_VIDEO_INFO_FORMAT(in_info) == GST_VIDEO_INFO_FORMAT(out_info))
		;
}


gboolean gst_imx_ipu_video_transform_are_transforms_necessary(GstImxBlitterVideoTransform *blitter_video_transform, GstBuffer *input)
{
	GstImxIpuVideoTransform *ipu_video_transform = GST_IMX_IPU_VIDEO_TRANSFORM(blitter_video_transform);

	if (ipu_video_transform->deinterlacing_enabled)
	{
		switch (GST_VIDEO_INFO_INTERLACE_MODE(GST_IMX_BLITTER_VIDEO_TRANSFORM_INPUT_INFO(ipu_video_transform)))
		{
			case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
				break;

			case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
				GST_DEBUG_OBJECT(blitter_video_transform, "interlace is required in interleaved mode");
				return TRUE;

			case GST_VIDEO_INTERLACE_MODE_MIXED:
			{
				if (GST_BUFFER_FLAG_IS_SET(input, GST_VIDEO_BUFFER_FLAG_INTERLACED))
				{
					GST_DEBUG_OBJECT(blitter_video_transform, "interlace is required in mixed mode, interlacing flag is set");
					return TRUE;
				}
				else
					GST_DEBUG_OBJECT(blitter_video_transform, "interlace is required in mixed mode, but interlacing flag not set");
			}

			default:
				break;
		}
	}

	return FALSE;
}
