/* GStreamer video encoder base class using the Freescale VPU hardware video engine
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
#include "base_enc.h"
#include "allocator.h"
#include "../mem_blocks.h"
#include "../utils.h"
#include "../../common/phys_mem_buffer_pool.h"
#include "../../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(imx_vpu_base_enc_debug);
#define GST_CAT_DEFAULT imx_vpu_base_enc_debug


enum
{
	PROP_0,
	PROP_GOP_SIZE,
	PROP_BITRATE
};


#define DEFAULT_GOP_SIZE          16
#define DEFAULT_BITRATE           0


#define ALIGN_VAL_TO(LENGTH, ALIGN_SIZE)  ( ((guintptr)((LENGTH) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )


static GMutex inst_counter_mutex;


G_DEFINE_ABSTRACT_TYPE(GstImxVpuBaseEnc, gst_imx_vpu_base_enc, GST_TYPE_VIDEO_ENCODER)


/* miscellaneous functions */
static gboolean gst_imx_vpu_base_enc_alloc_enc_mem_blocks(GstImxVpuBaseEnc *vpu_base_enc);
static gboolean gst_imx_vpu_base_enc_free_enc_mem_blocks(GstImxVpuBaseEnc *vpu_base_enc);
static void gst_imx_vpu_base_enc_close_encoder(GstImxVpuBaseEnc *vpu_base_enc);
static void gst_imx_vpu_base_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_base_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* functions for the base class */
static gboolean gst_imx_vpu_base_enc_start(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_base_enc_stop(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_base_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static GstFlowReturn gst_imx_vpu_base_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame);




/* required function declared by G_DEFINE_TYPE */

void gst_imx_vpu_base_enc_class_init(GstImxVpuBaseEncClass *klass)
{
	GObjectClass *object_class;
	GstVideoEncoderClass *base_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_base_enc_debug, "imxvpubaseenc", 0, "Freescale i.MX VPU video encoder base class");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_VIDEO_ENCODER_CLASS(klass);

	object_class->set_property    = GST_DEBUG_FUNCPTR(gst_imx_vpu_base_enc_set_property);
	object_class->get_property    = GST_DEBUG_FUNCPTR(gst_imx_vpu_base_enc_get_property);
	base_class->start             = GST_DEBUG_FUNCPTR(gst_imx_vpu_base_enc_start);
	base_class->stop              = GST_DEBUG_FUNCPTR(gst_imx_vpu_base_enc_stop);
	base_class->set_format        = GST_DEBUG_FUNCPTR(gst_imx_vpu_base_enc_set_format);
	base_class->handle_frame      = GST_DEBUG_FUNCPTR(gst_imx_vpu_base_enc_handle_frame);
	
	klass->inst_counter = 0;

	klass->set_open_params = NULL;
	klass->get_output_caps = NULL;
	klass->set_frame_enc_params = NULL;
	klass->fill_output_buffer = NULL;

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
}


void gst_imx_vpu_base_enc_init(GstImxVpuBaseEnc *vpu_base_enc)
{
	vpu_base_enc->vpu_inst_opened = FALSE;

	vpu_base_enc->gen_second_iframe = FALSE;

	vpu_base_enc->output_phys_buffer = NULL;
	vpu_base_enc->framebuffers = NULL;

	vpu_base_enc->internal_bufferpool = NULL;
	vpu_base_enc->internal_input_buffer = NULL;

	vpu_base_enc->virt_enc_mem_blocks = NULL;
	vpu_base_enc->phys_enc_mem_blocks = NULL;

	vpu_base_enc->gop_size         = DEFAULT_GOP_SIZE;
	vpu_base_enc->bitrate          = DEFAULT_BITRATE;
}




/***************************/
/* miscellaneous functions */

static gboolean gst_imx_vpu_base_enc_alloc_enc_mem_blocks(GstImxVpuBaseEnc *vpu_base_enc)
{
	int i;
	int size;
	unsigned char *ptr;

	for (i = 0; i < vpu_base_enc->mem_info.nSubBlockNum; ++i)
 	{
		size = vpu_base_enc->mem_info.MemSubBlock[i].nAlignment + vpu_base_enc->mem_info.MemSubBlock[i].nSize;
		GST_DEBUG_OBJECT(vpu_base_enc, "sub block %d  type: %s  size: %d", i, (vpu_base_enc->mem_info.MemSubBlock[i].MemType == VPU_MEM_VIRT) ? "virtual" : "phys", size);
 
		if (vpu_base_enc->mem_info.MemSubBlock[i].MemType == VPU_MEM_VIRT)
		{
			if (!gst_imx_vpu_alloc_virt_mem_block(&ptr, size))
				return FALSE;

			vpu_base_enc->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO(ptr, vpu_base_enc->mem_info.MemSubBlock[i].nAlignment);

			gst_imx_vpu_append_virt_mem_block(ptr, &(vpu_base_enc->virt_enc_mem_blocks));
		}
		else if (vpu_base_enc->mem_info.MemSubBlock[i].MemType == VPU_MEM_PHY)
		{
			GstImxPhysMemory *memory = (GstImxPhysMemory *)gst_allocator_alloc(gst_imx_vpu_enc_allocator_obtain(), size, NULL);
			if (memory == NULL)
				return FALSE;

			/* it is OK to use mapped_virt_addr directly without explicit mapping here,
			 * since the VPU encoder allocation functions define a virtual address upon
			 * allocation, so an actual "mapping" does not exist (map just returns
			 * mapped_virt_addr, unmap does nothing) */
			vpu_base_enc->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(memory->mapped_virt_addr), vpu_base_enc->mem_info.MemSubBlock[i].nAlignment);
			vpu_base_enc->mem_info.MemSubBlock[i].pPhyAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(memory->phys_addr), vpu_base_enc->mem_info.MemSubBlock[i].nAlignment);

			gst_imx_vpu_append_phys_mem_block(memory, &(vpu_base_enc->phys_enc_mem_blocks));
		}
		else
		{
			GST_WARNING_OBJECT(vpu_base_enc, "sub block %d type is unknown - skipping", i);
		}
 	}

	return TRUE;
}


static gboolean gst_imx_vpu_base_enc_free_enc_mem_blocks(GstImxVpuBaseEnc *vpu_base_enc)
{
	gboolean ret1, ret2;
	/* NOT using the two calls with && directly, since otherwise an early exit could happen; in other words,
	 * if the first call failed, the second one wouldn't even be invoked
	 * doing the logical AND afterwards fixes this */
	ret1 = gst_imx_vpu_free_virt_mem_blocks(&(vpu_base_enc->virt_enc_mem_blocks));
	ret2 = gst_imx_vpu_free_phys_mem_blocks((GstImxPhysMemAllocator *)gst_imx_vpu_enc_allocator_obtain(), &(vpu_base_enc->phys_enc_mem_blocks));
	return ret1 && ret2;
}


static void gst_imx_vpu_base_enc_close_encoder(GstImxVpuBaseEnc *vpu_base_enc)
{
	VpuEncRetCode enc_ret;

	if (vpu_base_enc->internal_input_buffer != NULL)
		gst_buffer_unref(vpu_base_enc->internal_input_buffer);
	if (vpu_base_enc->internal_bufferpool != NULL)
		gst_object_unref(vpu_base_enc->internal_bufferpool);

	if (vpu_base_enc->output_phys_buffer != NULL)
	{
		gst_allocator_free(gst_imx_vpu_enc_allocator_obtain(), (GstMemory *)(vpu_base_enc->output_phys_buffer));
		vpu_base_enc->output_phys_buffer = NULL;
	}

	if (vpu_base_enc->vpu_inst_opened)
	{
		enc_ret = VPU_EncClose(vpu_base_enc->handle);
		if (enc_ret != VPU_ENC_RET_SUCCESS)
			GST_ERROR_OBJECT(vpu_base_enc, "closing encoder failed: %s", gst_imx_vpu_strerror(enc_ret));

		vpu_base_enc->vpu_inst_opened = FALSE;
	}
}


static void gst_imx_vpu_base_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuBaseEnc *vpu_base_enc = GST_IMX_VPU_BASE_ENC(object);

	switch (prop_id)
	{
		case PROP_GOP_SIZE:
			vpu_base_enc->gop_size = g_value_get_uint(value);
			break;
		case PROP_BITRATE:
			vpu_base_enc->bitrate = g_value_get_uint(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_base_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuBaseEnc *vpu_base_enc = GST_IMX_VPU_BASE_ENC(object);

	switch (prop_id)
	{
		case PROP_GOP_SIZE:
			g_value_set_uint(value, vpu_base_enc->gop_size);
			break;
		case PROP_BITRATE:
			g_value_set_uint(value, vpu_base_enc->bitrate);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_vpu_base_enc_start(GstVideoEncoder *encoder)
{
	VpuEncRetCode ret;
	GstImxVpuBaseEnc *vpu_base_enc;
	GstImxVpuBaseEncClass *klass;

	vpu_base_enc = GST_IMX_VPU_BASE_ENC(encoder);
	klass = GST_IMX_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

#define VPUINIT_ERR(RET, DESC, UNLOAD) \
	if ((RET) != VPU_ENC_RET_SUCCESS) \
	{ \
		g_mutex_unlock(&inst_counter_mutex); \
		GST_ELEMENT_ERROR(vpu_base_enc, LIBRARY, INIT, ("%s: %s", (DESC), gst_imx_vpu_strerror(RET)), (NULL)); \
		if (UNLOAD) \
			VPU_EncUnLoad(); \
		return FALSE; \
	}

	g_mutex_lock(&inst_counter_mutex);
	if (klass->inst_counter == 0)
	{
		ret = VPU_EncLoad();
		VPUINIT_ERR(ret, "loading VPU encoder failed", FALSE);

		{
			VpuVersionInfo version;
			VpuWrapperVersionInfo wrapper_version;

			ret = VPU_EncGetVersionInfo(&version);
			VPUINIT_ERR(ret, "getting version info failed", TRUE);

			ret = VPU_EncGetWrapperVersionInfo(&wrapper_version);
			VPUINIT_ERR(ret, "getting wrapper version info failed", TRUE);

			GST_INFO_OBJECT(vpu_base_enc, "VPU encoder loaded");
			GST_INFO_OBJECT(vpu_base_enc, "VPU firmware version %d.%d.%d_r%d", version.nFwMajor, version.nFwMinor, version.nFwRelease, version.nFwCode);
			GST_INFO_OBJECT(vpu_base_enc, "VPU library version %d.%d.%d", version.nLibMajor, version.nLibMinor, version.nLibRelease);
			GST_INFO_OBJECT(vpu_base_enc, "VPU wrapper version %d.%d.%d %s", wrapper_version.nMajor, wrapper_version.nMinor, wrapper_version.nRelease, wrapper_version.pBinary);
		}
	}
	++klass->inst_counter;
	g_mutex_unlock(&inst_counter_mutex);

	/* mem_info contains information about how to set up memory blocks
	 * the VPU uses as temporary storage (they are "work buffers") */
	memset(&(vpu_base_enc->mem_info), 0, sizeof(VpuMemInfo));
	ret = VPU_EncQueryMem(&(vpu_base_enc->mem_info));
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "could not get VPU memory information: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	/* Allocate the work buffers
	 * Note that these are independent of encoder instances, so they
	 * are allocated before the VPU_EncOpen() call, and are not
	 * recreated in set_format */
	if (!gst_imx_vpu_base_enc_alloc_enc_mem_blocks(vpu_base_enc))
		return FALSE;

#undef VPUINIT_ERR

	/* The encoder is initialized in set_format, not here, since only then the input bitstream
	 * format is known (and this information is necessary for initialization). */

	return TRUE;
}


static gboolean gst_imx_vpu_base_enc_stop(GstVideoEncoder *encoder)
{
	gboolean ret;
	VpuEncRetCode enc_ret;
	GstImxVpuBaseEnc *vpu_base_enc;
	GstImxVpuBaseEncClass *klass;

	ret = TRUE;

	vpu_base_enc = GST_IMX_VPU_BASE_ENC(encoder);
	klass = GST_IMX_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

	if (vpu_base_enc->framebuffers != NULL)
	{
		gst_object_unref(vpu_base_enc->framebuffers);
		vpu_base_enc->framebuffers = NULL;
	}
	if (vpu_base_enc->output_phys_buffer != NULL)
	{
		gst_allocator_free(gst_imx_vpu_enc_allocator_obtain(), (GstMemory *)(vpu_base_enc->output_phys_buffer));
		vpu_base_enc->output_phys_buffer = NULL;
	}

	gst_imx_vpu_base_enc_close_encoder(vpu_base_enc);
	gst_imx_vpu_base_enc_free_enc_mem_blocks(vpu_base_enc);

	g_mutex_lock(&inst_counter_mutex);
	if (klass->inst_counter > 0)
	{
		--klass->inst_counter;
		if (klass->inst_counter == 0)
		{
			enc_ret = VPU_EncUnLoad();
			if (enc_ret != VPU_ENC_RET_SUCCESS)
			{
				GST_ERROR_OBJECT(vpu_base_enc, "unloading VPU encoder failed: %s", gst_imx_vpu_strerror(enc_ret));
			}
			else
				GST_INFO_OBJECT(vpu_base_enc, "VPU encoder unloaded");
		}
	}
	g_mutex_unlock(&inst_counter_mutex);

	vpu_base_enc->gen_second_iframe = FALSE;

	return ret;
}


static gboolean gst_imx_vpu_base_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
	VpuEncRetCode ret;
	GstVideoCodecState *output_state;
	GstImxVpuBaseEncClass *klass;
	GstImxVpuBaseEnc *vpu_base_enc;
	
	vpu_base_enc = GST_IMX_VPU_BASE_ENC(encoder);
	klass = GST_IMX_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

	g_assert(klass->set_open_params != NULL);
	g_assert(klass->get_output_caps != NULL);

	/* Close old encoder instance */
	gst_imx_vpu_base_enc_close_encoder(vpu_base_enc);

	/* Clean up existing framebuffers structure;
	 * if some previous and still existing buffer pools depend on this framebuffers
	 * structure, they will extend its lifetime, since they ref'd it
	 */
	if (vpu_base_enc->framebuffers != NULL)
	{
		gst_object_unref(vpu_base_enc->framebuffers);
		vpu_base_enc->framebuffers = NULL;
	}

	if (vpu_base_enc->output_phys_buffer != NULL)
	{
		gst_allocator_free(gst_imx_vpu_enc_allocator_obtain(), (GstMemory *)(vpu_base_enc->output_phys_buffer));
		vpu_base_enc->output_phys_buffer = NULL;
	}

	memset(&(vpu_base_enc->open_param), 0, sizeof(VpuEncOpenParamSimp));

	/* These params are usually not set by derived classes */
	vpu_base_enc->open_param.nPicWidth = GST_VIDEO_INFO_WIDTH(&(state->info));
	vpu_base_enc->open_param.nPicHeight = GST_VIDEO_INFO_HEIGHT(&(state->info));
	vpu_base_enc->open_param.nFrameRate = (GST_VIDEO_INFO_FPS_N(&(state->info)) & 0xffffUL) | (((GST_VIDEO_INFO_FPS_D(&(state->info)) - 1) & 0xffffUL) << 16);
	vpu_base_enc->open_param.sMirror = VPU_ENC_MIRDIR_NONE; /* don't use VPU mirroring (IPU has better performance) */
	vpu_base_enc->open_param.nBitRate = vpu_base_enc->bitrate;
	vpu_base_enc->open_param.nGOPSize = vpu_base_enc->gop_size;

	GST_DEBUG_OBJECT(vpu_base_enc, "setting bitrate to %u kbps and GOP size to %u", vpu_base_enc->open_param.nBitRate, vpu_base_enc->open_param.nGOPSize);

	/* Give the derived class a chance to set params */
	if (!klass->set_open_params(vpu_base_enc, &(vpu_base_enc->open_param)))
	{
		GST_ERROR_OBJECT(vpu_base_enc, "derived class could not set open params");
		return FALSE;
	}

	/* The actual initialization; requires bitstream information (such as the codec type), which
	 * is determined by the fill_param_set call before */
	ret = VPU_EncOpenSimp(&(vpu_base_enc->handle), &(vpu_base_enc->mem_info), &(vpu_base_enc->open_param));
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "opening new VPU handle failed: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	vpu_base_enc->vpu_inst_opened = TRUE;

	/* configure AFTER setting vpu_inst_opened to TRUE, to make sure that in case of
	   config failure the VPU handle is closed in the finalizer */

	ret = VPU_EncConfig(vpu_base_enc->handle, VPU_ENC_CONF_NONE, NULL);
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "could not apply default configuration: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	ret = VPU_EncGetInitialInfo(vpu_base_enc->handle, &(vpu_base_enc->init_info));
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "retrieving init info failed: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	/* Framebuffers are created in handle_frame(), to make sure the actual stride is used */

	/* Set the output state, using caps defined by the derived class */
	output_state = gst_video_encoder_set_output_state(
		encoder,
		klass->get_output_caps(vpu_base_enc),
		state
	);
	gst_video_codec_state_unref(output_state);

	vpu_base_enc->video_info = state->info;

	vpu_base_enc->gen_second_iframe = FALSE;

	return TRUE;
}


static GstFlowReturn gst_imx_vpu_base_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame)
{
	VpuEncRetCode enc_ret;
	VpuEncEncParam enc_enc_param;
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxVpuBaseEncClass *klass;
	GstImxVpuBaseEnc *vpu_base_enc;
	VpuFrameBuffer input_framebuf;
	GstBuffer *input_buffer;
	gint src_stride;

	vpu_base_enc = GST_IMX_VPU_BASE_ENC(encoder);
	klass = GST_IMX_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

	g_assert(klass->set_frame_enc_params != NULL);

	memset(&enc_enc_param, 0, sizeof(enc_enc_param));
	memset(&input_framebuf, 0, sizeof(input_framebuf));

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(frame->input_buffer);

	/* If the incoming frame's buffer is not using physically contiguous memory,
	 * it needs to be copied to the internal input buffer, otherwise the VPU
	 * encoder cannot read the frame */
	if (phys_mem_meta == NULL)
	{
		/* No physical memory metadata found -> buffer is not physically contiguous */

		GstVideoFrame temp_input_video_frame, temp_incoming_video_frame;

		if (vpu_base_enc->internal_input_buffer == NULL)
		{
			/* The internal input buffer is the temp input frame's DMA memory.
			 * If it does not exist yet, it needs to be created here. The temp input
			 * frame is then mapped. */

			GstFlowReturn flow_ret;

			if (vpu_base_enc->internal_bufferpool == NULL)
			{
				/* Internal bufferpool does not exist yet - create it now,
				 * so that it can in turn create the internal input buffer */

				GstStructure *config;
				GstCaps *caps;
				GstAllocator *allocator;

				caps = gst_video_info_to_caps(&(vpu_base_enc->video_info));
				vpu_base_enc->internal_bufferpool = gst_imx_phys_mem_buffer_pool_new(FALSE);
				allocator = gst_imx_vpu_enc_allocator_obtain();

				config = gst_buffer_pool_get_config(vpu_base_enc->internal_bufferpool);
				gst_buffer_pool_config_set_params(config, caps, vpu_base_enc->video_info.size, 2, 0);
				gst_buffer_pool_config_set_allocator(config, allocator, NULL);
				gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
				gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
				gst_buffer_pool_set_config(vpu_base_enc->internal_bufferpool, config);

				gst_caps_unref(caps);

				if (vpu_base_enc->internal_bufferpool == NULL)
				{
					GST_ERROR_OBJECT(vpu_base_enc, "failed to create internal bufferpool");
					return FALSE;
				}
			}

			/* Future versions of this code may propose the internal bufferpool upstream;
			 * hence the is_active check */
			if (!gst_buffer_pool_is_active(vpu_base_enc->internal_bufferpool))
				gst_buffer_pool_set_active(vpu_base_enc->internal_bufferpool, TRUE);

			/* Create the internal input buffer */
			flow_ret = gst_buffer_pool_acquire_buffer(vpu_base_enc->internal_bufferpool, &(vpu_base_enc->internal_input_buffer), NULL);
			if (flow_ret != GST_FLOW_OK)
			{
				GST_ERROR_OBJECT(vpu_base_enc, "error acquiring input frame buffer: %s", gst_pad_mode_get_name(flow_ret));
				return FALSE;
			}
		}

		/* The internal input buffer exists at this point. Since the incoming frame
		 * is not stored in physical memory, copy its pixels to the internal
		 * input buffer, so the encoder can read them. */

		gst_video_frame_map(&temp_incoming_video_frame, &(vpu_base_enc->video_info), frame->input_buffer, GST_MAP_READ);
		gst_video_frame_map(&temp_input_video_frame, &(vpu_base_enc->video_info), vpu_base_enc->internal_input_buffer, GST_MAP_WRITE);

		gst_video_frame_copy(&temp_input_video_frame, &temp_incoming_video_frame);

		gst_video_frame_unmap(&temp_incoming_video_frame);
		gst_video_frame_unmap(&temp_input_video_frame);

		/* Set the internal input buffer as the encoder's input */
		input_buffer = vpu_base_enc->internal_input_buffer;
		/* And use the internal input buffer's physical memory metadata */
		phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(vpu_base_enc->internal_input_buffer);
	}
	else
	{
		/* Physical memory metadata found -> buffer is physically contiguous
		 * It can be used directly as input for the VPU encoder */
		input_buffer = frame->input_buffer;
	}

	/* Set up physical addresses for the input framebuffer */
	{
		gsize *plane_offsets;
		gint *plane_strides;
		GstVideoMeta *video_meta;
		unsigned char *phys_ptr;

		/* Try to use plane offset and stride information from the video
		 * metadata if present, since these can be more accurate than
		 * the information from the video info */
		video_meta = gst_buffer_get_video_meta(input_buffer);
		if (video_meta != NULL)
		{
			plane_offsets = video_meta->offset;
			plane_strides = video_meta->stride;
		}
		else
		{
			plane_offsets = vpu_base_enc->video_info.offset;
			plane_strides = vpu_base_enc->video_info.stride;
		}

		phys_ptr = (unsigned char*)(phys_mem_meta->phys_addr);

		input_framebuf.pbufY = phys_ptr;
		input_framebuf.pbufCb = phys_ptr + plane_offsets[1];
		input_framebuf.pbufCr = phys_ptr + plane_offsets[2];
		input_framebuf.pbufMvCol = NULL; /* not used by the VPU encoder */
		input_framebuf.nStrideY = plane_strides[0];
		input_framebuf.nStrideC = plane_strides[1];

		/* this is needed for framebuffers registration below */
		src_stride = plane_strides[0];

		GST_TRACE_OBJECT(vpu_base_enc, "width: %d   height: %d   stride 0: %d   stride 1: %d   offset 0: %d   offset 1: %d   offset 2: %d", GST_VIDEO_INFO_WIDTH(&(vpu_base_enc->video_info)), GST_VIDEO_INFO_HEIGHT(&(vpu_base_enc->video_info)), plane_strides[0], plane_strides[1], plane_offsets[0], plane_offsets[1], plane_offsets[2]);
	}

	/* Create framebuffers structure (if not already present) */
	if (vpu_base_enc->framebuffers == NULL)
	{
		GstImxVpuFramebufferParams fbparams;
		gst_imx_vpu_framebuffers_enc_init_info_to_params(&(vpu_base_enc->init_info), &fbparams);
		fbparams.pic_width = vpu_base_enc->open_param.nPicWidth;
		fbparams.pic_height = vpu_base_enc->open_param.nPicHeight;
		vpu_base_enc->framebuffers = gst_imx_vpu_framebuffers_new(&fbparams, gst_imx_vpu_enc_allocator_obtain());
		gst_imx_vpu_framebuffers_register_with_encoder(vpu_base_enc->framebuffers, vpu_base_enc->handle, src_stride);
	}

	/* Allocate physical buffer for output data (if not already present) */
	if (vpu_base_enc->output_phys_buffer == NULL)
	{
		vpu_base_enc->output_phys_buffer = (GstImxPhysMemory *)gst_allocator_alloc(gst_imx_vpu_enc_allocator_obtain(), vpu_base_enc->framebuffers->total_size, NULL);

		if (vpu_base_enc->output_phys_buffer == NULL)
		{
			GST_ERROR_OBJECT(vpu_base_enc, "could not allocate physical buffer for output data");
			return GST_FLOW_ERROR;
		}
	}

	/* Set up encoding parameters */
	enc_enc_param.nInVirtOutput = (unsigned int)(vpu_base_enc->output_phys_buffer->mapped_virt_addr); /* TODO */
	enc_enc_param.nInPhyOutput = (unsigned int)(vpu_base_enc->output_phys_buffer->phys_addr);
	enc_enc_param.nInOutputBufLen = vpu_base_enc->output_phys_buffer->mem.size;
	enc_enc_param.nPicWidth = vpu_base_enc->framebuffers->pic_width;
	enc_enc_param.nPicHeight = vpu_base_enc->framebuffers->pic_height;
	enc_enc_param.nFrameRate = vpu_base_enc->open_param.nFrameRate;
	enc_enc_param.pInFrame = &input_framebuf;
	enc_enc_param.nForceIPicture = 0;

	/* Force I-frame if either IS_FORCE_KEYFRAME is set for the current frame,
	 * or if the previous frame was a forced I-frame.
	 * Several encoder elements such as x264enc generate two I-frames when IS_FORCE_KEYFRAME
	 * is set. If only one is generated, h264parse may miss the SPS/PPS headers.
	 * TODO: find a more detailed explanation as to why this is necessary. */
	if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(frame))
	{
		vpu_base_enc->gen_second_iframe = TRUE;
		enc_enc_param.nForceIPicture = 1;
		GST_DEBUG_OBJECT(vpu_base_enc, "Got request to make this a keyframe - forcing first I frame");
	}
	else if (vpu_base_enc->gen_second_iframe)
	{
		vpu_base_enc->gen_second_iframe = FALSE;
		enc_enc_param.nForceIPicture = 1;
		GST_DEBUG_OBJECT(vpu_base_enc, "Last frame was a keyframe upon request - forcing second I frame");
	}

	/* Give the derived class a chance to set up encoding parameters too */
	if (!klass->set_frame_enc_params(vpu_base_enc, &enc_enc_param, &(vpu_base_enc->open_param)))
	{
		GST_ERROR_OBJECT(vpu_base_enc, "derived class could not frame enc params");
		return GST_FLOW_ERROR;
	}

	/* Perform the frame encoding */
	enc_ret = VPU_EncEncodeFrame(vpu_base_enc->handle, &enc_enc_param);
	if (enc_ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "failed to encode frame: %s", gst_imx_vpu_strerror(enc_ret));
		VPU_EncReset(vpu_base_enc->handle);
		return GST_FLOW_ERROR;
	}

	GST_LOG_OBJECT(vpu_base_enc, "out ret code: 0x%x  out size: %u", enc_enc_param.eOutRetCode, enc_enc_param.nOutOutputSize);

	/* Output contains a header, or an encoded frame, or both
	 * -> copy encoded data to the frame's output buffer */
	if (enc_enc_param.eOutRetCode & (VPU_ENC_OUTPUT_DIS | VPU_ENC_OUTPUT_SEQHEADER))
	{
		if (klass->fill_output_buffer != NULL)
		{
			/* The derived data can handle the output buffer filling on its own;
			 * allocate an output frame that is as large as the input frame.
			 * The derived class may want to insert data (for example, SPS/PPS headers
			 * in h.264 NAL streams). Since the output data is typically much smaller
			 * than the input frame, this gives the derived class enough room for
			 * additional data. */
			gsize actual_output_size;
			gst_video_encoder_allocate_output_frame(encoder, frame, vpu_base_enc->output_phys_buffer->mem.size);

			actual_output_size = klass->fill_output_buffer(vpu_base_enc, frame, vpu_base_enc->output_phys_buffer->mapped_virt_addr, enc_enc_param.nOutOutputSize, enc_enc_param.eOutRetCode & VPU_ENC_OUTPUT_SEQHEADER);

			/* Set the output buffer's size to the actual number of bytes
			 * filled by the derived class */
			gst_buffer_set_size(frame->output_buffer, actual_output_size);
		}
		else
		{
			/* The derived class does not handle output buffer filling, so simply copy
			 * the encoded data to the output buffer. */
			gst_video_encoder_allocate_output_frame(encoder, frame, enc_enc_param.nOutOutputSize);
			gst_buffer_fill(frame->output_buffer, 0, vpu_base_enc->output_phys_buffer->mapped_virt_addr, enc_enc_param.nOutOutputSize);
		}

		/* Either way, the frame is finished */
		gst_video_encoder_finish_frame(encoder, frame);
	}

	return GST_FLOW_OK;
}

