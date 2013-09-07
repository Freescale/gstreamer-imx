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




GST_DEBUG_CATEGORY_STATIC(ipusink_debug);
#define GST_CAT_DEFAULT ipusink_debug


static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_FSL_IPU_BLITTER_CAPS
);


struct _GstFslIpuSinkPrivate
{
	int framebuffer_fd;
	GstBuffer *fb_buffer;
	GstFslIpuBlitter *blitter;
	GstVideoFrame fb_output_frame;
};


G_DEFINE_TYPE(GstFslIpuSink, gst_fsl_ipu_sink, GST_TYPE_VIDEO_SINK)


static void gst_fsl_ipu_sink_finalize(GObject *object);
static gboolean gst_fsl_ipu_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_fsl_ipu_propose_allocation(GstBaseSink *sink, GstQuery *query);
static GstFlowReturn gst_fsl_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf);




/* required function declared by G_DEFINE_TYPE */

void gst_fsl_ipu_sink_class_init(GstFslIpuSinkClass *klass)
{
	GObjectClass *object_class;
	GstBaseSinkClass *base_class;
	GstVideoSinkClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(ipusink_debug, "ipusink", 0, "Freescale IPU video sink");

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
	base_class->set_caps = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_set_caps);
	base_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_fsl_ipu_propose_allocation);
	parent_class->show_frame = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_show_frame);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_finalize);
}


void gst_fsl_ipu_sink_init(GstFslIpuSink *ipu_sink)
{
	GstVideoInfo fb_video_info;
	GstVideoMeta *fb_video_meta;

	ipu_sink->priv = g_slice_alloc(sizeof(GstFslIpuSinkPrivate));
	ipu_sink->priv->framebuffer_fd = -1;
	ipu_sink->priv->blitter = NULL;

	ipu_sink->priv->framebuffer_fd = open("/dev/fb0", O_RDWR, 0);
	if (ipu_sink->priv->framebuffer_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/fb0: %s", strerror(errno)), (NULL));
		return;
	}

	ipu_sink->priv->blitter = g_object_new(gst_fsl_ipu_blitter_get_type(), NULL);

	ipu_sink->priv->fb_buffer = gst_fsl_ipu_blitter_wrap_framebuffer(ipu_sink->priv->blitter, ipu_sink->priv->framebuffer_fd, 0, 0, 0, 0);
	if (ipu_sink->priv->fb_buffer == NULL)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("wrapping framebuffer in GstBuffer failed"), (NULL));
		return;
	}

	fb_video_meta = gst_buffer_get_video_meta(ipu_sink->priv->fb_buffer);
	g_assert(fb_video_meta != NULL);
	gst_video_info_init(&fb_video_info);
	gst_video_info_set_format(&fb_video_info, fb_video_meta->format, fb_video_meta->width, fb_video_meta->height);

	if (!gst_video_frame_map(&(ipu_sink->priv->fb_output_frame), &fb_video_info, ipu_sink->priv->fb_buffer, GST_MAP_WRITE))
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not map framebuffer output frame"), (NULL));
	}

	gst_fsl_ipu_blitter_set_output_frame(ipu_sink->priv->blitter, &(ipu_sink->priv->fb_output_frame));
}


static void gst_fsl_ipu_sink_finalize(GObject *object)
{
	GstFslIpuSink *ipu_sink = GST_FSL_IPU_SINK(object);

	gst_video_frame_unmap(&(ipu_sink->priv->fb_output_frame));

	if (ipu_sink->priv != NULL)
	{
		if (ipu_sink->priv->blitter != NULL)
			gst_object_unref(ipu_sink->priv->blitter);
		if (ipu_sink->priv->framebuffer_fd >= 0)
			close(ipu_sink->priv->framebuffer_fd);
		g_slice_free1(sizeof(GstFslIpuSinkPrivate), ipu_sink->priv);
	}

	G_OBJECT_CLASS(gst_fsl_ipu_sink_parent_class)->finalize(object);
}


static gboolean gst_fsl_ipu_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	GstVideoInfo video_info;
	GstFslIpuSink *ipu_sink = GST_FSL_IPU_SINK(sink);

	gst_video_info_init(&video_info);
	if (!gst_video_info_from_caps(&video_info, caps))
		return FALSE;

	gst_fsl_ipu_blitter_set_input_info(ipu_sink->priv->blitter, &video_info);

	return TRUE;
}


static gboolean gst_fsl_ipu_propose_allocation(GstBaseSink *sink, GstQuery *query)
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


static GstFlowReturn gst_fsl_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf)
{
	GstFslIpuSink *ipu_sink;
	GstVideoFrame input_video_frame;

	ipu_sink = GST_FSL_IPU_SINK(video_sink);

	if (ipu_sink->priv->fb_buffer == NULL)
	{
		GST_ERROR_OBJECT(ipu_sink, "framebuffer GstBuffer is NULL");
		return GST_FLOW_ERROR;
	}

	if (!gst_video_frame_map(&input_video_frame, &(ipu_sink->priv->blitter->input_video_info), buf, GST_MAP_READ))
	{
		GST_ERROR_OBJECT(ipu_sink, "could not map incoming input frame");
		return GST_FLOW_ERROR;
	}

	if (!gst_fsl_ipu_blitter_set_incoming_frame(ipu_sink->priv->blitter, &input_video_frame))
		return GST_FLOW_ERROR;

	if (!gst_fsl_ipu_blitter_blit(ipu_sink->priv->blitter))
		return GST_FLOW_ERROR;

	gst_video_frame_unmap(&input_video_frame);

	return GST_FLOW_OK;
}

