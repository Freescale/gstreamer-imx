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
#include "../../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(vpu_base_enc_debug);
#define GST_CAT_DEFAULT vpu_base_enc_debug


#define ALIGN_VAL_TO(LENGTH, ALIGN_SIZE)  ( ((guintptr)((LENGTH) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )


static GMutex inst_counter_mutex;


G_DEFINE_ABSTRACT_TYPE(GstFslVpuBaseEnc, gst_fsl_vpu_base_enc, GST_TYPE_VIDEO_ENCODER)


/* miscellaneous functions */
static gboolean gst_fsl_vpu_base_enc_alloc_enc_mem_blocks(GstFslVpuBaseEnc *vpu_base_enc);
static gboolean gst_fsl_vpu_base_enc_free_enc_mem_blocks(GstFslVpuBaseEnc *vpu_base_enc);
static void gst_fsl_vpu_base_enc_close_encoder(GstFslVpuBaseEnc *vpu_base_enc);

/* functions for the base class */
static gboolean gst_fsl_vpu_base_enc_start(GstVideoEncoder *encoder);
static gboolean gst_fsl_vpu_base_enc_stop(GstVideoEncoder *encoder);
static gboolean gst_fsl_vpu_base_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static GstFlowReturn gst_fsl_vpu_base_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame);




/* required function declared by G_DEFINE_TYPE */

void gst_fsl_vpu_base_enc_class_init(GstFslVpuBaseEncClass *klass)
{
	GstVideoEncoderClass *base_class;

	GST_DEBUG_CATEGORY_INIT(vpu_base_enc_debug, "vpubaseenc", 0, "Freescale VPU video encoder base class");

	base_class = GST_VIDEO_ENCODER_CLASS(klass);

	base_class->start             = GST_DEBUG_FUNCPTR(gst_fsl_vpu_base_enc_start);
	base_class->stop              = GST_DEBUG_FUNCPTR(gst_fsl_vpu_base_enc_stop);
	base_class->set_format        = GST_DEBUG_FUNCPTR(gst_fsl_vpu_base_enc_set_format);
	base_class->handle_frame      = GST_DEBUG_FUNCPTR(gst_fsl_vpu_base_enc_handle_frame);

	
	klass->inst_counter = 0;
}


void gst_fsl_vpu_base_enc_init(GstFslVpuBaseEnc *vpu_base_enc)
{
	vpu_base_enc->vpu_inst_opened = FALSE;

	vpu_base_enc->output_phys_buffer = NULL;
	vpu_base_enc->framebuffers = NULL;

	vpu_base_enc->virt_enc_mem_blocks = NULL;
	vpu_base_enc->phys_enc_mem_blocks = NULL;
}




/***************************/
/* miscellaneous functions */

static gboolean gst_fsl_vpu_base_enc_alloc_enc_mem_blocks(GstFslVpuBaseEnc *vpu_base_enc)
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
			if (!gst_fsl_vpu_alloc_virt_mem_block(&ptr, size))
				return FALSE;

			vpu_base_enc->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO(ptr, vpu_base_enc->mem_info.MemSubBlock[i].nAlignment);

			gst_fsl_vpu_append_virt_mem_block(ptr, &(vpu_base_enc->virt_enc_mem_blocks));
		}
		else if (vpu_base_enc->mem_info.MemSubBlock[i].MemType == VPU_MEM_PHY)
		{
			GstFslPhysMemory *memory = (GstFslPhysMemory *)gst_allocator_alloc(gst_fsl_vpu_enc_allocator_obtain(), size, NULL);
			if (memory == NULL)
				return FALSE;

			/* it is OK to use mapped_virt_addr directly without explicit mapping here,
			 * since the VPU encoder allocation functions define a virtual address upon
			 * allocation, so an actual "mapping" does not exist (map just returns
			 * mapped_virt_addr, unmap does nothing) */
			vpu_base_enc->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(memory->mapped_virt_addr), vpu_base_enc->mem_info.MemSubBlock[i].nAlignment);
			vpu_base_enc->mem_info.MemSubBlock[i].pPhyAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(memory->phys_addr), vpu_base_enc->mem_info.MemSubBlock[i].nAlignment);

			gst_fsl_vpu_append_phys_mem_block(memory, &(vpu_base_enc->phys_enc_mem_blocks));
		}
		else
		{
			GST_WARNING_OBJECT(vpu_base_enc, "sub block %d type is unknown - skipping", i);
		}
 	}

	return TRUE;
}


static gboolean gst_fsl_vpu_base_enc_free_enc_mem_blocks(GstFslVpuBaseEnc *vpu_base_enc)
{
	gboolean ret1, ret2;
	/* NOT using the two calls with && directly, since otherwise an early exit could happen; in other words,
	 * if the first call failed, the second one wouldn't even be invoked
	 * doing the logical AND afterwards fixes this */
	ret1 = gst_fsl_vpu_free_virt_mem_blocks(&(vpu_base_enc->virt_enc_mem_blocks));
	ret2 = gst_fsl_vpu_free_phys_mem_blocks((GstFslPhysMemAllocator *)gst_fsl_vpu_enc_allocator_obtain(), &(vpu_base_enc->phys_enc_mem_blocks));
	return ret1 && ret2;
}


static void gst_fsl_vpu_base_enc_close_encoder(GstFslVpuBaseEnc *vpu_base_enc)
{
	VpuEncRetCode enc_ret;

	if (vpu_base_enc->output_phys_buffer != NULL)
	{
		gst_allocator_free(gst_fsl_vpu_enc_allocator_obtain(), (GstMemory *)(vpu_base_enc->output_phys_buffer));
		vpu_base_enc->output_phys_buffer = NULL;
	}

	if (vpu_base_enc->vpu_inst_opened)
	{
		enc_ret = VPU_EncClose(vpu_base_enc->handle);
		if (enc_ret != VPU_ENC_RET_SUCCESS)
			GST_ERROR_OBJECT(vpu_base_enc, "closing encoder failed: %s", gst_fsl_vpu_strerror(enc_ret));

		vpu_base_enc->vpu_inst_opened = FALSE;
	}
}


static gboolean gst_fsl_vpu_base_enc_start(GstVideoEncoder *encoder)
{
	VpuEncRetCode ret;
	GstFslVpuBaseEnc *vpu_base_enc;
	GstFslVpuBaseEncClass *klass;

	vpu_base_enc = GST_FSL_VPU_BASE_ENC(encoder);
	klass = GST_FSL_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

#define VPUINIT_ERR(RET, DESC, UNLOAD) \
	if ((RET) != VPU_ENC_RET_SUCCESS) \
	{ \
		g_mutex_unlock(&inst_counter_mutex); \
		GST_ELEMENT_ERROR(vpu_base_enc, LIBRARY, INIT, ("%s: %s", (DESC), gst_fsl_vpu_strerror(RET)), (NULL)); \
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
		GST_ERROR_OBJECT(vpu_base_enc, "could not get VPU memory information: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	/* Allocate the work buffers
	 * Note that these are independent of encoder instances, so they
	 * are allocated before the VPU_EncOpen() call, and are not
	 * recreated in set_format */
	if (!gst_fsl_vpu_base_enc_alloc_enc_mem_blocks(vpu_base_enc))
		return FALSE;

#undef VPUINIT_ERR

	/* The encoder is initialized in set_format, not here, since only then the input bitstream
	 * format is known (and this information is necessary for initialization). */

	return TRUE;
}


static gboolean gst_fsl_vpu_base_enc_stop(GstVideoEncoder *encoder)
{
	gboolean ret;
	VpuEncRetCode enc_ret;
	GstFslVpuBaseEnc *vpu_base_enc;
	GstFslVpuBaseEncClass *klass;

	ret = TRUE;

	vpu_base_enc = GST_FSL_VPU_BASE_ENC(encoder);
	klass = GST_FSL_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

	gst_fsl_vpu_base_enc_close_encoder(vpu_base_enc);
	gst_fsl_vpu_base_enc_free_enc_mem_blocks(vpu_base_enc);

	g_mutex_lock(&inst_counter_mutex);
	if (klass->inst_counter > 0)
	{
		--klass->inst_counter;
		if (klass->inst_counter == 0)
		{
			enc_ret = VPU_EncUnLoad();
			if (enc_ret != VPU_ENC_RET_SUCCESS)
			{
				GST_ERROR_OBJECT(vpu_base_enc, "unloading VPU encoder failed: %s", gst_fsl_vpu_strerror(enc_ret));
			}
			else
				GST_INFO_OBJECT(vpu_base_enc, "VPU encoder unloaded");
		}
	}
	g_mutex_unlock(&inst_counter_mutex);

	return ret;
}


static gboolean gst_fsl_vpu_base_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
	VpuEncRetCode ret;
	GstVideoCodecState *output_state;
	GstFslVpuBaseEncClass *klass;
	GstFslVpuBaseEnc *vpu_base_enc;
	
	vpu_base_enc = GST_FSL_VPU_BASE_ENC(encoder);
	klass = GST_FSL_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

	g_assert(klass->set_open_params != NULL);
	g_assert(klass->get_output_caps != NULL);

	/* Close old encoder instance */
	gst_fsl_vpu_base_enc_close_encoder(vpu_base_enc);

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
		gst_allocator_free(gst_fsl_vpu_enc_allocator_obtain(), (GstMemory *)(vpu_base_enc->output_phys_buffer));
		vpu_base_enc->output_phys_buffer = NULL;
	}

	memset(&(vpu_base_enc->open_param), 0, sizeof(VpuEncOpenParam));

	/* These params are usually not set by derived classes */
	// TODO: do width&height have to be aligned to 16-pixel boundaries here?
	vpu_base_enc->open_param.nPicWidth = GST_VIDEO_INFO_WIDTH(&(state->info));
	vpu_base_enc->open_param.nPicHeight = GST_VIDEO_INFO_HEIGHT(&(state->info));
	vpu_base_enc->open_param.nFrameRate = (GST_VIDEO_INFO_FPS_N(&(state->info)) & 0xffffUL) | (((GST_VIDEO_INFO_FPS_D(&(state->info)) - 1) & 0xffffUL) << 16);
	/* These params are defaults, and are usually overwritten by derived classes */
	vpu_base_enc->open_param.sMirror = VPU_ENC_MIRDIR_NONE;
	vpu_base_enc->open_param.nGOPSize = 16;
	vpu_base_enc->open_param.nUserQpMax = -1;
	vpu_base_enc->open_param.nUserQpMin = -1;
	vpu_base_enc->open_param.nRcIntraQp = -1;
	vpu_base_enc->open_param.nUserGamma = 75 * 32768 / 100;
	vpu_base_enc->open_param.nRcIntervalMode = 1;

	/* Give the derived class a chance to set params */
	if (!klass->set_open_params(vpu_base_enc, &(vpu_base_enc->open_param)))
	{
		GST_ERROR_OBJECT(vpu_base_enc, "derived class could not set open params");
		return FALSE;
	}

	/* The actual initialization; requires bitstream information (such as the codec type), which
	 * is determined by the fill_param_set call before */
	ret = VPU_EncOpen(&(vpu_base_enc->handle), &(vpu_base_enc->mem_info), &(vpu_base_enc->open_param));
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "opening new VPU handle failed: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	vpu_base_enc->vpu_inst_opened = TRUE;

	/* configure AFTER setting vpu_inst_opened to TRUE, to make sure that in case of
	   config failure the VPU handle is closed in the finalizer */

	ret = VPU_EncConfig(vpu_base_enc->handle, VPU_ENC_CONF_NONE, NULL);
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "could not apply default configuration: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	ret = VPU_EncGetInitialInfo(vpu_base_enc->handle, &(vpu_base_enc->init_info));
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "retrieving init info failed: %s", gst_fsl_vpu_strerror(ret));
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

	return TRUE;
}


static GstFlowReturn gst_fsl_vpu_base_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *frame)
{
	VpuEncRetCode enc_ret;
	VpuEncEncParam enc_enc_param;
	GstFslPhysMemMeta *phys_mem_meta;
	GstFslVpuBaseEncClass *klass;
	GstFslVpuBaseEnc *vpu_base_enc;
	VpuFrameBuffer input_framebuf;

	vpu_base_enc = GST_FSL_VPU_BASE_ENC(encoder);
	klass = GST_FSL_VPU_BASE_ENC_CLASS(G_OBJECT_GET_CLASS(vpu_base_enc));

	g_assert(klass->set_frame_enc_params != NULL);

	memset(&enc_enc_param, 0, sizeof(enc_enc_param));
	memset(&input_framebuf, 0, sizeof(input_framebuf));

	if ((phys_mem_meta = GST_FSL_PHYS_MEM_META_GET(frame->input_buffer)) != NULL)
	{
		gsize *plane_offsets;
		gint *plane_strides;
		GstVideoMeta *video_meta;
		unsigned char *phys_ptr;

		video_meta = gst_buffer_get_video_meta(frame->input_buffer);
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
		input_framebuf.pbufMvCol = NULL;
		input_framebuf.nStrideY = plane_strides[0];
		input_framebuf.nStrideC = plane_strides[1];

		GST_DEBUG_OBJECT(vpu_base_enc, "width: %d   height: %d   stride 0: %d   stride 1: %d   offset 0: %d   offset 1: %d   offset 2: %d", GST_VIDEO_INFO_WIDTH(&(vpu_base_enc->video_info)), GST_VIDEO_INFO_HEIGHT(&(vpu_base_enc->video_info)), plane_strides[0], plane_strides[1], plane_offsets[0], plane_offsets[1], plane_offsets[2]);

		if (vpu_base_enc->framebuffers == NULL)
		{
			GstFslVpuFramebufferParams fbparams;
			gst_fsl_vpu_framebuffers_enc_init_info_to_params(&(vpu_base_enc->init_info), &fbparams);
			fbparams.pic_width = vpu_base_enc->open_param.nPicWidth;
			fbparams.pic_height = vpu_base_enc->open_param.nPicHeight;
			vpu_base_enc->framebuffers = gst_fsl_vpu_framebuffers_new(&fbparams, gst_fsl_vpu_enc_allocator_obtain());
			gst_fsl_vpu_framebuffers_register_with_encoder(vpu_base_enc->framebuffers, vpu_base_enc->handle, plane_strides[0]);
		}

		if (vpu_base_enc->output_phys_buffer == NULL)
		{
			vpu_base_enc->output_phys_buffer = (GstFslPhysMemory *)gst_allocator_alloc(gst_fsl_vpu_enc_allocator_obtain(), vpu_base_enc->framebuffers->total_size, NULL);

			if (vpu_base_enc->output_phys_buffer == NULL)
			{
				GST_ERROR_OBJECT(vpu_base_enc, "could not allocate physical buffer for output data");
				return GST_FLOW_ERROR;
			}
		}
	}
	else
	{
		GST_ERROR_OBJECT(vpu_base_enc, "buffer is not physically contiguous");
		return GST_FLOW_ERROR;
	}

	enc_enc_param.nInVirtOutput = (unsigned int)(vpu_base_enc->output_phys_buffer->mapped_virt_addr); /* TODO */
	enc_enc_param.nInPhyOutput = (unsigned int)(vpu_base_enc->output_phys_buffer->phys_addr);
	enc_enc_param.nInOutputBufLen = vpu_base_enc->output_phys_buffer->mem.size;
	enc_enc_param.nPicWidth = vpu_base_enc->framebuffers->pic_width;
	enc_enc_param.nPicHeight = vpu_base_enc->framebuffers->pic_height;
	enc_enc_param.nFrameRate = vpu_base_enc->open_param.nFrameRate;
	enc_enc_param.pInFrame = &input_framebuf;

	if (!klass->set_frame_enc_params(vpu_base_enc, &enc_enc_param, &(vpu_base_enc->open_param)))
	{
		GST_ERROR_OBJECT(vpu_base_enc, "derived class could not frame enc params");
		return GST_FLOW_ERROR;
	}

	enc_ret = VPU_EncEncodeFrame(vpu_base_enc->handle, &enc_enc_param);
	if (enc_ret != VPU_ENC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_base_enc, "failed to encode frame: %s", gst_fsl_vpu_strerror(enc_ret));
		VPU_EncReset(vpu_base_enc->handle);
		return GST_FLOW_ERROR;
	}

	GST_LOG_OBJECT(vpu_base_enc, "out ret code: 0x%x  out size: %u", enc_enc_param.eOutRetCode, enc_enc_param.nOutOutputSize);

	if ((enc_enc_param.eOutRetCode & VPU_ENC_OUTPUT_DIS) || (enc_enc_param.eOutRetCode & VPU_ENC_OUTPUT_SEQHEADER))
	{
		gst_video_encoder_allocate_output_frame(encoder, frame, enc_enc_param.nOutOutputSize);
		gst_buffer_fill(frame->output_buffer, 0, vpu_base_enc->output_phys_buffer->mapped_virt_addr, enc_enc_param.nOutOutputSize);
		gst_video_encoder_finish_frame(encoder, frame);
	}

	return GST_FLOW_OK;
}

