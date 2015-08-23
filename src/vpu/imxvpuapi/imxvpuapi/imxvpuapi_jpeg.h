/* Simplified API for JPEG en- and decoding with the Freescale i.MX SoC
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


/* This is a convenience interface for simple en- and decoding of JPEG data.
 * For merely en/decoding JPEGs, having to set up a VPU en/decoder involves
 * a considerable amount of boilerplate code. This interface takes care of
 * these details, and presents a much simpler interface focused on this one
 * task: to en/decode JPEGs. */

#ifndef IMXVPUAPI_JPEG_H
#define IMXVPUAPI_JPEG_H

#include "imxvpuapi.h"


#ifdef __cplusplus
extern "C" {
#endif


typedef struct
{
	/* Width and height of VPU framebuffers are aligned to internal boundaries.
	 * The picture consists of the actual image pixels and extra padding pixels.
	 * aligned_frame_width / aligned_frame_height specify the full width/height
	 * including the padding pixels, and actual_frame_width / actual_frame_height
	 * specify the width/height without padding pixels. */
	unsigned int aligned_frame_width, aligned_frame_height;
	unsigned int actual_frame_width, actual_frame_height;

	/* Stride and size of the Y, Cr, and Cb planes. The Cr and Cb planes always
	 * have the same stride and size. */
	unsigned int y_stride, cbcr_stride;
	unsigned int y_size, cbcr_size;

	/* Offset from the start of a framebuffer's memory, in bytes. Note that the
	 * Cb and Cr offset values are *not* the same, unlike the stride and size ones. */
	unsigned int y_offset, cb_offset, cr_offset;

	/* Color format of the decoded picture. */
	ImxVpuColorFormat color_format;
}
ImxVpuJPEGInfo;


typedef struct _ImxVpuJPEGDecoder ImxVpuJPEGDecoder;

/* Opens a new VPU JPEG decoder instance.
 *
 * Internally, this function calls imx_vpu_dec_load().
 *
 * If dma_buffer_allocator is NULL, the default decoder allocator is used.
 *
 * num_extra_framebuffers is used for instructing this function to allocate this many
 * more framebuffers. Usually this value is zero, but in certain cases where many
 * JPEGs need to be decoded quickly, or the DMA buffers of decoded pictures need to
 * be kept around elsewhere, having more framebuffers available can be helpful.
 * Note though that more framebuffers also means more DMA memory consumption. */
ImxVpuDecReturnCodes imx_vpu_jpeg_dec_open(ImxVpuJPEGDecoder **jpeg_decoder, ImxVpuDMABufferAllocator *dma_buffer_allocator, unsigned int num_extra_framebuffers);

/* Closes a JPEG decoder instance. Trying to close the same instance multiple times results in undefined behavior. */
ImxVpuDecReturnCodes imx_vpu_jpeg_dec_close(ImxVpuJPEGDecoder *jpeg_decoder);

/* Determines if the VPU can decode a frame at this moment.
 *
 * The return value depends on how many framebuffers in the decoder are free.
 * If enough framebuffers are free, this returns 1, otherwise 0.
 *
 * For simple decoding schemes where one frame is decoded, then displayed or
 * consumed in any other way, and then returned to the decoder by calling
 * @imx_vpu_jpeg_dec_picture_finished, this function does not have to be used,
 * since in this case, there will always be enough free framebuffers.
 * If however the consumption of the decoded frame occurs in a different thread
 * than the decoding, it makes sense to use this function. Also, in this case,
 * this function is more likely to return 1 the more extra framebuffers were
 * requested in the @imx_vpu_jpeg_dec_open call.
 */
int imx_vpu_jpeg_dec_can_decode(ImxVpuJPEGDecoder *jpeg_decoder);

/* Decodes a JPEG frame.
 *
 * In encoded_frame, data.virtual_address must be set to the memory block that
 * contains the encoded JPEG data, and data_size must be set to the size of that
 * block, in bytes. The values of picture will be then filled with data about
 * the decoded picture. In particular, the picture.framebuffer pointer is NULL
 * if no picture could be decoded, otherwise it points to the framebuffer that
 * contains the decoded pixels. (In the picture, the pic_type and context values
 * are meaningless when decoding JPEGs.)
 *
 * Note that the return value can be IMX_VPU_DEC_RETURN_CODE_OK even though
 * no picture was returned. This is the case when not enough free framebuffers
 * are present. It is recommended to check the return value of the
 * @imx_vpu_jpeg_dec_can_decode function before calling this, unless the decoding
 * sequence is simple (like in the example mentioned in the @imx_vpu_jpeg_dec_can_decode
 * description). */
ImxVpuDecReturnCodes imx_vpu_jpeg_dec_decode(ImxVpuJPEGDecoder *jpeg_decoder, ImxVpuEncodedFrame *encoded_frame, ImxVpuPicture *picture);

/* Retrieves information about the decoded JPEG picture.
 *
 * This function must not be called before @imx_vpu_jpeg_dec_decode , since otherwise,
 * there is no information available (it is read in the decoding step). */
void imx_vpu_jpeg_dec_get_info(ImxVpuJPEGDecoder *jpeg_decoder, ImxVpuJPEGInfo *info);

/* Inform the JPEG decoder that this picture is no longer being used.
 *
 * This function must always be called once the user is done with a picture,
 * otherwise the VPU cannot reclaim its associated framebuffer, and will
 * eventually run out of pictures to decode into. */
ImxVpuDecReturnCodes imx_vpu_jpeg_dec_picture_finished(ImxVpuJPEGDecoder *jpeg_decoder, ImxVpuPicture *picture);


typedef struct _ImxVpuJPEGEncoder ImxVpuJPEGEncoder;

ImxVpuEncReturnCodes imx_vpu_jpeg_enc_open(ImxVpuJPEGEncoder **jpeg_encoder, ImxVpuDMABufferAllocator *dma_buffer_allocator, unsigned int frame_width, unsigned int frame_height, unsigned int frame_rate_numerator, unsigned int frame_rate_denominator);
ImxVpuEncReturnCodes imx_vpu_jpeg_enc_close(ImxVpuJPEGEncoder *jpeg_encoder);
ImxVpuEncReturnCodes imx_vpu_jpeg_enc_encode(ImxVpuJPEGEncoder *jpeg_encoder, ImxVpuPicture *picture, ImxVpuEncodedFrame *encoded_frame);


#ifdef __cplusplus
}
#endif


#endif
