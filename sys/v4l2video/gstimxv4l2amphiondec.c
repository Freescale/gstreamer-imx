/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2022  Carlos Rafael Giani
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

#include <time.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <imxdmabuffer/imxdmabuffer.h>

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideometa.h>

#include "gst/imx/common/gstimxdmabufallocator.h"
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gst/imx/video/gstimxvideobufferpool.h"
#include "gstimxv4l2amphiondec.h"
#include "gstimxv4l2amphionmisc.h"

#include "imx2d/imx2d.h"
#include "imx2d/backend/g2d/g2d_blitter.h"


GST_DEBUG_CATEGORY_STATIC(imx_v4l2_amphion_dec_debug);
GST_DEBUG_CATEGORY_STATIC(imx_v4l2_amphion_dec_in_debug);
GST_DEBUG_CATEGORY_STATIC(imx_v4l2_amphion_dec_out_debug);
#define GST_CAT_DEFAULT imx_v4l2_amphion_dec_debug


/* NXP Amphion Malone driver specific V4L2 control for
 * disabling frame reordering in the driver. */
#ifndef V4L2_CID_USER_FRAME_DIS_REORDER
#define V4L2_CID_USER_FRAME_DIS_REORDER      (V4L2_CID_USER_BASE + 0x1300)
#endif

/* NXP Amphion Malone driver specific V4L2 event that
 * notifies subscribers when a frame was skipped.
 * Unfortunately, there's no attached information about
 * which frame was skipped. */
#ifndef V4L2_NXP_AMPHION_EVENT_SKIP
#define V4L2_NXP_AMPHION_EVENT_SKIP          (V4L2_EVENT_PRIVATE_START + 2)
#endif

/* We need 2 buffers for the output queue, where encoded frames
 * are pushed to be decoder. One buffer is in the queue, the
 * other is available for accepting more encoded data. */
#define DEC_MIN_NUM_REQUIRED_OUTPUT_BUFFERS 2

/* We allocate 2 MB for each output v4l2_buffer. This gives
 * us plenty of room. Encoded frames are expected to be
 * far smaller than this. */
#define DEC_REQUESTED_OUTPUT_BUFFER_SIZE (2 * 1024 * 1024)

/* The number of planes in capture buffers. The Amphion
 * Malone decoder always produces NV12 data (8 or 10 bit), so
 * there are always exactly 2 planes (one Y- and one UV-plane).
 * Note that the actual _output_ of the decoder can be something
 * different, since there is a detiling process in between the
 * dequeuing of the capture buffers and the actual decoder output.
 * That detiling can produce a number of color formats. */
#define DEC_NUM_CAPTURE_BUFFER_PLANES 2

/* A stride alignment of 128 is required for the Amphion detiling.
 * Note that this is required for the _destination_ surface.
 * If that surface is not aligned this way, the resulting detiled
 * frames are corrupted. The _source_ surface is not affected. */
#define G2D_DEST_AMPHION_STRIDE_ALIGNMENT 128

#define ALIGN_VAL_TO(VALUE, ALIGN_SIZE) \
	( \
		( \
			((VALUE) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE) \
		) * (ALIGN_SIZE) \
	)


/* Structure for housing a V4L2 output buffer and its associated plane structure.
 * Note that "output" is V4L2 mem2mem decoder terminology for "encoded data". */
typedef struct
{
	/* The buffer's "planes" pointer is set to point to the "plane" instance
	 * below when the decoder's output_buffer_items are allocated.
	 * This happens in the imx_vpu_api_dec_open() function. */
	struct v4l2_buffer buffer;
	/* Since the Amphion decoder uses the multi-planar API, we need to
	 * specify a plane structure. (Encoded data uses exactly 1 "plane"). */
	struct v4l2_plane plane;
}
DecV4L2OutputBufferItem;


/* Structure for housing a V4L2 capture buffer and its associated
 * plane structure and DMA-BUF FDs & physical addresses for the planes. */
typedef struct
{
	/* The buffer's "planes" pointer is set to point to the "plane" instance
	 * below when the decoder's capture_buffer_items are allocated.
	 * This happens in the imx_vpu_api_dec_handle_resolution_change() function. */
	struct v4l2_buffer buffer;
	/* Since the Amphion decoder uses the multi-planar API, we need to
	 * specify a plane structure. */
	struct v4l2_plane planes[DEC_NUM_CAPTURE_BUFFER_PLANES];
	/* FD and physical address of the planes, exported as DMA-BUF.
	 * The FD is retrieved from V4L2 via VIDIOC_EXPBUF. The physical
	 * address is extracted out of that FD. */
	int dmabuf_fds[DEC_NUM_CAPTURE_BUFFER_PLANES];
	imx_physical_address_t physical_addresses[DEC_NUM_CAPTURE_BUFFER_PLANES];
	ImxWrappedDmaBuffer wrapped_imx_dma_buffers[DEC_NUM_CAPTURE_BUFFER_PLANES];
}
DecV4L2CaptureBufferItem;


static gboolean frame_reordering_required_always(G_GNUC_UNUSED GstStructure *format)
{
	return TRUE;
}

static gboolean frame_reordering_required_never(G_GNUC_UNUSED GstStructure *format)
{
	return TRUE;
}

static gboolean h264_is_frame_reordering_required(GstStructure *format)
{
	gchar const *media_type_str;
	gchar const *profile_str;

	g_assert(format != NULL);

	/* Disable frame reordering if we are handling h.264 baseline / constrained
	 * baseline. These h.264 profiles do not use frame reodering, the Amphion
	 * Malone VPU decoder seems to actually have lower latency when it is disabled. */

	media_type_str = gst_structure_get_name(format);
	g_assert(g_strcmp0(media_type_str, "video/x-h264") == 0);

	profile_str = gst_structure_get_string(format, "profile");

	return (profile_str == NULL) || ((g_strcmp0(profile_str, "constrained-baseline") != 0) && (g_strcmp0(profile_str, "baseline") != 0));
}

typedef struct
{
	gchar const *element_name_suffix;
	gchar const *class_name_suffix;
	gchar const *desc_name;
	guint32 v4l2_pixelformat;
	gboolean requires_codec_data;
	gboolean (*is_frame_reordering_required)(GstStructure *format);
}
GstImxV4L2AmphionDecSupportedFormatDetails;


/* IMPORTNT:
 *
 * V4L2 mem2mem terminology can be confusing. In a mem2mem decoder,
 * the output queue actually is given the *input* (that is, the encoded data),
 * and the capture queue provides the *output* (the decoded frames). To reduce
 * confusion, the V4L2 output/capture entities are prefixed with "v4l2_". */

struct _GstImxV4L2AmphionDec
{
	GstVideoDecoder parent;

	/*< private >*/

	/* The flow error that was reported in the last decoder loop run.
	 * GST_FLOW_OK indicates that no error happened. Any other value
	 * implies that the decoder loop srcpad task is paused.
	 * The recipient of these errors is handle_frame(). That function
	 * reads the current value of this field, then sets it back to
	 * GST_FLOW_OK. Afterwards, if the field contained a non-OK value,
	 * handle_frame() exits immediately, returning that flow error.
	 * start() and flush() reset this field to GST_FLOW_OK. */
	GstFlowReturn decoder_loop_flow_error;

	/* File descriptor for the V4L2 device. Opened in set_format(). */
	int v4l2_fd;

	/* Out-of-band codec data along with mapping information.
	 * See the code in set_format() for details. */
	// TODO: This is currently unused.
	GstBuffer *codec_data;
	GstMapInfo codecdata_map_info;
	gboolean codec_data_is_mapped;

	/* Input and output video codec states. The input state is
	 * set in set_format(). The output state is set when the
	 * V4L2_EVENT_SOURCE_CHANGE event is observed. */ 
	GstVideoCodecState *input_state;
	GstVideoCodecState *output_state;

	/* If set to true, frame reordering is enabled. This is set
	 * in set_format and depends on the return value of the
	 * is_frame_reordering_required function from the
	 * GstImxV4L2AmphionDecSupportedFormatDetails structure. */
	gboolean use_frame_reordering;

	/* DMA buffer pool for decoded frames. Created in decide_allocation().
	 * This is a special buffer pool that can contain two internal pools
	 * to facilitate CPU based copies if necessary. See the documentation
	 * of GstImxVideoBufferPool for details. */
	GstImxVideoBufferPool *video_buffer_pool;
	/* Allocator for the frames from video_buffer_pool. This must be
	 * based on GstImxDmaBufAllocator, since when the V4L2 resolution
	 * change event is received, physical addresses for DMA-BUF FDs must
	 * be fetched using gst_imx_dmabuf_allocator_get_physical_address(). */
	GstAllocator *imx_dma_buffer_allocator;

	/* Sometimes, even after one of the GstVideoDecoder vfunctions
	 * reports an error, processing continues. This flag is intended
	 * to handle such cases. If set to TRUE, several functions such as
	 * gst_imx_v4l2_amphion_dec_handle_frame() will exit early. The flag
	 * is cleared once the decoder is restarted. */
	gboolean fatal_error_cannot_decode;

	/* imx2d G2D blitter and surfaces, needed for detiling decoded frames,
	 * since the Amphion Malone VPU only produces Amphion-tiled frames. */
	Imx2dBlitter *g2d_blitter;
	Imx2dSurface *tiled_surface;
	Imx2dSurface *detiled_surface;
	Imx2dSurfaceDesc tiled_surface_desc;
	Imx2dSurfaceDesc detiled_surface_desc;

	/* The format of the final output frames that are producd at the end of
	 * the Malone decoder -> detiler -> video_buffer_pool chain. (The last
	 * one may involve CPU based frame copies; see GstImxVideoBufferPool for
	 * details.) It is set in set_format(). */
	GstVideoFormat final_output_format;

	/* Video info describing the result of the detiler. This is what comes
	 * between detiler and GstImxVideoBufferPool. It is set when the
	 * V4L2 source change event is observed. */
	GstVideoInfo detiler_output_info;

	/*** V4L2 output queue states. ***/

	GstPoll *v4l2_output_queue_poll;
	GstPollFD v4l2_output_queue_fd;

	/* Array of allocated output buffer items that contain V4L2 output buffers.
	 * There is exactly one output buffer item for each V4L2 output buffer that
	 * was allocated with the VIDIOC_REQBUFS ioctl. */
	DecV4L2OutputBufferItem *v4l2_output_buffer_items;
	int num_v4l2_output_buffers;

	/* TRUE if the output queue was enabled with the VIDIOC_STREAMON ioctl. */
	gboolean v4l2_output_stream_enabled;

	/* The actual output buffer format, retrieved by using the VIDIOC_G_FMT ioctl.
	 * The driver may pick a format that differs from the requested format
	 * (requested with the VIDIOC_S_FMT ioctl), so we store the actual format here. */
	struct v4l2_format v4l2_output_buffer_format;

	/* Size in bytes of one V4L2 output buffer. This needs to be
	 * passed to mmap() when writing encoded data to such a buffer. */
	int v4l2_output_buffer_size;

	/* How many of the output buffers have been pushed into the output queue
	 * with the VIDIOC_QBUF ioctl and haven't yet been dequeued again. */
	int num_v4l2_output_buffers_in_queue;

	/*** V4L2 capture queue states. ***/

	GstPoll *v4l2_capture_queue_poll;
	GstPollFD v4l2_capture_queue_fd;

	/* Array of allocated capture buffer items that contain V4L2 capture buffers.
	 * There is exactly one capture buffer item for each V4L2 capture buffer that
	 * was allocated with the VIDIOC_REQBUFS ioctl when the resolution change
	 * event is observed. */
	DecV4L2CaptureBufferItem *v4l2_capture_buffer_items;
	int num_v4l2_capture_buffers;

	/* TRUE if the capture queue was enabled with the VIDIOC_STREAMON ioctl. */
	gboolean v4l2_capture_stream_enabled;

	/* The actual capture buffer format, retrieved by using the VIDIOC_G_FMT ioctl.
	 * The driver may pick a format that differs from the requested format
	 * (requested with the VIDIOC_S_FMT ioctl), so we store the actual format here. */
	struct v4l2_format v4l2_capture_buffer_format;
};


struct _GstImxV4L2AmphionDecClass
{
	GstVideoDecoderClass parent_class;

	gboolean (*is_frame_reordering_required)(GstStructure *format);

	gboolean requires_codec_data;
};


static GQuark gst_imx_v4l2_amphion_dec_format_details_quark(void)
{
	return g_quark_from_static_string("gst-imx-v4l2-amphion-dec-format-details-quark");
}


/* Helper macro to access the supported format details that are stored
 * inside a GObject class. */
#define GST_IMX_V4L2_AMPHION_DEC_GET_ELEMENT_COMPRESSION_FORMAT(obj) \
	((GstImxV4L2AmphionDecSupportedFormatDetails const *)g_type_get_qdata(G_OBJECT_CLASS_TYPE(GST_OBJECT_GET_CLASS(obj)), gst_imx_v4l2_amphion_dec_format_details_quark()))


G_DEFINE_ABSTRACT_TYPE(GstImxV4L2AmphionDec, gst_imx_v4l2_amphion_dec, GST_TYPE_VIDEO_DECODER)


static GstStateChangeReturn gst_imx_v4l2_amphion_dec_change_state(GstElement *element, GstStateChange transition);

static gboolean gst_imx_v4l2_amphion_dec_start(GstVideoDecoder *decoder);
static gboolean gst_imx_v4l2_amphion_dec_stop(GstVideoDecoder *decoder);
static gboolean gst_imx_v4l2_amphion_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state);
static GstFlowReturn gst_imx_v4l2_amphion_dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *cur_frame);
static gboolean gst_imx_v4l2_amphion_dec_flush(GstVideoDecoder *decoder);
static GstFlowReturn gst_imx_v4l2_amphion_dec_drain(GstVideoDecoder *decoder);
static GstFlowReturn gst_imx_v4l2_amphion_dec_finish(GstVideoDecoder *decoder);
static gboolean gst_imx_v4l2_amphion_dec_decide_allocation(GstVideoDecoder *decoder, GstQuery *query);

static gboolean gst_imx_v4l2_amphion_dec_enable_stream(GstImxV4L2AmphionDec *self, gboolean do_enable, enum v4l2_buf_type type);
static void gst_imx_v4l2_amphion_dec_cleanup_decoding_resources(GstImxV4L2AmphionDec *self);

static gboolean gst_imx_v4l2_amphion_dec_decoder_start_output_loop(GstImxV4L2AmphionDec *self);
static void gst_imx_v4l2_amphion_dec_decoder_stop_output_loop(GstImxV4L2AmphionDec *self);
static void gst_imx_v4l2_amphion_dec_decoder_output_loop(GstImxV4L2AmphionDec *self);
static gboolean gst_imx_v4l2_amphion_dec_handle_resolution_change(GstImxV4L2AmphionDec *self);
static GstVideoCodecFrame* gst_imx_v4l2_amphion_dec_get_oldest_frame(GstImxV4L2AmphionDec *self);
static GstFlowReturn gst_imx_v4l2_amphion_dec_process_skipped_frame(GstImxV4L2AmphionDec *self);
static GstFlowReturn gst_imx_v4l2_amphion_dec_process_decoded_frame(GstImxV4L2AmphionDec *self);


static void gst_imx_v4l2_amphion_dec_class_init(GstImxV4L2AmphionDecClass *klass)
{
	GstElementClass *element_class;
	GstVideoDecoderClass *video_decoder_class;

	GST_DEBUG_CATEGORY_INIT(imx_v4l2_amphion_dec_debug, "imxv4l2amphiondec", 0, "NXP i.MX V4L2 Amphion Malone decoder");
	GST_DEBUG_CATEGORY_INIT(imx_v4l2_amphion_dec_in_debug, "imxv4l2amphiondec_in", 0, "NXP i.MX V4L2 Amphion Malone decoder, input (= V4L2 output queue) code path");
	GST_DEBUG_CATEGORY_INIT(imx_v4l2_amphion_dec_out_debug, "imxv4l2amphiondec_out", 0, "NXP i.MX V4L2 Amphion Malone decoder, output (= V4L2 capture queue) code path");

	element_class = GST_ELEMENT_CLASS(klass);
	video_decoder_class = GST_VIDEO_DECODER_CLASS(klass);

	element_class->change_state = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_change_state);

	video_decoder_class->start             = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_start);
	video_decoder_class->stop              = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_stop);
	video_decoder_class->set_format        = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_set_format);
	video_decoder_class->handle_frame      = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_handle_frame);
	video_decoder_class->flush             = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_flush);
	video_decoder_class->drain             = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_drain);
	video_decoder_class->finish            = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_finish);
	video_decoder_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_imx_v4l2_amphion_dec_decide_allocation);

	klass->is_frame_reordering_required = NULL;
	klass->requires_codec_data = FALSE;
}


static void gst_imx_v4l2_amphion_dec_init(GstImxV4L2AmphionDec *self)
{
	self->decoder_loop_flow_error = GST_FLOW_OK;

	self->v4l2_fd = -1;

	self->codec_data = NULL;
	self->codec_data_is_mapped = FALSE;

	self->input_state = NULL;
	self->output_state = NULL;

	self->use_frame_reordering = FALSE;

	self->video_buffer_pool = NULL;
	self->imx_dma_buffer_allocator = NULL;

	self->fatal_error_cannot_decode = FALSE;

	self->g2d_blitter = NULL;
	self->tiled_surface = NULL;
	self->detiled_surface = NULL;

	self->v4l2_output_queue_poll = NULL;
	self->v4l2_output_buffer_items = NULL;
	self->num_v4l2_output_buffers = 0;
	self->v4l2_output_stream_enabled = FALSE;
	self->v4l2_output_buffer_size = 0;
	self->num_v4l2_output_buffers_in_queue = 0;

	self->v4l2_capture_queue_poll = NULL;
	self->v4l2_capture_buffer_items = NULL;
	self->num_v4l2_capture_buffers = 0;
	self->v4l2_capture_stream_enabled = FALSE;
}


static GstStateChangeReturn gst_imx_v4l2_amphion_dec_change_state(GstElement *element, GstStateChange transition)
{
	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	switch (transition)
	{
		case GST_STATE_CHANGE_PAUSED_TO_READY:
		{
			GST_VIDEO_DECODER_STREAM_LOCK(self);

			if (self->v4l2_output_queue_poll != NULL)
				gst_poll_set_flushing(self->v4l2_output_queue_poll, TRUE);
			if (self->v4l2_output_queue_poll != NULL)
				gst_poll_set_flushing(self->v4l2_capture_queue_poll, TRUE);

			GST_VIDEO_DECODER_STREAM_UNLOCK(self);

			gst_pad_stop_task(GST_VIDEO_DECODER_CAST(self)->srcpad);

			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_v4l2_amphion_dec_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	return ret;
}


static gboolean gst_imx_v4l2_amphion_dec_start(GstVideoDecoder *decoder)
{
	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(decoder);
	GstImxV4L2AmphionDecSupportedFormatDetails const *supported_format_details = GST_IMX_V4L2_AMPHION_DEC_GET_ELEMENT_COMPRESSION_FORMAT(decoder);

	gst_imx_v4l2_amphion_device_filenames_init();

	self->fatal_error_cannot_decode = FALSE;

	self->decoder_loop_flow_error = GST_FLOW_OK;

	self->imx_dma_buffer_allocator = gst_imx_dmabuf_allocator_new();

	self->g2d_blitter = imx_2d_backend_g2d_blitter_create();
	if (G_UNLIKELY(self->g2d_blitter == NULL))
	{
		GST_ERROR_OBJECT(self, "creating G2D blitter failed");
		goto error;
	}

	self->tiled_surface = imx_2d_surface_create(NULL);
	if (G_UNLIKELY(self->tiled_surface == NULL))
	{
		GST_ERROR_OBJECT(self, "creating tiled surface failed");
		goto error;
	}

	self->detiled_surface = imx_2d_surface_create(NULL);
	if (G_UNLIKELY(self->detiled_surface == NULL))
	{
		GST_ERROR_OBJECT(self, "creating detiled surface failed");
		goto error;
	}

	self->v4l2_output_queue_poll = gst_poll_new(TRUE);
	if (G_UNLIKELY(self->v4l2_output_queue_poll == NULL))
	{
		GST_ERROR_OBJECT(self, "creating V4L2 output queue gstpoll object failed");
		goto error;
	}

	gst_poll_fd_init(&(self->v4l2_output_queue_fd));

	self->v4l2_capture_queue_poll = gst_poll_new(TRUE);
	if (G_UNLIKELY(self->v4l2_capture_queue_poll == NULL))
	{
		GST_ERROR_OBJECT(self, "creating V4L2 capture queue gstpoll object failed");
		goto error;
	}

	gst_poll_fd_init(&(self->v4l2_capture_queue_fd));

	GST_INFO_OBJECT(self, "i.MX V4L2 Amphion Malone decoder %s decoder started", supported_format_details->desc_name);
	return TRUE;

error:
	gst_imx_v4l2_amphion_dec_stop(decoder);
	return FALSE;
}


static gboolean gst_imx_v4l2_amphion_dec_stop(GstVideoDecoder *decoder)
{
	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(decoder);
	GstImxV4L2AmphionDecSupportedFormatDetails const *supported_format_details = GST_IMX_V4L2_AMPHION_DEC_GET_ELEMENT_COMPRESSION_FORMAT(decoder);

	/* Stop the decoder output loop if it is running, otherwise
	 * we cannot disable the streams and cleanup resources. */
	gst_imx_v4l2_amphion_dec_decoder_stop_output_loop(self);

	gst_imx_v4l2_amphion_dec_cleanup_decoding_resources(self);

	if (self->v4l2_output_queue_poll != NULL)
	{
		gst_poll_free(self->v4l2_output_queue_poll);
		self->v4l2_output_queue_poll = NULL;
	}

	if (self->v4l2_capture_queue_poll != NULL)
	{
		gst_poll_free(self->v4l2_capture_queue_poll);
		self->v4l2_capture_queue_poll = NULL;
	}

	if (self->tiled_surface != NULL)
	{
		imx_2d_surface_destroy(self->tiled_surface);
		self->tiled_surface = NULL;
	}

	if (self->detiled_surface != NULL)
	{
		imx_2d_surface_destroy(self->detiled_surface);
		self->detiled_surface = NULL;
	}

	if (self->g2d_blitter != NULL)
	{
		imx_2d_blitter_destroy(self->g2d_blitter);
		self->g2d_blitter = NULL;
	}

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}

	GST_INFO_OBJECT(self, "i.MX V4L2 Amphion Malone decoder %s decoder stopped", supported_format_details->desc_name);

	return TRUE;
}


static gboolean gst_imx_v4l2_amphion_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state)
{
	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(decoder);
	GstImxV4L2AmphionDecClass *klass = GST_IMX_V4L2_AMPHION_DEC_CLASS(G_OBJECT_GET_CLASS(self));
	GstImxV4L2AmphionDecSupportedFormatDetails const *supported_format_details;
	struct v4l2_capability capability;
	struct v4l2_format requested_output_buffer_format;
	struct v4l2_requestbuffers output_buffer_request;
	struct v4l2_event_subscription event_subscription;
	GstCaps *allowed_srccaps = NULL;
	gboolean ret = TRUE;
	gint i;

	supported_format_details = (GstImxV4L2AmphionDecSupportedFormatDetails const *)g_type_get_qdata(G_OBJECT_CLASS_TYPE(klass), gst_imx_v4l2_amphion_dec_format_details_quark());

	/* Stop any ongoing decoder output loop; we are done with it. */
	GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);
	gst_imx_v4l2_amphion_dec_decoder_stop_output_loop(self);
	GST_VIDEO_DECODER_STREAM_LOCK(decoder);

	/* Cleanup any existing resources since they belong to a previous decoding session. */
	gst_imx_v4l2_amphion_dec_cleanup_decoding_resources(self);

	self->use_frame_reordering = (klass->is_frame_reordering_required == NULL)
		                       || klass->is_frame_reordering_required(gst_caps_get_structure(state->caps, 0));
	GST_DEBUG_OBJECT(self, "using frame reordering: %d", self->use_frame_reordering);

	/* Get the caps that downstream allows so we can decide
	 * what format to use for the decoded and detiled output. */

	allowed_srccaps = gst_pad_get_allowed_caps(GST_VIDEO_DECODER_SRC_PAD(decoder));

	if (allowed_srccaps != NULL)
	{
		gchar const *format_str;
		GValue const *format_value;
		GstStructure *structure;

		GST_DEBUG_OBJECT(self, "allowed srccaps: %" GST_PTR_FORMAT "; using its first structure", (gpointer)allowed_srccaps);

		/* Look at the sample format values from the first structure */
		structure = gst_caps_get_structure(allowed_srccaps, 0);
		format_value = gst_structure_get_value(structure, "format");

		if (G_UNLIKELY(format_value == NULL))
		{
			GST_ERROR_OBJECT(self, "allowed srccaps structure %" GST_PTR_FORMAT " does not contain a format field", (gpointer)structure);
			goto error;
		}
		else if (GST_VALUE_HOLDS_LIST(format_value))
		{
			/* if value is a format list, pick the first entry */
			GValue const *fmt_list_value = gst_value_list_get_value(format_value, 0);
			format_str = g_value_get_string(fmt_list_value);
		}
		else if (G_VALUE_HOLDS_STRING(format_value))
		{
			/* if value is a string, use it directly */
			format_str = g_value_get_string(format_value);
		}
		else
		{
			GST_ERROR_OBJECT(self, "unexpected type for format field in allowed srccaps structure %" GST_PTR_FORMAT, (gpointer)structure);
			goto error;
		}

		self->final_output_format = gst_video_format_from_string(format_str);
		if (G_UNLIKELY(self->final_output_format == GST_VIDEO_FORMAT_UNKNOWN))
		{
			GST_ERROR_OBJECT(self, "format field in allowed srccaps structure %" GST_PTR_FORMAT " contains invalid/unsupported value", (gpointer)structure);
			goto error;
		}
	}
	else
	{
		GST_DEBUG_OBJECT(self, "downstream did not report allowed caps; decoder will freely pick format");
		self->final_output_format = GST_VIDEO_FORMAT_UNKNOWN;
	}

	/* Open the V4L2 FD and query capabilities to check that we accessed the correct device. */

	self->v4l2_fd = open(gst_imx_v4l2_amphion_device_filenames.decoder_filename, O_RDWR);
	if (self->v4l2_fd < 0)
	{
		GST_ERROR_OBJECT(self, "could not open V4L2 device: %s (%d)", strerror(errno), errno);
		goto error;
	}

	if (ioctl(self->v4l2_fd, VIDIOC_QUERYCAP, &capability) < 0)
	{
		GST_ERROR_OBJECT(self, "could not query capability: %s (%d)", strerror(errno), errno);
		goto error;
	}

	GST_DEBUG_OBJECT(self, "V4L2 FD: %d", self->v4l2_fd);
	GST_DEBUG_OBJECT(self, "driver:         [%s]", (char const *)(capability.driver));
	GST_DEBUG_OBJECT(self, "card:           [%s]", (char const *)(capability.card));
	GST_DEBUG_OBJECT(self, "bus info:       [%s]", (char const *)(capability.bus_info));
	GST_DEBUG_OBJECT(self,
		"driver version: %d.%d.%d",
		(int)((capability.version >> 16) & 0xFF),
		(int)((capability.version >> 8) & 0xFF),
		(int)((capability.version >> 0) & 0xFF)
	);

	if ((capability.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) == 0)
	{
		GST_ERROR_OBJECT(self, "device does not support multi-planar mem2mem decoding");
		goto error;
	}

	if ((capability.capabilities & V4L2_CAP_STREAMING) == 0)
	{
		GST_ERROR_OBJECT(self, "device does not support frame streaming");
		goto error;
	}


	/* Set the encoded data format in the OUTPUT queue. */

	memset(&requested_output_buffer_format, 0, sizeof(struct v4l2_format));
	requested_output_buffer_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	requested_output_buffer_format.fmt.pix_mp.width = GST_VIDEO_INFO_WIDTH(&(state->info));
	requested_output_buffer_format.fmt.pix_mp.height = GST_VIDEO_INFO_HEIGHT(&(state->info));
	requested_output_buffer_format.fmt.pix_mp.pixelformat = supported_format_details->v4l2_pixelformat;
	requested_output_buffer_format.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	requested_output_buffer_format.fmt.pix_mp.num_planes = 1;
	requested_output_buffer_format.fmt.pix_mp.plane_fmt[0].sizeimage = DEC_REQUESTED_OUTPUT_BUFFER_SIZE;
	requested_output_buffer_format.fmt.pix_mp.plane_fmt[0].bytesperline = 0; /* This is set to 0 for encoded data. */

	if (ioctl(self->v4l2_fd, VIDIOC_S_FMT, &requested_output_buffer_format) < 0)
	{
		GST_ERROR_OBJECT(self, "could not set V4L2 output buffer video format (= encoded data format): %s (%d)", strerror(errno), errno);
		goto error;
	}

	GST_INFO_OBJECT(
		self,
		"set up V4L2 output buffer video format (= encoded data format): %s (V4L2 fourCC: %" GST_FOURCC_FORMAT ")",
		supported_format_details->desc_name,
		GST_FOURCC_ARGS(requested_output_buffer_format.fmt.pix_mp.pixelformat)
	);

	/* The driver may adjust the size of the output buffers. Retrieve
	 * the sizeimage value (which contains what the driver picked). */
	self->v4l2_output_buffer_size = requested_output_buffer_format.fmt.pix_mp.plane_fmt[0].sizeimage;
	GST_DEBUG_OBJECT(
		self,
		"V4L2 output buffer size in bytes:  requested: %d  actual: %d",
		DEC_REQUESTED_OUTPUT_BUFFER_SIZE,
		self->v4l2_output_buffer_size
	);

	/* Finished setting the format. Make a copy for later use. */
	memcpy(&(self->v4l2_output_buffer_format), &requested_output_buffer_format, sizeof(struct v4l2_format));


	/* Allocate the output buffers. */

	GST_DEBUG_OBJECT(self, "requesting output buffers");

	memset(&output_buffer_request, 0, sizeof(output_buffer_request));
	output_buffer_request.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	output_buffer_request.memory = V4L2_MEMORY_MMAP;
	output_buffer_request.count = DEC_MIN_NUM_REQUIRED_OUTPUT_BUFFERS;

	if (ioctl(self->v4l2_fd, VIDIOC_REQBUFS, &output_buffer_request) < 0)
	{
		GST_ERROR_OBJECT(self, "could not request output buffers: %s (%d)", strerror(errno), errno);
		goto error;
	}

	/* VIDIOC_REQBUFS stores the number of actually requested buffers in the "count" field. */
	self->num_v4l2_output_buffers = output_buffer_request.count;
	GST_DEBUG_OBJECT(
		self,
		"num V4L2 output buffers:  requested: %d  actual: %d",
		DEC_MIN_NUM_REQUIRED_OUTPUT_BUFFERS,
		self->num_v4l2_output_buffers
	);

	g_assert(self->num_v4l2_output_buffers > 0);

	self->v4l2_output_buffer_items = g_malloc_n(self->num_v4l2_output_buffers, sizeof(DecV4L2OutputBufferItem));

	/* After requesting the buffers we need to query them to get
	 * the necessary information for later access via mmap().
	 * In here, we also associate each DecV4L2OutputBufferItem's
	 * v4l2_plane with the accompanying v4l2_buffer. */
	for (i = 0; i < self->num_v4l2_output_buffers; ++i)
	{
		DecV4L2OutputBufferItem *output_buffer_item = &(self->v4l2_output_buffer_items[i]);

		output_buffer_item->buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		output_buffer_item->buffer.memory = V4L2_MEMORY_MMAP;
		output_buffer_item->buffer.index = i;
		output_buffer_item->buffer.m.planes = &(output_buffer_item->plane);
		output_buffer_item->buffer.length = 1;

		if (ioctl(self->v4l2_fd, VIDIOC_QUERYBUF, &(output_buffer_item->buffer)) < 0)
		{
			GST_ERROR_OBJECT(self, "could not query output buffer #%d: %s (%d)", i, strerror(errno), errno);
			goto error;
		}

		GST_DEBUG_OBJECT(
			self,
			"  output buffer #%d:  flags: %08x  length: %u  mem offset: %u",
			i,
			(guint)(output_buffer_item->buffer.flags),
			(guint)(output_buffer_item->buffer.m.planes[0].length),
			(guint)(output_buffer_item->buffer.m.planes[0].m.mem_offset)
		);
	}


	/* Subscribe to the V4L2_EVENT_SOURCE_CHANGE event to get notified
	 * when (1) the initial resolution information becomes available
	 * and (2) when during the stream a new resolution is found. */

	GST_DEBUG_OBJECT(self, "subscribing to source change event");

	memset(&event_subscription, 0, sizeof(event_subscription));
	event_subscription.type = V4L2_EVENT_SOURCE_CHANGE;

	if (ioctl(self->v4l2_fd, VIDIOC_SUBSCRIBE_EVENT, &event_subscription) < 0) {
		GST_ERROR_OBJECT(self, "could not subscribe to source change event: %s (%d)", strerror(errno), errno);
		goto error;
	}

	/* Subscribe to the custom Malone skip event. This is used
	 * to keep track of skipped frames and to drop them. */

	GST_DEBUG_OBJECT(self, "subscribing to Amphion Malone skip event");

	memset(&event_subscription, 0, sizeof(event_subscription));
	event_subscription.type = V4L2_NXP_AMPHION_EVENT_SKIP;

	if (ioctl(self->v4l2_fd, VIDIOC_SUBSCRIBE_EVENT, &event_subscription) < 0) {
		GST_ERROR_OBJECT(self, "could not subscribe to Amphion Malone skip event: %s (%d)", strerror(errno), errno);
		goto error;
	}

	/* Turn off frame reordering in the Amphion Malone driver
	 * if necessary. Turning this off for formats that don't
	 * need it improves latency. */
	{
		struct v4l2_control control =
		{
			.id = V4L2_CID_USER_FRAME_DIS_REORDER,
			.value = !(self->use_frame_reordering)
		};

		if (ioctl(self->v4l2_fd, VIDIOC_S_CTRL, &control) < 0)
		{
			GST_ERROR_OBJECT(self, "could not set the driver's frame reordering V4L2 control: %s (%d)", strerror(errno), errno);
			goto error;
		}
	}


	/* Ref the codec state, to be able to use it later as reference
	 * for the gst_video_decoder_set_output_state() function. */
	self->input_state = gst_video_codec_state_ref(state);


	self->v4l2_output_queue_fd.fd = self->v4l2_fd;
	gst_poll_add_fd(self->v4l2_output_queue_poll, &(self->v4l2_output_queue_fd));
	gst_poll_fd_ctl_read(self->v4l2_output_queue_poll, &(self->v4l2_output_queue_fd), FALSE);
	gst_poll_fd_ctl_write(self->v4l2_output_queue_poll, &(self->v4l2_output_queue_fd), TRUE);
	gst_poll_fd_ctl_pri(self->v4l2_output_queue_poll, &(self->v4l2_output_queue_fd), FALSE);

	self->v4l2_capture_queue_fd.fd = self->v4l2_fd;
	gst_poll_add_fd(self->v4l2_capture_queue_poll, &(self->v4l2_capture_queue_fd));
	gst_poll_fd_ctl_read(self->v4l2_capture_queue_poll, &(self->v4l2_capture_queue_fd), TRUE);
	gst_poll_fd_ctl_write(self->v4l2_capture_queue_poll, &(self->v4l2_capture_queue_fd), FALSE);
	gst_poll_fd_ctl_pri(self->v4l2_capture_queue_poll, &(self->v4l2_capture_queue_fd), TRUE);


	GST_DEBUG_OBJECT(self, "setting format finished");


finish:
	gst_caps_replace(&allowed_srccaps, NULL);
	return ret;

error:
	self->fatal_error_cannot_decode = TRUE;
	ret = FALSE;
	goto finish;
}


static GstFlowReturn gst_imx_v4l2_amphion_dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *cur_frame)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC_CAST(decoder);
	struct v4l2_plane plane;
	struct v4l2_buffer buffer;
	int available_space_for_encoded_data;
	void *mapped_v4l2_buffer_data = NULL;
	GstMapInfo encoded_data_map_info;
	gint poll_errno = 0;
	gboolean input_buffer_mapped = FALSE;
	GstFlowReturn decoder_loop_flow_error = GST_FLOW_OK;

	if (G_UNLIKELY(self->v4l2_fd < 0))
	{
		GST_ERROR_OBJECT(self, "V4L2 VPU decoder FD was not opened; cannot continue");
		goto error;
	}

	GST_OBJECT_LOCK(self);
	/* Retrieve the last reported decoder loop flow error (if any).
	 * Reset the decoder_loop_flow_error field afterwards, otherwise
	 * we'd handle the same flow error more than once. */
	decoder_loop_flow_error = self->decoder_loop_flow_error;
	self->decoder_loop_flow_error = GST_FLOW_OK;
	GST_OBJECT_UNLOCK(self);

	if (G_UNLIKELY(self->fatal_error_cannot_decode))
	{
		GST_ERROR_OBJECT(self, "aborting handle_frame call; a fatal error was previously recorded");
		goto error;
	}

	if (G_UNLIKELY(decoder_loop_flow_error != GST_FLOW_OK))
	{
		GST_DEBUG_OBJECT(self, "aborting handle_frame call; decoder output loop reported flow return value %s", gst_flow_get_name(decoder_loop_flow_error));
		// TODO is this really necessary?
		if (decoder_loop_flow_error == GST_FLOW_FLUSHING)
			decoder_loop_flow_error = GST_FLOW_OK;
		flow_ret = decoder_loop_flow_error;
		goto finish;
	}

	if (self->num_v4l2_output_buffers_in_queue == DEC_MIN_NUM_REQUIRED_OUTPUT_BUFFERS)
	{
		GST_VIDEO_DECODER_STREAM_UNLOCK(self);
		if (gst_poll_wait(self->v4l2_output_queue_poll, GST_CLOCK_TIME_NONE) < 0)
			poll_errno = errno;
		GST_VIDEO_DECODER_STREAM_LOCK(self);

		if (G_UNLIKELY(poll_errno != 0))
		{
			switch (poll_errno)
			{
				case EBUSY:
					GST_DEBUG_OBJECT(self, "V4L2 output queue poll interrupted");
					flow_ret = GST_FLOW_FLUSHING;
					goto finish;

				default:
					GST_ERROR_OBJECT(self, "V4L2 output queue poll reports error: %s (%d)", strerror(poll_errno), poll_errno);
					goto error;
			}
		}

		if (!gst_poll_fd_can_write(self->v4l2_output_queue_poll, &(self->v4l2_output_queue_fd)))
		{
			GST_WARNING_OBJECT(self, "V4L2 output queue poll finished, but write bit was not set");
			goto finish;
		}
	}

	if (self->num_v4l2_output_buffers_in_queue < DEC_MIN_NUM_REQUIRED_OUTPUT_BUFFERS)
	{
		int output_buffer_index = self->num_v4l2_output_buffers_in_queue;
		DecV4L2OutputBufferItem *output_buffer_item = &(self->v4l2_output_buffer_items[output_buffer_index]);
		self->num_v4l2_output_buffers_in_queue++;

		/* We copy the v4l2_buffer instance in case the driver
		 * modifies its fields. (This preserves the original.) */
		memcpy(&buffer, &(output_buffer_item->buffer), sizeof(buffer));
		memcpy(&plane, &(output_buffer_item->plane), sizeof(plane));
		buffer.m.planes = &plane;
		buffer.length = 1;

		GST_CAT_LOG_OBJECT(
			imx_v4l2_amphion_dec_in_debug,
			self,
			"V4L2 output queue has room for %d more buffer(s); using buffer with buffer index %d to fill it with new encoded data and enqueue it",
			DEC_MIN_NUM_REQUIRED_OUTPUT_BUFFERS - self->num_v4l2_output_buffers_in_queue,
			output_buffer_index
		);
	}
	else
	{
		memset(&buffer, 0, sizeof(buffer));
		buffer.m.planes = &plane;
		buffer.length = 1;
		buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;

		if (ioctl(self->v4l2_fd, VIDIOC_DQBUF, &buffer) < 0)
		{
			GST_ERROR_OBJECT(self, "could not dequeue V4L2 output buffer: %s (%d)", strerror(errno), errno);
			goto error;
		}

		GST_CAT_LOG_OBJECT(
			imx_v4l2_amphion_dec_in_debug,
			self,
			"V4L2 output queue is full; dequeued output buffer with buffer index %d to fill it with new encoded data and then re-enqueue it",
			(int)(buffer.index)
		);
	}


	input_buffer_mapped = gst_buffer_map(cur_frame->input_buffer, &encoded_data_map_info, GST_MAP_READ);
	if (G_UNLIKELY(!input_buffer_mapped))
	{
		GST_ERROR_OBJECT(self, "could not map input buffer");
		goto error;
	}

	// TODO: compare this with v4l2_output_buffer_size. If they
	// are equal, remove v4l2_output_buffer_size as a decoder field.
	available_space_for_encoded_data = buffer.m.planes[0].length;

	/* Sanity check. This should never happen. */
	if ((int)(encoded_data_map_info.size) > available_space_for_encoded_data)
	{
		GST_ERROR_OBJECT(
			self,
			"encoded frame size %" G_GSIZE_FORMAT " exceeds available space for encoded data %d",
			encoded_data_map_info.size,
			available_space_for_encoded_data
		);
		goto error;
	}


	buffer.m.planes[0].bytesused = encoded_data_map_info.size;
	if (GST_BUFFER_PTS_IS_VALID(cur_frame->input_buffer))
	{
		GstClockTime timestamp = GST_BUFFER_PTS(cur_frame->input_buffer);
		GST_TIME_TO_TIMEVAL(timestamp, buffer.timestamp);
	}
	else
		buffer.timestamp.tv_sec = -1;


	/* Copy the encoded data into the output buffer. */

	mapped_v4l2_buffer_data = mmap(
		NULL,
		available_space_for_encoded_data,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		self->v4l2_fd,
		buffer.m.planes[0].m.mem_offset
	);
	if (mapped_v4l2_buffer_data == MAP_FAILED)
	{
		GST_ERROR_OBJECT(self, "could not map V4L2 output buffer: %s (%d)", strerror(errno), errno);
		goto error;
	}
	memcpy(mapped_v4l2_buffer_data, encoded_data_map_info.data, encoded_data_map_info.size);
	munmap(mapped_v4l2_buffer_data, available_space_for_encoded_data);


	/* Finally, queue the buffer. */
	if (ioctl(self->v4l2_fd, VIDIOC_QBUF, &buffer) < 0)
	{
		GST_ERROR_OBJECT(self, "could not queue output buffer: %s (%d)", strerror(errno), errno);
		goto error;
	}


	GST_CAT_LOG_OBJECT(
		imx_v4l2_amphion_dec_in_debug,
		self,
		"queued V4L2 output buffer with a payload of %" G_GSIZE_FORMAT " byte(s) "
		"buffer index %d system frame number %" G_GUINT32_FORMAT " "
		"PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT,
		encoded_data_map_info.size,
		(int)(buffer.index),
		cur_frame->system_frame_number,
		GST_TIME_ARGS(cur_frame->pts),
		GST_TIME_ARGS(cur_frame->dts)
	);


	if (!(self->v4l2_output_stream_enabled) && (self->num_v4l2_output_buffers_in_queue == DEC_MIN_NUM_REQUIRED_OUTPUT_BUFFERS))
	{
		GstTaskState task_state;

		/* If there are enough queued encoded frames,
		 * enable the OUTPUT stream if not already eanbled. */

		if (!gst_imx_v4l2_amphion_dec_enable_stream(self, TRUE, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE))
			goto error;

		task_state = gst_pad_get_task_state(GST_VIDEO_DECODER_SRC_PAD(self));
		if ((task_state == GST_TASK_STOPPED) || (task_state == GST_TASK_PAUSED))
		{
			if (!gst_imx_v4l2_amphion_dec_decoder_start_output_loop(self))
				goto error;
		}
	}


finish:
	if (input_buffer_mapped)
		gst_buffer_unmap(cur_frame->input_buffer, &encoded_data_map_info);

	gst_video_codec_frame_unref(cur_frame);

	return flow_ret;

error:
	flow_ret = GST_FLOW_ERROR;
	self->fatal_error_cannot_decode = TRUE;

	GST_VIDEO_DECODER_STREAM_UNLOCK(self);
	gst_imx_v4l2_amphion_dec_decoder_stop_output_loop(self);
	GST_VIDEO_DECODER_STREAM_LOCK(self);

	goto finish;
}


static gboolean gst_imx_v4l2_amphion_dec_flush(GstVideoDecoder *decoder)
{
	/* The decoder stream lock is held when this is called. */

	gint i;
	gboolean capture_stream_was_enabled;
	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(decoder);

	if (self->v4l2_fd < 0)
		return GST_FLOW_OK;

	GST_DEBUG_OBJECT(self, "begin flush");

	GST_DEBUG_OBJECT(self, "stopping output loop before actual flush");
	GST_VIDEO_DECODER_STREAM_UNLOCK(self);
	gst_imx_v4l2_amphion_dec_decoder_stop_output_loop(self);
	GST_VIDEO_DECODER_STREAM_LOCK(self);

	// TODO: sync access to the capture_stream_was_enabled variable
	capture_stream_was_enabled = self->v4l2_capture_stream_enabled;

	/* Reset this. Otherwise, the next handle_frame call may incorrectly exit early. */
	self->decoder_loop_flow_error = GST_FLOW_OK;

	GST_DEBUG_OBJECT(self, "flush VPU decoder by disabling running V4L2 streams");
	gst_imx_v4l2_amphion_dec_enable_stream(self, FALSE, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	gst_imx_v4l2_amphion_dec_enable_stream(self, FALSE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	/* There are no output buffers queued anymore. */
	self->num_v4l2_output_buffers_in_queue = 0;

	GST_DEBUG_OBJECT(self, "re-queuing all %d capture buffers", self->num_v4l2_capture_buffers);
	for (i = 0; i < self->num_v4l2_capture_buffers; ++i)
	{
		struct v4l2_buffer buffer;
		struct v4l2_plane planes[DEC_NUM_CAPTURE_BUFFER_PLANES];
		DecV4L2CaptureBufferItem *capture_buffer_item = &(self->v4l2_capture_buffer_items[i]);

		/* We copy the v4l2_buffer instance in case the driver
		 * modifies its fields. (This preserves the original.) */
		memcpy(&buffer, &(capture_buffer_item->buffer), sizeof(buffer));
		memcpy(planes, capture_buffer_item->planes, sizeof(struct v4l2_plane) * DEC_NUM_CAPTURE_BUFFER_PLANES);
		/* Make sure "planes" points to the _copy_ of the planes structures. */
		buffer.m.planes = planes;

		if (ioctl(self->v4l2_fd, VIDIOC_QBUF, &buffer) < 0)
		{
			GST_ERROR_OBJECT(self, "could not queue capture buffer: %s (%d)", strerror(errno), errno);
			goto error;
		}
	}

	if (capture_stream_was_enabled)
		gst_imx_v4l2_amphion_dec_enable_stream(self, TRUE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	GST_DEBUG_OBJECT(self, "flush done");

	return TRUE;

error:
	return FALSE;
}


static GstFlowReturn gst_imx_v4l2_amphion_dec_drain(GstVideoDecoder *decoder)
{
	/* The decoder stream lock is held when this is called. */

	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(decoder);

	if (self->v4l2_fd < 0)
		return GST_FLOW_OK;

	gst_imx_v4l2_amphion_dec_finish(decoder);
	gst_imx_v4l2_amphion_dec_flush(decoder);

	return GST_FLOW_OK;
}


static GstFlowReturn gst_imx_v4l2_amphion_dec_finish(GstVideoDecoder *decoder)
{
	/* The decoder stream lock is held when this is called. */

	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(decoder);
	GstTask *task;

	struct v4l2_decoder_cmd command = {
		.cmd = V4L2_DEC_CMD_STOP,
		.flags = 0,
		.stop.pts = 0
	};

	if (self->v4l2_fd < 0)
		return GST_FLOW_OK;

	if (ioctl(self->v4l2_fd, VIDIOC_DECODER_CMD, &command) < 0)
	{
		GST_ERROR_OBJECT(self, "could not initiate finish: %s (%d)", strerror(errno), errno);
		return GST_FLOW_ERROR;
	}

	GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);

	task = decoder->srcpad->task;

	if (task != NULL)
	{
		GST_DEBUG_OBJECT(self, "waiting for decoder loop to finish decoding pending frames");
		GST_OBJECT_LOCK(task);
		while (GST_TASK_STATE(task) == GST_TASK_STARTED)
			GST_TASK_WAIT(task);
		GST_OBJECT_UNLOCK(task);
		GST_DEBUG_OBJECT(self, "decoder loop finished");
	}

	gst_imx_v4l2_amphion_dec_decoder_stop_output_loop(self);

	GST_VIDEO_DECODER_STREAM_LOCK(decoder);

	return GST_FLOW_OK;
}


static gboolean gst_imx_v4l2_amphion_dec_decide_allocation(GstVideoDecoder *decoder, GstQuery *query)
{
	GstImxV4L2AmphionDec *self = GST_IMX_V4L2_AMPHION_DEC(decoder);

	/* This happens if gap events are sent downstream before the first caps event.
	 * GstVideoDecoder then produces default sink caps and negotiates with these
	 * caps, which ultimately ends up calling decide_allocation() even though there
	 * is no output state yet. We must do an early exit then, since the contents
	 * of detiler_output_info aren't filled at this stage. */
	if (G_UNLIKELY(self->output_state == NULL))
	{
		GstCaps *negotiated_caps;

		gst_query_parse_allocation(query, &negotiated_caps, NULL);
		GST_WARNING_OBJECT(
			self,
			"not responding to allocation query since no output state exists (yet); negotiated caps = %" GST_PTR_FORMAT,
			(gpointer)negotiated_caps
		);

		return FALSE;
	}

	/* Chain up to the base class.
	 * We first do that, then modify the query. That way, we can be
	 * sure that our modifications remain, and aren't overwritten. */
	if (!GST_VIDEO_DECODER_CLASS(gst_imx_v4l2_amphion_dec_parent_class)->decide_allocation(decoder, query))
		return FALSE;

	GST_TRACE_OBJECT(self, "attempting to decide what buffer pool and allocator to use");

	/* Discard any previously created buffer pool before creating a new one. */
	if (self->video_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->video_buffer_pool));
		self->video_buffer_pool = NULL;
	}

	self->video_buffer_pool = gst_imx_video_buffer_pool_new(
		self->imx_dma_buffer_allocator,
		query,
		&(self->detiler_output_info)
	);

	gst_object_ref_sink(self->video_buffer_pool);

	return (self->video_buffer_pool != NULL);
}


static gboolean gst_imx_v4l2_amphion_dec_enable_stream(GstImxV4L2AmphionDec *self, gboolean do_enable, enum v4l2_buf_type type)
{
	gboolean *stream_enabled;
	char const *stream_name;

	switch (type)
	{
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			stream_enabled = &(self->v4l2_output_stream_enabled);
			stream_name = "output (= encoded data)";
			break;

		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
			stream_enabled = &(self->v4l2_capture_stream_enabled);
			stream_name = "capture (= decoded data)";
			break;

		default:
			g_assert_not_reached();
	}

	if (*stream_enabled == do_enable)
		return TRUE;

	GST_DEBUG_OBJECT(self, "%s %s stream", (do_enable ? "enabling" : "disabling"), stream_name);

	if (ioctl(self->v4l2_fd, do_enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type) < 0)
	{
		GST_ERROR_OBJECT(self, "could not %s %s stream: %s (%d)", (do_enable ? "enable" : "disable"), stream_name, strerror(errno), errno);
		return FALSE;
	}
	else
	{
		GST_DEBUG_OBJECT(self, "%s stream %s", stream_name, (do_enable ? "enabled" : "disabled"));
		*stream_enabled = do_enable;
		return TRUE;
	}
}


static void gst_imx_v4l2_amphion_dec_cleanup_decoding_resources(GstImxV4L2AmphionDec *self)
{
	struct v4l2_requestbuffers frame_buffer_request;

	if (self->v4l2_output_stream_enabled)
	{
		GST_DEBUG_OBJECT(self, "disabling V4L2 output stream");
		gst_imx_v4l2_amphion_dec_enable_stream(self, FALSE, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	}

	if (self->num_v4l2_output_buffers > 0)
	{
		GST_DEBUG_OBJECT(self, "freeing V4L2 output buffers");

		memset(&frame_buffer_request, 0, sizeof(frame_buffer_request));
		frame_buffer_request.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		frame_buffer_request.memory = V4L2_MEMORY_MMAP;
		frame_buffer_request.count = 0;

		if (ioctl(self->v4l2_fd, VIDIOC_REQBUFS, &frame_buffer_request) < 0)
			GST_ERROR_OBJECT(self, "could not free V4L2 output buffers: %s (%d)", strerror(errno), errno);
	}

	if (self->v4l2_capture_stream_enabled)
	{
		GST_DEBUG_OBJECT(self, "disabling V4L2 capture stream");
		gst_imx_v4l2_amphion_dec_enable_stream(self, FALSE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	}

	if (self->num_v4l2_output_buffers > 0)
	{
		gint i;

		GST_DEBUG_OBJECT(self, "freeing V4L2 capture buffers");

		for (i = 0; i < self->num_v4l2_capture_buffers; ++i)
		{
			gint plane_nr;
			DecV4L2CaptureBufferItem *capture_buffer_item = &(self->v4l2_capture_buffer_items[i]);

			for (plane_nr = 0; plane_nr < (gint)GST_VIDEO_INFO_N_PLANES(&(self->detiler_output_info)); ++plane_nr)
			{
				int fd = capture_buffer_item->dmabuf_fds[plane_nr];

				if (fd > 0)
				{
					GST_DEBUG_OBJECT(self, "closing exported V4L2 DMA-BUF FD %d for capture buffer item #%d plane #%d", fd, i, plane_nr);
					close(fd);
				}
			}
		}

		memset(&frame_buffer_request, 0, sizeof(frame_buffer_request));
		frame_buffer_request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		frame_buffer_request.memory = V4L2_MEMORY_MMAP;
		frame_buffer_request.count = 0;

		if (ioctl(self->v4l2_fd, VIDIOC_REQBUFS, &frame_buffer_request) < 0)
			GST_ERROR_OBJECT(self, "could not free V4L2 capture buffers: %s (%d)", strerror(errno), errno);
	}

	g_free(self->v4l2_output_buffer_items);
	self->v4l2_output_buffer_items = NULL;
	self->num_v4l2_output_buffers = 0;

	g_free(self->v4l2_capture_buffer_items);
	self->v4l2_capture_buffer_items = NULL;
	self->num_v4l2_capture_buffers = 0;

	self->num_v4l2_output_buffers_in_queue = 0;

	if (self->codec_data_is_mapped)
	{
		g_assert(self->codec_data != NULL);
		gst_buffer_unmap(self->codec_data, &(self->codecdata_map_info));
		self->codec_data_is_mapped = FALSE;
	}

	gst_buffer_replace(&(self->codec_data), NULL);

	if (self->input_state != NULL)
	{
		gst_video_codec_state_unref(self->input_state);
		self->input_state = NULL;
	}

	if (self->output_state != NULL)
	{
		gst_video_codec_state_unref(self->output_state);
		self->output_state = NULL;
	}

	if (self->video_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(self->video_buffer_pool));
		self->video_buffer_pool = NULL;
	}

	if (self->v4l2_output_queue_fd.fd > 0)
	{
		gst_poll_remove_fd(self->v4l2_output_queue_poll, &(self->v4l2_output_queue_fd));
		self->v4l2_output_queue_fd.fd = -1;
	}
	if (self->v4l2_capture_queue_fd.fd > 0)
	{
		gst_poll_remove_fd(self->v4l2_capture_queue_poll, &(self->v4l2_capture_queue_fd));
		self->v4l2_capture_queue_fd.fd = -1;
	}

	if (self->v4l2_fd > 0)
	{
		close(self->v4l2_fd);
		self->v4l2_fd = -1;
	}
}


static gboolean gst_imx_v4l2_amphion_dec_decoder_start_output_loop(GstImxV4L2AmphionDec *self)
{
	/* Must be called with the decoder stream lock held. */

	return gst_pad_start_task(
		GST_VIDEO_DECODER_CAST(self)->srcpad,
		(GstTaskFunction)gst_imx_v4l2_amphion_dec_decoder_output_loop,
		self,
		NULL
	);
}


static void gst_imx_v4l2_amphion_dec_decoder_stop_output_loop(GstImxV4L2AmphionDec *self)
{
	/* Must be called with the decoder stream lock *released* (!). */

	gst_poll_set_flushing(self->v4l2_capture_queue_poll, TRUE);
	gst_pad_stop_task(GST_VIDEO_DECODER_CAST(self)->srcpad);
	gst_poll_set_flushing(self->v4l2_capture_queue_poll, FALSE);
}


static void gst_imx_v4l2_amphion_dec_decoder_output_loop(GstImxV4L2AmphionDec *self)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstVideoDecoder *decoder = GST_VIDEO_DECODER_CAST(self);
	gint poll_errno = 0;

	GST_CAT_LOG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "new decoder output loop iteration");

	if (gst_poll_wait(self->v4l2_capture_queue_poll, GST_CLOCK_TIME_NONE) < 0)
		poll_errno = errno;

	if (G_UNLIKELY(poll_errno != 0))
	{
		switch (poll_errno)
		{
			case EBUSY:
				GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "V4L2 capture queue poll interrupted");
				flow_ret = GST_FLOW_FLUSHING;
				goto finish;

			default:
				GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "V4L2 capture queue poll reports error: %s (%d)", strerror(poll_errno), poll_errno);
				goto error;
		}
	}

	if (gst_poll_fd_has_pri(self->v4l2_capture_queue_poll, &(self->v4l2_capture_queue_fd)))
	{
		struct v4l2_event event;

		if (ioctl(self->v4l2_fd, VIDIOC_DQEVENT, &event) < 0)
		{
			GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not dequeue event: %s (%d)", strerror(errno), errno);
			goto error;
		}

		switch (event.type)
		{
			case V4L2_EVENT_SOURCE_CHANGE:
			{
				if (event.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION)
				{
					GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "source change event with a resolution change detected");

					if (!gst_imx_v4l2_amphion_dec_handle_resolution_change(self))
						goto error;
				}
				else
					GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "ignoring source change event that does not contain a resolution change bit");

				break;
			}

			case V4L2_NXP_AMPHION_EVENT_SKIP:
			{
				GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "skip event detected");
				gst_imx_v4l2_amphion_dec_process_skipped_frame(self);
				break;
			}

			default:
				GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "ignoring event of type %" G_GUINT32_FORMAT, (guint32)(event.type));
				break;
		}
	}

	if (gst_poll_fd_can_read(self->v4l2_capture_queue_poll, &(self->v4l2_capture_queue_fd)))
		flow_ret = gst_imx_v4l2_amphion_dec_process_decoded_frame(self);

finish:
	if (flow_ret != GST_FLOW_OK)
	{
		/* Report a non-OK flow return value back to the handle_frame() function. */
		GST_OBJECT_LOCK(self);
		self->decoder_loop_flow_error = flow_ret;
		GST_OBJECT_UNLOCK(self);

		gst_pad_pause_task(decoder->srcpad);				
	}

	return;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


static Imx2dPixelFormat gst_video_format_to_imx2d_pixel_format(GstVideoFormat gst_video_format)
{
	switch (gst_video_format)
	{
		case GST_VIDEO_FORMAT_NV12: return IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12;
		case GST_VIDEO_FORMAT_UYVY: return IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY;
		case GST_VIDEO_FORMAT_YUY2: return IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV;
		case GST_VIDEO_FORMAT_RGBA: return IMX_2D_PIXEL_FORMAT_RGBA8888;
		case GST_VIDEO_FORMAT_BGRA: return IMX_2D_PIXEL_FORMAT_BGRA8888;
		case GST_VIDEO_FORMAT_RGB16: return IMX_2D_PIXEL_FORMAT_RGB565;
		case GST_VIDEO_FORMAT_BGR16: return IMX_2D_PIXEL_FORMAT_BGR565;
		default: return IMX_2D_PIXEL_FORMAT_UNKNOWN;
	}
}


static gboolean gst_imx_v4l2_amphion_dec_handle_resolution_change(GstImxV4L2AmphionDec *self)
{
	gint i, num_planes, plane_nr;
	gint min_num_buffers_for_capture;
	gint original_width, original_height;
	gint detiler_input_width, detiler_input_height;
	gint detiler_output_width, detiler_output_height;
	guint32 v4l2_pixelformat;
	struct v4l2_control control;
	struct v4l2_requestbuffers capture_buffer_request;
	GstVideoDecoder *decoder = GST_VIDEO_DECODER_CAST(self);
	GstImxDmaBufAllocator *dma_buf_allocator = GST_IMX_DMABUF_ALLOCATOR(self->imx_dma_buffer_allocator);
	Imx2dHardwareCapabilities const *imx2d_hw_caps = imx_2d_blitter_get_hardware_capabilities(self->g2d_blitter);

	/* Get resolution and format for decoded frames from
	 * the driver so we can set up the capture buffers. */

	memset(&(self->v4l2_capture_buffer_format), 0, sizeof(self->v4l2_capture_buffer_format));
	self->v4l2_capture_buffer_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (ioctl(self->v4l2_fd, VIDIOC_G_FMT, &(self->v4l2_capture_buffer_format)) < 0)
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not get V4L2 capture buffer format: %s (%d)", strerror(errno), errno);
		goto error;
	}

	original_width = self->v4l2_capture_buffer_format.fmt.pix_mp.width;
	original_height = self->v4l2_capture_buffer_format.fmt.pix_mp.height;
	detiler_input_width = original_width;
	detiler_input_height = original_height;
	detiler_output_width = original_width;
	detiler_output_height = original_height;
	v4l2_pixelformat = self->v4l2_capture_buffer_format.fmt.pix_mp.pixelformat;

	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "V4L2 capture buffer format and detiler resolution details:");
	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "  original V4L2 width x height in pixels: %d x %d", original_width, original_height);
	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "  V4L2 pixel format: %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS(v4l2_pixelformat));
	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "  detiler input width x height in pixels: %d x %d", detiler_input_width, detiler_input_height);
	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "  detiler output width x height in pixels: %d x %d", detiler_output_width, detiler_output_height);

	if (self->final_output_format == GST_VIDEO_FORMAT_UNKNOWN)
	{
		self->final_output_format = GST_VIDEO_FORMAT_NV12;
		GST_CAT_DEBUG_OBJECT(
			imx_v4l2_amphion_dec_out_debug,
			self,
			"downstream did not report allowed srccaps; choosing %s as output format",
			gst_video_format_to_string(self->final_output_format)
		);
	}

	gst_video_info_set_format(
		&(self->detiler_output_info),
		self->final_output_format,
		detiler_output_width, detiler_output_height
	);

	num_planes = (int)(self->v4l2_capture_buffer_format.fmt.pix_mp.num_planes);

	/* Since the Amphion decoder always produces NV12 data (8 or 10 bit),
	 * we always expect the same number of planes (2). */
	g_assert(num_planes == DEC_NUM_CAPTURE_BUFFER_PLANES);

	{
		guint plane_offset = 0;

		for (plane_nr = 0; plane_nr < num_planes; ++plane_nr)
		{
			gint unaligned_stride, aligned_stride;
			gint unaligned_num_rows, aligned_num_rows;

			unaligned_num_rows = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT(self->detiler_output_info.finfo, plane_nr, detiler_output_height);
			aligned_num_rows = ALIGN_VAL_TO(unaligned_num_rows, imx2d_hw_caps->total_row_count_alignment);

			unaligned_stride = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH(self->detiler_output_info.finfo, plane_nr, detiler_output_width) * GST_VIDEO_INFO_COMP_PSTRIDE(&(self->detiler_output_info), plane_nr);
			aligned_stride = ALIGN_VAL_TO(unaligned_stride, G2D_DEST_AMPHION_STRIDE_ALIGNMENT);

			GST_VIDEO_INFO_PLANE_STRIDE(&(self->detiler_output_info), plane_nr) = aligned_stride;
			GST_VIDEO_INFO_PLANE_OFFSET(&(self->detiler_output_info), plane_nr) = plane_offset;

			GST_CAT_DEBUG_OBJECT(
				imx_v4l2_amphion_dec_out_debug,
				self,
				"  plane %d/%d: V4L2 sizeimage/bytesperline %" G_GUINT32_FORMAT "/%" G_GUINT32_FORMAT
				" unaligned/aligned num rows %d/%d unaligned/aligned plane stride %d/%d plane offset %u",
				plane_nr, num_planes,
				(guint32)(self->v4l2_capture_buffer_format.fmt.pix_mp.plane_fmt[plane_nr].sizeimage),
				(guint32)(self->v4l2_capture_buffer_format.fmt.pix_mp.plane_fmt[plane_nr].bytesperline),
				unaligned_num_rows, aligned_num_rows,
				unaligned_stride, aligned_stride,
				plane_offset
			);

			plane_offset += aligned_num_rows * GST_VIDEO_INFO_PLANE_STRIDE(&(self->detiler_output_info), plane_nr);
		}

		GST_VIDEO_INFO_SIZE(&(self->detiler_output_info)) = plane_offset;
	}


	/* Allocate and queue the capture buffers. */

	memset(&control, 0, sizeof(control));
	control.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
	if (ioctl(self->v4l2_fd, VIDIOC_G_CTRL, &control) < 0)
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not query min number of V4L2 capture buffers: %s (%d)", strerror(errno), errno);
		goto error;
	}
	min_num_buffers_for_capture = control.value;
	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "min num buffers for capture queue: %d", min_num_buffers_for_capture);

	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "requesting V4L2 capture buffers");
	memset(&capture_buffer_request, 0, sizeof(capture_buffer_request));
	capture_buffer_request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	capture_buffer_request.memory = V4L2_MEMORY_MMAP;
	capture_buffer_request.count = min_num_buffers_for_capture;

	if (ioctl(self->v4l2_fd, VIDIOC_REQBUFS, &capture_buffer_request) < 0)
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not request V4L2 capture buffers: %s (%d)", strerror(errno), errno);
		goto error;
	}

	self->num_v4l2_capture_buffers = capture_buffer_request.count;
	GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self,
		"num V4L2 capture buffers:  requested: %d  actual: %d",
		min_num_buffers_for_capture,
		self->num_v4l2_capture_buffers
	);

	if (self->num_v4l2_capture_buffers < min_num_buffers_for_capture)
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "driver did not provide enough capture buffers");
		goto error;
	}

	g_assert(self->num_v4l2_capture_buffers > 0);

	self->v4l2_capture_buffer_items = g_malloc_n(self->num_v4l2_capture_buffers, sizeof(DecV4L2CaptureBufferItem));

	/* For each requested buffer, query its details, export the buffer
	 * as a DMA-BUF buffer (getting its FD), and retrieving the
	 * physical address associated with it. Then queue that buffer. */
	for (i = 0; i < self->num_v4l2_capture_buffers; ++i)
	{
		struct v4l2_exportbuffer expbuf;
		struct v4l2_buffer buffer;
		struct v4l2_plane planes[DEC_NUM_CAPTURE_BUFFER_PLANES];
		DecV4L2CaptureBufferItem *capture_buffer_item = &(self->v4l2_capture_buffer_items[i]);

		capture_buffer_item->buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		capture_buffer_item->buffer.index = i;
		capture_buffer_item->buffer.m.planes = capture_buffer_item->planes;
		capture_buffer_item->buffer.length = DEC_NUM_CAPTURE_BUFFER_PLANES;
		capture_buffer_item->buffer.timestamp.tv_sec = -1;

		if (ioctl(self->v4l2_fd, VIDIOC_QUERYBUF, &(capture_buffer_item->buffer)) < 0)
		{
			GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not query capture buffer #%d: %s (%d)", i, strerror(errno), errno);
			goto error;
		}

		for (plane_nr = 0; plane_nr < num_planes; ++plane_nr)
		{
			imx_physical_address_t physical_address;
			ImxWrappedDmaBuffer *wrapped_dma_buffer;

			memset(&expbuf, 0, sizeof(expbuf));
			expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			expbuf.index = i;
			expbuf.plane = plane_nr;

			if (ioctl(self->v4l2_fd, VIDIOC_EXPBUF, &expbuf) < 0)
			{
				GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not export plane #%d of capture buffer #%d as DMA-BUF FD: %s (%d)", plane_nr, i, strerror(errno), errno);
				goto error;
			}

			capture_buffer_item->dmabuf_fds[plane_nr] = expbuf.fd;

			physical_address = gst_imx_dmabuf_allocator_get_physical_address(dma_buf_allocator, expbuf.fd);
			if (physical_address == 0)
			{
				GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not get physical address for DMA-BUF FD %d", expbuf.fd);
				goto error;
			}
			GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug,
				self,
				"got physical address %" IMX_PHYSICAL_ADDRESS_FORMAT " for DMA-BUF FD %d plane #%d capture buffer #%d",
				physical_address, expbuf.fd, plane_nr, i
			);

			capture_buffer_item->physical_addresses[plane_nr] = physical_address;

			wrapped_dma_buffer = &(capture_buffer_item->wrapped_imx_dma_buffers[plane_nr]);

			imx_dma_buffer_init_wrapped_buffer(wrapped_dma_buffer);
			wrapped_dma_buffer->fd = expbuf.fd;
			wrapped_dma_buffer->physical_address = physical_address;
			wrapped_dma_buffer->size = self->v4l2_capture_buffer_format.fmt.pix_mp.plane_fmt[plane_nr].sizeimage;
		}

		/* We copy the v4l2_buffer instance in case the driver
		 * modifies its fields. (This preserves the original.) */
		memcpy(&buffer, &(capture_buffer_item->buffer), sizeof(buffer));
		memcpy(planes, capture_buffer_item->planes, sizeof(struct v4l2_plane) * DEC_NUM_CAPTURE_BUFFER_PLANES);
		/* Make sure "planes" points to the _copy_ of the planes structures. */
		buffer.m.planes = planes;

		if (ioctl(self->v4l2_fd, VIDIOC_QBUF, &buffer) < 0)
		{
			GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not queue capture buffer: %s (%d)", strerror(errno), errno);
			goto error;
		}
	}

	GST_VIDEO_DECODER_STREAM_LOCK(decoder);

	self->output_state = gst_video_decoder_set_output_state(
		decoder,
		self->final_output_format,
		original_width, original_height,
		self->input_state
	);

	/* This is necessary to make sure decide_allocation
	 * is called, because this creates the video_buffer_pool. */
	gst_video_decoder_negotiate(decoder);

	GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);


	/* Fill the tiled imx2d surface desc. */
	{
		self->tiled_surface_desc.width = detiler_input_width;
		self->tiled_surface_desc.height = detiler_input_height;
		self->tiled_surface_desc.num_padding_rows =
			self->v4l2_capture_buffer_format.fmt.pix_mp.plane_fmt[0].sizeimage /
			self->v4l2_capture_buffer_format.fmt.pix_mp.plane_fmt[0].bytesperline - detiler_input_height;
		self->tiled_surface_desc.format = (v4l2_pixelformat == V4L2_PIX_FMT_NV12)
			? IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128
			: IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128_10BIT;

		GST_CAT_DEBUG_OBJECT(
			imx_v4l2_amphion_dec_out_debug,
			self,
			"tiled imx2d surface desc:"
			"  width x height: %d x %d  format: %s  num padding rows: %d",
			self->tiled_surface_desc.width, self->tiled_surface_desc.height,
			imx_2d_pixel_format_to_string(self->tiled_surface_desc.format),
			self->tiled_surface_desc.num_padding_rows
		);

		for (plane_nr = 0; plane_nr < num_planes; ++plane_nr)
		{
			self->tiled_surface_desc.plane_strides[plane_nr] = self->v4l2_capture_buffer_format.fmt.pix_mp.plane_fmt[plane_nr].bytesperline;
			GST_CAT_DEBUG_OBJECT(
				imx_v4l2_amphion_dec_out_debug,
				self,
				"  plane %d/%d stride: %d",
				plane_nr,
				num_planes,
				self->tiled_surface_desc.plane_strides[plane_nr]
			);
		}

		imx_2d_surface_set_desc(self->tiled_surface, &(self->tiled_surface_desc));
	}

	/* Fill the detiled imx2d surface desc. */
	{
		GstVideoInfo *detiler_output_info = &(self->detiler_output_info);

		self->detiled_surface_desc.width = detiler_output_width;
		self->detiled_surface_desc.height = detiler_output_height;
		self->detiled_surface_desc.format = gst_video_format_to_imx2d_pixel_format(self->final_output_format);

		if (GST_VIDEO_INFO_N_PLANES(detiler_output_info) > 1)
		{
			self->detiled_surface_desc.num_padding_rows =
				(GST_VIDEO_INFO_PLANE_OFFSET(detiler_output_info, 1) - GST_VIDEO_INFO_PLANE_OFFSET(detiler_output_info, 0))
				/ GST_VIDEO_INFO_PLANE_STRIDE(detiler_output_info, 0);
			self->detiled_surface_desc.num_padding_rows -= detiler_output_height;
			g_assert(self->detiled_surface_desc.num_padding_rows >= 0);
		}
		else
			self->detiled_surface_desc.num_padding_rows = 0;

		GST_CAT_DEBUG_OBJECT(
			imx_v4l2_amphion_dec_out_debug,
			self,
			"detiled imx2d surface desc:"
			"  width x height: %d x %d  format: %s  num padding rows: %d",
			self->detiled_surface_desc.width, self->detiled_surface_desc.height,
			imx_2d_pixel_format_to_string(self->detiled_surface_desc.format),
			self->detiled_surface_desc.num_padding_rows
		);

		for (plane_nr = 0; plane_nr < num_planes; ++plane_nr)
		{
			self->detiled_surface_desc.plane_strides[plane_nr] = GST_VIDEO_INFO_PLANE_STRIDE(detiler_output_info, plane_nr);
			GST_CAT_DEBUG_OBJECT(
				imx_v4l2_amphion_dec_out_debug,
				self,
				"  plane %d/%d stride: %d",
				plane_nr,
				num_planes,
				self->detiled_surface_desc.plane_strides[plane_nr]
			);
		}

		imx_2d_surface_set_desc(self->detiled_surface, &(self->detiled_surface_desc));
	}


	/* Everything is configured for the new resolution. Enable capture stream. */

	if (!gst_imx_v4l2_amphion_dec_enable_stream(self, TRUE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE))
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not enable V4L2 capture stream");
		goto error;
	}

	return TRUE;

error:
	return FALSE;
}


static GstVideoCodecFrame* gst_imx_v4l2_amphion_dec_get_oldest_frame(GstImxV4L2AmphionDec *self)
{
	GstVideoDecoder *decoder = GST_VIDEO_DECODER_CAST(self);

	if (self->use_frame_reordering)
	{
		/* When frame reordering is enabled, the decoder will return frames
		 * in order of their PTS. Unfortunately, we cannot just rely on the
		 * v4l2_buffer timestamp field to pass around system frame numbers,
		 * because the Amphion Malone driver includes a "timestamp manager"
		 * which cannot be turned off and which modifies the values in that
		 * field in an effort to "smoothen" timestamps. So, instead, we rely
		 * on the by-PTS sorted order and just get the GstVideoCodecFrame
		 * inside the GstVideoDecoder base that has the oldest PTS of all.
		 * This is not the same as in gst_video_decoder_get_oldest_frame();
		 * that function gives us the frame in decoding order, that is,
		 * the frame with the oldest DTS. */
		// TODO: It could help to store unfinished frames in a separate data
		// structure that allows for more efficient search and insertion,
		// like a binary heap in an array.

		GList *frames, *l;
		gint count = 0;
		GstVideoCodecFrame *frame = NULL;

		frames = gst_video_decoder_get_frames(decoder);

		for (l = frames; l != NULL; l = l->next)
		{
			GstVideoCodecFrame *f = l->data;

			if ((frame == NULL) || (f->pts < frame->pts))
				frame = f;

			count++;
		}

		if (frame != NULL)
		{
			GST_LOG_OBJECT(self,
				"oldest frame is %d %" GST_TIME_FORMAT " and %d frames left",
				frame->system_frame_number, GST_TIME_ARGS(frame->pts), count - 1);
			gst_video_codec_frame_ref(frame);
		}

		g_list_free_full(frames, (GDestroyNotify) gst_video_codec_frame_unref);

		return frame;
	}
	else
	{
		/* If frame reordering is not done, then there is no difference
		 * in frame order by PTS and frame order by DTS, so we can just
		 * use this function to get the oldest frame. */
		return gst_video_decoder_get_oldest_frame(decoder);
	}
}


static GstFlowReturn gst_imx_v4l2_amphion_dec_process_skipped_frame(GstImxV4L2AmphionDec *self)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	GstVideoDecoder *decoder = GST_VIDEO_DECODER_CAST(self);
	GstVideoCodecFrame *video_codec_frame;

	/* There is currently no way to know which frame specifically was
	 * skipped by just using the V4L2 API. We have to stick to assumptions.
	 * Our assumption here is that the skipped frame is the oldest one.
	 * Fetch and drop that one. */

	GST_VIDEO_DECODER_STREAM_LOCK(self);

	video_codec_frame = gst_imx_v4l2_amphion_dec_get_oldest_frame(self);

	if (G_LIKELY(video_codec_frame != NULL))
	{
		GST_CAT_DEBUG_OBJECT(
			imx_v4l2_amphion_dec_out_debug,
			self,
			"processing oldest frame as a skipped frame; frame details: PTS: %" GST_TIME_FORMAT " DTS: %"
			GST_TIME_FORMAT " duration %" GST_TIME_FORMAT " system frame number %"
			G_GUINT32_FORMAT " input buffer %" GST_PTR_FORMAT,
			GST_TIME_ARGS(video_codec_frame->pts),
			GST_TIME_ARGS(video_codec_frame->dts),
			GST_TIME_ARGS(video_codec_frame->duration),
			video_codec_frame->system_frame_number,
			(gpointer)(video_codec_frame->input_buffer)
		);

		GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY(video_codec_frame);
		flow_ret = gst_video_decoder_finish_frame(decoder, video_codec_frame);
	}
	else
		GST_CAT_DEBUG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "cannot process skipped frame - no frames in videodecoder");

	GST_VIDEO_DECODER_STREAM_UNLOCK(self);

	return flow_ret;
}


static GstFlowReturn gst_imx_v4l2_amphion_dec_process_decoded_frame(GstImxV4L2AmphionDec *self)
{
	// TODO:
	// add documentation that the imx2d blitter outputs single-memory gstbuffers
	// even though the blitter input is per-plane memory

	gint plane_nr;
	struct v4l2_buffer buffer;
	struct v4l2_plane planes[DEC_NUM_CAPTURE_BUFFER_PLANES];
	gint dequeued_capture_buffer_index;
	DecV4L2CaptureBufferItem *capture_buffer_item;
	GstVideoCodecFrame *video_codec_frame = NULL;
	GstBuffer *intermediate_buffer = NULL;
	GstVideoDecoder *decoder = GST_VIDEO_DECODER_CAST(self);
	GstFlowReturn flow_ret = GST_FLOW_OK;
	gboolean buffer_transferred = FALSE;
	ImxDmaBuffer *intermediate_gstbuffer_dma_buffer;

	GST_CAT_LOG_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "processing new decoded frame");

	/* Get the oldest video codec frame and allocate its output buffer. */

	GST_VIDEO_DECODER_STREAM_LOCK(decoder);

	video_codec_frame = gst_imx_v4l2_amphion_dec_get_oldest_frame(self);
	if (G_UNLIKELY(video_codec_frame != NULL))
	{
		flow_ret = gst_video_decoder_allocate_output_frame(decoder, video_codec_frame);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		{
			GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);
			GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "error while allocating output frame: %s", gst_flow_get_name(flow_ret));
			goto error;
		}

		GST_CAT_LOG_OBJECT(
			imx_v4l2_amphion_dec_out_debug,
			self,
			"got oldest video codec frame for decoding: PTS: %" GST_TIME_FORMAT " DTS: %"
			GST_TIME_FORMAT " duration %" GST_TIME_FORMAT " system frame number %"
			G_GUINT32_FORMAT " input buffer %" GST_PTR_FORMAT,
			GST_TIME_ARGS(video_codec_frame->pts),
			GST_TIME_ARGS(video_codec_frame->dts),
			GST_TIME_ARGS(video_codec_frame->duration),
			video_codec_frame->system_frame_number,
			(gpointer)(video_codec_frame->input_buffer)
		);
	}
	else
	{
		/* If we got no video codec frame, it means that all frames
		 * from the GstVideoDecoder queue have been used up. And this
		 * indicates that the VPU produced more frames than expected.
		 * Typically, this is the result of a corrupted stream; the
		 * VPU then tends to produce partial frames. */
		GST_CAT_WARNING_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "there is no video codec frame available; decoder is producing too many frames; incoming data corrupted perhaps?");
	}

	GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);

	/* Dequeue the decoded frame. */

	memset(&buffer, 0, sizeof(buffer));
	memset(planes, 0, sizeof(planes));

	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.m.planes = planes;
	buffer.length = DEC_NUM_CAPTURE_BUFFER_PLANES;

	/* Dequeue the decoded frame from the CAPTURE queue. */
	if (ioctl(self->v4l2_fd, VIDIOC_DQBUF, &buffer) < 0)
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not dequeue decoded frame buffer: %s (%d)", strerror(errno), errno);
		goto error;
	}
	GST_CAT_LOG_OBJECT(
		imx_v4l2_amphion_dec_out_debug,
		self,
		"dequeued V4L2 buffer with index %" G_GUINT32_FORMAT " from capture queue",
		(guint32)(buffer.index)
	);

	/* Get information about the dequeued buffer. */

	dequeued_capture_buffer_index = buffer.index;
	g_assert(dequeued_capture_buffer_index < self->num_v4l2_capture_buffers);

	capture_buffer_item = &(self->v4l2_capture_buffer_items[dequeued_capture_buffer_index]);

	/* If we got no video_codec_frame, skip all processing and just re-queue the buffer. */
	if (G_UNLIKELY(video_codec_frame == NULL))
		goto requeue_buffer;

	/* Prepare the intermediate buffer. It will be used
	 * as the target for the G2D based detiler. This call
	 * acquires a new separate GstBuffer for intermediate
	 * data if necessary, otherwise it just refs the
	 * output buffer. */
	flow_ret = gst_imx_video_buffer_pool_acquire_intermediate_buffer(self->video_buffer_pool, video_codec_frame->output_buffer, &intermediate_buffer);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not get intermediate buffer: %s", gst_flow_get_name(flow_ret));
		goto error;
	}

	/* Prepare the imx2d surfaces for detiling. */

	intermediate_gstbuffer_dma_buffer = gst_imx_get_dma_buffer_from_buffer(intermediate_buffer);
	g_assert(intermediate_gstbuffer_dma_buffer != NULL);

	for (plane_nr = 0; plane_nr < DEC_NUM_CAPTURE_BUFFER_PLANES; ++plane_nr)
	{
		ImxDmaBuffer *capture_4l2_buffer_dma_buffer = (ImxDmaBuffer *)&(capture_buffer_item->wrapped_imx_dma_buffers[plane_nr]);

		imx_2d_surface_set_dma_buffer(
			self->tiled_surface,
			capture_4l2_buffer_dma_buffer,
			plane_nr,
			0
		);

	}

	for (plane_nr = 0; plane_nr < (gint)GST_VIDEO_INFO_N_PLANES(&(self->detiler_output_info)); ++plane_nr)
	{
		imx_2d_surface_set_dma_buffer(
			self->detiled_surface,
			intermediate_gstbuffer_dma_buffer,
			plane_nr,
			GST_VIDEO_INFO_PLANE_OFFSET(&(self->detiler_output_info), plane_nr)
		);
	}

	/* Perform the detiling. */

	if (G_UNLIKELY(imx_2d_blitter_start(self->g2d_blitter, self->detiled_surface) == 0))
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not start G2D blitter detiling");
		goto error;
	}

	if (G_UNLIKELY(imx_2d_blitter_do_blit(self->g2d_blitter, self->tiled_surface, NULL) == 0))
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not detile with the G2D blitter");
		goto error;
	}

	if (G_UNLIKELY(imx_2d_blitter_finish(self->g2d_blitter) == 0))
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not finish G2D blitter detiling");
		goto error;
	}

	/* Transfer the detiled result to the output buffer through the pool.
	 * This will create a CPU-based copy if downstream can't handle video meta
	 * and the intermediate frame is not "tightly packed". Otherwise, this
	 * will just unref intermediate_buffer, since in that case, output_buffer
	 * and intermediate_buffer are the same GstBuffer. */
	buffer_transferred = gst_imx_video_buffer_pool_transfer_to_output_buffer(self->video_buffer_pool, intermediate_buffer, video_codec_frame->output_buffer);
	/* Set this to NULL to prevent the unref below from doing anything
	 * since gst_imx_video_buffer_pool_transfer_to_output_buffer()
	 * already unrefs it. */
	intermediate_buffer = NULL;
	if (G_UNLIKELY(!buffer_transferred))
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not transfer intermediate buffer to video buffer pool");
		goto error;
	}

	GST_VIDEO_DECODER_STREAM_LOCK(decoder);
	flow_ret = gst_video_decoder_finish_frame(decoder, video_codec_frame);
	video_codec_frame = NULL;
	GST_VIDEO_DECODER_STREAM_UNLOCK(decoder);

	switch (flow_ret)
	{
		case GST_FLOW_OK:
			GST_CAT_LOG_OBJECT(
				imx_v4l2_amphion_dec_out_debug,
				self,
				"finished video codec frame successfully"
			);
			break;

		case GST_FLOW_FLUSHING:
			GST_CAT_DEBUG_OBJECT(
				imx_v4l2_amphion_dec_out_debug,
				self,
				"could not finish video codec frame because we are flushing"
			);
			break;

		default:
			GST_CAT_ERROR_OBJECT(
				imx_v4l2_amphion_dec_out_debug,
				self,
				"could not finish video codec frame: %s",
				gst_flow_get_name(flow_ret)
			);
	}

requeue_buffer:
	/* Finally, return the V4L2 capture buffer back to the capture queue. */

	/* We copy the v4l2_buffer instance in case the driver
	 * modifies its fields. (This preserves the original.) */
	memcpy(&buffer, &(capture_buffer_item->buffer), sizeof(buffer));
	memcpy(planes, capture_buffer_item->planes, sizeof(struct v4l2_plane) * DEC_NUM_CAPTURE_BUFFER_PLANES);
	/* Make sure "planes" points to the _copy_ of the planes structures. */
	buffer.m.planes = planes;

	GST_CAT_LOG_OBJECT(
		imx_v4l2_amphion_dec_out_debug,
		self,
		"re-queuing V4L2 buffer with index %" G_GUINT32_FORMAT " to capture queue",
		(guint32)(buffer.index)
	);

	if (ioctl(self->v4l2_fd, VIDIOC_QBUF, &buffer) < 0)
	{
		GST_CAT_ERROR_OBJECT(imx_v4l2_amphion_dec_out_debug, self, "could not queue capture buffer: %s (%d)", strerror(errno), errno);
		goto error;
	}

finish:
	if (video_codec_frame != NULL)
		gst_video_codec_frame_unref(video_codec_frame);

	/* Unref the intermediate buffer in case it is still around. */
	gst_buffer_replace(&intermediate_buffer, NULL);

	return flow_ret;

error:
	if (flow_ret == GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}



static GstImxV4L2AmphionDecSupportedFormatDetails const gst_imx_v4l2_amphion_dec_supported_format_details[] =
{
	{ "jpeg",    "Jpeg",      "JPEG",                                              V4L2_PIX_FMT_MJPEG,       FALSE, frame_reordering_required_never   },
	{ "mpeg2",   "Mpeg2",     "MPEG-1 & 2",                                        V4L2_PIX_FMT_MPEG2,       TRUE,  frame_reordering_required_never   },
	{ "mpeg4",   "Mpeg4",     "MPEG-4",                                            V4L2_PIX_FMT_MPEG4,       TRUE,  frame_reordering_required_always  },
	{ "h263",    "H263",      "h.263",                                             V4L2_PIX_FMT_H263,        FALSE, frame_reordering_required_never   },
	{ "h264",    "H264",      "h.264 / AVC",                                       V4L2_PIX_FMT_H264,        FALSE, h264_is_frame_reordering_required },
	{ "h265",    "H265",      "h.265 / HEVC",                                      V4L2_PIX_FMT_HEVC,        FALSE, frame_reordering_required_always  },
	{ "wmv3",    "Wmv3",      "WMV3 / Window Media Video 9 / VC-1 simple profile", V4L2_PIX_FMT_VC1_ANNEX_L, TRUE,  frame_reordering_required_never   },
	{ "vc1",     "Vc1",       "VC-1 advanced profile",                             V4L2_PIX_FMT_VC1_ANNEX_G, TRUE,  frame_reordering_required_always  },
	{ "vp6",     "Vp6",       "VP6",                                               V4L2_VPU_PIX_FMT_VP6,     FALSE, frame_reordering_required_never   },
	{ "vp8",     "Vp8",       "VP8",                                               V4L2_PIX_FMT_VP8,         FALSE, frame_reordering_required_always  },
	{ "cavs",    "Avs",       "AVS (Audio and Video Coding Standard)",             V4L2_VPU_PIX_FMT_AVS,     FALSE, frame_reordering_required_always  },
	{ "rv",      "RV",        "RealVideo 8, 9, 10",                                V4L2_VPU_PIX_FMT_RV,      TRUE,  frame_reordering_required_always  },
	{ "divx3",   "DivX3" ,    "DivX 3",                                            V4L2_VPU_PIX_FMT_DIV3,    FALSE, frame_reordering_required_never   },
	{ "divx456", "DivX456",   "DivX 4 & 5 & 6",                                    V4L2_VPU_PIX_FMT_DIVX,    FALSE, frame_reordering_required_always  },
	{ "sspark",  "SSpark",    "Sorenson Spark",                                    V4L2_VPU_PIX_FMT_SPK,     FALSE, frame_reordering_required_always  }
};

static gint const num_gst_imx_v4l2_amphion_dec_supported_formats = sizeof(gst_imx_v4l2_amphion_dec_supported_format_details) / sizeof(GstImxV4L2AmphionDecSupportedFormatDetails);


static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw, "
		"format = (string) { NV12, UYVY, YUY2, RGBA, BGRA, RGB16, BGR16 }, "
		"width = (int) [ 4, 3840 ], "
		"height = (int) [ 4, 2160 ], "
		"framerate = (fraction) [ 0/1, 60/1 ]"
	)
);


/* class_init function for autogenerated subclasses. */
static void derived_class_init(void *klass)
{
	GstImxV4L2AmphionDecClass *imx_v4l2_amphion_dec_class;
	GstElementClass *element_class;
	GstCaps *sink_template_caps;
	gchar *longname;
	gchar *classification;
	gchar *description;
	gchar *author;
	GstImxV4L2AmphionDecSupportedFormatDetails const *supported_format_details;

	imx_v4l2_amphion_dec_class = GST_IMX_V4L2_AMPHION_DEC_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	supported_format_details = (GstImxV4L2AmphionDecSupportedFormatDetails const *)g_type_get_qdata(G_OBJECT_CLASS_TYPE(klass), gst_imx_v4l2_amphion_dec_format_details_quark());
	g_assert(supported_format_details != NULL);

	sink_template_caps = gst_imx_v4l2_amphion_get_caps_for_format(supported_format_details->v4l2_pixelformat);
	g_assert(sink_template_caps != NULL);

	gst_element_class_add_pad_template(element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	imx_v4l2_amphion_dec_class->is_frame_reordering_required = supported_format_details->is_frame_reordering_required;
	imx_v4l2_amphion_dec_class->requires_codec_data = supported_format_details->requires_codec_data;

	longname = g_strdup_printf("i.MX V4L2 %s video decoder", supported_format_details->desc_name);
	classification = g_strdup("Codec/Decoder/Video/Hardware");
	description = g_strdup_printf("Hardware-accelerated %s video decoding using the Amphion Malone VPU through V4L2 on i.MX platforms", supported_format_details->desc_name);
	author = g_strdup("Carlos Rafael Giani <crg7475@mailbox.org>");
	gst_element_class_set_metadata(element_class, longname, classification, description, author);
	g_free(longname);
	g_free(classification);
	g_free(description);
	g_free(author);}


GTypeInfo gst_imx_v4l2_amphion_dec_get_derived_type_info(void)
{
	GTypeInfo type_info =
	{
		sizeof(GstImxV4L2AmphionDecClass),
		NULL,
		NULL,
		(GClassInitFunc)(void (*)(void))derived_class_init,
		NULL,
		NULL,
		sizeof(GstImxV4L2AmphionDec),
		0,
		NULL,
		NULL
	};

	return type_info;
}


gboolean gst_imx_v4l2_amphion_dec_register_decoder_types(GstPlugin *plugin)
{
	gint i;

	for (i = 0; i < num_gst_imx_v4l2_amphion_dec_supported_formats; ++i)
	{
		GType type;
		gchar *element_name, *type_name;
		gboolean ret = FALSE;
		GTypeInfo typeinfo = gst_imx_v4l2_amphion_dec_get_derived_type_info();
		GstImxV4L2AmphionDecSupportedFormatDetails const *supported_format_details = &(gst_imx_v4l2_amphion_dec_supported_format_details[i]);

		element_name = g_strdup_printf("imxv4l2amphiondec_%s", supported_format_details->element_name_suffix);
		type_name = g_strdup_printf("GstImxV4l2VideoDec%s", supported_format_details->class_name_suffix);
		type = g_type_from_name(type_name);
		if (!type)
		{
			type = g_type_register_static(GST_TYPE_IMX_V4L2_AMPHION_DEC, type_name, &typeinfo, 0);
			g_type_set_qdata(type, gst_imx_v4l2_amphion_dec_format_details_quark(), (gpointer)supported_format_details);
		}

		ret = gst_element_register(plugin, element_name, GST_RANK_PRIMARY + 1, type);

		g_free(type_name);

		if (!ret)
			return FALSE;
	}

	return TRUE;
}
