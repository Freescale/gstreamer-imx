/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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

#ifndef GST_IMX_VPU_ENC_H
#define GST_IMX_VPU_ENC_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <imxvpuapi2/imxvpuapi2.h>
#include "gstimxvpucommon.h"
#include "gst/imx/common/gstimxdmabufferuploader.h"


G_BEGIN_DECLS


#define GST_TYPE_IMX_VPU_ENC             (gst_imx_vpu_enc_get_type())
#define GST_IMX_VPU_ENC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_ENC,GstImxVpuEnc))
#define GST_IMX_VPU_ENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_ENC,GstImxVpuEncClass))
#define GST_IMX_VPU_ENC_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VPU_ENC, GstImxVpuEncClass))
#define GST_IMX_VPU_ENC_CAST(obj)        ((GstImxVpuEnc *)(obj))
#define GST_IS_IMX_VPU_ENC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_ENC))
#define GST_IS_IMX_VPU_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_ENC))

#define GST_IMX_VPU_ENC_BASE_PROP_VALUE  100


typedef struct _GstImxVpuEnc GstImxVpuEnc;
typedef struct _GstImxVpuEncClass GstImxVpuEncClass;


struct _GstImxVpuEnc
{
	GstVideoEncoder parent;

	/* The stream buffer that is needed by the decoder for all
	 * of its decoding operations. Created in gst_imx_vpu_enc_start(). */
	GstMemory *stream_buffer;
	/* The actual libimxvpuapi encoder. Created in
	 * gst_imx_vpu_enc_set_format(). */
	ImxVpuApiEncoder *encoder;
	/* Pointer to the constant, static global encoder
	 * information from libimxvpuapi. */
	ImxVpuApiEncGlobalInfo const *enc_global_info;
	/* Copy of the stream info received right after opening the
	 * libimxvpuapi encoder instance. */
	ImxVpuApiEncStreamInfo current_stream_info;
	/* The parameters that are passed on to the imx_vpu_api_enc_open()
	 * call that opens a libimxvpuapi encoder instance. */
	ImxVpuApiEncOpenParams open_params;
	/* libimxdmabuffer-based DMA buffer allocator that is used for
	 * allocating the stream buffer and the VPU framebuffer pool buffers.
	 * Depending on the configuration, this may or may not be the
	 * DMA-BUF backed GstImxIonAllocator. */
	GstAllocator *default_dma_buf_allocator;

	/* Current DMA buffer pool. Created in
	 * gst_imx_vpu_enc_set_format() by calling
	 * gst_imx_vpu_enc_create_dma_buffer_pool(). */
	GstBufferPool *dma_buffer_pool;

	/* Used for uploading incoming buffers into ImxDmaBuffer-backed
	 * GstMemory that we can use with the VPU encoder. */
	GstImxDmaBufferUploader *uploader;
	/* The uploader produces new gstbuffers with the uploaded variants
	 * of input buffers. These are stored here, and get removed once the
	 * corresponding input frames got fully processed by the encoder.
	 * This table helps keeping track of these temp buffers at all times. */
	GHashTable *uploaded_buffers_table;

	/* The GstBufferList that was created to act as the backing store
	 * for the VPU's framebuffer pool. */
	GstBufferList *fb_pool_buffers;

	/* Sometimes, even after one of the GstVideoEncoder vfunctions
	 * reports an error, processing continues. This flag is intended
	 * to handle such cases. If set to TRUE, several functions such as
	 * gst_imx_vpu_enc_handle_frame() will exit early. The flag is
	 * cleared once the encoder is restarted. */
	gboolean fatal_error_cannot_encode;

	/* Copy of the GstVideoInfo that describes the raw input frames. */
	GstVideoInfo in_video_info;

	/* GObject property values. */
	guint gop_size;
	guint bitrate;
	guint quantization;
	guint intra_refresh;
};


struct _GstImxVpuEncClass
{
	GstVideoEncoderClass parent_class;

	/* Virtual functions that are implemented by subclasses to allow
	 * them to installtheir own GObject properties. This is necessary
	 * because gst_imx_vpu_enc_common_class_init() already sets the
	 * subclasses' set_property and get_property vfunctions. */
	void (*set_encoder_property)(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
	void (*get_encoder_property)(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

	gboolean (*set_open_params)(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params);
	GstCaps* (*get_output_caps)(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info);
};


void gst_imx_vpu_enc_common_class_init(GstImxVpuEncClass *klass, ImxVpuApiCompressionFormat compression_format, gboolean with_rate_control, gboolean with_constant_quantization, gboolean with_gop_support);
void gst_imx_vpu_enc_common_init(GstImxVpuEnc *imx_vpu_enc);

GType gst_imx_vpu_enc_get_type(void);


G_END_DECLS


#endif /* GST_IMX_VPU_ENC_H */
