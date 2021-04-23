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

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gst/imx/common/gstimxdmabufferuploader.h"
#include "gstimxv4l2videosink.h"
#include "gstimxv4l2videoformat.h"
#include "gstimxv4l2context.h"
#include "gstimxv4l2object.h"


GST_DEBUG_CATEGORY_STATIC(imx_v4l2_video_sink_debug);
#define GST_CAT_DEFAULT imx_v4l2_video_sink_debug


enum
{
	PROP_0,
	PROP_DEVICE,
	PROP_NUM_V4L2_BUFFERS
};


#define DEFAULT_DEVICE "/dev/video0"
#define DEFAULT_NUM_V4L2_BUFFERS 4


struct _GstImxV4L2VideoSink
{
	GstVideoSink parent;

	/*< private >*/

	/* Context with the device probing data etc. */
	GstImxV4L2Context *context;

	/* Buffer uploader for incoming data, in case it is delivered in
	 * a form that is unsuitable for our purposes (we need buffers that
	 * use ImxDmaBuffer as memory). */
	GstImxDmaBufferUploader *uploader;

	/* Allocator for the buffer uploader in case it has to create new
	 * buffer to upload data into. */
	GstAllocator *imx_dma_buffer_allocator;

	/* Current video info, derived from the caps passed to the sink
	 * via gst_imx_v4l2_video_sink_set_caps(). */
	GstImxV4L2VideoInfo current_video_info;

	/* Current V4L2 object. This one is created as soon as new caps
	 * arrive and gst_imx_v4l2_video_sink_set_caps() is called. V4L2
	 * objects need to be created with known video info right from the
	 * start, and cannot have its video information reconfigured later.
	 * So, if necessary, we create a new object and unref the old one
	 * (both of these steps are done in gst_imx_v4l2_video_sink_set_caps()). */
	GstImxV4L2Object *current_v4l2_object;
};


struct _GstImxV4L2VideoSinkClass
{
	GstVideoSinkClass parent_class;
};


G_DEFINE_TYPE(GstImxV4L2VideoSink, gst_imx_v4l2_video_sink, GST_TYPE_VIDEO_SINK)


static void gst_imx_v4l2_video_sink_dispose(GObject *object);
static void gst_imx_v4l2_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_v4l2_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstCaps* gst_imx_v4l2_video_sink_get_caps(GstBaseSink *sink, GstCaps *filter);
static gboolean gst_imx_v4l2_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_imx_v4l2_video_sink_start(GstBaseSink *sink);
static gboolean gst_imx_v4l2_video_sink_stop(GstBaseSink *sink);
static gboolean gst_imx_v4l2_video_sink_unlock(GstBaseSink *sink);
static gboolean gst_imx_v4l2_video_sink_unlock_stop(GstBaseSink *sink);

static GstFlowReturn gst_imx_v4l2_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *input_buffer);




static void gst_imx_v4l2_video_sink_class_init(GstImxV4L2VideoSinkClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstBaseSinkClass *base_sink_class;
	GstVideoSinkClass *video_sink_class;
	GstPadTemplate *sink_template;
	GstCaps *sink_template_caps;

	GST_DEBUG_CATEGORY_INIT(imx_v4l2_video_sink_debug, "imxv4l2videosink", 0, "NXP i.MX V4L2 video sink");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_sink_class = GST_BASE_SINK_CLASS(klass);
	video_sink_class = GST_VIDEO_SINK_CLASS(klass);

	sink_template_caps = gst_imx_v4l2_get_all_possible_caps();
	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	gst_element_class_add_pad_template(element_class, sink_template);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_dispose);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_get_property);

	base_sink_class->get_caps = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_get_caps);
	base_sink_class->set_caps = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_set_caps);
	base_sink_class->start = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_start);
	base_sink_class->stop = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_stop);
	base_sink_class->unlock = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_unlock);
	base_sink_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_unlock_stop);

	video_sink_class->show_frame = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_sink_show_frame);

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

	g_object_class_install_property(
		object_class,
		PROP_NUM_V4L2_BUFFERS,
		g_param_spec_int(
			"num-v4l2-buffers",
			"Number of V4L2 buffers",
			"How many V4L2 buffers to request (higher value = more robust against dropouts, "
			"but higher latency and memory usage; not related to GStreamer buffer pool size)",
			2, G_MAXINT,
			DEFAULT_NUM_V4L2_BUFFERS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"NXP i.MX V4L2 video sink",
		"Sink/Video/Hardware",
		"Outputs video frame on NXP i.MX platforms using the Video4Linux2 API",
		"Carlos Rafael Giani <crg7475@mailbox.org>"
	);
}


void gst_imx_v4l2_video_sink_init(GstImxV4L2VideoSink *self)
{
	self->uploader = NULL;
	self->imx_dma_buffer_allocator = NULL;

	self->context = gst_imx_v4l2_context_new(GST_IMX_V4L2_DEVICE_TYPE_OUTPUT);
	g_assert(self->context != NULL);

	gst_imx_v4l2_context_set_device_node(self->context, DEFAULT_DEVICE);
	gst_imx_v4l2_context_set_num_buffers(self->context, DEFAULT_NUM_V4L2_BUFFERS);

	self->current_v4l2_object = NULL;
}


static void gst_imx_v4l2_video_sink_dispose(GObject *object)
{
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(object);

	if (self->context != NULL)
	{
		gst_object_unref(GST_OBJECT(self->context));
		self->context = NULL;
	}

	G_OBJECT_CLASS(gst_imx_v4l2_video_sink_parent_class)->dispose(object);
}


static void gst_imx_v4l2_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_DEVICE:
			GST_OBJECT_LOCK(self->context);
			gst_imx_v4l2_context_set_device_node(self->context, g_value_get_string(value));
			GST_OBJECT_UNLOCK(self->context);
			break;

		case PROP_NUM_V4L2_BUFFERS:
			GST_OBJECT_LOCK(self->context);
			gst_imx_v4l2_context_set_num_buffers(self->context, g_value_get_int(value));
			GST_OBJECT_UNLOCK(self->context);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_v4l2_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_DEVICE:
			GST_OBJECT_LOCK(self->context);
			g_value_set_string(value, gst_imx_v4l2_context_get_device_node(self->context));
			GST_OBJECT_UNLOCK(self->context);
			break;

		case PROP_NUM_V4L2_BUFFERS:
			GST_OBJECT_LOCK(self->context);
			g_value_set_int(value, gst_imx_v4l2_context_get_num_buffers(self->context));
			GST_OBJECT_UNLOCK(self->context);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstCaps* gst_imx_v4l2_video_sink_get_caps(GstBaseSink *sink, GstCaps *filter)
{
	GstCaps *result, *sink_caps;
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(sink);
	gchar const *caps_name;
	GstImxV4L2ProbeResult const *probe_result;

	/* Lock this object's mutex, since get_caps() may be called at the
	 * same time as start(), and the latter is where the device is probed
	 * (meaning that the probe result is created there). */
	GST_OBJECT_LOCK(self->context);

	probe_result = gst_imx_v4l2_context_get_probe_result(self->context);

	caps_name = (probe_result != NULL) ? "available" : "template";

	if (probe_result != NULL)
		sink_caps = gst_caps_ref(probe_result->device_caps);
	else
		sink_caps = gst_pad_get_pad_template_caps(GST_BASE_SINK_PAD(sink));

	if (filter != NULL)
	{
		result = gst_caps_intersect_full(filter, sink_caps, GST_CAPS_INTERSECT_FIRST);
		GST_DEBUG_OBJECT(
			self,
			"responding to get_caps request with caps %" GST_PTR_FORMAT " as a result of intersecting %s caps with filter %" GST_PTR_FORMAT,
			(gpointer)result,
			caps_name,
			(gpointer)(filter)
		);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "responding to get_caps request with %s caps (no filter specified)", caps_name);
		result = gst_caps_ref(sink_caps);
	}

	gst_caps_unref(sink_caps);

	GST_OBJECT_UNLOCK(self->context);

	return result;
}


static gboolean gst_imx_v4l2_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	GstImxV4L2VideoInfo initial_video_info;
	GstImxV4L2Object *v4l2_object = NULL;
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(sink);

	GST_DEBUG_OBJECT(self, "attempting to set caps %" GST_PTR_FORMAT, (gpointer)caps);

	if (!gst_imx_v4l2_video_info_from_caps(&initial_video_info, caps))
	{
		GST_ERROR_OBJECT(self, "could not use caps %" GST_PTR_FORMAT " since they cannot be converted to imxv4l2 video info", (gpointer)caps);
		goto error;
	}

	v4l2_object = gst_imx_v4l2_object_new(self->context, &initial_video_info);
	if (v4l2_object == NULL)
	{
		GST_ERROR_OBJECT(self, "could not create imxv4l2 object");
		goto error;
	}

	/* We own the V4L2 object, so sink the ref. */
	gst_object_ref_sink(GST_OBJECT(v4l2_object));

	/* The video info may have been adjusted by the driver,
	 * so copy the video info back from the V4L2 object. */
	memcpy(&(self->current_video_info), gst_imx_v4l2_object_get_video_info(v4l2_object), sizeof(GstImxV4L2VideoInfo));

	if (self->current_v4l2_object != NULL)
		gst_object_unref(GST_OBJECT(self->current_v4l2_object));
	self->current_v4l2_object = v4l2_object;


	return TRUE;

error:
	return FALSE;
}


static gboolean gst_imx_v4l2_video_sink_start(GstBaseSink *sink)
{
	gboolean retval = TRUE;
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(sink);

	self->imx_dma_buffer_allocator = gst_imx_allocator_new();
	self->uploader = gst_imx_dma_buffer_uploader_new(self->imx_dma_buffer_allocator);

	GST_OBJECT_LOCK(self->context);

	if (!gst_imx_v4l2_context_probe_device(self->context))
		goto error;

finish:
	GST_OBJECT_UNLOCK(self->context);
	return retval;

error:
	gst_imx_v4l2_video_sink_stop(sink);
	retval = FALSE;
	goto finish;
}


static gboolean gst_imx_v4l2_video_sink_stop(GstBaseSink *sink)
{
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(sink);

	if (self->current_v4l2_object != NULL)
	{
		gst_object_unref(GST_OBJECT(self->current_v4l2_object));
		self->current_v4l2_object = NULL;
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

	return TRUE;
}


static gboolean gst_imx_v4l2_video_sink_unlock(GstBaseSink *sink)
{
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(sink);

	if (self->current_v4l2_object != NULL)
		gst_imx_v4l2_object_unlock(self->current_v4l2_object);

	return TRUE;
}


static gboolean gst_imx_v4l2_video_sink_unlock_stop(GstBaseSink *sink)
{
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(sink);

	if (self->current_v4l2_object != NULL)
		gst_imx_v4l2_object_unlock_stop(self->current_v4l2_object);

	return TRUE;
}


static GstFlowReturn gst_imx_v4l2_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *input_buffer)
{
	GstFlowReturn flow_ret;
	GstBuffer *uploaded_input_buffer = NULL;
	GstImxV4L2VideoSink *self = GST_IMX_V4L2_VIDEO_SINK(video_sink);
	gboolean loop = TRUE;

	g_assert(self->current_v4l2_object != NULL);

	GST_LOG_OBJECT(self, "showing video frame");


	/* Upload the input buffer. The uploader creates a deep
	 * copy if necessary, but tries to avoid that if possible
	 * by passing through the buffer (if it consists purely
	 * of imxdmabuffer backend gstmemory blocks) or by
	 * duplicating DMA-BUF FDs with dup(). */
	flow_ret = gst_imx_dma_buffer_uploader_perform(self->uploader, input_buffer, &uploaded_input_buffer);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		return flow_ret;


	while (loop)
	{
		GstBuffer *dequeued_buffer = NULL;

		flow_ret = gst_imx_v4l2_object_queue_buffer(self->current_v4l2_object, uploaded_input_buffer);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		{
			/* Could not output the frame. If this happened because there's
			 * no room for queuing it, try to dequeue a frame to make some
			 * space. Otherwise, consider this an error and exit. */

			switch (flow_ret)
			{
				case GST_IMX_V4L2_FLOW_QUEUE_IS_FULL:
					GST_DEBUG_OBJECT(self, "imxv4l2 object queue is full");
					break;

				default:
					goto error;
			}
		}
		else
		{
			/* Don't keep looping if the output went OK. */
			loop = FALSE;
		}

		flow_ret = gst_imx_v4l2_object_dequeue_buffer(self->current_v4l2_object, &dequeued_buffer);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
			break;

		gst_buffer_unref(dequeued_buffer);
	}


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
