/* Base class for video encoders using the Freescale VPU hardware video engine
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


#include <string.h>
#include "encoder_base.h"
#include "allocator.h"
#include "device.h"
#include "../common/phys_mem_buffer_pool.h"
#include "../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_encoder_base_debug);
#define GST_CAT_DEFAULT imx_vpu_encoder_base_debug


enum
{
	PROP_0,
	PROP_DROP,
	PROP_GOP_SIZE,
	PROP_BITRATE,
	PROP_SLICE_SIZE,
	PROP_INTRA_REFRESH,
	PROP_ME_SEARCH_RANGE
};


#define DEFAULT_DROP              FALSE
#define DEFAULT_GOP_SIZE          16
#define DEFAULT_BITRATE           0
#define DEFAULT_SLICE_SIZE        0
#define DEFAULT_INTRA_REFRESH     0
#define DEFAULT_ME_SEARCH_RANGE   IMX_VPU_ENC_ME_SEARCH_RANGE_256x128

#define GST_IMX_VPU_ENCODER_ALLOCATOR_MEM_TYPE "ImxVpuEncMemory2"


/* TODO: Memory-mapped writes into physically contiguous memory blocks are quite slow. This is
 * probably caused by the mapping type: if for example it is not mapped with write combining
 * enabled, random access to the memory will cause lots of wasteful cycles, explaining the
 * slowdown. Until this can be verified, the buffer pool is disabled; upstream does not get a
 * proposal for its allocation, and buffer contents end up copied over to a local physical
 * memory block by using memcpy(). Currently, doing that is ~3 times faster than letting
 * upstream write directly into physical memory blocks allocated by the proposed buffer pool.
 * (This also affects the IPU elements.)
 */
/*#define ENABLE_PROPOSE_ALLOCATION 1*/


G_DEFINE_ABSTRACT_TYPE(GstImxVpuEncoderBase, gst_imx_vpu_encoder_base, GST_TYPE_VIDEO_ENCODER)

static void gst_imx_vpu_encoder_base_dispose(GObject *object);
static void gst_imx_vpu_encoder_base_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_encoder_base_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_vpu_encoder_base_start(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_encoder_base_stop(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_encoder_base_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static gboolean gst_imx_vpu_encoder_base_sink_event(GstVideoEncoder *encoder, GstEvent *event);
static GstFlowReturn gst_imx_vpu_encoder_base_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame);
#ifdef ENABLE_PROPOSE_ALLOCATION
static gboolean gst_imx_vpu_encoder_base_propose_allocation(GstVideoEncoder *encoder, GstQuery *query);
#endif
static gboolean gst_imx_vpu_encoder_flush(GstVideoEncoder *encoder);

static void gst_imx_vpu_encoder_base_close(GstImxVpuEncoderBase *vpu_encoder_base);
static gboolean gst_imx_vpu_encoder_base_set_bitrate(GstImxVpuEncoderBase *vpu_encoder_base);

static void* gst_imx_vpu_encoder_base_acquire_output_buffer(void *context, size_t size, void **acquired_handle);
static void gst_imx_vpu_encoder_base_finish_output_buffer(void *context, void *acquired_handle);

GType gst_imx_vpu_encoder_me_search_range_get_type(void);




static void gst_imx_vpu_encoder_base_class_init(GstImxVpuEncoderBaseClass *klass)
{
	GObjectClass *object_class;
	GstVideoEncoderClass *video_encoder_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_encoder_base_debug, "imxvpuencoderbase", 0, "Freescale i.MX VPU video encoder base class");

	imx_vpu_setup_logging();

	object_class = G_OBJECT_CLASS(klass);
	video_encoder_class = GST_VIDEO_ENCODER_CLASS(klass);

	object_class->dispose      = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_dispose);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_get_property);

	video_encoder_class->start              = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_start);
	video_encoder_class->stop               = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_stop);
	video_encoder_class->set_format         = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_set_format);
	video_encoder_class->sink_event         = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_sink_event);
	video_encoder_class->handle_frame       = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_handle_frame);
#ifdef ENABLE_PROPOSE_ALLOCATION
	video_encoder_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_base_propose_allocation);
#endif
	video_encoder_class->flush              = GST_DEBUG_FUNCPTR(gst_imx_vpu_encoder_flush);

	klass->get_output_caps = NULL;
	klass->set_open_params = NULL;
	klass->set_frame_enc_params = NULL;
	klass->process_output_buffer = NULL;
	klass->sink_event = NULL;

	g_object_class_install_property(
		object_class,
		PROP_DROP,
		g_param_spec_boolean(
			"drop",
			"Drop",
			"Drop frames",
			DEFAULT_DROP,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
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
	g_object_class_install_property(
		object_class,
		PROP_BITRATE,
		g_param_spec_uint(
			"bitrate",
			"Bitrate",
			"Bitrate to use, in kbps (0 = no bitrate control; constant quality mode is used)",
			0, G_MAXUINT,
			DEFAULT_BITRATE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_SLICE_SIZE,
		g_param_spec_int(
			"slice-size",
			"Slice size",
			"Maximum slice size (0 = unlimited, <0 in MB, >0 in bits)",
			G_MININT, G_MAXINT,
			DEFAULT_SLICE_SIZE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_INTRA_REFRESH,
		g_param_spec_uint(
			"intra-refresh",
			"Intra Refresh",
			"Minimum number of MBs to encode as intra MB",
			0, G_MAXUINT,
			DEFAULT_INTRA_REFRESH,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ME_SEARCH_RANGE,
		g_param_spec_enum(
			"me-search-range",
			"Motion estimation search range",
			"Search range for motion estimation",
			gst_imx_vpu_encoder_me_search_range_get_type(),
			DEFAULT_ME_SEARCH_RANGE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_vpu_encoder_base_init(GstImxVpuEncoderBase *vpu_encoder_base)
{
	vpu_encoder_base->encoder = NULL;
	vpu_encoder_base->bitstream_buffer = NULL;
	vpu_encoder_base->phys_mem_allocator = NULL;

	vpu_encoder_base->internal_input_bufferpool = NULL;
	vpu_encoder_base->internal_input_buffer = NULL;

	memset(&(vpu_encoder_base->input_frame), 0, sizeof(ImxVpuRawFrame));
	imx_vpu_init_wrapped_dma_buffer(&(vpu_encoder_base->input_dmabuffer));
	vpu_encoder_base->input_frame.framebuffer = &(vpu_encoder_base->input_framebuffer);
	vpu_encoder_base->input_framebuffer.dma_buffer = (ImxVpuDMABuffer *)(&(vpu_encoder_base->input_dmabuffer));

	vpu_encoder_base->framebuffer_array = NULL;

	gst_video_info_init(&(vpu_encoder_base->video_info));

	vpu_encoder_base->drop             = DEFAULT_DROP;
	vpu_encoder_base->gop_size         = DEFAULT_GOP_SIZE;
	vpu_encoder_base->bitrate          = DEFAULT_BITRATE;
	vpu_encoder_base->slice_size       = DEFAULT_SLICE_SIZE;
	vpu_encoder_base->intra_refresh    = DEFAULT_INTRA_REFRESH;
	vpu_encoder_base->me_search_range  = DEFAULT_ME_SEARCH_RANGE;
}


static void gst_imx_vpu_encoder_base_dispose(GObject *object)
{
	G_OBJECT_CLASS(gst_imx_vpu_encoder_base_parent_class)->dispose(object);
}


static void gst_imx_vpu_encoder_base_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncoderBase *vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(object);

	switch (prop_id)
	{
		case PROP_DROP:
			vpu_encoder_base->drop = g_value_get_boolean(value);
			break;
		case PROP_GOP_SIZE:
			vpu_encoder_base->gop_size = g_value_get_uint(value);
			break;
		case PROP_BITRATE:
			GST_OBJECT_LOCK(vpu_encoder_base);

			vpu_encoder_base->bitrate = g_value_get_uint(value);
			if(vpu_encoder_base->encoder != NULL)
				gst_imx_vpu_encoder_base_set_bitrate(vpu_encoder_base);

			GST_OBJECT_UNLOCK(vpu_encoder_base);
			break;
		case PROP_SLICE_SIZE:
			vpu_encoder_base->slice_size = g_value_get_int(value);
			break;
		case PROP_INTRA_REFRESH:
			vpu_encoder_base->intra_refresh = g_value_get_uint(value);
			break;
		case PROP_ME_SEARCH_RANGE:
			vpu_encoder_base->me_search_range = g_value_get_enum(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_encoder_base_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncoderBase *vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(object);

	switch (prop_id)
	{
		case PROP_DROP:
			g_value_set_boolean(value, vpu_encoder_base->drop);
			break;
		case PROP_GOP_SIZE:
			g_value_set_uint(value, vpu_encoder_base->gop_size);
			break;
		case PROP_BITRATE:
			g_value_set_uint(value, vpu_encoder_base->bitrate);
			break;
		case PROP_SLICE_SIZE:
			g_value_set_int(value, vpu_encoder_base->slice_size);
			break;
		case PROP_INTRA_REFRESH:
			g_value_set_uint(value, vpu_encoder_base->intra_refresh);
			break;
		case PROP_ME_SEARCH_RANGE:
			g_value_set_enum(value, vpu_encoder_base->me_search_range);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_vpu_encoder_base_start(GstVideoEncoder *encoder)
{
	size_t bitstream_buffer_size;
	unsigned int bitstream_buffer_alignment;
	GstImxVpuEncoderBase *vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(encoder);

	GST_INFO_OBJECT(vpu_encoder_base, "starting VPU encoder");

	/* Make sure the firmware is loaded */
	if (!gst_imx_vpu_encoder_load())
		return FALSE;

	/* Set up a DMA buffer allocator for framebuffers and the bitstream buffer */
	if ((vpu_encoder_base->phys_mem_allocator = gst_imx_vpu_allocator_new(imx_vpu_enc_get_default_allocator(), GST_IMX_VPU_ENCODER_ALLOCATOR_MEM_TYPE)) == NULL)
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "could not create physical memory allocator");
		return FALSE;
	}

	/* Allocate the bitstream buffer */
	imx_vpu_enc_get_bitstream_buffer_info(&bitstream_buffer_size, &bitstream_buffer_alignment);
	vpu_encoder_base->bitstream_buffer = gst_buffer_new_allocate(vpu_encoder_base->phys_mem_allocator, bitstream_buffer_size, NULL); // TODO: pass on alignment

	if (vpu_encoder_base->bitstream_buffer == NULL)
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "could not allocate bitstream buffer");
		return FALSE;
	}

	/* The encoder is initialized in set_format, not here, since only then the input bitstream
	 * format is known (and this information is necessary for initialization). */

	GST_INFO_OBJECT(vpu_encoder_base, "VPU encoder started");

	return TRUE;
}


static gboolean gst_imx_vpu_encoder_base_stop(GstVideoEncoder *encoder)
{
	GstImxVpuEncoderBase *vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(encoder);

	gst_imx_vpu_encoder_base_close(vpu_encoder_base);

	if (vpu_encoder_base->bitstream_buffer != NULL)
	{
		gst_buffer_unref(vpu_encoder_base->bitstream_buffer);
		vpu_encoder_base->bitstream_buffer = NULL;
	}

	GST_INFO_OBJECT(vpu_encoder_base, "VPU encoder stopped");

	if (vpu_encoder_base->phys_mem_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(vpu_encoder_base->phys_mem_allocator));
		vpu_encoder_base->phys_mem_allocator = NULL;
	}

	/* Make sure the firmware is unloaded */
	gst_imx_vpu_encoder_unload();

	return TRUE;
}


static gboolean gst_imx_vpu_encoder_base_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
	GstVideoCodecState *output_state;
	GstImxVpuEncoderBaseClass *klass;
	GstImxVpuEncoderBase *vpu_encoder_base;
	ImxVpuEncReturnCodes enc_ret;
	
	vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(encoder);
	klass = GST_IMX_VPU_ENCODER_BASE_CLASS(G_OBJECT_GET_CLASS(vpu_encoder_base));

	g_assert(klass->get_output_caps != NULL);

	GST_INFO_OBJECT(encoder, "setting encoder format");


	/* Cleanup any existing old encoder */
	gst_imx_vpu_encoder_base_close(vpu_encoder_base);


	/* Set up the open params */

	memset(&(vpu_encoder_base->open_params), 0, sizeof(ImxVpuEncOpenParams));
	imx_vpu_enc_set_default_open_params(klass->codec_format, &(vpu_encoder_base->open_params));

	/* Fill the open params. (Derived classes can set different values.) */

	/* All encoders except MJPEG support only grayscale and 4:2:0 color formats */
	vpu_encoder_base->open_params.color_format = (GST_VIDEO_INFO_FORMAT(&(state->info)) == GST_VIDEO_FORMAT_GRAY8) ? IMX_VPU_COLOR_FORMAT_YUV400 : IMX_VPU_COLOR_FORMAT_YUV420;
	vpu_encoder_base->open_params.frame_width = GST_VIDEO_INFO_WIDTH(&(state->info));
	vpu_encoder_base->open_params.frame_height = GST_VIDEO_INFO_HEIGHT(&(state->info));
	vpu_encoder_base->open_params.frame_rate_numerator = GST_VIDEO_INFO_FPS_N(&(state->info));
	vpu_encoder_base->open_params.frame_rate_denominator = GST_VIDEO_INFO_FPS_D(&(state->info));
	vpu_encoder_base->open_params.bitrate = vpu_encoder_base->bitrate;
	vpu_encoder_base->open_params.gop_size = vpu_encoder_base->gop_size;

	/* If the input format has one plane with interleaved chroma data
	 * (= the input format is NV12/NV16/NV24), set chroma_interleave
	 * to 1, otherwise set it to 0 */
	switch (GST_VIDEO_INFO_FORMAT(&(state->info)))
	{
		case GST_VIDEO_FORMAT_NV12:
		case GST_VIDEO_FORMAT_NV16:
		case GST_VIDEO_FORMAT_NV24:
			GST_DEBUG_OBJECT(vpu_encoder_base, "input format uses shared chroma plane; enabling chroma interleave");
			vpu_encoder_base->open_params.chroma_interleave = 1;
			break;

		default:
			GST_DEBUG_OBJECT(vpu_encoder_base, "input format uses separate chroma planes; disabling chroma interleave");
			vpu_encoder_base->open_params.chroma_interleave = 0;
	}

	GST_INFO_OBJECT(vpu_encoder_base, "setting bitrate to %u kbps and GOP size to %u", vpu_encoder_base->open_params.bitrate, vpu_encoder_base->open_params.gop_size);

	if (vpu_encoder_base->slice_size != 0)
	{
		vpu_encoder_base->open_params.slice_mode.multiple_slices_per_frame = 1;

		if (vpu_encoder_base->slice_size < 0)
		{
			vpu_encoder_base->open_params.slice_mode.slice_size_unit = IMX_VPU_ENC_SLICE_SIZE_UNIT_MACROBLOCKS;
			vpu_encoder_base->open_params.slice_mode.slice_size = -vpu_encoder_base->slice_size;
		}
		else
		{
			vpu_encoder_base->open_params.slice_mode.slice_size_unit = IMX_VPU_ENC_SLICE_SIZE_UNIT_BITS;
			vpu_encoder_base->open_params.slice_mode.slice_size = vpu_encoder_base->slice_size;
		}
	}

	vpu_encoder_base->open_params.min_intra_refresh_mb_count = vpu_encoder_base->intra_refresh;
	vpu_encoder_base->open_params.me_search_range = vpu_encoder_base->me_search_range;

	/* Give the derived class a chance to set params */
	if (klass->set_open_params && !klass->set_open_params(vpu_encoder_base, state, &(vpu_encoder_base->open_params)))
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "derived class could not set open params");
		return FALSE;
	}


	/* Open and configure encoder */

	if ((enc_ret = imx_vpu_enc_open(&(vpu_encoder_base->encoder), &(vpu_encoder_base->open_params), gst_imx_vpu_get_dma_buffer_from(vpu_encoder_base->bitstream_buffer)) != IMX_VPU_ENC_RETURN_CODE_OK))
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "could not open encoder: %s", imx_vpu_enc_error_string(enc_ret));
		return FALSE;
	}

	GST_TRACE_OBJECT(vpu_encoder_base, "configuring encoder");

	if (vpu_encoder_base->bitrate != 0)
		imx_vpu_enc_configure_bitrate(vpu_encoder_base->encoder, vpu_encoder_base->bitrate);

	if (vpu_encoder_base->intra_refresh != 0)
		imx_vpu_enc_configure_min_intra_refresh(vpu_encoder_base->encoder, vpu_encoder_base->intra_refresh);


	/* Retrieve initial info */

	GST_TRACE_OBJECT(vpu_encoder_base, "retrieving initial info");

	if ((enc_ret = imx_vpu_enc_get_initial_info(vpu_encoder_base->encoder, &(vpu_encoder_base->initial_info))) != IMX_VPU_ENC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "could not get initial info: %s", imx_vpu_enc_error_string(enc_ret));
		return FALSE;
	}


	/* Allocate and register framebuffer array */

	GST_TRACE_OBJECT(vpu_encoder_base, "allocating framebuffer array");

	vpu_encoder_base->framebuffer_array = gst_imx_vpu_framebuffer_array_new(
		vpu_encoder_base->open_params.color_format,
		vpu_encoder_base->open_params.frame_width,
		vpu_encoder_base->open_params.frame_height,
		vpu_encoder_base->initial_info.framebuffer_alignment,
		FALSE,
		FALSE,
		vpu_encoder_base->initial_info.min_num_required_framebuffers,
		(GstImxPhysMemAllocator *)(vpu_encoder_base->phys_mem_allocator)
	);

	if (vpu_encoder_base->framebuffer_array == NULL)
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "could not create new framebuffer array");
		return FALSE;
	}

	GST_TRACE_OBJECT(vpu_encoder_base, "registering framebuffer array");

	if ((enc_ret = imx_vpu_enc_register_framebuffers(vpu_encoder_base->encoder, vpu_encoder_base->framebuffer_array->framebuffers, vpu_encoder_base->framebuffer_array->num_framebuffers)) != IMX_VPU_ENC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "could not register framebuffers: %s", imx_vpu_enc_error_string(enc_ret));
		return FALSE;
	}


	GST_TRACE_OBJECT(vpu_encoder_base, "allocating output buffer with %u bytes", vpu_encoder_base->framebuffer_array->framebuffer_sizes.total_size);


	/* Set the output state, using caps defined by the derived class */
	output_state = gst_video_encoder_set_output_state(
		encoder,
		klass->get_output_caps(vpu_encoder_base),
		state
	);
	gst_video_codec_state_unref(output_state);


	vpu_encoder_base->video_info = state->info;


	GST_TRACE_OBJECT(vpu_encoder_base, "encoder format set");


	return TRUE;
}


static gboolean gst_imx_vpu_encoder_base_sink_event(GstVideoEncoder *encoder, GstEvent *event)
{
	GstImxVpuEncoderBaseClass *klass;
	GstImxVpuEncoderBase *vpu_encoder_base;
	gboolean ret;

	vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(encoder);
	klass = GST_IMX_VPU_ENCODER_BASE_CLASS(G_OBJECT_GET_CLASS(vpu_encoder_base));

	ret = TRUE;
	if (klass->sink_event != NULL)
		ret = klass->sink_event(encoder, event);

	return ret && GST_VIDEO_ENCODER_CLASS(gst_imx_vpu_encoder_base_parent_class)->sink_event(encoder, event);
}


static GstFlowReturn gst_imx_vpu_encoder_base_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *input_frame)
{
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxVpuEncoderBaseClass *klass;
	GstImxVpuEncoderBase *vpu_encoder_base;
	GstBuffer *input_buffer;
	ImxVpuEncParams enc_params;

	vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(encoder);
	klass = GST_IMX_VPU_ENCODER_BASE_CLASS(G_OBJECT_GET_CLASS(vpu_encoder_base));

	if (vpu_encoder_base->drop)
	{
		input_frame->output_buffer = NULL; /* necessary to make finish_frame() drop the frame */
		gst_video_encoder_finish_frame(encoder, input_frame);
		return GST_FLOW_OK;
	}

	/* Get access to the input buffer's physical address */

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(input_frame->input_buffer);

	/* If the incoming frame's buffer is not using physically contiguous memory,
	 * it needs to be copied to the internal input buffer, otherwise the VPU
	 * encoder cannot read the frame */
	if (phys_mem_meta == NULL)
	{
		/* No physical memory metadata found -> buffer is not physically contiguous */

		GstVideoFrame temp_input_video_frame, temp_incoming_video_frame;

		GST_LOG_OBJECT(vpu_encoder_base, "input buffer not physically contiguous - frame copy is necessary");

		if (vpu_encoder_base->internal_input_buffer == NULL)
		{
			/* The internal input buffer is the temp input frame's DMA memory.
			 * If it does not exist yet, it needs to be created here. The temp input
			 * frame is then mapped. */

			GstFlowReturn flow_ret;

			if (vpu_encoder_base->internal_input_bufferpool == NULL)
			{
				/* Internal bufferpool does not exist yet - create it now,
				 * so that it can in turn create the internal input buffer */

				GstStructure *config;
				GstCaps *caps;

				GST_DEBUG_OBJECT(vpu_encoder_base, "creating internal bufferpool");

				caps = gst_video_info_to_caps(&(vpu_encoder_base->video_info));
				vpu_encoder_base->internal_input_bufferpool = gst_imx_phys_mem_buffer_pool_new(FALSE);

				config = gst_buffer_pool_get_config(vpu_encoder_base->internal_input_bufferpool);
				gst_buffer_pool_config_set_params(config, caps, vpu_encoder_base->video_info.size, 2, 0);
				gst_buffer_pool_config_set_allocator(config, vpu_encoder_base->phys_mem_allocator, NULL);
				gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
				gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
				gst_buffer_pool_set_config(vpu_encoder_base->internal_input_bufferpool, config);

				gst_caps_unref(caps);

				if (vpu_encoder_base->internal_input_bufferpool == NULL)
				{
					GST_ERROR_OBJECT(vpu_encoder_base, "failed to create internal bufferpool");
					return GST_FLOW_ERROR;
				}
			}

			/* Future versions of this code may propose the internal bufferpool upstream;
			 * hence the is_active check */
			if (!gst_buffer_pool_is_active(vpu_encoder_base->internal_input_bufferpool))
				gst_buffer_pool_set_active(vpu_encoder_base->internal_input_bufferpool, TRUE);

			/* Create the internal input buffer */
			flow_ret = gst_buffer_pool_acquire_buffer(vpu_encoder_base->internal_input_bufferpool, &(vpu_encoder_base->internal_input_buffer), NULL);
			if (flow_ret != GST_FLOW_OK)
			{
				GST_ERROR_OBJECT(vpu_encoder_base, "error acquiring input frame buffer: %s", gst_pad_mode_get_name(flow_ret));
				return flow_ret;
			}
		}

		/* The internal input buffer exists at this point. Since the incoming frame
		 * is not stored in physical memory, copy its pixels to the internal
		 * input buffer, so the encoder can read them. */

		gst_video_frame_map(&temp_incoming_video_frame, &(vpu_encoder_base->video_info), input_frame->input_buffer, GST_MAP_READ);
		gst_video_frame_map(&temp_input_video_frame, &(vpu_encoder_base->video_info), vpu_encoder_base->internal_input_buffer, GST_MAP_WRITE);

		gst_video_frame_copy(&temp_input_video_frame, &temp_incoming_video_frame);

		gst_video_frame_unmap(&temp_incoming_video_frame);
		gst_video_frame_unmap(&temp_input_video_frame);

		/* Set the input buffer as the encoder's input */
		input_buffer = vpu_encoder_base->internal_input_buffer;
		/* And use the input buffer's physical memory metadata */
		phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(vpu_encoder_base->internal_input_buffer);
	}
	else
	{
		/* Physical memory metadata found -> buffer is physically contiguous
		 * It can be used directly as input for the VPU encoder */
		input_buffer = input_frame->input_buffer;
	}


	/* Prepare the input buffer's information (strides, plane offsets ..) for encoding */

	{
		GstVideoMeta *video_meta;

		/* Try to use plane offset and stride information from the video
		 * metadata if present, since these can be more accurate than
		 * the information from the video info */
		video_meta = gst_buffer_get_video_meta(input_buffer);
		if (video_meta != NULL)
		{
			vpu_encoder_base->input_framebuffer.y_stride = video_meta->stride[0];
			vpu_encoder_base->input_framebuffer.cbcr_stride = video_meta->stride[1];

			vpu_encoder_base->input_framebuffer.y_offset = video_meta->offset[0];
			vpu_encoder_base->input_framebuffer.cb_offset = video_meta->offset[1];
			vpu_encoder_base->input_framebuffer.cr_offset = video_meta->offset[2];
		}
		else
		{
			vpu_encoder_base->input_framebuffer.y_stride = GST_VIDEO_INFO_PLANE_STRIDE(&(vpu_encoder_base->video_info), 0);
			vpu_encoder_base->input_framebuffer.cbcr_stride = GST_VIDEO_INFO_PLANE_STRIDE(&(vpu_encoder_base->video_info), 1);

			vpu_encoder_base->input_framebuffer.y_offset = GST_VIDEO_INFO_PLANE_OFFSET(&(vpu_encoder_base->video_info), 0);
			vpu_encoder_base->input_framebuffer.cb_offset = GST_VIDEO_INFO_PLANE_OFFSET(&(vpu_encoder_base->video_info), 1);
			vpu_encoder_base->input_framebuffer.cr_offset = GST_VIDEO_INFO_PLANE_OFFSET(&(vpu_encoder_base->video_info), 2);
		}

		vpu_encoder_base->input_framebuffer.mvcol_offset = 0; /* this is not used by the encoder */
		vpu_encoder_base->input_framebuffer.context = (void *)(input_frame->system_frame_number);

		vpu_encoder_base->input_dmabuffer.fd = -1;
		vpu_encoder_base->input_dmabuffer.physical_address = phys_mem_meta->phys_addr;
		vpu_encoder_base->input_dmabuffer.size = gst_buffer_get_size(input_buffer);
	}


	/* Prepare the encoding parameters */

	memset(&enc_params, 0, sizeof(enc_params));
	imx_vpu_enc_set_default_encoding_params(vpu_encoder_base->encoder, &enc_params);
	enc_params.force_I_frame = 0;
	enc_params.acquire_output_buffer = gst_imx_vpu_encoder_base_acquire_output_buffer;
	enc_params.finish_output_buffer = gst_imx_vpu_encoder_base_finish_output_buffer;
	enc_params.output_buffer_context = vpu_encoder_base;

	/* Force I-frame if either IS_FORCE_KEYFRAME or IS_FORCE_KEYFRAME_HEADERS is set for the current frame. */
	if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(input_frame) || GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME_HEADERS(input_frame))
	{
		enc_params.force_I_frame = 1;
		GST_LOG_OBJECT(vpu_encoder_base, "got request to make this a keyframe - forcing I frame");
		GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(input_frame);
	}

	/* Give the derived class a chance to set up encoding parameters too */
	if ((klass->set_frame_enc_params != NULL) && !klass->set_frame_enc_params(vpu_encoder_base, &enc_params))
	{
		GST_ERROR_OBJECT(vpu_encoder_base, "derived class could not frame enc params");
		return GST_FLOW_ERROR;
	}


	/* Main encoding block */
	{
		ImxVpuEncReturnCodes enc_ret;
		unsigned int output_code = 0;
		ImxVpuEncodedFrame encoded_data_frame;

		vpu_encoder_base->output_buffer = NULL;

		/* The actual encoding call */
		memset(&encoded_data_frame, 0, sizeof(ImxVpuEncodedFrame));
		enc_ret = imx_vpu_enc_encode(vpu_encoder_base->encoder, &(vpu_encoder_base->input_frame), &encoded_data_frame, &enc_params, &output_code);
		if (enc_ret != IMX_VPU_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(vpu_encoder_base, "failed to encode frame: %s", imx_vpu_enc_error_string(enc_ret));
			if (vpu_encoder_base->output_buffer != NULL)
				gst_buffer_unref(vpu_encoder_base->output_buffer);
			return GST_FLOW_ERROR;
		}

		/* Give the derived class a chance to process the output_block_buffer */
		if ((klass->process_output_buffer != NULL) && !klass->process_output_buffer(vpu_encoder_base, input_frame, &(vpu_encoder_base->output_buffer)))
		{
			GST_ERROR_OBJECT(vpu_encoder_base, "derived class reports failure while processing encoded output");
			if (vpu_encoder_base->output_buffer != NULL)
				gst_buffer_unref(vpu_encoder_base->output_buffer);
			return GST_FLOW_ERROR;
		}

		if (output_code & IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE)
		{
			GST_LOG_OBJECT(vpu_encoder_base, "VPU outputs encoded frame");

			/* TODO: make use of the frame context that is retrieved with get_frame(i)
			 * This is not strictly necessary, since the VPU encoder does not
			 * do frame reordering, nor does it produce delays, but it would
			 * be a bit cleaner. */

			input_frame->dts = input_frame->pts;

			/* Take all of the encoded bits. The adapter contains an encoded frame
			 * at this point. */
			input_frame->output_buffer = vpu_encoder_base->output_buffer;

			/* And finish the frame, handing the output data over to the base class */
			gst_video_encoder_finish_frame(encoder, input_frame);
		}
		else
		{
			/* If at this point IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE is not set
			 * in the output_code, it means the input was used up before a frame could be
			 * encoded. Therefore, no output frame can be pushed downstream. Note that this
			 * should not happen during normal operation, so a warning is logged. */

			if (vpu_encoder_base->output_buffer != NULL)
				gst_buffer_unref(vpu_encoder_base->output_buffer);

			GST_WARNING_OBJECT(vpu_encoder_base, "frame unfinished ; dropping");
			input_frame->output_buffer = NULL; /* necessary to make finish_frame() drop the frame */
			gst_video_encoder_finish_frame(encoder, input_frame);
		}
	}


	return GST_FLOW_OK;
}


#ifdef ENABLE_PROPOSE_ALLOCATION
static gboolean gst_imx_vpu_encoder_base_propose_allocation(GstVideoEncoder *encoder, GstQuery *query)
{
	GstStructure *config;
	GstCaps *caps;
	gboolean need_pool;
	GstVideoInfo info;
	GstBufferPool *pool;

	gst_query_parse_allocation (query, &caps, &need_pool);

	if (need_pool)
	{
		if (caps == NULL)
		{
			GST_WARNING_OBJECT(encoder, "no caps");
			return FALSE;
		}

		if (!gst_video_info_from_caps(&info, caps))
		{
			GST_WARNING_OBJECT(encoder, "invalid caps");
			return FALSE;
		}

		pool = gst_imx_phys_mem_buffer_pool_new(FALSE);

		config = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_set_params(config, caps, info.size, 2, 0);
		gst_buffer_pool_config_set_allocator(config, encoder->phys_mem_allocator, NULL);
		gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
		gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
		gst_buffer_pool_set_config(pool, config);

		gst_query_add_allocation_pool(query, pool, info.size, 2, 0);
		gst_object_unref (pool);
	}

	return TRUE;
}
#endif


static gboolean gst_imx_vpu_encoder_flush(GstVideoEncoder *encoder)
{
	ImxVpuEncReturnCodes ret;
	GstImxVpuEncoderBase *vpu_encoder_base = GST_IMX_VPU_ENCODER_BASE(encoder);

	if (vpu_encoder_base->encoder && ((ret = imx_vpu_enc_flush(vpu_encoder_base->encoder)) != IMX_VPU_ENC_RETURN_CODE_OK))
		GST_ERROR_OBJECT(vpu_encoder_base, "could not flush encoder: %s", imx_vpu_enc_error_string(ret));

	return ret == IMX_VPU_ENC_RETURN_CODE_OK;
}


static void gst_imx_vpu_encoder_base_close(GstImxVpuEncoderBase *vpu_encoder_base)
{
	ImxVpuEncReturnCodes ret;

	if (vpu_encoder_base->encoder == NULL)
		return;

	GST_DEBUG_OBJECT(vpu_encoder_base, "closing encoder");

	if (vpu_encoder_base->internal_input_bufferpool != NULL)
	{
		gst_object_unref(vpu_encoder_base->internal_input_bufferpool);
		vpu_encoder_base->internal_input_bufferpool = NULL;
	}

	if (vpu_encoder_base->internal_input_buffer != NULL)
	{
		gst_buffer_unref(vpu_encoder_base->internal_input_buffer);
		vpu_encoder_base->internal_input_buffer = NULL;
	}

	if ((ret = imx_vpu_enc_close(vpu_encoder_base->encoder)) != IMX_VPU_ENC_RETURN_CODE_OK)
		GST_ERROR_OBJECT(vpu_encoder_base, "error while closing encoder: %s", imx_vpu_enc_error_string(ret));

	if (vpu_encoder_base->framebuffer_array != NULL)
	{
		gst_object_unref(GST_OBJECT(vpu_encoder_base->framebuffer_array));
		vpu_encoder_base->framebuffer_array = NULL;
	}

	vpu_encoder_base->encoder = NULL;
}


static gboolean gst_imx_vpu_encoder_base_set_bitrate(GstImxVpuEncoderBase *vpu_encoder_base)
{
	if (vpu_encoder_base->bitrate != 0)
		imx_vpu_enc_configure_bitrate(vpu_encoder_base->encoder, vpu_encoder_base->bitrate);

	return TRUE;
}


static void* gst_imx_vpu_encoder_base_acquire_output_buffer(void *context, size_t size, void **acquired_handle)
{
	GstImxVpuEncoderBase *vpu_encoder_base = (GstImxVpuEncoderBase *)(context);
	GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);
	vpu_encoder_base->output_buffer = buffer;
	gst_buffer_map(buffer, &(vpu_encoder_base->output_buffer_map_info), GST_MAP_WRITE);
	GST_LOG_OBJECT(vpu_encoder_base, "acquired output buffer %p with %zu byte", (gpointer)buffer, size);
	*acquired_handle = buffer;
	return vpu_encoder_base->output_buffer_map_info.data;
}


static void gst_imx_vpu_encoder_base_finish_output_buffer(void *context, void *acquired_handle)
{
	GstImxVpuEncoderBase *vpu_encoder_base = (GstImxVpuEncoderBase *)(context);
	GstBuffer *buffer = vpu_encoder_base->output_buffer;
	GST_LOG_OBJECT(vpu_encoder_base, "finished output buffer %p with %zu byte", (gpointer)buffer, vpu_encoder_base->output_buffer_map_info.size);
	gst_buffer_unmap(buffer, &(vpu_encoder_base->output_buffer_map_info));
}


GType gst_imx_vpu_encoder_me_search_range_get_type(void)
{
	static GType gst_imx_vpu_encoder_me_search_range_type = 0;

	if (!gst_imx_vpu_encoder_me_search_range_type)
	{
		static GEnumValue me_search_ranges[] =
		{
			{ IMX_VPU_ENC_ME_SEARCH_RANGE_256x128, "256x128 blocks", "256x128" },
			{ IMX_VPU_ENC_ME_SEARCH_RANGE_128x64, "128x64 blocks", "128x64" },
			{ IMX_VPU_ENC_ME_SEARCH_RANGE_64x32, "64x32 blocks", "64x32" },
			{ IMX_VPU_ENC_ME_SEARCH_RANGE_32x32, "32x32 blocks", "32x32" },
			{ 0, NULL, NULL },
		};

		gst_imx_vpu_encoder_me_search_range_type = g_enum_register_static(
			"ImxVpuEncMESearchRanges",
			me_search_ranges
		);
	}

	return gst_imx_vpu_encoder_me_search_range_type;
}


ImxVpuEncOpenParams const * gst_imx_vpu_encoder_base_get_open_params(GstImxVpuEncoderBase *vpu_encoder_base)
{
	return &(vpu_encoder_base->open_params);
}
