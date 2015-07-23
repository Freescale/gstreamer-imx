/* IPU-based i.MX video sink class
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


#include "video_sink.h"
#include "blitter.h"




GST_DEBUG_CATEGORY_STATIC(imx_ipu_video_sink_debug);
#define GST_CAT_DEFAULT imx_ipu_video_sink_debug


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


G_DEFINE_TYPE(GstImxIpuVideoSink, gst_imx_ipu_video_sink, GST_TYPE_IMX_BLITTER_VIDEO_SINK)


static void gst_imx_ipu_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_ipu_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_ipu_video_sink_start(GstImxBlitterVideoSink *blitter_video_sink);
static gboolean gst_imx_ipu_video_sink_stop(GstImxBlitterVideoSink *blitter_video_sink);
static GstImxBlitter* gst_imx_ipu_video_sink_create_blitter(GstImxBlitterVideoSink *blitter_video_sink);




/* required functions declared by G_DEFINE_TYPE */

void gst_imx_ipu_video_sink_class_init(GstImxIpuVideoSinkClass *klass)
{
	GObjectClass *object_class;
	GstImxBlitterVideoSinkClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_ipu_video_sink_debug, "imxipuvideosink", 0, "Freescale i.MX IPU video sink");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BLITTER_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video sink",
		"Sink/Video",
		"Video output using the Freescale IPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_get_property);

	base_class->start          = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_start);
	base_class->stop           = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_stop);
	base_class->create_blitter = GST_DEBUG_FUNCPTR(gst_imx_ipu_video_sink_create_blitter);

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


void gst_imx_ipu_video_sink_init(G_GNUC_UNUSED GstImxIpuVideoSink *ipu_video_sink)
{
	ipu_video_sink->blitter = NULL;
	ipu_video_sink->deinterlacing_enabled = GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT;
}



/* base class functions */

static void gst_imx_ipu_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_DEINTERLACE:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(ipu_video_sink);
			ipu_video_sink->deinterlacing_enabled = g_value_get_boolean(value);
			if (ipu_video_sink->blitter != NULL)
				gst_imx_ipu_blitter_enable_deinterlacing((GstImxIpuBlitter *)(ipu_video_sink->blitter), ipu_video_sink->deinterlacing_enabled);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(ipu_video_sink);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_ipu_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_DEINTERLACE:
			GST_IMX_BLITTER_VIDEO_SINK_LOCK(ipu_video_sink);
			g_value_set_boolean(value, ipu_video_sink->deinterlacing_enabled);
			GST_IMX_BLITTER_VIDEO_SINK_UNLOCK(ipu_video_sink);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static gboolean gst_imx_ipu_video_sink_start(GstImxBlitterVideoSink *blitter_video_sink)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(blitter_video_sink);
	GstImxIpuBlitter *blitter;

	if ((blitter = gst_imx_ipu_blitter_new()) == NULL)
	{
		GST_ERROR_OBJECT(blitter_video_sink, "could not create IPU blitter");
		return FALSE;
	}

	ipu_video_sink->blitter = (GstImxBlitter *)gst_object_ref(blitter);
	gst_imx_ipu_blitter_enable_deinterlacing((GstImxIpuBlitter *)(ipu_video_sink->blitter), ipu_video_sink->deinterlacing_enabled);

	return TRUE;
}


static gboolean gst_imx_ipu_video_sink_stop(GstImxBlitterVideoSink *blitter_video_sink)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(blitter_video_sink);
	gst_object_unref(ipu_video_sink->blitter);
	return TRUE;
}


static GstImxBlitter* gst_imx_ipu_video_sink_create_blitter(GstImxBlitterVideoSink *blitter_video_sink)
{
	GstImxIpuVideoSink *ipu_video_sink = GST_IMX_IPU_VIDEO_SINK(blitter_video_sink);
	return ipu_video_sink->blitter;
}
