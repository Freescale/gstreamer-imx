/* imxvpuapi implementation on top of the Freescale VPU wrapper
 * Copyright (C) 2014 Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */


#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <vpu_wrapper.h>
#include "imxvpuapi.h"
#include "imxvpuapi_priv.h"




/***********************************************/
/******* COMMON STRUCTURES AND FUNCTIONS *******/
/***********************************************/


#ifndef TRUE
#define TRUE (1)
#endif


#ifndef FALSE
#define FALSE (0)
#endif


#ifndef BOOL
#define BOOL int
#endif


static ImxVpuColorFormat convert_from_wrapper_color_format(int format)
{
	return (ImxVpuColorFormat)format;
}


static int convert_to_wrapper_color_format(ImxVpuColorFormat format)
{
	return (int)format;
}


static ImxVpuFrameType convert_from_wrapper_pic_type(VpuPicType type)
{
	switch (type)
	{
		case VPU_I_PIC:    return IMX_VPU_FRAME_TYPE_I;
		case VPU_P_PIC:    return IMX_VPU_FRAME_TYPE_P;
		case VPU_B_PIC:    return IMX_VPU_FRAME_TYPE_B;
		case VPU_IDR_PIC:  return IMX_VPU_FRAME_TYPE_IDR;
		case VPU_BI_PIC:   return IMX_VPU_FRAME_TYPE_BI;
		case VPU_SKIP_PIC: return IMX_VPU_FRAME_TYPE_SKIP;
		default: return IMX_VPU_FRAME_TYPE_UNKNOWN;
	}
}


static ImxVpuInterlacingMode convert_from_wrapper_field_type(VpuFieldType type)
{
	switch (type)
	{
		case VPU_FIELD_NONE:   return IMX_VPU_INTERLACING_MODE_NO_INTERLACING;
		case VPU_FIELD_TOP:    return IMX_VPU_INTERLACING_MODE_TOP_FIELD_ONLY;
		case VPU_FIELD_BOTTOM: return IMX_VPU_INTERLACING_MODE_BOTTOM_FIELD_ONLY;
		case VPU_FIELD_TB:     return IMX_VPU_INTERLACING_MODE_TOP_FIELD_FIRST;
		case VPU_FIELD_BT:     return IMX_VPU_INTERLACING_MODE_BOTTOM_FIELD_FIRST;
		default: return IMX_VPU_INTERLACING_MODE_UNKNOWN;
	}
}


static VpuCodStd convert_to_wrapper_codec_std(ImxVpuCodecFormat format)
{
	switch (format)
	{
		case IMX_VPU_CODEC_FORMAT_MPEG4:    return VPU_V_MPEG4;
		case IMX_VPU_CODEC_FORMAT_H263:     return VPU_V_H263;
		case IMX_VPU_CODEC_FORMAT_H264:     return VPU_V_AVC;
		case IMX_VPU_CODEC_FORMAT_WMV3:     return VPU_V_VC1;
		case IMX_VPU_CODEC_FORMAT_WVC1:     return VPU_V_VC1_AP;
		case IMX_VPU_CODEC_FORMAT_MPEG2:    return VPU_V_MPEG2;
		case IMX_VPU_CODEC_FORMAT_MJPEG:    return VPU_V_MJPG;
		case IMX_VPU_CODEC_FORMAT_VP8:      return VPU_V_VP8;
		default: assert(FALSE);
	}

	return VPU_V_MPEG2; /* should never be reached */
}




/**************************************************/
/******* ALLOCATOR STRUCTURES AND FUNCTIONS *******/
/**************************************************/


/*********** Default allocator ***********/


typedef struct
{
	ImxVpuDMABuffer parent;
	VpuMemDesc mem_desc;

	size_t size;

	uint8_t*            aligned_virtual_address;
	imx_vpu_phys_addr_t aligned_physical_address;
}
DefaultDMABuffer;


typedef struct
{
	ImxVpuDMABufferAllocator parent;
	int enc_allocator; /* 0 = decoder allocator  1 = encoder allocator */
}
DefaultDMABufferAllocator;


static ImxVpuDecReturnCodes dec_convert_retcode(VpuDecRetCode code);
static ImxVpuEncReturnCodes enc_convert_retcode(VpuEncRetCode code);


static ImxVpuDMABuffer* default_dmabufalloc_allocate(ImxVpuDMABufferAllocator *allocator, size_t size, unsigned int alignment, unsigned int flags)
{
	IMXVPUAPI_UNUSED_PARAM(flags);

	VpuDecRetCode ret, ok_ret;
	char const *errmsg;

	DefaultDMABufferAllocator *defallocator = (DefaultDMABufferAllocator *)allocator;

	DefaultDMABuffer *dmabuffer = IMX_VPU_ALLOC(sizeof(DefaultDMABuffer));
	if (dmabuffer == NULL)
	{
		IMX_VPU_ERROR("allocating heap block for DMA buffer failed");
		return NULL;
	}

	dmabuffer->mem_desc.nSize = size;

	if (alignment == 0)
		alignment = 1;
	if (alignment > 1)
		dmabuffer->mem_desc.nSize += alignment;

	dmabuffer->parent.allocator = allocator;
	dmabuffer->size = size;

	if (defallocator->enc_allocator)
	{
		ret = VPU_EncGetMem(&(dmabuffer->mem_desc));
		ok_ret = VPU_ENC_RET_SUCCESS;
		errmsg = imx_vpu_enc_error_string(enc_convert_retcode(ret));
	}
	else
	{
		ret = VPU_DecGetMem(&(dmabuffer->mem_desc));
		ok_ret = VPU_DEC_RET_SUCCESS;
		errmsg = imx_vpu_dec_error_string(dec_convert_retcode(ret));
	}

	if (ret != ok_ret)
	{
		IMX_VPU_FREE(dmabuffer, sizeof(DefaultDMABuffer));
		IMX_VPU_ERROR("allocating %d bytes of physical memory failed: %s", size, errmsg);
		return NULL;
	}
	else
		IMX_VPU_TRACE("allocated %d bytes of physical memory", size);

	dmabuffer->aligned_virtual_address = (void *)IMX_VPU_ALIGN_VAL_TO((void *)(dmabuffer->mem_desc.nVirtAddr), alignment);
	dmabuffer->aligned_physical_address = (imx_vpu_phys_addr_t)IMX_VPU_ALIGN_VAL_TO((imx_vpu_phys_addr_t)(dmabuffer->mem_desc.nPhyAddr), alignment);

	return (ImxVpuDMABuffer *)dmabuffer;
}


static void default_dmabufalloc_deallocate(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);

	ImxVpuDecReturnCodes ret;
	DefaultDMABuffer *defaultbuf = (DefaultDMABuffer *)buffer;

	ret = dec_convert_retcode(VPU_DecFreeMem(&(defaultbuf->mem_desc)));
	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
		IMX_VPU_ERROR("deallocating %d bytes of physical memory failed: %s", defaultbuf->size, imx_vpu_dec_error_string(ret));
	else
		IMX_VPU_TRACE("deallocated %d bytes of physical memory", defaultbuf->size);

	IMX_VPU_FREE(defaultbuf, sizeof(DefaultDMABuffer));
}


static uint8_t* default_dmabufalloc_map(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer, unsigned int flags)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	IMXVPUAPI_UNUSED_PARAM(flags);

	DefaultDMABuffer *defaultbuf = (DefaultDMABuffer *)buffer;
	return defaultbuf->aligned_virtual_address;
}


static void default_dmabufalloc_unmap(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	IMXVPUAPI_UNUSED_PARAM(buffer);
}


int default_dmabufalloc_get_fd(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	IMXVPUAPI_UNUSED_PARAM(buffer);
	return -1;
}


imx_vpu_phys_addr_t default_dmabufalloc_get_physical_address(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	DefaultDMABuffer *defaultbuf = (DefaultDMABuffer *)buffer;
	return defaultbuf->aligned_physical_address;
}


size_t default_dmabufalloc_get_size(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	DefaultDMABuffer *defaultbuf = (DefaultDMABuffer *)buffer;
	return defaultbuf->size;
}




/******************************************************/
/******* MISCELLANEOUS STRUCTURES AND FUNCTIONS *******/
/******************************************************/


#define FRAME_ALIGN 16


void imx_vpu_calc_framebuffer_sizes(ImxVpuColorFormat color_format, unsigned int frame_width, unsigned int frame_height, unsigned int framebuffer_alignment, int uses_interlacing, int chroma_interleave, ImxVpuFramebufferSizes *calculated_sizes)
{
	int alignment;

	assert(calculated_sizes != NULL);
	assert(frame_width > 0);
	assert(frame_height > 0);

	calculated_sizes->aligned_frame_width = IMX_VPU_ALIGN_VAL_TO(frame_width, FRAME_ALIGN);
	if (uses_interlacing)
		calculated_sizes->aligned_frame_height = IMX_VPU_ALIGN_VAL_TO(frame_height, (2 * FRAME_ALIGN));
	else
		calculated_sizes->aligned_frame_height = IMX_VPU_ALIGN_VAL_TO(frame_height, FRAME_ALIGN);

	calculated_sizes->y_stride = calculated_sizes->aligned_frame_width;
	calculated_sizes->y_size = calculated_sizes->y_stride * calculated_sizes->aligned_frame_height;

	switch (color_format)
	{
		case IMX_VPU_COLOR_FORMAT_YUV420:
			calculated_sizes->cbcr_stride = calculated_sizes->y_stride / 2;
			calculated_sizes->cbcr_size = calculated_sizes->mvcol_size = calculated_sizes->y_size / 4;
			break;
		case IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL:
			calculated_sizes->cbcr_stride = calculated_sizes->y_stride / 2;
			calculated_sizes->cbcr_size = calculated_sizes->mvcol_size = calculated_sizes->y_size / 2;
			break;
		case IMX_VPU_COLOR_FORMAT_YUV444:
			calculated_sizes->cbcr_stride = calculated_sizes->y_stride;
			calculated_sizes->cbcr_size = calculated_sizes->mvcol_size = calculated_sizes->y_size;
			break;
		case IMX_VPU_COLOR_FORMAT_YUV400:
			calculated_sizes->cbcr_stride = 0;
			calculated_sizes->cbcr_size = calculated_sizes->mvcol_size = 0;
			break;
		default:
			assert(FALSE);
	}

	if (chroma_interleave)
	{
		/* chroma_interleave != 0 means the Cb and Cr values are interleaved
		 * and share one plane. The stride values are doubled compared to
		 * the chroma_interleave == 0 case because the interleaving happens
		 * horizontally, meaning 2 bytes in the shared chroma plane for the
		 * chroma information of one pixel. */

		calculated_sizes->cbcr_stride *= 2;
		calculated_sizes->cbcr_size *= 2;
	}

	alignment = framebuffer_alignment;
	if (alignment > 1)
	{
		calculated_sizes->y_size = IMX_VPU_ALIGN_VAL_TO(calculated_sizes->y_size, alignment);
		calculated_sizes->cbcr_size = IMX_VPU_ALIGN_VAL_TO(calculated_sizes->cbcr_size, alignment);
		calculated_sizes->mvcol_size = IMX_VPU_ALIGN_VAL_TO(calculated_sizes->mvcol_size, alignment);
	}

	/* cbcr_size is added twice if chroma_interleave is 0, since in that case,
	 * there are *two* separate planes for Cb and Cr, each one with cbcr_size bytes,
	 * while in the chroma_interleave == 1 case, there is one shared chroma plane
	 * for both Cb and Cr data, with cbcr_size bytes */
	calculated_sizes->total_size = calculated_sizes->y_size
	                             + (chroma_interleave ? calculated_sizes->cbcr_size : (calculated_sizes->cbcr_size * 2))
	                             + calculated_sizes->mvcol_size
	                             + alignment;

	calculated_sizes->chroma_interleave = chroma_interleave;
}


void imx_vpu_fill_framebuffer_params(ImxVpuFramebuffer *framebuffer, ImxVpuFramebufferSizes *calculated_sizes, ImxVpuDMABuffer *fb_dma_buffer, void* context)
{
	assert(framebuffer != NULL);
	assert(calculated_sizes != NULL);

	framebuffer->dma_buffer = fb_dma_buffer;
	framebuffer->context = context;
	framebuffer->y_stride = calculated_sizes->y_stride;
	framebuffer->cbcr_stride = calculated_sizes->cbcr_stride;
	framebuffer->y_offset = 0;
	framebuffer->cb_offset = calculated_sizes->y_size;
	framebuffer->cr_offset = calculated_sizes->y_size + calculated_sizes->cbcr_size;
	framebuffer->mvcol_offset = calculated_sizes->y_size + calculated_sizes->cbcr_size * (calculated_sizes->chroma_interleave ? 1 : 2);
}




/************************************************/
/******* DECODER STRUCTURES AND FUNCTIONS *******/
/************************************************/


#define MIN_NUM_FREE_FB_REQUIRED 5


typedef struct
{
	void *context;
	uint64_t pts, dts;
}
ImxVpuDecFrameEntry;


struct _ImxVpuDecoder
{
	VpuDecHandle handle;

	void *virt_mem_sub_block;
	size_t virt_mem_sub_block_size;

	ImxVpuDMABuffer *bitstream_buffer;

	uint8_t const *codec_data;
	size_t codec_data_size;

	ImxVpuCodecFormat codec_format;

	unsigned int num_framebuffers;
	VpuFrameBuffer **wrapper_framebuffers;
	ImxVpuFramebuffer *framebuffers;
	ImxVpuDecFrameEntry *frame_entries;
	ImxVpuDecFrameEntry pending_entry;
	ImxVpuDecFrameEntry dropped_frame_entry;
	int num_context;

	BOOL output_info_available;
	BOOL consumption_info_available;
	BOOL flush_vpu_upon_reset;

	BOOL drain_mode_enabled;

	BOOL recalculate_num_avail_framebuffers;
	int num_available_framebuffers;
	int num_times_counter_decremented;
	int num_framebuffers_in_use;

	imx_vpu_dec_new_initial_info_callback initial_info_callback;
	void *callback_user_data;
};


static ImxVpuDecReturnCodes dec_convert_retcode(VpuDecRetCode code)
{
	switch (code)
	{
		case VPU_DEC_RET_SUCCESS:                    return IMX_VPU_DEC_RETURN_CODE_OK;
		case VPU_DEC_RET_FAILURE:                    return IMX_VPU_DEC_RETURN_CODE_ERROR;
		case VPU_DEC_RET_INVALID_PARAM:              return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;
		case VPU_DEC_RET_INVALID_HANDLE:             return IMX_VPU_DEC_RETURN_CODE_INVALID_HANDLE;
		case VPU_DEC_RET_INVALID_FRAME_BUFFER:       return IMX_VPU_DEC_RETURN_CODE_INVALID_FRAMEBUFFER;
		case VPU_DEC_RET_INSUFFICIENT_FRAME_BUFFERS: return IMX_VPU_DEC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS;
		case VPU_DEC_RET_INVALID_STRIDE:             return IMX_VPU_DEC_RETURN_CODE_INVALID_STRIDE;
		case VPU_DEC_RET_WRONG_CALL_SEQUENCE:        return IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE;
		case VPU_DEC_RET_FAILURE_TIMEOUT:            return IMX_VPU_DEC_RETURN_CODE_TIMEOUT;

		default: return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}
}


static unsigned int dec_convert_outcode(VpuDecBufRetCode code)
{
	/* TODO: REPEAT? SKIP? */
	unsigned int out = 0;
	if (code & VPU_DEC_INPUT_USED)         out |= IMX_VPU_DEC_OUTPUT_CODE_INPUT_USED;
	if (code & VPU_DEC_OUTPUT_EOS)         out |= IMX_VPU_DEC_OUTPUT_CODE_EOS;
	if (code & VPU_DEC_OUTPUT_DIS)         out |= IMX_VPU_DEC_OUTPUT_CODE_DECODED_FRAME_AVAILABLE;
	if (code & VPU_DEC_OUTPUT_DROPPED)     out |= IMX_VPU_DEC_OUTPUT_CODE_DROPPED;
	if (code & VPU_DEC_OUTPUT_MOSAIC_DIS)  out |= IMX_VPU_DEC_OUTPUT_CODE_DROPPED; /* mosaic frames are dropped */
	if (code & VPU_DEC_NO_ENOUGH_BUF)      out |= IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_OUTPUT_FRAMES;
	if (code & VPU_DEC_NO_ENOUGH_INBUF)    out |= IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA;
	if (code & VPU_DEC_RESOLUTION_CHANGED) out |= IMX_VPU_DEC_OUTPUT_CODE_RESOLUTION_CHANGED;
	return out;
}


static void dec_convert_to_wrapper_open_param(ImxVpuDecOpenParams *open_params, VpuDecOpenParam *wrapper_open_param)
{
	memset(wrapper_open_param, 0, sizeof(VpuDecOpenParam));

	wrapper_open_param->CodecFormat       = convert_to_wrapper_codec_std(open_params->codec_format);
	wrapper_open_param->nReorderEnable    = open_params->enable_frame_reordering;
	wrapper_open_param->nPicWidth         = open_params->frame_width;
	wrapper_open_param->nPicHeight        = open_params->frame_height;
	wrapper_open_param->nChromaInterleave = open_params->chroma_interleave;
}


static void dec_convert_from_wrapper_initial_info(VpuDecInitInfo *wrapper_info, ImxVpuDecInitialInfo *info)
{
	info->frame_width             = wrapper_info->nPicWidth;
	info->frame_height            = wrapper_info->nPicHeight;
	info->frame_rate_numerator    = wrapper_info->nFrameRateRes;
	info->frame_rate_denominator  = wrapper_info->nFrameRateDiv;

	info->min_num_required_framebuffers = wrapper_info->nMinFrameBufferCount + MIN_NUM_FREE_FB_REQUIRED;
	info->color_format                  = convert_from_wrapper_color_format(wrapper_info->nMjpgSourceFormat);

	info->interlacing = wrapper_info->nInterlace;

	info->framebuffer_alignment = wrapper_info->nAddressAlignment;
}


static int dec_get_wrapper_framebuffer_index(ImxVpuDecoder *decoder, VpuFrameBuffer *wrapper_fb)
{
	unsigned int i;

	// TODO: do something faster, like a hash table
	for (i = 0; i < decoder->num_framebuffers; ++i)
	{
		if (wrapper_fb == decoder->wrapper_framebuffers[i])
			return (int)i;
	}
	return -1;
}


char const * imx_vpu_dec_error_string(ImxVpuDecReturnCodes code)
{
	switch (code)
	{
		case IMX_VPU_DEC_RETURN_CODE_OK:                        return "ok";
		case IMX_VPU_DEC_RETURN_CODE_ERROR:                     return "unspecified error";
		case IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS:            return "invalid params";
		case IMX_VPU_DEC_RETURN_CODE_INVALID_HANDLE:            return "invalid handle";
		case IMX_VPU_DEC_RETURN_CODE_INVALID_FRAMEBUFFER:       return "invalid framebuffer";
		case IMX_VPU_DEC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS: return "insufficient_framebuffers";
		case IMX_VPU_DEC_RETURN_CODE_INVALID_STRIDE:            return "invalid stride";
		case IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE:       return "wrong call sequence";
		case IMX_VPU_DEC_RETURN_CODE_TIMEOUT:                   return "timeout";
		case IMX_VPU_DEC_RETURN_CODE_ALREADY_CALLED:            return "already called";
		default: return "<unknown>";
	}
}


static unsigned long vpu_dec_load_inst_counter = 0;
static DefaultDMABufferAllocator default_dec_dma_buffer_allocator =
{
	{
		default_dmabufalloc_allocate,
		default_dmabufalloc_deallocate,
		default_dmabufalloc_map,
		default_dmabufalloc_unmap,
		default_dmabufalloc_get_fd,
		default_dmabufalloc_get_physical_address,
		default_dmabufalloc_get_size
	},
	0
};


ImxVpuDecReturnCodes imx_vpu_dec_load(void)
{
	IMX_VPU_TRACE("VPU decoder load instance counter: %lu", vpu_dec_load_inst_counter);

	if (vpu_dec_load_inst_counter != 0)
	{
		++vpu_dec_load_inst_counter;
		return IMX_VPU_DEC_RETURN_CODE_OK;
	}
	else
	{
		ImxVpuDecReturnCodes ret = dec_convert_retcode(VPU_DecLoad());
		if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
			IMX_VPU_ERROR("loading decoder failed: %s", imx_vpu_dec_error_string(ret));
		else
		{
			IMX_VPU_TRACE("loaded decoder");
			++vpu_dec_load_inst_counter;
		}

		return ret;
	}
}


ImxVpuDecReturnCodes imx_vpu_dec_unload(void)
{
	IMX_VPU_TRACE("VPU decoder load instance counter: %lu", vpu_dec_load_inst_counter);

	if (vpu_dec_load_inst_counter != 0)
	{
		ImxVpuDecReturnCodes ret = IMX_VPU_DEC_RETURN_CODE_OK;
		--vpu_dec_load_inst_counter;

		if (vpu_dec_load_inst_counter == 0)
		{
			ImxVpuDecReturnCodes ret = dec_convert_retcode(VPU_DecUnLoad());
			if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
				IMX_VPU_ERROR("unloading decoder failed: %s", imx_vpu_dec_error_string(ret));
			else
			{
				IMX_VPU_TRACE("unloaded decoder");
			}
		}

		return ret;
	}
	else
		return IMX_VPU_DEC_RETURN_CODE_OK;
}


ImxVpuDMABufferAllocator* imx_vpu_dec_get_default_allocator(void)
{
	return (ImxVpuDMABufferAllocator*)(&default_dec_dma_buffer_allocator);
}


void imx_vpu_dec_get_bitstream_buffer_info(size_t *size, unsigned int *alignment)
{
	int i;
	VpuMemInfo mem_info;

	assert(size != NULL);
	assert(alignment != NULL);

	VPU_DecQueryMem(&mem_info);

	/* only two sub blocks are ever present - get the VPU_MEM_PHY one */

	for (i = 0; i < mem_info.nSubBlockNum; ++i)
	{
		if (mem_info.MemSubBlock[i].MemType == VPU_MEM_PHY)
		{
			*alignment = mem_info.MemSubBlock[i].nAlignment;
			*size = mem_info.MemSubBlock[i].nSize;
			IMX_VPU_TRACE("determined alignment %d and size %d for the physical memory for the bitstream buffer", *alignment, *size);
			break;
		}
	}

	/* virtual memory block is allocated internally inside imx_vpu_dec_open() */
}


ImxVpuDecReturnCodes imx_vpu_dec_open(ImxVpuDecoder **decoder, ImxVpuDecOpenParams *open_params, ImxVpuDMABuffer *bitstream_buffer, imx_vpu_dec_new_initial_info_callback new_initial_info_callback, void *callback_user_data)
{
	int config_param;
	VpuDecRetCode ret;
	VpuMemInfo mem_info;
	VpuDecOpenParam open_param;
	uint8_t *bitstream_buffer_virtual_address;
	imx_vpu_phys_addr_t bitstream_buffer_physical_address;

	assert(decoder != NULL);
	assert(open_params != NULL);
	assert(bitstream_buffer != NULL);

	*decoder = IMX_VPU_ALLOC(sizeof(ImxVpuDecoder));
	if ((*decoder) == NULL)
	{
		IMX_VPU_ERROR("allocating memory for decoder object failed");
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}

	memset(*decoder, 0, sizeof(ImxVpuDecoder));

	bitstream_buffer_virtual_address = imx_vpu_dma_buffer_map(bitstream_buffer, 0);
	bitstream_buffer_physical_address = imx_vpu_dma_buffer_get_physical_address(bitstream_buffer);

	(*decoder)->initial_info_callback = new_initial_info_callback;
	(*decoder)->callback_user_data = callback_user_data;

	{
		int i;

		VPU_DecQueryMem(&mem_info);

		IMX_VPU_INFO("about to allocate %d memory sub blocks", mem_info.nSubBlockNum);
		for (i = 0; i < mem_info.nSubBlockNum; ++i)
		{
			char const *type_str = "<unknown>";
			VpuMemSubBlockInfo *sub_block = &(mem_info.MemSubBlock[i]);

			switch (sub_block->MemType)
			{
				case VPU_MEM_VIRT:
					type_str = "virtual";

					(*decoder)->virt_mem_sub_block_size = sub_block->nSize + sub_block->nAlignment;
					(*decoder)->virt_mem_sub_block = IMX_VPU_ALLOC((*decoder)->virt_mem_sub_block_size);
					if ((*decoder)->virt_mem_sub_block == NULL)
					{
						imx_vpu_dma_buffer_unmap(bitstream_buffer);
						IMX_VPU_ERROR("allocating memory for sub block failed");
						return IMX_VPU_DEC_RETURN_CODE_ERROR;
					}

					sub_block->pVirtAddr = (unsigned char *)IMX_VPU_ALIGN_VAL_TO((*decoder)->virt_mem_sub_block, sub_block->nAlignment);
					sub_block->pPhyAddr = 0;
					break;

				case VPU_MEM_PHY:
					type_str = "physical";

					sub_block->pVirtAddr = (unsigned char *)(bitstream_buffer_virtual_address);
					sub_block->pPhyAddr = (unsigned char *)(bitstream_buffer_physical_address);
					break;
				default:
					break;
			}

			IMX_VPU_INFO(
				"allocated memory sub block #%d:  type: %s  size: %d  alignment: %d  virtual address: %p  physical address: %" IMX_VPU_PHYS_ADDR_FORMAT,
				i,
				type_str,
				sub_block->nSize,
				sub_block->nAlignment,
				sub_block->pVirtAddr,
				(imx_vpu_phys_addr_t)(sub_block->pPhyAddr)
			);
		}
	}

	dec_convert_to_wrapper_open_param(open_params, &open_param);

	IMX_VPU_TRACE("opening decoder");

	switch (open_params->codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_H264:
		case IMX_VPU_CODEC_FORMAT_MPEG2:
		case IMX_VPU_CODEC_FORMAT_MPEG4:
			(*decoder)->consumption_info_available = TRUE;
			(*decoder)->flush_vpu_upon_reset = TRUE;
			break;
		case IMX_VPU_CODEC_FORMAT_H263:
		case IMX_VPU_CODEC_FORMAT_WMV3:
		case IMX_VPU_CODEC_FORMAT_WVC1:
			(*decoder)->consumption_info_available = FALSE;
			(*decoder)->flush_vpu_upon_reset = FALSE;
			break;
		case IMX_VPU_CODEC_FORMAT_MJPEG:
		case IMX_VPU_CODEC_FORMAT_VP8:
			(*decoder)->consumption_info_available = FALSE;
			(*decoder)->flush_vpu_upon_reset = TRUE;
			break;
		default:
			break;
	}

	ret = VPU_DecOpen(&((*decoder)->handle), &open_param, &mem_info);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		IMX_VPU_ERROR("opening decoder failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
		goto cleanup;
	}

	IMX_VPU_TRACE("setting configuration");

	config_param = VPU_DEC_SKIPNONE;
	ret = VPU_DecConfig((*decoder)->handle, VPU_DEC_CONF_SKIPMODE, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		IMX_VPU_ERROR("setting skipmode to NONE failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
		goto cleanup;
	}

	config_param = 0;
	ret = VPU_DecConfig((*decoder)->handle, VPU_DEC_CONF_BUFDELAY, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		IMX_VPU_ERROR("setting bufdelay to 0 failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
		goto cleanup;
	}

	config_param = VPU_DEC_IN_NORMAL;
	ret = VPU_DecConfig((*decoder)->handle, VPU_DEC_CONF_INPUTTYPE, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		IMX_VPU_ERROR("setting input type to \"normal\" failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
		goto cleanup;
	}

	(*decoder)->codec_format = open_params->codec_format;
	(*decoder)->bitstream_buffer = bitstream_buffer;

finish:
	if (ret == VPU_DEC_RET_SUCCESS)
		IMX_VPU_TRACE("successfully opened decoder");

	return dec_convert_retcode(ret);

cleanup:
	imx_vpu_dma_buffer_unmap(bitstream_buffer);
	if ((*decoder)->virt_mem_sub_block != NULL)
		IMX_VPU_FREE((*decoder)->virt_mem_sub_block, (*decoder)->virt_mem_sub_block_size);
	IMX_VPU_FREE(*decoder, sizeof(ImxVpuDecoder));
	*decoder = NULL;

	goto finish;
}


ImxVpuDecReturnCodes imx_vpu_dec_close(ImxVpuDecoder *decoder)
{
	VpuDecRetCode ret;

	assert(decoder != NULL);

	IMX_VPU_TRACE("closing decoder");

	ret = VPU_DecFlushAll(decoder->handle);
	if (ret == VPU_DEC_RET_FAILURE_TIMEOUT)
	{
		IMX_VPU_WARNING("resetting decoder after a timeout occurred");
		ret = VPU_DecReset(decoder->handle);
		if (ret != VPU_DEC_RET_SUCCESS)
			IMX_VPU_ERROR("resetting decoder failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
	}
	else if (ret != VPU_DEC_RET_SUCCESS)
		IMX_VPU_ERROR("flushing decoder failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));

	ret = VPU_DecClose(decoder->handle);
	if (ret != VPU_DEC_RET_SUCCESS)
		IMX_VPU_ERROR("closing decoder failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));

	imx_vpu_dma_buffer_unmap(decoder->bitstream_buffer);

	if (decoder->framebuffers != NULL)
	{
		unsigned int i;
		for (i = 0; i < decoder->num_framebuffers; ++i)
			imx_vpu_dma_buffer_unmap(decoder->framebuffers[i].dma_buffer);
	}

	if (decoder->frame_entries != NULL)
		IMX_VPU_FREE(decoder->frame_entries, sizeof(ImxVpuDecFrameEntry) * decoder->num_framebuffers);
	if (decoder->wrapper_framebuffers != NULL)
		IMX_VPU_FREE(decoder->wrapper_framebuffers, sizeof(VpuFrameBuffer*) * decoder->num_framebuffers);
	if (decoder->virt_mem_sub_block != NULL)
		IMX_VPU_FREE(decoder->virt_mem_sub_block, decoder->virt_mem_sub_block_size);
	IMX_VPU_FREE(decoder, sizeof(ImxVpuDecoder));

	IMX_VPU_TRACE("closed decoder");

	return dec_convert_retcode(ret);
}


ImxVpuDMABuffer* imx_vpu_dec_get_bitstream_buffer(ImxVpuDecoder *decoder)
{
	return decoder->bitstream_buffer;
}


ImxVpuDecReturnCodes imx_vpu_dec_enable_drain_mode(ImxVpuDecoder *decoder, int enabled)
{
	int config_param;
	VpuDecRetCode ret;

	assert(decoder != NULL);

	if (decoder->drain_mode_enabled == enabled)
		return IMX_VPU_DEC_RETURN_CODE_OK;

	config_param = enabled ? VPU_DEC_IN_DRAIN : VPU_DEC_IN_NORMAL;
	ret = VPU_DecConfig(decoder->handle, VPU_DEC_CONF_INPUTTYPE, &config_param);

	decoder->drain_mode_enabled = enabled;

	if (ret != VPU_DEC_RET_SUCCESS)
		IMX_VPU_ERROR("setting decoder drain mode failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
	else
		IMX_VPU_INFO("set decoder drain mode to %d", enabled);

	return dec_convert_retcode(ret);
}


int imx_vpu_dec_is_drain_mode_enabled(ImxVpuDecoder *decoder)
{
	assert(decoder != NULL);

	return decoder->drain_mode_enabled;
}


ImxVpuDecReturnCodes imx_vpu_dec_flush(ImxVpuDecoder *decoder)
{
	VpuDecRetCode ret = VPU_DEC_RET_SUCCESS;

	assert(decoder != NULL);

	if (decoder->flush_vpu_upon_reset)
	{
		ret = VPU_DecFlushAll(decoder->handle);
		if (ret == VPU_DEC_RET_FAILURE_TIMEOUT)
		{
			IMX_VPU_WARNING("resetting decoder after a timeout occurred");
			ret = VPU_DecReset(decoder->handle);
			if (ret != VPU_DEC_RET_SUCCESS)
				IMX_VPU_ERROR("resetting decoder failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
		}
		else if (ret != VPU_DEC_RET_SUCCESS)
			IMX_VPU_ERROR("flushing decoder failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
		else
			IMX_VPU_INFO("flushed decoder");

		decoder->recalculate_num_avail_framebuffers = TRUE;
	}
	else
		IMX_VPU_INFO("decoder not flushed, because it is unnecessary for this codec format");

	if (decoder->frame_entries != NULL)
		IMX_VPU_FREE(decoder->frame_entries, sizeof(ImxVpuDecFrameEntry) * decoder->num_framebuffers);
	decoder->num_context = 0;

	return dec_convert_retcode(ret);
}


ImxVpuDecReturnCodes imx_vpu_dec_register_framebuffers(ImxVpuDecoder *decoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers)
{
	unsigned int i;
	VpuDecRetCode ret;
	VpuFrameBuffer *temp_fbs;

	assert(decoder != NULL);
	assert(framebuffers != NULL);
	assert(num_framebuffers > 0);

	IMX_VPU_TRACE("attempting to register %u framebuffers", num_framebuffers);

	decoder->wrapper_framebuffers = NULL;

	temp_fbs = IMX_VPU_ALLOC(sizeof(VpuFrameBuffer) * num_framebuffers);
	if (temp_fbs == NULL)
	{
		IMX_VPU_ERROR("allocating memory for framebuffers failed");
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}

	memset(temp_fbs, 0, sizeof(VpuFrameBuffer) * num_framebuffers);
	for (i = 0; i < num_framebuffers; ++i)
	{
		imx_vpu_phys_addr_t phys_addr;
		ImxVpuFramebuffer *fb = &framebuffers[i];

		phys_addr = imx_vpu_dma_buffer_get_physical_address(fb->dma_buffer);
		if (phys_addr == 0)
		{
			IMX_VPU_FREE(temp_fbs, sizeof(VpuFrameBuffer) * num_framebuffers);
			IMX_VPU_ERROR("could not map buffer %u/%u", i, num_framebuffers);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;
		}

		temp_fbs[i].nStrideY = fb->y_stride;
		temp_fbs[i].nStrideC = fb->cbcr_stride;

		temp_fbs[i].pbufY = (unsigned char*)(phys_addr + fb->y_offset);
		temp_fbs[i].pbufCb = (unsigned char*)(phys_addr + fb->cb_offset);
		temp_fbs[i].pbufCr = (unsigned char*)(phys_addr + fb->cr_offset);
		temp_fbs[i].pbufMvCol = (unsigned char*)(phys_addr + fb->mvcol_offset);
	}

	ret = VPU_DecRegisterFrameBuffer(decoder->handle, temp_fbs, num_framebuffers);

	IMX_VPU_FREE(temp_fbs, sizeof(VpuFrameBuffer) * num_framebuffers);


	if (ret != VPU_DEC_RET_SUCCESS)
	{
		ImxVpuDecReturnCodes imxret = dec_convert_retcode(ret);
		IMX_VPU_ERROR("registering framebuffers failed: %s", imx_vpu_dec_error_string(imxret));
		return imxret;
	}

	decoder->wrapper_framebuffers = IMX_VPU_ALLOC(sizeof(VpuFrameBuffer*) * num_framebuffers);
	if (decoder->wrapper_framebuffers == NULL)
	{
		IMX_VPU_ERROR("allocating memory for internal framebuffer structures failed");
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}

	{
		int out_num;
		VPU_DecAllRegFrameInfo(decoder->handle, decoder->wrapper_framebuffers, &out_num);
		IMX_VPU_LOG("out_num: %d  num_framebuffers: %u", out_num, num_framebuffers);
	}

	decoder->frame_entries = IMX_VPU_ALLOC(sizeof(ImxVpuDecFrameEntry) * num_framebuffers);
	if (decoder->frame_entries == NULL)
	{
		IMX_VPU_ERROR("allocating memory for frame context pointers failed");
		IMX_VPU_FREE(decoder->wrapper_framebuffers, sizeof(VpuFrameBuffer*) * num_framebuffers);
		decoder->wrapper_framebuffers = NULL;
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}

	decoder->framebuffers = framebuffers;
	decoder->num_framebuffers = num_framebuffers;
	decoder->num_available_framebuffers = num_framebuffers;

	memset(decoder->frame_entries, 0, sizeof(ImxVpuDecFrameEntry) * num_framebuffers);
	decoder->num_context = 0;


	return IMX_VPU_DEC_RETURN_CODE_OK;
}


void imx_vpu_dec_set_codec_data(ImxVpuDecoder *decoder, uint8_t const *codec_data, size_t codec_data_size)
{
	decoder->codec_data = codec_data;
	decoder->codec_data_size = codec_data_size;
}


ImxVpuDecReturnCodes imx_vpu_dec_decode(ImxVpuDecoder *decoder, ImxVpuEncodedFrame const *encoded_frame, unsigned int *output_code)
{
	VpuDecRetCode ret;
	VpuBufferNode node;
	int buf_ret_code;

	assert(decoder != NULL);
	assert(encoded_frame != NULL);
	assert(output_code != NULL);
	assert(decoder->drain_mode_enabled || (encoded_frame->data != NULL));

	node.pVirAddr = encoded_frame->data;
	node.pPhyAddr = 0; /* encoded data is always read from a regular memory block, not a DMA buffer */
	node.nSize = encoded_frame->data_size;

	node.sCodecData.pData = (void *)(decoder->codec_data);
	node.sCodecData.nSize = decoder->codec_data_size;

	decoder->pending_entry.context = encoded_frame->context;
	decoder->pending_entry.pts = encoded_frame->pts;
	decoder->pending_entry.dts = encoded_frame->dts;

	ret = VPU_DecDecodeBuf(decoder->handle, &node, &buf_ret_code);
	IMX_VPU_LOG("VPU_DecDecodeBuf buf ret code: 0x%x", buf_ret_code);

	*output_code = dec_convert_outcode(buf_ret_code);

	if (ret != VPU_DEC_RET_SUCCESS)
	{
		IMX_VPU_ERROR("decoding frame failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
		return dec_convert_retcode(ret);
	}

	if (decoder->recalculate_num_avail_framebuffers)
	{
		decoder->num_available_framebuffers = decoder->num_framebuffers - decoder->num_framebuffers_in_use;
		IMX_VPU_LOG("recalculated number of available framebuffers to %d", decoder->num_available_framebuffers);
		decoder->recalculate_num_avail_framebuffers = FALSE;
	}

	if (buf_ret_code & VPU_DEC_INIT_OK)
	{
		/* Init info is available. Get this info, and then proceed with decoding the frame.
		 *
		 * This is ordinarily not possible, since the first frame is consumed by VPU_DecDecodeBuf(),
		 * and after this call, initial info is available. Ther caller is then supposed to
		 * allocate and register framebuffers using that initial info, and then feed to second
		 * encoded frame to VPU_DecDecodeBuf(), which will (depending on the codec) decode the first.
		 * This means that with this ordinary approach, there is always at least a one-frame delay
		 * (caused by the initial info phase).
		 * Such a one-frame delay can make things more difficult for users, especially when performing
		 * tasks like JPEG decoding. So, to circumvent this delay, a trick is used.
		 * If VPU_DecDecodeBuf() reports that initial info is available, the code inside this scope
		 * here first retrieves this info, and then calls the initial_info_callback . Inside this
		 * callback, users can allocate and register framebuffers. Afterwards, the VPU wrapper's
		 * drain mode is enabled. This is necessary to force the wrapper to decode the frame that was
		 * fed into VPU_DecDecodeBuf() earlier. Then, VPU_DecDecodeBuf() is called again, but with
		 * an input VpuBufferNode with size 0 and null pointers (since it is in the drain mode).
		 * After this VPU_DecDecodeBuf() call, the drain mode is immediately disabled. Then,
		 * the code proceeds as usual.
		 * Result: the delay does not affect operations.
		 */

		VpuDecInitInfo wrapper_init_info;
		ImxVpuDecInitialInfo initial_info;

		/* Dummy drain node with null pointers and size zero */
		VpuBufferNode drain_node;
		drain_node.pVirAddr = 0;
		drain_node.pPhyAddr = 0;
		drain_node.nSize = 0;

		/* Extract the initial info */
		if ((ret = VPU_DecGetInitialInfo(decoder->handle, &wrapper_init_info)) != VPU_DEC_RET_SUCCESS)
		{
			IMX_VPU_ERROR("could not get initial info: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
			return dec_convert_retcode(ret);
		}

		IMX_VPU_LOG("VPU_DecGetInitialInfo: min num framebuffers required: %d", wrapper_init_info.nMinFrameBufferCount);
		/* Convert the initial info to the imxvpuapi structure equivalent for the callback */
		dec_convert_from_wrapper_initial_info(&wrapper_init_info, &initial_info);

		/* Invoke the initial_info_callback. Framebuffers for decoding are allocated
		 * and registered there. */
		if (!decoder->initial_info_callback(decoder, &initial_info, *output_code, decoder->callback_user_data))
		{
			IMX_VPU_ERROR("initial info callback reported failure - cannot continue");
			return IMX_VPU_DEC_RETURN_CODE_ERROR;
		}

		/* Enable drain mode to force the VPU wrapper to decode the frame that
		 * was fed into it previously */
		imx_vpu_dec_enable_drain_mode(decoder, TRUE);

		/* Decode the frame */
		ret = VPU_DecDecodeBuf(decoder->handle, &drain_node, &buf_ret_code);
		*output_code = dec_convert_outcode(buf_ret_code);
		IMX_VPU_LOG("VPU_DecDecodeBuf buf ret code: 0x%x", buf_ret_code);
		if (ret != VPU_DEC_RET_SUCCESS)
		{
			IMX_VPU_ERROR("decoding frame failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
			return dec_convert_retcode(ret);
		}

		/* Frame decoded, disable drain mode */
		imx_vpu_dec_enable_drain_mode(decoder, FALSE);

		if (ret != VPU_DEC_RET_SUCCESS)
		{
			IMX_VPU_ERROR("decoding frame failed: %s", imx_vpu_dec_error_string(dec_convert_retcode(ret)));
			return dec_convert_retcode(ret);
		}

		/* From here on, the rest of the code can assume it was just
		 * a regular VPU_DecDecodeBuf() call */
	}

	if (buf_ret_code & VPU_DEC_FLUSH)
	{
		IMX_VPU_INFO("VPU requested a decoder flush");
		ret = VPU_DecFlushAll(decoder->handle);
		if (ret == VPU_DEC_RET_FAILURE_TIMEOUT)
		{
			IMX_VPU_WARNING("timeout detected, resetting decoder");

			ret = VPU_DecReset(decoder->handle);
			if (ret != VPU_DEC_RET_SUCCESS)
			{
				ImxVpuDecReturnCodes imxret = dec_convert_retcode(ret);
				IMX_VPU_ERROR("resetting decoder failed: %s", imx_vpu_dec_error_string(imxret));
				return imxret;
			}
		}
		else if (ret != VPU_DEC_RET_SUCCESS)
		{
			ImxVpuDecReturnCodes imxret = dec_convert_retcode(ret);
			IMX_VPU_ERROR("flushing decoder failed: %s", imx_vpu_dec_error_string(imxret));
			return imxret;
		}
		else
			IMX_VPU_INFO("flushed decoder");
	}

	if (buf_ret_code & VPU_DEC_RESOLUTION_CHANGED)
	{
		/* Resolution changed; reset internal states, since in the next
		 * VPU_DecDecodeBuf() call, new initial info will become available */

		IMX_VPU_INFO("resolution changed - resetting internal states");

		decoder->recalculate_num_avail_framebuffers = FALSE;

		decoder->num_context = 0;

		if (decoder->context_for_frames != NULL)
			IMX_VPU_FREE(decoder->context_for_frames, sizeof(void*) * decoder->num_framebuffers);
		if (decoder->wrapper_framebuffers != NULL)
			IMX_VPU_FREE(decoder->wrapper_framebuffers, sizeof(VpuFrameBuffer*) * decoder->num_framebuffers);

		decoder->context_for_frames = NULL;
		decoder->wrapper_framebuffers = NULL;
	}

	if (buf_ret_code & VPU_DEC_NO_ENOUGH_INBUF)
	{
		/* Not dropping frame here on purpose; the next input frame may
		 * complete the input */
	}

	{
		/* In here, the frame context is stored in the context_for_frames array.
		 * There are two ways of doing this. Which one to pick depends on whether or not
		 * the VPU wrapper produces information about the consumed frame (that is, the
		 * frame that was just picked by the VPU to decode a frame into). If it does,
		 * method 1 is used: VPU_DecGetConsumedFrameInfo() is called, and based on
		 * its pFrame pointer, the appropriate array index is computed. The context is
		 * stored there.
		 * If the VPU wrapper does not produce such information, store the context
		 * as the "newest" one. context_for_frames then behaves like a FIFO. This is
		 * appropriate, since codec formats that don't produce consumed frame information
		 * do not use frame reordering. They can delay frame decoding, but the order of
		 * frames stays intact. */

		ImxVpuDecFrameEntry entry = decoder->pending_entry;

		if ((buf_ret_code & VPU_DEC_ONE_FRM_CONSUMED) && !(buf_ret_code & VPU_DEC_OUTPUT_DROPPED))
		{
			int fb_index;

			VpuDecFrameLengthInfo consumed_frame_info;
			ret = VPU_DecGetConsumedFrameInfo(decoder->handle, &consumed_frame_info);
			if (ret != VPU_DEC_RET_SUCCESS)
			{
				ImxVpuDecReturnCodes imxret = dec_convert_retcode(ret);
				IMX_VPU_ERROR("getting consumed frame info failed: %s", imx_vpu_dec_error_string(imxret));
				return imxret;
			}

			fb_index = dec_get_wrapper_framebuffer_index(decoder, consumed_frame_info.pFrame);

			if (consumed_frame_info.pFrame != NULL)
			{
				if ((fb_index >= 0) && (fb_index < (int)(decoder->num_framebuffers)))
				{
					IMX_VPU_LOG("framebuffer index %d for framebuffer %p user data %p pts %" PRIu64 " dts %" PRIu64, fb_index, (void *)(consumed_frame_info.pFrame), entry.context, entry.pts, entry.dts);
					decoder->frame_entries[fb_index] = entry;
				}
				else
					IMX_VPU_ERROR("framebuffer index %d for framebuffer %p user data %p pts %" PRIu64 " dts %" PRIu64 " out of bounds", fb_index, (void *)(consumed_frame_info.pFrame), entry.context, entry.pts, entry.dts);
			}
			else
				IMX_VPU_WARNING("consumed frame info contains a NULL frame");
		}
		else if (!(decoder->consumption_info_available) && (decoder->framebuffers != NULL))
		{
			if (decoder->num_context < (int)(decoder->num_framebuffers))
			{
				decoder->frame_entries[decoder->num_context] = entry;
				decoder->num_context++;

				IMX_VPU_LOG("user data %p pts %" PRIu64 " dts %" PRIu64 " stored as newest", entry.context, entry.pts, entry.dts);

				IMX_VPU_TRACE("incremented number of userdata pointers to %d", decoder->num_context);
			}
			else
				IMX_VPU_WARNING("too many user data pointers in memory - cannot store current one");
		}
	}

	if ((buf_ret_code & VPU_DEC_ONE_FRM_CONSUMED) && !(buf_ret_code & VPU_DEC_OUTPUT_DROPPED))
	{
		decoder->num_available_framebuffers--;
		decoder->num_times_counter_decremented++;
		IMX_VPU_LOG("decremented number of available framebuffers to %d (with consumed frame info); number of times decremented is now %d", decoder->num_available_framebuffers, decoder->num_times_counter_decremented);
	}

	/* VPU_DEC_NO_ENOUGH_BUF handled by caller - should be treated as an error */

	if ((buf_ret_code & VPU_DEC_OUTPUT_DIS) && !(decoder->consumption_info_available))
	{
		decoder->num_available_framebuffers--;
		decoder->num_times_counter_decremented++;
		IMX_VPU_LOG("decremented number of available framebuffers to %d (no consumed frame info); number of times decremented is now %d", decoder->num_available_framebuffers, decoder->num_times_counter_decremented);
	}
	else if (buf_ret_code & VPU_DEC_OUTPUT_MOSAIC_DIS)
	{
		IMX_VPU_TRACE("dropping mosaic frame");

		/* mosaic frames do not seem to be useful for anything, so they are just dropped here */

		ImxVpuDecReturnCodes imxret;
		ImxVpuRawFrame decoded_frame;

		if ((imxret = imx_vpu_dec_get_decoded_frame(decoder, &decoded_frame)) != IMX_VPU_DEC_RETURN_CODE_OK)
		{
			IMX_VPU_ERROR("error getting output mosaic frame: %s", imx_vpu_dec_error_string(imxret));
			return imxret;
		}

		if ((imxret = imx_vpu_dec_mark_framebuffer_as_displayed(decoder, decoded_frame.framebuffer)) != IMX_VPU_DEC_RETURN_CODE_OK)
		{
			IMX_VPU_ERROR("error marking mosaic frame as displayed: %s", imx_vpu_dec_error_string(imxret));
			return imxret;
		}

		decoder->dropped_frame_entry.context = decoded_frame.context;
		decoder->dropped_frame_entry.pts = decoded_frame.pts;
		decoder->dropped_frame_entry.dts = decoded_frame.dts;

		*output_code |= IMX_VPU_DEC_OUTPUT_CODE_DROPPED;
	}
	else if (buf_ret_code & VPU_DEC_OUTPUT_DROPPED)
	{
		// TODO improve this for formats with consumption info
		if (decoder->num_context > 0)
		{
			decoder->dropped_frame_entry = decoder->frame_entries[0];
			memmove(decoder->frame_entries, decoder->frame_entries + 1, sizeof(ImxVpuDecFrameEntry) * (decoder->num_context - 1));
			decoder->num_context--;
		}
		else
			memset(&(decoder->dropped_frame_entry), 0, sizeof(ImxVpuDecFrameEntry));
	}

	/* In case the VPU didn't use the input and no consumed frame info is available,
	 * drop the input frame to make sure timestamps are okay
	 * (If consumed frame info is present it is still possible it might be used for input-output frame
	 * associations; unlikely to occur thought) */
	if ((encoded_frame->data != NULL) && !(buf_ret_code & (VPU_DEC_ONE_FRM_CONSUMED | VPU_DEC_INPUT_USED)))
	{
		decoder->dropped_frame_entry.context = encoded_frame->context;
		decoder->dropped_frame_entry.pts = encoded_frame->pts;
		decoder->dropped_frame_entry.dts = encoded_frame->dts;
		*output_code |= IMX_VPU_DEC_OUTPUT_CODE_DROPPED;
	}

	if (*output_code & IMX_VPU_DEC_OUTPUT_CODE_DECODED_FRAME_AVAILABLE)
		decoder->output_info_available = TRUE;

	return IMX_VPU_DEC_RETURN_CODE_OK;
}


ImxVpuDecReturnCodes imx_vpu_dec_get_decoded_frame(ImxVpuDecoder *decoder, ImxVpuRawFrame *decoded_frame)
{
	VpuDecRetCode ret;
	VpuDecOutFrameInfo out_frame_info;
	int fb_index;
	ImxVpuDecFrameEntry entry;

	assert(decoder != NULL);
	assert(decoded_frame != NULL);

	if (!(decoder->output_info_available))
	{
		IMX_VPU_ERROR("no decoded frame available, or function was already called earlier");
		return IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE;
	}

	decoder->output_info_available = FALSE;

	ret = VPU_DecGetOutputFrame(decoder->handle, &out_frame_info);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		ImxVpuDecReturnCodes imxret = dec_convert_retcode(ret);
		IMX_VPU_ERROR("error getting decoded output frame: %s", imx_vpu_dec_error_string(imxret));
		return imxret;
	}

	fb_index = dec_get_wrapper_framebuffer_index(decoder, out_frame_info.pDisplayFrameBuf);

	memset(&entry, 0, sizeof(ImxVpuDecFrameEntry));
	if (decoder->consumption_info_available)
	{
		if ((fb_index >= 0) && (fb_index < (int)(decoder->num_framebuffers)))
		{
			entry = decoder->frame_entries[fb_index];
			IMX_VPU_LOG("framebuffer index %d for framebuffer %p and user data %p pts %" PRIu64 " dts %" PRIu64, fb_index, (void *)(out_frame_info.pDisplayFrameBuf), entry.context, entry.pts, entry.dts);
			memset(&(decoder->frame_entries[fb_index]), 0, sizeof(ImxVpuDecFrameEntry));
		}
		else
			IMX_VPU_ERROR("framebuffer index %d for framebuffer %p out of bounds", fb_index, (void *)(out_frame_info.pDisplayFrameBuf));
	}
	else
	{
		if (decoder->num_context > 0)
		{
			entry = decoder->frame_entries[0];
			IMX_VPU_LOG("framebuffer index %d user data %p pts %" PRIu64 " dts %" PRIu64 " retrieved as oldest", fb_index, entry.context, entry.pts, entry.dts);
			memmove(decoder->frame_entries, decoder->frame_entries + 1, sizeof(ImxVpuDecFrameEntry) * (decoder->num_context - 1));
			decoder->num_context--;
		}
	}

	decoded_frame->frame_types[0] = decoded_frame->frame_types[1] = convert_from_wrapper_pic_type(out_frame_info.ePicType);
	decoded_frame->interlacing_mode = convert_from_wrapper_field_type(out_frame_info.eFieldType);
	decoded_frame->context = entry.context;
	decoded_frame->pts = entry.pts;
	decoded_frame->dts = entry.dts;

	/* XXX
	 * This association assumes that the order of internal framebuffer entries
	 * inside the VPU wrapper is the same as the order of the framebuffers here.
	 * So, decoder->framebuffers[1] equals internal framebuffer entry with index 1 etc.
	 */
	decoded_frame->framebuffer = &(decoder->framebuffers[fb_index]);
	/* This is used in imx_vpu_dec_mark_framebuffer_as_displayed() to be able
	 * to mark the vpuwrapper framebuffer as displayed */
	decoded_frame->framebuffer->internal = out_frame_info.pDisplayFrameBuf;
	decoded_frame->framebuffer->already_marked = FALSE;

	decoder->num_framebuffers_in_use++;

	return IMX_VPU_DEC_RETURN_CODE_OK;
}


void imx_vpu_dec_get_dropped_frame_info(ImxVpuDecoder *decoder, void **context, uint64_t *pts, uint64_t *dts)
{
	assert(decoder != NULL);
	if (context != NULL)
		*context = decoder->dropped_frame_entry.context;
	if (pts != NULL)
		*pts = decoder->dropped_frame_entry.pts;
	if (dts != NULL)
		*dts = decoder->dropped_frame_entry.dts;
}


int imx_vpu_dec_check_if_can_decode(ImxVpuDecoder *decoder)
{
	assert(decoder != NULL);
	return decoder->num_available_framebuffers >= MIN_NUM_FREE_FB_REQUIRED;
}


ImxVpuDecReturnCodes imx_vpu_dec_mark_framebuffer_as_displayed(ImxVpuDecoder *decoder, ImxVpuFramebuffer *framebuffer)
{
	VpuDecRetCode ret;
	VpuFrameBuffer *wrapper_fb;

	assert(decoder != NULL);
	assert(framebuffer != NULL);

	if (framebuffer->already_marked)
		return IMX_VPU_DEC_RETURN_CODE_OK;

	wrapper_fb = (VpuFrameBuffer *)(framebuffer->internal);

	ret = VPU_DecOutFrameDisplayed(decoder->handle, wrapper_fb);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		ImxVpuDecReturnCodes imxret = dec_convert_retcode(ret);
		IMX_VPU_ERROR("error marking output frame as displayed: %s", imx_vpu_dec_error_string(imxret));
		return imxret;
	}

	IMX_VPU_LOG("marked framebuffer %p with DMA buffer %p as displayed", (void *)framebuffer, framebuffer->dma_buffer);

	if (decoder->num_times_counter_decremented > 0)
	{
		decoder->num_available_framebuffers++;
		decoder->num_times_counter_decremented--;
		decoder->num_framebuffers_in_use--;

		IMX_VPU_LOG("num_available_framebuffers %d  num_times_counter_decremented %d  num_framebuffers_in_use %d", decoder->num_available_framebuffers, decoder->num_times_counter_decremented, decoder->num_framebuffers_in_use);
	}

	framebuffer->already_marked = TRUE;

	return IMX_VPU_DEC_RETURN_CODE_OK;
}




/************************************************/
/******* ENCODER STRUCTURES AND FUNCTIONS *******/
/************************************************/


struct _ImxVpuEncoder
{
	VpuEncHandle handle;

	void *virt_mem_sub_block;
	size_t virt_mem_sub_block_size;

	uint8_t *temp_enc_data_buffer;

	ImxVpuDMABuffer *bitstream_buffer;
	size_t bitstream_buffer_size;

	ImxVpuCodecFormat codec_format;
	unsigned int frame_width, frame_height;
	unsigned int frame_rate_numerator, frame_rate_denominator;

	unsigned int num_framebuffers;
	ImxVpuFramebuffer *framebuffers;
};


static ImxVpuEncReturnCodes enc_convert_retcode(VpuEncRetCode code)
{
	switch (code)
	{
		case VPU_ENC_RET_SUCCESS:                    return IMX_VPU_ENC_RETURN_CODE_OK;
		case VPU_ENC_RET_FAILURE:                    return IMX_VPU_ENC_RETURN_CODE_ERROR;
		case VPU_ENC_RET_INVALID_PARAM:              return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;
		case VPU_ENC_RET_INVALID_HANDLE:             return IMX_VPU_ENC_RETURN_CODE_INVALID_HANDLE;
		case VPU_ENC_RET_INVALID_FRAME_BUFFER:       return IMX_VPU_ENC_RETURN_CODE_INVALID_FRAMEBUFFER;
		case VPU_ENC_RET_INSUFFICIENT_FRAME_BUFFERS: return IMX_VPU_ENC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS;
		case VPU_ENC_RET_INVALID_STRIDE:             return IMX_VPU_ENC_RETURN_CODE_INVALID_STRIDE;
		case VPU_ENC_RET_WRONG_CALL_SEQUENCE:        return IMX_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE;
		case VPU_ENC_RET_FAILURE_TIMEOUT:            return IMX_VPU_ENC_RETURN_CODE_TIMEOUT;

		default: return IMX_VPU_ENC_RETURN_CODE_ERROR;
	}
}


static int enc_convert_to_wrapper_open_param(ImxVpuEncOpenParams *open_params, VpuEncOpenParam *wrapper_open_param)
{
	memset(wrapper_open_param, 0, sizeof(VpuEncOpenParam));

	wrapper_open_param->eFormat = convert_to_wrapper_codec_std(open_params->codec_format);
	wrapper_open_param->nPicWidth = open_params->frame_width;
	wrapper_open_param->nPicHeight = open_params->frame_height;
	wrapper_open_param->nRotAngle = 0;
	wrapper_open_param->nFrameRate = (open_params->frame_rate_numerator & 0xffffUL) | (((open_params->frame_rate_denominator - 1) & 0xffffUL) << 16);
	wrapper_open_param->nBitRate = open_params->bitrate;
	wrapper_open_param->nGOPSize = open_params->gop_size;
	wrapper_open_param->nChromaInterleave = open_params->chroma_interleave;
	wrapper_open_param->sMirror = VPU_ENC_MIRDIR_NONE;
	wrapper_open_param->nMapType = 0;
	wrapper_open_param->nLinear2TiledEnable = 1;
	wrapper_open_param->eColorFormat = convert_to_wrapper_color_format(open_params->color_format);

	/* The specification states that both values must be set if user defined
	 * values are used, so disable both if both values are -1, and enable
	 * both otherwise */
	if ((open_params->user_defined_min_qp == -1) && (open_params->user_defined_max_qp == -1))
	{
		wrapper_open_param->nUserQpMinEnable = 0;
		wrapper_open_param->nUserQpMaxEnable = 0;
		wrapper_open_param->nUserQpMin = 0;
		wrapper_open_param->nUserQpMax = 0;
	}
	else
	{
		wrapper_open_param->nUserQpMinEnable = 1;
		wrapper_open_param->nUserQpMaxEnable = 1;
		wrapper_open_param->nUserQpMin = open_params->user_defined_min_qp;
		wrapper_open_param->nUserQpMax = open_params->user_defined_max_qp;
	}

	wrapper_open_param->nIntraRefresh = open_params->min_intra_refresh_mb_count;
	wrapper_open_param->nRcIntraQp = open_params->intra_qp;
	
	wrapper_open_param->nUserGamma = open_params->qp_estimation_smoothness;

	wrapper_open_param->nRcIntervalMode = open_params->rate_control_mode;
	wrapper_open_param->nMbInterval = open_params->macroblock_interval;

	wrapper_open_param->sliceMode.sliceMode = open_params->slice_mode.multiple_slices_per_frame;
	wrapper_open_param->sliceMode.sliceSizeMode = open_params->slice_mode.slice_size_unit;
	wrapper_open_param->sliceMode.sliceSize = open_params->slice_mode.slice_size;

	wrapper_open_param->nInitialDelay = open_params->initial_delay;
	wrapper_open_param->nVbvBufferSize = open_params->vbv_buffer_size;
	
	wrapper_open_param->nMESearchRange = open_params->me_search_range;
	wrapper_open_param->nMEUseZeroPmv = open_params->use_me_zero_pmv;
	wrapper_open_param->nIntraCostWeight = open_params->additional_intra_cost_weight;

	switch (open_params->codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_MPEG4:
			wrapper_open_param->VpuEncStdParam.mp4Param.mp4_dataPartitionEnable = open_params->codec_params.mpeg4_params.enable_data_partitioning;
			wrapper_open_param->VpuEncStdParam.mp4Param.mp4_reversibleVlcEnable = open_params->codec_params.mpeg4_params.enable_reversible_vlc;
			wrapper_open_param->VpuEncStdParam.mp4Param.mp4_intraDcVlcThr = open_params->codec_params.mpeg4_params.intra_dc_vlc_thr;
			wrapper_open_param->VpuEncStdParam.mp4Param.mp4_hecEnable = open_params->codec_params.mpeg4_params.enable_hec;
			wrapper_open_param->VpuEncStdParam.mp4Param.mp4_verid = open_params->codec_params.mpeg4_params.version_id;
			break;

		case IMX_VPU_CODEC_FORMAT_H263:
			wrapper_open_param->VpuEncStdParam.h263Param.h263_annexIEnable = open_params->codec_params.h263_params.enable_annex_i;
			wrapper_open_param->VpuEncStdParam.h263Param.h263_annexJEnable = open_params->codec_params.h263_params.enable_annex_j;
			wrapper_open_param->VpuEncStdParam.h263Param.h263_annexKEnable = open_params->codec_params.h263_params.enable_annex_k;
			wrapper_open_param->VpuEncStdParam.h263Param.h263_annexTEnable = open_params->codec_params.h263_params.enable_annex_t;
			break;

		case IMX_VPU_CODEC_FORMAT_H264:
			/* The VPU encoder actually doesn't support the AVCC output; the VPU wrapper does
			 * an internal conversion from byte-stream to AVCC unless this is set to 0 */
			wrapper_open_param->nIsAvcc = 0;

			wrapper_open_param->VpuEncStdParam.avcParam.avc_constrainedIntraPredFlag = open_params->codec_params.h264_params.enable_constrained_intra_prediction;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_disableDeblk = open_params->codec_params.h264_params.disable_deblocking;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_deblkFilterOffsetAlpha = open_params->codec_params.h264_params.deblock_filter_offset_alpha;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_deblkFilterOffsetBeta = open_params->codec_params.h264_params.deblock_filter_offset_beta;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_chromaQpOffset = open_params->codec_params.h264_params.chroma_qp_offset;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_audEnable = open_params->codec_params.h264_params.enable_access_unit_delimiters;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_fmoEnable = 0;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_fmoSliceNum = 1;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_fmoType = 0;
			wrapper_open_param->VpuEncStdParam.avcParam.avc_fmoSliceSaveBufSize = 32;
			break;

		case IMX_VPU_CODEC_FORMAT_MJPEG:
			break;

		default:
			IMX_VPU_ERROR("invalid codec format");
			return FALSE;
	}

	return TRUE;
}

static void enc_convert_from_wrapper_initial_info(VpuEncInitInfo *wrapper_info, ImxVpuEncInitialInfo *info)
{
	info->min_num_required_framebuffers = wrapper_info->nMinFrameBufferCount;
	info->framebuffer_alignment = wrapper_info->nAddressAlignment;
}


char const * imx_vpu_enc_error_string(ImxVpuEncReturnCodes code)
{
	switch (code)
	{
		case IMX_VPU_ENC_RETURN_CODE_OK:                        return "ok";
		case IMX_VPU_ENC_RETURN_CODE_ERROR:                     return "unspecified error";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS:            return "invalid params";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_HANDLE:            return "invalid handle";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_FRAMEBUFFER:       return "invalid framebuffer";
		case IMX_VPU_ENC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS: return "insufficient_framebuffers";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_STRIDE:            return "invalid stride";
		case IMX_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE:       return "wrong call sequence";
		case IMX_VPU_ENC_RETURN_CODE_TIMEOUT:                   return "timeout";
		default: return "<unknown>";
	}
}


static unsigned long vpu_enc_load_inst_counter = 0;
static DefaultDMABufferAllocator default_enc_dma_buffer_allocator =
{
	{
		default_dmabufalloc_allocate,
		default_dmabufalloc_deallocate,
		default_dmabufalloc_map,
		default_dmabufalloc_unmap,
		default_dmabufalloc_get_fd,
		default_dmabufalloc_get_physical_address,
		default_dmabufalloc_get_size
	},
	1
};


ImxVpuEncReturnCodes imx_vpu_enc_load(void)
{
	IMX_VPU_TRACE("VPU encoder load instance counter: %lu", vpu_enc_load_inst_counter);

	if (vpu_enc_load_inst_counter != 0)
	{
		++vpu_enc_load_inst_counter;
		return IMX_VPU_ENC_RETURN_CODE_OK;
	}
	else
	{
		ImxVpuEncReturnCodes ret = enc_convert_retcode(VPU_EncLoad());
		if (ret != IMX_VPU_ENC_RETURN_CODE_OK)
			IMX_VPU_ERROR("loading encoder failed: %s", imx_vpu_enc_error_string(ret));
		else
		{
			IMX_VPU_TRACE("loaded encoder");
			++vpu_enc_load_inst_counter;
		}

		return ret;
	}
}


ImxVpuEncReturnCodes imx_vpu_enc_unload(void)
{
	IMX_VPU_TRACE("VPU encoder load instance counter: %lu", vpu_enc_load_inst_counter);

	if (vpu_enc_load_inst_counter != 0)
	{
		ImxVpuEncReturnCodes ret = IMX_VPU_ENC_RETURN_CODE_OK;
		--vpu_enc_load_inst_counter;

		if (vpu_enc_load_inst_counter == 0)
		{
			ImxVpuEncReturnCodes ret = enc_convert_retcode(VPU_EncUnLoad());
			if (ret != IMX_VPU_ENC_RETURN_CODE_OK)
				IMX_VPU_ERROR("unloading encoder failed: %s", imx_vpu_enc_error_string(ret));
			else
			{
				IMX_VPU_TRACE("unloaded encoder");
			}
		}

		return ret;
	}
	else
		return IMX_VPU_ENC_RETURN_CODE_OK;
}


ImxVpuDMABufferAllocator* imx_vpu_enc_get_default_allocator(void)
{
	return (ImxVpuDMABufferAllocator*)(&default_enc_dma_buffer_allocator);
}


void imx_vpu_enc_get_bitstream_buffer_info(size_t *size, unsigned int *alignment)
{
	int i;
	VpuMemInfo mem_info;

	assert(size != NULL);
	assert(alignment != NULL);

	VPU_EncQueryMem(&mem_info);

	/* only two sub blocks are ever present - get the VPU_MEM_PHY one */

	for (i = 0; i < mem_info.nSubBlockNum; ++i)
	{
		if (mem_info.MemSubBlock[i].MemType == VPU_MEM_PHY)
		{
			*alignment = mem_info.MemSubBlock[i].nAlignment;
			*size = mem_info.MemSubBlock[i].nSize;
			IMX_VPU_TRACE("determined alignment %d and size %d for the physical memory for the bitstream buffer", *alignment, *size);
			break;
		}
	}

	/* virtual memory block is allocated internally inside imx_vpu_enc_open() */
}


void imx_vpu_enc_set_default_open_params(ImxVpuCodecFormat codec_format, ImxVpuEncOpenParams *open_params)
{
	assert(open_params != NULL);

	open_params->codec_format = codec_format;
	open_params->frame_width = 0;
	open_params->frame_height = 0;
	open_params->frame_rate_numerator = 1;
	open_params->frame_rate_denominator = 1;
	open_params->bitrate = 100;
	open_params->gop_size = 16;
	open_params->color_format = IMX_VPU_COLOR_FORMAT_YUV420;
	open_params->user_defined_min_qp = -1;
	open_params->user_defined_max_qp = -1;
	open_params->min_intra_refresh_mb_count = 0;
	open_params->intra_qp = -1;
	open_params->qp_estimation_smoothness = (int)(0.75*32768);
	open_params->rate_control_mode = IMX_VPU_ENC_RATE_CONTROL_MODE_NORMAL;
	open_params->macroblock_interval = 0;
	open_params->slice_mode.multiple_slices_per_frame = 0;
	open_params->slice_mode.slice_size_unit = IMX_VPU_ENC_SLICE_SIZE_UNIT_BITS;
	open_params->slice_mode.slice_size = 4000;
	open_params->initial_delay = 0;
	open_params->vbv_buffer_size = 0;
	open_params->me_search_range = IMX_VPU_ENC_ME_SEARCH_RANGE_256x128;
	open_params->use_me_zero_pmv = 0;
	open_params->additional_intra_cost_weight = 0;
	open_params->chroma_interleave = 0;

	switch (codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_MPEG4:
			open_params->codec_params.mpeg4_params.enable_data_partitioning = 0;
			open_params->codec_params.mpeg4_params.enable_reversible_vlc = 0;
			open_params->codec_params.mpeg4_params.intra_dc_vlc_thr = 0;
			open_params->codec_params.mpeg4_params.enable_hec = 0;
			open_params->codec_params.mpeg4_params.version_id = 2;
			break;

		case IMX_VPU_CODEC_FORMAT_H263:
			open_params->codec_params.h263_params.enable_annex_i = 0;
			open_params->codec_params.h263_params.enable_annex_j = 1;
			open_params->codec_params.h263_params.enable_annex_k = 0;
			open_params->codec_params.h263_params.enable_annex_t = 0;
			break;

		case IMX_VPU_CODEC_FORMAT_H264:
			open_params->codec_params.h264_params.enable_constrained_intra_prediction = 0;
			open_params->codec_params.h264_params.disable_deblocking = 0;
			open_params->codec_params.h264_params.deblock_filter_offset_alpha = 6;
			open_params->codec_params.h264_params.deblock_filter_offset_beta = 0;
			open_params->codec_params.h264_params.chroma_qp_offset = 0;
			open_params->codec_params.h264_params.enable_access_unit_delimiters = 0;
			break;

		default:
			break;
	}
}


ImxVpuEncReturnCodes imx_vpu_enc_open(ImxVpuEncoder **encoder, ImxVpuEncOpenParams *open_params, ImxVpuDMABuffer *bitstream_buffer)
{
	VpuEncRetCode ret = VPU_ENC_RET_SUCCESS;
	VpuMemInfo mem_info;
	VpuEncOpenParam open_param;
	uint8_t *bitstream_buffer_virtual_address;
	imx_vpu_phys_addr_t bitstream_buffer_physical_address;
	size_t bitstream_buffer_size;

	assert(encoder != NULL);
	assert(open_params != NULL);
	assert(bitstream_buffer != NULL);

	bitstream_buffer_size = imx_vpu_dma_buffer_get_size(bitstream_buffer);
	bitstream_buffer_virtual_address = imx_vpu_dma_buffer_map(bitstream_buffer, 0);
	bitstream_buffer_physical_address = imx_vpu_dma_buffer_get_physical_address(bitstream_buffer);

	*encoder = IMX_VPU_ALLOC(sizeof(ImxVpuEncoder));
	if ((*encoder) == NULL)
	{
		IMX_VPU_ERROR("allocating memory for encoder object failed");
		ret = VPU_ENC_RET_FAILURE;
		goto cleanup;
	}

	memset(*encoder, 0, sizeof(ImxVpuEncoder));

	(*encoder)->temp_enc_data_buffer = IMX_VPU_ALLOC(bitstream_buffer_size);
	if ((*encoder)->temp_enc_data_buffer == NULL)
	{
		IMX_VPU_ERROR("allocating memory for temporary encoded data buffer failed");
		ret = VPU_ENC_RET_FAILURE;
		goto cleanup;
	}

	{
		int i;

		VPU_EncQueryMem(&mem_info);

		IMX_VPU_INFO("about to allocate %d memory sub blocks", mem_info.nSubBlockNum);
		for (i = 0; i < mem_info.nSubBlockNum; ++i)
		{
			char const *type_str = "<unknown>";
			VpuMemSubBlockInfo *sub_block = &(mem_info.MemSubBlock[i]);

			switch (sub_block->MemType)
			{
				case VPU_MEM_VIRT:
					type_str = "virtual";

					(*encoder)->virt_mem_sub_block_size = sub_block->nSize + sub_block->nAlignment;
					(*encoder)->virt_mem_sub_block = IMX_VPU_ALLOC((*encoder)->virt_mem_sub_block_size);
					if ((*encoder)->virt_mem_sub_block == NULL)
					{
						IMX_VPU_ERROR("allocating memory for sub block failed");
						ret = VPU_ENC_RET_FAILURE;
						goto cleanup;
					}

					sub_block->pVirtAddr = (unsigned char *)IMX_VPU_ALIGN_VAL_TO((*encoder)->virt_mem_sub_block, sub_block->nAlignment);
					sub_block->pPhyAddr = 0;
					break;

				case VPU_MEM_PHY:
					type_str = "physical";

					sub_block->pVirtAddr = (unsigned char *)(bitstream_buffer_virtual_address);
					sub_block->pPhyAddr = (unsigned char *)(bitstream_buffer_physical_address);
					break;
				default:
					break;
			}

			IMX_VPU_INFO(
				"allocated memory sub block #%d:  type: %s  size: %d  alignment: %d  virtual address: %p  physical address: %" IMX_VPU_PHYS_ADDR_FORMAT,
				i,
				type_str,
				sub_block->nSize,
				sub_block->nAlignment,
				sub_block->pVirtAddr,
				sub_block->pPhyAddr
			);
		}
	}

	if (!enc_convert_to_wrapper_open_param(open_params, &open_param))
	{
		IMX_VPU_ERROR("converting open params failed");
		ret = VPU_ENC_RET_INVALID_PARAM;
		goto cleanup;
	}

	IMX_VPU_TRACE("opening encoder");

	ret = VPU_EncOpen(&((*encoder)->handle), &mem_info, &open_param);
	if (ret != VPU_ENC_RET_SUCCESS)
	{
		IMX_VPU_ERROR("opening encoder failed: %s", imx_vpu_enc_error_string(enc_convert_retcode(ret)));
		goto cleanup;
	}

	(*encoder)->codec_format = open_params->codec_format;
	(*encoder)->frame_width = open_params->frame_width;
	(*encoder)->frame_height = open_params->frame_height;
	(*encoder)->frame_rate_numerator = open_params->frame_rate_numerator;
	(*encoder)->frame_rate_denominator = open_params->frame_rate_denominator;
	(*encoder)->bitstream_buffer = bitstream_buffer;
	(*encoder)->bitstream_buffer_size = bitstream_buffer_size;

finish:
	if (ret == VPU_ENC_RET_SUCCESS)
		IMX_VPU_TRACE("successfully opened encoder");

	return enc_convert_retcode(ret);

cleanup:
	imx_vpu_dma_buffer_unmap(bitstream_buffer);

	if ((*encoder) != NULL)
	{
		if ((*encoder)->virt_mem_sub_block != NULL)
			IMX_VPU_FREE((*encoder)->virt_mem_sub_block, (*encoder)->virt_mem_sub_block_size);
		IMX_VPU_FREE(*encoder, sizeof(ImxVpuDecoder));
		*encoder = NULL;
	}

	goto finish;
}


ImxVpuEncReturnCodes imx_vpu_enc_close(ImxVpuEncoder *encoder)
{
	VpuEncRetCode ret;

	if (encoder == NULL)
		return IMX_VPU_ENC_RETURN_CODE_OK;

	ret = VPU_EncClose(encoder->handle);
	if (ret != VPU_ENC_RET_SUCCESS)
		IMX_VPU_ERROR("closing encoder failed: %s", imx_vpu_enc_error_string(enc_convert_retcode(ret)));

	if (encoder->temp_enc_data_buffer != NULL)
		IMX_VPU_FREE(encoder->temp_enc_data_buffer, encoder->bitstream_buffer_size);

	imx_vpu_dma_buffer_unmap(encoder->bitstream_buffer);

	if (encoder->framebuffers != NULL)
	{
		unsigned int i;
		for (i = 0; i < encoder->num_framebuffers; ++i)
			imx_vpu_dma_buffer_unmap(encoder->framebuffers[i].dma_buffer);
	}

	if (encoder->virt_mem_sub_block != NULL)
		IMX_VPU_FREE(encoder->virt_mem_sub_block, encoder->virt_mem_sub_block_size);
	IMX_VPU_FREE(encoder, sizeof(ImxVpuEncoder));

	IMX_VPU_TRACE("closed encoder");

	return enc_convert_retcode(ret);
}


ImxVpuDMABuffer* imx_vpu_enc_get_bitstream_buffer(ImxVpuEncoder *encoder)
{
	return encoder->bitstream_buffer;
}


ImxVpuEncReturnCodes imx_vpu_enc_flush(ImxVpuEncoder *encoder)
{
	IMXVPUAPI_UNUSED_PARAM(encoder);

	/* The VPU wrapper does not have any VPU encoder flushing functions */
	return IMX_VPU_ENC_RETURN_CODE_OK;
}


ImxVpuEncReturnCodes imx_vpu_enc_register_framebuffers(ImxVpuEncoder *encoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers)
{
	unsigned int i;
	VpuEncRetCode ret;
	VpuFrameBuffer *temp_fbs;

	assert(encoder != NULL);
	assert(framebuffers != NULL);
	assert(num_framebuffers > 0);

	IMX_VPU_TRACE("attempting to register %u framebuffers", num_framebuffers);

	temp_fbs = IMX_VPU_ALLOC(sizeof(VpuFrameBuffer) * num_framebuffers);
	if (temp_fbs == NULL)
	{
		IMX_VPU_ERROR("allocating memory for framebuffers failed");
		return IMX_VPU_ENC_RETURN_CODE_ERROR;
	}

	memset(temp_fbs, 0, sizeof(VpuFrameBuffer) * num_framebuffers);
	for (i = 0; i < num_framebuffers; ++i)
	{
		imx_vpu_phys_addr_t phys_addr;
		ImxVpuFramebuffer *fb = &framebuffers[i];

		phys_addr = imx_vpu_dma_buffer_get_physical_address(fb->dma_buffer);
		if (phys_addr == 0)
		{
			IMX_VPU_FREE(temp_fbs, sizeof(VpuFrameBuffer) * num_framebuffers);
			IMX_VPU_ERROR("could not map buffer %u/%u", i, num_framebuffers);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;
		}

		temp_fbs[i].nStrideY = fb->y_stride;
		temp_fbs[i].nStrideC = fb->cbcr_stride;

		temp_fbs[i].pbufY = (unsigned char*)(phys_addr + fb->y_offset);
		temp_fbs[i].pbufCb = (unsigned char*)(phys_addr + fb->cb_offset);
		temp_fbs[i].pbufCr = (unsigned char*)(phys_addr + fb->cr_offset);
		temp_fbs[i].pbufMvCol = (unsigned char*)(phys_addr + fb->mvcol_offset);
	}

	ret = VPU_EncRegisterFrameBuffer(encoder->handle, temp_fbs, num_framebuffers, temp_fbs[0].nStrideY);

	IMX_VPU_FREE(temp_fbs, sizeof(VpuFrameBuffer) * num_framebuffers);

	if (ret != VPU_ENC_RET_SUCCESS)
	{
		ImxVpuEncReturnCodes imxret = enc_convert_retcode(ret);
		IMX_VPU_ERROR("registering framebuffers failed: %s", imx_vpu_enc_error_string(imxret));
		return ret;
	}

	encoder->framebuffers = framebuffers;
	encoder->num_framebuffers = num_framebuffers;

	return IMX_VPU_DEC_RETURN_CODE_OK;
}


ImxVpuEncReturnCodes imx_vpu_enc_get_initial_info(ImxVpuEncoder *encoder, ImxVpuEncInitialInfo *info)
{
	VpuEncRetCode ret;
	VpuEncInitInfo init_info;

	assert(encoder != NULL);
	assert(info != NULL);

	ret = VPU_EncGetInitialInfo(encoder->handle, &init_info);
	IMX_VPU_LOG("VPU_EncGetInitialInfo: min num framebuffers required: %d", init_info.nMinFrameBufferCount);
	enc_convert_from_wrapper_initial_info(&init_info, info);
	return enc_convert_retcode(ret);
}


void imx_vpu_enc_set_default_encoding_params(ImxVpuEncoder *encoder, ImxVpuEncParams *encoding_params)
{
	assert(encoding_params != NULL);

	IMXVPUAPI_UNUSED_PARAM(encoder);

	encoding_params->force_I_frame = 0;
	encoding_params->skip_frame = 0;
	encoding_params->enable_autoskip = 0;
}


void imx_vpu_enc_configure_bitrate(ImxVpuEncoder *encoder, unsigned int bitrate)
{
	int param;
	assert(encoder != NULL);
	param = bitrate;
	VPU_EncConfig(encoder->handle, VPU_ENC_CONF_BIT_RATE, &param);
}


void imx_vpu_enc_configure_min_intra_refresh(ImxVpuEncoder *encoder, unsigned int min_intra_refresh_num)
{
	int param;
	assert(encoder != NULL);
	if (encoder->codec_format != IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		/* MJPEG does not support this parameter */
		param = min_intra_refresh_num;
		VPU_EncConfig(encoder->handle, VPU_ENC_CONF_INTRA_REFRESH, &param);
	}
}


void imx_vpu_enc_configure_intra_qp(ImxVpuEncoder *encoder, int intra_qp)
{
	assert(encoder != NULL);
	VPU_EncConfig(encoder->handle, VPU_ENC_CONF_RC_INTRA_QP, &intra_qp);
}


ImxVpuEncReturnCodes imx_vpu_enc_encode(ImxVpuEncoder *encoder, ImxVpuRawFrame const *raw_frame, ImxVpuEncodedFrame *encoded_frame, ImxVpuEncParams *encoding_params,  unsigned int *output_code)
{
	VpuEncRetCode ret;
	VpuEncEncParam enc_enc_param;
	VpuFrameBuffer in_framebuffer;
	imx_vpu_phys_addr_t raw_frame_phys_addr;
	uint8_t *write_ptr;
	void *output_buffer_ptr;
	size_t encoded_data_size;

	assert(encoder != NULL);
	assert(encoded_frame != NULL);
	assert(encoding_params != NULL);
	assert(output_code != NULL);

	raw_frame_phys_addr = imx_vpu_dma_buffer_get_physical_address(raw_frame->framebuffer->dma_buffer);

	memset(&enc_enc_param, 0, sizeof(enc_enc_param));

	in_framebuffer.nStrideY = raw_frame->framebuffer->y_stride;
	in_framebuffer.nStrideC = raw_frame->framebuffer->cbcr_stride;
	in_framebuffer.pbufY = (unsigned char*)(raw_frame_phys_addr + raw_frame->framebuffer->y_offset);
	in_framebuffer.pbufCb = (unsigned char*)(raw_frame_phys_addr + raw_frame->framebuffer->cb_offset);
	in_framebuffer.pbufCr = (unsigned char*)(raw_frame_phys_addr + raw_frame->framebuffer->cr_offset);
	in_framebuffer.pbufMvCol = (unsigned char*)(raw_frame_phys_addr + raw_frame->framebuffer->mvcol_offset);

	enc_enc_param.eFormat = convert_to_wrapper_codec_std(encoder->codec_format);
	enc_enc_param.nPicWidth = encoder->frame_width;
	enc_enc_param.nPicHeight = encoder->frame_height;
	/* Unlike with VpuEncOpenParam, the frame rate here has to be an integer
	 * value, and not a numerator/denominator pair, so calculate an integer
	 * result out of numerator/denominator, rounding up */
	enc_enc_param.nFrameRate = (encoder->frame_rate_numerator + (encoder->frame_rate_denominator - 1)) / encoder->frame_rate_denominator;
	enc_enc_param.nQuantParam = encoding_params->quant_param;

	enc_enc_param.nInPhyOutput = 0; /* This is not used by the VPU wrapper on i.MX6 SoCs */

	enc_enc_param.nForceIPicture = encoding_params->force_I_frame;
	enc_enc_param.nSkipPicture = encoding_params->skip_frame;
	enc_enc_param.nEnableAutoSkip = encoding_params->enable_autoskip;

	enc_enc_param.pInFrame = &in_framebuffer;

	write_ptr = encoder->temp_enc_data_buffer;
	encoded_data_size = 0;
	*output_code = 0;

	/* When encoding h.264 or MPEG4, the VPU wrapper outputs the header separately.
	 * imxvpuapi however does not. To solve this, gather output data until the
	 * wrapper sets the VPU_ENC_INPUT_USED output code. This is safe, since the
	 * wrapper never sets this code until the actual frame is encoded. So, if for
	 * example h.264 is encoded, then the first VPU_EncEncodeFrame call will
	 * produce an output code with the VPU_ENC_OUTPUT_SEQHEADER bit but without
	 * the VPU_ENC_INPUT_USED. The loop continues. In the second iteration, the
	 * call will return an output code that contains VPU_ENC_INPUT_USED. This
	 * second call will also produce the actual encoded frame. Loop exits. */
	while (TRUE)
	{
		size_t num_written_bytes = write_ptr - encoder->temp_enc_data_buffer;

		if (num_written_bytes >= encoder->bitstream_buffer_size)
		{
			IMX_VPU_ERROR("cannot encode frame - ran out of temporary encoded data buffer space");
			return IMX_VPU_ENC_RETURN_CODE_ERROR;
		}

		enc_enc_param.nInVirtOutput = (unsigned int)write_ptr;
		enc_enc_param.nInOutputBufLen = encoder->bitstream_buffer_size - num_written_bytes;

		ret = VPU_EncEncodeFrame(encoder->handle, &enc_enc_param);
		IMX_VPU_LOG("VPU_EncEncodeFrame out ret code: 0x%x size: %d", enc_enc_param.eOutRetCode, enc_enc_param.nOutOutputSize);

		if (ret != VPU_ENC_RET_SUCCESS)
		{
			IMX_VPU_ERROR("encoding frame failed: %s", imx_vpu_enc_error_string(enc_convert_retcode(ret)));
			return enc_convert_retcode(ret);
		}

		encoded_data_size += enc_enc_param.nOutOutputSize;
		write_ptr += enc_enc_param.nOutOutputSize;

		if (enc_enc_param.eOutRetCode & VPU_ENC_OUTPUT_DIS)
			*output_code |= IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE;

		if (enc_enc_param.eOutRetCode & VPU_ENC_OUTPUT_SEQHEADER)
			*output_code |= IMX_VPU_ENC_OUTPUT_CODE_CONTAINS_HEADER;

		if (enc_enc_param.eOutRetCode & VPU_ENC_INPUT_USED)
		{
			*output_code |= IMX_VPU_ENC_OUTPUT_CODE_INPUT_USED;
			break;
		}
	}

	/* Acquire an output buffer and Transfer the encoded data to it */
	output_buffer_ptr = encoding_params->acquire_output_buffer(encoding_params->output_buffer_context, encoded_data_size, &(encoded_frame->acquired_handle));
	if (output_buffer_ptr == NULL)
	{
		IMX_VPU_ERROR("could not acquire buffer with %zu byte for encoded frame data", encoded_data_size);
		return IMX_VPU_ENC_RETURN_CODE_ERROR;
	}
	memcpy(output_buffer_ptr, encoder->temp_enc_data_buffer, encoded_data_size);
	encoding_params->finish_output_buffer(encoding_params->output_buffer_context, encoded_frame->acquired_handle);

	/* Since the encoder does not perform any kind of delay
	 * or reordering, this is appropriate, because in that
	 * case, one input frame always immediately leads to
	 * one output frame */
	encoded_frame->context = raw_frame->context;

	return IMX_VPU_DEC_RETURN_CODE_OK;
}
