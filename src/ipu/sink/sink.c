/* GStreamer video sink using the Freescale IPU
 * Copyright (C) 2013  Carlos Rafael Giani
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


#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/gstvideometa.h>

#include "sink.h"
#include "../blitter.h"




GST_DEBUG_CATEGORY_STATIC(imx_ipu_sink_debug);
#define GST_CAT_DEFAULT imx_ipu_sink_debug


enum
{
	PROP_0,
	PROP_OUTPUT_ROTATION,
	PROP_INPUT_CROP,
	PROP_DEINTERLACE_MODE
};


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_IMX_IPU_BLITTER_CAPS
);


struct _GstImxIpuSinkPrivate
{
	int framebuffer_fd;
	GstBuffer *fb_buffer;
	GstImxIpuBlitter *blitter;

	GstImxIpuBlitterRotationMode output_rotation;
	gboolean input_crop;
	GstImxIpuBlitterDeinterlaceMode deinterlace_mode;
};


G_DEFINE_TYPE(GstImxIpuSink, gst_imx_ipu_sink, GST_TYPE_VIDEO_SINK)


static GstStateChangeReturn gst_imx_ipu_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_imx_ipu_sink_init_device(GstImxIpuSink *ipu_sink);
static void gst_imx_ipu_sink_uninit_device(GstImxIpuSink *ipu_sink);
static void gst_imx_ipu_sink_finalize(GObject *object);
static void gst_imx_ipu_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_ipu_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static gboolean gst_imx_ipu_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_imx_ipu_propose_allocation(GstBaseSink *sink, GstQuery *query);
static GstFlowReturn gst_imx_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf);




/* required function declared by G_DEFINE_TYPE */

void gst_imx_ipu_sink_class_init(GstImxIpuSinkClass *klass)
{
	GObjectClass *object_class;
	GstBaseSinkClass *base_class;
	GstVideoSinkClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_ipu_sink_debug, "imxipusink", 0, "Freescale i.MX IPU video sink");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_BASE_SINK_CLASS(klass);
	parent_class = GST_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video sink",
		"Sink/Video",
		"Video output using the Freescale IPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);


	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	base_class->set_caps = GST_DEBUG_FUNCPTR(gst_imx_ipu_sink_set_caps);
	base_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_ipu_propose_allocation);
	parent_class->show_frame = GST_DEBUG_FUNCPTR(gst_imx_ipu_sink_show_frame);
	element_class->change_state = GST_DEBUG_FUNCPTR(gst_imx_ipu_sink_change_state);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_ipu_sink_finalize);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_ipu_sink_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_ipu_sink_get_property);

	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_ROTATION,
		g_param_spec_enum(
			"output-rotation",
			"Output rotation",
			"Rotation that shall be applied to output frames",
			gst_imx_ipu_blitter_rotation_mode_get_type(),
			GST_IMX_IPU_BLITTER_OUTPUT_ROTATION_DEFAULT,
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
			GST_IMX_IPU_BLITTER_CROP_DEFAULT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_DEINTERLACE_MODE,
		g_param_spec_enum(
			"deinterlace-mode",
			"Deinterlace mode",
			"Deinterlacing mode to be used for incoming frames (ignored if frames are not interlaced)",
			gst_imx_ipu_blitter_deinterlace_mode_get_type(),
			GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


void gst_imx_ipu_sink_init(GstImxIpuSink *ipu_sink)
{
	ipu_sink->priv = g_slice_alloc(sizeof(GstImxIpuSinkPrivate));
	ipu_sink->priv->framebuffer_fd = -1;
	ipu_sink->priv->blitter = NULL;

	ipu_sink->priv->output_rotation = GST_IMX_IPU_BLITTER_OUTPUT_ROTATION_DEFAULT;
	ipu_sink->priv->input_crop = GST_IMX_IPU_BLITTER_CROP_DEFAULT;
	ipu_sink->priv->deinterlace_mode = GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT;


static GstStateChangeReturn gst_imx_ipu_sink_change_state(GstElement *element, GstStateChange transition)
{
	GstImxIpuSink *ipu_sink = GST_IMX_IPU_SINK(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if (!gst_imx_ipu_sink_init_device(ipu_sink))
			{
				gst_imx_ipu_sink_uninit_device(ipu_sink);
				return GST_STATE_CHANGE_FAILURE;
			}
			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_ipu_sink_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
		{
			gst_imx_ipu_sink_uninit_device(ipu_sink);
			break;
		}

		default:
			break;
	}

	return ret;
}

static gboolean gst_imx_ipu_sink_init_device(GstImxIpuSink *ipu_sink)
{

	g_assert(ipu_sink->priv != NULL);

	ipu_sink->priv->framebuffer_fd = open("/dev/fb0", O_RDWR, 0);
	if (ipu_sink->priv->framebuffer_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/fb0: %s", strerror(errno)), (NULL));
		return FALSE;
	}

	ipu_sink->priv->blitter = g_object_new(gst_imx_ipu_blitter_get_type(), NULL);

	gst_imx_ipu_blitter_set_output_rotation_mode(ipu_sink->priv->blitter, ipu_sink->priv->output_rotation);
	gst_imx_ipu_blitter_enable_crop(ipu_sink->priv->blitter, ipu_sink->priv->input_crop);
	gst_imx_ipu_blitter_set_deinterlace_mode(ipu_sink->priv->blitter, ipu_sink->priv->deinterlace_mode);

	ipu_sink->priv->fb_buffer = gst_imx_ipu_blitter_wrap_framebuffer(ipu_sink->priv->blitter, ipu_sink->priv->framebuffer_fd, 0, 0, 0, 0);
	if (ipu_sink->priv->fb_buffer == NULL)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("wrapping framebuffer in GstBuffer failed"), (NULL));
		return FALSE;
	}

	gst_imx_ipu_blitter_set_output_buffer(ipu_sink->priv->blitter, ipu_sink->priv->fb_buffer);

	return TRUE;
}


static void gst_imx_ipu_sink_uninit_device(GstImxIpuSink *ipu_sink)
{

	if (ipu_sink->priv != NULL)
	{
		if (ipu_sink->priv->fb_buffer != NULL)
			gst_buffer_unref(ipu_sink->priv->fb_buffer);
		if (ipu_sink->priv->blitter != NULL)
			gst_object_unref(ipu_sink->priv->blitter);
		if (ipu_sink->priv->framebuffer_fd >= 0)
			close(ipu_sink->priv->framebuffer_fd);

		ipu_sink->priv->fb_buffer = NULL;
		ipu_sink->priv->blitter = NULL;
		ipu_sink->priv->framebuffer_fd = -1;
	}
}


static void gst_imx_ipu_sink_finalize(GObject *object)
{
	GstImxIpuSink *ipu_sink = GST_IMX_IPU_SINK(object);

	gst_imx_ipu_sink_uninit_device(ipu_sink);
	if (ipu_sink->priv != NULL)
	{
		g_slice_free1(sizeof(GstImxIpuSinkPrivate), ipu_sink->priv);
	}

	G_OBJECT_CLASS(gst_imx_ipu_sink_parent_class)->finalize(object);
}


static void gst_imx_ipu_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxIpuSink *ipu_sink = GST_IMX_IPU_SINK(object);

	switch (prop_id)
	{
		case PROP_OUTPUT_ROTATION:
			ipu_sink->priv->output_rotation = g_value_get_enum(value);
			if (ipu_sink->priv->blitter != NULL)
				gst_imx_ipu_blitter_set_output_rotation_mode(ipu_sink->priv->blitter, ipu_sink->priv->output_rotation);
			break;
		case PROP_INPUT_CROP:
			ipu_sink->priv->input_crop = g_value_get_boolean(value);
			if (ipu_sink->priv->blitter != NULL)
				gst_imx_ipu_blitter_enable_crop(ipu_sink->priv->blitter, ipu_sink->priv->input_crop);
			break;
		case PROP_DEINTERLACE_MODE:
			ipu_sink->priv->deinterlace_mode = g_value_get_enum(value);
			if (ipu_sink->priv->blitter != NULL)
				gst_imx_ipu_blitter_set_deinterlace_mode(ipu_sink->priv->blitter, ipu_sink->priv->deinterlace_mode);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_ipu_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxIpuSink *ipu_sink = GST_IMX_IPU_SINK(object);

	switch (prop_id)
	{
		case PROP_OUTPUT_ROTATION:
			g_value_set_enum(value, ipu_sink->priv->output_rotation);
			break;
		case PROP_INPUT_CROP:
			g_value_set_boolean(value, ipu_sink->priv->input_crop);
			break;
		case PROP_DEINTERLACE_MODE:
			g_value_set_enum(value, ipu_sink->priv->deinterlace_mode);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_ipu_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	GstVideoInfo video_info;
	GstImxIpuSink *ipu_sink = GST_IMX_IPU_SINK(sink);

	gst_video_info_init(&video_info);
	if (!gst_video_info_from_caps(&video_info, caps))
		return FALSE;

	gst_imx_ipu_blitter_set_input_info(ipu_sink->priv->blitter, &video_info);

	return TRUE;
}


static gboolean gst_imx_ipu_propose_allocation(GstBaseSink *sink, GstQuery *query)
{
	GstCaps *caps;
	GstVideoInfo info;
	GstBufferPool *pool;
	guint size;

	gst_query_parse_allocation(query, &caps, NULL);

	if (caps == NULL)
	{
		GST_DEBUG_OBJECT(sink, "no caps specified");
		return FALSE;
	}

	if (!gst_video_info_from_caps(&info, caps))
		return FALSE;

	size = GST_VIDEO_INFO_SIZE(&info);

	if (gst_query_get_n_allocation_pools(query) == 0)
	{
		GstStructure *structure;
		GstAllocationParams params;
		GstAllocator *allocator = NULL;

		memset(&params, 0, sizeof(params));
		params.flags = 0;
		params.align = 15;
		params.prefix = 0;
		params.padding = 0;

		if (gst_query_get_n_allocation_params(query) > 0)
			gst_query_parse_nth_allocation_param(query, 0, &allocator, &params);
		else
			gst_query_add_allocation_param(query, allocator, &params);

		pool = gst_video_buffer_pool_new();

		structure = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_set_params(structure, caps, size, 0, 0);
		gst_buffer_pool_config_set_allocator(structure, allocator, &params);

		if (allocator)
			gst_object_unref(allocator);

		if (!gst_buffer_pool_set_config(pool, structure))
		{
			GST_ERROR_OBJECT(sink, "failed to set config");
			gst_object_unref(pool);
			return FALSE;
		}

		gst_query_add_allocation_pool(query, pool, size, 0, 0);
		gst_object_unref(pool);
		gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
	}

	return TRUE;
}


static GstFlowReturn gst_imx_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf)
{
	gboolean ret;
	GstImxIpuSink *ipu_sink;

	ipu_sink = GST_IMX_IPU_SINK(video_sink);

	if (ipu_sink->priv->fb_buffer == NULL)
	{
		GST_ERROR_OBJECT(ipu_sink, "framebuffer GstBuffer is NULL");
		return GST_FLOW_ERROR;
	}


	/* using early exit optimization here to avoid calls if necessary */
	ret = TRUE;
	ret = ret && gst_imx_ipu_blitter_set_input_buffer(ipu_sink->priv->blitter, buf);
	ret = ret && gst_imx_ipu_blitter_blit(ipu_sink->priv->blitter);


	return ret ? GST_FLOW_OK : GST_FLOW_ERROR;
}

