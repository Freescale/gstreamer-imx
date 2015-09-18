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

#ifndef IMXVPUAPI_H
#define IMXVPUAPI_H

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif




/* This library provides a high-level interface for controlling the Freescale
 * i.MX VPU en/decoder.
 *
 * Note that the functions are _not_ thread safe. If they may be called from
 * different threads, you must make sure they are surrounded by a mutex lock.
 * It is recommended to use one global mutex for the imx_vpu_*_load()/unload()
 * functions, and another de/encoder instance specific mutex for all of the other
 * calls. */




/**************************************************/
/******* ALLOCATOR STRUCTURES AND FUNCTIONS *******/
/**************************************************/


#define IMX_VPU_PHYS_ADDR_FORMAT "#lx"
typedef unsigned long imx_vpu_phys_addr_t;


typedef enum
{
	IMX_VPU_ALLOCATION_FLAG_WRITECOMBINE = (1UL << 0),
	IMX_VPU_ALLOCATION_FLAG_UNCACHED     = (1UL << 1)
}
ImxVpuAllocationFlags;


typedef enum
{
	IMX_VPU_MAPPING_FLAG_WRITE   = (1UL << 0),
	IMX_VPU_MAPPING_FLAG_READ    = (1UL << 1),
	IMX_VPU_MAPPING_FLAG_DISCARD = (1UL << 2)
}
ImxVpuMappingFlags;


typedef struct _ImxVpuDMABuffer ImxVpuDMABuffer;
typedef struct _ImxVpuWrappedDMABuffer ImxVpuWrappedDMABuffer;
typedef struct _ImxVpuDMABufferAllocator ImxVpuDMABufferAllocator;

/* ImxVpuDMABufferAllocator:
 *
 * This structure contains function pointers (referred to as "vfuncs") which define an allocator for
 * DMA buffers (= physically contiguous memory blocks). It is possible to define a custom allocator,
 * which is useful for tracing memory allocations, and for hooking up any existing allocation mechanisms,
 * such as ION or CMA.
 *
 * Older allocators like the VPU ones unfortunately work with physical addresses directly, and do not support
 * DMA-BUF or the like. To keep compatible with these older allocators and allowing for newer and better
 * methods, both physical addresses and FDs are supported by this API. Typically, an allocator allows for
 * one of them. If an allocator does not support FDs, @get_fd must return -1. If it does not support physical
 * addresses, then the physical address returned by @get_physical_address must be set to zero.
 *
 * The vfuncs are:
 *
 * @allocate: Allocates a DMA buffer. "size" is the size of the buffer in bytes. "alignment" is the address
 *            alignment in bytes. An alignment of 1 or 0 means that no alignment is required.
 *            "flags" is a bitwise OR combination of flags (or 0 if no flags are used, in which case
 *            cached pages are used by default).
 *            If allocation fails, NULL is returned.
 *
 * @deallocate: Deallocates a DMA buffer. The buffer must have been allocated with the same allocator.
 *
 * @map: Maps a DMA buffer to the local address space, and returns the virtual address to this space.
 *       "flags" is a bitwise OR combination of flags (or 0 if no flags are used, in which case it will map
 *       in regular read/write mode).
 *
 * @unmap: Unmaps a DMA buffer. @map and @unmap must contain an internal counter, to allow for multiple
 *       map/unmap calls. Only if that counter reaches zero again - in other words, once @unmap is called as
 *       many times as @map was called - unmapping shall actually occur.
 *
 * @get_fd: Gets the file descriptor associated with the DMA buffer. This is the preferred way of interacting
 *          with DMA buffers. If the underlying allocator does not support FDs, this function returns -1.
 *
 * @get_physical_address: Gets the physical address associated with the DMA buffer. This address points to the
 *                        start of the buffer in the physical address space. If no physical addresses are
 *                        supported by the allocator, this function returns 0.
 *
 * @get_size: Returns the size of the buffer, in bytes.
 *
 * The vfuncs @get_fd, @get_physical_address, and @get_size can also be used while the buffer is mapped. */
struct _ImxVpuDMABufferAllocator
{
	ImxVpuDMABuffer* (*allocate)(ImxVpuDMABufferAllocator *allocator, size_t size, unsigned int alignment, unsigned int flags);
	void (*deallocate)(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer);

	/* Mapping/unmapping must use some kind of internal counter, to allow for multiple map() calls */
	uint8_t* (*map)(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer, unsigned int flags);
	void (*unmap)(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer);

	int (*get_fd)(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer);
	imx_vpu_phys_addr_t (*get_physical_address)(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer);

	size_t (*get_size)(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer);
};


/* ImxVpuDMABuffer:
 *
 * Opaque object containing a DMA buffer. Its structure is defined by the allocator which created the object. */
struct _ImxVpuDMABuffer
{
	ImxVpuDMABufferAllocator *allocator;
};


/* ImxVpuWrappedDMABuffer:
 *
 * Structure for wrapping existing DMA buffers. This is useful for interfacing with existing buffers
 * that were not allocated by imxvpuapi.
 *
 * fd, physical_address, and size are filled with user-defined values. If the DMA buffer is referred to
 * by a file descriptor, then fd must be set to the descriptor value, otherwise fd must be set to -1.
 * If the buffer is referred to by a physical address, then physical_address must be set to that address,
 * otherwise physical_address must be 0.
 * map_func and unmap_func are used in the imx_vpu_dma_buffer_map() / imx_vpu_dma_buffer_unmap() calls.
 * If these function pointers are NULL, no mapping will be done. NOTE: imx_vpu_dma_buffer_map() will return
 * a NULL pointer in this case.
 */
struct _ImxVpuWrappedDMABuffer
{
	ImxVpuDMABuffer parent;

	uint8_t* (*map_func)(ImxVpuWrappedDMABuffer *wrapped_dma_buffer, unsigned int flags);
	void (*unmap_func)(ImxVpuWrappedDMABuffer *wrapped_dma_buffer);

	int fd;
	imx_vpu_phys_addr_t physical_address;
	size_t size;
};


/* Convenience functions which call the corresponding vfuncs in the allocator */
ImxVpuDMABuffer* imx_vpu_dma_buffer_allocate(ImxVpuDMABufferAllocator *allocator, size_t size, unsigned int alignment, unsigned int flags);
void imx_vpu_dma_buffer_deallocate(ImxVpuDMABuffer *buffer);
uint8_t* imx_vpu_dma_buffer_map(ImxVpuDMABuffer *buffer, unsigned int flags);
void imx_vpu_dma_buffer_unmap(ImxVpuDMABuffer *buffer);
int imx_vpu_dma_buffer_get_fd(ImxVpuDMABuffer *buffer);
imx_vpu_phys_addr_t imx_vpu_dma_buffer_get_physical_address(ImxVpuDMABuffer *buffer);
size_t imx_vpu_dma_buffer_get_size(ImxVpuDMABuffer *buffer);

void imx_vpu_init_wrapped_dma_buffer(ImxVpuWrappedDMABuffer *buffer);


/* Heap allocation function for virtual memory blocks internally allocated by imxvpuapi.
 * These have nothing to do with the DMA buffer allocation interface defined above.
 * By default, malloc/free are used. */
typedef void* (*ImxVpuHeapAllocFunc)(size_t const size, void *context);
typedef void (*ImxVpuHeapFreeFunc)(void *memblock, size_t const size, void *context);

/* This function allows for setting custom heap allocators, which are used to create internal heap blocks.
 * The heap allocator referred to by "heap_alloc_fn" must return NULL if allocation fails.
 * "context" is a user-defined value, which is passed on unchanged to the allocator functions.
 * Calling this function with either "heap_alloc_fn" or "heap_free_fn" set to NULL resets the internal
 * pointers to use malloc and free (the default allocators). */
void imx_vpu_set_heap_allocator_functions(ImxVpuHeapAllocFunc heap_alloc_fn, ImxVpuHeapFreeFunc heap_free_fn, void *context);




/***********************/
/******* LOGGING *******/
/***********************/


typedef enum
{
	IMX_VPU_LOG_LEVEL_ERROR = 0,
	IMX_VPU_LOG_LEVEL_WARNING = 1,
	IMX_VPU_LOG_LEVEL_INFO = 2,
	IMX_VPU_LOG_LEVEL_DEBUG = 3,
	IMX_VPU_LOG_LEVEL_LOG = 4,
	IMX_VPU_LOG_LEVEL_TRACE = 5
}
ImxVpuLogLevel;

typedef void (*ImxVpuLoggingFunc)(ImxVpuLogLevel level, char const *file, int const line, char const *fn, const char *format, ...);

/* Defines the threshold for logging. Logs with lower priority are discarded.
 * By default, the threshold is set to IMX_VPU_LOG_LEVEL_INFO. */
void imx_vpu_set_logging_threshold(ImxVpuLogLevel threshold);

/* Defines a custom logging function.
 * If logging_fn is NULL, logging is disabled. This is the default value. */
void imx_vpu_set_logging_function(ImxVpuLoggingFunc logging_fn);




/******************************************************/
/******* MISCELLANEOUS STRUCTURES AND FUNCTIONS *******/
/******************************************************/


typedef enum
{
	IMX_VPU_PIC_TYPE_UNKNOWN = 0,
	IMX_VPU_PIC_TYPE_I,
	IMX_VPU_PIC_TYPE_P,
	IMX_VPU_PIC_TYPE_B,
	IMX_VPU_PIC_TYPE_IDR,
	IMX_VPU_PIC_TYPE_BI,
	IMX_VPU_PIC_TYPE_SKIP
}
ImxVpuPicType;


typedef enum
{
	IMX_VPU_FIELD_TYPE_UNKNOWN = 0,
	IMX_VPU_FIELD_TYPE_NO_INTERLACING,
	IMX_VPU_FIELD_TYPE_TOP_FIRST,
	IMX_VPU_FIELD_TYPE_BOTTOM_FIRST,
	IMX_VPU_FIELD_TYPE_TOP_ONLY,
	IMX_VPU_FIELD_TYPE_BOTTOM_ONLY
}
ImxVpuFieldType;


typedef enum
{
	IMX_VPU_CODEC_FORMAT_MPEG2 = 0, /* includes MPEG1 */
	IMX_VPU_CODEC_FORMAT_MPEG4,
	IMX_VPU_CODEC_FORMAT_H263,
	IMX_VPU_CODEC_FORMAT_H264,
	IMX_VPU_CODEC_FORMAT_H264_MVC, // TODO: change MVC to a flag in the openparams
	IMX_VPU_CODEC_FORMAT_WMV3,
	IMX_VPU_CODEC_FORMAT_WVC1,
	IMX_VPU_CODEC_FORMAT_MJPEG,
	IMX_VPU_CODEC_FORMAT_VP8
	/* XXX others will be added when the firmware supports them */
}
ImxVpuCodecFormat;


typedef enum
{
	/* planar 4:2:0; if the chroma_interleave parameter is 1, the corresponding format is NV12, otherwise it is I420 */
	IMX_VPU_COLOR_FORMAT_YUV420            = 0,
	/* planar 4:2:2; if the chroma_interleave parameter is 1, the corresponding format is NV16 */
	IMX_VPU_COLOR_FORMAT_YUV422_HORIZONTAL = 1,
	/* 4:2:2 vertical, actually 2:2:4 (according to the VPU docs); no corresponding format known for the chroma_interleave=1 case */
	/* NOTE: this format is rarely used, and has only been seen in a few JPEG files */
	IMX_VPU_COLOR_FORMAT_YUV422_VERTICAL   = 2,
	/* planar 4:4:4; if the chroma_interleave parameter is 1, the corresponding format is NV24 */
	IMX_VPU_COLOR_FORMAT_YUV444            = 3,
	IMX_VPU_COLOR_FORMAT_YUV400            = 4  /* 8-bit grayscale */
}
ImxVpuColorFormat;


/* Framebuffers are picture containers, and are used both for en- and decoding. */
typedef struct
{
	/* Stride of the Y and of the Cb&Cr components.
	 * Specified in bytes. */
	unsigned int y_stride, cbcr_stride;

	/* DMA buffer which contains the pixels. */
	ImxVpuDMABuffer *dma_buffer;

	/* These define the starting offsets of each component
	 * relative to the start of the buffer. Specified in bytes.
	 *
	 * mvcol is the "co-located motion vector" data. It is
	 * not used by the encoder. */
	size_t
		y_offset,
		cb_offset,
		cr_offset,
		mvcol_offset;

	/* User-defined pointer. The library does not touch this value.
	 * Not to be confused with the context fields of ImxVpuEncodedFrame
	 * and ImxVpuPicture.
	 * This can be used for example to identify which framebuffer out of
	 * the initially allocated pool was used by the VPU to contain a frame.
	 */
	void *context;

	/* Set to 1 if the framebuffer was already marked as displayed. This is for
	 * internal use only. Not to be read or written from the outside. */
	int already_marked;

	/* Internal, implementation-defined data. Do not modify. */
	void *internal;
}
ImxVpuFramebuffer;


/* Structure containing details about encoded frames. */
typedef struct
{
	/* When decoding, set virtual_address. It must point to the memory block
	 * that contains encoded frame data.
	 * When decoding, and drain mode is enabled, set virtual_address to NULL.
	 * When encoding, set dma_buffer. This DMA buffer will contain the
	 * resulting encoded frame. */
	union
	{
		uint8_t *virtual_address;
		ImxVpuDMABuffer *dma_buffer;
	}
	data;

	/* Size of the encoded data, in bytes. When decoding, this is set
	 * by the user. When encoding, the VPU sets this. */
	unsigned int data_size;

	/* Pointer to out-of-band codec/header data. If such data exists,
	 * specify the pointer to the memory block containing the data,
	 * as well as the size of the memory block (in bytes).
	 * Set pointer and size for every encoded frame when decoding.
	 * If no such data exists or is required, or if drain mode is enabled,
	 * the pointer must be NULL, the size must be 0. Not used by the encoder. */
	uint8_t *codec_data;
	unsigned int codec_data_size;

	/* User-defined pointer. The library does not touch this value.
	 * This pointer and the one from the corresponding
	 * picture will have the same value. The library will
	 * pass then through.
	 * It can be used to identify which picture is associated with
	 * this encoded frame for example. */
	void *context;
}
ImxVpuEncodedFrame;


/* Structure containing details about unencoded frames (also called "pictures"). */
typedef struct
{
	/* When decoding: pointer to the framebuffer containing the decoded picture.
	 * When encoding: pointer to the framebuffer containing the picture to be encoded.
	 * Must always be valid. */
	ImxVpuFramebuffer *framebuffer;

	/* Picture type (I, P, B, ..) ; unused by the encoder */
	ImxVpuPicType pic_type;

	/* Interlacing field type (top-first, bottom-first..); unused by the encoder */
	ImxVpuFieldType field_type;

	/* User-defined pointer. The library does not touch this value.
	 * This pointer and the one from the corresponding
	 * encoded frame will have the same value. The library will
	 * pass then through.
	 * It can be used to identify which picture is associated with
	 * this encoded frame for example. */
	void *context;
}
ImxVpuPicture;


/* Structure used together with @imx_vpu_calc_framebuffer_sizes */
typedef struct
{
	/* Frame width and height, aligned to the 16-pixel boundary required by the VPU. */
	unsigned int aligned_frame_width, aligned_frame_height;

	/* Stride sizes, in bytes, with alignment applied. The Cb and Cr planes always
	 * use the same stride, so they share the same value. */
	unsigned int y_stride, cbcr_stride;

	/* Required DMA memory size for the Y,Cb,Cr planes and the MvCol data, in bytes.
	 * The Cb and Cr planes always are of the same size, so they share the same value. */
	unsigned int y_size, cbcr_size, mvcol_size;

	/* Total required size of a framebuffer's DMA buffer, in bytes. This value includes
	 * the sizes of all planes, the MvCol data, and extra bytes for alignment and padding.
	 * This value must be used when allocating DMA buffers for decoder framebuffers. */
	unsigned int total_size;

	/* This corresponds to the other chroma_interleave values used in imxvpuapi.
	 * It is stored here to allow other functions to select the correct offsets. */
	int chroma_interleave;
}
ImxVpuFramebufferSizes;


/* Convenience function which calculates various sizes out of the given width & height and color format.
 * The results are stored in "calculated_sizes". The given frame width and height will be aligned if
 * they aren't already, and the aligned value will be stored in calculated_sizes. Width & height must be
 * nonzero. The calculated_sizes pointer must also be non-NULL. framebuffer_alignment is an alignment
 * value for the sizes of the Y/U/V planes. 0 or 1 mean no alignment. uses_interlacing is set to 1
 * if interlacing is to be used, 0 otherwise. chroma_interleave is set to 1 if a shared CbCr chroma
 * plane is to be used, 0 if Cb and Cr shall use separate planes. */
void imx_vpu_calc_framebuffer_sizes(ImxVpuColorFormat color_format, unsigned int frame_width, unsigned int frame_height, unsigned int framebuffer_alignment, int uses_interlacing, int chroma_interleave, ImxVpuFramebufferSizes *calculated_sizes);

/* Convenience function which fills fields of the ImxVpuFramebuffer structure, based on data from "calculated_sizes".
 * The specified DMA buffer and context pointer are also set. */
void imx_vpu_fill_framebuffer_params(ImxVpuFramebuffer *framebuffer, ImxVpuFramebufferSizes *calculated_sizes, ImxVpuDMABuffer *fb_dma_buffer, void* context);

char const *imx_vpu_color_format_string(ImxVpuColorFormat color_format);




/************************************************/
/******* DECODER STRUCTURES AND FUNCTIONS *******/
/************************************************/


/* How to use the decoder (error handling omitted for clarity):
 * 1. Call imx_vpu_dec_load()
 * 2. Call imx_vpu_dec_get_bitstream_buffer_info(), and allocate a DMA buffer
 *    with the given size and alignment. This is the minimum required size.
 *    The buffer can be larger, but must not be smaller than the given size.
 * 3. Fill an instance of ImxVpuDecOpenParams with the values specific to the
 *    input data. In most cases, one wants to set enable_frame_reordering to 1
 *    with h.264 data here. Width & height can be zero for formats which carry size
 *    information inside their bitstreams and/or out-of-band codec data.
 * 4. Call imx_vpu_dec_open(), passing in a pointer to the filled ImxVpuDecOpenParams
 *    instance, the DMA buffer of the bitstream DMA buffer which was allocated in step 2,
 *    a callback of type imx_vpu_dec_new_initial_info_callback, and a user defined pointer
 *    that is passed to the callback (if not needed, just set it to NULL).
 * 5. Call imx_vpu_dec_decode(), and push data to it. Once initial information about the
 *    bitstream becomes available, the callback from step 4 is invoked.
 * 6. Inside the callback, the new initial info is available. The new_initial_info pointer
 *    is never NULL. In this callback, framebuffers are allocated and registered, as
 *    explained in the next steps. Steps 7-9 are performed inside the callback.
 * 7. (Optional) Perform the necessary size and alignment calculations by calling
 *    imx_vpu_calc_framebuffer_sizes(). Pass in either the frame width & height from
 *    ImxVpuDecInitialInfo , or some explicit values that were determined externally.
 * 8. Create an array of at least as many ImxVpuFramebuffer instances as specified in
 *    min_num_required_framebuffers. Each instance must point to a DMA buffer that is big
 *    enough to hold a frame. If step 7 was performed, allocating as many bytes as indicated
 *    by total_size is enough. Make sure the Y,Cb,Cr,MvCol offsets in each ImxVpuFramebuffer
 *    instance are valid. Using the imx_vpu_fill_framebuffer_params() convenience function
 *    for this is recommended.
 * 9. Call imx_vpu_dec_register_framebuffers() and pass in the ImxVpuFramebuffer array
 *    and the number of ImxVpuFramebuffer instances.
 *    This should be the last action in the callback.
 * 10. Continue calling imx_vpu_dec_decode(). The virtual address in encoded_frame
 *     must not be NULL.
 *     If the IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE flag is set in the output code,
 *     call imx_vpu_dec_get_decoded_picture() with a pointer to an ImxVpuPicture instance
 *     which gets filled with information about the decoded picture. Once the decoded picture
 *     has been processed by the user, imx_vpu_dec_mark_framebuffer_as_displayed() must be
 *     called to let the decoder know that the framebuffer is available for storing new
 *     decoded pictures again.
 *     If IMX_VPU_DEC_OUTPUT_CODE_DROPPED is set, you can call
 *     imx_vpu_dec_get_dropped_frame_context() to retrieve the context field
 *     of the dropped frame. If IMX_VPU_DEC_OUTPUT_CODE_EOS is set, stop playback and close
 *     the decoder.
 * 11. In case a flush/reset is desired (typically after seeking), call imx_vpu_dec_flush().
 *     Note that any internal context pointers from the en/decoded frames will be
 *     set to NULL after this call (this is the only exception where the library modifies
 *     the context fields).
 * 12. When there is no more incoming data, and pending decoded frames need to be retrieved
 *     from the decoder, enable drain mode with imx_vpu_dec_enable_drain_mode(). This is
 *     typically necessary when the data source reached its end, playback is finishing, and
 *     there is a delay of N frames at the beginning.
 *     After this call, continue calling imx_vpu_dec_decode() to retrieve the pending
 *     decoded pictures, but the virtual address of encoded_frame and the codec data pointer
 *     must be NULL.
 *     As in step 10, if IMX_VPU_DEC_OUTPUT_CODE_EOS is set, stop playback, close the decoder.
 * 13. After playback is finished, close the decoder with imx_vpu_dec_close().
 * 14. Deallocate framebuffer memory blocks and the bitstream buffer memory block.
 * 15. Call imx_vpu_dec_unload().
 *
 * Step 15 should only be called if no more playback sessions will occur.
 *
 * In situations where decoding and display of decoded frames happen in different threads, it
 * is necessary to wait until decoding is possible. imx_vpu_dec_check_if_can_decode() is used
 * for this purpose. This needs to be done in steps 5 and 10. Example pseudo code:
 *
 *   mutex_lock(&mutex);
 *
 *   while (!imx_vpu_dec_check_if_can_decode(decode))
 *     condition_wait(&condition_variable, &mutex);
 *
 *   imx_vpu_dec_decode_frame(decoder, encoded_frame, &output_code);
 *   ...
 *
 *   mutex_unlock(&mutex);
 */


typedef struct _ImxVpuDecoder ImxVpuDecoder;


typedef enum
{
	IMX_VPU_DEC_RETURN_CODE_OK = 0,
	IMX_VPU_DEC_RETURN_CODE_ERROR,
	IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS,
	IMX_VPU_DEC_RETURN_CODE_INVALID_HANDLE,
	IMX_VPU_DEC_RETURN_CODE_INVALID_FRAMEBUFFER,
	IMX_VPU_DEC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS,
	IMX_VPU_DEC_RETURN_CODE_INVALID_STRIDE,
	IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE,
	IMX_VPU_DEC_RETURN_CODE_TIMEOUT,
	IMX_VPU_DEC_RETURN_CODE_ALREADY_CALLED
}
ImxVpuDecReturnCodes;


typedef enum
{
	IMX_VPU_DEC_OUTPUT_CODE_INPUT_USED                   = (1UL << 0),
	IMX_VPU_DEC_OUTPUT_CODE_EOS                          = (1UL << 1),
	IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE    = (1UL << 2),
	IMX_VPU_DEC_OUTPUT_CODE_DROPPED                      = (1UL << 3),
	IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_OUTPUT_FRAMES     = (1UL << 4),
	IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA        = (1UL << 5),
	IMX_VPU_DEC_OUTPUT_CODE_RESOLUTION_CHANGED           = (1UL << 6),
	IMX_VPU_DEC_OUTPUT_CODE_DECODE_ONLY                  = (1UL << 7),
	IMX_VPU_DEC_OUTPUT_CODE_INTERNAL_RESET               = (1UL << 8)
}
ImxVpuDecOutputCodes;


/* Structure used together with @imx_vpu_dec_open */
typedef struct
{
	ImxVpuCodecFormat codec_format;

	/* Set to 1 if frame reordering shall be done by the VPU, 0 otherwise.
	 * Useful only for formats which can reorder frames, like h.264. */
	int enable_frame_reordering;

	/* These are necessary with some formats which do not store the width
	 * and height in the bitstream. If the format does store them, these
	 * values can be set to zero. */
	unsigned int frame_width, frame_height;

	/* If this is 1, then Cb and Cr are interleaved in one shared chroma
	 * plane, otherwise they are separated in their own planes.
	 * See the ImxVpuColorFormat documentation for the consequences of this. */
	int chroma_interleave;
}
ImxVpuDecOpenParams;


/* Structure used together with @imx_vpu_dec_new_initial_info_callback */
typedef struct
{
	/* Width of height of frames, in pixels. Note: it is not guaranteed that
	 * these values are aligned to a 16-pixel boundary (which is required
	 * for VPU framebuffers). These are the width and height of the frame
	 * with actual pixel content. It may be a subset of the total frame,
	 * in case these sizes need to be aligned. In that case, there are
	 * padding columns to the right, and padding rows below the frames. */
	unsigned int frame_width, frame_height;
	/* Frame rate ratio. */
	unsigned int frame_rate_numerator, frame_rate_denominator;

	/* Caller must register at least this many framebuffers
	 * with the decoder. */
	unsigned int min_num_required_framebuffers;

	/* Color format of the decoded frames. For codec formats
	 * other than motion JPEG, this value will always be
	 * IMX_VPU_COLOR_FORMAT_YUV420. */
	ImxVpuColorFormat color_format;

	/* 0 = no interlacing, 1 = interlacing. */
	int interlacing;

	/* Physical framebuffer addresses must be aligned to this value. */
	unsigned int framebuffer_alignment;
}
ImxVpuDecInitialInfo;


/* Callback for handling new ImxVpuDecInitialInfo data. This is called when new
 * information about the bitstream becomes available. output_code is useful
 * to check why this callback was invoked. IMX_VPU_DEC_OUTPUT_CODE_INITIAL_INFO_AVAILABLE
 * is always set. If IMX_VPU_DEC_OUTPUT_CODE_RESOLUTION_CHANGED is also set,
 * it means that this callback was called before, and not got called again, because
 * the resolution is now different. Either way, every time this callback gets called,
 * new framebuffers should be allocated and registered with imx_vpu_dec_register_framebuffers().
 * user_data is a user-defined pointer that is passed to this callback. It has the same value
 * as the callback_user_data pointer from the imx_vpu_dec_open() call.
 * It returns 0 if something failed, nonzero if successful. */
typedef int (*imx_vpu_dec_new_initial_info_callback)(ImxVpuDecoder *decoder, ImxVpuDecInitialInfo *new_initial_info, unsigned int output_code, void *user_data);


/* Returns a human-readable description of the error code.
 * Useful for logging. */
char const * imx_vpu_dec_error_string(ImxVpuDecReturnCodes code);

/* These two functions load/unload the decoder. Due to an internal reference
 * counter, it is safe to call these functions more than once. However, the
 * number of unload() calls must match the number of load() calls.
 *
 * The decoder must be loaded before doing anything else with it.
 * Similarly, the decoder must not be unloaded before all decoder activities
 * have been finished. This includes opening/decoding decoder instances. */
ImxVpuDecReturnCodes imx_vpu_dec_load(void);
ImxVpuDecReturnCodes imx_vpu_dec_unload(void);

/* Convenience predefined allocator for allocating DMA buffers. */
ImxVpuDMABufferAllocator* imx_vpu_dec_get_default_allocator(void);

/* Called before @imx_vpu_dec_open, it returns the alignment and size for the
 * physical memory block necessary for the decoder's bitstream buffer. The user
 * must allocate a DMA buffer of at least this size, and its physical address
 * must be aligned according to the alignment value. */
void imx_vpu_dec_get_bitstream_buffer_info(size_t *size, unsigned int *alignment);

/* Opens a new decoder instance. "open_params", "bitstream_buffer", and "new_initial_info"
 * must not be NULL. "callback_user_data" is a user-defined pointer that is passed on to
 * the callback when it is invoked. The bitstream buffer must use the alignment and size
 * that imx_vpu_dec_get_bitstream_buffer_info() specifies (it can also be larger, but must
 * not be smaller than the size this function gives). */
ImxVpuDecReturnCodes imx_vpu_dec_open(ImxVpuDecoder **decoder, ImxVpuDecOpenParams *open_params, ImxVpuDMABuffer *bitstream_buffer, imx_vpu_dec_new_initial_info_callback new_initial_info_callback, void *callback_user_data);

/* Closes a decoder instance. Trying to close the same instance multiple times results in undefined behavior. */
ImxVpuDecReturnCodes imx_vpu_dec_close(ImxVpuDecoder *decoder);

/* Returns the bitstream buffer that is used by the decoder */
ImxVpuDMABuffer* imx_vpu_dec_get_bitstream_buffer(ImxVpuDecoder *decoder);

/* Enables/disables the drain mode. In drain mode, no new input data is used; instead, any undecoded frames
 * still stored in the VPU are decoded, until the queue is empty. This is useful when there is no more input
 * data, and playback shall stop once all frames are shown. */
ImxVpuDecReturnCodes imx_vpu_dec_enable_drain_mode(ImxVpuDecoder *decoder, int enabled);

/* Checks if drain mode is enabled. 1 = enabled. 0 = disabled. */
int imx_vpu_dec_is_drain_mode_enabled(ImxVpuDecoder *decoder);

/* Flushes the decoder. Any internal undecoded or queued frames are discarded. */
ImxVpuDecReturnCodes imx_vpu_dec_flush(ImxVpuDecoder *decoder);

/* Registers the specified array of framebuffers with the decoder. This must be called after
 * @imx_vpu_dec_decode returned an output code with IMX_VPU_DEC_OUTPUT_CODE_INITIAL_INFO_AVAILABLE
 * set in it. Registering can happen only once during the lifetime of a decoder instance. If for some reason
 * framebuffers need to be re-registered, the instance must be closed, and a new one opened.
 * The caller must ensure that the specified framebuffer array remains valid until the decoder instance
 * is closed. Note that internally, values might be written to the array (though it will never be reallocated
 * and/or freed from the inside). Also, the framebuffers' DMA buffers will be memory-mapped until the decoder
 * is closed.
 *
 * The framebuffers must contain valid values. The convenience functions @imx_vpu_calc_framebuffer_sizes and
 * @imx_vpu_fill_framebuffer_params can be used for this. Note that all framebuffers must have the same
 * stride values. */
ImxVpuDecReturnCodes imx_vpu_dec_register_framebuffers(ImxVpuDecoder *decoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers);

/* Decodes an encoded input frame. "encoded_frame" must always be set, even in drain mode. See ImxVpuEncodedFrame
 * for details about its contents. output_code is a bit mask, must not be NULL, and returns important information
 * about the decoding process. The value is a bitwise OR combination of the codes in ImxVpuDecOutputCodes. */
ImxVpuDecReturnCodes imx_vpu_dec_decode(ImxVpuDecoder *decoder, ImxVpuEncodedFrame *encoded_frame, unsigned int *output_code);

/* Retrieves a decoded picture. The structure referred to by "decoded_picture" will be filled with data about
 * the decoded picture. "decoded_picture" must not be NULL.
 *
 * CAUTION: This function must not be called before @imx_vpu_dec_decode, and even then, only if the output code
 * has the IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE flag set. Otherwise, undefined behavior happens.
 * If the flag is set, this function must not be called more than once. Again, doing so causes undefined
 * behavior. Only after another @imx_vpu_dec_decode call (again, with the flag set) it is valid to
 * call this function again. */
ImxVpuDecReturnCodes imx_vpu_dec_get_decoded_picture(ImxVpuDecoder *decoder, ImxVpuPicture *decoded_picture);

/* Retrieves the context of the dropped frame. This is useful to be able to identify which input frame
 * was dropped. Media frameworks may require this to properly keep track of timestamping.
 *
 * NOTE: This function must not be called before @imx_vpu_dec_decode, and even then, only if the output code has
 * the IMX_VPU_DEC_OUTPUT_CODE_DROPPED flag set. Otherwise, the returned context value is invalid. */
void* imx_vpu_dec_get_dropped_frame_context(ImxVpuDecoder *decoder);

/* Check if the VPU can decode right now. While decoding a video stream, sometimes the VPU may not be able
 * to decode. This is directly related to the set of free framebuffers. If this function returns 0, decoding
 * should not be attempted until after imx_vpu_dec_mark_framebuffer_as_displayed() was called. If this
 * happens, imx_vpu_dec_check_if_can_decode() should be called again to check if the situation changed and
 * decoding can be done again. See the explanation above for details. */
int imx_vpu_dec_check_if_can_decode(ImxVpuDecoder *decoder);

/* Marks a framebuffer as displayed. This always needs to be called once the application is done with the decoded
 * picture. It returns the framebuffer to the VPU pool so it can be reused for further decoding. Not calling
 * this will eventually cause the decoder to fail, because it won't find any free framebuffer for storing
 * a decoded frame anymore.
 *
 * It is safe to mark a framebuffer multiple times. The library will simply ignore the subsequent calls. */
ImxVpuDecReturnCodes imx_vpu_dec_mark_framebuffer_as_displayed(ImxVpuDecoder *decoder, ImxVpuFramebuffer *framebuffer);




/************************************************/
/******* ENCODER STRUCTURES AND FUNCTIONS *******/
/************************************************/


/* How to use the encoder (error handling omitted for clarity):
 * 1. Call imx_vpu_enc_load
 * 2. Call imx_vpu_enc_get_bitstream_buffer_info(), and allocate a DMA buffer
 *    with the given size and alignment.
 * 3. Fill an instance of ImxVpuEncOpenParams with the values specific to the
 *    input data. It is recommended to use imx_vpu_enc_set_default_open_params()
 *    and afterwards set any explicit valus.
 * 4. Call imx_vpu_enc_open(), passing in a pointer to the filled ImxVpuEncOpenParams
 *    instance, and the DMA buffer of the bitstream DMA buffer which was allocated in step 2.
 * 5. Call imx_vpu_enc_get_initial_info(). The encoder's initial info contains the
 *    minimum number of framebuffers that must be allocated and registered, and the
 *    address alignment value for these framebuffers.
 * 6. (Optional) Perform the necessary size and alignment calculations by calling
 *    imx_vpu_calc_framebuffer_sizes(). Pass in the width & height of the frames that
 *    shall be encoded.
 * 7. Create an array of at least as many ImxVpuFramebuffer instances as specified in
 *    min_num_required_framebuffers. Each instance must point to a DMA buffer that is big
 *    enough to hold a frame. If step 6 was performed, allocating as many bytes as indicated
 *    by total_size is enough. Make sure the Y,Cb,Cr,MvCol offsets in each ImxVpuFramebuffer
 *    instance are valid. Using the imx_vpu_fill_framebuffer_params() convenience function
 *    for this is recommended. Note that these framebuffers are used for temporary internal
 *    encoding only, and will not contain input or output data.
 * 8. Call imx_vpu_enc_register_framebuffers() and pass in the ImxVpuFramebuffer array
 *    and the number of ImxVpuFramebuffer instances.
 * 9. Allocate a DMA buffer for the input frames. Only one buffer is necessary. Simply
 *    reuse the sizes used for the temporary buffers in step 7.
 * 10. Allocate a DMA buffer for the encoded output data. Set its size to an appropriate value.
 *     Typically, using the same size as the input buffer is enough, since the whole point of
 *     encoding is to produce encoded frames that are much smaller than the original ones.
 *     However, if a very high bitrate is used, and the input frames are small in size, the
 *     encoded frames may be bigger. This is considered a questionable use case, though, since
 *     then there is no point in encoding.
 * 11. Create an instance of ImxVpuPicture, set its values to zero (typically by using memset()),
 *     and set its framebuffer pointer to refer to the DMA buffer allocated in step 9.
 * 12. Create an instance of ImxVpuEncodedFrame, set its values to zero (typically by using memset()),
 *     and set its data.dma_buffer pointer to refer to the DMA buffer allocated in step 10.
 * 13. Create an instance of ImxVpuEncParams, set its values to zero (typically by using memset()),
 *     and set at least its frame_width, frame_height, framerate, and quant_param values.
 *     In most cases, setting them to the same values as in the open params is enough.
 * 14. Fill the DMA buffer from step 9 with pixels from an input frame, either by transferring
 *     then over DMA somehow (through an i.MX IPU operation for example), or by mapping the buffer
 *     and filling it with pixels with the CPU. Make sure the buffer is unmapped afterwards!
 *     (See imx_vpu_dma_buffer_map() / imx_vpu_dma_buffer_unmap())
 * 15. Call imx_vpu_enc_encode(). Pass the structures from steps 11, 12 and 13 to it.
 *     If the IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE bit is set in the output code,
 *     it is possible to map the output DMA buffer, and access the encoded data. The data_size
 *     member in the encoded frame structure contains the actual size of the data, in bytes.
 *     If the IMX_VPU_ENC_OUTPUT_CODE_SEQUENCE_HEADER is set, sequence header data is available as
 *     well (this is h.264 specific). The encoded data is typically stored in a container format
 *     at this stage.
 * 16. Repeat step 15 until there are no more frames to encode or an error occurs.
 * 13. After encoding is finished, close the encoder with imx_vpu_enc_close().
 * 14. Deallocate framebuffer memory blocks, the input and output DMA buffer blocks, and
 *     the bitstream buffer memory block.
 * 15. Call imx_vpu_enc_unload().
 *
 * Step 15 should only be called if no more playback sessions will occur.
 *
 * Note that the encoder does not use any kind of frame reordering. h.264 data uses the
 * baseline profile. An input frame immediately results in an output frame; there is no delay.
 */


typedef struct _ImxVpuEncoder ImxVpuEncoder;


typedef enum
{
	IMX_VPU_ENC_RETURN_CODE_OK = 0,
	IMX_VPU_ENC_RETURN_CODE_ERROR,
	IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS,
	IMX_VPU_ENC_RETURN_CODE_INVALID_HANDLE,
	IMX_VPU_ENC_RETURN_CODE_INVALID_FRAMEBUFFER,
	IMX_VPU_ENC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS,
	IMX_VPU_ENC_RETURN_CODE_INVALID_STRIDE,
	IMX_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE,
	IMX_VPU_ENC_RETURN_CODE_TIMEOUT
}
ImxVpuEncReturnCodes;


typedef enum
{
	IMX_VPU_ENC_OUTPUT_CODE_INPUT_USED                 = (1UL << 0),
	IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE    = (1UL << 1),
	IMX_VPU_ENC_OUTPUT_CODE_SEQUENCE_HEADER            = (1UL << 2)
}
ImxVpuEncOutputCodes;


typedef enum
{
	IMX_VPU_ENC_SLICE_SIZE_MODE_BITS = 0,
	IMX_VPU_ENC_SLICE_SIZE_MODE_MACROBLOCKS
}
ImxVpuEncSliceSizeMode;


typedef enum
{
	IMX_VPU_ENC_RATE_INTERVAL_MODE_NORMAL = 0,
	IMX_VPU_ENC_RATE_INTERVAL_MODE_FRAME_LEVEL,
	IMX_VPU_ENC_RATE_INTERVAL_MODE_SLICE_LEVEL,
	IMX_VPU_ENC_RATE_INTERVAL_MODE_USER_DEFINED_LEVEL
}
ImxVpuEncRateIntervalMode;


typedef enum
{
	IMX_VPU_ENC_ME_SEARCH_RANGE_256x128 = 0,
	IMX_VPU_ENC_ME_SEARCH_RANGE_128x64,
	IMX_VPU_ENC_ME_SEARCH_RANGE_64x32,
	IMX_VPU_ENC_ME_SEARCH_RANGE_32x32
}
ImxVpuEncMESearchRanges;


typedef struct
{
	int multiple_slices_per_picture;
	ImxVpuEncSliceSizeMode slice_size_mode;
	unsigned int slice_size;
}
ImxVpuEncSliceMode;


typedef struct
{
	int enable_data_partition;
	int enable_reversible_vlc;
	unsigned int intra_dc_vlc_thr;
	int enable_hec;
	unsigned int version_id;
}
ImxVpuEncMPEG4Params;


typedef struct
{
	int enable_annex_i;
	int enable_annex_j;
	int enable_annex_k;
	int enable_annex_t;
}
ImxVpuEncH263Params;


typedef struct
{
	int enable_constrained_intra_prediction;
	int disable_deblocking;
	int deblock_filter_offset_alpha, deblock_filter_offset_beta;
	int chroma_qp_offset;
	int enable_access_unit_delimiters;
}
ImxVpuEncH264Params;


typedef struct
{
	ImxVpuCodecFormat codec_format;

	unsigned int frame_width, frame_height;
	unsigned int frame_rate_numerator;
	unsigned int frame_rate_denominator;
	unsigned int bitrate;
	unsigned int gop_size;
	ImxVpuColorFormat color_format;

	int user_defined_min_qp;
	int user_defined_max_qp;
	int enable_user_defined_min_qp;
	int enable_user_defined_max_qp;

	int min_intra_refresh_mb_count;
	int intra_qp;

	unsigned int user_gamma;

	ImxVpuEncRateIntervalMode rate_interval_mode;
	unsigned int macroblock_interval;

	int enable_avc_intra_16x16_only_mode;

	ImxVpuEncSliceMode slice_mode;

	unsigned int initial_delay;
	unsigned int vbv_buffer_size;

	ImxVpuEncMESearchRanges me_search_range;
	int use_me_zero_pmv;
	unsigned int additional_intra_cost_weight;

	union
	{
		ImxVpuEncMPEG4Params mpeg4_params;
		ImxVpuEncH263Params h263_params;
		ImxVpuEncH264Params h264_params;
	}
	codec_params;
}
ImxVpuEncOpenParams;


typedef struct
{
	/* Caller must register at least this many framebuffers
	 * with the encoder. */
	unsigned int min_num_required_framebuffers;

	/* Physical framebuffer addresses must be aligned to this value. */
	unsigned int framebuffer_alignment;
}
ImxVpuEncInitialInfo;


typedef struct
{
	int force_I_picture;
	int skip_picture;
	int enable_autoskip;

	unsigned int quant_param;
}
ImxVpuEncParams;


/* Returns a human-readable description of the error code.
 * Useful for logging. */
char const * imx_vpu_enc_error_string(ImxVpuEncReturnCodes code);

/* These two functions load/unload the encoder. Due to an internal reference
 * counter, it is safe to call these functions more than once. However, the
 * number of unload() calls must match the number of load() calls.
 *
 * The encoder must be loaded before doing anything else with it.
 * Similarly, the encoder must not be unloaded before all encoder activities
 * have been finished. This includes opening/decoding encoder instances. */
ImxVpuEncReturnCodes imx_vpu_enc_load(void);
ImxVpuEncReturnCodes imx_vpu_enc_unload(void);

/* Convenience predefined allocator for allocating DMA buffers. */
ImxVpuDMABufferAllocator* imx_vpu_enc_get_default_allocator(void);

/* Called before imx_vpu_enc_open(), it returns the alignment and size for the
 * physical memory block necessary for the encoder's bitstream buffer. The user
 * must allocate a DMA buffer of at least this size, and its physical address
 * must be aligned according to the alignment value. */
void imx_vpu_enc_get_bitstream_buffer_info(size_t *size, unsigned int *alignment);

/* Set the fields in "open_params" to valid defaults
 * Useful if the caller wants to modify only a few fields (or none at all) */
void imx_vpu_enc_set_default_open_params(ImxVpuCodecFormat codec_format, ImxVpuEncOpenParams *open_params);

/* Opens a new encoder instance. "open_params" and "bitstream_buffer" must not be NULL. */
ImxVpuEncReturnCodes imx_vpu_enc_open(ImxVpuEncoder **encoder, ImxVpuEncOpenParams *open_params, ImxVpuDMABuffer *bitstream_buffer);

/* Closes a encoder instance. Trying to close the same instance multiple times results in undefined behavior. */
ImxVpuEncReturnCodes imx_vpu_enc_close(ImxVpuEncoder *encoder);

/* Returns the bitstream buffer that is used by the encoder */
ImxVpuDMABuffer* imx_vpu_enc_get_bitstream_buffer(ImxVpuEncoder *encoder);

/* Registers the specified array of framebuffers with the encoder. These framebuffers are used for temporary
 * values during encoding, unlike the decoder framebuffers. The minimum valid value for "num_framebuffers" is
 * the "min_num_required_framebuffers" field of ImxVpuEncInitialInfo. */
ImxVpuEncReturnCodes imx_vpu_enc_register_framebuffers(ImxVpuEncoder *encoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers);

/* Retrieves initial information available after calling @imx_vpu_enc_open. */
ImxVpuEncReturnCodes imx_vpu_enc_get_initial_info(ImxVpuEncoder *encoder, ImxVpuEncInitialInfo *info);

/* Set the fields in "open_params" to valid defaults
 * Useful if the caller wants to modify only a few fields (or none at all) */
void imx_vpu_enc_set_default_encoding_params(ImxVpuEncoder *encoder, ImxVpuEncParams *encoding_params);

/* Sets/updates the bitrate. This allows for controlled bitrate updates during encoding. Calling this
 * function is optional; by default, the bitrate from the open_params in imx_vpu_enc_open() is used. */
void imx_vpu_enc_configure_bitrate(ImxVpuEncoder *encoder, unsigned int bitrate);
/* Sets/updates the miniimum number of macroblocks to refresh in a frame. */
void imx_vpu_enc_configure_min_intra_refresh(ImxVpuEncoder *encoder, unsigned int min_intra_refresh_num);
/* Sets/updates a constant I-frame quantization parameter (1-31 for MPEG-4, 0-51 for h.264). -1 enables
 * automatic selection by the VPU. Calling this function is optional; by default, the intra QP value from
 * the open_params in imx_vpu_enc_open() is used. */
void imx_vpu_enc_configure_intra_qp(ImxVpuEncoder *encoder, int intra_qp);

ImxVpuEncReturnCodes imx_vpu_enc_encode(ImxVpuEncoder *encoder, ImxVpuPicture *picture, ImxVpuEncodedFrame *encoded_frame, ImxVpuEncParams *encoding_params, unsigned int *output_code);




#ifdef __cplusplus
}
#endif


#endif
