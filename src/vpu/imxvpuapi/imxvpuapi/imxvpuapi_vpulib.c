/* imxvpuapi implementation on top of the Freescale imx-vpu library
 * Copyright (C) 2015 Carlos Rafael Giani
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
#include <vpu_lib.h>
#include <vpu_io.h>
#include "imxvpuapi.h"
#include "imxvpuapi_priv.h"
#include "imxvpuapi_parse_jpeg.h"




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


/* This catches fringe cases where somebody passes a
 * non-null value as TRUE that is not the same as TRUE */
#define TO_BOOL(X) ((X) ? TRUE : FALSE)


#define MIN_NUM_FREE_FB_REQUIRED 6
#define FRAME_ALIGN 16

#define VPU_MEMORY_ALIGNMENT         0x8
#define VPU_MAIN_BITSTREAM_BUFFER_SIZE    (1024*1024*3)
#define VPU_MAX_SLICE_BUFFER_SIZE    (1920*1088*15/20)
#define VPU_PS_SAVE_BUFFER_SIZE      (1024*512)
#define VPU_VP8_MB_PRED_BUFFER_SIZE  (68*(1920*1088/256))

/* The bitstream buffer shares space with other fields,
 * to not have to allocate several DMA blocks. The actual bitstream buffer is called
 * the "main bitstream buffer". It makes up all bytes from the start of the buffer
 * and is VPU_MAIN_BITSTREAM_BUFFER_SIZE large. Bytes beyond that are codec format
 * specific data. */
#define VPU_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE  (VPU_MAIN_BITSTREAM_BUFFER_SIZE + VPU_MAX_SLICE_BUFFER_SIZE + VPU_PS_SAVE_BUFFER_SIZE)

#define VP8_SEQUENCE_HEADER_SIZE  32
#define VP8_FRAME_HEADER_SIZE     12

#define WMV3_RCV_SEQUENCE_LAYER_SIZE (6 * 4)
#define WMV3_RCV_FRAME_LAYER_SIZE    4

#define VC1_NAL_FRAME_LAYER_MAX_SIZE   4
#define VC1_IS_NOT_NAL(ID)             (( (ID) & 0x00FFFFFF) != 0x00010000)

#define VPU_WAIT_TIMEOUT             500 /* milliseconds to wait for frame completion */
#define VPU_MAX_TIMEOUT_COUNTS       4   /* how many timeouts are allowed in series */


static unsigned long vpu_init_inst_counter = 0;


static BOOL imx_vpu_load(void)
{
	IMX_VPU_TRACE("VPU init instance counter: %lu", vpu_init_inst_counter);

	if (vpu_init_inst_counter != 0)
	{
		++vpu_init_inst_counter;
		return TRUE;
	}
	else
	{
		if (vpu_Init(NULL) == RETCODE_SUCCESS)
		{
			IMX_VPU_TRACE("loaded VPU");
			++vpu_init_inst_counter;
			return TRUE;
		}
		else
		{
			IMX_VPU_ERROR("loading VPU failed");
			return FALSE;
		}
	}

}


static BOOL imx_vpu_unload(void)
{
	IMX_VPU_TRACE("VPU init instance counter: %lu", vpu_init_inst_counter);

	if (vpu_init_inst_counter != 0)
	{
		--vpu_init_inst_counter;

		if (vpu_init_inst_counter == 0)
		{
			vpu_UnInit();
			IMX_VPU_TRACE("unloaded VPU");
		}
	}

	return TRUE;
}


static ImxVpuPicType convert_pic_type(ImxVpuCodecFormat codec_format, int pic_type)
{
	switch (codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_H264:
		case IMX_VPU_CODEC_FORMAT_H264_MVC:
			if ((pic_type & 0x01) == 0)
				return IMX_VPU_PIC_TYPE_IDR;
			else
			{
				switch ((pic_type >> 1) & 0x03)
				{
					case 0: return IMX_VPU_PIC_TYPE_I;
					case 1: return IMX_VPU_PIC_TYPE_P;
					case 2: case 3: return IMX_VPU_PIC_TYPE_B;
					default: break;
				}
			}
			break;

		case IMX_VPU_CODEC_FORMAT_WMV3:
			switch (pic_type & 0x07)
			{
				case 0: return IMX_VPU_PIC_TYPE_I;
				case 1: return IMX_VPU_PIC_TYPE_P;
				case 2: return IMX_VPU_PIC_TYPE_BI;
				case 3: return IMX_VPU_PIC_TYPE_B;
				case 4: return IMX_VPU_PIC_TYPE_SKIP;
				default: break;
			}
			break;

		/*case IMX_VPU_CODEC_FORMAT_WVC1: // TODO
			break;*/

		default:
			switch (pic_type)
			{
				case 0: return IMX_VPU_PIC_TYPE_I;
				case 1: return IMX_VPU_PIC_TYPE_P;
				case 2: case 3: return IMX_VPU_PIC_TYPE_B;
				default: break;
			}
	}

	return IMX_VPU_PIC_TYPE_UNKNOWN;
}


ImxVpuFieldType convert_field_type(ImxVpuCodecFormat codec_format, DecOutputInfo *dec_output_info)
{
	if (dec_output_info->interlacedFrame)
	{
		ImxVpuFieldType result = dec_output_info->topFieldFirst ? IMX_VPU_FIELD_TYPE_TOP_FIRST : IMX_VPU_FIELD_TYPE_BOTTOM_FIRST;

		if ((codec_format == IMX_VPU_CODEC_FORMAT_H264) || (codec_format == IMX_VPU_CODEC_FORMAT_H264_MVC))
		{
			switch (dec_output_info->h264Npf)
			{
				case 1: result = IMX_VPU_FIELD_TYPE_BOTTOM_ONLY; break;
				case 2: result = IMX_VPU_FIELD_TYPE_TOP_ONLY; break;
				default: break;
			}
		}

		return result;
	}
	else
		return IMX_VPU_FIELD_TYPE_NO_INTERLACING;
}




/**************************************************/
/******* ALLOCATOR STRUCTURES AND FUNCTIONS *******/
/**************************************************/


/*********** Default allocator ***********/



typedef struct
{
	ImxVpuDMABuffer parent;
	vpu_mem_desc mem_desc;

	/* Not the same as mem_desc->size
	 * the value in mem_desc is potentially larger due to alignment */
	size_t size;

	uint8_t*            aligned_virtual_address;
	imx_vpu_phys_addr_t aligned_physical_address;
}
DefaultDMABuffer;


typedef struct
{
	ImxVpuDMABufferAllocator parent;
}
DefaultDMABufferAllocator;


static ImxVpuDMABuffer* default_dmabufalloc_allocate(ImxVpuDMABufferAllocator *allocator, size_t size, unsigned int alignment, unsigned int flags)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	IMXVPUAPI_UNUSED_PARAM(flags);

	DefaultDMABuffer *dmabuffer = IMX_VPU_ALLOC(sizeof(DefaultDMABuffer));
	if (dmabuffer == NULL)
	{
		IMX_VPU_ERROR("allocating heap block for DMA buffer failed");
		return NULL;
	}

	dmabuffer->mem_desc.size = size;

	if (alignment == 0)
		alignment = 1;
	if (alignment > 1)
		dmabuffer->mem_desc.size += alignment;

	dmabuffer->parent.allocator = allocator;
	dmabuffer->size = size;

	if (IOGetPhyMem(&(dmabuffer->mem_desc)) == RETCODE_FAILURE)
	{
		IMX_VPU_FREE(dmabuffer, sizeof(DefaultDMABuffer));
		IMX_VPU_ERROR("allocating %d bytes of physical memory failed", size);
		return NULL;
	}
	else
		IMX_VPU_TRACE("allocated %d bytes of physical memory", size);

	if (IOGetVirtMem(&(dmabuffer->mem_desc)) == RETCODE_FAILURE)
	{
		IOFreePhyMem(&(dmabuffer->mem_desc));
		IMX_VPU_FREE(dmabuffer, sizeof(DefaultDMABuffer));
		IMX_VPU_ERROR("retrieving virtual address for physical memory failed");
		return NULL;
	}
	else
		IMX_VPU_TRACE("retrieved virtual address for physical memory");

	dmabuffer->aligned_virtual_address = (uint8_t *)IMX_VPU_ALIGN_VAL_TO((uint8_t *)(dmabuffer->mem_desc.virt_uaddr), alignment);
	dmabuffer->aligned_physical_address = (imx_vpu_phys_addr_t)IMX_VPU_ALIGN_VAL_TO((imx_vpu_phys_addr_t)(dmabuffer->mem_desc.phy_addr), alignment);

	IMX_VPU_TRACE("virtual address:  0x%x  aligned: %p", dmabuffer->mem_desc.virt_uaddr, dmabuffer->aligned_virtual_address);
	IMX_VPU_TRACE("physical address: 0x%x  aligned: %" IMX_VPU_PHYS_ADDR_FORMAT, dmabuffer->mem_desc.phy_addr, dmabuffer->aligned_physical_address);

	return (ImxVpuDMABuffer *)dmabuffer;
}


static void default_dmabufalloc_deallocate(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);

	DefaultDMABuffer *defaultbuf = (DefaultDMABuffer *)buffer;

	if (IOFreePhyMem(&(defaultbuf->mem_desc)) != 0)
		IMX_VPU_ERROR("deallocating %d bytes of physical memory failed", defaultbuf->size);
	else
		IMX_VPU_TRACE("deallocated %d bytes of physical memory", defaultbuf->size);
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


static DefaultDMABufferAllocator default_dma_buffer_allocator =
{
	{
		default_dmabufalloc_allocate,
		default_dmabufalloc_deallocate,
		default_dmabufalloc_map,
		default_dmabufalloc_unmap,
		default_dmabufalloc_get_fd,
		default_dmabufalloc_get_physical_address,
		default_dmabufalloc_get_size
	}
};




/******************************************************/
/******* MISCELLANEOUS STRUCTURES AND FUNCTIONS *******/
/******************************************************/


void imx_vpu_calc_framebuffer_sizes(ImxVpuColorFormat color_format, unsigned int frame_width, unsigned int frame_height, unsigned int framebuffer_alignment, int uses_interlacing, ImxVpuFramebufferSizes *calculated_sizes)
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
		case IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL:
			calculated_sizes->cbcr_stride = calculated_sizes->y_stride / 2;
			calculated_sizes->cbcr_size = calculated_sizes->mvcol_size = calculated_sizes->y_size / 2;
			break;
		case IMX_VPU_COLOR_FORMAT_YUV444:
			calculated_sizes->cbcr_stride = calculated_sizes->y_stride;
			calculated_sizes->cbcr_size = calculated_sizes->mvcol_size = calculated_sizes->y_size;
			break;
		case IMX_VPU_COLOR_FORMAT_YUV400:
			/* TODO: check if this is OK */
			calculated_sizes->cbcr_stride = 0;
			calculated_sizes->cbcr_size = calculated_sizes->mvcol_size = 0;
			break;
		default:
			assert(FALSE);
	}

	alignment = framebuffer_alignment;
	if (alignment > 1)
	{
		calculated_sizes->y_size = IMX_VPU_ALIGN_VAL_TO(calculated_sizes->y_size, alignment);
		calculated_sizes->cbcr_size = IMX_VPU_ALIGN_VAL_TO(calculated_sizes->cbcr_size, alignment);
		calculated_sizes->mvcol_size = IMX_VPU_ALIGN_VAL_TO(calculated_sizes->mvcol_size, alignment);
	}

	calculated_sizes->total_size = calculated_sizes->y_size + calculated_sizes->cbcr_size + calculated_sizes->cbcr_size + calculated_sizes->mvcol_size + alignment;
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
	framebuffer->mvcol_offset = calculated_sizes->y_size + calculated_sizes->cbcr_size * 2;
}




/************************************************/
/******* DECODER STRUCTURES AND FUNCTIONS *******/
/************************************************/


/* Frames are not just occupied or free. They can be in one of three modes:
 * - FrameMode_Free: framebuffer is not being used for decoding, and does not hold
     a displayable picture
 * - FrameMode_ReservedForDecoding: framebuffer contains picture data that is
 *   being decoded; this data can not be displayed yet though
 * - FrameMode_ContainsDisplayablePicture: framebuffer contains picture that has
 *   been fully decoded; this can be displayed
 *
 * Frames in FrameMode_ReservedForDecoding do not reach the outside. Only frames in
 * FrameMode_ContainsDisplayablePicture mode, via the imx_vpu_dec_get_decoded_picture()
 * function.
 */
typedef enum
{
	FrameMode_Free,
	FrameMode_ReservedForDecoding,
	FrameMode_ContainsDisplayablePicture
}
FrameMode;


struct _ImxVpuDecoder
{
	DecHandle handle;

	ImxVpuDMABuffer *bitstream_buffer;
	uint8_t *bitstream_buffer_virtual_address;
	imx_vpu_phys_addr_t bitstream_buffer_physical_address;

	ImxVpuCodecFormat codec_format;
	unsigned int picture_width, picture_height;

	unsigned int old_jpeg_width, old_jpeg_height;
	ImxVpuColorFormat old_jpeg_color_format;

	unsigned int num_framebuffers, num_used_framebuffers;
	FrameBuffer *internal_framebuffers;
	ImxVpuFramebuffer *framebuffers;
	void **context_for_frames;
	FrameMode *frame_modes;
	void *dropped_frame_context;

	BOOL main_header_pushed;

	BOOL drain_mode_enabled;
	BOOL drain_eos_sent_to_vpu;

	DecInitialInfo initial_info;
	BOOL initial_info_available;

	DecOutputInfo dec_output_info;
	int available_decoded_pic_idx;

	imx_vpu_dec_new_initial_info_callback initial_info_callback;
	void *callback_user_data;
};


#define IMX_VPU_DEC_HANDLE_ERROR(MSG_START, RET_CODE) \
	imx_vpu_dec_handle_error_full(__FILE__, __LINE__, __FUNCTION__, (MSG_START), (RET_CODE))


#define VPU_DECODER_DISPLAYIDX_ALL_PICTURED_DISPLAYED -1
#define VPU_DECODER_DISPLAYIDX_SKIP_MODE_NO_PICTURE_TO_DISPLAY -2
#define VPU_DECODER_DISPLAYIDX_NO_PICTURE_TO_DISPLAY -3

#define VPU_DECODER_DECODEIDX_ALL_FRAMES_DECODED -1
#define VPU_DECODER_DECODEIDX_FRAME_NOT_DECODED -2


static ImxVpuDecReturnCodes imx_vpu_dec_handle_error_full(char const *fn, int linenr, char const *funcn, char const *msg_start, RetCode ret_code);
static ImxVpuDecReturnCodes imx_vpu_dec_get_initial_info_internal(ImxVpuDecoder *decoder);

static void imx_vpu_dec_insert_vp8_ivf_main_header(uint8_t *header, unsigned int pic_width, unsigned int pic_height);
static void imx_vpu_dec_insert_vp8_ivf_frame_header(uint8_t *header, uint32_t main_data_size, uint64_t pts);

static void imx_vpu_dec_insert_wmv3_sequence_layer_header(uint8_t *header, unsigned int pic_width, unsigned int pic_height, unsigned int main_data_size, uint8_t *codec_data);
static void imx_vpu_dec_insert_wmv3_frame_layer_header(uint8_t *header, unsigned int main_data_size);

static void imx_vpu_dec_insert_vc1_frame_layer_header(uint8_t *header, uint8_t *main_data, unsigned int *actual_header_length);

static ImxVpuDecReturnCodes imx_vpu_dec_insert_frame_headers(ImxVpuDecoder *decoder, uint8_t *codec_data, unsigned int codec_data_size, uint8_t *main_data, unsigned int main_data_size);

static ImxVpuDecReturnCodes imx_vpu_dec_push_input_data(ImxVpuDecoder *decoder, void *data, unsigned int data_size);

static int imx_vpu_dec_find_free_framebuffer(ImxVpuDecoder *decoder);


static ImxVpuDecReturnCodes imx_vpu_dec_handle_error_full(char const *fn, int linenr, char const *funcn, char const *msg_start, RetCode ret_code)
{
	switch (ret_code)
	{
		case RETCODE_SUCCESS:
			return IMX_VPU_DEC_RETURN_CODE_OK;

		case RETCODE_FAILURE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: failure", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		case RETCODE_INVALID_HANDLE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid handle", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_HANDLE;

		case RETCODE_INVALID_PARAM:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid parameters", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_INVALID_COMMAND:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid command", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		case RETCODE_ROTATOR_OUTPUT_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: rotation enabled but rotator output buffer not set", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_ROTATOR_STRIDE_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: rotation enabled but rotator stride not set", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_FRAME_NOT_COMPLETE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: frame decoding operation not complete", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		case RETCODE_INVALID_FRAME_BUFFER:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: frame buffers are invalid", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_INSUFFICIENT_FRAME_BUFFERS:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: not enough frame buffers specified (must be equal to or larger than the minimum number reported by imx_vpu_dec_get_initial_info)", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_INVALID_STRIDE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid stride - check Y stride values of framebuffers (must be a multiple of 8 and equal to or larger than the picture width)", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_WRONG_CALL_SEQUENCE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: wrong call sequence", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE;

		case RETCODE_CALLED_BEFORE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: already called before (may not be called more than once in a VPU instance)", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ALREADY_CALLED;

		case RETCODE_NOT_INITIALIZED:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: VPU is not initialized", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE;

		case RETCODE_DEBLOCKING_OUTPUT_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: deblocking activated but deblocking information not available", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		case RETCODE_NOT_SUPPORTED:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: feature not supported", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		case RETCODE_REPORT_BUF_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: data report buffer address not set", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_FAILURE_TIMEOUT:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: timeout", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		case RETCODE_MEMORY_ACCESS_VIOLATION:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: memory access violation", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		case RETCODE_JPEG_EOS:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: MJPEG end-of-stream reached", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_OK;

		case RETCODE_JPEG_BIT_EMPTY:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: MJPEG bit buffer empty - cannot parse header", msg_start);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;

		default:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: unknown error 0x%x", msg_start, ret_code);
			return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}
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
		case IMX_VPU_DEC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS: return "insufficient framebuffers";
		case IMX_VPU_DEC_RETURN_CODE_INVALID_STRIDE:            return "invalid stride";
		case IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE:       return "wrong call sequence";
		case IMX_VPU_DEC_RETURN_CODE_TIMEOUT:                   return "timeout";
		case IMX_VPU_DEC_RETURN_CODE_ALREADY_CALLED:            return "already called";
		default: return "<unknown>";
	}
}


ImxVpuDecReturnCodes imx_vpu_dec_load(void)
{
	return imx_vpu_load() ? IMX_VPU_DEC_RETURN_CODE_OK : IMX_VPU_DEC_RETURN_CODE_ERROR;
}


ImxVpuDecReturnCodes imx_vpu_dec_unload(void)
{
	return imx_vpu_unload() ? IMX_VPU_DEC_RETURN_CODE_OK : IMX_VPU_DEC_RETURN_CODE_ERROR;
}


ImxVpuDMABufferAllocator* imx_vpu_dec_get_default_allocator(void)
{
	return (ImxVpuDMABufferAllocator*)(&default_dma_buffer_allocator);
}


void imx_vpu_dec_get_bitstream_buffer_info(size_t *size, unsigned int *alignment)
{
	assert(VPU_VP8_MB_PRED_BUFFER_SIZE < (VPU_MAX_SLICE_BUFFER_SIZE + VPU_PS_SAVE_BUFFER_SIZE));
	*size = VPU_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE;
	*alignment = VPU_MEMORY_ALIGNMENT;
}


ImxVpuDecReturnCodes imx_vpu_dec_open(ImxVpuDecoder **decoder, ImxVpuDecOpenParams *open_params, ImxVpuDMABuffer *bitstream_buffer, imx_vpu_dec_new_initial_info_callback new_initial_info_callback, void *callback_user_data)
{
	ImxVpuDecReturnCodes ret;
	DecOpenParam dec_open_param;
	RetCode dec_ret;

	assert(decoder != NULL);
	assert(open_params != NULL);
	assert(bitstream_buffer != NULL);


	/* Check that the allocated bitstream buffer is big enough */
	assert(imx_vpu_dma_buffer_get_size(bitstream_buffer) >= VPU_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE);


	/* Allocate decoder instance */
	*decoder = IMX_VPU_ALLOC(sizeof(ImxVpuDecoder));
	if ((*decoder) == NULL)
	{
		IMX_VPU_ERROR("allocating memory for decoder object failed");
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}


	/* Set default decoder values */
	memset(*decoder, 0, sizeof(ImxVpuDecoder));
	(*decoder)->available_decoded_pic_idx = -1;


	/* Map the bitstream buffer. This mapping will persist until the decoder is closed. */
	(*decoder)->bitstream_buffer_virtual_address = imx_vpu_dma_buffer_map(bitstream_buffer, 0);
	(*decoder)->bitstream_buffer_physical_address = imx_vpu_dma_buffer_get_physical_address(bitstream_buffer);

	(*decoder)->initial_info_callback = new_initial_info_callback;
	(*decoder)->callback_user_data = callback_user_data;


	/* Fill in values into the VPU's decoder open param structure */
	memset(&dec_open_param, 0, sizeof(dec_open_param));
	switch (open_params->codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_H264:
		case IMX_VPU_CODEC_FORMAT_H264_MVC:
			dec_open_param.bitstreamFormat = STD_AVC;
			dec_open_param.reorderEnable = open_params->enable_frame_reordering;
			break;
		case IMX_VPU_CODEC_FORMAT_MPEG2:
			dec_open_param.bitstreamFormat = STD_MPEG2;
			break;
		case IMX_VPU_CODEC_FORMAT_MPEG4:
			dec_open_param.bitstreamFormat = STD_MPEG4;
			dec_open_param.mp4Class = 0;
			break;
		case IMX_VPU_CODEC_FORMAT_H263:
			dec_open_param.bitstreamFormat = STD_H263;
			break;
		case IMX_VPU_CODEC_FORMAT_WMV3:
			dec_open_param.bitstreamFormat = STD_VC1;
			break;
		case IMX_VPU_CODEC_FORMAT_WVC1:
			dec_open_param.bitstreamFormat = STD_VC1;
			dec_open_param.reorderEnable = 1;
			break;
		case IMX_VPU_CODEC_FORMAT_MJPEG:
			dec_open_param.bitstreamFormat = STD_MJPG;
			break;
		case IMX_VPU_CODEC_FORMAT_VP8:
			dec_open_param.bitstreamFormat = STD_VP8;
			dec_open_param.reorderEnable = 1;
			break;
		default:
			break;
	}

	dec_open_param.bitstreamBuffer = (*decoder)->bitstream_buffer_physical_address;
	dec_open_param.bitstreamBufferSize = VPU_MAIN_BITSTREAM_BUFFER_SIZE;
	dec_open_param.qpReport = 0;
	dec_open_param.mp4DeblkEnable = 0;
	dec_open_param.chromaInterleave = 0;
	dec_open_param.filePlayEnable = 0;
	dec_open_param.picWidth = open_params->frame_width;
	dec_open_param.picHeight = open_params->frame_height;
	dec_open_param.avcExtension = (open_params->codec_format == IMX_VPU_CODEC_FORMAT_H264_MVC);
	dec_open_param.dynamicAllocEnable = 0;
	dec_open_param.streamStartByteOffset = 0;
	dec_open_param.mjpg_thumbNailDecEnable = 0;
	dec_open_param.psSaveBuffer = (*decoder)->bitstream_buffer_physical_address + VPU_MAIN_BITSTREAM_BUFFER_SIZE + VPU_MAX_SLICE_BUFFER_SIZE;
	dec_open_param.psSaveBufferSize = VPU_PS_SAVE_BUFFER_SIZE;
	dec_open_param.mapType = 0;
	dec_open_param.tiled2LinearEnable = 0; // this must ALWAYS be 0, otherwise VPU hangs eventually (it is 0 in the wrapper except for MX6X)
	dec_open_param.bitstreamMode = 1;

	/* Motion-JPEG specific settings
	 * With motion JPEG, the VPU is configured to operate in line buffer mode,
	 * because it is easier to handle. During decoding, pointers to the
	 * beginning of the JPEG data inside the bitstream buffer have to be set,
	 * which is much simpler if the VPU operates in line buffer mode (one then
	 * has to only set the pointers to refer to the beginning of the bitstream
	 * buffer, since in line buffer mode, this is where the encoded frame
	 * is always placed*/
	if (open_params->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		dec_open_param.jpgLineBufferMode = 1;
		/* This one is not mentioned in the specs for some reason,
		 * but is required for motion JPEG to work */
		dec_open_param.pBitStream = (*decoder)->bitstream_buffer_virtual_address;
	}
	else
		dec_open_param.jpgLineBufferMode = 0;


	/* Now actually open the decoder instance */
	IMX_VPU_TRACE("opening decoder, picture size: %u x %u pixel", open_params->frame_width, open_params->frame_height);
	dec_ret = vpu_DecOpen(&((*decoder)->handle), &dec_open_param);
	ret = IMX_VPU_DEC_HANDLE_ERROR("could not open decoder", dec_ret);
	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
		goto cleanup;

	(*decoder)->codec_format = open_params->codec_format;
	(*decoder)->bitstream_buffer = bitstream_buffer;
	(*decoder)->picture_width = open_params->frame_width;
	(*decoder)->picture_height = open_params->frame_height;


	/* Finish & cleanup (in case of error) */
finish:
	if (ret == IMX_VPU_DEC_RETURN_CODE_OK)
		IMX_VPU_TRACE("successfully opened decoder");

	return ret;

cleanup:
	imx_vpu_dma_buffer_unmap(bitstream_buffer);
	IMX_VPU_FREE(*decoder, sizeof(ImxVpuDecoder));
	*decoder = NULL;

	goto finish;
}


ImxVpuDecReturnCodes imx_vpu_dec_close(ImxVpuDecoder *decoder)
{
	ImxVpuDecReturnCodes ret;
	RetCode dec_ret;

	assert(decoder != NULL);

	IMX_VPU_TRACE("closing decoder");


	// TODO: call vpu_DecGetOutputInfo() for all started frames


	/* Flush the VPU bit buffer */
	if (decoder->codec_format != IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		dec_ret = vpu_DecBitBufferFlush(decoder->handle);
		ret = IMX_VPU_DEC_HANDLE_ERROR("could not flush decoder", dec_ret);
	}

	/* Signal EOS to the decoder by passing 0 as size to vpu_DecUpdateBitstreamBuffer() */
	dec_ret = vpu_DecUpdateBitstreamBuffer(decoder->handle, 0);
	ret = IMX_VPU_DEC_HANDLE_ERROR("could not signal EOS to the decoder", dec_ret);

	/* Now, actually close the decoder */
	dec_ret = vpu_DecClose(decoder->handle);
	ret = IMX_VPU_DEC_HANDLE_ERROR("could not close decoder", dec_ret);


	/* Remaining cleanup */

	if (decoder->internal_framebuffers != NULL)
		IMX_VPU_FREE(decoder->internal_framebuffers, sizeof(FrameBuffer) * decoder->num_framebuffers);
	if (decoder->context_for_frames != NULL)
		IMX_VPU_FREE(decoder->context_for_frames, sizeof(void*) * decoder->num_framebuffers);
	if (decoder->frame_modes != NULL)
		IMX_VPU_FREE(decoder->frame_modes, sizeof(FrameMode) * decoder->num_framebuffers);

	IMX_VPU_FREE(decoder, sizeof(ImxVpuDecoder));

	if (ret == IMX_VPU_DEC_RETURN_CODE_OK)
		IMX_VPU_TRACE("closed decoder");

	return ret;
}


ImxVpuDMABuffer* imx_vpu_dec_get_bitstream_buffer(ImxVpuDecoder *decoder)
{
	return decoder->bitstream_buffer;
}


ImxVpuDecReturnCodes imx_vpu_dec_enable_drain_mode(ImxVpuDecoder *decoder, int enabled)
{
	assert(decoder != NULL);

	if (decoder->drain_mode_enabled == TO_BOOL(enabled))
		return IMX_VPU_DEC_RETURN_CODE_OK;

	decoder->drain_mode_enabled = TO_BOOL(enabled);
	if (enabled)
		decoder->drain_eos_sent_to_vpu = FALSE;

	IMX_VPU_INFO("set decoder drain mode to %d", enabled);

	return IMX_VPU_DEC_RETURN_CODE_OK;
}


int imx_vpu_dec_is_drain_mode_enabled(ImxVpuDecoder *decoder)
{
	assert(decoder != NULL);
	return decoder->drain_mode_enabled;
}


ImxVpuDecReturnCodes imx_vpu_dec_flush(ImxVpuDecoder *decoder)
{
	ImxVpuDecReturnCodes ret;
	RetCode dec_ret;
	unsigned int i;

	assert(decoder != NULL);

	IMX_VPU_TRACE("flushing decoder");

	if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_WMV3)
		return IMX_VPU_DEC_RETURN_CODE_OK;


	/* Clear any framebuffers that aren't ready for display yet but
	 * are being used for decoding (since flushing will clear them) */
	for (i = 0; i < decoder->num_framebuffers; ++i)
	{
		if (decoder->frame_modes[i] == FrameMode_ReservedForDecoding)
		{
			dec_ret = vpu_DecClrDispFlag(decoder->handle, i);
			IMX_VPU_DEC_HANDLE_ERROR("vpu_DecClrDispFlag failed while flushing", dec_ret);
			decoder->frame_modes[i] = FrameMode_Free;
		}
	}


	/* Perform the actual flush */
	dec_ret = vpu_DecBitBufferFlush(decoder->handle);
	ret = IMX_VPU_DEC_HANDLE_ERROR("could not flush decoder", dec_ret);

	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
		return ret;


	/* After the flush, any context will be thrown away */
	memset(decoder->context_for_frames, 0, sizeof(void*) * decoder->num_framebuffers);

	decoder->num_used_framebuffers = 0;


	return ret;
}


ImxVpuDecReturnCodes imx_vpu_dec_register_framebuffers(ImxVpuDecoder *decoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers)
{
	unsigned int i;
	ImxVpuDecReturnCodes ret;
	RetCode dec_ret;
	DecBufInfo buf_info;

	assert(decoder != NULL);
	assert(framebuffers != NULL);
	assert(num_framebuffers > 0);

	IMX_VPU_TRACE("attempting to register %u framebuffers", num_framebuffers);

	if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		if (decoder->internal_framebuffers != NULL)
			IMX_VPU_FREE(decoder->internal_framebuffers, sizeof(FrameBuffer) * decoder->num_framebuffers);
		if (decoder->context_for_frames != NULL)
			IMX_VPU_FREE(decoder->context_for_frames, sizeof(void*) * decoder->num_framebuffers);
		if (decoder->frame_modes != NULL)
			IMX_VPU_FREE(decoder->frame_modes, sizeof(FrameMode) * decoder->num_framebuffers);
	}
	else if (decoder->internal_framebuffers != NULL)
	{
		IMX_VPU_ERROR("other framebuffers have already been registered");
		return IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE;
	}


	/* Allocate memory for framebuffer structures and contexts */

	decoder->internal_framebuffers = IMX_VPU_ALLOC(sizeof(FrameBuffer) * num_framebuffers);
	if (decoder->internal_framebuffers == NULL)
	{
		IMX_VPU_ERROR("allocating memory for framebuffers failed");
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}

	decoder->context_for_frames = IMX_VPU_ALLOC(sizeof(void*) * num_framebuffers);
	if (decoder->context_for_frames == NULL)
	{
		IMX_VPU_ERROR("allocating memory for frame contexts failed");
		IMX_VPU_FREE(decoder->internal_framebuffers, sizeof(FrameBuffer) * num_framebuffers);
		decoder->internal_framebuffers = NULL;
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}

	decoder->frame_modes = IMX_VPU_ALLOC(sizeof(FrameMode) * num_framebuffers);
	if (decoder->frame_modes == NULL)
	{
		IMX_VPU_ERROR("allocating memory for frame context set flags failed");
		IMX_VPU_FREE(decoder->internal_framebuffers, sizeof(FrameBuffer) * num_framebuffers);
		IMX_VPU_FREE(decoder->context_for_frames, sizeof(void*) * num_framebuffers);
		decoder->internal_framebuffers = NULL;
		decoder->context_for_frames = NULL;
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}


	/* Copy the values from the framebuffers array to the internal_framebuffers
	 * one, which in turn will be used by the VPU (this will also map the buffers) */
	memset(decoder->internal_framebuffers, 0, sizeof(FrameBuffer) * num_framebuffers);
	for (i = 0; i < num_framebuffers; ++i)
	{
		imx_vpu_phys_addr_t phys_addr;
		ImxVpuFramebuffer *fb = &framebuffers[i];
		FrameBuffer *internal_fb = &(decoder->internal_framebuffers[i]);

		phys_addr = imx_vpu_dma_buffer_get_physical_address(fb->dma_buffer);
		if (phys_addr == 0)
		{
			IMX_VPU_ERROR("could not map buffer %u/%u", i, num_framebuffers);
			ret = IMX_VPU_DEC_RETURN_CODE_ERROR;
			goto cleanup;
		}

		/* In-place modifications in the framebuffers array */
		fb->already_marked = TRUE;
		fb->internal = (void*)i; /* Use the internal value to contain the index */

		internal_fb->strideY = fb->y_stride;
		internal_fb->strideC = fb->cbcr_stride;
		internal_fb->myIndex = i;
		internal_fb->bufY = (PhysicalAddress)(phys_addr + fb->y_offset);
		internal_fb->bufCb = (PhysicalAddress)(phys_addr + fb->cb_offset);
		internal_fb->bufCr = (PhysicalAddress)(phys_addr + fb->cr_offset);
		internal_fb->bufMvCol = (PhysicalAddress)(phys_addr + fb->mvcol_offset);
	}


	/* Initialize the extra AVC slice buf info; its DMA buffer backing store is
	 * located inside the bitstream buffer, right after the actual bitstream content */
	memset(&buf_info, 0, sizeof(buf_info));
	buf_info.avcSliceBufInfo.bufferBase = decoder->bitstream_buffer_physical_address + VPU_MAIN_BITSTREAM_BUFFER_SIZE;
	buf_info.avcSliceBufInfo.bufferSize = VPU_MAX_SLICE_BUFFER_SIZE;
	buf_info.vp8MbDataBufInfo.bufferBase = decoder->bitstream_buffer_physical_address + VPU_MAIN_BITSTREAM_BUFFER_SIZE;
	buf_info.vp8MbDataBufInfo.bufferSize = VPU_VP8_MB_PRED_BUFFER_SIZE;

	/* The actual registration */
	if (decoder->codec_format != IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		dec_ret = vpu_DecRegisterFrameBuffer(
			decoder->handle,
			decoder->internal_framebuffers,
			num_framebuffers,
			framebuffers[0].y_stride, /* The stride value is assumed to be the same for all framebuffers */
			&buf_info
		);
		ret = IMX_VPU_DEC_HANDLE_ERROR("could not register framebuffers", dec_ret);
		if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
			goto cleanup;
	}


	/* Set default rotator settings for motion JPEG */
	if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		/* the datatypes are int, but this is undocumented; determined by looking
		 * into the imx-vpu library's vpu_lib.c vpu_DecGiveCommand() definition */
		int rotation_angle = 0;
		int mirror = 0;
		int stride = framebuffers[0].y_stride;

		vpu_DecGiveCommand(decoder->handle, SET_ROTATION_ANGLE, (void *)(&rotation_angle));
		vpu_DecGiveCommand(decoder->handle, SET_MIRROR_DIRECTION,(void *)(&mirror));
		vpu_DecGiveCommand(decoder->handle, SET_ROTATOR_STRIDE, (void *)(&stride));
	}


	/* Store the pointer to the caller-supplied framebuffer array,
	 * and set the context pointers to their initial value (0) */
	decoder->framebuffers = framebuffers;
	decoder->num_framebuffers = num_framebuffers;
	memset(decoder->context_for_frames, 0, sizeof(void*) * num_framebuffers);
	for (i = 0; i < num_framebuffers; ++i)
		decoder->frame_modes[i] = FrameMode_Free;

	return IMX_VPU_DEC_RETURN_CODE_OK;

cleanup:
	IMX_VPU_FREE(decoder->internal_framebuffers, sizeof(FrameBuffer) * num_framebuffers);
	decoder->internal_framebuffers = NULL;

	return ret;
}


static ImxVpuDecReturnCodes imx_vpu_dec_get_initial_info_internal(ImxVpuDecoder *decoder)
{
	ImxVpuDecReturnCodes ret;
	RetCode dec_ret;

	assert(decoder != NULL);


	decoder->initial_info_available = FALSE;

	/* Set the force escape flag first (see section 4.3.2.2
	 * in the VPU documentation for an explanation why) */
	if (vpu_DecSetEscSeqInit(decoder->handle, 1) != RETCODE_SUCCESS)
	{
		IMX_VPU_ERROR("could not set force escape flag: invalid handle");
		return IMX_VPU_DEC_RETURN_CODE_ERROR;
	}

	/* The actual retrieval */
	dec_ret = vpu_DecGetInitialInfo(decoder->handle, &(decoder->initial_info));

	/* As recommended in section 4.3.2.2, clear the force
	 * escape flag immediately after retrieval is finished */
	vpu_DecSetEscSeqInit(decoder->handle, 0);

	ret = IMX_VPU_DEC_HANDLE_ERROR("could not retrieve configuration information", dec_ret);
	if (ret == IMX_VPU_DEC_RETURN_CODE_OK)
		decoder->initial_info_available = TRUE;

	return ret;
}


#define WRITE_16BIT_LE(BUF, OFS, VALUE) \
	do \
	{ \
		(BUF)[(OFS) + 0] = ((VALUE) >> 0) & 0xFF; \
		(BUF)[(OFS) + 1] = ((VALUE) >> 8) & 0xFF; \
	} \
	while (0)


#define WRITE_16BIT_LE_AND_INCR_IDX(BUF, IDX, VALUE) \
	do \
	{ \
		(BUF)[(IDX)++] = ((VALUE) >> 0) & 0xFF; \
		(BUF)[(IDX)++] = ((VALUE) >> 8) & 0xFF; \
	} \
	while (0)


#define WRITE_32BIT_LE(BUF, OFS, VALUE) \
	do \
	{ \
		(BUF)[(OFS) + 0] = ((VALUE) >> 0) & 0xFF; \
		(BUF)[(OFS) + 1] = ((VALUE) >> 8) & 0xFF; \
		(BUF)[(OFS) + 2] = ((VALUE) >> 16) & 0xFF; \
		(BUF)[(OFS) + 3] = ((VALUE) >> 24) & 0xFF; \
	} \
	while (0)


#define WRITE_32BIT_LE_AND_INCR_IDX(BUF, IDX, VALUE) \
	do \
	{ \
		(BUF)[(IDX)++] = ((VALUE) >> 0) & 0xFF; \
		(BUF)[(IDX)++] = ((VALUE) >> 8) & 0xFF; \
		(BUF)[(IDX)++] = ((VALUE) >> 16) & 0xFF; \
		(BUF)[(IDX)++] = ((VALUE) >> 24) & 0xFF; \
	} \
	while (0)


static void imx_vpu_dec_insert_vp8_ivf_main_header(uint8_t *header, unsigned int pic_width, unsigned int pic_height)
{
	int i = 0;
	/* At this point in time, these values are unknown, so just use defaults */
	uint32_t const fps_numerator = 1, fps_denominator = 1, num_frames = 0;

	/* DKIF signature */
	header[i++] = 'D';
	header[i++] = 'K';
	header[i++] = 'I';
	header[i++] = 'F';

	/* Version number (has to be 0) */
	WRITE_16BIT_LE_AND_INCR_IDX(header, i, 0);

	/* Size of the header, in bytes */
	WRITE_16BIT_LE_AND_INCR_IDX(header, i, VP8_SEQUENCE_HEADER_SIZE);

	/* Codec FourCC ("VP80") */
	header[i++] = 'V';
	header[i++] = 'P';
	header[i++] = '8';
	header[i++] = '0';

	/* Picture width and height, in pixels */
	WRITE_16BIT_LE_AND_INCR_IDX(header, i, pic_width);
	WRITE_16BIT_LE_AND_INCR_IDX(header, i, pic_height);

	/* Frame rate numerator and denominator */
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, fps_numerator);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, fps_denominator);

	/* Number of frames */
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, num_frames);

	/* Unused bytes */
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, 0);
}


static void imx_vpu_dec_insert_vp8_ivf_frame_header(uint8_t *header, uint32_t main_data_size, uint64_t pts)
{
	int i = 0;
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, main_data_size);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, (pts >> 0) & 0xFFFFFFFF);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, (pts >> 32) & 0xFFFFFFFF);
}


static void imx_vpu_dec_insert_wmv3_sequence_layer_header(uint8_t *header, unsigned int pic_width, unsigned int pic_height, unsigned int main_data_size, uint8_t *codec_data)
{
	/* Header as specified in the VC-1 specification, Annex J and L,
	 * L.2 , Sequence Layer */

	/* 0xFFFFFF is special value denoting an infinite sequence;
	 * since the number of frames isn't known at this point, use that */
	uint32_t const num_frames = 0xFFFFFF;
	/* XXX: the spec requires a constant 0xC5 , but the VPU needs 0x85 ; why? */
	uint32_t const struct_c_values = (0x85 << 24) | num_frames; /* 0xC5 is a constant as described in the spec */
	uint32_t const ext_header_length = 4;

	int i = 0;

	WRITE_32BIT_LE_AND_INCR_IDX(header, i, struct_c_values);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, ext_header_length);

	memcpy(&(header[i]), codec_data, 4);
	i += 4;

	WRITE_32BIT_LE_AND_INCR_IDX(header, i, pic_height);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, pic_width);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, main_data_size);
}


static void imx_vpu_dec_insert_wmv3_frame_layer_header(uint8_t *header, unsigned int main_data_size)
{
	/* Header as specified in the VC-1 specification, Annex J and L,
	 * L.3 , Frame Layer */
	WRITE_32BIT_LE(header, 0, main_data_size);
}


static void imx_vpu_dec_insert_vc1_frame_layer_header(uint8_t *header, uint8_t *main_data, unsigned int *actual_header_length)
{
	if (VC1_IS_NOT_NAL(main_data[0]))
	{
		/* Insert frame start code if necessary (note that it is
		 * written in little endian order; 0x0D is the last byte) */
		WRITE_32BIT_LE(header, 0, 0x0D010000);
		*actual_header_length = 4;
	}
	else
		*actual_header_length = 0;
}


static ImxVpuDecReturnCodes imx_vpu_dec_insert_frame_headers(ImxVpuDecoder *decoder, uint8_t *codec_data, unsigned int codec_data_size, uint8_t *main_data, unsigned int main_data_size)
{
	BOOL can_push_codec_data;
	ImxVpuDecReturnCodes ret = IMX_VPU_DEC_RETURN_CODE_OK;

	can_push_codec_data = (!(decoder->main_header_pushed) && (codec_data != NULL) && (codec_data_size > 0));

	switch (decoder->codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_WMV3:
		{
			/* Add RCV headers. RCV is a thin layer on
			 * top of WMV3 to make it ASF independent. */

			if (decoder->main_header_pushed)
			{
				uint8_t header[WMV3_RCV_FRAME_LAYER_SIZE];
				imx_vpu_dec_insert_wmv3_frame_layer_header(header, main_data_size);
				ret = imx_vpu_dec_push_input_data(decoder, header, WMV3_RCV_FRAME_LAYER_SIZE);
			}
			else
			{
				uint8_t header[WMV3_RCV_SEQUENCE_LAYER_SIZE];

				if (codec_data_size < 4)
				{
					IMX_VPU_ERROR("WMV3 input expects codec data size of 4 bytes, got %u bytes", codec_data_size);
					return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;
				}

				imx_vpu_dec_insert_wmv3_sequence_layer_header(header, decoder->picture_width, decoder->picture_height, main_data_size, codec_data);
				ret = imx_vpu_dec_push_input_data(decoder, header, WMV3_RCV_SEQUENCE_LAYER_SIZE);
				decoder->main_header_pushed = TRUE;
			}

			break;
		}

		case IMX_VPU_CODEC_FORMAT_WVC1:
		{
			if (!(decoder->main_header_pushed))
			{
				/* First, push the codec_data (except for its first byte,
				 * which contains the size of the codec data), since it
				 * contains the sequence layer header */
				if ((ret = imx_vpu_dec_push_input_data(decoder, codec_data + 1, codec_data_size - 1)) != IMX_VPU_DEC_RETURN_CODE_OK)
				{
					IMX_VPU_ERROR("could not push codec data to bitstream buffer");
					return ret;
				}

				decoder->main_header_pushed = TRUE;

				/* Next, the frame layer header will be pushed by the
				 * block below */
			}

			if (decoder->main_header_pushed)
			{
				uint8_t header[VC1_NAL_FRAME_LAYER_MAX_SIZE];
				unsigned int actual_header_length;
				imx_vpu_dec_insert_vc1_frame_layer_header(header, main_data, &actual_header_length);
				if (actual_header_length > 0)
					ret = imx_vpu_dec_push_input_data(decoder, header, actual_header_length);
			}

			break;
		}

		case IMX_VPU_CODEC_FORMAT_VP8:
		{
			/* VP8 does not need out-of-band codec data. However, some headers
			 * need to be inserted to contain it in an IVF stream, which the VPU needs. */
			// XXX the vpu wrapper has a special mode for "raw VP8 data". What is this?
			// Perhaps it means raw IVF-contained VP8?

			uint8_t header[VP8_SEQUENCE_HEADER_SIZE + VP8_FRAME_HEADER_SIZE];
			unsigned int header_size = 0;

			if (decoder->main_header_pushed)
			{
				imx_vpu_dec_insert_vp8_ivf_frame_header(&(header[0]), main_data_size, 0);
				header_size = VP8_FRAME_HEADER_SIZE;
			}
			else
			{
				imx_vpu_dec_insert_vp8_ivf_main_header(&(header[0]), decoder->picture_width, decoder->picture_height);
				imx_vpu_dec_insert_vp8_ivf_frame_header(&(header[VP8_SEQUENCE_HEADER_SIZE]), main_data_size, 0);
				header_size = VP8_SEQUENCE_HEADER_SIZE + VP8_FRAME_HEADER_SIZE;
				decoder->main_header_pushed = TRUE;
			}

			if (header_size != 0)
				ret = imx_vpu_dec_push_input_data(decoder, header, header_size);

			break;
		}

		default:
			if (can_push_codec_data)
			{
				ret = imx_vpu_dec_push_input_data(decoder, codec_data, codec_data_size);
				decoder->main_header_pushed = TRUE;
			}
	}

	return ret;
}


static ImxVpuDecReturnCodes imx_vpu_dec_push_input_data(ImxVpuDecoder *decoder, void *data, unsigned int data_size)
{
	PhysicalAddress read_ptr, write_ptr;
	Uint32 num_free_bytes;
	RetCode dec_ret;
	unsigned int read_offset, write_offset, num_free_bytes_at_end, num_bytes_to_push;
	size_t bbuf_size;
	int i;
	ImxVpuDecReturnCodes ret;

	assert(decoder != NULL);

	/* Only touch data within the first VPU_MAIN_BITSTREAM_BUFFER_SIZE bytes of the
	 * overall bitstream buffer, since the bytes beyond are reserved for slice and
	 * ps save data and/or VP8 data */
	bbuf_size = VPU_MAIN_BITSTREAM_BUFFER_SIZE;


	/* Get the current read and write position pointers in the bitstream buffer For
	 * decoding, the write_ptr is the interesting one. The read_ptr is just logged.
	 * These pointers are physical addresses. To get an offset value for the write
	 * position for example, one calculates:
	 * write_offset = (write_ptr - bitstream_buffer_physical_address) */
	dec_ret = vpu_DecGetBitstreamBuffer(decoder->handle, &read_ptr, &write_ptr, &num_free_bytes);
	ret = IMX_VPU_DEC_HANDLE_ERROR("could not retrieve bitstream buffer information", dec_ret);
	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
		return ret;
	IMX_VPU_TRACE("bitstream buffer status:  read ptr 0x%x  write ptr 0x%x  num free bytes %u", read_ptr, write_ptr, num_free_bytes);


	/* The bitstream buffer behaves like a ring buffer. This means that incoming data
	 * be written at once, if there is enough room at the current write position, or
	 * the write position may be near the end of the buffer, in which case two writes
	 * have to be performed (the first N bytes at the end of the buffer, and the remaining
	 * (bbuf_size - N) bytes at the beginning).
	 * Exception: motion JPEG data. With motion JPEG, the decoder operates in the line
	 * buffer mode. Meaning that the encoded JPEG frame is always placed at the beginning
	 * of the bitstream buffer. It does not have to work like a ring buffer, since with
	 * motion JPEG, one input frame immediately produces one decoded output frame. */
	if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
		write_offset = 0;
	else
		write_offset = write_ptr - decoder->bitstream_buffer_physical_address;

	num_free_bytes_at_end = bbuf_size - write_offset;

	read_offset = 0;

	/* This stores the number of bytes to push in the next immediate write operation
	 * If the write position is near the end of the buffer, not all bytes can be written
	 * at once, as described above */
	num_bytes_to_push = (num_free_bytes_at_end < data_size) ? num_free_bytes_at_end : data_size;

	/* Write the bytes to the bitstream buffer, either in one, or in two steps (see above) */
	for (i = 0; (i < 2) && (read_offset < data_size); ++i)
	{
		/* The actual write */
		uint8_t *src = ((uint8_t*)data) + read_offset;
		uint8_t *dest = ((uint8_t*)(decoder->bitstream_buffer_virtual_address)) + write_offset;
		memcpy(dest, src, num_bytes_to_push);

		/* Inform VPU about new data */
		dec_ret = vpu_DecUpdateBitstreamBuffer(decoder->handle, num_bytes_to_push);
		ret = IMX_VPU_DEC_HANDLE_ERROR("could not update bitstream buffer with new data", dec_ret);
		if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;

		/* Update offsets and write sizes */
		read_offset += num_bytes_to_push;
		write_offset += num_bytes_to_push;
		num_bytes_to_push = data_size - read_offset;

		/* Handle wrap-around if it occurs */
		if (write_offset >= bbuf_size)
			write_offset -= bbuf_size;
	}


	return IMX_VPU_DEC_RETURN_CODE_OK;
}


static int imx_vpu_dec_find_free_framebuffer(ImxVpuDecoder *decoder)
{
	unsigned int i;

	/* For motion JPEG, the user has to find a free framebuffer manually;
	 * the VPU does not do that in this case */

	for (i = 0; i < decoder->num_framebuffers; ++i)
	{
		if (decoder->frame_modes[i] == FrameMode_Free)
			return (int)i;
	}

	return -1;
}


ImxVpuDecReturnCodes imx_vpu_dec_decode(ImxVpuDecoder *decoder, ImxVpuEncodedFrame *encoded_frame, unsigned int *output_code)
{
	ImxVpuDecReturnCodes ret;
	unsigned int jpeg_width, jpeg_height;
	ImxVpuColorFormat jpeg_color_format;


	assert(decoder != NULL);
	assert(encoded_frame != NULL);
	assert(output_code != NULL);

	*output_code = 0;
	ret = IMX_VPU_DEC_RETURN_CODE_OK;


	IMX_VPU_TRACE("input info: %d byte", encoded_frame->data_size);


	/* Handle input data
	 * If in drain mode, signal EOS to decoder (if not already done)
	 * If not in drain mode, push input data and codec data to the decoder
	 * (the latter only once) */
	if (decoder->drain_mode_enabled)
	{
		/* Drain mode */

		if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
		{
			/* There is no real drain mode for motion JPEG, since there
			 * is nothing to drain (JPEG frames are never delayed - the
			 * VPU decodes them as soon as they arrive). However, the
			 * VPU also does not report an EOS. So, do this manually. */
			*output_code = IMX_VPU_DEC_OUTPUT_CODE_EOS;
			return IMX_VPU_DEC_RETURN_CODE_OK;
		}
		if (!(decoder->drain_eos_sent_to_vpu))
		{
			RetCode dec_ret;
			decoder->drain_eos_sent_to_vpu = TRUE;
			dec_ret = vpu_DecUpdateBitstreamBuffer(decoder->handle, 0);
			ret = IMX_VPU_DEC_HANDLE_ERROR("could not signal EOS to VPU", dec_ret);
			if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
				return ret;
		}
	}
	else
	{
		/* Regular mode */

		/* Insert any necessary extra frame headers */
		if ((ret = imx_vpu_dec_insert_frame_headers(decoder, encoded_frame->codec_data, encoded_frame->codec_data_size, encoded_frame->data.virtual_address, encoded_frame->data_size)) != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;

		/* Handle main frame data */
		if ((ret = imx_vpu_dec_push_input_data(decoder, encoded_frame->data.virtual_address, encoded_frame->data_size)) != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;
	}

	*output_code |= IMX_VPU_DEC_OUTPUT_CODE_INPUT_USED;


	if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		/* JPEGs are a special case
		 * The VPU does not report size or color format changes. Therefore,
		 * JPEG header have to be parsed here manually to retrieve the
		 * width, height, and color format and check if these changed.
		 * If so, invoke the initial_info_callback again.
		 */

		if (!imx_vpu_parse_jpeg_header(encoded_frame->data.virtual_address, encoded_frame->data_size, &jpeg_width, &jpeg_height, &jpeg_color_format))
		{
			IMX_VPU_ERROR("encoded frame is not valid JPEG data");
			return IMX_VPU_DEC_RETURN_CODE_ERROR;
		}

		IMX_VPU_LOG("JPEG frame information:  width: %u  height: %u  format: %s", jpeg_width, jpeg_height, imx_vpu_color_format_string(jpeg_color_format));

		if (decoder->initial_info_available && ((decoder->old_jpeg_width != jpeg_width) || (decoder->old_jpeg_height != jpeg_height) || (decoder->old_jpeg_color_format != jpeg_color_format)))
		{
			ImxVpuDecInitialInfo initial_info;
			initial_info.frame_width = jpeg_width;
			initial_info.frame_height = jpeg_height;
			initial_info.frame_rate_numerator = 0;
			initial_info.frame_rate_denominator = 1;
			initial_info.min_num_required_framebuffers = 1 + MIN_NUM_FREE_FB_REQUIRED;
			initial_info.color_format = jpeg_color_format;
			initial_info.interlacing = 0;
			initial_info.framebuffer_alignment = 1;

			/* Invoke the initial_info_callback. Framebuffers for decoding are allocated
			 * and registered there. */
			if (!decoder->initial_info_callback(decoder, &initial_info, *output_code, decoder->callback_user_data))
			{
				IMX_VPU_ERROR("initial info callback reported failure - cannot continue");
				return IMX_VPU_DEC_RETURN_CODE_ERROR;
			}
		}

		decoder->old_jpeg_width = jpeg_width;
		decoder->old_jpeg_height = jpeg_height;
		decoder->old_jpeg_color_format = jpeg_color_format;
	}


	/* Start decoding process */

	if (!(decoder->initial_info_available))
	{
		ImxVpuDecInitialInfo initial_info;

		/* Initial info is not available yet. Fetch it, and store it
		 * inside the decoder instance structure. */
		ret = imx_vpu_dec_get_initial_info_internal(decoder);
		switch (ret)
		{
			case IMX_VPU_DEC_RETURN_CODE_OK:
				break;

			case IMX_VPU_DEC_RETURN_CODE_INVALID_HANDLE:
				return IMX_VPU_DEC_RETURN_CODE_INVALID_HANDLE;

			case IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS:
				/* if this error occurs, something inside this code is wrong; this is no user error */
				IMX_VPU_ERROR("Internal error: invalid info structure while retrieving initial info");
				return IMX_VPU_DEC_RETURN_CODE_ERROR;

			case IMX_VPU_DEC_RETURN_CODE_TIMEOUT:
				IMX_VPU_ERROR("VPU reported timeout while retrieving initial info");
				return IMX_VPU_DEC_RETURN_CODE_TIMEOUT;

			case IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE:
				 return IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE;

			case IMX_VPU_DEC_RETURN_CODE_ALREADY_CALLED:
				IMX_VPU_ERROR("Initial info was already retrieved - duplicate call");
				return IMX_VPU_DEC_RETURN_CODE_ALREADY_CALLED;

			default:
				/* do not report error; instead, let the caller supply the
				 * VPU with more data, until initial info can be retrieved */
				*output_code |= IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA;
		}

		initial_info.frame_width = decoder->initial_info.picWidth;
		initial_info.frame_height = decoder->initial_info.picHeight;
		initial_info.frame_rate_numerator = decoder->initial_info.frameRateRes;
		initial_info.frame_rate_denominator = decoder->initial_info.frameRateDiv;
		initial_info.min_num_required_framebuffers = decoder->initial_info.minFrameBufferCount + MIN_NUM_FREE_FB_REQUIRED;
		initial_info.interlacing = decoder->initial_info.interlace ? 1 : 0;
		initial_info.framebuffer_alignment = 1; /* for maptype 0 (linear, non-tiling) */

		/* Make sure that at least one framebuffer is allocated and registered
		 * (Also for motion JPEG, even though the VPU doesn't use framebuffers then) */
		if (initial_info.min_num_required_framebuffers < 1)
			initial_info.min_num_required_framebuffers = 1;

		if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
		{
			if (initial_info.frame_width == 0)
				initial_info.frame_width = jpeg_width;
			if (initial_info.frame_height == 0)
				initial_info.frame_height = jpeg_height;
		}

		switch (decoder->initial_info.mjpg_sourceFormat)
		{
			case FORMAT_420:
				initial_info.color_format = IMX_VPU_COLOR_FORMAT_YUV420;
				break;
			case FORMAT_422:
				initial_info.color_format = IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL;
				break;
			case FORMAT_224:
				initial_info.color_format = IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL;
				break;
			case FORMAT_444:
				initial_info.color_format = IMX_VPU_COLOR_FORMAT_YUV444;
				break;
			case FORMAT_400:
				initial_info.color_format = IMX_VPU_COLOR_FORMAT_YUV400;
				break;
			default:
				IMX_VPU_ERROR("unknown source color format value %d", decoder->initial_info.mjpg_sourceFormat);
				return IMX_VPU_DEC_RETURN_CODE_ERROR;
		}

		/* Invoke the initial_info_callback. Framebuffers for decoding are allocated
		 * and registered there. */
		if (!decoder->initial_info_callback(decoder, &initial_info, *output_code, decoder->callback_user_data))
		{
			IMX_VPU_ERROR("initial info callback reported failure - cannot continue");
			return IMX_VPU_DEC_RETURN_CODE_ERROR;
		}
	}

	{
		RetCode dec_ret;
		DecParam params;
		BOOL timeout;
		int jpeg_frame_idx = -1;

		memset(&params, 0, sizeof(params));

		if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
		{
			/* There is an error in the specification. It states that chunkSize
			 * is not used in the i.MX6. This is untrue; for motion JPEG, this
			 * must be nonzero. */
			params.chunkSize = encoded_frame->data_size;

			/* Set the virtual and physical memory pointers that point to the
			 * start of the frame. These always point to the beginning of the
			 * bitstream buffer, because the VPU operates in line buffer mode
			 * when decoding motion JPEG data. */
			params.virtJpgChunkBase = (unsigned char *)(decoder->bitstream_buffer_virtual_address);
			params.phyJpgChunkBase = decoder->bitstream_buffer_physical_address;

			/* The framebuffer array isn't used when decoding motion JPEG data.
			 * Instead, the user has to manually specify a framebuffer for the
			 * output by sending the SET_ROTATOR_OUTPUT command. */
			jpeg_frame_idx = imx_vpu_dec_find_free_framebuffer(decoder);
			if (jpeg_frame_idx != -1)
				vpu_DecGiveCommand(decoder->handle, SET_ROTATOR_OUTPUT, (void *)(&(decoder->internal_framebuffers[jpeg_frame_idx])));
			else
			{
				IMX_VPU_ERROR("could not find free framebuffer for MJPEG output");
				return IMX_VPU_DEC_RETURN_CODE_ERROR;
			}
		}

		/* XXX: currently, iframe search and skip frame modes are not supported */


		/* Start frame decoding */
		dec_ret = vpu_DecStartOneFrame(decoder->handle, &params);

		if (dec_ret == RETCODE_JPEG_BIT_EMPTY)
		{
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA;
			return IMX_VPU_DEC_RETURN_CODE_OK;
		}
		else if (dec_ret == RETCODE_JPEG_EOS)
		{
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_EOS;
			dec_ret = RETCODE_SUCCESS;
		}

		if ((ret = IMX_VPU_DEC_HANDLE_ERROR("could not decode frame", dec_ret)) != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;


		/* Wait for frame completion */
		{
			int cnt;
			timeout = TRUE;
			for (cnt = 0; cnt < VPU_MAX_TIMEOUT_COUNTS; ++cnt)
			{
				if (vpu_WaitForInt(VPU_WAIT_TIMEOUT) != RETCODE_SUCCESS)
				{
					IMX_VPU_INFO("timeout after waiting %d ms for frame completion", VPU_WAIT_TIMEOUT);
				}
				else
				{
					timeout = FALSE;
					break;
				}
			}
		}


		/* Retrieve information about the result of the decode process There may be no
		 * decoded frame yet though; this only finishes processing the input frame. In
		 * case of formats like h.264, it may take several input frames until output
		 * frames start coming out. However, the output information does contain valuable
		 * data even at the beginning, like which framebuffer in the framebuffer array
		 * is used for decoding the frame into.
		 *
		 * Also, vpu_DecGetOutputInfo() is called even if a timeout occurred. This is
		 * intentional, since according to the VPU docs, vpu_DecStartOneFrame() won't be
		 * usable again until vpu_DecGetOutputInfo() is called. In other words, the
		 * vpu_DecStartOneFrame() locks down some internals inside the VPU, and
		 * vpu_DecGetOutputInfo() releases them. */

		dec_ret = vpu_DecGetOutputInfo(decoder->handle, &(decoder->dec_output_info));
		ret = IMX_VPU_DEC_HANDLE_ERROR("could not get output information", dec_ret);
		if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;


		if (timeout)
			return IMX_VPU_DEC_RETURN_CODE_TIMEOUT;


		/* Log some information about the decoded frame */
		IMX_VPU_TRACE("output info:  indexFrameDisplay %d  indexFrameDecoded %d  NumDecFrameBuf %d  picType %d  numOfErrMBs %d  hScaleFlag %d  vScaleFlag %d  notSufficientPsBuffer %d  notSufficientSliceBuffer %d  decodingSuccess %d  interlacedFrame %d  mp4PackedPBframe %d  h264Npf %d  pictureStructure %d  topFieldFirst %d  repeatFirstField %d  fieldSequence %d  decPicWidth %d  decPicHeight %d",
			decoder->dec_output_info.indexFrameDisplay,
			decoder->dec_output_info.indexFrameDecoded,
			decoder->dec_output_info.NumDecFrameBuf,
			decoder->dec_output_info.picType,
			decoder->dec_output_info.numOfErrMBs,
			decoder->dec_output_info.hScaleFlag,
			decoder->dec_output_info.vScaleFlag,
			decoder->dec_output_info.notSufficientPsBuffer,
			decoder->dec_output_info.notSufficientSliceBuffer,
			decoder->dec_output_info.decodingSuccess,
			decoder->dec_output_info.interlacedFrame,
			decoder->dec_output_info.mp4PackedPBframe,
			decoder->dec_output_info.h264Npf,
			decoder->dec_output_info.pictureStructure,
			decoder->dec_output_info.topFieldFirst,
			decoder->dec_output_info.repeatFirstField,
			decoder->dec_output_info.fieldSequence,
			decoder->dec_output_info.decPicWidth,
			decoder->dec_output_info.decPicHeight
		);


		/* VP8 requires some workarounds */
		if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_VP8)
		{
			if ((decoder->dec_output_info.indexFrameDecoded >= 0) && (decoder->dec_output_info.indexFrameDisplay == VPU_DECODER_DISPLAYIDX_NO_PICTURE_TO_DISPLAY))
			{
				/* Internal invisible frames are supposed to be used for decoding only,
				 * so don't output it, and drop it instead; to that end, set the index
				 * values to resemble indices used for dropped frames to make sure the
				 * dropped frames block below thinks this frame got dropped by the VPU */
				IMX_VPU_DEBUG("skip internal invisible frame for VP8");
				decoder->dec_output_info.indexFrameDecoded = VPU_DECODER_DECODEIDX_FRAME_NOT_DECODED;
				decoder->dec_output_info.indexFrameDisplay = VPU_DECODER_DISPLAYIDX_NO_PICTURE_TO_DISPLAY;
			}
		}

		/* Motion JPEG requires frame index adjustments */
		if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
		{
			IMX_VPU_DEBUG("MJPEG data -> adjust indexFrameDisplay and indexFrameDecoded values to %d", jpeg_frame_idx);
			decoder->dec_output_info.indexFrameDecoded = jpeg_frame_idx;
			decoder->dec_output_info.indexFrameDisplay = jpeg_frame_idx;
		}

		/* Report dropped frames */
		if (
		  (decoder->dec_output_info.indexFrameDecoded == VPU_DECODER_DECODEIDX_FRAME_NOT_DECODED) &&
		  (
		    (decoder->dec_output_info.indexFrameDisplay == VPU_DECODER_DISPLAYIDX_NO_PICTURE_TO_DISPLAY) ||
		    (decoder->dec_output_info.indexFrameDisplay == VPU_DECODER_DISPLAYIDX_SKIP_MODE_NO_PICTURE_TO_DISPLAY)
		  )
		)
		{
			IMX_VPU_DEBUG("frame got dropped (context: %p)", encoded_frame->context);
			decoder->dropped_frame_context = encoded_frame->context;
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_DROPPED;
		}

		/* Check if information about the decoded frame is available.
		 * In particular, the index of the framebuffer where the frame is being
		 * decoded into is essential with formats like h.264, which allow for both
		 * delays between decoding and presentation, and reordering of frames.
		 * With the indexFrameDecoded value, it is possible to know which framebuffer
		 * is associated with what input buffer. This is necessary to properly
		 * associate context information which can later be retrieved again when a
		 * frame can be displayed.
		 * indexFrameDecoded can be negative, meaning there is no frame currently being
		 * decoded. This typically happens when the drain mode is enabled, since then,
		 * there will be no more input data. */

		if (decoder->dec_output_info.indexFrameDecoded >= 0)
		{
			int idx_decoded = decoder->dec_output_info.indexFrameDecoded;
			assert(idx_decoded < (int)(decoder->num_framebuffers));

			decoder->context_for_frames[idx_decoded] = encoded_frame->context;
			decoder->frame_modes[idx_decoded] = FrameMode_ReservedForDecoding;

			decoder->num_used_framebuffers++;			
		}


		/* Check if information about a displayable picture is available.
		 * A frame can be presented when it is fully decoded. In that case,
		 * indexFrameDisplay is >= 0. If no fully decoded and displayable
		 * frame exists (yet), indexFrameDisplay is -2 or -3 (depending on the
		 * currently enabled frame skip mode). If indexFrameDisplay is -1,
		 * all pictures have been decoded. This typically happens after drain
		 * mode was enabled.
		 * This index is later used to retrieve the context that was associated
		 * with the input data that corresponds to the decoded and displayable
		 * picture (see above). available_decoded_pic_idx stores the index for
		 * this precise purpose. Also see imx_vpu_dec_get_decoded_picture(). */

		if (decoder->dec_output_info.indexFrameDisplay >= 0)
		{
			int idx_display = decoder->dec_output_info.indexFrameDisplay;
			assert(idx_display < (int)(decoder->num_framebuffers));

			IMX_VPU_TRACE("Decoded and displayable picture available (framebuffer display index: %d  context: %p)", idx_display, decoder->context_for_frames[idx_display]);

			decoder->frame_modes[idx_display] = FrameMode_ContainsDisplayablePicture;

			decoder->available_decoded_pic_idx = idx_display;
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE;
		}
		else if (decoder->dec_output_info.indexFrameDisplay == VPU_DECODER_DISPLAYIDX_ALL_PICTURED_DISPLAYED)
		{
			IMX_VPU_TRACE("EOS reached");
			decoder->available_decoded_pic_idx = -1;
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_EOS;
		}
		else
		{
			IMX_VPU_TRACE("Nothing yet to display ; indexFrameDisplay: %d", decoder->dec_output_info.indexFrameDisplay);
		}

	}


	return ret;
}


ImxVpuDecReturnCodes imx_vpu_dec_get_decoded_picture(ImxVpuDecoder *decoder, ImxVpuPicture *decoded_picture)
{
	int idx;

	assert(decoder != NULL);
	assert(decoded_picture != NULL);


	/* available_decoded_pic_idx < 0 means there is no picture
	 * to retrieve yet, or the picture was already retrieved */
	if (decoder->available_decoded_pic_idx < 0)
	{
		IMX_VPU_ERROR("no decoded picture available");
		return IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE;
	}


	idx = decoder->available_decoded_pic_idx;
	assert(idx < (int)(decoder->num_framebuffers));


	/* retrieve the framebuffer at the given index, and set its already_marked flag
	 * to FALSE, since it contains a fully decoded and still undisplayed framebuffer */
	decoded_picture->framebuffer = &(decoder->framebuffers[idx]);
	decoded_picture->framebuffer->already_marked = FALSE;
	decoded_picture->pic_type = convert_pic_type(decoder->codec_format, decoder->dec_output_info.picType);
	decoded_picture->field_type = convert_field_type(decoder->codec_format, &(decoder->dec_output_info));
	decoded_picture->context = decoder->context_for_frames[idx];


	/* erase the context from context_for_frames after retrieval, and set
	 * available_decoded_pic_idx to -1 ; this ensures no erroneous
	 * double-retrieval can occur */
	decoder->context_for_frames[idx] = NULL;
	decoder->available_decoded_pic_idx = -1;


	return IMX_VPU_DEC_RETURN_CODE_OK;
}


void* imx_vpu_dec_get_dropped_frame_context(ImxVpuDecoder *decoder)
{
	return decoder->dropped_frame_context;
}


int imx_vpu_dec_check_if_can_decode(ImxVpuDecoder *decoder)
{
	assert(decoder != NULL);
	int num_free_framebuffers = decoder->num_framebuffers - decoder->num_used_framebuffers;
	return num_free_framebuffers >= MIN_NUM_FREE_FB_REQUIRED;
}


ImxVpuDecReturnCodes imx_vpu_dec_mark_framebuffer_as_displayed(ImxVpuDecoder *decoder, ImxVpuFramebuffer *framebuffer)
{
	ImxVpuDecReturnCodes ret;
	RetCode dec_ret;
	int idx;

	assert(decoder != NULL);
	assert(framebuffer != NULL);


	/* don't do anything if the framebuffer has already been marked
	 * this ensures the num_used_framebuffers counter remains valid
	 * even if this function is called for the same framebuffer twice */
	if (framebuffer->already_marked)
	{
		IMX_VPU_ERROR("framebuffer has already been marked as displayed");
		return IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS;
	}


	/* the index into the framebuffer array is stored in the "internal" field */
	idx = (int)(framebuffer->internal);
	assert(idx < (int)(decoder->num_framebuffers));


	/* frame is no longer being used */
	decoder->frame_modes[idx] = FrameMode_Free;


	/* mark it as displayed in the VPU */
	if (decoder->codec_format != IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		dec_ret = vpu_DecClrDispFlag(decoder->handle, idx);
		ret = IMX_VPU_DEC_HANDLE_ERROR("could not mark framebuffer as displayed", dec_ret);

		if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;
	}


	/* set the already_marked flag to inform the rest of the imxvpuapi
	 * decoder instance that the framebuffer isn't occupied anymore,
	 * and count down num_used_framebuffers to reflect that fact */
	framebuffer->already_marked = TRUE;
	decoder->num_used_framebuffers--;


	return IMX_VPU_DEC_RETURN_CODE_OK;
}




/************************************************/
/******* ENCODER STRUCTURES AND FUNCTIONS *******/
/************************************************/


char const * imx_vpu_enc_error_string(ImxVpuEncReturnCodes code)
{
	switch (code)
	{
		case IMX_VPU_ENC_RETURN_CODE_OK:                        return "ok";
		case IMX_VPU_ENC_RETURN_CODE_ERROR:                     return "unspecified error";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS:            return "invalid params";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_HANDLE:            return "invalid handle";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_FRAMEBUFFER:       return "invalid framebuffer";
		case IMX_VPU_ENC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS: return "insufficient framebuffers";
		case IMX_VPU_ENC_RETURN_CODE_INVALID_STRIDE:            return "invalid stride";
		case IMX_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE:       return "wrong call sequence";
		case IMX_VPU_ENC_RETURN_CODE_TIMEOUT:                   return "timeout";
		default: return "<unknown>";
	}
}


ImxVpuEncReturnCodes imx_vpu_enc_load(void)
{
	return imx_vpu_load() ? IMX_VPU_ENC_RETURN_CODE_OK : IMX_VPU_ENC_RETURN_CODE_ERROR;
}


ImxVpuEncReturnCodes imx_vpu_enc_unload(void)
{
	return imx_vpu_unload() ? IMX_VPU_ENC_RETURN_CODE_OK : IMX_VPU_ENC_RETURN_CODE_ERROR;
}


ImxVpuDMABufferAllocator* imx_vpu_enc_get_default_allocator(void)
{
	return (ImxVpuDMABufferAllocator*)(&default_dma_buffer_allocator);
}


void imx_vpu_enc_get_bitstream_buffer_info(size_t *size, unsigned int *alignment)
{
}


void imx_vpu_enc_set_default_open_params(ImxVpuCodecFormat codec_format, ImxVpuEncOpenParams *open_params)
{
}


ImxVpuEncReturnCodes imx_vpu_enc_open(ImxVpuEncoder **encoder, ImxVpuEncOpenParams *open_params, ImxVpuDMABuffer *bitstream_buffer)
{
}


ImxVpuEncReturnCodes imx_vpu_enc_close(ImxVpuEncoder *encoder)
{
}


ImxVpuDMABuffer* imx_vpu_enc_get_bitstream_buffer(ImxVpuEncoder *encoder)
{
}


ImxVpuEncReturnCodes imx_vpu_enc_register_framebuffers(ImxVpuEncoder *encoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers)
{
}


ImxVpuEncReturnCodes imx_vpu_enc_get_initial_info(ImxVpuEncoder *encoder, ImxVpuEncInitialInfo *info)
{
}


void imx_vpu_enc_set_default_encoding_params(ImxVpuEncoder *encoder, ImxVpuEncParams *encoding_params)
{
}


void imx_vpu_enc_configure_bitrate(ImxVpuEncoder *encoder, unsigned int bitrate)
{
}


void imx_vpu_enc_configure_min_intra_refresh(ImxVpuEncoder *encoder, unsigned int min_intra_refresh_num)
{
}


void imx_vpu_enc_configure_intra_qp(ImxVpuEncoder *encoder, int intra_qp)
{
}


ImxVpuEncReturnCodes imx_vpu_enc_encode(ImxVpuEncoder *encoder, ImxVpuPicture *picture, ImxVpuEncodedFrame *encoded_frame, ImxVpuEncParams *encoding_params, unsigned int *output_code)
{
}







