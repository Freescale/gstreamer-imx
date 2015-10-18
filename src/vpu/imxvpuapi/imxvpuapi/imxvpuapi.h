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


/* Format and for printf-compatibel format-strings
 * example use: printf("physical address: %" IMX_VPU_PHYS_ADDR_FORMAT, phys_addr */
#define IMX_VPU_PHYS_ADDR_FORMAT "#lx"
/* Typedef for physical addresses */
typedef unsigned long imx_vpu_phys_addr_t;


/* ImxVpuMappingFlags: flags for the ImxVpuDMABufferAllocator's allocate vfunc
 * These flags can be bitwise-OR combined, although WRITECOMBINE and UNCACHED
 * cannot both be set */
typedef enum
{
	IMX_VPU_ALLOCATION_FLAG_WRITECOMBINE = (1UL << 0),
	IMX_VPU_ALLOCATION_FLAG_UNCACHED     = (1UL << 1)
	/* XXX: When adding extra flags here, follow the pattern: IMX_VPU_ALLOCATION_FLAG_<NAME> = (1UL << <INDEX>) */
}
ImxVpuAllocationFlags;


/* ImxVpuMappingFlags: flags for the ImxVpuDMABufferAllocator's map vfuncs
 * These flags can be bitwise-OR combined, although READ and WRITE cannot
 * both be set */
typedef enum
{
	/* Map memory for CPU read access */
	IMX_VPU_MAPPING_FLAG_WRITE   = (1UL << 0),
	/* Map memory for CPU write access */
	IMX_VPU_MAPPING_FLAG_READ    = (1UL << 1)
	/* XXX: When adding extra flags here, follow the pattern: IMX_VPU_MAPPING_FLAG_<NAME> = (1UL << <INDEX>) */
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
 * one of them. If an allocator does not support FDs, get_fd() must return -1. If it does not support physical
 * addresses, then the physical address returned by get_physical_address() must be 0.
 *
 * The vfuncs are:
 *
 * allocate(): Allocates a DMA buffer. "size" is the size of the buffer in bytes. "alignment" is the address
 *             alignment in bytes. An alignment of 1 or 0 means that no alignment is required.
 *             "flags" is a bitwise OR combination of flags (or 0 if no flags are used, in which case
 *             cached pages are used by default). See ImxVpuAllocationFlags for a list of valid flags.
 *             If allocation fails, NULL is returned.
 *
 * deallocate(): Deallocates a DMA buffer. The buffer must have been allocated with the same allocator.
 *
 * map(): Maps a DMA buffer to the local address space, and returns the virtual address to this space.
 *        "flags" is a bitwise OR combination of flags (or 0 if no flags are used, in which case it will map
 *        in regular read/write mode). See ImxVpuMappingFlags for a list of valid flags.
 *
 * unmap(): Unmaps a DMA buffer. If the buffer isn't currently mapped, this function does nothing.
 *
 * get_fd(): Gets the file descriptor associated with the DMA buffer. This is the preferred way of interacting
 *           with DMA buffers. If the underlying allocator does not support FDs, this function returns -1.
 *
 * get_physical_address(): Gets the physical address associated with the DMA buffer. This address points to the
 *                         start of the buffer in the physical address space. If no physical addresses are
 *                         supported by the allocator, this function returns 0.
 *
 * get_size(): Returns the size of the buffer, in bytes.
 *
 * The vfuncs get_fd(), get_physical_address(), and get_size() can also be used while the buffer is mapped. */
struct _ImxVpuDMABufferAllocator
{
	ImxVpuDMABuffer* (*allocate)(ImxVpuDMABufferAllocator *allocator, size_t size, unsigned int alignment, unsigned int flags);
	void (*deallocate)(ImxVpuDMABufferAllocator *allocator, ImxVpuDMABuffer *buffer);

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

	uint8_t* (*map)(ImxVpuWrappedDMABuffer *wrapped_dma_buffer, unsigned int flags);
	void (*unmap)(ImxVpuWrappedDMABuffer *wrapped_dma_buffer);

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

/* Call for initializing wrapped DMA buffer structures.
 * Always call this before further using such a structure. */
void imx_vpu_init_wrapped_dma_buffer(ImxVpuWrappedDMABuffer *buffer);


/* Heap allocation function for virtual memory blocks internally allocated by imxvpuapi.
 * These have nothing to do with the DMA buffer allocation interface defined above.
 * By default, malloc/free are used. */
typedef void* (*ImxVpuHeapAllocFunc)(size_t const size, void *context, char const *file, int const line, char const *fn);
typedef void (*ImxVpuHeapFreeFunc)(void *memblock, size_t const size, void *context, char const *file, int const line, char const *fn);

/* This function allows for setting custom heap allocators, which are used to create internal heap blocks.
 * The heap allocator referred to by "heap_alloc_fn" must return NULL if allocation fails.
 * "context" is a user-defined value, which is passed on unchanged to the allocator functions.
 * Calling this function with either "heap_alloc_fn" or "heap_free_fn" set to NULL resets the internal
 * pointers to use malloc and free (the default allocators). */
void imx_vpu_set_heap_allocator_functions(ImxVpuHeapAllocFunc heap_alloc_fn, ImxVpuHeapFreeFunc heap_free_fn, void *context);




/***********************/
/******* LOGGING *******/
/***********************/


/* Log levels. */
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

/* Function pointer type for logging functions.
 *
 * This function is invoked by IMX_VPU_LOG() macro calls. This macro also passes the name
 * of the source file, the line in that file, and the function name where the logging occurs
 * to the logging function (over the file, line, and fn arguments, respectively).
 * Together with the log level, custom logging functions can output this metadata, or use
 * it for log filtering etc.*/
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


/* Picture types understood by the VPU. Note that no codec format
 * supports all of these types. */
typedef enum
{
	/* Unknown picture type */
	IMX_VPU_PIC_TYPE_UNKNOWN = 0,
	/* Picture is an I (= intra) frame. These can be used as keyframes / sync points.
	 * All codec formats support this one. With MJPEG, all frames are I frames. */
	IMX_VPU_PIC_TYPE_I,
	/* Picture is n P (= predicted) frame. All codec formats except MJPEG
	 * support these frames. */
	IMX_VPU_PIC_TYPE_P,
	/* Picture is n B (= bidirectionally predicted) frame. Out of the list of codec
	 * formats the VPU can decode, h.264, MPEG-2, MPEG-4, and VC-1 support these. */
	IMX_VPU_PIC_TYPE_B,
	/* Picture is n IDR frame. These are h.264 specific frames, and can be used as
	 * key frames / sync points. */
	IMX_VPU_PIC_TYPE_IDR,
	/* Picture is a B frame (see above), but all of its macroblocks are intra coded.
	 * VC-1 specific. Cannot be used as a keyframe / sync point. */
	IMX_VPU_PIC_TYPE_BI,
	/* Picture was skipped. TODO: is this necessary? */
	IMX_VPU_PIC_TYPE_SKIP
}
ImxVpuPicType;


/* Valid interlacing modes. When interlacing is used, each frame is made of one or
 * or two interlaced fields (in almost all cases, it's two fields). Rows with odd
 * Y coordinates belong to the top field, rows with even Y coordinates to the bottom.
 *
 * Some video sources send the top field first, some the bottom first, some send
 * only the top or bottom fields. If both fields got transmitted, it is important
 * to know which field was transmitted first to establish a correct temporal order.
 * This is because in interlacing, the top and bottom fields do not contain the
 * data from the same frame (unless the source data was progressive video). If the
 * top field came first, then the top field contains rows from a time t, and the
 * bottom field from a time t+1. For operations like deinterlacing, knowing the
 * right temporal order might be essential. */
typedef enum
{
	/* Unknown interlacing mode */
	IMX_VPU_INTERLACING_MODE_UNKNOWN = 0,
	/* Picture is progressive; it does not use interlacing */
	IMX_VPU_INTERLACING_MODE_NO_INTERLACING,
	/* Top field (= odd rows) came first */
	IMX_VPU_INTERLACING_MODE_TOP_FIELD_FIRST,
	/* Bottom field (= even rows) came first */
	IMX_VPU_INTERLACING_MODE_BOTTOM_FIELD_FIRST,
	/* Only the top field was transmitted (even rows are empty) */
	IMX_VPU_INTERLACING_MODE_TOP_FIELD_ONLY,
	/* Only the bottom field was transmitted (odd rows are empty) */
	IMX_VPU_INTERLACING_MODE_BOTTOM_FIELD_ONLY
}
ImxVpuInterlacingMode;


/* Codec format to use for en/decoding. Only a subset of these
 * are also supported by the encoder. Unless otherwise noted, the maximum
 * supported resolution is 1920x1088.*/
typedef enum
{
	/* MPEG-1 part 2 and MPEG-2 part 2.
	 *
	 * Decoding: Fully compatible with the ISO/IEC 13182-2 specification and
	 * the main and high profiles. Both progressive and interlaced content is
	 * supported. */
	IMX_VPU_CODEC_FORMAT_MPEG2 = 0,

	/* MPEG-4 part 2.
	 *
	 * Decoding: Supports simple and advanced simple profile (except for GMC).
	 * NOTE: DivX 3/5/6 are not supported and require special licensing by
	 * Freescale.
	 *
	 * Encoding: Supports the simple profile and max. level 5/6.
	 */
	IMX_VPU_CODEC_FORMAT_MPEG4,

	/* h.263.
	 *
	 * Decoding: Supports baseline profile and Annex I, J, K (except for RS/ASO), T, and max. level 70.
	 * Encoding: Supports baseline profile and Annex I, J, K (RS and ASO are 0), T, and max. level 70.
	 */
	IMX_VPU_CODEC_FORMAT_H263,

	/* h.264.
	 *
	 * Decoding: Supports baseline, main, high profiles, max. level 4.1.
	 * Encoding: Supports baseline and constrained baseline profile, max. level 4.0.
	 */
	IMX_VPU_CODEC_FORMAT_H264,

	/* WMV3, also known as Windows Media Video 9. Compatible to VC-1 simple and main profiles.
	 *
	 * Decoding: Fully supported WMV3 decoding, excluding the deprecated WMV3
	 * interlace support (which has been obsoleted by the interlacing in the
	 * VC-1 advanced profile). */
	IMX_VPU_CODEC_FORMAT_WMV3,

	/* VC-1, also known as Windows Media Video 9 Advanced Profile.
	 *
	 * Decoding: SMPTE VC-1 compressed video standard fully supported. Max. level is 3.
	 */
	IMX_VPU_CODEC_FORMAT_WVC1,

	/* Motion JPEG.
	 *
	 * Decoding: Only baseline JPEG frames are supported. Maximum resolution is 8192x8192.
	 *
	 * Encoding: Only baseline JPEG frames are supported. Maximum resolution is 8192x8192.
	 * MJPEG always operates in constant quality mode, even if the encoder open params
	 * have a nonzero bitrate set.
	 */
	IMX_VPU_CODEC_FORMAT_MJPEG,

	/* VP8.
	 *
	 * Decoder: fully compatible with the VP8 decoding specification.
	 * Both simple and normal in-loop deblocking are supported.
	 * NOTE: VPU specs state that the maximum supported resolution is 1280x720, but tests
	 * show that up to 1920x1088 pixels do work.
	 */
	IMX_VPU_CODEC_FORMAT_VP8

	/* XXX others will be added when the firmware supports them or when the
	 * firmware for them is acquired (this includes formats like AVS, DivX 3/5/6) */
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
	/* 8-bit greayscale */
	IMX_VPU_COLOR_FORMAT_YUV400            = 4
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
	/* When decoding, data must point to the memory block which contains
	 * encoded frame data that gets consumed by the VPU. Not used by
	 * the encoder. */
	uint8_t *data;

	/* Size of the encoded data, in bytes. This is set by the user, and
	 * is the size of the encoded data that is pointed to by data.
	 * Not used by the encoder. */
	size_t data_size;

	/* Pointer to out-of-band codec/header data. If such data exists,
	 * specify the pointer to the memory block containing the data,
	 * as well as the size of the memory block (in bytes).
	 * Set pointer and size for every encoded frame when decoding.
	 * If no such data exists or is required, or if drain mode is enabled,
	 * the pointer must be NULL, the size must be 0. Not used by the encoder. */
	uint8_t *codec_data;
	size_t codec_data_size;

	/* Picture type (I, P, B, ..) of the encoded frame. Filled by the encoder.
	 * Unused by the decoder. */
	ImxVpuPicType pic_type;

	/* Handle produced by the user-defined acquire_output_buffer function
	 * during encoding. Not used by the decoder. */
	void *acquired_handle;

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

	/* Picture types (I, P, B, ..) ; unused by the encoder
	 * In case of interlaced content, the first picture type corresponds to the
	 * first field, the second type to the second field. For progressive content,
	 * both types are set to the same value. */
	ImxVpuPicType pic_types[2];

	/* Interlacing mode (top-first, bottom-first..); unused by the encoder */
	ImxVpuInterlacingMode interlacing_mode;

	/* User-defined pointer. The library does not touch this value.
	 * This pointer and the one from the corresponding
	 * encoded frame will have the same value. The library will
	 * pass then through.
	 * It can be used to identify which picture is associated with
	 * this encoded frame for example. */
	void *context;
}
ImxVpuPicture;


/* Structure used together with imx_vpu_calc_framebuffer_sizes() */
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

/* Returns a human-readable description of the given color format. Useful for logging. */
char const *imx_vpu_color_format_string(ImxVpuColorFormat color_format);
/* Returns a human-readable description of the given picture-type. Useful for logging. */
char const *imx_vpu_picture_type_string(ImxVpuPicType picture_type);




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
 *    for this is strongly recommended.
 * 9. Call imx_vpu_dec_register_framebuffers() and pass in the ImxVpuFramebuffer array
 *    and the number of ImxVpuFramebuffer instances.
 *    This should be the last action in the callback.
 * 10. Continue calling imx_vpu_dec_decode(). The data point in encoded_frame
 *     must not be NULL; also, the data_size value in encoded_frame must be > 0.
 *     If the IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE flag is set in the output code,
 *     call imx_vpu_dec_get_decoded_picture() with a pointer to an ImxVpuPicture instance
 *     which gets filled with information about the decoded picture. Once the decoded picture
 *     has been processed by the user, imx_vpu_dec_mark_framebuffer_as_displayed() must be
 *     called to let the decoder know that the framebuffer is available for storing new
 *     decoded pictures again.
 *     If IMX_VPU_DEC_OUTPUT_CODE_DROPPED is set, you can call
 *     imx_vpu_dec_get_dropped_frame_context() to retrieve the context field
 *     of the dropped frame. If IMX_VPU_DEC_OUTPUT_CODE_EOS is set, or if imx_vpu_dec_decode()
 *     returns a value other than IMX_VPU_DEC_RETURN_CODE_OK, stop playback and close the decoder.
 * 11. In case a flush/reset is desired (typically after seeking), call imx_vpu_dec_flush().
 *     Note that any internal context pointers from the en/decoded frames will be
 *     thrown away after this call; if for example the context is an index, the system that
 *     hands out the indices should be informed that any previously handed out index is now
 *     unused.
 * 12. When there is no more incoming data, and pending decoded frames need to be retrieved
 *     from the decoder, enable drain mode with imx_vpu_dec_enable_drain_mode(). This is
 *     typically necessary when the data source reached its end, playback is finishing, and
 *     there is a delay of N frames at the beginning.
 *     After this call, continue calling imx_vpu_dec_decode() to retrieve the pending
 *     decoded pictures, but the data and the codec data pointers of encoded_frame
 *     must be NULL.
 *     As in step 10, if IMX_VPU_DEC_OUTPUT_CODE_EOS is set, or if imx_vpu_dec_decode() returns
 *     a value other than IMX_VPU_DEC_RETURN_CODE_OK, stop playback and close the decoder.
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


/* Opaque decoder structure. */
typedef struct _ImxVpuDecoder ImxVpuDecoder;


/* Decoder return codes. With the exception of IMX_VPU_DEC_RETURN_CODE_OK, these
 * should be considered hard errors, and the decoder should be closed when they
 * are returned. */
typedef enum
{
	/* Operation finished successfully. */
	IMX_VPU_DEC_RETURN_CODE_OK = 0,
	/* General return code for when an error occurs. This is used as a catch-all
	 * for when the other error return codes do not match the error. */
	IMX_VPU_DEC_RETURN_CODE_ERROR,
	/* Input parameters were invalid. */
	IMX_VPU_DEC_RETURN_CODE_INVALID_PARAMS,
	/* VPU decoder handle is invalid. This is an internal error, and most likely
	 * a bug in the library. Please report such errors. */
	IMX_VPU_DEC_RETURN_CODE_INVALID_HANDLE,
	/* Framebuffer information is invalid. Typically happens when the ImxVpuFramebuffer
	 * structures that get passed to imx_vpu_dec_register_framebuffers() contain
	 * invalid values. */
	IMX_VPU_DEC_RETURN_CODE_INVALID_FRAMEBUFFER,
	/* Registering framebuffers for decoding failed because not enough framebuffers
	 * were given to the imx_vpu_dec_register_framebuffers() function. */
	IMX_VPU_DEC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS,
	/* A stride value (for example one of the stride values of a framebuffer) is invalid. */
	IMX_VPU_DEC_RETURN_CODE_INVALID_STRIDE,
	/* A function was called at an inappropriate time (for example, when
	 * imx_vpu_dec_register_framebuffers() is called before a single byte of input data
	 * was passed to imx_vpu_dec_decode() ). */
	IMX_VPU_DEC_RETURN_CODE_WRONG_CALL_SEQUENCE,
	/* The operation timed out. */
	IMX_VPU_DEC_RETURN_CODE_TIMEOUT,
	/* A function that should only be called once for the duration of the decoding
	 * session was called again. One example is imx_vpu_dec_register_framebuffers(). */
	IMX_VPU_DEC_RETURN_CODE_ALREADY_CALLED
}
ImxVpuDecReturnCodes;


/* Decoder output codes. These can be bitwise OR combined, so check
 * for their presence in the output_codes bitmask returned by
 * imx_vpu_dec_decode() by using a bitwise AND. */
typedef enum
{
	/* Input data was used. If this code is present, the input data
	 * that was given to the imx_vpu_dec_decode() must not be given
	 * to a following imx_vpu_dec_decode() call; instead, new data
	 * should be loaded. If this code is not present, then the decoder
	 * didn't use it yet, so give it to the decoder again until this
	 * code is set or an error is returned. */
	IMX_VPU_DEC_OUTPUT_CODE_INPUT_USED                   = (1UL << 0),
	/* EOS was reached; no more unfinished frames are queued internally.
	 * This can be reached either by bitstreams with no frame delay,
	 * or by running the decoder in drain mode (enabled by calling
	 * imx_vpu_dec_enable_drain_mode() ).
	 */
	IMX_VPU_DEC_OUTPUT_CODE_EOS                          = (1UL << 1),
	/* A fully decoded picture is now available, and can be retrieved
	 * by calling imx_vpu_dec_get_decoded_picture(). */
	IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE    = (1UL << 2),
	/* A picture was dropped by the decoder. The dropped picture's
	 * context value can be retrieved by calling
	 * imx_vpu_dec_get_dropped_frame_context(). */
	IMX_VPU_DEC_OUTPUT_CODE_DROPPED                      = (1UL << 3),
	/* There aren't enough free framebuffers available for decoding.
	 * This usually happens when imx_vpu_dec_mark_framebuffer_as_displayed()
	 * wasn't called before imx_vpu_dec_decode(), which can occur in
	 * multithreaded environments. imx_vpu_dec_check_if_can_decode() is useful
	 * to avoid this. Also see the guide above for more. */
	IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_OUTPUT_FRAMES     = (1UL << 4),
	/* Input data for a frame is incomplete. No decoded picture will
	 * be available until the input frame's data has been fully and
	 * correctly delivered. */
	IMX_VPU_DEC_OUTPUT_CODE_NOT_ENOUGH_INPUT_DATA        = (1UL << 5),
	/* The VPU detected a resolution change. This resets the framebuffer
	 * registration, meaning that the initial info callback that was
	 * passed to imx_vpu_dec_open will be called again, with the new
	 * resolution. The framebuffer registration process is repeated.
	 * Make sure the old framebuffers are deallocated when this happens!
	 * If the environment is a framework like GStreamer or libav, which
	 * has parsers that detect resolution changes on its own, chances are,
	 * this output code is never encountered, because in these frameworks,
	 * the decoder is reopened with the updated resolution instead. */
	IMX_VPU_DEC_OUTPUT_CODE_RESOLUTION_CHANGED           = (1UL << 6)
}
ImxVpuDecOutputCodes;


/* Structure used together with imx_vpu_dec_open() */
typedef struct
{
	/* Format of the incoming data to decode. */
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


/* Structure used together with imx_vpu_dec_new_initial_info_callback() .
 * The values are filled by the decoder. */
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
 * it means that this callback was called before, and now got called again, because
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

/* Called before imx_vpu_dec_open(), it returns the alignment and size for the
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
 * imx_vpu_dec_decode() returned an output code with IMX_VPU_DEC_OUTPUT_CODE_INITIAL_INFO_AVAILABLE
 * set in it. Registering can happen only once during the lifetime of a decoder instance. If for some reason
 * framebuffers need to be re-registered, the instance must be closed, and a new one opened.
 * The caller must ensure that the specified framebuffer array remains valid until the decoder instance
 * is closed. Note that internally, values might be written to the array (though it will never be reallocated
 * and/or freed from the inside). Also, the framebuffers' DMA buffers will be memory-mapped until the decoder
 * is closed.
 *
 * The framebuffers must contain valid values. The convenience functions imx_vpu_calc_framebuffer_sizes() and
 * imx_vpu_fill_framebuffer_params() can be used for this. Note that all framebuffers must have the same
 * stride values. */
ImxVpuDecReturnCodes imx_vpu_dec_register_framebuffers(ImxVpuDecoder *decoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers);

/* Decodes an encoded input frame. "encoded_frame" must always be set, even in drain mode. See ImxVpuEncodedFrame
 * for details about its contents. output_code is a bit mask, must not be NULL, and returns important information
 * about the decoding process. The value is a bitwise OR combination of the codes in ImxVpuDecOutputCodes. */
ImxVpuDecReturnCodes imx_vpu_dec_decode(ImxVpuDecoder *decoder, ImxVpuEncodedFrame *encoded_frame, unsigned int *output_code);

/* Retrieves a decoded picture. The structure referred to by "decoded_picture" will be filled with data about
 * the decoded picture. "decoded_picture" must not be NULL.
 *
 * CAUTION: This function must not be called before imx_vpu_dec_decode(), and even then, only if the output code
 * has the IMX_VPU_DEC_OUTPUT_CODE_DECODED_PICTURE_AVAILABLE flag set. Otherwise, undefined behavior happens.
 * If the flag is set, this function must not be called more than once. Again, doing so causes undefined
 * behavior. Only after another imx_vpu_dec_decode() call (again, with the flag set) it is valid to
 * call this function again. */
ImxVpuDecReturnCodes imx_vpu_dec_get_decoded_picture(ImxVpuDecoder *decoder, ImxVpuPicture *decoded_picture);

/* Retrieves the context of the dropped frame. This is useful to be able to identify which input frame
 * was dropped. Media frameworks may require this to properly keep track of timestamping.
 *
 * NOTE: This function must not be called before imx_vpu_dec_decode(), and even then, only if the output code has
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
 * 10. Allocate a memory block for the encoded output data; it does not have to be a DMA buffer.
 *     Set its size to an appropriate value.
 *     Typically, using the same size as the input buffer is enough, since the whole point of
 *     encoding is to produce encoded frames that are much smaller than the original ones.
 * 11. Create an instance of ImxVpuPicture, set its values to zero (typically by using memset()),
 *     and set its framebuffer pointer to refer to the DMA buffer allocated in step 9.
 * 12. Create an instance of ImxVpuEncodedFrame. Set its values to zero (typically by using memset()).
 *     Set its data pointer to refer to the memory block allocated in step 10, and data_size to
 *     the size of that block.
 * 13. Create an instance of ImxVpuEncParams, set its values to zero (typically by using memset()),
 *     and set at least its quant_param value (unless CBR is used).
 * 14. Fill the DMA buffer from step 9 with pixels from an input frame, either by transferring
 *     then over DMA somehow (through an i.MX IPU operation for example), or by mapping the buffer
 *     and filling it with pixels with the CPU. Make sure the buffer is unmapped afterwards!
 *     (See imx_vpu_dma_buffer_map() / imx_vpu_dma_buffer_unmap())
 * 15. Call imx_vpu_enc_encode(). Pass the structures from steps 11, 12 and 13 to it.
 *     The IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE output code bit will always be set
 *     unless the function returned a code other than IMX_VPU_ENC_RETURN_CODE_OK.
 *     If the IMX_VPU_ENC_OUTPUT_CODE_CONTAINS_HEADER bit is set, then header data has been
 *     written in the output memory block allocated in step 10. It is placed right before the
 *     actual encoded frame data. The data_size value of the ImxVpuEncodedFrame structure from
 *     step 12 will contain the actual number of written bytes (including any header bytes).
 *     Use this to do further processing with the encoded data that is now in the output memory
 *     block.
 * 16. Repeat step 15 until there are no more frames to encode or an error occurs.
 * 13. After encoding is finished, close the encoder with imx_vpu_enc_close().
 * 14. Deallocate framebuffer memory blocks, the input DMA buffer block, the output memory block,
 *     and the bitstream buffer memory block.
 * 15. Call imx_vpu_enc_unload().
 *
 * Step 15 should only be called if no more playback sessions will occur.
 *
 * Note that the encoder does not use any kind of frame reordering. h.264 data uses the
 * baseline profile. An input frame immediately results in an output frame; there is no delay.
 */


/* Opaque encoder structure. */
typedef struct _ImxVpuEncoder ImxVpuEncoder;


/* Encoder return codes. With the exception of IMX_VPU_ENC_RETURN_CODE_OK, these
 * should be considered hard errors, and the encoder should be closed when they
 * are returned. */
typedef enum
{
	/* Operation finished successfully. */
	IMX_VPU_ENC_RETURN_CODE_OK = 0,
	/* General return code for when an error occurs. This is used as a catch-all
	 * for when the other error return codes do not match the error. */
	IMX_VPU_ENC_RETURN_CODE_ERROR,
	/* Input parameters were invalid. */
	IMX_VPU_ENC_RETURN_CODE_INVALID_PARAMS,
	/* VPU encoder handle is invalid. This is an internal error, and most likely
	 * a bug in the library. Please report such errors. */
	IMX_VPU_ENC_RETURN_CODE_INVALID_HANDLE,
	/* Framebuffer information is invalid. Typically happens when the ImxVpuFramebuffer
	 * structures that get passed to imx_vpu_enc_register_framebuffers() contain
	 * invalid values. */
	IMX_VPU_ENC_RETURN_CODE_INVALID_FRAMEBUFFER,
	/* Registering framebuffers for encoding failed because not enough framebuffers
	 * were given to the imx_vpu_enc_register_framebuffers() function. */
	IMX_VPU_ENC_RETURN_CODE_INSUFFICIENT_FRAMEBUFFERS,
	/* A stride value (for example one of the stride values of a framebuffer) is invalid. */
	IMX_VPU_ENC_RETURN_CODE_INVALID_STRIDE,
	/* A function was called at an inappropriate time. */
	IMX_VPU_ENC_RETURN_CODE_WRONG_CALL_SEQUENCE,
	/* The operation timed out. */
	IMX_VPU_ENC_RETURN_CODE_TIMEOUT
}
ImxVpuEncReturnCodes;


/* Encoder output codes. These can be bitwise OR combined, so check
 * for their presence in the output_codes bitmask returned by
 * imx_vpu_enc_encode() by using a bitwise AND. */
typedef enum
{
	/* Input data was used. If this code is present, the input picture
	 * that was given to the imx_vpu_dec_encode() must not be given
	 * to a following imx_vpu_dec_encode() call; instead, a new picture
	 * should be loaded. If this code is not present, then the encoder
	 * didn't use it yet, so give it to the encoder again until this
	 * code is set or an error is returned. */
	IMX_VPU_ENC_OUTPUT_CODE_INPUT_USED                 = (1UL << 0),
	/* A fully encoded frame is now available. The encoded_frame argument
	 * passed to imx_vpu_enc_encode() contains information about this frame. */
	IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE    = (1UL << 1),
	/* The data in the encoded frame also contains header information
	 * like SPS/PSS for h.264. Headers are always placed at the beginning
	 * of the encoded data, and this code is never present if the
	 * IMX_VPU_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE isn't set. */
	IMX_VPU_ENC_OUTPUT_CODE_CONTAINS_HEADER            = (1UL << 2)
}
ImxVpuEncOutputCodes;


/* Valid slice size modes that can be used in the ImxVpuEncSliceMode structure. */
typedef enum
{
	/* The slice_size value in ImxVpuEncSliceMode is given in bits */
	IMX_VPU_ENC_SLICE_SIZE_UNIT_BITS = 0,
	/* The slice_size value in ImxVpuEncSliceMode is given in macroblocks */
	IMX_VPU_ENC_SLICE_SIZE_UNIT_MACROBLOCKS
}
ImxVpuEncSliceSizeUnits;


/* Rate control mode to use in the encoder. Not used in constant quality mode. */
typedef enum
{
	/* Normal rate control mode. */
	IMX_VPU_ENC_RATE_CONTROL_MODE_NORMAL = 0,
	/* Per-frame rate control mode. */
	IMX_VPU_ENC_RATE_CONTROL_MODE_FRAME_LEVEL,
	/* Per-slice rate control mode. */
	IMX_VPU_ENC_RATE_CONTROL_MODE_SLICE_LEVEL,
	/* User defined rate control mode. The macroblock_interval value
	 * in ImxVpuEncOpenParams must also be set if this mode is used. */
	IMX_VPU_ENC_RATE_CONTROL_MODE_USER_DEFINED_LEVEL
}
ImxVpuEncRateControlModes;


/* Motion estimation search window range to use in the encoder. This specifies
 * the size of the window around the current block that is searched by the
 * encoder. Naming convention is: width_in_blocks x height_in_blocks. */
typedef enum
{
	IMX_VPU_ENC_ME_SEARCH_RANGE_256x128 = 0,
	IMX_VPU_ENC_ME_SEARCH_RANGE_128x64,
	IMX_VPU_ENC_ME_SEARCH_RANGE_64x32,
	IMX_VPU_ENC_ME_SEARCH_RANGE_32x32
}
ImxVpuEncMESearchRanges;


/* Slice mode information to be used when opening an encoder instance. */
typedef struct
{
	/* If this is 1, multiple sizes are produced per picture. If it is 0,
	 * one slice per picture is used. Default value is 0. */
	int multiple_slices_per_picture;
	/* If multiple_slices_per_picture is 1, this specifies the unit
	 * for the slice_size value. if multiple_slices_per_picture is 0,
	 * this value is ignored. Default value is IMX_VPU_ENC_SLICE_SIZE_UNIT_BITS. */
	ImxVpuEncSliceSizeUnits slice_size_unit;
	/* If multiple_slices_per_picture is 1, this specifies the size of
	 * a slice, in units specified by slice_size_unit. If
	 * multiple_slices_per_picture is 0, this value is ignored. Default
	 * vlaue is 4000. */
	unsigned int slice_size;
}
ImxVpuEncSliceMode;


/* MPEG-4 part 2 parameters to be used when opening an encoder instance. */
typedef struct
{
	/* If set to 1, MPEG-4 data partitioning mode is enabled. If set to
	 * 0, it is disabled. Default value is 0. */
	int enable_data_partitioning;
	/* If set to 1, additional reversible variable length codes for
	 * increased resilience are added. If 0, they are omitted.
	 * Default value is 0. */
	int enable_reversible_vlc;
	/* The mechanism to use for switching between two VLC's for intra
	 * coeffient encoding, as described in ISO/IEC 14496-2 section 6.3.6.
	 * Default value is 0. Valid range is 0 to 7. */
	unsigned int intra_dc_vlc_thr;
	/* If set to 1, it enables the header extension code. 0 disables it.
	 * Default value is 0. */
	int enable_hec;
	/* The MPEG-4 part 2 standard version ID. Valid values are 1 and 2.
	 * Default value is 2. */
	unsigned int version_id;
}
ImxVpuEncMPEG4Params;


/* h.263 parameters for the new encoder instance. */
typedef struct
{
	/* 1 = Annex.I support is enabled. 0 = disabled. Default value is 0. */
	int enable_annex_i;
	/* 1 = Annex.J support is enabled. 0 = disabled. Default value is 1. */
	int enable_annex_j;
	/* 1 = Annex.K support is enabled. 0 = disabled. Default value is 0. */
	int enable_annex_k;
	/* 1 = Annex.T support is enabled. 0 = disabled. Default value is 0. */
	int enable_annex_t;
}
ImxVpuEncH263Params;


/* h.264 parameters for the new encoder instance. */
typedef struct
{
	/* If set to 1, constrained intra prediction is enabled, as described
	 * in ISO/IEC 14496-10 section 7.4.2.2. If set to 0, it is disabled.
	 * Default value is 0. */
	int enable_constrained_intra_prediction;
	/* If set to 1, the deblocking filter at slice boundaries is disabled.
	 * If set to 0, it remains enabled. Default value is 0.
	 * This value corresponds to disable_deblocking_filter_idc in
	 * ISO/IEC 14496-10 section 7.4.3. */
	int disable_deblocking;
	/* Alpha offset for the deblocking filter. This corresponds to
	 * slice_alpha_c0_offset_div2 in ISO/IEC 14496-10 section 7.4.3.
	 * Default value is 6. */
	int deblock_filter_offset_alpha;
	/* Beta offset for the deblocking filter. This corresponds to
	 * slice_beta_offset_div2 in ISO/IEC 14496-10 section 7.4.3.
	 * Default value is 0. */
	int deblock_filter_offset_beta;
	/* Chroma offset for QP chroma value indices. This corresponds to
	 * chroma_qp_index_offset in ISO/IEC 14496-10 section 7.4.3.
	 * Default value is 0. */
	int chroma_qp_offset;
	/* If set to 1, the encoder produces access unit delimiters.
	 * If set to 0, this is disabled. Default value is 0. */
	int enable_access_unit_delimiters;
}
ImxVpuEncH264Params;


/* Motion JPEG parameters for the new encoder instance. */
typedef struct
{
	/* Quality factor for JPEG encoding, between 0 (worst quality, best
	 * compression) and 100 (best quality, worst compression). Default
	 * value is 85.
	 * This quality factor is the one from the Independent JPEG Group's
	 * formula for generating a scale factor out of the quality factor.
	 * This means that this quality factor is exactly the same as the
	 * one used by libjpeg. */
	unsigned int quality_factor;
}
ImxVpuEncMJPEGParams;


/* Structure used together with imx_vpu_enc_open() */
typedef struct
{
	/* Format encoded data to produce. */
	ImxVpuCodecFormat codec_format;

	/* Width and height of the incoming frames, in pixels. These
	 * do not have to be aligned to any boundaries. */
	unsigned int frame_width, frame_height;
	/* Frame rate, given as a rational number. */
	unsigned int frame_rate_numerator;
	unsigned int frame_rate_denominator;
	/* Bitrate in kbps. If this is set to 0, rate control is disabled, and
	 * constant quality mode is active instead. This value is ignored for
	 * MJPEG (which essentially always operates in constant quality mode).
	 * Default value is 100. */
	unsigned int bitrate;
	/* Size of the Group of Pictures. It specifies the number of frames
	 * between the initial I frames of each group. Therefore, 0 causes the
	 * VPU to only produce I frames, 1 allows for one P frame in between,
	 * 2 for two P frames etc. MJPEG ignores this value, and behaves as if
	 * it were set to 0 (= produces only I frames). Maximum value is 32767.
	 * Default value is 16. */
	unsigned int gop_size;
	/* Color format to use for incoming frames. Only MJPEG actually uses
	 * this value; other codec formats always use IMX_VPU_COLOR_FORMAT_YUV420.
	 * See the ImxVpuColorFormat documentation for an explanation how
	 * the chroma_interleave value can affec the pixel format that is used. */
	ImxVpuColorFormat color_format;

	/* User defined minimum allowed qp value. Not used in constant quality mode.
	 * This is a constraint for the rate control's qp estimation. -1 causes
	 * the VPU to use its internal default limit for the given codec format.
	 * Default value is -1. */
	int user_defined_min_qp;
	/* User defined maximum allowed qp value. Not used in constant quality mode.
	 * This is a constraint for the rate control's qp estimation. -1 causes
	 * the VPU to use its internal default limit for the given codec format.
	 * Default value is -1. */
	int user_defined_max_qp;

	/* How many macroblocks at least to encode as intra macroblocks in every
	 * P frame. If this is set to 0, intra macroblocks are not used. This value
	 * is ignored for MJPEG. Default value is 0. */
	int min_intra_refresh_mb_count;
	/* Quantization parameter for I frames. -1 instructs the VPU to automatically
	 * determine its value. Valid range is 1-31 for MPEG4 and h.263, and 0-51
	 * for h.264. MJPEG ignores this value. Default value is -1. */
	int intra_qp;

	/* Smoothness factor for qp (quantization parameter) estimation. Not used
	 * in constant quality mode. Valid value are between 0 and 32768. Default
	 * value is 24576 (= 0.75 * 32768). Low values cause the qp to change
	 * slowly, high values cause it to change quickly. */
	unsigned int qp_estimation_smoothness;

	/* Rate control mode to use. This defines the intervals for race control
	 * updates. For user defined intervals, macroblock_interval must also be
	 * set. Not used in constant quality mode. Default value is
	 * IMX_VPU_ENC_RATE_CONTROL_MODE_NORMAL. */
	ImxVpuEncRateControlModes rate_control_mode;
	/* User defined macroblock interval. This value is only used if no constant
	 * quakity control mode is active and if rate_control_mode is set to
	 * IMX_VPU_ENC_RATE_CONTROL_MODE_USER_DEFINED_LEVEL. */
	unsigned int macroblock_interval;

	/* Encoding slice mode to use. */
	ImxVpuEncSliceMode slice_mode;

	/* delay in milliseconds for the bitstream to fully occupy the vbv buffer
	 * starting from an empty buffer. In constant quality mode, this value is
	 * ignored. 0 means the buffer size constraints are not checked for.
	 * Default value is 0. */
	unsigned int initial_delay;
	/* Size of the vbv buffer, in bits. This is only used if initial_delay is
	 * nonzero and if rate control is active (= the bitrate in ImxVpuEncOpenParams
	 * is nonzero). 0 means the buffer size constraints are not checked for.
	 * Default value is 0. */
	unsigned int vbv_buffer_size;

	/* Search range for motion estimation computation. Default value is
	 * IMX_VPU_ENC_ME_SEARCH_RANGE_256x128. */
	ImxVpuEncMESearchRanges me_search_range;
	/* If this is 0, then during encoding, the current pmv (predicted motion vector)
	 * is derived from the neighbouring pmv. If it is 1, a zero PMV is used.
	 * Default value is 0. */
	int use_me_zero_pmv;
	/* Additional weight factor for deciding whether to generate intra- or
	 * inter-macroblocks. The VPU computes a weight factor, and adds this value to it.
	 * A higher combined weight factor tends to produce more inter-macroblocks.
	 * Default value is 0. */
	unsigned int additional_intra_cost_weight;

	/* Additional codec format specific parameters. */
	union
	{
		ImxVpuEncMPEG4Params mpeg4_params;
		ImxVpuEncH263Params h263_params;
		ImxVpuEncH264Params h264_params;
		ImxVpuEncMJPEGParams mjpeg_params;
	}
	codec_params;

	/* If this is 1, then Cb and Cr are interleaved in one shared chroma
	 * plane, otherwise they are separated in their own planes.
	 * See the ImxVpuColorFormat documentation for the consequences of this. */
	int chroma_interleave;
}
ImxVpuEncOpenParams;


/* Initial encoding information, produced by the encoder. This structure is
 * essential to actually begin encoding, since it contains all of the
 * necessary information to create and register enough framebuffers. */
typedef struct
{
	/* Caller must register at least this many framebuffers
	 * with the encoder. */
	unsigned int min_num_required_framebuffers;

	/* Physical framebuffer addresses must be aligned to this value. */
	unsigned int framebuffer_alignment;
}
ImxVpuEncInitialInfo;


/* Function pointer used during encoding for acquiring output buffers.
 * See imx_vpu_enc_encode() for details about the encoding process.
 * context is the value of output_buffer_context specified in
 * ImxVpuEncParams. size is the size of the block to acquire, in bytes.
 * acquired_handle is an output value; the function can set this to a
 * handle that corresponds to the acquired buffer. For example, in
 * libav/FFmpeg, this handle could be a pointer to an AVBuffer. In
 * GStreamer, this could be a pointer to a GstBuffer. The value of
 * *acquired_handle will later be copied to the acquired_handle value
 * of ImxVpuEncodedFrame.
 * The return value is a pointer to a memory-mapped region of the
 * output buffer, or NULL if acquiring failed.
 * This function is only used by imx_vpu_enc_encode(). */
typedef void* (*ImxVpuEncAcquireOutputBuffer)(void *context, size_t size, void **acquired_handle);
/* Function pointer used during encoding for notifying that the encoder
 * is done with the output buffer. This is *not* a function for freeing
 * allocated buffers; instead, it makes it possible to release, unmap etc.
 * context is the value of output_buffer_context specified in
 * ImxVpuEncParams. acquired_handle equals the value of *acquired_handle in
 * ImxVpuEncAcquireOutputBuffer. */
typedef void (*ImxVpuEncFinishOutputBuffer)(void *context, void *acquired_handle);


typedef struct
{
	/* If set to 1, this forces the encoder to produce an I frame.
	 * 0 disables this. Default value is 0. */
	int force_I_frame;
	/* If set to 1, the VPU ignores the given source picture, and
	 * instead generates a "skipped picture". If such a picture is
	 * reconstructed, it is a duplicate of the preceding picture.
	 * This skipped picture is encoded as a P frame.
	 * 0 disables skipped picture generation. Default value is 0. */
	int skip_picture;
	/* If set to 1, the rate control mechanism can automatically
	 * decide to use skipped pictures. This is ignored if rate
	 * control is disabled (= if the bitrate value is nonzero in
	 * ImxVpuEncOpenParams). Default value is 0. */
	int enable_autoskip;

	/* Functions for acquiring and finishing output buffers. See the
	 * typedef documentations above for details about how these
	 * functions should behave, and the imx_vpu_enc_encode()
	 * documentation for how they are used. */
	ImxVpuEncAcquireOutputBuffer acquire_output_buffer;
	ImxVpuEncFinishOutputBuffer finish_output_buffer;
	void *output_buffer_context;

	/* Quantization parameter. Its value and valid range depends on
	 * the codec format the encoder has been configured to use.
	 * This value is ignored is rate control mode is enabled
	 * (= the bitrate value in ImxVpuEncOpenParams was set to a
	 * nonzero value). For MPEG-4 and h.263, the valid range is 1-31,
	 * where 1 produces the best quality, and 31 the best compression.
	 * For h.264, the valid range is 0-51, where 0 is the best quality,
	 * and 51 the best compression. This value is not used for MJPEG;
	 * its quality factor can only be set in the ImxVpuEncOpenParams
	 * structure.
	 * NOTE: since this parameter is codec specific, no default value
	 * is set by imx_vpu_enc_set_default_encoding_params(). */
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

/* Flushes the encoder. Any internal temporary data is discarded. */
ImxVpuEncReturnCodes imx_vpu_enc_flush(ImxVpuEncoder *encoder);

/* Registers the specified array of framebuffers with the encoder. These framebuffers are used for temporary
 * values during encoding, unlike the decoder framebuffers. The minimum valid value for "num_framebuffers" is
 * the "min_num_required_framebuffers" field of ImxVpuEncInitialInfo. */
ImxVpuEncReturnCodes imx_vpu_enc_register_framebuffers(ImxVpuEncoder *encoder, ImxVpuFramebuffer *framebuffers, unsigned int num_framebuffers);

/* Retrieves initial information available after calling imx_vpu_enc_open(). */
ImxVpuEncReturnCodes imx_vpu_enc_get_initial_info(ImxVpuEncoder *encoder, ImxVpuEncInitialInfo *info);

/* Set the fields in "encoding_params" to valid defaults
 * Useful if the caller wants to modify only a few fields (or none at all) */
void imx_vpu_enc_set_default_encoding_params(ImxVpuEncoder *encoder, ImxVpuEncParams *encoding_params);

/* Sets/updates the bitrate. This allows for controlled bitrate updates during encoding. Calling this
 * function is optional; by default, the bitrate from the open_params in imx_vpu_enc_open() is used. */
void imx_vpu_enc_configure_bitrate(ImxVpuEncoder *encoder, unsigned int bitrate);
/* Sets/updates the minimum number of macroblocks to refresh in a P frame. */
void imx_vpu_enc_configure_min_intra_refresh(ImxVpuEncoder *encoder, unsigned int min_intra_refresh_num);
/* Sets/updates a constant I-frame quantization parameter (1-31 for MPEG-4, 0-51 for h.264). -1 enables
 * automatic selection by the VPU. Calling this function is optional; by default, the intra QP value from
 * the open_params in imx_vpu_enc_open() is used. */
void imx_vpu_enc_configure_intra_qp(ImxVpuEncoder *encoder, int intra_qp);

/* Encodes a given input picture with the given encoding parameters. encoded_frame is filled with information
 * about the resulting encoded output frame. The encoded frame data itself is stored in a buffer that is
 * allocated by user-supplied functions (which are set as the acquire_output_buffer and finish_output_buffer
 * function pointers in the encoding_params).
 *
 * Encoding internally works as follows: first, the actual encoding operation is performed by the VPU. Next,
 * information about the encoded data is queried, particularly its size in bytes. Once this size is known,
 * acquire_output_buffer() from encoding_params is called. This function must acquire a buffer that can be
 * used to store the encoded data. This buffer must be at least as large as the size of the encoded data
 * (which is given to acquire_output_buffer() as an argument). The return value of acquire_output_buffer()
 * is a pointer to the (potentially memory-mapped) region of the buffer. The encoded frame data is then
 * copied to this buffer, and finish_output_buffer() is called. This function can be used to inform the
 * caller tha the encoder is done with this buffer; it now contains encoded data, and will not be modified
 * further. encoded_frame is filled with information about the encoded frame data.
 * If acquiring the buffer fails, acquire_output_buffer() returns a NULL pointer.
 * NOTE: again, finish_output_buffer() is NOT a function to free the buffer; it just signals that the encoder
 * won't touch the memory inside the buffer anymore.
 *
 * acquire_output_buffer() can also pass on a handle to the acquired buffer (for example, in FFmpeg/libav,
 * this handle would be a pointer to an AVBuffer). The handle is called the "acquired_handle".
 * acquire_output_buffer() can return such a handle. This handle is copied to the encoded_frame struct's
 * acquired_handle field. This way, a more intuitive workflow can be used; if for example, acquire_output_buffer()
 * returns an AVBuffer pointer as the handle, this AVBuffer pointer ends up in the encoded_frame. Afterwards,
 * encoded_frame contains all the necessary information to process the encoded frame data.
 *
 * It is guaranteed that once the buffer was acquired, finish_output_buffer() will always be called, even if
 * an error occurs. This prevents potential memory/resource leaks if the finish_output_buffer() call somehow
 * unlocks or releases the buffer for further processing.
 *
 * The other fields in encoding_params specify additional encoding parameters, which can vary from frame to
 * frame.
 * output_code is a bit mask containing information about the encoding result. The value is a bitwise OR
 * combination of the codes in ImxVpuEncOutputCodes.
 *
 * None of the arguments may be NULL. */
ImxVpuEncReturnCodes imx_vpu_enc_encode(ImxVpuEncoder *encoder, ImxVpuPicture *picture, ImxVpuEncodedFrame *encoded_frame, ImxVpuEncParams *encoding_params,  unsigned int *output_code);




#ifdef __cplusplus
}
#endif


#endif
