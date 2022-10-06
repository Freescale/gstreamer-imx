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
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <gst/allocators/allocators.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimxv4l2videosrc.h"
#include "gstimxv4l2videoformat.h"
#include "gstimxv4l2context.h"
#include "gstimxv4l2object.h"


GST_DEBUG_CATEGORY_STATIC(imx_v4l2_video_src_debug);
#define GST_CAT_DEFAULT imx_v4l2_video_src_debug


enum
{
	PROP_0,
	PROP_DEVICE,
	PROP_NUM_V4L2_BUFFERS
};


#define DEFAULT_DEVICE "/dev/video0"
#define DEFAULT_NUM_V4L2_BUFFERS 4


struct _GstImxV4L2VideoSrc
{
	GstPushSrc parent;

	GstImxV4L2Context *context;

	GstImxV4L2VideoInfo current_video_info;
	GstImxV4L2Object *current_v4l2_object;
	guint calculated_output_buffer_size;
	gint current_framerate[2];
	GstClockTime current_frame_duration;
	GstAllocator *imx_dma_buffer_allocator;
};


struct _GstImxV4L2VideoSrcClass
{
	GstPushSrcClass parent_class;
};


static void gst_imx_v4l2_video_src_uri_handler_init(gpointer iface, gpointer iface_data);


G_DEFINE_TYPE_WITH_CODE(
	GstImxV4L2VideoSrc, gst_imx_v4l2_video_src, GST_TYPE_PUSH_SRC,
	G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, gst_imx_v4l2_video_src_uri_handler_init)
)


static void gst_imx_v4l2_video_src_dispose(GObject *object);
static void gst_imx_v4l2_video_src_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_v4l2_video_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstCaps* gst_imx_v4l2_video_src_get_caps(GstBaseSrc *src, GstCaps *filter);
static gboolean gst_imx_v4l2_video_src_negotiate(GstBaseSrc *src);
static gboolean gst_imx_v4l2_video_src_decide_allocation(GstBaseSrc *src, GstQuery *query);
static gboolean gst_imx_v4l2_video_src_start(GstBaseSrc *src);
static gboolean gst_imx_v4l2_video_src_stop(GstBaseSrc *src);
static gboolean gst_imx_v4l2_video_src_unlock(GstBaseSrc *src);
static gboolean gst_imx_v4l2_video_src_unlock_stop(GstBaseSrc *src);
static gboolean gst_imx_v4l2_video_src_query(GstBaseSrc *src, GstQuery *query);

static GstFlowReturn gst_imx_v4l2_video_src_create(GstPushSrc *src, GstBuffer **buf);

static GstURIType gst_imx_v4l2_video_src_uri_get_type(GType type);
static gchar const * const * gst_imx_v4l2_video_src_uri_get_protocols(GType type);
static gchar* gst_imx_v4l2_video_src_uri_get_uri(GstURIHandler *handler);
static gboolean gst_imx_v4l2_video_src_uri_set_uri(GstURIHandler *handler, const gchar *uri, GError **error);

static GstCaps* gst_imx_v4l2_video_src_fixate_caps(GstImxV4L2VideoSrc *self, GstCaps *negotiated_caps, GstStructure *preferred_values_structure);




static void gst_imx_v4l2_video_src_class_init(GstImxV4L2VideoSrcClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstBaseSrcClass *base_src_class;
	GstPushSrcClass *push_src_class;
	GstPadTemplate *src_template;
	GstCaps *src_template_caps;

	GST_DEBUG_CATEGORY_INIT(imx_v4l2_video_src_debug, "imxv4l2videosrc", 0, "NXP i.MX V4L2 video source");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_src_class = GST_BASE_SRC_CLASS(klass);
	push_src_class = GST_PUSH_SRC_CLASS(klass);

	src_template_caps = gst_imx_v4l2_get_all_possible_caps();
	src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_template_caps);
	gst_element_class_add_pad_template(element_class, src_template);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_dispose);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_get_property);

	base_src_class->get_caps = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_get_caps);
	base_src_class->negotiate = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_negotiate);
	base_src_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_decide_allocation);
	base_src_class->start = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_start);
	base_src_class->stop = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_stop);
	base_src_class->unlock = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_unlock);
	base_src_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_unlock_stop);
	base_src_class->query = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_query);

	push_src_class->create = GST_DEBUG_FUNCPTR(gst_imx_v4l2_video_src_create);

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
			"but higher maximum latency and memory usage; not related to GStreamer buffer pool size)",
			2, G_MAXINT,
			DEFAULT_NUM_V4L2_BUFFERS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"NXP i.MX V4L2 video source",
		"Source/Video/Hardware",
		"Captures video frame on NXP i.MX platforms using the Video4Linux2 API",
		"Carlos Rafael Giani <crg7475@mailbox.org>"
	);
}


void gst_imx_v4l2_video_src_init(GstImxV4L2VideoSrc *self)
{
	gst_base_src_set_format(GST_BASE_SRC(self), GST_FORMAT_TIME);
	gst_base_src_set_live(GST_BASE_SRC(self), TRUE);

	self->context = gst_imx_v4l2_context_new(GST_IMX_V4L2_DEVICE_TYPE_CAPTURE);
	g_assert(self->context != NULL);

	gst_imx_v4l2_context_set_device_node(self->context, DEFAULT_DEVICE);
	gst_imx_v4l2_context_set_num_buffers(self->context, DEFAULT_NUM_V4L2_BUFFERS);

	self->current_v4l2_object = NULL;
}


static void gst_imx_v4l2_video_src_uri_handler_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
	GstURIHandlerInterface *uri_handler_iface = (GstURIHandlerInterface *)iface;

	uri_handler_iface->get_type = gst_imx_v4l2_video_src_uri_get_type;
	uri_handler_iface->get_protocols = gst_imx_v4l2_video_src_uri_get_protocols;
	uri_handler_iface->get_uri = gst_imx_v4l2_video_src_uri_get_uri;
	uri_handler_iface->set_uri = gst_imx_v4l2_video_src_uri_set_uri;
}


static void gst_imx_v4l2_video_src_dispose(GObject *object)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(object);

	if (self->context != NULL)
	{
		gst_object_unref(GST_OBJECT(self->context));
		self->context = NULL;
	}

	G_OBJECT_CLASS(gst_imx_v4l2_video_src_parent_class)->dispose(object);
}


static void gst_imx_v4l2_video_src_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(object);

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


static void gst_imx_v4l2_video_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(object);

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


static GstCaps* gst_imx_v4l2_video_src_get_caps(GstBaseSrc *src, GstCaps *filter)
{
	GstCaps *result, *src_caps;
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);
	gchar const *caps_name;
	GstImxV4L2ProbeResult const *probe_result;

	GST_OBJECT_LOCK(self->context);

	probe_result = gst_imx_v4l2_context_get_probe_result(self->context);

	caps_name = (probe_result != NULL) ? "available" : "template";

	if (probe_result != NULL)
		src_caps = gst_caps_ref(probe_result->device_caps);
	else
		src_caps = gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(src));

	if (filter != NULL)
	{
		result = gst_caps_intersect_full(filter, src_caps, GST_CAPS_INTERSECT_FIRST);
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
		result = gst_caps_ref(src_caps);
	}

	gst_caps_unref(src_caps);

	GST_OBJECT_UNLOCK(self->context);

	return result;
}


static gboolean gst_imx_v4l2_video_src_negotiate(GstBaseSrc *src)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);
	gboolean result = TRUE;
	GstCaps *negotiated_caps = NULL;
	GstCaps *our_caps = NULL;
	GstCaps *peer_caps = NULL;
	GstStructure *preferred_values_structure = NULL;
	GstImxV4L2VideoInfo initial_video_info;
	GstImxV4L2Object *v4l2_object = NULL;

	/* Query the caps our src pad supports. This will issue a caps
	 * query, which will cause our get_caps() function to be called.
	 * In other words, this will give us the caps that we can handle. */
	our_caps = gst_pad_query_caps(GST_BASE_SRC_PAD(src), NULL);
	GST_DEBUG_OBJECT(self, "our caps: %" GST_PTR_FORMAT, (gpointer)our_caps);
	if ((our_caps == NULL) || gst_caps_is_any(our_caps))
	{
		GST_DEBUG_OBJECT(self, "no negotiation needed");
		goto done;
	}

	/* Query the caps the peer pad supports. This will issue a caps
	 * query just like above, giving us the caps the peer can handle. */
	peer_caps = gst_pad_peer_query_caps(GST_BASE_SRC_PAD(src), NULL);
	GST_DEBUG_OBJECT(self, "unfiltered peer caps: %" GST_PTR_FORMAT, (gpointer)peer_caps);

	if ((peer_caps != NULL) && !gst_caps_is_any(peer_caps))
	{
		/* If the peer gave us meaningful caps, do an intersection so
		 * we know what both our pad and the peer pad can handle. */
		negotiated_caps = gst_caps_intersect_full(peer_caps, our_caps, GST_CAPS_INTERSECT_FIRST);
		GST_DEBUG_OBJECT(self, "intersection of peer caps and our caps: %" GST_PTR_FORMAT, (gpointer)negotiated_caps);
	}
	else
	{
		/* Peer did not respond with useful caps. Assume that it can
		 * just handle anything we give it. This means that we do not
		 * intersect any caps, and instead just use our pad's caps.
		 * Ref these caps, since our_caps will be unref'd at the end
		 * of this function _and_ negotiated_caps will be unref'd
		 * by gst_imx_v4l2_video_src_fixate_caps. */
		negotiated_caps = gst_caps_ref(our_caps);
	}

	/* Did not get any useful caps. This typically happens because our
	 * src pad and the peer pad handle completely different caps. */
	if ((negotiated_caps == NULL) || gst_caps_is_empty(negotiated_caps))
		goto could_not_negotiate;

	if ((peer_caps != NULL) && !gst_caps_is_any(peer_caps))
	{
		preferred_values_structure = gst_caps_get_structure(peer_caps, 0);
		GST_DEBUG_OBJECT(self, "using first structure of unfiltered peer caps as the structure containing preferred values");
	}

	/* Fixate negotiated_caps. Note that this unrefs the original
	 * negotiated_caps and returns a fixated version. */
	negotiated_caps = gst_imx_v4l2_video_src_fixate_caps(self, negotiated_caps, preferred_values_structure);
	if (negotiated_caps == NULL)
		goto could_not_negotiate;

	GST_DEBUG_OBJECT(self, "negotiated and fixated caps: %" GST_PTR_FORMAT, (gpointer)negotiated_caps);

	g_assert(gst_caps_is_fixed(negotiated_caps));

	/* Convert the negotiated caps to a GstImxV4L2VideoInfo structure that
	 * we can adjust and then use for creating GstImxV4L2Object instances. */
	if (!gst_imx_v4l2_video_info_from_caps(&initial_video_info, negotiated_caps))
	{
		GST_ERROR_OBJECT(self, "could not use caps %" GST_PTR_FORMAT " since they cannot be converted to imxv4l2 video info", (gpointer)negotiated_caps);
		goto error;
	}

	v4l2_object = gst_imx_v4l2_object_new(
		self->context,
		&initial_video_info,
		GST_IS_DMABUF_ALLOCATOR(self->imx_dma_buffer_allocator)
	);
	if (v4l2_object == NULL)
	{
		GST_ERROR_OBJECT(self, "could not create imxv4l2 object");
		goto error;
	}
	gst_object_ref_sink(GST_OBJECT(v4l2_object));

	/* The video info may have been adjusted by the driver,
	 * so copy the video info back from the V4L2 object.
	 * Also convert it back to caps to propagate those
	 * changes to negotiated_caps, since we need to pass
	 * them to the base class. */
	memcpy(&(self->current_video_info), gst_imx_v4l2_object_get_video_info(v4l2_object), sizeof(GstImxV4L2VideoInfo));

	{
		GstCaps *previous_negotiated_caps = negotiated_caps;

		negotiated_caps = gst_imx_v4l2_video_info_to_caps(&(self->current_video_info));

		GST_DEBUG_OBJECT(self, "negotiated caps before creating the V4L2 object: %" GST_PTR_FORMAT, (gpointer)previous_negotiated_caps);
		GST_DEBUG_OBJECT(self, "                 after creating the V4L2 object: %" GST_PTR_FORMAT, (gpointer)negotiated_caps);

		gst_caps_unref(previous_negotiated_caps);
	}

	/* Now set the negotiated caps, *after* they were recreated from the video info. */
	if (!gst_base_src_set_caps(src, negotiated_caps))
	{
		GST_ERROR_OBJECT(self, "setting caps %" GST_PTR_FORMAT " as srccaps failed", (gpointer)negotiated_caps);
		goto error;
	}

	/* Get the size of a buffer that can hold one frame. This is used in
	 * decide_allocation() and when creating new buffers in create(). */
	self->calculated_output_buffer_size = gst_imx_v4l2_calculate_buffer_size_from_video_info(&(self->current_video_info));
	GST_DEBUG_OBJECT(self, "calculated output buffer size: %u", self->calculated_output_buffer_size);

	switch (self->current_video_info.type)
	{
		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			self->current_framerate[0] = GST_VIDEO_INFO_FPS_N(&(self->current_video_info.info.gst_info));
			self->current_framerate[1] = GST_VIDEO_INFO_FPS_D(&(self->current_video_info.info.gst_info));
			break;

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			self->current_framerate[0] = self->current_video_info.info.bayer_info.fps_n;
			self->current_framerate[1] = self->current_video_info.info.bayer_info.fps_d;
			break;

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
			self->current_framerate[0] = self->current_video_info.info.codec_info.fps_n;
			self->current_framerate[1] = self->current_video_info.info.codec_info.fps_d;
			break;

		default:
			break;
	}

	if ((self->current_framerate[0] > 0) && (self->current_framerate[1] > 0))
	{
		self->current_frame_duration = gst_util_uint64_scale_int(GST_SECOND, self->current_framerate[1], self->current_framerate[0]);
		GST_DEBUG_OBJECT(
			self,
			"computed frame duration %" GST_TIME_FORMAT " out of frame rate %d/%d",
			GST_TIME_ARGS(self->current_frame_duration),
			self->current_framerate[0], self->current_framerate[1]
		);
	}
	else
	{
		GST_DEBUG_OBJECT(
			self,
			"could not compute frame duration out of frame rate %d/%d",
			self->current_framerate[0], self->current_framerate[1]
		);
		self->current_frame_duration = GST_CLOCK_TIME_NONE;
	}

	/* Unref an old V4L2 object if one exists. The old object cannot
	 * be used anymore, since it is configured for different caps. */
	if (self->current_v4l2_object != NULL)
		gst_object_unref(GST_OBJECT(self->current_v4l2_object));

	self->current_v4l2_object = v4l2_object;


done:
	if (negotiated_caps != NULL)
		gst_caps_unref(negotiated_caps);
	if (our_caps != NULL)
		gst_caps_unref(our_caps);
	if (peer_caps != NULL)
		gst_caps_unref(peer_caps);

	return result;


could_not_negotiate:
	GST_DEBUG_OBJECT(self, "did not manage to negotiate usable caps; exiting and returning FALSE");
	goto error;


error:
	if (v4l2_object != NULL)
	{
		gst_object_unref(v4l2_object);
		v4l2_object = NULL;
	}

	result = FALSE;
	goto done;
}


static gboolean gst_imx_v4l2_video_src_decide_allocation(GstBaseSrc *src, GstQuery *query)
{
	GstStructure *pool_config;
	GstCaps *negotiated_caps;
	GstAllocator *selected_allocator = NULL;
	GstAllocationParams allocation_params;
	GstBufferPool *new_buffer_pool = NULL;
	guint buffer_size = 0, min_num_buffers = 0, max_num_buffers = 0;
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);

	GST_TRACE_OBJECT(self, "attempting to decide what buffer pool and allocator to use");

	/* decide_allocation() is always called _after_ negotiate() was run,
	 * meaning that current_video_info will contain information equivalent
	 * to that in the caps in the query. So, we do not actually have to
	 * convert those caps here. We just use them for the buffer pool config. */
	gst_query_parse_allocation(query, &negotiated_caps, NULL);

	/* Select our own allocator. This ensures that we allocate physically
	 * contiguous memory, which is currently a strict requirement.
	 * XXX: See if for non-mxc_v4l2 devices regular virtual memory can be
	 * used in case downstream offers its own custom buffer pool and/or
	 * allocator. */
	gst_allocation_params_init(&allocation_params);
	selected_allocator = self->imx_dma_buffer_allocator;
	gst_object_ref(GST_OBJECT_CAST(self->imx_dma_buffer_allocator));

	/* Create our own buffer pool, and use the calculated buffer size
	 * as its buffer size. This ensures that it allocates DMA memory;
	 * other pools are not required to use the allocators from this query. */
	new_buffer_pool = gst_video_buffer_pool_new();
	GST_DEBUG_OBJECT(
		self,
		"created new video buffer pool, using calculated buffer size %u; new pool %p: %" GST_PTR_FORMAT,
		self->calculated_output_buffer_size,
		(gpointer)new_buffer_pool,
		(gpointer)new_buffer_pool
	);
	min_num_buffers = max_num_buffers = 0;
	buffer_size = self->calculated_output_buffer_size;

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

	/* Make sure our custom buffer pool is picked by setting
	 * it as the first entry in the allocation pool list. */
	if (gst_query_get_n_allocation_pools(query) == 0)
	{
		GST_DEBUG_OBJECT(self, "there are no allocation pools in the allocation query; adding our buffer pool to it");
		gst_query_add_allocation_pool(query, new_buffer_pool, buffer_size, min_num_buffers, max_num_buffers);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "there are allocation pools in the allocation query; setting our buffer pool as the first one in the query");
		gst_query_set_nth_allocation_pool(query, 0, new_buffer_pool, buffer_size, min_num_buffers, max_num_buffers);
	}

	/* Enable the videometa and videoalignment options in the
	 * buffer pool to make sure they get added. */
	pool_config = gst_buffer_pool_get_config(new_buffer_pool);
	gst_buffer_pool_config_set_params(pool_config, negotiated_caps, buffer_size, 0, 0);
	gst_buffer_pool_config_add_option(pool_config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(new_buffer_pool, pool_config);

	/* Unref these, since we passed them to the query. */
	gst_object_unref(GST_OBJECT(selected_allocator));
	gst_object_unref(GST_OBJECT(new_buffer_pool));

	/* Chain up to the base class. */
	return GST_BASE_SRC_CLASS(gst_imx_v4l2_video_src_parent_class)->decide_allocation(src, query);
}


static gboolean gst_imx_v4l2_video_src_start(GstBaseSrc *src)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);

	/* Lock the object mutex, since get_caps() may run concurrently,
	 * and object properties may also be set concurrently. */
	GST_OBJECT_LOCK(self->context);

	self->imx_dma_buffer_allocator = gst_imx_allocator_new();
	if (G_UNLIKELY(self->imx_dma_buffer_allocator == NULL))
		goto error;

	if (!gst_imx_v4l2_context_probe_device(self->context))
		goto error;

	GST_OBJECT_UNLOCK(self->context);
	return TRUE;

error:
	GST_OBJECT_UNLOCK(self->context);
	gst_imx_v4l2_video_src_stop(src);
	return FALSE;
}


static gboolean gst_imx_v4l2_video_src_stop(GstBaseSrc *src)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);

	if (self->current_v4l2_object != NULL)
	{
		gst_object_unref(GST_OBJECT(self->current_v4l2_object));
		self->current_v4l2_object = NULL;
	}

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}

	return TRUE;
}


static gboolean gst_imx_v4l2_video_src_unlock(GstBaseSrc *src)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);

	if (self->current_v4l2_object != NULL)
		gst_imx_v4l2_object_unlock(self->current_v4l2_object);

	return TRUE;
}


static gboolean gst_imx_v4l2_video_src_unlock_stop(GstBaseSrc *src)
{
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);

	if (self->current_v4l2_object != NULL)
		gst_imx_v4l2_object_unlock_stop(self->current_v4l2_object);

	return TRUE;
}


static gboolean gst_imx_v4l2_video_src_query(GstBaseSrc *src, GstQuery *query)
{
	gboolean ret = TRUE;
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);

	switch (GST_QUERY_TYPE(query))
	{		
		case GST_QUERY_LATENCY:
		{
			gint num_buffers = 0;
			GstClockTime min_latency, max_latency;

			GST_TRACE_OBJECT(self, "processing latency query");

			GST_OBJECT_LOCK(self->context);
			num_buffers = gst_imx_v4l2_context_get_num_buffers(self->context);
			GST_OBJECT_UNLOCK(self->context);

			/* Minimum latency equals the time it takes to capture one frame. */
			min_latency = self->current_frame_duration;

			if (!GST_CLOCK_TIME_IS_VALID(min_latency))
			{
				GST_DEBUG_OBJECT(self, "cannot respond to latency query since the configured framerate isn't fixed");
				ret = FALSE;
				break;
			}

			max_latency = num_buffers * min_latency;

			gst_query_set_latency(query, TRUE, min_latency, max_latency);

			break;
		}

		default:
			ret = GST_BASE_SRC_CLASS(gst_imx_v4l2_video_src_parent_class)->query(src, query);
			break;
	}

	return ret;
}


static GstFlowReturn gst_imx_v4l2_video_src_create(GstPushSrc *src, GstBuffer **buf)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(src);
	gboolean loop = TRUE;

	g_assert(self->current_v4l2_object != NULL);

	GST_LOG_OBJECT(self, "producing video frame");

	while (loop)
	{
		GstBuffer *new_buffer = NULL;

		/* Set an initial NULL value to make sure the gst_buffer_replace()
		 * call at the end of the function body always behaves as expected. */
		*buf = NULL;

		/* Dequeue a previously queued buffer. */
		flow_ret = gst_imx_v4l2_object_dequeue_buffer(self->current_v4l2_object, buf);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		{
			switch (flow_ret)
			{
				/* This may happen if either the V4L2 object ran out of queued buffers
				 * (not expected to happen) or the capture is starting up (V4L2 capturing
				 * requires a minimum set of buffers to be queued before the streaming
				 * is enabled). */
				case GST_IMX_V4L2_FLOW_NEEDS_MORE_BUFFERS_QUEUED:
					GST_DEBUG_OBJECT(self, "imxv4l2 object needs more buffers queued");
					break;

				case GST_FLOW_FLUSHING:
					GST_DEBUG_OBJECT(self, "we are flushing; dequeue aborted");
					goto finish;

				default:
					GST_ERROR_OBJECT(self, "error while dequeuing buffer: %s", gst_flow_get_name(flow_ret));
					goto error;
			}
		}
		else
		{
			GstClock *pipeline_clock;
			GstClockTime pipeline_clock_now = GST_CLOCK_TIME_NONE;
			GstClockTime pipeline_base_time;
			GstClockTime current_sysclock_time;
			GstClockTimeDiff capture_delay;
			GstClockTime capture_timestamp;
			GstClockTime final_timestamp;

			capture_timestamp = GST_BUFFER_PTS(*buf);

			/* Note that the buffer returned by gst_imx_v4l2_object_dequeue_buffer()
			 * is NOT unref'd here. That's because at this point, we have the only
			 * reference to it (the other one was unref'd after the previous alloc()
			 * vmethod call was done and that buffer was queued; see the code below
			 * that calls ->new_buffer().) */

			/* The timestamp of the captured buffer needs to be adjusted to account
			 * for the delay between the moment it was captured and the moment it
			 * was dequeued. We can assume that the timestamp from V4L2 is one from
			 * the realtime clock or the monotonic clock. So, by getting that clock's
			 * current time, we just subtract the captured timestamp from the current
			 * timestamp, and we get the delay. Also, for a final timestamp, we also
			 * have to subtract the base-time to translate the timestamp from clock-time
			 * to running-time (which is what the pipeline expects from us). */

			GST_OBJECT_LOCK(self);
			pipeline_clock = GST_ELEMENT_CLOCK(self);
			if (G_LIKELY(pipeline_clock != NULL))
			{
				pipeline_base_time = GST_ELEMENT_CAST(self)->base_time;
				gst_object_ref(GST_OBJECT_CAST(pipeline_clock));
			}
			else
			{
				pipeline_base_time = GST_CLOCK_TIME_NONE;
			}
			GST_OBJECT_UNLOCK(self);

			if (pipeline_clock != NULL)
			{
				pipeline_clock_now = gst_clock_get_time(pipeline_clock);
				gst_object_unref(pipeline_clock);
			}

			if (GST_CLOCK_TIME_IS_VALID(capture_timestamp))
			{
				/* Get the current time of the system clock. That's the clock the driver used
				 * for getting the v4l2_buffer timestamp. Most newer drivers use the monotonic
				 * system clock, while older ones use the realtime clock (mxc_v4l2 does as well).
				 * Try the monotonic clock first, and if using it results in bogus deltas
				 * between its current time and the v4l2 timestamp, try the realtime one.
				 * (This trick is originally from the v4l2src gst_v4l2src_create() vfunc.) */

				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				current_sysclock_time = GST_TIMESPEC_TO_TIME(now);

				if ((capture_timestamp > current_sysclock_time) || (GST_CLOCK_DIFF(capture_timestamp, current_sysclock_time) > (10 * GST_SECOND)))
				{
					clock_gettime(CLOCK_REALTIME, &now);
					current_sysclock_time = GST_TIMESPEC_TO_TIME(now);
				}

				capture_delay = GST_CLOCK_DIFF(capture_timestamp, current_sysclock_time);

				GST_LOG_OBJECT(
					self,
					"captured buffer V4L2 timestamp: %" GST_TIME_FORMAT " current sysclock time: %" GST_TIME_FORMAT " -> capture delay: %" GST_STIME_FORMAT,
					GST_TIME_ARGS(capture_timestamp),
					GST_TIME_ARGS(current_sysclock_time),
					GST_STIME_ARGS(capture_delay)
				);
			}
			else
			{
				/* If there is no V4L2 timestamp, assume a delay of 1 frame,
				 * or set delay to 0 if we don't know the frame duration. */
				if (GST_CLOCK_TIME_IS_VALID(self->current_frame_duration))
					capture_delay = self->current_frame_duration;
				else
					capture_delay = 0;
			}

			if (G_LIKELY(GST_CLOCK_TIME_IS_VALID(pipeline_clock_now)))
			{
				final_timestamp = pipeline_clock_now - pipeline_base_time;

				if ((capture_delay > 0) && (final_timestamp > (GstClockTime)capture_delay))
					final_timestamp -= capture_delay;
				else
					final_timestamp = 0;

				GST_LOG_OBJECT(
					self,
					"pipeline clock time %" GST_TIME_FORMAT " - base time %" GST_TIME_FORMAT " - capture delay -> final timestamp: %" GST_TIME_FORMAT,
					GST_TIME_ARGS(pipeline_clock_now),
					GST_TIME_ARGS(pipeline_base_time),
					GST_TIME_ARGS(final_timestamp)
				);
			}
			else
				final_timestamp = GST_CLOCK_TIME_NONE;

			GST_BUFFER_PTS(*buf) = GST_BUFFER_DTS(*buf) = final_timestamp;
			GST_BUFFER_DURATION(*buf) = self->current_frame_duration;

			/* Not exiting loop right away; instead, we just set loop to FALSE, and
			 * resume the current iteration to queue a new buffer. Otherwise, we can
			 * run out of queued buffers, and V4L2 will miss frames, resulting in
			 * stuttering video. */
			loop = FALSE;
		}

		/* Acquire a new buffer to queue into the V4L2 object. */
		flow_ret = GST_BASE_SRC_CLASS(gst_imx_v4l2_video_src_parent_class)->alloc(GST_BASE_SRC(src), 0, self->calculated_output_buffer_size, &new_buffer);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		{
			/* Only log this if it actually is an error. (Flushing is not an error.) */
			if (flow_ret != GST_FLOW_FLUSHING)
				GST_ERROR_OBJECT(self, "could not allocate buffer for next captured frame: %s", gst_flow_get_name(flow_ret));
			goto error;
		}

		/* Queue the newly acquired buffer. */
		flow_ret = gst_imx_v4l2_object_queue_buffer(self->current_v4l2_object, new_buffer);

		/* Unref new_buffer. That's because at this point, there are *two* references:
		 * The first one is the one that we have here, in form of new_buffer. The
		 * second one is one that gst_imx_v4l2_object_queue_buffer() just created
		 * (by internally ref'ing the buffer). Since we no longer need our reference
		 * here, unref it to avoid a leak. */
		gst_buffer_unref(new_buffer);

		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
			break;
	}

	GST_LOG_OBJECT(self, "produced output buffer: %" GST_PTR_FORMAT, (gpointer)(*buf));

finish:
	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;

	gst_buffer_replace(buf, NULL);
	goto finish;
}


static GstURIType gst_imx_v4l2_video_src_uri_get_type(G_GNUC_UNUSED GType type)
{
	return GST_URI_SRC;
}


static gchar const * const * gst_imx_v4l2_video_src_uri_get_protocols(G_GNUC_UNUSED GType type)
{
	static gchar const *supported_protocols[] =
	{
		"imxv4l2video",
		NULL
	};

	return supported_protocols;
}


static gchar* gst_imx_v4l2_video_src_uri_get_uri(GstURIHandler *handler)
{
	gchar *uri;
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(handler);

	GST_OBJECT_LOCK(self->context);
	uri = g_strdup_printf("%s%s", "imxv4l2video://", gst_imx_v4l2_context_get_device_node(self->context));
	GST_OBJECT_UNLOCK(self->context);

	return uri;
}


static gboolean gst_imx_v4l2_video_src_uri_set_uri(GstURIHandler *handler, const gchar *uri, G_GNUC_UNUSED GError **error)
{
	gboolean retval = TRUE;
	gchar *protocol = NULL, *location = NULL;
	GstImxV4L2VideoSrc *self = GST_IMX_V4L2_VIDEO_SRC(handler);

	protocol = gst_uri_get_protocol(uri);
	if (g_strcmp0(protocol, "imxv4l2video") != 0)
	{
		g_set_error(error, GST_URI_ERROR, GST_URI_ERROR_UNSUPPORTED_PROTOCOL, "invalid protocol \"%s\"", protocol);
		goto error;
	}

	location = gst_uri_get_location(uri);
	if (strlen(location) == 0)
	{
		g_set_error(error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, "URI \"%s\" has empty location", uri);
		goto error;
	}

	GST_OBJECT_LOCK(self->context);
	gst_imx_v4l2_context_set_device_node(self->context, location);
	GST_OBJECT_UNLOCK(self->context);

finish:
	g_free(protocol);
	g_free(location);

	return retval;

error:
	retval = FALSE;
	goto finish;
}


typedef struct
{
	gint preferred_width, preferred_height;
	gint preferred_fps_num, preferred_fps_denom;
}
PreferredCapsData;


static gint compare_fixed_structures(GstStructure *first, GstStructure *second, PreferredCapsData *preferred_caps_data)
{
	/* We compare sizes relative to the preferred sizes.
	 * Sizes closest to the preferred ones "win", that
	 * is, they are considered to come first. */

	gint first_width, first_height;
	gint second_width, second_height;
	gint first_width_delta, first_height_delta;
	gint second_width_delta, second_height_delta;
	gint preferred_width, preferred_height;

	/* If for some reason a structure does not have
	 * width/height fields, append it, and warn about it. */
	if (!gst_structure_get_int(first, "width", &first_width)
	 || !gst_structure_get_int(first, "height", &first_height))
	{
		GST_WARNING("structure %" GST_PTR_FORMAT " has no width or height fields; appending", (gpointer)first);
		return 1;
	}
	if (!gst_structure_get_int(second, "width", &second_width)
	 || !gst_structure_get_int(second, "height", &second_height))
	{
		GST_WARNING("structure %" GST_PTR_FORMAT " has no width or height fields; appending", (gpointer)second);
		return 1;
	}

	preferred_width = preferred_caps_data->preferred_width;
	preferred_height = preferred_caps_data->preferred_height;

	first_width_delta = ABS(first_width - preferred_width);
	first_height_delta = ABS(first_height - preferred_height);
	second_width_delta = ABS(second_width - preferred_width);
	second_height_delta = ABS(second_height - preferred_height);

	if ((first_width_delta < second_width_delta) && (first_height_delta < second_height_delta))
		return -1;
	else
		return 1;
}


static GstCaps* gst_imx_v4l2_video_src_fixate_caps(GstImxV4L2VideoSrc *self, GstCaps *negotiated_caps, GstStructure *preferred_values_structure)
{
	GstStructure *structure;
	GstCaps *fixated_caps = NULL;
	GList *structure_list = NULL;
	guint i;

	/* Start with hardcoded preferences in case downstream does not give us any caps. */
	PreferredCapsData preferred_caps_data = {
		.preferred_width = 1920,
		.preferred_height = 1080,
		.preferred_fps_num = 120,
		.preferred_fps_denom = 1,
	};

	/* Look at preferred_values_structure and pick the preferred
	 * values from there, overwriting the hardcoded ones. */
	if (preferred_values_structure != NULL)
	{
		GST_DEBUG_OBJECT(self, "taking preferred caps out of structure %" GST_PTR_FORMAT, (gpointer)preferred_values_structure);

		/* The preferred_values_structure may not be fixed. */
		structure = gst_structure_copy(preferred_values_structure);

		if (gst_structure_has_field(structure, "width"))
		{
			gst_structure_fixate_field_nearest_int(structure, "width", preferred_caps_data.preferred_width);
			gst_structure_get_int(structure, "width", &(preferred_caps_data.preferred_width));
		}

		if (gst_structure_has_field(structure, "height"))
		{
			gst_structure_fixate_field_nearest_int(structure, "height", preferred_caps_data.preferred_height);
			gst_structure_get_int(structure, "height", &(preferred_caps_data.preferred_height));
		}

		if (gst_structure_has_field(structure, "framerate"))
		{
			gst_structure_fixate_field_nearest_fraction(structure, "framerate", preferred_caps_data.preferred_fps_num, preferred_caps_data.preferred_fps_denom);
			gst_structure_get_fraction(structure, "framerate", &(preferred_caps_data.preferred_fps_num), &(preferred_caps_data.preferred_fps_denom));
		}

		gst_structure_free(structure);
	}

	GST_DEBUG_OBJECT(
		self,
		"preferred caps:  width: %d pixel(s)  height: %d pixel(s)  framerate: %d/%d",
		preferred_caps_data.preferred_width, preferred_caps_data.preferred_height,
		preferred_caps_data.preferred_fps_num, preferred_caps_data.preferred_fps_denom
	);

	/* Insert the structures from the negotiated caps into a list,
	 * and sort while inserting. We want the best match to be
	 * at the beginning of the list. We use a simple sorting
	 * comparison function here. It produces a "best match".
	 * The structures after the first one (= the best one) may
	 * not be sorted optimally, but this is irrelevant here,
	 * since we are only interested in the best one. */
	for (i = 0; i < gst_caps_get_size(negotiated_caps); ++i)
	{
		/* The structures in the negotiated caps may not be sorted.
		 * So, first make a copy of the structure, then make sure
		 * its values are fixated. */

		structure = gst_structure_copy(gst_caps_get_structure(negotiated_caps, i));

		gst_structure_fixate_field_nearest_int(structure, "width", preferred_caps_data.preferred_width);
		gst_structure_fixate_field_nearest_int(structure, "height", preferred_caps_data.preferred_height);
		gst_structure_fixate_field_nearest_fraction(structure, "framerate", preferred_caps_data.preferred_fps_num, preferred_caps_data.preferred_fps_denom);

		gst_structure_fixate(structure);

		GST_DEBUG_OBJECT(self, "inserting fixated caps structure %" GST_PTR_FORMAT, (gpointer)structure);

		structure_list = g_list_insert_sorted_with_data(structure_list, structure, (GCompareDataFunc)compare_fixed_structures, &preferred_caps_data);
	}

	/* We do not expect this list to ever be empty. */
	g_assert(structure_list != NULL);

	/* Pick the first element for our fixated caps. Thanks to
	 * the sorting done above, the first one is the "best" one.
	 * We do not call gst_structure_free() on it, since we trasfer
	 * ownership over it to the caps via gst_caps_new_full(). */
	structure = structure_list->data;
	GST_DEBUG_OBJECT(self, "picked structure for fixated caps: %" GST_PTR_FORMAT, (gpointer)structure);
	fixated_caps = gst_caps_new_full(structure, NULL);
	structure_list = g_list_delete_link(structure_list, structure_list);

	/* Get rid of the other structures in the list. */
	while (structure_list != NULL)
	{
		structure = structure_list->data;
		GST_DEBUG_OBJECT(self, "freeing remaining unused structure: %" GST_PTR_FORMAT, (gpointer)structure);
		gst_structure_free(structure);
		structure_list = g_list_delete_link(structure_list, structure_list);
	}

	/* We no longer need the negotiated caps, since we transformed them
	 * into a fixated version, so unref them here to prevent a leak. */
	gst_caps_unref(negotiated_caps);

	GST_DEBUG_OBJECT(self, "fixated caps: %" GST_PTR_FORMAT, (gpointer)fixated_caps);
	return fixated_caps;
}
