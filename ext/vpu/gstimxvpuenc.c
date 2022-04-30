/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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
#include <gst/allocators/allocators.h>
#include <gst/video/gstvideometa.h>
#include <imxdmabuffer/imxdmabuffer.h>
#include <imxdmabuffer/imxdmabuffer_config.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimxvpucommon.h"
#include "gstimxvpuenc.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_debug


/* This is the base class for encoder elements. Derived classes
 * are implemented manually, unlike decoder ones. This is because
 * encoders typically have additional GObject properties that are
 * format specific, so autogenerating these subclasses (as it is
 * done for decoders) would not work.
 *
 * Still, some of the setup is done automatically in the base
 * class. To that end, the libimxvpuapi compression format enum
 * is stored as qdata in the derived classes. That qdata is
 * accessed using gst_imx_vpu_compression_format_quark().
 *
 * Furthermore, encoders do have common parameters, but they
 * differ slightly between formats. One example is the
 * quantization parameter, whose valid range depends on the
 * format. FOr this reason, not all common GObject properties
 * can be added to the GstImxVpuEnc base class, and instead must
 * be added to the subclasses directly. For this reason, there
 * are functions that must be called in the _class_init() and
 * _init() functions of the subclasses. These functions are
 * gst_imx_vpu_enc_common_class_init() and
 * gst_imx_vpu_enc_common_init(). */


enum
{
	PROP_0,
	PROP_GOP_SIZE,
	PROP_BITRATE,
	PROP_QUANTIZATION,
	PROP_INTRA_REFRESH
};


#define DEFAULT_GOP_SIZE          16
#define DEFAULT_BITRATE           0
#define DEFAULT_INTRA_REFRESH     0




#define ENCODING_THREAD_LOCK(imx_vpu_enc) g_mutex_lock(&((imx_vpu_enc)->encoding_thread_mutex))
#define ENCODING_THREAD_UNLOCK(imx_vpu_enc) g_mutex_unlock(&((imx_vpu_enc)->encoding_thread_mutex))
#define ENCODING_THREAD_SIGNAL(imx_vpu_enc) g_cond_broadcast(&((imx_vpu_enc)->encoding_thread_cond))
#define ENCODING_THREAD_WAIT(imx_vpu_enc) g_cond_wait(&((imx_vpu_enc)->encoding_thread_cond), &((imx_vpu_enc)->encoding_thread_mutex))


G_DEFINE_ABSTRACT_TYPE(GstImxVpuEnc, gst_imx_vpu_enc, GST_TYPE_VIDEO_ENCODER)


static void gst_imx_vpu_enc_dispose(GObject *object);
static void gst_imx_vpu_enc_finalize(GObject *object);

static void gst_imx_vpu_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstStateChangeReturn gst_imx_vpu_enc_change_state(GstElement *element, GstStateChange transition);

static gboolean gst_imx_vpu_enc_start(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_stop(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static GstFlowReturn gst_imx_vpu_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *cur_frame);
static GstFlowReturn gst_imx_vpu_enc_finish(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_flush(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query);
static gboolean gst_imx_vpu_enc_sink_event(GstVideoEncoder *encoder, GstEvent *event);

static void gst_imx_vpu_enc_start_encoding_thread(GstImxVpuEnc *imx_vpu_enc);
static gboolean gst_imx_vpu_enc_drain_encoding_thread(GstImxVpuEnc *imx_vpu_enc);
static void gst_imx_vpu_enc_stop_encoding_thread(GstImxVpuEnc *imx_vpu_enc);
static gpointer gst_imx_vpu_enc_encoding_thread(gpointer user_data);

static gboolean gst_imx_vpu_enc_dequeue_and_push_frame(GstImxVpuEnc *imx_vpu_enc);
static gboolean gst_imx_vpu_enc_create_dma_buffer_pool(GstImxVpuEnc *imx_vpu_enc);
static void gst_imx_vpu_enc_free_fb_pool_dmabuffers(GstImxVpuEnc *imx_vpu_enc);
static GstFlowReturn gst_imx_vpu_enc_encode_queued_frames(GstImxVpuEnc *imx_vpu_enc);


static void gst_imx_vpu_enc_class_init(GstImxVpuEncClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstVideoEncoderClass *video_encoder_class;

	gst_imx_vpu_api_setup_logging();

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_debug, "imxvpuenc", 0, "NXP i.MX VPU video encoder");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	video_encoder_class = GST_VIDEO_ENCODER_CLASS(klass);

	object_class->dispose                   = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_dispose);
	object_class->finalize                  = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_finalize);

	element_class->change_state             = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_change_state);

	video_encoder_class->start              = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_start);
	video_encoder_class->stop               = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_stop);
	video_encoder_class->set_format         = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_set_format);
	video_encoder_class->handle_frame       = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_handle_frame);
	video_encoder_class->finish             = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_finish);
	video_encoder_class->flush              = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_flush);
	video_encoder_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_propose_allocation);
	video_encoder_class->sink_event         = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_sink_event);
}


static void gst_imx_vpu_enc_init(GstImxVpuEnc *imx_vpu_enc)
{
	imx_vpu_enc->gop_size = DEFAULT_GOP_SIZE;
	imx_vpu_enc->bitrate = DEFAULT_BITRATE;
	imx_vpu_enc->intra_refresh = DEFAULT_INTRA_REFRESH;

	imx_vpu_enc->stream_buffer = NULL;
	imx_vpu_enc->encoder = NULL;
	imx_vpu_enc->enc_global_info = imx_vpu_api_enc_get_global_info();
	memset(&(imx_vpu_enc->open_params), 0, sizeof(imx_vpu_enc->open_params));
	imx_vpu_enc->default_dma_buf_allocator = NULL;

	imx_vpu_enc->dma_buffer_pool = NULL;
	imx_vpu_enc->uploader = NULL;
	imx_vpu_enc->uploaded_buffers_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)gst_buffer_unref);
	imx_vpu_enc->fb_pool_buffers = NULL;

	imx_vpu_enc->fatal_error_cannot_encode = FALSE;

	imx_vpu_enc->encoding_thread = NULL;
	imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_INACTIVE;
	g_mutex_init(&(imx_vpu_enc->encoding_thread_mutex));
	g_cond_init(&(imx_vpu_enc->encoding_thread_cond));
	imx_vpu_enc->raw_frame_queue = g_queue_new();
}


static void gst_imx_vpu_enc_dispose(GObject *object)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);

	if (imx_vpu_enc->uploaded_buffers_table != NULL)
	{
		g_hash_table_remove_all(imx_vpu_enc->uploaded_buffers_table);
		g_hash_table_unref(imx_vpu_enc->uploaded_buffers_table);
		imx_vpu_enc->uploaded_buffers_table = NULL;
	}

	G_OBJECT_CLASS(gst_imx_vpu_enc_parent_class)->dispose(object);
}


static void gst_imx_vpu_enc_finalize(GObject *object)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);

	g_mutex_clear(&(imx_vpu_enc->encoding_thread_mutex));
	g_cond_clear(&(imx_vpu_enc->encoding_thread_cond));
	g_queue_free(imx_vpu_enc->raw_frame_queue);

	G_OBJECT_CLASS(gst_imx_vpu_enc_parent_class)->finalize(object);
}


static GstStateChangeReturn gst_imx_vpu_enc_change_state(GstElement *element, GstStateChange transition)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(element);

	switch (transition)
	{
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			if (imx_vpu_enc->encoding_thread != NULL)
			{
				/* Change to the STOPPING state to force the encoding
				* thread loop to immediately exit. */
				ENCODING_THREAD_LOCK(imx_vpu_enc);
				imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_STOPPING;
				ENCODING_THREAD_SIGNAL(imx_vpu_enc);
				imx_vpu_api_enc_cancel_encode(imx_vpu_enc->encoder);
				ENCODING_THREAD_UNLOCK(imx_vpu_enc);
			}
			break;
		default:
			break;
	}

	return GST_ELEMENT_CLASS(gst_imx_vpu_enc_parent_class)->change_state (element, transition);
}


static void gst_imx_vpu_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(object));

	switch (prop_id)
	{
		case PROP_GOP_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->gop_size = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_BITRATE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->bitrate = g_value_get_uint(value);
			if (imx_vpu_enc->encoder != NULL)
				imx_vpu_api_enc_set_bitrate(imx_vpu_enc->encoder, imx_vpu_enc->bitrate);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QUANTIZATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->quantization = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->intra_refresh = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		default:
			if (klass->set_encoder_property != NULL)
				klass->set_encoder_property(object, prop_id, value, pspec);
			else
				G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(object));

	switch (prop_id)
	{
		case PROP_GOP_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->gop_size);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_BITRATE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->bitrate);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QUANTIZATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->quantization);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->intra_refresh);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		default:
			if (klass->get_encoder_property != NULL)
				klass->get_encoder_property(object, prop_id, value, pspec);
			else
				G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_vpu_enc_start(GstVideoEncoder *encoder)
{
	gboolean ret = TRUE;
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(encoder);
	size_t stream_buffer_size, stream_buffer_alignment;
	GstAllocationParams alloc_params;
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(encoder);
	GstImxVpuCodecDetails const * codec_details = gst_imx_vpu_get_codec_details(compression_format);

	imx_vpu_enc->fatal_error_cannot_encode = FALSE;

	stream_buffer_size = imx_vpu_enc->enc_global_info->min_required_stream_buffer_size;
	stream_buffer_alignment = imx_vpu_enc->enc_global_info->required_stream_buffer_physaddr_alignment;

	GST_DEBUG_OBJECT(
		imx_vpu_enc,
		"stream buffer info:  required min size: %zu bytes  required alignment: %zu",
		stream_buffer_size,
		stream_buffer_alignment
	);

	memset(&alloc_params, 0, sizeof(alloc_params));
	alloc_params.align = stream_buffer_alignment - 1;

	imx_vpu_enc->default_dma_buf_allocator = gst_imx_allocator_new();

	imx_vpu_enc->uploader = gst_imx_dma_buffer_uploader_new(imx_vpu_enc->default_dma_buf_allocator);

	if (stream_buffer_size > 0)
	{
		imx_vpu_enc->stream_buffer = gst_allocator_alloc(
			imx_vpu_enc->default_dma_buf_allocator,
			stream_buffer_size,
			&alloc_params
		);
		if (G_UNLIKELY(imx_vpu_enc->stream_buffer == NULL))
		{
			GST_ELEMENT_ERROR(imx_vpu_enc, RESOURCE, FAILED, ("could not allocate DMA memory for stream buffer"), (NULL));
			ret = FALSE;
			goto finish;
		}
	}
	else
		GST_DEBUG_OBJECT(imx_vpu_enc, "not allocating stream buffer since the VPU does not need one");

	/* VPU encoder setup continues in set_format(), since we need to
	 * know the input caps to fill the open_params structure. */

	GST_INFO_OBJECT(imx_vpu_enc, "i.MX VPU %s encoder started", codec_details->desc_name);


finish:
	return ret;
}


static gboolean gst_imx_vpu_enc_stop(GstVideoEncoder *encoder)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(encoder);
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(encoder);
	GstImxVpuCodecDetails const * codec_details = gst_imx_vpu_get_codec_details(compression_format);

	/* Immediately stop the encoding thread. Any queued
	 * but not yet encoded frames are discarded. */
	gst_imx_vpu_enc_stop_encoding_thread(imx_vpu_enc);

	g_hash_table_remove_all(imx_vpu_enc->uploaded_buffers_table);

	if (imx_vpu_enc->uploader != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->uploader));
		imx_vpu_enc->uploader = NULL;
	}

	if (imx_vpu_enc->encoder != NULL)
	{
		imx_vpu_api_enc_close(imx_vpu_enc->encoder);
		imx_vpu_enc->encoder = NULL;
	}

	gst_imx_vpu_enc_free_fb_pool_dmabuffers(imx_vpu_enc);

	if (imx_vpu_enc->dma_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->dma_buffer_pool));
		imx_vpu_enc->dma_buffer_pool = NULL;
	}

	if (imx_vpu_enc->stream_buffer != NULL)
	{
		gst_memory_unref(imx_vpu_enc->stream_buffer);
		imx_vpu_enc->stream_buffer = NULL;
	}

	if (imx_vpu_enc->default_dma_buf_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->default_dma_buf_allocator));
		imx_vpu_enc->default_dma_buf_allocator = NULL;
	}

	GST_INFO_OBJECT(imx_vpu_enc, "i.MX VPU %s encoder stopped", codec_details->desc_name);

	return TRUE;
}


static gboolean gst_imx_vpu_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
	ImxVpuApiEncReturnCodes enc_ret;
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(encoder);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(encoder));
	gboolean ret = TRUE;
	GstVideoFormat video_format;
	ImxVpuApiEncOpenParams *open_params = &(imx_vpu_enc->open_params);
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(encoder);
	ImxVpuApiColorFormat color_format;
	GstCaps *output_caps;
	GstVideoCodecState *output_state;

	// TODO: Communicate alignment information from ImxVpuApiEncGlobalInfo to upstream somehow

	g_assert(klass->get_output_caps != NULL);

	GST_DEBUG_OBJECT(encoder, "setting encoder format");


	/* Drain frames that are already encoded but not yet output. */
	GST_DEBUG_OBJECT(encoder, "draining remaining frames from encoder");
	if ((imx_vpu_enc->encoder != NULL) && (gst_imx_vpu_enc_encode_queued_frames(imx_vpu_enc) != GST_FLOW_OK))
	ret = gst_imx_vpu_enc_drain_encoding_thread(imx_vpu_enc);
	gst_imx_vpu_enc_stop_encoding_thread(imx_vpu_enc);
	if (!ret)
	{
		imx_vpu_enc->fatal_error_cannot_encode = TRUE;
		GST_ERROR_OBJECT(imx_vpu_enc, "cannot set new format: draining existing encoder failed");
		goto finish;
	}


	if (imx_vpu_enc->encoder != NULL)
	{
		imx_vpu_api_enc_close(imx_vpu_enc->encoder);
		imx_vpu_enc->encoder = NULL;
	}

	g_hash_table_remove_all(imx_vpu_enc->uploaded_buffers_table);

	gst_imx_vpu_enc_free_fb_pool_dmabuffers(imx_vpu_enc);

	if (imx_vpu_enc->dma_buffer_pool != NULL)
	{
		gst_object_unref(imx_vpu_enc->dma_buffer_pool);
		imx_vpu_enc->dma_buffer_pool = NULL;
	}


	/* Begin filling the open_params. */

	imx_vpu_enc->in_video_info = state->info;

	video_format = GST_VIDEO_INFO_FORMAT(&(state->info));
	if (!gst_imx_vpu_color_format_from_gstvidfmt(&color_format, video_format))
	{
		GST_ERROR_OBJECT(encoder, "unsupported color format %s", gst_video_format_to_string(video_format));
		ret = FALSE;
		goto finish;
	}

	memset(open_params, 0, sizeof(ImxVpuApiEncOpenParams));
	imx_vpu_api_enc_set_default_open_params(
		compression_format,
		color_format,
		GST_VIDEO_INFO_WIDTH(&(state->info)),
		GST_VIDEO_INFO_HEIGHT(&(state->info)),
		open_params
	);

	open_params->frame_rate_numerator = GST_VIDEO_INFO_FPS_N(&(state->info));
	open_params->frame_rate_denominator = GST_VIDEO_INFO_FPS_D(&(state->info));

	GST_OBJECT_LOCK(imx_vpu_enc);
	open_params->bitrate = imx_vpu_enc->bitrate;
	open_params->gop_size = imx_vpu_enc->gop_size;
	open_params->quantization = imx_vpu_enc->quantization;
	open_params->min_intra_refresh_mb_count = imx_vpu_enc->intra_refresh;
	GST_OBJECT_UNLOCK(imx_vpu_enc);

	GST_DEBUG_OBJECT(encoder, "setting bitrate to %u kbps and GOP size to %u", open_params->bitrate, open_params->gop_size);
	GST_DEBUG_OBJECT(encoder, "setting min intra refresh macroblock count to %u", open_params->min_intra_refresh_mb_count);


	/* Let the subclass fill the format specific open params. */
	if ((klass->set_open_params != NULL) && !(klass->set_open_params(imx_vpu_enc, open_params)))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set compression format specific open params");
		ret = FALSE;
		goto finish;
	}


	/* Open and configure encoder. */
	if ((enc_ret = imx_vpu_api_enc_open(
		&(imx_vpu_enc->encoder),
		&(imx_vpu_enc->open_params),
		(imx_vpu_enc->stream_buffer != NULL) ? gst_imx_get_dma_buffer_from_memory(imx_vpu_enc->stream_buffer) : NULL
	)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not open encoder: %s", imx_vpu_api_enc_return_code_string(enc_ret));
		ret = FALSE;
		goto finish;
	}


	/* Retrieve stream info. */
	{
		ImxVpuApiEncStreamInfo const *new_stream_info = imx_vpu_api_enc_get_stream_info(imx_vpu_enc->encoder);
		g_assert(new_stream_info != NULL);
		imx_vpu_enc->current_stream_info = *new_stream_info;
	}


	/* Get output caps from the subclass and set the output state. */

	if ((output_caps = klass->get_output_caps(imx_vpu_enc, &(imx_vpu_enc->current_stream_info))) == NULL)
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not get output caps");
		ret = FALSE;
		goto finish;
	}

	output_state = gst_video_encoder_set_output_state(encoder, output_caps, state);
	gst_video_codec_state_unref(output_state);


	/* Create DMA buffer pool that will be used for the encoder's
	 * framebuffer pool and for internal input buffers. */
	if (!gst_imx_vpu_enc_create_dma_buffer_pool(imx_vpu_enc))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not create DMA buffer pool");
		ret = FALSE;
		goto finish;
	}


	/* Allocate framebuffer pool buffers and register them with the VPU. */
	if (imx_vpu_enc->current_stream_info.min_num_required_framebuffers > 0)
	{
		gsize i;
		gsize num_buffers;
		ImxDmaBuffer **fb_dmabuffers;

		num_buffers = imx_vpu_enc->current_stream_info.min_num_required_framebuffers;
		imx_vpu_enc->fb_pool_buffers = gst_buffer_list_new_sized(num_buffers);

		for (i = 0; i < num_buffers; ++i)
		{
			GstBuffer *buffer = NULL;
			GstFlowReturn flow_ret;

			flow_ret = gst_buffer_pool_acquire_buffer(imx_vpu_enc->dma_buffer_pool, &buffer, NULL);
			if (flow_ret != GST_FLOW_OK)
			{
				GST_ERROR_OBJECT(imx_vpu_enc, "could not acquire DMA buffer: %s", gst_flow_get_name(flow_ret));
				ret = FALSE;
				goto finish;
			}

			gst_buffer_list_add(imx_vpu_enc->fb_pool_buffers, buffer);
		}

		fb_dmabuffers = g_slice_alloc(num_buffers * sizeof(ImxDmaBuffer *));
		for (i = 0; i < num_buffers; ++i)
			fb_dmabuffers[i] = gst_imx_get_dma_buffer_from_buffer(gst_buffer_list_get(imx_vpu_enc->fb_pool_buffers, i));
		enc_ret = imx_vpu_api_enc_add_framebuffers_to_pool(imx_vpu_enc->encoder, fb_dmabuffers, num_buffers);
		g_slice_free1(num_buffers * sizeof(ImxDmaBuffer *), fb_dmabuffers);

		if (enc_ret != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "could not : add framebuffers to VPU pool: %s", imx_vpu_api_enc_return_code_string(enc_ret));
			ret = FALSE;
			goto finish;
		}
	}


	/* The encoder is set up, encoding can begin. */
	gst_imx_vpu_enc_start_encoding_thread(imx_vpu_enc);


finish:
	return ret;
}


static GstFlowReturn gst_imx_vpu_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *cur_frame)
{
	/* In here, we queue cur_frame into raw_frame_queue. The actual
	 * encoding happens in gst_imx_vpu_enc_encoding_thread(). */

	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);
	GstFlowReturn flow_ret;

	/* Sanity checks. */

	if (G_UNLIKELY(imx_vpu_enc->encoder == NULL))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "encoder was not initialized; cannot continue");
		gst_video_codec_frame_unref(cur_frame);
		imx_vpu_enc->fatal_error_cannot_encode = TRUE;
		return GST_FLOW_ERROR;
	}

	if (G_UNLIKELY(imx_vpu_enc->fatal_error_cannot_encode))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "fatal error previously recorded; cannot encode");
		gst_video_codec_frame_unref(cur_frame);
		return GST_FLOW_ERROR;
	}

	if (G_UNLIKELY(cur_frame == NULL))
		return GST_FLOW_OK;

	flow_ret = GST_FLOW_OK;

	ENCODING_THREAD_LOCK(imx_vpu_enc);

	while (TRUE)
	{
		/* We are only supposed to queue frames in the RUNNING state. */

		if (imx_vpu_enc->encoding_thread_state == GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_RUNNING)
		{
			if (g_queue_get_length(imx_vpu_enc->raw_frame_queue) > 2)
			{
				GST_LOG_OBJECT(imx_vpu_enc, "encoded frame queue is full; waiting until there is free space");

				GST_VIDEO_ENCODER_STREAM_UNLOCK(imx_vpu_enc);
				ENCODING_THREAD_WAIT(imx_vpu_enc);
				GST_VIDEO_ENCODER_STREAM_LOCK(imx_vpu_enc);
			}
			else
			{
				GST_LOG_OBJECT(imx_vpu_enc, "encoded frame queue is not full; pushing encoded frame into it");
				g_queue_push_head(imx_vpu_enc->raw_frame_queue, cur_frame);
				ENCODING_THREAD_SIGNAL(imx_vpu_enc);
				break;
			}
		}
		else
		{
			if (imx_vpu_enc->encoding_thread_state == GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED)
			{
				imx_vpu_enc->fatal_error_cannot_encode = TRUE;
				flow_ret = GST_FLOW_ERROR;
			}
			else if (imx_vpu_enc->encoding_thread_state == GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_STOPPING || imx_vpu_enc->encoding_thread_state == GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_INACTIVE)
			{
				flow_ret = GST_FLOW_FLUSHING;
			}

			gst_video_codec_frame_unref(cur_frame);
			break;
		}
	}

	ENCODING_THREAD_UNLOCK(imx_vpu_enc);

	return flow_ret;
}


static GstFlowReturn gst_imx_vpu_enc_finish(GstVideoEncoder *encoder)
{
	GstFlowReturn flow_ret;
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);

	if (imx_vpu_enc->encoder == NULL)
		return GST_FLOW_OK;

	if (G_UNLIKELY(imx_vpu_enc->fatal_error_cannot_encode))
		return GST_FLOW_OK;

	GST_VIDEO_ENCODER_STREAM_UNLOCK(imx_vpu_enc);
	flow_ret = gst_imx_vpu_enc_drain_encoding_thread(imx_vpu_enc) ? GST_FLOW_OK : GST_FLOW_ERROR;
	GST_VIDEO_ENCODER_STREAM_LOCK(imx_vpu_enc);

	if (G_UNLIKELY(flow_ret == GST_FLOW_ERROR))
		imx_vpu_enc->fatal_error_cannot_encode = TRUE;

	return flow_ret;
}


static gboolean gst_imx_vpu_enc_flush(GstVideoEncoder *encoder)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);

	GST_DEBUG_OBJECT(imx_vpu_enc, "flushing encoder");

	if (imx_vpu_enc->encoder != NULL)
	{
		gboolean is_encoding_thread_running = (imx_vpu_enc->encoding_thread != NULL);

		if (is_encoding_thread_running)
		{
			/* Stop the thread. This immediately stop the ongoing encoding
			 * and discards any queued and not yet encoded frames. */
			gst_imx_vpu_enc_stop_encoding_thread(imx_vpu_enc);
		}

		if (imx_vpu_enc->encoding_thread_state == GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED)
		{
			imx_vpu_enc->fatal_error_cannot_encode = TRUE;
			return FALSE;
		}

		if (is_encoding_thread_running)
		{
			/* Restart the thread after the encoder was fully flushed. */
			gst_imx_vpu_enc_start_encoding_thread(imx_vpu_enc);
		}

		imx_vpu_api_enc_flush(imx_vpu_enc->encoder);
	}

	return TRUE;
}


static gboolean gst_imx_vpu_enc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query)
{
	if (!GST_VIDEO_ENCODER_CLASS(gst_imx_vpu_enc_parent_class)->propose_allocation(encoder, query))
		return FALSE;

	/* Inform upstream that we can handle GstVideoMeta. */
	gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);

	return TRUE;
}


static gboolean gst_imx_vpu_enc_sink_event(GstVideoEncoder *encoder, GstEvent *event)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);
	gboolean ret;

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_FLUSH_START:
			if (imx_vpu_enc->encoding_thread != NULL)
			{
				/* Change to the STOPPING state to force the encoding
				* thread loop to immediately exit. */
				ENCODING_THREAD_LOCK(imx_vpu_enc);
				imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_STOPPING;
				ENCODING_THREAD_SIGNAL(imx_vpu_enc);
				ENCODING_THREAD_UNLOCK(imx_vpu_enc);
			}
			break;
		default:
			break;
	}

	ret = GST_VIDEO_ENCODER_CLASS (gst_imx_vpu_enc_parent_class)->sink_event (encoder, event);

	return ret;
}


static gboolean gst_imx_vpu_enc_create_dma_buffer_pool(GstImxVpuEnc *imx_vpu_enc)
{
	GstStructure *pool_config;
	GstAllocationParams alloc_params;
	gboolean ret = TRUE;

	g_assert(imx_vpu_enc->dma_buffer_pool == NULL);

	memset(&alloc_params, 0, sizeof(alloc_params));
	alloc_params.align = imx_vpu_enc->current_stream_info.framebuffer_alignment;
	if (alloc_params.align > 0)
		alloc_params.align--;

	imx_vpu_enc->dma_buffer_pool = gst_buffer_pool_new();

	pool_config = gst_buffer_pool_get_config(imx_vpu_enc->dma_buffer_pool);
	g_assert(pool_config != NULL);
	gst_buffer_pool_config_set_params(pool_config, NULL, imx_vpu_enc->current_stream_info.min_framebuffer_size, 0, 0);
	gst_buffer_pool_config_set_allocator(pool_config, imx_vpu_enc->default_dma_buf_allocator, &alloc_params);
	if (!gst_buffer_pool_set_config(imx_vpu_enc->dma_buffer_pool, pool_config))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set DMA buffer pool configuration");
		goto error;
	}

	if (!gst_buffer_pool_set_active(imx_vpu_enc->dma_buffer_pool, TRUE))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not activate DMA buffer pool");
		goto error;
	}


finish:
	return ret;


error:
	if (imx_vpu_enc->dma_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->dma_buffer_pool));
		imx_vpu_enc->dma_buffer_pool = NULL;
	}

	ret = FALSE;

	goto finish;
}


static void gst_imx_vpu_enc_free_fb_pool_dmabuffers(GstImxVpuEnc *imx_vpu_enc)
{
	if (imx_vpu_enc->fb_pool_buffers != NULL)
	{
		gst_buffer_list_unref(imx_vpu_enc->fb_pool_buffers);
		imx_vpu_enc->fb_pool_buffers = NULL;
	}
}


static GstFlowReturn gst_imx_vpu_enc_encode_queued_frames(GstImxVpuEnc *imx_vpu_enc)
{
	GstVideoEncoder *encoder = GST_VIDEO_ENCODER_CAST(imx_vpu_enc);
	GstFlowReturn flow_ret = GST_FLOW_OK;
	gboolean do_loop = TRUE;
	ImxVpuApiEncReturnCodes enc_ret;
	ImxVpuApiEncOutputCodes output_code;
	size_t encoded_frame_size;

	do_loop = TRUE;

	do
	{
		if (imx_vpu_enc->fatal_error_cannot_encode)
			break;

		GST_TRACE_OBJECT(imx_vpu_enc, "encoding");

		if ((enc_ret = imx_vpu_api_enc_encode(imx_vpu_enc->encoder, &encoded_frame_size, &output_code)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "encoding frames failed: %s", imx_vpu_api_enc_return_code_string(enc_ret));
			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}

		switch (output_code)
		{
			case IMX_VPU_API_ENC_OUTPUT_CODE_NEED_ADDITIONAL_FRAMEBUFFER:
			{
				GstBuffer *buffer = NULL;
				GstFlowReturn flow_ret;
				ImxDmaBuffer *fb_dma_buffer;

				GST_LOG_OBJECT(imx_vpu_enc, "need to acquire additional DMA buffer");

				flow_ret = gst_buffer_pool_acquire_buffer(imx_vpu_enc->dma_buffer_pool, &buffer, NULL);
				if (flow_ret != GST_FLOW_OK)
				{
					GST_ERROR_OBJECT(imx_vpu_enc, "could not acquire DMA buffer: %s", gst_flow_get_name(flow_ret));
					flow_ret = GST_FLOW_ERROR;
					goto finish;
				}

				gst_buffer_list_add(imx_vpu_enc->fb_pool_buffers, buffer);

				fb_dma_buffer = gst_imx_get_dma_buffer_from_buffer(buffer);
				if ((enc_ret = imx_vpu_api_enc_add_framebuffers_to_pool(imx_vpu_enc->encoder, &fb_dma_buffer, 1)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
				{
					GST_ERROR_OBJECT(imx_vpu_enc, "could not add framebuffer to pool: %s", imx_vpu_api_enc_return_code_string(enc_ret));
					flow_ret = GST_FLOW_ERROR;
					goto finish;
				}

				break;
			}

			case IMX_VPU_API_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE:
			{
				guint32 system_frame_number;
				GstMapInfo map_info;
				GstBuffer *output_buffer;
				ImxVpuApiEncodedFrame encoded_frame;
				GstVideoCodecFrame *out_frame;

				if ((output_buffer = gst_video_encoder_allocate_output_buffer(encoder, encoded_frame_size)) == NULL)
				{
					GST_ERROR_OBJECT(imx_vpu_enc, "could not allocate output buffer for encoded frame");
					flow_ret = GST_FLOW_ERROR;
					goto finish;
				}

				gst_buffer_map(output_buffer, &map_info, GST_MAP_WRITE);

				g_assert(map_info.size >= encoded_frame_size);
				memset(&encoded_frame, 0, sizeof(encoded_frame));
				encoded_frame.data = map_info.data;
				encoded_frame.data_size = encoded_frame_size;

				enc_ret = imx_vpu_api_enc_get_encoded_frame(imx_vpu_enc->encoder, &encoded_frame);

				gst_buffer_unmap(output_buffer, &map_info);

				if (enc_ret != IMX_VPU_API_ENC_RETURN_CODE_OK)
				{
					GST_ERROR_OBJECT(imx_vpu_enc, "could not retrieve encoded frame: %s", imx_vpu_api_enc_return_code_string(enc_ret));
					flow_ret = GST_FLOW_ERROR;
					goto finish;
				}

				system_frame_number = (guint32)((guintptr)(encoded_frame.context));
				out_frame = gst_video_encoder_get_frame(encoder, system_frame_number);
				if (G_UNLIKELY(out_frame == NULL))
				{
					GST_WARNING_OBJECT(imx_vpu_enc, "no gstframe exists with number #%" G_GUINT32_FORMAT " - discarding encoded frame", system_frame_number);
					gst_buffer_unref(output_buffer);
					goto finish;
				}
				out_frame->output_buffer = output_buffer;

				flow_ret = gst_video_encoder_finish_frame(encoder, out_frame);

				g_hash_table_remove(imx_vpu_enc->uploaded_buffers_table, (gpointer)(gintptr)system_frame_number);

				break;
			}

			case IMX_VPU_API_ENC_OUTPUT_CODE_MORE_INPUT_DATA_NEEDED:
				GST_LOG_OBJECT(imx_vpu_enc, "VPU has no more data to encode");
				do_loop = FALSE;
				break;

			case IMX_VPU_API_ENC_OUTPUT_CODE_EOS:
				GST_DEBUG_OBJECT(imx_vpu_enc, "VPU reports EOS; no more frames to encode");
				flow_ret = GST_FLOW_EOS;
				do_loop = FALSE;
				break;

			default:
				break;
		}
	}
	while (do_loop);


finish:
	if (flow_ret == GST_FLOW_ERROR)
		imx_vpu_enc->fatal_error_cannot_encode = TRUE;

	return flow_ret;
}


static void gst_imx_vpu_enc_start_encoding_thread(GstImxVpuEnc *imx_vpu_enc)
{
	if (imx_vpu_enc->encoding_thread != NULL)
		return;

	imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_RUNNING;
	imx_vpu_enc->encoding_thread = g_thread_new(
		"ImxVpuEnc",
		gst_imx_vpu_enc_encoding_thread,
		(gpointer)imx_vpu_enc
	);
	g_assert(imx_vpu_enc->encoding_thread != NULL);
}


static gboolean gst_imx_vpu_enc_drain_encoding_thread(GstImxVpuEnc *imx_vpu_enc)
{
	gboolean retval = TRUE;

	if (imx_vpu_enc->encoding_thread == NULL)
		return TRUE;

	ENCODING_THREAD_LOCK(imx_vpu_enc);

	/* No point in draining if the current state isn't RUNNING. */
	if (imx_vpu_enc->encoding_thread_state != GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_RUNNING)
		goto finish;

	GST_DEBUG_OBJECT(imx_vpu_enc, "starting drain");

	/* Change the state to DRAINING and notify the encoding thread. */
	imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_DRAINING;
	ENCODING_THREAD_SIGNAL(imx_vpu_enc);

	GST_DEBUG_OBJECT(imx_vpu_enc, "waiting for drain to complete");

	/* Wait for draining to be completed. */
	while (imx_vpu_enc->encoding_thread_state == GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_DRAINING)
		ENCODING_THREAD_WAIT(imx_vpu_enc);

	retval = (imx_vpu_enc->encoding_thread_state != GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED);

finish:
	ENCODING_THREAD_UNLOCK(imx_vpu_enc);
	return retval;
}


static void gst_imx_vpu_enc_stop_encoding_thread(GstImxVpuEnc *imx_vpu_enc)
{
	if (imx_vpu_enc->encoding_thread == NULL)
		return;

	/* Change to the STOPPING state to force the encoding
	 * thread loop to immediately exit. */
	ENCODING_THREAD_LOCK(imx_vpu_enc);
	imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_STOPPING;
	ENCODING_THREAD_SIGNAL(imx_vpu_enc);
	ENCODING_THREAD_UNLOCK(imx_vpu_enc);

	/* Wait until the loop exits and the encoding thread function ends. */
	g_thread_join(imx_vpu_enc->encoding_thread);

	/* Thread is stopped. Discard it and reset the associated states. */
	g_thread_unref(imx_vpu_enc->encoding_thread);
	imx_vpu_enc->encoding_thread = NULL;
	imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_INACTIVE;

	/* Unref the contents of raw_frame_queue since these
	 * frames will not be encoded anymore. */
	while (!g_queue_is_empty(imx_vpu_enc->raw_frame_queue))
	{
		GstVideoCodecFrame *gst_frame;

		gst_frame = g_queue_pop_tail(imx_vpu_enc->raw_frame_queue);
		gst_video_codec_frame_unref(gst_frame);
	}
}


static gpointer gst_imx_vpu_enc_encoding_thread(gpointer user_data)
{
	gboolean keep_running = TRUE;
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(user_data);

	while (keep_running)
	{
		GstFlowReturn flow_ret;
		gboolean skip_loop = FALSE;
		gboolean draining = FALSE;

		/* Lock the encoding thread mutex since we need to evaluate
		 * the current encoding state. */
		ENCODING_THREAD_LOCK(imx_vpu_enc);

		switch (imx_vpu_enc->encoding_thread_state)
		{
			/* In the RUNNING state, we dequeue frames from the raw_frame_queue and
			 * feed them to the encoder. If the raw_frame_queue is empty, we wait
			 * until the queue is filled with frames or an encoding state change occurs. */
			case GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_RUNNING:
			{
				if (g_queue_is_empty(imx_vpu_enc->raw_frame_queue))
				{
					ENCODING_THREAD_WAIT(imx_vpu_enc);
					/* Skip the rest of the loop to reevaluate the current encoding thread state. */
					skip_loop = TRUE;
				}
				else
				{
					if (!gst_imx_vpu_enc_dequeue_and_push_frame(imx_vpu_enc))
					{
						/* An error occurred. We need to exit the loop. To inform the main
						 * streaming thread about the failure, we set the state to FAILED.
						 * Not signaling here since this will happen anyway below
						 * when leaving the loop. */
						keep_running = FALSE;
						imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED;
					}
					else
					{
						if (imx_vpu_enc->encoding_thread_state != GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_RUNNING)
						{
							/* If the state changed during the gst_imx_vpu_enc_dequeue_and_push_frame()
							 * call, we skip the current loop to reevaluate the state. */
							skip_loop = TRUE;
						}
						else
						{
							/* Signal that there is now room in the encoded frame queue. */
							ENCODING_THREAD_SIGNAL(imx_vpu_enc);
						}
					}
				}

				break;
			}

			/* In the DRAINING state, we first work through all frames in the raw_frame_queue.
			 * Once that queue is empty, we enable the drain mode to force the encoder to encode
			 * any frames that may be in its own internal queue. */
			case GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_DRAINING:
			{
				if (g_queue_is_empty(imx_vpu_enc->raw_frame_queue))
				{
					/* We worked through all the frames in raw_frame_queue.
					 * Now enable the draining mode if not already done so. */
					if (!draining)
					{
						draining = TRUE;
						imx_vpu_api_enc_enable_drain_mode(imx_vpu_enc->encoder);
					}
				}
				else
				{
					/* We still need to process frames in the raw_frame_queue
					 * before we can enable the drain mode. */
					if (!gst_imx_vpu_enc_dequeue_and_push_frame(imx_vpu_enc))
					{
						keep_running = FALSE;
						imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED;
						/* An error occurred. We need to exit the loop. To inform the main
						 * streaming thread about the failure, we set the state to FAILED.
						 * Not signaling here since this will happen anyway below
						 * when leaving the loop. */
					}
					else
					{
						if (imx_vpu_enc->encoding_thread_state != GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_DRAINING)
						{
							/* If the state changed during the gst_imx_vpu_enc_dequeue_and_push_frame()
							 * call, we skip the current loop to reevaluate the state. */
							skip_loop = TRUE;
						}
					}
				}

				break;
			}

			/* In this state, the encoding loop is supposed to be stopped immediately. */
			case GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_STOPPING:
			{
				keep_running = FALSE;
				break;
			}

			/* Similarly to STOPPING, we must stop the loop immediately in this state. */
			case GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED:
			{
				keep_running = FALSE;
				break;
			}

			/* Should not occur.. */
			default:
				keep_running = FALSE;
				g_assert_not_reached();
				break;
		}

		ENCODING_THREAD_UNLOCK(imx_vpu_enc);

		if (skip_loop)
		{
			GST_TRACE_OBJECT(imx_vpu_enc, "skipping loop");
			continue;
		}

		if (!keep_running)
		{
			GST_DEBUG_OBJECT(imx_vpu_enc, "stopping thread");
			break;
		}

		/* Perform the actual frame encoding. This function blocks until
		 * the VPU ran out of data to encode or an error occurred. In the
		 * DRAINING state, the VPU does not really "run out of data";
		 * rather, it just encodes all remaining data. Consequently,
		 * once this function call finishes successfully, we can consider
		 * the encoder to be fully drained. */
		flow_ret = gst_imx_vpu_enc_encode_queued_frames(imx_vpu_enc);

		switch (flow_ret)
		{
			case GST_FLOW_FLUSHING:
			case GST_FLOW_EOS:
				keep_running = FALSE;
				ENCODING_THREAD_LOCK(imx_vpu_enc);
				imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_STOPPING;
				ENCODING_THREAD_UNLOCK(imx_vpu_enc);
				break;

			case GST_FLOW_OK:
				break;

			default:
				keep_running = FALSE;
				ENCODING_THREAD_LOCK(imx_vpu_enc);
				imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED;
				ENCODING_THREAD_UNLOCK(imx_vpu_enc);
				break;
		}

		/* As mentioned above, in DRAINING mode, only one call to
		 * gst_imx_vpu_enc_encode_queued_frames() occurs, since one call
		 * is enough to fully drain the encoder.. */
		if (draining)
		{
			ENCODING_THREAD_LOCK(imx_vpu_enc);

			if (imx_vpu_enc->encoding_thread_state != GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_FAILED)
			{
				draining = FALSE;
				/* XXX: Not undoing drain mode here because drain mode is only
				 * ever done in finish(), which does not require the encoder
				 * to continue working. */
				imx_vpu_enc->encoding_thread_state = GST_IMX_VPU_ENC_ENCODING_THREAD_STATE_STOPPING;
				GST_DEBUG_OBJECT(imx_vpu_enc, "drain complete");
				ENCODING_THREAD_SIGNAL(imx_vpu_enc);
			}

			ENCODING_THREAD_UNLOCK(imx_vpu_enc);
		}
	}

	ENCODING_THREAD_SIGNAL(imx_vpu_enc);

	return NULL;
}


static gboolean gst_imx_vpu_enc_dequeue_and_push_frame(GstImxVpuEnc *imx_vpu_enc)
{
	/* This must be called with the encoding thread mutex locked. */

	GstVideoCodecFrame *gst_frame;
	ImxDmaBuffer *fb_dma_buffer = NULL;
	ImxVpuApiRawFrame raw_frame;
	ImxVpuApiEncReturnCodes enc_ret;
	GstBuffer *uploaded_input_buffer;
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(imx_vpu_enc));

	g_assert(!g_queue_is_empty(imx_vpu_enc->raw_frame_queue));
	gst_frame = g_queue_pop_tail(imx_vpu_enc->raw_frame_queue);

	/* Unlock the mutex here since we won't touch the encoding state
	 * after this point until this function finishes. Since the
	 * imx_vpu_api_enc_push_raw_frame() may block for a little
	 * while, we unlock the mutex to allow other threads to fill the
	 * raw_frame_queue in the meantime. */
	ENCODING_THREAD_UNLOCK(imx_vpu_enc);

	GST_LOG_OBJECT(imx_vpu_enc, "about to prepare and queue frame with number #%" G_GUINT32_FORMAT " for encoding", gst_frame->system_frame_number);

	flow_ret = gst_imx_dma_buffer_uploader_perform(imx_vpu_enc->uploader, gst_frame->input_buffer, &uploaded_input_buffer);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
			goto finish;

	g_hash_table_insert(imx_vpu_enc->uploaded_buffers_table, (gpointer)(gintptr)(gst_frame->system_frame_number), uploaded_input_buffer);

	if (gst_buffer_n_memory(uploaded_input_buffer) == 1)
	{
		fb_dma_buffer = gst_imx_get_dma_buffer_from_buffer(uploaded_input_buffer);

		g_assert(fb_dma_buffer != NULL);

		raw_frame.fb_dma_buffer = fb_dma_buffer;
		raw_frame.frame_types[0] = raw_frame.frame_types[1] = IMX_VPU_API_FRAME_TYPE_UNKNOWN;
		raw_frame.pts = gst_frame->pts;
		raw_frame.dts = gst_frame->dts;
		/* The system frame number is necessary to correctly associate encoded
		 * frames and decoded frames. This is required, because some formats
		 * have a delay (= output frames only show up after N complete input
		 * frames), and others like h.264 even reorder frames. */
		raw_frame.context = (void *)((guintptr)(gst_frame->system_frame_number));

		if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(gst_frame))
		{
			GST_LOG_OBJECT(imx_vpu_enc, "force-keyframe flag set; forcing VPU to encode this frame as an %s frame", klass->use_idr_frame_type_for_keyframes ? "IDR" : "I");
			raw_frame.frame_types[0] = klass->use_idr_frame_type_for_keyframes ? IMX_VPU_API_FRAME_TYPE_IDR : IMX_VPU_API_FRAME_TYPE_I;
		}

		/* The actual encoding */
		if ((enc_ret = imx_vpu_api_enc_push_raw_frame(imx_vpu_enc->encoder, &raw_frame)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "could not push raw frame into encoder: %s", imx_vpu_api_enc_return_code_string(enc_ret));

			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}
	}
	else
	{
		GstMemory *memory;

		memory = gst_buffer_peek_memory(uploaded_input_buffer, 0);
		fb_dma_buffer = gst_imx_get_dma_buffer_from_memory(memory);
		g_assert(fb_dma_buffer != NULL);

		raw_frame.fb_dma_buffer = fb_dma_buffer;
		raw_frame.frame_types[0] = raw_frame.frame_types[1] = IMX_VPU_API_FRAME_TYPE_UNKNOWN;
		raw_frame.pts = gst_frame->pts;
		raw_frame.dts = gst_frame->dts;
		/* The system frame number is necessary to correctly associate encoded
		 * frames and decoded frames. This is required, because some formats
		 * have a delay (= output frames only show up after N complete input
		 * frames), and others like h.264 even reorder frames. */
		raw_frame.context = (void *)((guintptr)(gst_frame->system_frame_number));

		memory = gst_buffer_peek_memory(uploaded_input_buffer, 1);
		fb_dma_buffer = gst_imx_get_dma_buffer_from_memory(memory);
		g_assert(fb_dma_buffer != NULL);

		if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(gst_frame))
		{
			GST_LOG_OBJECT(imx_vpu_enc, "force-keyframe flag set; forcing VPU to encode this frame as an %s frame", klass->use_idr_frame_type_for_keyframes ? "IDR" : "I");
			raw_frame.frame_types[0] = klass->use_idr_frame_type_for_keyframes ? IMX_VPU_API_FRAME_TYPE_IDR : IMX_VPU_API_FRAME_TYPE_I;
		}

		/* The actual encoding */
		if ((enc_ret = imx_vpu_api_enc_push_raw_frame_2(imx_vpu_enc->encoder, &raw_frame, &fb_dma_buffer)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "could not push raw frame into encoder: %s", imx_vpu_api_enc_return_code_string(enc_ret));

			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}
	}

	/* The GstVideoCodecFrame passed to handle_frame() gets ref'd prior
	 * to that call. Since we don't pass it directly to finish_frame()
	 * here (because we aren't done with it yet), we have to unref it
	 * here. We'll pull the frame from the GstVideoEncoder queue based
	 * on its system frame number later, and then we finish it.
	 * (We explicitely unref it here, even though the code below unrefs
	 * it as well if it is non-NULL. That's because this way, it is
	 * ensured that it is unref'd *before* encoding queued frames, thus
	 * making sure that buffers with encoded data are finished as soon
	 * as possible once downstream are done with them.) */
	gst_video_codec_frame_unref(gst_frame);
	gst_frame = NULL;

finish:
	if (gst_frame != NULL)
		gst_video_codec_frame_unref(gst_frame);

	/* Re-lock the mutex since this function was called with the lock held. */
	ENCODING_THREAD_LOCK(imx_vpu_enc);
	return (flow_ret == GST_FLOW_OK);
}


void gst_imx_vpu_enc_common_class_init(GstImxVpuEncClass *klass, ImxVpuApiCompressionFormat compression_format, gboolean with_rate_control, gboolean with_constant_quantization, gboolean with_gop_support, gboolean with_intra_refresh)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstPadTemplate *sink_template;
	GstPadTemplate *src_template;
	GstCaps *sink_template_caps;
	GstCaps *src_template_caps;
	gboolean got_caps;
	gchar *longname;
	gchar *classification;
	gchar *description;
	gchar *author;
	GstImxVpuCodecDetails const *codec_details;
	ImxVpuApiCompressionFormatSupportDetails const *format_support_details;

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	codec_details = gst_imx_vpu_get_codec_details(compression_format);
	format_support_details = imx_vpu_api_enc_get_compression_format_support_details(compression_format);

	g_type_set_qdata(G_OBJECT_CLASS_TYPE(klass), gst_imx_vpu_compression_format_quark(), (gpointer *)compression_format);

	got_caps = gst_imx_vpu_get_caps_for_format(compression_format, format_support_details, &src_template_caps, &sink_template_caps, TRUE);
	g_assert(got_caps);

	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_template_caps);

	gst_element_class_add_pad_template(element_class, sink_template);
	gst_element_class_add_pad_template(element_class, src_template);

	klass->use_idr_frame_type_for_keyframes = FALSE;

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_get_property);

	if (with_gop_support)
	{
		g_object_class_install_property(
			object_class,
			PROP_GOP_SIZE,
			g_param_spec_uint(
				"gop-size",
				"Group-of-picture size",
				"How many frames a group-of-picture shall contain",
				0, 32767,
				DEFAULT_GOP_SIZE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
	}
	if (with_rate_control)
	{
		g_object_class_install_property(
			object_class,
			PROP_BITRATE,
			g_param_spec_uint(
				"bitrate",
				"Bitrate",
				with_constant_quantization ? "Bitrate to use, in kbps (0 = no rate control; constant quality mode is used)" : "Bitrate to use, in kbps",
				with_constant_quantization ? 0 : 1, G_MAXUINT,
				DEFAULT_BITRATE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
	}
	if (with_constant_quantization)
	{
		g_object_class_install_property(
			object_class,
			PROP_QUANTIZATION,
			g_param_spec_uint(
				"quantization",
				"Quantization",
				with_rate_control ? "Constant quantization factor to use if rate control is disabled (meaning, bitrate is set to 0)" : "Constant quantization factor to use",
				format_support_details->min_quantization, format_support_details->max_quantization,
				gst_imx_vpu_get_default_quantization(format_support_details),
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
	}
	if (with_intra_refresh)
	{
		g_object_class_install_property(
			object_class,
			PROP_INTRA_REFRESH,
			g_param_spec_uint(
				"intra-refresh",
				"Intra Refresh",
				"Minimum number of MBs to encode as intra MB",
				0, G_MAXUINT, DEFAULT_INTRA_REFRESH,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
	}

	longname = g_strdup_printf("i.MX VPU %s video encoder", codec_details->desc_name);
	classification = g_strdup("Codec/Encoder/Video/Hardware");
	description = g_strdup_printf("Hardware-accelerated %s video encoding using the i.MX VPU codec", codec_details->desc_name);
	author = g_strdup("Carlos Rafael Giani <crg7475@mailbox.org>");
	gst_element_class_set_metadata(element_class, longname, classification, description, author);
	g_free(longname);
	g_free(classification);
	g_free(description);
	g_free(author);
}


void gst_imx_vpu_enc_common_init(GstImxVpuEnc *imx_vpu_enc)
{
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(imx_vpu_enc);

	imx_vpu_enc->quantization = gst_imx_vpu_get_default_quantization(imx_vpu_api_enc_get_compression_format_support_details(compression_format));
}
