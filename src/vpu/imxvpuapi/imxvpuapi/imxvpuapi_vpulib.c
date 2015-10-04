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
#define VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE (1024*1024*3)
#define VPU_ENC_MAIN_BITSTREAM_BUFFER_SIZE (1024*1024*1)
#define VPU_ENC_MPEG4_SCRATCH_SIZE         0x080000
#define VPU_MAX_SLICE_BUFFER_SIZE          (1920*1088*15/20)
#define VPU_PS_SAVE_BUFFER_SIZE            (1024*512)
#define VPU_VP8_MB_PRED_BUFFER_SIZE        (68*(1920*1088/256))

/* The decoder's bitstream buffer shares space with other fields,
 * to not have to allocate several DMA blocks. The actual bitstream buffer is called
 * the "main bitstream buffer". It makes up all bytes from the start of the buffer
 * and is VPU_MAIN_BITSTREAM_BUFFER_SIZE large. Bytes beyond that are codec format
 * specific data. */
#define VPU_DEC_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE  (VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE + VPU_MAX_SLICE_BUFFER_SIZE + VPU_PS_SAVE_BUFFER_SIZE)

#define VPU_ENC_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE  (VPU_ENC_MAIN_BITSTREAM_BUFFER_SIZE + VPU_ENC_MPEG4_SCRATCH_SIZE)

#define VPU_ENC_NUM_EXTRA_SUBSAMPLE_FRAMEBUFFERS  2

#define VP8_SEQUENCE_HEADER_SIZE  32
#define VP8_FRAME_HEADER_SIZE     12

#define WMV3_RCV_SEQUENCE_LAYER_SIZE (6 * 4)
#define WMV3_RCV_FRAME_LAYER_SIZE    4

#define VC1_NAL_FRAME_LAYER_MAX_SIZE   4

#define VPU_WAIT_TIMEOUT             500 /* milliseconds to wait for frame completion */
#define VPU_MAX_TIMEOUT_COUNTS       4   /* how many timeouts are allowed in series */


static uint8_t const mjpeg_enc_component_info_tables[5][4 * 6] =
{
	{ 0x00, 0x02, 0x02, 0x00, 0x00, 0x00,
	  0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x03, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* YUV 4:2:0 */

	{ 0x00, 0x02, 0x01, 0x00, 0x00, 0x00,
	  0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x03, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* YUV 4:2:2 horizontal */

	{ 0x00, 0x01, 0x02, 0x00, 0x00, 0x00,
	  0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x03, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* YUV 4:2:2 vertical */

	{ 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
	  0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
	  0x03, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* YUV 4:4:4 */

	{ 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
	  0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x03, 0x00, 0x00, 0x00, 0x00, 0x00 }  /* YUV 4:0:0 */
};


/* These quantization tables are from the JPEG specification, section K.1 */


static uint8_t const mjpeg_enc_quantization_luma[64] =
{
	16,  11,  10,  16,  24,  40,  51,  61,
	12,  12,  14,  19,  26,  58,  60,  55,
	14,  13,  16,  24,  40,  57,  69,  56,
	14,  17,  22,  29,  51,  87,  80,  62,
	18,  22,  37,  56,  68, 109, 103,  77,
	24,  35,  55,  64,  81, 104, 113,  92,
	49,  64,  78,  87, 103, 121, 120, 101,
	72,  92,  95,  98, 112, 100, 103,  99
};


static uint8_t const mjpeg_enc_quantization_chroma[64] =
{
	17,  18,  24,  47,  99,  99,  99,  99,
	18,  21,  26,  66,  99,  99,  99,  99,
	24,  26,  56,  99,  99,  99,  99,  99,
	47,  66,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99,
	99,  99,  99,  99,  99,  99,  99,  99
};


/* These Huffman tables correspond to the default tables inside the VPU library */


static uint8_t const mjpeg_enc_huffman_bits_luma_dc[16] =
{
	0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


static uint8_t const mjpeg_enc_huffman_bits_luma_ac[16] =
{
	0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
	0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D
};


static uint8_t const mjpeg_enc_huffman_bits_chroma_dc[16] =
{
	0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};


static uint8_t const mjpeg_enc_huffman_bits_chroma_ac[16] =
{
	0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
	0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77
};


static uint8_t const mjpeg_enc_huffman_value_luma_dc[12] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B
};


static uint8_t const mjpeg_enc_huffman_value_luma_ac[162] =
{
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
	0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
	0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
	0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
	0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
	0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
	0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
	0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
	0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
	0xF9, 0xFA
};


static uint8_t const mjpeg_enc_huffman_value_chroma_dc[12] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B
};


static uint8_t const mjpeg_enc_huffman_value_chroma_ac[162] =
{
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
	0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
	0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
	0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
	0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
	0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
	0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
	0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
	0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
	0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
	0xF9, 0xFA
};






static unsigned long vpu_init_inst_counter = 0;


static BOOL imx_vpu_load(void)
{
	IMX_VPU_LOG("VPU init instance counter: %lu", vpu_init_inst_counter);

	if (vpu_init_inst_counter != 0)
	{
		++vpu_init_inst_counter;
		return TRUE;
	}
	else
	{
		if (vpu_Init(NULL) == RETCODE_SUCCESS)
		{
			IMX_VPU_DEBUG("loaded VPU");
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
	IMX_VPU_LOG("VPU init instance counter: %lu", vpu_init_inst_counter);

	if (vpu_init_inst_counter != 0)
	{
		--vpu_init_inst_counter;

		if (vpu_init_inst_counter == 0)
		{
			vpu_UnInit();
			IMX_VPU_DEBUG("unloaded VPU");
		}
	}

	return TRUE;
}


static void convert_pic_type(ImxVpuCodecFormat codec_format, int vpu_pic_type, BOOL interlaced, ImxVpuPicType *pic_types)
{
	ImxVpuPicType type = IMX_VPU_PIC_TYPE_UNKNOWN;

	switch (codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_WMV3:
			/* This assumes progressive content and sets both picture
			 * types to the same value. WMV3 *does* have support for
			 * interlacing, but it has never been documented, and was
			 * deprecated by Microsoft in favor of VC-1, which officially
			 * has proper interlacing support. */
			switch (vpu_pic_type & 0x07)
			{
				case 0: type = IMX_VPU_PIC_TYPE_I; break;
				case 1: type = IMX_VPU_PIC_TYPE_P; break;
				case 2: type = IMX_VPU_PIC_TYPE_BI; break;
				case 3: type = IMX_VPU_PIC_TYPE_B; break;
				case 4: type = IMX_VPU_PIC_TYPE_SKIP; break;
				default: break;
			}
			pic_types[0] = pic_types[1] = type;
			break;

		case IMX_VPU_CODEC_FORMAT_WVC1:
		{
			int i;
			int vpu_pic_types[2];

			if (interlaced)
			{
				vpu_pic_types[0] = (vpu_pic_type >> 0) & 0x7;
				vpu_pic_types[1] = (vpu_pic_type >> 3) & 0x7;
			}
			else
			{
				vpu_pic_types[0] = (vpu_pic_type >> 0) & 0x7;
				vpu_pic_types[1] = (vpu_pic_type >> 0) & 0x7;
			}

			for (i = 0; i < 2; ++i)
			{
				switch (vpu_pic_types[i])
				{
					case 0: pic_types[i] = IMX_VPU_PIC_TYPE_I; break;
					case 1: pic_types[i] = IMX_VPU_PIC_TYPE_P; break;
					case 2: pic_types[i] = IMX_VPU_PIC_TYPE_BI; break;
					case 3: pic_types[i] = IMX_VPU_PIC_TYPE_B; break;
					case 4: pic_types[i] = IMX_VPU_PIC_TYPE_SKIP; break;
					default: pic_types[i] = IMX_VPU_PIC_TYPE_UNKNOWN;
				}
			}

			break;
		}

		/* XXX: the VPU documentation indicates that picType's bit #0 is
		 * cleared if it is an IDR picture, and set if it is non-IDR, and
		 * the bits 1..3 indicate if this is an I, P, or B picture.
		 * However, tests show this to be untrue. picType actually conforms
		 * to the default case below for h.264 content as well. */

		default:
			switch (vpu_pic_type)
			{
				case 0: type = IMX_VPU_PIC_TYPE_I; break;
				case 1: type = IMX_VPU_PIC_TYPE_P; break;
				case 2: case 3: type = IMX_VPU_PIC_TYPE_B; break;
				default: break;
			}
			pic_types[0] = pic_types[1] = type;
	}
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
		IMX_VPU_DEBUG("allocated %d bytes of physical memory", size);

	if (IOGetVirtMem(&(dmabuffer->mem_desc)) == RETCODE_FAILURE)
	{
		IOFreePhyMem(&(dmabuffer->mem_desc));
		IMX_VPU_FREE(dmabuffer, sizeof(DefaultDMABuffer));
		IMX_VPU_ERROR("retrieving virtual address for physical memory failed");
		return NULL;
	}
	else
		IMX_VPU_DEBUG("retrieved virtual address for physical memory");

	dmabuffer->aligned_virtual_address = (uint8_t *)IMX_VPU_ALIGN_VAL_TO((uint8_t *)(dmabuffer->mem_desc.virt_uaddr), alignment);
	dmabuffer->aligned_physical_address = (imx_vpu_phys_addr_t)IMX_VPU_ALIGN_VAL_TO((imx_vpu_phys_addr_t)(dmabuffer->mem_desc.phy_addr), alignment);

	IMX_VPU_DEBUG("virtual address:  0x%x  aligned: %p", dmabuffer->mem_desc.virt_uaddr, dmabuffer->aligned_virtual_address);
	IMX_VPU_DEBUG("physical address: 0x%x  aligned: %" IMX_VPU_PHYS_ADDR_FORMAT, dmabuffer->mem_desc.phy_addr, dmabuffer->aligned_physical_address);

	return (ImxVpuDMABuffer *)dmabuffer;
}


static void default_dmabufalloc_deallocate(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);

	DefaultDMABuffer *defaultbuf = (DefaultDMABuffer *)buffer;

	if (IOFreePhyMem(&(defaultbuf->mem_desc)) != 0)
		IMX_VPU_ERROR("deallocating %d bytes of physical memory failed", defaultbuf->size);
	else
		IMX_VPU_DEBUG("deallocated %d bytes of physical memory", defaultbuf->size);

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
		case IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL:
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


typedef struct
{
	void *context;
	ImxVpuPicType pic_types[2];
	ImxVpuFieldType field_type;
	FrameMode mode;
}
ImxVpuDecFrameEntry;


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
	/* internal_framebuffers and framebuffers are separate from
	 * frame_entries: internal_framebuffers must be given directly
	 * to the vpu_DecRegisterFrameBuffer() function, and framebuffers
	 * is a user-supplied input value */
	FrameBuffer *internal_framebuffers;
	ImxVpuFramebuffer *framebuffers;
	ImxVpuDecFrameEntry *frame_entries;
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
static ImxVpuDecReturnCodes imx_vpu_dec_get_initial_info(ImxVpuDecoder *decoder);

static void imx_vpu_dec_insert_vp8_ivf_main_header(uint8_t *header, unsigned int pic_width, unsigned int pic_height);
static void imx_vpu_dec_insert_vp8_ivf_frame_header(uint8_t *header, size_t main_data_size, uint64_t pts);

static void imx_vpu_dec_insert_wmv3_sequence_layer_header(uint8_t *header, unsigned int pic_width, unsigned int pic_height, size_t main_data_size, uint8_t *codec_data);
static void imx_vpu_dec_insert_wmv3_frame_layer_header(uint8_t *header, size_t main_data_size);

static void imx_vpu_dec_insert_vc1_frame_layer_header(uint8_t *header, uint8_t *main_data, size_t *actual_header_length);

static ImxVpuDecReturnCodes imx_vpu_dec_insert_frame_headers(ImxVpuDecoder *decoder, uint8_t *codec_data, size_t codec_data_size, uint8_t *main_data, size_t main_data_size);

static ImxVpuDecReturnCodes imx_vpu_dec_push_input_data(ImxVpuDecoder *decoder, void *data, size_t data_size);

static int imx_vpu_dec_find_free_framebuffer(ImxVpuDecoder *decoder);

static void imx_vpu_dec_free_internal_arrays(ImxVpuDecoder *decoder);


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
	/* The VP8 prediction buffer and the h.264 slice buffer & SPS/PPS (PS) buffer
	 * share the same memory space, since the decoder does not use them both
	 * at the same time. Check that the sizes are correct (slice & SPS/PPS buffer
	 * sizes must together be larger than the VP8 prediction buffer size). */
	assert(VPU_VP8_MB_PRED_BUFFER_SIZE < (VPU_MAX_SLICE_BUFFER_SIZE + VPU_PS_SAVE_BUFFER_SIZE));
	*size = VPU_DEC_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE;
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


	IMX_VPU_DEBUG("opening decoder");


	/* Check that the allocated bitstream buffer is big enough */
	assert(imx_vpu_dma_buffer_get_size(bitstream_buffer) >= VPU_DEC_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE);


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
	(*decoder)->bitstream_buffer = bitstream_buffer;

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
	dec_open_param.bitstreamBufferSize = VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE;
	dec_open_param.qpReport = 0;
	dec_open_param.mp4DeblkEnable = 0;
	dec_open_param.chromaInterleave = open_params->chroma_interleave;
	dec_open_param.filePlayEnable = 0;
	dec_open_param.picWidth = open_params->frame_width;
	dec_open_param.picHeight = open_params->frame_height;
	dec_open_param.avcExtension = (open_params->codec_format == IMX_VPU_CODEC_FORMAT_H264_MVC);
	dec_open_param.dynamicAllocEnable = 0;
	dec_open_param.streamStartByteOffset = 0;
	dec_open_param.mjpg_thumbNailDecEnable = 0;
	dec_open_param.psSaveBuffer = (*decoder)->bitstream_buffer_physical_address + VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE + VPU_MAX_SLICE_BUFFER_SIZE;
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
	IMX_VPU_DEBUG("opening decoder, picture size: %u x %u pixel", open_params->frame_width, open_params->frame_height);
	dec_ret = vpu_DecOpen(&((*decoder)->handle), &dec_open_param);
	ret = IMX_VPU_DEC_HANDLE_ERROR("could not open decoder", dec_ret);
	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
		goto cleanup;

	(*decoder)->codec_format = open_params->codec_format;
	(*decoder)->picture_width = open_params->frame_width;
	(*decoder)->picture_height = open_params->frame_height;


	/* Finish & cleanup (in case of error) */
finish:
	if (ret == IMX_VPU_DEC_RETURN_CODE_OK)
		IMX_VPU_DEBUG("successfully opened decoder");

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

	IMX_VPU_DEBUG("closing decoder");


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

	if (decoder->bitstream_buffer != NULL)
		imx_vpu_dma_buffer_unmap(decoder->bitstream_buffer);

	imx_vpu_dec_free_internal_arrays(decoder);

	IMX_VPU_FREE(decoder, sizeof(ImxVpuDecoder));

	if (ret == IMX_VPU_DEC_RETURN_CODE_OK)
		IMX_VPU_DEBUG("successfully closed decoder");

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

	IMX_VPU_DEBUG("flushing decoder");

	if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_WMV3)
		return IMX_VPU_DEC_RETURN_CODE_OK;


	/* Clear any framebuffers that aren't ready for display yet but
	 * are being used for decoding (since flushing will clear them) */
	for (i = 0; i < decoder->num_framebuffers; ++i)
	{
		if (decoder->frame_entries[i].mode == FrameMode_ReservedForDecoding)
		{
			dec_ret = vpu_DecClrDispFlag(decoder->handle, i);
			IMX_VPU_DEC_HANDLE_ERROR("vpu_DecClrDispFlag failed while flushing", dec_ret);
			decoder->frame_entries[i].mode = FrameMode_Free;
		}
	}


	/* Perform the actual flush */
	dec_ret = vpu_DecBitBufferFlush(decoder->handle);
	ret = IMX_VPU_DEC_HANDLE_ERROR("could not flush decoder", dec_ret);

	if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
		return ret;


	/* After the flush, any context will be thrown away */
	for (i = 0; i < decoder->num_framebuffers; ++i)
		decoder->frame_entries[i].context = NULL;

	decoder->num_used_framebuffers = 0;


	IMX_VPU_DEBUG("successfully flushed decoder");


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

	IMX_VPU_DEBUG("attempting to register %u framebuffers", num_framebuffers);

	if (decoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		imx_vpu_dec_free_internal_arrays(decoder);
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
		ret = IMX_VPU_DEC_RETURN_CODE_ERROR;
		goto cleanup;
	}

	decoder->frame_entries = IMX_VPU_ALLOC(sizeof(ImxVpuDecFrameEntry) * num_framebuffers);
	if (decoder->frame_entries == NULL)
	{
		IMX_VPU_ERROR("allocating memory for frame entries failed");
		ret = IMX_VPU_DEC_RETURN_CODE_ERROR;
		goto cleanup;
	}


	/* Copy the values from the framebuffers array to the internal_framebuffers
	 * one, which in turn will be used by the VPU */
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
	buf_info.avcSliceBufInfo.bufferBase = decoder->bitstream_buffer_physical_address + VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE;
	buf_info.avcSliceBufInfo.bufferSize = VPU_MAX_SLICE_BUFFER_SIZE;
	buf_info.vp8MbDataBufInfo.bufferBase = decoder->bitstream_buffer_physical_address + VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE;
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
	 * and set the context pointers to their initial value (NULL) */
	decoder->framebuffers = framebuffers;
	decoder->num_framebuffers = num_framebuffers;
	for (i = 0; i < num_framebuffers; ++i)
	{
		decoder->frame_entries[i].context = NULL;
		decoder->frame_entries[i].mode = FrameMode_Free;
	}

	return IMX_VPU_DEC_RETURN_CODE_OK;

cleanup:
	imx_vpu_dec_free_internal_arrays(decoder);

	return ret;
}


static ImxVpuDecReturnCodes imx_vpu_dec_get_initial_info(ImxVpuDecoder *decoder)
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


static void imx_vpu_dec_insert_vp8_ivf_frame_header(uint8_t *header, size_t main_data_size, uint64_t pts)
{
	int i = 0;
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, main_data_size);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, (pts >> 0) & 0xFFFFFFFF);
	WRITE_32BIT_LE_AND_INCR_IDX(header, i, (pts >> 32) & 0xFFFFFFFF);
}


static void imx_vpu_dec_insert_wmv3_sequence_layer_header(uint8_t *header, unsigned int pic_width, unsigned int pic_height, size_t main_data_size, uint8_t *codec_data)
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


static void imx_vpu_dec_insert_wmv3_frame_layer_header(uint8_t *header, size_t main_data_size)
{
	/* Header as specified in the VC-1 specification, Annex J and L,
	 * L.3 , Frame Layer */
	WRITE_32BIT_LE(header, 0, main_data_size);
}


static void imx_vpu_dec_insert_vc1_frame_layer_header(uint8_t *header, uint8_t *main_data, size_t *actual_header_length)
{
	static uint8_t const start_code_prefix[3] = { 0x00, 0x00, 0x01 };

	/* Detect if a start code is present; if not, insert one.
	 * Detection works according to SMPTE 421M Annex E E.2.1:
	 * If the first two bytes are 0x00, and the third byte is
	 * 0x01, then this is a start code. Otherwise, it isn't
	 * one, and a frame start code is inserted. */
	if (memcmp(main_data, start_code_prefix, 3) != 0)
	{
		static uint8_t const frame_start_code[4] = { 0x00, 0x00, 0x01, 0x0D };
		memcpy(header, frame_start_code, 4);
		*actual_header_length = 4;
	}
	else
		*actual_header_length = 0;
}


static ImxVpuDecReturnCodes imx_vpu_dec_insert_frame_headers(ImxVpuDecoder *decoder, uint8_t *codec_data, size_t codec_data_size, uint8_t *main_data, size_t main_data_size)
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
				IMX_VPU_LOG("pushing codec data with %zu byte", codec_data_size - 1);
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
				size_t actual_header_length;
				imx_vpu_dec_insert_vc1_frame_layer_header(header, main_data, &actual_header_length);
				if (actual_header_length > 0)
				{
					IMX_VPU_LOG("pushing frame layer header with %zu byte", actual_header_length);
					ret = imx_vpu_dec_push_input_data(decoder, header, actual_header_length);
				}
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
			size_t header_size = 0;

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


static ImxVpuDecReturnCodes imx_vpu_dec_push_input_data(ImxVpuDecoder *decoder, void *data, size_t data_size)
{
	PhysicalAddress read_ptr, write_ptr;
	Uint32 num_free_bytes;
	RetCode dec_ret;
	size_t read_offset, write_offset, num_free_bytes_at_end, num_bytes_to_push;
	size_t bbuf_size;
	int i;
	ImxVpuDecReturnCodes ret;

	assert(decoder != NULL);

	/* Only touch data within the first VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE bytes of the
	 * overall bitstream buffer, since the bytes beyond are reserved for slice and
	 * ps save data and/or VP8 data */
	bbuf_size = VPU_DEC_MAIN_BITSTREAM_BUFFER_SIZE;


	/* Get the current read and write position pointers in the bitstream buffer For
	 * decoding, the write_ptr is the interesting one. The read_ptr is just logged.
	 * These pointers are physical addresses. To get an offset value for the write
	 * position for example, one calculates:
	 * write_offset = (write_ptr - bitstream_buffer_physical_address)
	 * Also, since MJPEG uses line buffer mode, this is not done for MJPEG */
	if (decoder->codec_format != IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		dec_ret = vpu_DecGetBitstreamBuffer(decoder->handle, &read_ptr, &write_ptr, &num_free_bytes);
		ret = IMX_VPU_DEC_HANDLE_ERROR("could not retrieve bitstream buffer information", dec_ret);
		if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;
		IMX_VPU_LOG("bitstream buffer status:  read ptr 0x%x  write ptr 0x%x  num free bytes %u", read_ptr, write_ptr, num_free_bytes);
	}


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

		/* Update the bitstream buffer pointers. Since MJPEG does not use the
		 * ring buffer (instead it uses the line buffer mode), update it only
		 * for non-MJPEG codec formats */
		if (decoder->codec_format != IMX_VPU_CODEC_FORMAT_MJPEG)
		{
			dec_ret = vpu_DecUpdateBitstreamBuffer(decoder->handle, num_bytes_to_push);
			ret = IMX_VPU_DEC_HANDLE_ERROR("could not update bitstream buffer with new data", dec_ret);
			if (ret != IMX_VPU_DEC_RETURN_CODE_OK)
				return ret;
		}

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
		if (decoder->frame_entries[i].mode == FrameMode_Free)
			return (int)i;
	}

	return -1;
}


static void imx_vpu_dec_free_internal_arrays(ImxVpuDecoder *decoder)
{
	if (decoder->internal_framebuffers != NULL)
	{
		IMX_VPU_FREE(decoder->internal_framebuffers, sizeof(FrameBuffer) * decoder->num_framebuffers);
		decoder->internal_framebuffers = NULL;
	}

	if (decoder->frame_entries != NULL)
	{
		IMX_VPU_FREE(decoder->frame_entries, sizeof(ImxVpuDecFrameEntry) * decoder->num_framebuffers);
		decoder->frame_entries = NULL;
	}
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


	IMX_VPU_LOG("input info: %d byte", encoded_frame->data_size);


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
		if ((ret = imx_vpu_dec_insert_frame_headers(decoder, encoded_frame->codec_data, encoded_frame->codec_data_size, encoded_frame->data, encoded_frame->data_size)) != IMX_VPU_DEC_RETURN_CODE_OK)
			return ret;

		/* Handle main frame data */
		IMX_VPU_LOG("pushing main frame data with %zu byte", encoded_frame->data_size);
		if ((ret = imx_vpu_dec_push_input_data(decoder, encoded_frame->data, encoded_frame->data_size)) != IMX_VPU_DEC_RETURN_CODE_OK)
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

		if (!imx_vpu_parse_jpeg_header(encoded_frame->data, encoded_frame->data_size, &jpeg_width, &jpeg_height, &jpeg_color_format))
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
		ret = imx_vpu_dec_get_initial_info(decoder);
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

			case IMX_VPU_DEC_RETURN_CODE_ERROR:
				IMX_VPU_ERROR("Internal error: unspecified error");
				return IMX_VPU_DEC_RETURN_CODE_ERROR;

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


		/* Start frame decoding
		 * The error handling code below does dummy vpu_DecGetOutputInfo() calls
		 * before exiting. This is done because according to the documentation,
		 * vpu_DecStartOneFrame() "locks out" most VPU calls until
		 * vpu_DecGetOutputInfo() is called, so this must be called *always*
		 * after vpu_DecStartOneFrame(), even if an error occurred. */
		dec_ret = vpu_DecStartOneFrame(decoder->handle, &params);

		if (dec_ret == RETCODE_JPEG_BIT_EMPTY)
		{
			vpu_DecGetOutputInfo(decoder->handle, &(decoder->dec_output_info));
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA;
			return IMX_VPU_DEC_RETURN_CODE_OK;
		}
		else if (dec_ret == RETCODE_JPEG_EOS)
		{
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_EOS;
			dec_ret = RETCODE_SUCCESS;
		}

		if ((ret = IMX_VPU_DEC_HANDLE_ERROR("could not decode frame", dec_ret)) != IMX_VPU_DEC_RETURN_CODE_OK)
		{
			vpu_DecGetOutputInfo(decoder->handle, &(decoder->dec_output_info));
			return ret;
		}


		/* Wait for frame completion */
		{
			int cnt;

			IMX_VPU_LOG("waiting for decoding completion");

			/* Wait a few times, since sometimes, it takes more than
			 * one vpu_WaitForInt() call to cover the decoding interval */
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


		/* If a timeout occurred earlier, this is the correct time to abort
		 * decoding and return an error code, since vpu_DecGetOutputInfo()
		 * has been called, unlocking the VPU decoder calls. */
		if (timeout)
			return IMX_VPU_DEC_RETURN_CODE_TIMEOUT;


		/* Log some information about the decoded frame */
		IMX_VPU_LOG(
			"output info:  indexFrameDisplay %d  indexFrameDecoded %d  NumDecFrameBuf %d  picType %d  idrFlg %d  numOfErrMBs %d  hScaleFlag %d  vScaleFlag %d  notSufficientPsBuffer %d  notSufficientSliceBuffer %d  decodingSuccess %d  interlacedFrame %d  mp4PackedPBframe %d  h264Npf %d  pictureStructure %d  topFieldFirst %d  repeatFirstField %d  fieldSequence %d  decPicWidth %d  decPicHeight %d",
			decoder->dec_output_info.indexFrameDisplay,
			decoder->dec_output_info.indexFrameDecoded,
			decoder->dec_output_info.NumDecFrameBuf,
			decoder->dec_output_info.picType,
			decoder->dec_output_info.idrFlg,
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
			ImxVpuPicType *pic_types;
			int idx_decoded = decoder->dec_output_info.indexFrameDecoded;
			assert(idx_decoded < (int)(decoder->num_framebuffers));

			decoder->frame_entries[idx_decoded].context = encoded_frame->context;
			decoder->frame_entries[idx_decoded].mode = FrameMode_ReservedForDecoding;
			decoder->frame_entries[idx_decoded].field_type = convert_field_type(decoder->codec_format, &(decoder->dec_output_info));

			/* XXX: The VPU documentation seems to be incorrect about IDR types.
			 * There is an undocumented idrFlg field which is also used by the
			 * VPU wrapper. If this flag's first bit is set, then this is an IDR
			 * picture, otherwise it is a non-IDR one. The non-IDR case is then
			 * handled in the default way (see convert_pic_type() for details). */
			pic_types = &(decoder->frame_entries[idx_decoded].pic_types[0]);
			if (((decoder->codec_format == IMX_VPU_CODEC_FORMAT_H264) || (decoder->codec_format == IMX_VPU_CODEC_FORMAT_H264_MVC)) && (decoder->dec_output_info.idrFlg & 0x01))
				pic_types[0] = pic_types[1] = IMX_VPU_PIC_TYPE_IDR;
			else
				convert_pic_type(decoder->codec_format, decoder->dec_output_info.picType, !!(decoder->dec_output_info.interlacedFrame), pic_types);

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

			IMX_VPU_LOG("decoded and displayable picture available (framebuffer display index: %d  context: %p)", idx_display, decoder->frame_entries[idx_display].context);

			decoder->frame_entries[idx_display].mode = FrameMode_ContainsDisplayablePicture;

			decoder->available_decoded_pic_idx = idx_display;
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE;
		}
		else if (decoder->dec_output_info.indexFrameDisplay == VPU_DECODER_DISPLAYIDX_ALL_PICTURED_DISPLAYED)
		{
			IMX_VPU_LOG("EOS reached");
			decoder->available_decoded_pic_idx = -1;
			*output_code |= IMX_VPU_DEC_OUTPUT_CODE_EOS;
		}
		else
		{
			IMX_VPU_LOG("nothing yet to display ; indexFrameDisplay: %d", decoder->dec_output_info.indexFrameDisplay);
		}

	}


	return ret;
}


ImxVpuDecReturnCodes imx_vpu_dec_get_decoded_picture(ImxVpuDecoder *decoder, ImxVpuPicture *decoded_picture)
{
	int i;
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
	decoded_picture->field_type = decoder->frame_entries[idx].field_type;
	decoded_picture->context = decoder->frame_entries[idx].context;
	for (i = 0; i < 2; ++i)
		decoded_picture->pic_types[i] = decoder->frame_entries[idx].pic_types[i];


	/* erase the context from context_for_frames after retrieval, and set
	 * available_decoded_pic_idx to -1 ; this ensures no erroneous
	 * double-retrieval can occur */
	decoder->frame_entries[idx].context = NULL;
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
	decoder->frame_entries[idx].mode = FrameMode_Free;


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


struct _ImxVpuEncoder
{
	EncHandle handle;

	ImxVpuDMABuffer *bitstream_buffer;
	uint8_t *bitstream_buffer_virtual_address;
	imx_vpu_phys_addr_t bitstream_buffer_physical_address;

	ImxVpuCodecFormat codec_format;
	unsigned int picture_width, picture_height;
	unsigned int frame_rate_numerator, frame_rate_denominator;

	unsigned int num_framebuffers;
	FrameBuffer *internal_framebuffers;
	ImxVpuFramebuffer *framebuffers;

	BOOL first_frame;

	union
	{
		struct
		{
			uint8_t *sps_rbsp;
			uint8_t *pps_rbsp;
			size_t sps_rbsp_size;
			size_t pps_rbsp_size;
		}
		h264_headers;

		struct
		{
			uint8_t *vos_header;
			uint8_t *vis_header;
			uint8_t *vol_header;
			size_t vos_header_size;
			size_t vis_header_size;
			size_t vol_header_size;
		}
		mpeg4_headers;
	}
	headers;
};


#define IMX_VPU_ENC_HANDLE_ERROR(MSG_START, RET_CODE) \
	imx_vpu_enc_handle_error_full(__FILE__, __LINE__, __FUNCTION__, (MSG_START), (RET_CODE))


static ImxVpuEncReturnCodes imx_vpu_enc_handle_error_full(char const *fn, int linenr, char const *funcn, char const *msg_start, RetCode ret_code)
{
	switch (ret_code)
	{
		case RETCODE_SUCCESS:
			return IMX_VPU_ENC_RETURN_CODE_OK;

		case RETCODE_FAILURE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: failure", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_INVALID_HANDLE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid handle", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_HANDLE;

		case RETCODE_INVALID_PARAM:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid parameters", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_INVALID_COMMAND:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid command", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_ROTATOR_OUTPUT_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: rotation enabled but rotator output buffer not set", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_ROTATOR_STRIDE_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: rotation enabled but rotator stride not set", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_FRAME_NOT_COMPLETE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: frame encoding operation not complete", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_INVALID_FRAME_BUFFER:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: frame buffers are invalid", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_INSUFFICIENT_FRAME_BUFFERS:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: not enough frame buffers specified (must be equal to or larger than the minimum number reported by imx_vpu_enc_get_initial_info)", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_INVALID_STRIDE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: invalid stride - check Y stride values of framebuffers (must be a multiple of 8 and equal to or larger than the picture width)", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_WRONG_CALL_SEQUENCE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: wrong call sequence", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE;

		case RETCODE_CALLED_BEFORE:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: already called before (may not be called more than once in a VPU instance)", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_NOT_INITIALIZED:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: VPU is not initialized", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE;

		case RETCODE_DEBLOCKING_OUTPUT_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: deblocking activated but deblocking information not available", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_NOT_SUPPORTED:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: feature not supported", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_REPORT_BUF_NOT_SET:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: data report buffer address not set", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS;

		case RETCODE_FAILURE_TIMEOUT:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: timeout", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_MEMORY_ACCESS_VIOLATION:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: memory access violation", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		case RETCODE_JPEG_EOS:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: MJPEG end-of-stream reached", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_OK;

		case RETCODE_JPEG_BIT_EMPTY:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: MJPEG bit buffer empty - cannot parse header", msg_start);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;

		default:
			IMX_VPU_ERROR_FULL(fn, linenr, funcn, "%s: unknown error 0x%x", msg_start, ret_code);
			return IMX_VPU_ENC_RETURN_CODE_ERROR;
	}
}


static void imx_vpu_enc_copy_quantization_table(uint8_t *dest_table, uint8_t const *src_table, size_t num_coefficients, unsigned int scale_factor)
{
	IMX_VPU_LOG("quantization table:  num coefficients: %u  scale factor: %u ", num_coefficients, scale_factor);

	for (size_t i = 0; i < num_coefficients; ++i)
	{
		/* The +50 ensures rounding instead of truncation */
		long val = (((long)src_table[i]) * scale_factor + 50) / 100;

		/* The VPU's JPEG encoder supports baseline data only,
		 * so no quantization matrix values above 255 are allowed */
		if (val <= 0)
			val = 1;
		else if (val >= 255)
			val = 255;

		dest_table[i] = val;
	}
}


static void imx_vpu_enc_set_mjpeg_tables(unsigned int quality_factor, EncMjpgParam *mjpeg_params)
{
	uint8_t const *component_info_table;
	unsigned int scale_factor;

	assert(mjpeg_params != NULL);


	/* NOTE: The tables in structure referred to by mjpeg_params must
	 * have been filled with nullbytes, and the mjpg_sourceFormat field
	 * must be valid */


	/* Copy the Huffman tables */

	memcpy(mjpeg_params->huffBits[DC_TABLE_INDEX0], mjpeg_enc_huffman_bits_luma_dc, sizeof(mjpeg_enc_huffman_bits_luma_dc));
	memcpy(mjpeg_params->huffBits[AC_TABLE_INDEX0], mjpeg_enc_huffman_bits_luma_ac, sizeof(mjpeg_enc_huffman_bits_luma_ac));
	memcpy(mjpeg_params->huffBits[DC_TABLE_INDEX1], mjpeg_enc_huffman_bits_chroma_dc, sizeof(mjpeg_enc_huffman_bits_chroma_dc));
	memcpy(mjpeg_params->huffBits[AC_TABLE_INDEX1], mjpeg_enc_huffman_bits_chroma_ac, sizeof(mjpeg_enc_huffman_bits_chroma_ac));

	memcpy(mjpeg_params->huffVal[DC_TABLE_INDEX0], mjpeg_enc_huffman_value_luma_dc, sizeof(mjpeg_enc_huffman_value_luma_dc));
	memcpy(mjpeg_params->huffVal[AC_TABLE_INDEX0], mjpeg_enc_huffman_value_luma_ac, sizeof(mjpeg_enc_huffman_value_luma_ac));
	memcpy(mjpeg_params->huffVal[DC_TABLE_INDEX1], mjpeg_enc_huffman_value_chroma_dc, sizeof(mjpeg_enc_huffman_value_chroma_dc));
	memcpy(mjpeg_params->huffVal[AC_TABLE_INDEX1], mjpeg_enc_huffman_value_chroma_ac, sizeof(mjpeg_enc_huffman_value_chroma_ac));


	/* Copy the quantization tables */

	/* Ensure the quality factor is in the 1..100 range */
	if (quality_factor < 1)
		quality_factor = 1;
	if (quality_factor > 100)
		quality_factor = 100;

	/* Using the Independent JPEG Group's formula, used in libjpeg, for generating
	 * a scale factor out of a quality factor in the 1..100 range */
	if (quality_factor < 50)
		scale_factor = 5000 / quality_factor;
	else
		scale_factor = 200 - quality_factor * 2;

	imx_vpu_enc_copy_quantization_table(mjpeg_params->qMatTab[DC_TABLE_INDEX0], mjpeg_enc_quantization_luma,   sizeof(mjpeg_enc_quantization_luma),   scale_factor);
	imx_vpu_enc_copy_quantization_table(mjpeg_params->qMatTab[AC_TABLE_INDEX0], mjpeg_enc_quantization_chroma, sizeof(mjpeg_enc_quantization_chroma), scale_factor);
	imx_vpu_enc_copy_quantization_table(mjpeg_params->qMatTab[DC_TABLE_INDEX1], mjpeg_enc_quantization_luma,   sizeof(mjpeg_enc_quantization_luma),   scale_factor);
	imx_vpu_enc_copy_quantization_table(mjpeg_params->qMatTab[AC_TABLE_INDEX1], mjpeg_enc_quantization_chroma, sizeof(mjpeg_enc_quantization_chroma), scale_factor);


	/* Copy the component info table (depends on the format) */

	switch (mjpeg_params->mjpg_sourceFormat)
	{
		case FORMAT_420: component_info_table = mjpeg_enc_component_info_tables[0]; break;
		case FORMAT_422: component_info_table = mjpeg_enc_component_info_tables[1]; break;
		case FORMAT_224: component_info_table = mjpeg_enc_component_info_tables[2]; break;
		case FORMAT_444: component_info_table = mjpeg_enc_component_info_tables[3]; break;
		case FORMAT_400: component_info_table = mjpeg_enc_component_info_tables[4]; break;
		default: assert(FALSE);
	}

	memcpy(mjpeg_params->cInfoTab, component_info_table, 4 * 6);
}


static ImxVpuEncReturnCodes imx_vpu_enc_generate_header_data(ImxVpuEncoder *encoder)
{
	ImxVpuEncReturnCodes ret;
	RetCode enc_ret;

#define GENERATE_HEADER_DATA(COMMAND, HEADER_TYPE, HEADER_FIELD, DESCRIPTION) \
	do \
	{ \
		enc_header_param.headerType = (HEADER_TYPE); \
		enc_ret = vpu_EncGiveCommand(encoder->handle, (COMMAND), &enc_header_param); \
		if ((ret = IMX_VPU_ENC_HANDLE_ERROR("header generation command failed", enc_ret)) != IMX_VPU_ENC_RETURN_CODE_OK) \
			return ret; \
		\
		if ((encoder->headers.HEADER_FIELD = IMX_VPU_ALLOC(enc_header_param.size)) == NULL) \
		{ \
			IMX_VPU_ERROR("could not allocate %d byte for %s memory block", enc_header_param.size, (DESCRIPTION)); \
			return IMX_VPU_ENC_RETURN_CODE_ERROR; \
		} \
		\
		memcpy( \
			encoder->headers.HEADER_FIELD, \
			encoder->bitstream_buffer_virtual_address + (enc_header_param.buf - encoder->bitstream_buffer_physical_address), \
			enc_header_param.size \
		); \
		encoder->headers.HEADER_FIELD ## _size = enc_header_param.size; \
		\
		IMX_VPU_LOG("generated %s with %d byte", (DESCRIPTION), enc_header_param.size); \
	} \
	while (0)

	switch (encoder->codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_H264:
		{
			EncHeaderParam enc_header_param;
			memset(&enc_header_param, 0, sizeof(enc_header_param));

			GENERATE_HEADER_DATA(ENC_PUT_AVC_HEADER, SPS_RBSP, h264_headers.sps_rbsp, "h.264 SPS");
			GENERATE_HEADER_DATA(ENC_PUT_AVC_HEADER, PPS_RBSP, h264_headers.pps_rbsp, "h.264 PPS");

			break;
		}

		case IMX_VPU_CODEC_FORMAT_MPEG4:
		{
			unsigned int num_macroblocks_per_frame;
			unsigned int num_macroblocks_per_second;
			unsigned int w, h;
			EncHeaderParam enc_header_param;

			memset(&enc_header_param, 0, sizeof(enc_header_param));

			w = encoder->picture_width;
			h = encoder->picture_height;

			/* Calculate the number of macroblocks per second in two steps.
			 * Step 1 calculates the number of macroblocks per frame.
			 * Based on that, step 2 calculates the actual number of
			 * macroblocks per second. The "((encoder->frame_rate_denominator + 1) / 2)"
			 * part is for rounding up. */
			num_macroblocks_per_frame = ((w + 15) / 16) * ((h + 15) / 16);
			num_macroblocks_per_second = (num_macroblocks_per_frame * encoder->frame_rate_numerator + ((encoder->frame_rate_denominator + 1) / 2)) / encoder->frame_rate_denominator;

			/* Decide the user profile level indication based on the VPU
			 * documentation's section 3.2.2.4 and Annex N in ISO/IEC 14496-2 */

			if ((w <= 176) && (h <= 144) && (num_macroblocks_per_second <= 1485))
				enc_header_param.userProfileLevelIndication = 1; /* XXX: this is set to 8 in the VPU wrapper, why? */
			else if ((w <= 352) && (h <= 288) && (num_macroblocks_per_second <= 5940))
				enc_header_param.userProfileLevelIndication = 2;
			else if ((w <= 352) && (h <= 288) && (num_macroblocks_per_second <= 11880))
				enc_header_param.userProfileLevelIndication = 3;
			else if ((w <= 640) && (h <= 480) && (num_macroblocks_per_second <= 36000))
				enc_header_param.userProfileLevelIndication = 4;
			else if ((w <= 720) && (h <= 576) && (num_macroblocks_per_second <= 40500))
				enc_header_param.userProfileLevelIndication = 5;
			else
				enc_header_param.userProfileLevelIndication = 6;

			enc_header_param.userProfileLevelEnable = 1;

			IMX_VPU_LOG("picture size: %u x %u pixel, %u macroblocks per second => MPEG-4 user profile level indication = %d", w, h, num_macroblocks_per_second, enc_header_param.userProfileLevelIndication);

			GENERATE_HEADER_DATA(ENC_PUT_MP4_HEADER, VOS_HEADER, mpeg4_headers.vos_header, "MPEG-4 VOS header");
			GENERATE_HEADER_DATA(ENC_PUT_MP4_HEADER, VIS_HEADER, mpeg4_headers.vis_header, "MPEG-4 VIS header");
			GENERATE_HEADER_DATA(ENC_PUT_MP4_HEADER, VOL_HEADER, mpeg4_headers.vol_header, "MPEG-4 VOL header");

			break;
		}

		default:
			break;
	}

#undef GENERATE_HEADER_DATA

	return IMX_VPU_ENC_RETURN_CODE_OK;
}


static void imx_vpu_enc_free_header_data(ImxVpuEncoder *encoder)
{
#define DEALLOC_HEADER(HEADER_FIELD) \
	if (encoder->headers.HEADER_FIELD != NULL) \
	{ \
		IMX_VPU_FREE(encoder->headers.HEADER_FIELD, encoder->headers.HEADER_FIELD ## _size); \
		encoder->headers.HEADER_FIELD = NULL; \
	}

	switch (encoder->codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_H264:
			DEALLOC_HEADER(h264_headers.sps_rbsp);
			DEALLOC_HEADER(h264_headers.pps_rbsp);
			break;

		case IMX_VPU_CODEC_FORMAT_MPEG4:
			DEALLOC_HEADER(mpeg4_headers.vos_header);
			DEALLOC_HEADER(mpeg4_headers.vis_header);
			DEALLOC_HEADER(mpeg4_headers.vol_header);
			break;

		default:
			break;
	}

#undef DEALLOC_HEADER
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
	*size = VPU_ENC_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE;
	*alignment = VPU_MEMORY_ALIGNMENT;
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
	open_params->user_defined_min_qp = 0;
	open_params->user_defined_max_qp = 0;
	open_params->enable_user_defined_min_qp = 0;
	open_params->enable_user_defined_max_qp = 0;
	open_params->min_intra_refresh_mb_count = 0;
	open_params->intra_qp = -1;
	open_params->user_gamma = (int)(0.75*32768);
	open_params->rate_interval_mode = IMX_VPU_ENC_RATE_INTERVAL_MODE_NORMAL;
	open_params->macroblock_interval = 0;
	open_params->enable_avc_intra_16x16_only_mode = 0;
	open_params->slice_mode.multiple_slices_per_picture = 0;
	open_params->slice_mode.slice_size_mode = IMX_VPU_ENC_SLICE_SIZE_MODE_BITS;
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
			open_params->codec_params.mpeg4_params.enable_data_partition = 0;
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

		case IMX_VPU_CODEC_FORMAT_MJPEG:
			open_params->codec_params.mjpeg_params.quality_factor = 85;

		default:
			break;
	}
}


ImxVpuEncReturnCodes imx_vpu_enc_open(ImxVpuEncoder **encoder, ImxVpuEncOpenParams *open_params, ImxVpuDMABuffer *bitstream_buffer)
{
	ImxVpuEncReturnCodes ret;
	EncOpenParam enc_open_param;
	RetCode enc_ret;

	assert(encoder != NULL);
	assert(open_params != NULL);
	assert(bitstream_buffer != NULL);


	/* Check that the allocated bitstream buffer is big enough */
	assert(imx_vpu_dma_buffer_get_size(bitstream_buffer) >= VPU_ENC_MIN_REQUIRED_BITSTREAM_BUFFER_SIZE);


	/* Allocate encoder instance */
	*encoder = IMX_VPU_ALLOC(sizeof(ImxVpuEncoder));
	if ((*encoder) == NULL)
	{
		IMX_VPU_ERROR("allocating memory for encoder object failed");
		return IMX_VPU_ENC_RETURN_CODE_ERROR;
	}


	/* Set default encoder values */
	memset(*encoder, 0, sizeof(ImxVpuEncoder));
	memset(&enc_open_param, 0, sizeof(enc_open_param));
	(*encoder)->first_frame = TRUE;


	/* Map the bitstream buffer. This mapping will persist until the encoder is closed. */
	(*encoder)->bitstream_buffer_virtual_address = imx_vpu_dma_buffer_map(bitstream_buffer, 0);
	(*encoder)->bitstream_buffer_physical_address = imx_vpu_dma_buffer_get_physical_address(bitstream_buffer);
	(*encoder)->bitstream_buffer = bitstream_buffer;


	/* Fill in the bitstream buffer address and size.
	 * The actual bitstream buffer is a subset of the bitstream buffer that got
	 * allocated by the user. The remaining space is reserved for the MPEG-4
	 * scratch buffer. This is a trick to reduce DMA memory fragmentation;
	 * both buffers share one DMA memory block, the actual bitstream buffer
	 * comes first, followed by the scratch buffer. */
	enc_open_param.bitstreamBuffer = (*encoder)->bitstream_buffer_physical_address;
	enc_open_param.bitstreamBufferSize = VPU_ENC_MAIN_BITSTREAM_BUFFER_SIZE;

	/* Miscellaneous codec format independent values */
	enc_open_param.picWidth = open_params->frame_width;
	enc_open_param.picHeight = open_params->frame_height;
	enc_open_param.frameRateInfo = (open_params->frame_rate_numerator & 0xffffUL) | (((open_params->frame_rate_denominator - 1) & 0xffffUL) << 16);
	enc_open_param.bitRate = open_params->bitrate;
	enc_open_param.initialDelay = open_params->initial_delay;
	enc_open_param.vbvBufferSize = open_params->vbv_buffer_size;
	enc_open_param.gopSize = open_params->gop_size;
	enc_open_param.slicemode.sliceMode = open_params->slice_mode.multiple_slices_per_picture;
	enc_open_param.slicemode.sliceSizeMode = open_params->slice_mode.slice_size_mode;
	enc_open_param.slicemode.sliceSize = open_params->slice_mode.slice_size;
	enc_open_param.intraRefresh = open_params->min_intra_refresh_mb_count;
	enc_open_param.rcIntraQp = open_params->intra_qp;
	enc_open_param.userQpMin = open_params->user_defined_min_qp;
	enc_open_param.userQpMax = open_params->user_defined_max_qp;
	enc_open_param.userQpMinEnable = open_params->enable_user_defined_min_qp;
	enc_open_param.userQpMaxEnable = open_params->enable_user_defined_max_qp;
	enc_open_param.userGamma = open_params->user_gamma;
	enc_open_param.RcIntervalMode = open_params->rate_interval_mode;
	enc_open_param.MbInterval = open_params->macroblock_interval;
	enc_open_param.avcIntra16x16OnlyModeEnable = open_params->enable_avc_intra_16x16_only_mode;
	enc_open_param.MESearchRange = open_params->me_search_range;
	enc_open_param.MEUseZeroPmv = open_params->use_me_zero_pmv;
	enc_open_param.IntraCostWeight = open_params->additional_intra_cost_weight;
	enc_open_param.chromaInterleave = open_params->chroma_interleave;

	/* Reports are currently not used */
	enc_open_param.sliceReport = 0;
	enc_open_param.mbReport = 0;
	enc_open_param.mbQpReport = 0;

	/* The i.MX6 does not support dynamic allocation */
	enc_open_param.dynamicAllocEnable = 0;

	/* Ring buffer mode isn't needed, so disable it, instructing
	 * the VPU to use the line buffer mode instead */
	enc_open_param.ringBufferEnable = 0;

	/* Currently, no tiling is supported */
	enc_open_param.linear2TiledEnable = 1;
	enc_open_param.mapType = 0;

	/* Fill in codec format specific values into the VPU's encoder open param structure */
	switch (open_params->codec_format)
	{
		case IMX_VPU_CODEC_FORMAT_MPEG4:
			enc_open_param.bitstreamFormat = STD_MPEG4;
			enc_open_param.EncStdParam.mp4Param.mp4_dataPartitionEnable = open_params->codec_params.mpeg4_params.enable_data_partition;
			enc_open_param.EncStdParam.mp4Param.mp4_reversibleVlcEnable = open_params->codec_params.mpeg4_params.enable_reversible_vlc;
			enc_open_param.EncStdParam.mp4Param.mp4_intraDcVlcThr = open_params->codec_params.mpeg4_params.intra_dc_vlc_thr;
			enc_open_param.EncStdParam.mp4Param.mp4_hecEnable = open_params->codec_params.mpeg4_params.enable_hec;
			enc_open_param.EncStdParam.mp4Param.mp4_verid = open_params->codec_params.mpeg4_params.version_id;
			break;

		case IMX_VPU_CODEC_FORMAT_H263:
			enc_open_param.bitstreamFormat = STD_H263;
			enc_open_param.EncStdParam.h263Param.h263_annexIEnable = open_params->codec_params.h263_params.enable_annex_i;
			enc_open_param.EncStdParam.h263Param.h263_annexJEnable = open_params->codec_params.h263_params.enable_annex_j;
			enc_open_param.EncStdParam.h263Param.h263_annexKEnable = open_params->codec_params.h263_params.enable_annex_k;
			enc_open_param.EncStdParam.h263Param.h263_annexTEnable = open_params->codec_params.h263_params.enable_annex_t;

			/* The VPU does not permit any other search range for h.263 */
			enc_open_param.MESearchRange = IMX_VPU_ENC_ME_SEARCH_RANGE_32x32;

			break;

		case IMX_VPU_CODEC_FORMAT_H264:
		{
			unsigned int width_remainder, height_remainder;

			enc_open_param.bitstreamFormat = STD_AVC;
			enc_open_param.EncStdParam.avcParam.avc_constrainedIntraPredFlag = open_params->codec_params.h264_params.enable_constrained_intra_prediction;
			enc_open_param.EncStdParam.avcParam.avc_disableDeblk = open_params->codec_params.h264_params.disable_deblocking;
			enc_open_param.EncStdParam.avcParam.avc_deblkFilterOffsetAlpha = open_params->codec_params.h264_params.deblock_filter_offset_alpha;
			enc_open_param.EncStdParam.avcParam.avc_deblkFilterOffsetBeta = open_params->codec_params.h264_params.deblock_filter_offset_beta;
			enc_open_param.EncStdParam.avcParam.avc_chromaQpOffset = open_params->codec_params.h264_params.chroma_qp_offset;
			enc_open_param.EncStdParam.avcParam.avc_audEnable = open_params->codec_params.h264_params.enable_access_unit_delimiters;

			/* XXX: h.264 MVC support is currently not implemented */
			enc_open_param.EncStdParam.avcParam.mvc_extension = 0;
			enc_open_param.EncStdParam.avcParam.interview_en = 0;
			enc_open_param.EncStdParam.avcParam.paraset_refresh_en = 0;
			enc_open_param.EncStdParam.avcParam.prefix_nal_en = 0;

			/* Check if the frame fits within the 16-pixel boundaries.
			 * If not, crop the remainders. */
			width_remainder = open_params->frame_width & 15;
			height_remainder = open_params->frame_height & 15;
			enc_open_param.EncStdParam.avcParam.avc_frameCroppingFlag = (width_remainder != 0) || (height_remainder != 0);
			enc_open_param.EncStdParam.avcParam.avc_frameCropRight = width_remainder;
			enc_open_param.EncStdParam.avcParam.avc_frameCropBottom = height_remainder;

			break;
		}

		case IMX_VPU_CODEC_FORMAT_MJPEG:
		{
			enc_open_param.bitstreamFormat = STD_MJPG;

			switch (open_params->color_format)
			{
				case IMX_VPU_COLOR_FORMAT_YUV420:
					enc_open_param.EncStdParam.mjpgParam.mjpg_sourceFormat = FORMAT_420;
					break;
				case IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL:
					enc_open_param.EncStdParam.mjpgParam.mjpg_sourceFormat = FORMAT_422;
					break;
				case IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL:
					enc_open_param.EncStdParam.mjpgParam.mjpg_sourceFormat = FORMAT_224;
					break;
				case IMX_VPU_COLOR_FORMAT_YUV444:
					enc_open_param.EncStdParam.mjpgParam.mjpg_sourceFormat = FORMAT_444;
					break;
				case IMX_VPU_COLOR_FORMAT_YUV400:
					enc_open_param.EncStdParam.mjpgParam.mjpg_sourceFormat = FORMAT_400;
					break;

				default:
					IMX_VPU_ERROR("unknown color format value %d", open_params->color_format);
					ret = IMX_VPU_DEC_RETURN_CODE_ERROR;
					goto cleanup;
			}

			imx_vpu_enc_set_mjpeg_tables(open_params->codec_params.mjpeg_params.quality_factor, &(enc_open_param.EncStdParam.mjpgParam));

			enc_open_param.EncStdParam.mjpgParam.mjpg_restartInterval = 60;
			enc_open_param.EncStdParam.mjpgParam.mjpg_thumbNailEnable = 0;
			enc_open_param.EncStdParam.mjpgParam.mjpg_thumbNailWidth = 0;
			enc_open_param.EncStdParam.mjpgParam.mjpg_thumbNailHeight = 0;
			break;
		}

		default:
			break;
	}


	/* Now actually open the encoder instance */
	IMX_VPU_LOG("opening encoder, picture size: %u x %u pixel", open_params->frame_width, open_params->frame_height);
	enc_ret = vpu_EncOpen(&((*encoder)->handle), &enc_open_param);
	ret = IMX_VPU_ENC_HANDLE_ERROR("could not open encoder", enc_ret);
	if (ret != IMX_VPU_ENC_RETURN_CODE_OK)
		goto cleanup;


	/* Store some parameters internally for later use */
	(*encoder)->codec_format = open_params->codec_format;
	(*encoder)->picture_width = open_params->frame_width;
	(*encoder)->picture_height = open_params->frame_height;
	(*encoder)->frame_rate_numerator = open_params->frame_rate_numerator;
	(*encoder)->frame_rate_denominator = open_params->frame_rate_denominator;


finish:
	if (ret == IMX_VPU_ENC_RETURN_CODE_OK)
		IMX_VPU_DEBUG("successfully opened encoder");

	return ret;

cleanup:
	imx_vpu_dma_buffer_unmap(bitstream_buffer);
	IMX_VPU_FREE(*encoder, sizeof(ImxVpuEncoder));
	*encoder = NULL;

	goto finish;
}


ImxVpuEncReturnCodes imx_vpu_enc_close(ImxVpuEncoder *encoder)
{
	ImxVpuEncReturnCodes ret;
	RetCode enc_ret;

	assert(encoder != NULL);

	IMX_VPU_DEBUG("closing encoder");


	/* Close the encoder handle */

	enc_ret = vpu_EncClose(encoder->handle);
	if (enc_ret == RETCODE_FRAME_NOT_COMPLETE)
	{
		/* VPU refused to close, since a frame is partially encoded.
		 * Force it to close by first resetting the handle and retry. */
		vpu_SWReset(encoder->handle, 0);
		enc_ret = vpu_EncClose(encoder->handle);
	}
	ret = IMX_VPU_ENC_HANDLE_ERROR("error while closing encoder", enc_ret);


	/* Remaining cleanup */

	imx_vpu_enc_free_header_data(encoder);

	if (encoder->bitstream_buffer != NULL)
		imx_vpu_dma_buffer_unmap(encoder->bitstream_buffer);

	if (encoder->internal_framebuffers != NULL)
		IMX_VPU_FREE(encoder->internal_framebuffers, sizeof(FrameBuffer) * encoder->num_framebuffers);

	IMX_VPU_FREE(encoder, sizeof(ImxVpuEncoder));

	if (ret == IMX_VPU_ENC_RETURN_CODE_OK)
		IMX_VPU_DEBUG("successfully closed encoder");

	return ret;
}


ImxVpuDMABuffer* imx_vpu_enc_get_bitstream_buffer(ImxVpuEncoder *encoder)
{
	return encoder->bitstream_buffer;
}


ImxVpuEncReturnCodes imx_vpu_enc_flush(ImxVpuEncoder *encoder)
{
	encoder->first_frame = TRUE;

	/* NOTE: A vpu_SWReset() call would require a re-registering of the
	 * framebuffers and does not yield any benefits */

	return IMX_VPU_ENC_RETURN_CODE_OK;
}


ImxVpuEncReturnCodes imx_vpu_enc_register_framebuffers(ImxVpuEncoder *encoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers)
{
	unsigned int i;
	ImxVpuEncReturnCodes ret;
	RetCode enc_ret;
	ExtBufCfg scratch_cfg;

	assert(encoder != NULL);
	assert(framebuffers != NULL);

	/* Additional buffers are reserved for the subsampled images */
	assert(num_framebuffers > VPU_ENC_NUM_EXTRA_SUBSAMPLE_FRAMEBUFFERS);
	num_framebuffers -= VPU_ENC_NUM_EXTRA_SUBSAMPLE_FRAMEBUFFERS;

	IMX_VPU_DEBUG("attempting to register %u framebuffers", num_framebuffers);


	/* Allocate memory for framebuffer structures */

	encoder->internal_framebuffers = IMX_VPU_ALLOC(sizeof(FrameBuffer) * num_framebuffers);
	if (encoder->internal_framebuffers == NULL)
	{
		IMX_VPU_ERROR("allocating memory for framebuffers failed");
		return IMX_VPU_ENC_RETURN_CODE_ERROR;
	}


	/* Copy the values from the framebuffers array to the internal_framebuffers
	 * one, which in turn will be used by the VPU */
	memset(encoder->internal_framebuffers, 0, sizeof(FrameBuffer) * num_framebuffers);
	for (i = 0; i < num_framebuffers; ++i)
	{
		imx_vpu_phys_addr_t phys_addr;
		ImxVpuFramebuffer *fb = &framebuffers[i];
		FrameBuffer *internal_fb = &(encoder->internal_framebuffers[i]);

		phys_addr = imx_vpu_dma_buffer_get_physical_address(fb->dma_buffer);
		if (phys_addr == 0)
		{
			IMX_VPU_ERROR("could not map buffer %u/%u", i, num_framebuffers);
			ret = IMX_VPU_ENC_RETURN_CODE_ERROR;
			goto cleanup;
		}

		internal_fb->strideY = fb->y_stride;
		internal_fb->strideC = fb->cbcr_stride;
		internal_fb->myIndex = i;
		internal_fb->bufY = (PhysicalAddress)(phys_addr + fb->y_offset);
		internal_fb->bufCb = (PhysicalAddress)(phys_addr + fb->cb_offset);
		internal_fb->bufCr = (PhysicalAddress)(phys_addr + fb->cr_offset);
		internal_fb->bufMvCol = (PhysicalAddress)(phys_addr + fb->mvcol_offset);
	}

	/* Set up the scratch buffer information. The MPEG-4 scratch buffer
	 * is located in the same DMA buffer as the bitstream buffer
	 * (the bitstream buffer comes first, and is the largest part of
	 * the DMA buffer, followed by the scratch buffer). */
	scratch_cfg.bufferBase = encoder->bitstream_buffer_physical_address + VPU_ENC_MAIN_BITSTREAM_BUFFER_SIZE;
	scratch_cfg.bufferSize = VPU_ENC_MPEG4_SCRATCH_SIZE;

	{
		/* NOTE: The vpu_EncRegisterFrameBuffer() API changed several times
		 * in the past. To maintain compatibility with (very) old BSPs,
		 * preprocessor macros are used to adapt the code.
		 * Before vpulib version 5.3.3, vpu_EncRegisterFrameBuffer() didn't
		 * accept any extra scratch buffer information. Between 5.3.3 and
		 * 5.3.7, it accepted an ExtBufCfg argument. Starting with 5.3.7,
		 * it expects an EncExtBufInfo argument.
		 */

		ImxVpuFramebuffer *subsample_buffer_A, *subsample_buffer_B;
#if (VPU_LIB_VERSION_CODE >= VPU_LIB_VERSION(5, 3, 7))
		EncExtBufInfo buf_info;
		memset(&buf_info, 0, sizeof(buf_info));
		buf_info.scratchBuf = scratch_cfg;
#endif

		/* TODO: is it really necessary to use two full buffers for the
		 * subsampling buffers? They could both be placed in one
		 * buffer, thus saving memory */
		subsample_buffer_A = &(framebuffers[num_framebuffers + 0]);
		subsample_buffer_B = &(framebuffers[num_framebuffers + 1]);

		enc_ret = vpu_EncRegisterFrameBuffer(
			encoder->handle,
			encoder->internal_framebuffers,
			num_framebuffers,
			framebuffers[0].y_stride, /* The stride value is assumed to be the same for all framebuffers */
			0, /* The i.MX6 does not actually need the sourceBufStride value (this is missing in the docs) */
			imx_vpu_dma_buffer_get_physical_address(subsample_buffer_A->dma_buffer),
			imx_vpu_dma_buffer_get_physical_address(subsample_buffer_B->dma_buffer)
#if (VPU_LIB_VERSION_CODE >= VPU_LIB_VERSION(5, 3, 7))
			, &buf_info
#elif (VPU_LIB_VERSION_CODE >= VPU_LIB_VERSION(5, 3, 3))
			, &scratch_cfg
#endif
		);
		ret = IMX_VPU_ENC_HANDLE_ERROR("could not register framebuffers", enc_ret);
		if (ret != IMX_VPU_ENC_RETURN_CODE_OK)
			goto cleanup;
	}


	/* Set default rotator settings for motion JPEG */
	if (encoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		/* the datatypes are int, but this is undocumented; determined by looking
		 * into the imx-vpu library's vpu_lib.c vpu_EncGiveCommand() definition */
		int rotation_angle = 0;
		int mirror = 0;
		int stride = framebuffers[0].y_stride;
		int append_nullbytes_to_sof_field = 0;

		vpu_EncGiveCommand(encoder->handle, SET_ROTATION_ANGLE, (void *)(&rotation_angle));
		vpu_EncGiveCommand(encoder->handle, SET_MIRROR_DIRECTION,(void *)(&mirror));
		vpu_EncGiveCommand(encoder->handle, SET_ROTATOR_STRIDE, (void *)(&stride));
		vpu_EncGiveCommand(encoder->handle, ENC_ENABLE_SOF_STUFF, (void*)(&append_nullbytes_to_sof_field));
	}


	/* Store the pointer to the caller-supplied framebuffer array */
	encoder->framebuffers = framebuffers;
	encoder->num_framebuffers = num_framebuffers;


	return IMX_VPU_DEC_RETURN_CODE_OK;

cleanup:
	IMX_VPU_FREE(encoder->internal_framebuffers, sizeof(FrameBuffer) * num_framebuffers);
	encoder->internal_framebuffers = NULL;

	return ret;
}


ImxVpuEncReturnCodes imx_vpu_enc_get_initial_info(ImxVpuEncoder *encoder, ImxVpuEncInitialInfo *info)
{
	RetCode enc_ret;
	ImxVpuEncReturnCodes ret;
	EncInitialInfo initial_info;

	assert(encoder != NULL);
	assert(info != NULL);

	enc_ret = vpu_EncGetInitialInfo(encoder->handle, &initial_info);
	ret = IMX_VPU_ENC_HANDLE_ERROR("could not get initial info", enc_ret);
	if (ret != IMX_VPU_ENC_RETURN_CODE_OK)
		return ret;

	info->framebuffer_alignment = 1;
	info->min_num_required_framebuffers = initial_info.minFrameBufferCount;
	if (info->min_num_required_framebuffers == 0)
		info->min_num_required_framebuffers = 1;

	/* Reserve extra framebuffers for the subsampled images */
	info->min_num_required_framebuffers += VPU_ENC_NUM_EXTRA_SUBSAMPLE_FRAMEBUFFERS;

	/* Generate out-of-band header data if necessary
	 * This data does not change during encoding, so
	 * it only has to be generated once */
	if ((ret = imx_vpu_enc_generate_header_data(encoder)) != IMX_VPU_ENC_RETURN_CODE_OK)
		return ret;

	return IMX_VPU_ENC_RETURN_CODE_OK;
}


void imx_vpu_enc_set_default_encoding_params(ImxVpuEncoder *encoder, ImxVpuEncParams *encoding_params)
{
	assert(encoding_params != NULL);

	IMXVPUAPI_UNUSED_PARAM(encoder);

	encoding_params->force_I_picture = 0;
	encoding_params->skip_picture = 0;
	encoding_params->enable_autoskip = 0;

	encoding_params->quant_param = 0;
}


void imx_vpu_enc_configure_bitrate(ImxVpuEncoder *encoder, unsigned int bitrate)
{
	int param;
	assert(encoder != NULL);
	param = bitrate;
	vpu_EncGiveCommand(encoder->handle, ENC_SET_BITRATE, &param);
}


void imx_vpu_enc_configure_min_intra_refresh(ImxVpuEncoder *encoder, unsigned int min_intra_refresh_num)
{
	int param;
	assert(encoder != NULL);
	if (encoder->codec_format != IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		/* MJPEG does not support this parameter */
		param = min_intra_refresh_num;
		vpu_EncGiveCommand(encoder->handle, ENC_SET_INTRA_MB_REFRESH_NUMBER, &param);
	}
}


void imx_vpu_enc_configure_intra_qp(ImxVpuEncoder *encoder, int intra_qp)
{
	assert(encoder != NULL);
	vpu_EncGiveCommand(encoder->handle, ENC_SET_INTRA_QP, &intra_qp);
}


ImxVpuEncReturnCodes imx_vpu_enc_encode(ImxVpuEncoder *encoder, ImxVpuPicture *picture, ImxVpuEncodedFrame *encoded_frame, ImxVpuEncParams *encoding_params, unsigned int *output_code)
{
#define GET_BITSTREAM_VIRT_ADDR(BITSTREAM_PHYS_ADDR) (encoder->bitstream_buffer_virtual_address + ((BITSTREAM_PHYS_ADDR) - encoder->bitstream_buffer_physical_address))

	ImxVpuEncReturnCodes ret;
	RetCode enc_ret;
	EncParam enc_param;
	EncOutputInfo enc_output_info;
	FrameBuffer source_framebuffer;
	uint8_t *encoded_frame_virt_addr, *encoded_frame_virt_addr_end;
	imx_vpu_phys_addr_t picture_phys_addr;
	uint8_t *write_ptr;
	BOOL timeout;

	ret = IMX_VPU_ENC_RETURN_CODE_OK;

	assert(encoder != NULL);
	assert(encoded_frame != NULL);
	assert(encoded_frame->data != NULL);
	assert(encoded_frame->data_size > 0);
	assert(encoding_params != NULL);
	assert(output_code != NULL);

	*output_code = 0;

	/* Get the physical address for the picture that shall be encoded
	 * and the virtual pointer to the output buffer */
	picture_phys_addr = imx_vpu_dma_buffer_get_physical_address(picture->framebuffer->dma_buffer);
	encoded_frame_virt_addr = encoded_frame->data;
	encoded_frame_virt_addr_end = encoded_frame->data + encoded_frame->data_size;
	write_ptr = encoded_frame_virt_addr;

	/* MJPEG frames always need JPEG headers, since each frame is an independent JPEG picture */
	if (encoder->codec_format == IMX_VPU_CODEC_FORMAT_MJPEG)
	{
		EncParamSet mjpeg_param;
		memset(&mjpeg_param, 0, sizeof(mjpeg_param));

		mjpeg_param.size = encoded_frame_virt_addr_end - write_ptr;
		mjpeg_param.pParaSet = write_ptr;

		vpu_EncGiveCommand(encoder->handle, ENC_GET_JPEG_HEADER, &mjpeg_param);
		IMX_VPU_LOG("added JPEG header with %d byte", mjpeg_param.size);

		write_ptr += mjpeg_param.size;

		*output_code |= IMX_VPU_ENC_OUTPUT_CODE_CONTAINS_HEADER;
	}

	IMX_VPU_LOG("encoding picture with physical address %" IMX_VPU_PHYS_ADDR_FORMAT, picture_phys_addr);

	/* Copy over data from the picture into the source_framebuffer
	 * structure, which is what vpu_EncStartOneFrame() expects
	 * as input */

	memset(&source_framebuffer, 0, sizeof(source_framebuffer));

	source_framebuffer.strideY = picture->framebuffer->y_stride;
	source_framebuffer.strideC = picture->framebuffer->cbcr_stride;

	/* Make sure the source framebuffer has an ID that is different
	 * to the IDs of the other, registered framebuffers */
	source_framebuffer.myIndex = encoder->num_framebuffers + 1;

	source_framebuffer.bufY = (PhysicalAddress)(picture_phys_addr + picture->framebuffer->y_offset);
	source_framebuffer.bufCb = (PhysicalAddress)(picture_phys_addr + picture->framebuffer->cb_offset);
	source_framebuffer.bufCr = (PhysicalAddress)(picture_phys_addr + picture->framebuffer->cr_offset);
	source_framebuffer.bufMvCol = (PhysicalAddress)(picture_phys_addr + picture->framebuffer->mvcol_offset);

	IMX_VPU_LOG("source framebuffer:  Y stride: %u  CbCr stride: %u", picture->framebuffer->y_stride, picture->framebuffer->cbcr_stride);


	/* Fill encoding parameters structure */

	memset(&enc_param, 0, sizeof(enc_param));

	enc_param.sourceFrame = &source_framebuffer;
	enc_param.forceIPicture = encoding_params->force_I_picture;
	enc_param.skipPicture = encoding_params->skip_picture;
	enc_param.quantParam = encoding_params->quant_param;
	enc_param.enableAutoSkip = encoding_params->enable_autoskip;


	/* Do the actual encoding */

	enc_ret = vpu_EncStartOneFrame(encoder->handle, &enc_param);
	ret = IMX_VPU_ENC_HANDLE_ERROR("could not start frame encoding", enc_ret);
	if (ret != IMX_VPU_ENC_RETURN_CODE_OK)
		goto finish;

	/* Wait for frame completion */
	{
		int cnt;

		IMX_VPU_LOG("waiting for encoding completion");

		/* Wait a few times, since sometimes, it takes more than
		 * one vpu_WaitForInt() call to cover the encoding interval */
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

	/* Retrieve information about the result of the encode process. Do so even if
	 * a timeout occurred. This is intentional, since according to the VPU docs,
	 * vpu_EncStartOneFrame() won't be usable again until vpu_EncGetOutputInfo()
	 * is called. In other words, the vpu_EncStartOneFrame() locks down some
	 * internals inside the VPU, and vpu_EncGetOutputInfo() releases them. */

	memset(&enc_output_info, 0, sizeof(enc_output_info));
	enc_ret = vpu_EncGetOutputInfo(encoder->handle, &enc_output_info);
	ret = IMX_VPU_ENC_HANDLE_ERROR("could not get output information", enc_ret);
	if (ret != IMX_VPU_ENC_RETURN_CODE_OK)
		goto finish;


	/* If a timeout occurred earlier, this is the correct time to abort
	 * encoding and return an error code, since vpu_EncGetOutputInfo()
	 * has been called, unlocking the VPU encoder calls. */
	if (timeout)
		return IMX_VPU_ENC_RETURN_CODE_TIMEOUT;


	{
		ImxVpuPicType pic_types[2];
		convert_pic_type(encoder->codec_format, enc_output_info.picType, FALSE, pic_types);
		encoded_frame->pic_type = pic_types[0];
	}


	IMX_VPU_LOG(
		"output info:  bitstreamBuffer %" IMX_VPU_PHYS_ADDR_FORMAT "  bitstreamSize %u  bitstreamWrapAround %d  skipEncoded %d  picType %d (%s)  numOfSlices %d",
		enc_output_info.bitstreamBuffer,
		enc_output_info.bitstreamSize,
		enc_output_info.bitstreamWrapAround,
		enc_output_info.skipEncoded,
		enc_output_info.picType, imx_vpu_picture_type_string(encoded_frame->pic_type),
		enc_output_info.numOfSlices
	);


	/* For h.264 and MPEG-4 streams, headers may have to be added */
	if ((encoder->codec_format == IMX_VPU_CODEC_FORMAT_H264) || (encoder->codec_format == IMX_VPU_CODEC_FORMAT_H264_MVC) || (encoder->codec_format == IMX_VPU_CODEC_FORMAT_MPEG4))
	{
		/* Add a header if at least one of these apply:
		 * 1. This is the first frame
		 * 2. I-frame generation was forced
		 * 3. Picture type is I or IDR
		 */
		BOOL add_header = encoder->first_frame || encoding_params->force_I_picture || (encoded_frame->pic_type == IMX_VPU_PIC_TYPE_IDR) || (encoded_frame->pic_type == IMX_VPU_PIC_TYPE_I);

		if (add_header)
		{
#define ADD_HEADER_DATA(HEADER_FIELD, DESCRIPTION) \
			do \
			{ \
				size_t size = encoder->headers.HEADER_FIELD ## _size; \
				memcpy(write_ptr, encoder->headers.HEADER_FIELD, size); \
				write_ptr += size; \
				IMX_VPU_LOG("added %s with %zu byte", (DESCRIPTION), size); \
			} \
			while (0)

			switch (encoder->codec_format)
			{
				case IMX_VPU_CODEC_FORMAT_H264:
				case IMX_VPU_CODEC_FORMAT_H264_MVC:
				{
					ADD_HEADER_DATA(h264_headers.sps_rbsp, "h.264 SPS RBSP");
					ADD_HEADER_DATA(h264_headers.pps_rbsp, "h.264 PPS RBSP");
					break;
				}

				case IMX_VPU_CODEC_FORMAT_MPEG4:
				{
					ADD_HEADER_DATA(mpeg4_headers.vos_header, "MPEG-4 VOS header");
					ADD_HEADER_DATA(mpeg4_headers.vis_header, "MPEG-4 VIS header");
					ADD_HEADER_DATA(mpeg4_headers.vol_header, "MPEG-4 VOL header");
					break;
				}

				default:
					break;
			}

			*output_code |= IMX_VPU_ENC_OUTPUT_CODE_CONTAINS_HEADER;
#undef ADD_HEADER_DATA
		}
	}


	/* Add this flag since the input picture has been successfully consumed */
	*output_code |= IMX_VPU_ENC_OUTPUT_CODE_INPUT_USED;

	/* Get the encoded data out of the bitstream buffer into the output buffer */
	if (enc_output_info.bitstreamBuffer != 0)
	{
		ptrdiff_t available_space = encoded_frame_virt_addr_end - write_ptr;
		uint8_t const *output_data_ptr = GET_BITSTREAM_VIRT_ADDR(enc_output_info.bitstreamBuffer);

		if (available_space < (ptrdiff_t)(enc_output_info.bitstreamSize))
		{
			IMX_VPU_ERROR(
				"insufficient space in output buffer for encoded data: need %u byte, got %td",
				enc_output_info.bitstreamSize,
				available_space
			);
			ret = IMX_VPU_ENC_RETURN_CODE_ERROR;

			goto finish;
		}

		memcpy(write_ptr, output_data_ptr, enc_output_info.bitstreamSize);
		IMX_VPU_LOG("added main encoded frame data with %u byte", enc_output_info.bitstreamSize);
		write_ptr += enc_output_info.bitstreamSize;

		*output_code |= IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE;
	}

	encoded_frame->data_size = write_ptr - encoded_frame->data;

	/* Since the encoder does not perform any kind of delay
	 * or reordering, this is appropriate, because in that
	 * case, one input frame always immediately leads to
	 * one output frame */
	encoded_frame->context = picture->context;

	encoder->first_frame = FALSE;
finish:
	return ret;

#undef GET_BITSTREAM_VIRT_ADDR
}
