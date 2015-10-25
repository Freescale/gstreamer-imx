/* imxvpuapi API library for the Freescale i.MX SoC
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

#include <string.h>
#include <stdlib.h>
#include "imxvpuapi.h"
#include "imxvpuapi_priv.h"


/**************************************************/
/******* ALLOCATOR STRUCTURES AND FUNCTIONS *******/
/**************************************************/


ImxVpuDMABuffer* wrapped_dma_buffer_allocator_allocate(ImxVpuDMABufferAllocator *allocator, size_t size, unsigned int alignment, unsigned int flags)
{
	/* This allocator is used for wrapping existing DMA memory. Therefore,
	 * it doesn't actually allocate anything. */
	IMXVPUAPI_UNUSED_PARAM(allocator);
	IMXVPUAPI_UNUSED_PARAM(size);
	IMXVPUAPI_UNUSED_PARAM(alignment);
	IMXVPUAPI_UNUSED_PARAM(flags);
	return NULL;
}


void wrapped_dma_buffer_allocator_deallocate(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	IMXVPUAPI_UNUSED_PARAM(buffer);
}


uint8_t* wrapped_dma_buffer_allocator_map(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer, unsigned int flags)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	ImxVpuWrappedDMABuffer *wrapped_buf = (ImxVpuWrappedDMABuffer *)(buffer);
	return (wrapped_buf->map != NULL) ? wrapped_buf->map(wrapped_buf, flags) : NULL;
}


void wrapped_dma_buffer_allocator_unmap(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	ImxVpuWrappedDMABuffer *wrapped_buf = (ImxVpuWrappedDMABuffer *)(buffer);
	if (wrapped_buf->unmap != NULL)
		wrapped_buf->unmap(wrapped_buf);
}


int wrapped_dma_buffer_allocator_get_fd(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	return ((ImxVpuWrappedDMABuffer *)(buffer))->fd;
}


imx_vpu_phys_addr_t wrapped_dma_buffer_allocator_get_physical_address(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	return ((ImxVpuWrappedDMABuffer *)(buffer))->physical_address;
}


size_t wrapped_dma_buffer_allocator_get_size(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer)
{
	IMXVPUAPI_UNUSED_PARAM(allocator);
	return ((ImxVpuWrappedDMABuffer *)(buffer))->size;
}


static ImxVpuDMABufferAllocator wrapped_dma_buffer_allocator =
{
	wrapped_dma_buffer_allocator_allocate,
	wrapped_dma_buffer_allocator_deallocate,
	wrapped_dma_buffer_allocator_map,
	wrapped_dma_buffer_allocator_unmap,
	wrapped_dma_buffer_allocator_get_fd,
	wrapped_dma_buffer_allocator_get_physical_address,
	wrapped_dma_buffer_allocator_get_size
};




ImxVpuDMABuffer* imx_vpu_dma_buffer_allocate(ImxVpuDMABufferAllocator *allocator, size_t size, unsigned int alignment, unsigned int flags)
{
	return allocator->allocate(allocator, size, alignment, flags);
}


void imx_vpu_dma_buffer_deallocate(ImxVpuDMABuffer *buffer)
{
	buffer->allocator->deallocate(buffer->allocator, buffer);
}


uint8_t* imx_vpu_dma_buffer_map(ImxVpuDMABuffer *buffer, unsigned int flags)
{
	return buffer->allocator->map(buffer->allocator, buffer, flags);
}


void imx_vpu_dma_buffer_unmap(ImxVpuDMABuffer *buffer)
{
	buffer->allocator->unmap(buffer->allocator, buffer);
}


int imx_vpu_dma_buffer_get_fd(ImxVpuDMABuffer *buffer)
{
	return buffer->allocator->get_fd(buffer->allocator, buffer);
}


imx_vpu_phys_addr_t imx_vpu_dma_buffer_get_physical_address(ImxVpuDMABuffer *buffer)
{
	return buffer->allocator->get_physical_address(buffer->allocator, buffer);
}


size_t imx_vpu_dma_buffer_get_size(ImxVpuDMABuffer *buffer)
{
	return buffer->allocator->get_size(buffer->allocator, buffer);
}


void imx_vpu_init_wrapped_dma_buffer(ImxVpuWrappedDMABuffer *buffer)
{
	memset(buffer, 0, sizeof(ImxVpuWrappedDMABuffer));
	buffer->parent.allocator = &wrapped_dma_buffer_allocator;
}




static void* default_heap_alloc_fn(size_t const size, void *context, char const *file, int const line, char const *fn)
{
	IMXVPUAPI_UNUSED_PARAM(context);
	IMXVPUAPI_UNUSED_PARAM(file);
	IMXVPUAPI_UNUSED_PARAM(line);
	IMXVPUAPI_UNUSED_PARAM(fn);
	return malloc(size);
}

static void default_heap_free_fn(void *memblock, size_t const size, void *context, char const *file, int const line, char const *fn)
{
	IMXVPUAPI_UNUSED_PARAM(context);
	IMXVPUAPI_UNUSED_PARAM(size);
	IMXVPUAPI_UNUSED_PARAM(file);
	IMXVPUAPI_UNUSED_PARAM(line);
	IMXVPUAPI_UNUSED_PARAM(fn);
	free(memblock);
}

void *imx_vpu_cur_heap_alloc_context;
ImxVpuHeapAllocFunc imx_vpu_cur_heap_alloc_fn = default_heap_alloc_fn;
ImxVpuHeapFreeFunc imx_vpu_cur_heap_free_fn = default_heap_free_fn;

void imx_vpu_set_heap_allocator_functions(ImxVpuHeapAllocFunc heap_alloc_fn, ImxVpuHeapFreeFunc heap_free_fn, void *context)
{
	imx_vpu_cur_heap_alloc_context = context;
	if ((heap_alloc_fn == NULL) || (heap_free_fn == NULL))
	{
		imx_vpu_cur_heap_alloc_fn = default_heap_alloc_fn;
		imx_vpu_cur_heap_free_fn = default_heap_free_fn;
	}
	else
	{
		imx_vpu_cur_heap_alloc_fn = heap_alloc_fn;
		imx_vpu_cur_heap_free_fn = heap_free_fn;
	}
}




/***********************/
/******* LOGGING *******/
/***********************/


static void default_logging_fn(ImxVpuLogLevel level, char const *file, int const line, char const *fn, const char *format, ...)
{
	IMXVPUAPI_UNUSED_PARAM(level);
	IMXVPUAPI_UNUSED_PARAM(file);
	IMXVPUAPI_UNUSED_PARAM(line);
	IMXVPUAPI_UNUSED_PARAM(fn);
	IMXVPUAPI_UNUSED_PARAM(format);
}

ImxVpuLogLevel imx_vpu_cur_log_level_threshold = IMX_VPU_LOG_LEVEL_ERROR;
ImxVpuLoggingFunc imx_vpu_cur_logging_fn = default_logging_fn;

void imx_vpu_set_logging_function(ImxVpuLoggingFunc logging_fn)
{
	imx_vpu_cur_logging_fn = (logging_fn != NULL) ? logging_fn : default_logging_fn;
}

void imx_vpu_set_logging_threshold(ImxVpuLogLevel threshold)
{
	imx_vpu_cur_log_level_threshold = threshold;
}




/******************************************************/
/******* MISCELLANEOUS STRUCTURES AND FUNCTIONS *******/
/******************************************************/


char const *imx_vpu_color_format_string(ImxVpuColorFormat color_format)
{
	switch (color_format)
	{
		case IMX_VPU_COLOR_FORMAT_YUV420: return "YUV 4:2:0";
		case IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL: return "YUV 4:2:2 horizontal";
		case IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL: return "YUV 2:2:4 vertical";
		case IMX_VPU_COLOR_FORMAT_YUV444: return "YUV 4:4:4";
		case IMX_VPU_COLOR_FORMAT_YUV400: return "YUV 4:0:0 (8-bit grayscale)";
		default:
			return "<unknown>";
	}
}


char const *imx_vpu_frame_type_string(ImxVpuFrameType frame_type)
{
	switch (frame_type)
	{
		case IMX_VPU_FRAME_TYPE_I: return "I";
		case IMX_VPU_FRAME_TYPE_P: return "P";
		case IMX_VPU_FRAME_TYPE_B: return "B";
		case IMX_VPU_FRAME_TYPE_IDR: return "IDR";
		case IMX_VPU_FRAME_TYPE_BI: return "BI";
		case IMX_VPU_FRAME_TYPE_SKIP: return "SKIP";
		default: return "<unknown>";
	}
}
