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
#include "gst/imx/gstimxdmabufferallocator.h"
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
	PROP_QUANTIZATION
};


#define DEFAULT_GOP_SIZE          16
#define DEFAULT_BITRATE           0




/* Internal miniobject for wrapping physical memory blocks whose
 * allocator isn't derived from GstImxDmaBufferAllocator. */

typedef struct _GstImxPhysMemWrapper GstImxPhysMemWrapper;

struct _GstImxPhysMemWrapper
{
	GstMiniObject mini_object;
	GstBuffer *phys_mem_gstbuffer;
	ImxWrappedDmaBuffer wrapped_dma_buffer;
};


static inline GstImxPhysMemWrapper* gst_imx_phys_mem_wrapper_ref(GstImxPhysMemWrapper *imx_phys_mem_wrapper)
{
	return (GstImxPhysMemWrapper *)gst_mini_object_ref(GST_MINI_OBJECT_CAST(imx_phys_mem_wrapper));
}


static inline void gst_imx_phys_mem_wrapper_unref(GstImxPhysMemWrapper *imx_phys_mem_wrapper)
{
	gst_mini_object_unref(GST_MINI_OBJECT_CAST(imx_phys_mem_wrapper));
}


GType gst_imx_phys_mem_wrapper_api_get_type(void)
{
	static volatile GType type;
	static gchar const *tags[] = { "resource", "imx", "wrapper", "physmem", NULL };

	if (g_once_init_enter(&type))
	{
		GType _type = gst_meta_api_type_register("GstImxPhysMemWrapperAPI", tags);
		g_once_init_leave(&type, _type);
	}

	return type;
}


static void gst_imx_phys_mem_wrapper_free(GstImxPhysMemWrapper *imx_phys_mem_wrapper)
{
	if (G_UNLIKELY(imx_phys_mem_wrapper->phys_mem_gstbuffer == NULL))
		return;

	gst_buffer_unref(imx_phys_mem_wrapper->phys_mem_gstbuffer);

	g_slice_free1(sizeof(GstImxPhysMemWrapper), imx_phys_mem_wrapper);
}


static void gst_imx_phys_mem_wrapper_init(GstImxPhysMemWrapper *imx_phys_mem_wrapper, GstBuffer *phys_mem_gstbuffer)
{
	GstMemory *phys_memory;

	gst_mini_object_init(
		GST_MINI_OBJECT_CAST(imx_phys_mem_wrapper),
		0,
		gst_imx_phys_mem_wrapper_api_get_type(),
		NULL,
		NULL,
		(GstMiniObjectFreeFunction)gst_imx_phys_mem_wrapper_free
	);

	phys_memory = gst_buffer_peek_memory(imx_phys_mem_wrapper->phys_mem_gstbuffer, 0);

	imx_phys_mem_wrapper->phys_mem_gstbuffer = gst_buffer_ref(phys_mem_gstbuffer);
	imx_dma_buffer_init_wrapped_buffer(&(imx_phys_mem_wrapper->wrapped_dma_buffer));
	imx_phys_mem_wrapper->wrapped_dma_buffer.fd = -1;
	imx_phys_mem_wrapper->wrapped_dma_buffer.physical_address = gst_phys_memory_get_phys_addr(phys_memory);
	imx_phys_mem_wrapper->wrapped_dma_buffer.size = phys_memory->size;
}


static GstImxPhysMemWrapper* gst_imx_phys_mem_wrapper_new(GstBuffer *phys_mem_gstbuffer)
{
	GstImxPhysMemWrapper *obj;

	g_assert(phys_mem_gstbuffer != NULL);
	g_assert(gst_buffer_n_memory(phys_mem_gstbuffer) > 0);

	obj = g_slice_alloc(sizeof(GstImxPhysMemWrapper));
	gst_imx_phys_mem_wrapper_init(obj, phys_mem_gstbuffer);

	return obj;
}




G_DEFINE_ABSTRACT_TYPE(GstImxVpuEnc, gst_imx_vpu_enc, GST_TYPE_VIDEO_ENCODER)


static void gst_imx_vpu_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_vpu_enc_start(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_stop(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static GstFlowReturn gst_imx_vpu_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *cur_frame);
static GstFlowReturn gst_imx_vpu_enc_finish(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_flush(GstVideoEncoder *encoder);

static gboolean gst_imx_vpu_enc_create_dma_buffer_pool(GstImxVpuEnc *imx_vpu_enc);
static void gst_imx_vpu_enc_free_fb_pool_dmabuffers(GstImxVpuEnc *imx_vpu_enc);
static GstFlowReturn gst_imx_vpu_enc_encode_queued_frames(GstImxVpuEnc *imx_vpu_enc);
static gboolean gst_imx_vpu_enc_verify_phys_buffer(GstImxVpuEnc *imx_vpu_enc, GstBuffer *buffer);


static void gst_imx_vpu_enc_class_init(GstImxVpuEncClass *klass)
{
	GstVideoEncoderClass *video_encoder_class;

	gst_imx_vpu_api_setup_logging();

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_debug, "imxvpuenc", 0, "NXP i.MX VPU video encoder");

	video_encoder_class = GST_VIDEO_ENCODER_CLASS(klass);

	video_encoder_class->start              = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_start);
	video_encoder_class->stop               = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_stop);
	video_encoder_class->set_format         = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_set_format);
	video_encoder_class->handle_frame       = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_handle_frame);
	video_encoder_class->finish             = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_finish);
	video_encoder_class->flush              = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_flush);
}


static void gst_imx_vpu_enc_init(GstImxVpuEnc *imx_vpu_enc)
{
	imx_vpu_enc->gop_size = DEFAULT_GOP_SIZE;
	imx_vpu_enc->bitrate = DEFAULT_BITRATE;

	imx_vpu_enc->stream_buffer = NULL;
	imx_vpu_enc->encoder = NULL;
	imx_vpu_enc->enc_global_info = imx_vpu_api_enc_get_global_info();
	memset(&(imx_vpu_enc->open_params), 0, sizeof(imx_vpu_enc->open_params));
	imx_vpu_enc->default_dma_buf_allocator = NULL;

	imx_vpu_enc->dma_buffer_pool = NULL;
	imx_vpu_enc->temp_mini_objects = g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL, (GDestroyNotify)gst_mini_object_unref);
	imx_vpu_enc->fb_pool_buffers = NULL;

	imx_vpu_enc->fatal_error_cannot_encode = FALSE;
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

	GST_DEBUG_OBJECT(imx_vpu_enc, "removing %u temp mini object(s) from hash table", (guint)(g_hash_table_size(imx_vpu_enc->temp_mini_objects)));
	g_hash_table_remove_all(imx_vpu_enc->temp_mini_objects);

	if (imx_vpu_enc->encoder != NULL)
	{
		imx_vpu_api_enc_close(imx_vpu_enc->encoder);
		imx_vpu_enc->encoder = NULL;
	}

	gst_imx_vpu_enc_free_fb_pool_dmabuffers(imx_vpu_enc);

	if (imx_vpu_enc->dma_buffer_pool != NULL)
	{
		gst_object_unref(imx_vpu_enc->dma_buffer_pool);
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


	if (imx_vpu_enc->encoder != NULL)
	{
		imx_vpu_api_enc_close(imx_vpu_enc->encoder);
		imx_vpu_enc->encoder = NULL;
	}

	g_hash_table_remove_all(imx_vpu_enc->temp_mini_objects);

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
	GST_OBJECT_UNLOCK(imx_vpu_enc);

	GST_DEBUG_OBJECT(encoder, "setting bitrate to %u kbps and GOP size to %u", open_params->bitrate, open_params->gop_size);


	/* Let the subclass fill the format specific open params. */
	if ((klass->set_open_params != NULL) && !(klass->set_open_params(imx_vpu_enc, open_params)))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set compression format specific open params");
		ret = FALSE;
		goto finish;
	}


	/* Open and configure encoder. */
	if ((enc_ret = imx_vpu_api_enc_open(&(imx_vpu_enc->encoder), &(imx_vpu_enc->open_params), gst_imx_get_dma_buffer_from_memory(imx_vpu_enc->stream_buffer)) != IMX_VPU_API_ENC_RETURN_CODE_OK))
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


finish:
	return ret;
}


static GstFlowReturn gst_imx_vpu_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *cur_frame)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);
	GstFlowReturn flow_ret = GST_FLOW_OK;

	if (G_UNLIKELY(imx_vpu_enc->encoder == NULL))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "encoder was not initialized; cannot continue");
		flow_ret = GST_FLOW_ERROR;
		goto finish;
	}

	if (G_UNLIKELY(imx_vpu_enc->fatal_error_cannot_encode))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "fatal error previously recorded; cannot encode");
		flow_ret = GST_FLOW_ERROR;
		goto finish;
	}

	flow_ret = GST_FLOW_OK;

	if (G_LIKELY(cur_frame != NULL))
	{
		ImxDmaBuffer *fb_dma_buffer = NULL;
		ImxVpuApiRawFrame raw_frame;
		ImxVpuApiEncReturnCodes enc_ret;
		GstBuffer *input_buffer = cur_frame->input_buffer;

		{
			GstMemory *memory;

			if (gst_buffer_n_memory(input_buffer) == 0)
			{
				GST_ERROR_OBJECT(imx_vpu_enc, "input buffer has no memory blocks");
				flow_ret = GST_FLOW_ERROR;
				goto finish;
			}

			memory = gst_buffer_peek_memory(input_buffer, 0);

			if (gst_imx_is_imx_dma_buffer_memory(memory))
			{
				fb_dma_buffer = gst_imx_get_dma_buffer_from_buffer(input_buffer);
			}
			else
			{
				gint64 sys_frame_num = cur_frame->system_frame_number;

				if (gst_is_phys_memory(memory))
				{
					GST_LOG_OBJECT(imx_vpu_enc, "input buffer is physically contiguous but was not created by a GstImxDmaBufferAllocator; checking if its memory can be used directly");

					if (gst_imx_vpu_enc_verify_phys_buffer(imx_vpu_enc, input_buffer))
					{
						GstImxPhysMemWrapper* wrapper;

						wrapper = gst_imx_phys_mem_wrapper_new(input_buffer);
						g_hash_table_insert(imx_vpu_enc->temp_mini_objects, &sys_frame_num, wrapper);
						fb_dma_buffer = (ImxDmaBuffer *)(&(wrapper->wrapped_dma_buffer));
					}
				}

				if (fb_dma_buffer == NULL)
				{
					gboolean ret;
					GstBuffer *temp_input_buffer = NULL;

					GstVideoFrame orig_in_video_frame, temp_in_video_frame;

					GST_LOG_OBJECT(imx_vpu_enc, "input buffer is not physically contiguous; creating temporary copy");

					if ((flow_ret = gst_buffer_pool_acquire_buffer(imx_vpu_enc->dma_buffer_pool, &temp_input_buffer, NULL)) != GST_FLOW_OK)
					{
						GST_ERROR_OBJECT(imx_vpu_enc, "could not acquire temporary input buffer: %s", gst_flow_get_name(flow_ret));
						flow_ret = GST_FLOW_ERROR;
						goto finish;
					}

					if (!gst_video_frame_map(&orig_in_video_frame, &(imx_vpu_enc->in_video_info), input_buffer, GST_MAP_READ))
					{
						GST_ERROR_OBJECT(imx_vpu_enc, "could not map input frame");
						flow_ret = GST_FLOW_ERROR;
						goto finish;
					}

					if (!gst_video_frame_map(&temp_in_video_frame, &(imx_vpu_enc->in_video_info), temp_input_buffer, GST_MAP_WRITE))
					{
						gst_video_frame_unmap(&orig_in_video_frame);
						GST_ERROR_OBJECT(imx_vpu_enc, "could not map temporary input frame");
						flow_ret = GST_FLOW_ERROR;
						goto finish;
					}

					ret = gst_video_frame_copy(&temp_in_video_frame, &orig_in_video_frame);

					gst_video_frame_unmap(&temp_in_video_frame);
					gst_video_frame_unmap(&orig_in_video_frame);

					if (!ret)
					{
						GST_ERROR_OBJECT(imx_vpu_enc, "could not make temporary copy of input frame");
						flow_ret = GST_FLOW_ERROR;
						goto finish;
					}

					g_hash_table_insert(imx_vpu_enc->temp_mini_objects, &sys_frame_num, temp_input_buffer);
					fb_dma_buffer = gst_imx_get_dma_buffer_from_buffer(temp_input_buffer);
				}
			}
		}

		g_assert(fb_dma_buffer != NULL);

		raw_frame.fb_dma_buffer = fb_dma_buffer;
		raw_frame.frame_types[0] = raw_frame.frame_types[1] = IMX_VPU_API_FRAME_TYPE_UNKNOWN;
		raw_frame.pts = cur_frame->pts;
		raw_frame.dts = cur_frame->dts;
		/* The system frame number is necessary to correctly associate encoded
		 * frames and decoded frames. This is required, because some formats
		 * have a delay (= output frames only show up after N complete input
		 * frames), and others like h.264 even reorder frames. */
		raw_frame.context = (void *)((guintptr)(cur_frame->system_frame_number));

		/* The actual encoding */
		if ((enc_ret = imx_vpu_api_enc_push_raw_frame(imx_vpu_enc->encoder, &raw_frame)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "could not push raw frame into encoder: %s", imx_vpu_api_enc_return_code_string(enc_ret));

			flow_ret = GST_FLOW_ERROR;
			goto finish;
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
		gst_video_codec_frame_unref(cur_frame);
		cur_frame = NULL;
	}

	flow_ret = gst_imx_vpu_enc_encode_queued_frames(imx_vpu_enc);


finish:
	if (cur_frame != NULL)
		gst_video_codec_frame_unref(cur_frame);

	if (flow_ret == GST_FLOW_ERROR)
		imx_vpu_enc->fatal_error_cannot_encode = TRUE;

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

	imx_vpu_api_enc_enable_drain_mode(imx_vpu_enc->encoder);

	GST_INFO_OBJECT(imx_vpu_enc, "pushing out all remaining unfinished frames");

	flow_ret = gst_imx_vpu_enc_encode_queued_frames(imx_vpu_enc);
	if (flow_ret == GST_FLOW_EOS)
		flow_ret = GST_FLOW_OK;

	return flow_ret;
}


static gboolean gst_imx_vpu_enc_flush(GstVideoEncoder *encoder)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);

	if (imx_vpu_enc->encoder != NULL)
		imx_vpu_api_enc_flush(imx_vpu_enc->encoder);

	return TRUE;
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

				{
					gint64 sys_frame_num = system_frame_number;
					g_hash_table_remove(imx_vpu_enc->temp_mini_objects, &sys_frame_num);
				}

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


static gboolean gst_imx_vpu_enc_verify_phys_buffer(GstImxVpuEnc *imx_vpu_enc, GstBuffer *buffer)
{
	/* First, check that the buffer's physical address is aligned properly. */
	if (imx_vpu_enc->current_stream_info.framebuffer_alignment > 1)
	{
		GstMemory *memory = gst_buffer_peek_memory(buffer, 0);
		imx_physical_address_t physical_address = gst_phys_memory_get_phys_addr(memory);
		size_t phys_address_alignment = imx_vpu_enc->current_stream_info.framebuffer_alignment;

		if ((imx_vpu_enc->current_stream_info.framebuffer_alignment & (phys_address_alignment - 1)) != 0)
		{
			GST_LOG_OBJECT(imx_vpu_enc, "buffer has physical address %" IMX_PHYSICAL_ADDRESS_FORMAT " which is not aligned to %zu byte; copying frame with CPU", physical_address, phys_address_alignment);
			return FALSE;
		}
	}

	/* Next, check the offsets and strides of the planes in the frame. */
	{
		GstVideoMeta *video_meta = gst_buffer_get_video_meta(buffer);
		gboolean has_video_meta = (video_meta != NULL);
		GstVideoInfo *video_info = &(imx_vpu_enc->in_video_info);
		guint num_planes;
		guint expected_plane_strides[3];
		gsize expected_plane_offsets[3];
		guint i;

		num_planes = has_video_meta ? video_meta->n_planes : GST_VIDEO_INFO_N_PLANES(video_info);

		expected_plane_strides[0] = imx_vpu_enc->current_stream_info.frame_encoding_framebuffer_metrics.y_stride;
		expected_plane_strides[1] = imx_vpu_enc->current_stream_info.frame_encoding_framebuffer_metrics.uv_stride;
		expected_plane_strides[2] = imx_vpu_enc->current_stream_info.frame_encoding_framebuffer_metrics.uv_stride;

		for (i = 0; i < num_planes; ++i)
		{
			guint actual_plane_stride = has_video_meta ? video_meta->stride[0] : GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
			if (actual_plane_stride != expected_plane_strides[i])
			{
				GST_LOG_OBJECT(imx_vpu_enc, "buffer plane #%u has stride %u, expected stride %u; copying frame with CPU", i, actual_plane_stride, expected_plane_strides[i]);
				return FALSE;
			}
		}

		expected_plane_offsets[0] = imx_vpu_enc->current_stream_info.frame_encoding_framebuffer_metrics.y_offset;
		expected_plane_offsets[1] = imx_vpu_enc->current_stream_info.frame_encoding_framebuffer_metrics.u_offset;
		expected_plane_offsets[2] = imx_vpu_enc->current_stream_info.frame_encoding_framebuffer_metrics.v_offset;

		for (i = 0; i < num_planes; ++i)
		{
			gsize actual_plane_offset = has_video_meta ? video_meta->offset[0] : GST_VIDEO_INFO_PLANE_OFFSET(video_info, 0);
			if (actual_plane_offset != expected_plane_offsets[i])
			{
				GST_LOG_OBJECT(imx_vpu_enc, "buffer plane #%u starts at offset %" G_GSIZE_FORMAT ", expected start at offset %" G_GSIZE_FORMAT "; copying frame with CPU", i, actual_plane_offset, expected_plane_offsets[i]);
				return FALSE;
			}
		}
	}

	return TRUE;
}


void gst_imx_vpu_enc_common_class_init(GstImxVpuEncClass *klass, ImxVpuApiCompressionFormat compression_format, gboolean with_rate_control, gboolean with_constant_quantization, gboolean with_gop_support)
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
