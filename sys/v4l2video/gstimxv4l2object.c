/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2021  Carlos Rafael Giani
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
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimxv4l2object.h"


GST_DEBUG_CATEGORY_STATIC(imx_v4l2_object_debug);
#define GST_CAT_DEFAULT imx_v4l2_object_debug


/* Read & write FDs for the internal pipe that is used for waking
 * up a blocking gst_imx_v4l2_object_dequeue_buffer() call. */
#define CONTROL_PIPE_READ_FD(obj) ((obj)->control_pipe_fds[0])
#define CONTROL_PIPE_WRITE_FD(obj) ((obj)->control_pipe_fds[1])


struct _GstImxV4L2Object
{
	GstObject parent;

	/*< private >*/

	/* Copies of the data from the context that is
	 * passed to gst_imx_v4l2_object_new(). */
	GstImxV4L2ProbeResult probe_result;
	int num_buffers;
	GstImxV4L2DeviceType device_type;
	GstImxV4L2VideoInfo video_info;

	/* Control pipe for unblocking gst_imx_v4l2_object_dequeue_buffer(). */
	int control_pipe_fds[2];

	/* Opened Unix file descriptor for accessing the V4L2 device. */
	int v4l2_fd;

	/* Used for setting the interlacing video buffer flags. */
	gboolean interlaced_video;
	gboolean interlace_top_field_first;

	/* One of V4L2_BUF_TYPE_VIDEO_CAPTURE or V4L2_BUF_TYPE_VIDEO_OUTPUT,
	 * depending on the value of device_type (see above). */
	guint32 v4l2_buffer_type;

	/* If this is set to TRUE, the V4L2 stream is ongoing. Frames are
	 * being captured / output. This is not immediately TRUE upon
	 * creating the V4L2 object; only after a certain amount of buffers
	 * were queued will streaming be enabled. */
	gboolean stream_on;

	/* Nonzero if the object is currently unlocked. This is
	 * set and accessed via GLibs g_atomic_int_* functions. */
	volatile gint unlocked;

	/* This stores indices of the entries in the V4L2 queue that
	 * aren't used yet. The indices are in the range 0..(num_buffers-1).
	 * When a buffer is queued, the index at the front of this queue
	 * is retrieved, and the buffer is stored in the queued_gstbuffers
	 * array at the at that index. Furthermore, that index is assigned
	 * to the index of the v4l2_buffer instance that is passed to the
	 * V4L2 device's VIDIOC_QBUF ioctl. If this GQeueue is empty, it
	 * means that there's no free slot available - the V4L2 queue is
	 * full. In that case, gst_imx_v4l2_object_queue_buffer() will
	 * return GST_IMX_V4L2_FLOW_QUEUE_IS_FULL. */
	GQueue unused_v4l2_buffer_indices;

	/* Array holding the GstBuffer pointers of the queued buffers. This
	 * exists to keep track of those pointers so they can be unref'd when
	 * buffers are dequeued or the object is finalized. The indices
	 * correspond to the indices passed to v4l2_buffer instances as
	 * well as the indices in unused_v4l2_buffer_indices. */
	GstBuffer **queued_gstbuffers;

	/* Sync primitives to be able to wait for a dequeue operation to
	 * finish / be canceled before turning off the stream inside the
	 * gst_imx_v4l2_object_unlock() function. */
	gboolean dequeuing_finished;
	GMutex dequeuing_mutex;
	GCond dequeuing_cond;
};


struct _GstImxV4L2ObjectClass
{
	GstObjectClass parent_class;
};


static GQuark gst_imx_v4l2_object_internal_imxdmabuffer_map_quark;


G_DEFINE_TYPE(GstImxV4L2Object, gst_imx_v4l2_object, GST_TYPE_OBJECT)


static void gst_imx_v4l2_object_finalize(GObject *object);
static gboolean setup_device(GstImxV4L2Object *self);
static gboolean start_v4l2_stream(GstImxV4L2Object *self, gboolean do_start);
static gboolean set_streaming_parm_capture_mode(GstImxV4L2Object *self, gint width, gint height, struct v4l2_captureparm *capture_parm);
static gboolean is_v4l2_queue_empty(GstImxV4L2Object *self);
static gboolean is_v4l2_queue_full(GstImxV4L2Object *self);


static void gst_imx_v4l2_object_class_init(GstImxV4L2ObjectClass *klass)
{
	GObjectClass *object_class;

	GST_DEBUG_CATEGORY_INIT(imx_v4l2_object_debug, "imxv4l2videoobject", 0, "NXP i.MX V4L2 object");

	gst_imx_v4l2_object_internal_imxdmabuffer_map_quark = g_quark_from_static_string("gst-imx-v4l2-imxdmabuffer-map");

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_v4l2_object_finalize);
}


static void gst_imx_v4l2_object_init(GstImxV4L2Object *self)
{
	int ret;

	ret = pipe(self->control_pipe_fds);
	g_assert(ret == 0);

	self->num_buffers = 0;

	self->v4l2_fd = -1;

	self->stream_on = FALSE;

	self->unlocked = 0;

	g_queue_init(&(self->unused_v4l2_buffer_indices));

	/* Set this initially to TRUE in case there is an error while queuing
	 * frames  _before_ streaming is enabled. In such a case, the waiting
	 * loop in gst_imx_v4l2_object_unlock() would never finish. */
	self->dequeuing_finished = TRUE;

	g_mutex_init(&(self->dequeuing_mutex));
	g_cond_init(&(self->dequeuing_cond));
}


static void gst_imx_v4l2_object_finalize(GObject *object)
{
	gint i;
	GstImxV4L2Object *self = GST_IMX_V4L2_OBJECT(object);

	if (self->stream_on)
		start_v4l2_stream(self, FALSE);

	gst_imx_v4l2_clear_probe_result(&(self->probe_result));

	/* If there any queued gstbuffers left,
	 * unref them to avoid resource leaks. */
	for (i = 0; i < self->num_buffers; ++i)
	{
		if (self->queued_gstbuffers[i] != NULL)
		{
			GstBuffer *queued_gstbuffer = self->queued_gstbuffers[i];
			GST_DEBUG_OBJECT(
				self,
				"unref'ing leftover queued buffer with refcount %d: %" GST_PTR_FORMAT,
				GST_MINI_OBJECT_REFCOUNT_VALUE(queued_gstbuffer),
				(gpointer)queued_gstbuffer
			);
			gst_buffer_unref(queued_gstbuffer);
		}
	}

	g_free(self->queued_gstbuffers);

	close(self->control_pipe_fds[0]);
	close(self->control_pipe_fds[1]);

	if (self->v4l2_fd > 0)
		close(self->v4l2_fd);

	g_queue_clear(&(self->unused_v4l2_buffer_indices));

	g_mutex_clear(&(self->dequeuing_mutex));
	g_cond_clear(&(self->dequeuing_cond));

	G_OBJECT_CLASS(gst_imx_v4l2_object_parent_class)->finalize(object);
}


GstImxV4L2Object* gst_imx_v4l2_object_new(GstImxV4L2Context *imx_v4l2_context, GstImxV4L2VideoInfo const *video_info)
{
	gint i;
	GstImxV4L2Object *imx_v4l2_object;
	GstImxV4L2ProbeResult const *context_probe_result;

	g_assert(imx_v4l2_context != NULL);
	g_assert(video_info != NULL);


	imx_v4l2_object = (GstImxV4L2Object *)g_object_new(gst_imx_v4l2_object_get_type(), NULL);

	GST_DEBUG_OBJECT(imx_v4l2_object, "created new imxv4l2 object %" GST_PTR_FORMAT, (gpointer)(imx_v4l2_object));


	memcpy(&(imx_v4l2_object->video_info), video_info, sizeof(GstImxV4L2VideoInfo));


	GST_OBJECT_LOCK(imx_v4l2_context);

	context_probe_result = gst_imx_v4l2_context_get_probe_result(imx_v4l2_context);
	if (context_probe_result == NULL)
	{
		GST_ERROR_OBJECT(imx_v4l2_object, "context does not contain a probe result; device may not have been probed");
		goto error;
	}

	gst_imx_v4l2_copy_probe_result(
		&(imx_v4l2_object->probe_result),
		context_probe_result 
	);

	imx_v4l2_object->num_buffers = gst_imx_v4l2_context_get_num_buffers(imx_v4l2_context);
	imx_v4l2_object->device_type = gst_imx_v4l2_context_get_device_type(imx_v4l2_context);

	if (imx_v4l2_object->num_buffers < 2)
	{
		GST_ERROR_OBJECT(imx_v4l2_object, "insufficient buffers configured in context; expected: >= 2; got: %d", imx_v4l2_object->num_buffers);
		goto error;
	}

	imx_v4l2_object->v4l2_fd = gst_imx_v4l2_context_open_fd(imx_v4l2_context);
	if (imx_v4l2_object->v4l2_fd < 0)
		goto error;

	imx_v4l2_object->queued_gstbuffers = g_malloc0(sizeof(GstBuffer *) * imx_v4l2_object->num_buffers);


	/* Initially, all indices are unused, since no buffer has been
	 * queued. Fill the unused_v4l2_buffer_indices queue to reflect
	 * that all indices are available for new buffers to use. */
	for (i = 0; i < imx_v4l2_object->num_buffers; ++i)
		g_queue_push_tail(&(imx_v4l2_object->unused_v4l2_buffer_indices), GINT_TO_POINTER(i));


	if (!setup_device(imx_v4l2_object))
		goto error;


finish:
	GST_OBJECT_UNLOCK(imx_v4l2_context);
	return imx_v4l2_object;

error:
	g_object_unref(G_OBJECT(imx_v4l2_object));
	imx_v4l2_object = NULL;

	goto finish;
}


GstImxV4L2VideoInfo const *gst_imx_v4l2_object_get_video_info(GstImxV4L2Object *imx_v4l2_object)
{
	return &(imx_v4l2_object->video_info);
}


GstFlowReturn gst_imx_v4l2_object_queue_buffer(GstImxV4L2Object *imx_v4l2_object, GstBuffer *buffer)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	struct v4l2_buffer v4l2_buf;
	gint v4l2_buf_index;
	ImxDmaBuffer *dma_buffer;

	g_assert(imx_v4l2_object != NULL);
	g_assert(buffer != NULL);

	if (g_atomic_int_get(&(imx_v4l2_object->unlocked)))
	{
		GST_DEBUG_OBJECT(imx_v4l2_object, "we are currently unlocked, probably due to flushing; not queuing anything");
		flow_ret = GST_FLOW_FLUSHING;
		goto finish;
	}

	if (is_v4l2_queue_full(imx_v4l2_object))
	{
		GST_DEBUG_OBJECT(imx_v4l2_object, "we cannot currently queue buffers because the queue is full");
		flow_ret = GST_IMX_V4L2_FLOW_QUEUE_IS_FULL;
		goto finish;
	}

	dma_buffer = gst_imx_get_dma_buffer_from_buffer(buffer);
	if (G_UNLIKELY(dma_buffer == NULL))
	{
		GST_ERROR_OBJECT(imx_v4l2_object, "supplied gstbuffer does not contain a DMA buffer");
		flow_ret = GST_FLOW_ERROR;
		goto finish;
	}

	v4l2_buf_index = GPOINTER_TO_INT(g_queue_pop_head(&(imx_v4l2_object->unused_v4l2_buffer_indices)));
	g_assert(v4l2_buf_index < imx_v4l2_object->num_buffers);

	memset(&v4l2_buf, 0, sizeof(v4l2_buf));

	v4l2_buf.type = imx_v4l2_object->v4l2_buffer_type;
	v4l2_buf.memory = V4L2_MEMORY_USERPTR;
	v4l2_buf.index = v4l2_buf_index;

	if (imx_v4l2_object->probe_result.capture_chip != GST_IMX_V4L2_CAPTURE_CHIP_UNIDENTIFIED)
	{
		/* We use an NXP mxc_v4l2 driver specific hack. That driver
		 * uses USERPTR in a non standard compliant way. the m.userptr
		 * field isn't really used in the driver. Instead, m.offset
		 * contains the physical address to the buffer we pass to
		 * the driver. From the driver source's mxc_v4l2_prepare_bufs()
		 * function:
		 *
		 * cam->frame[buf->index].buffer.m.offset = cam->frame[buf->index].paddress = buf->m.offset;
		 */

		GST_LOG_OBJECT(imx_v4l2_object, "will use V4L2 buffer index %d for queuing gstbuffer %" GST_PTR_FORMAT " (physical address %" IMX_PHYSICAL_ADDRESS_FORMAT ")", v4l2_buf_index, (gpointer)buffer, imx_dma_buffer_get_physical_address(dma_buffer));

		v4l2_buf.m.offset = imx_dma_buffer_get_physical_address(dma_buffer);
		v4l2_buf.length = imx_dma_buffer_get_size(dma_buffer);
	}
	else
	{
		/* If this is a device that doesn't use mxc_v4l2, we use
		 * USERPTR in the standard compliant way. For this, we need
		 * a virtual memory address to pass to V4L2 as the userptr
		 * value. Memory-map the imxdmabuffer and store the memory
		 * mapped virtual address as a buffer qdata so we can
		 * retrieve this address later. (libimxdmabuffer unmaps
		 * the buffer automatically when that buffer is deallocated.) */

		gpointer mapped_virtual_address;

		mapped_virtual_address = gst_mini_object_get_qdata(
			GST_MINI_OBJECT_CAST(buffer),
			gst_imx_v4l2_object_internal_imxdmabuffer_map_quark
			);
		if (mapped_virtual_address == NULL)
		{
			int err;

			mapped_virtual_address = imx_dma_buffer_map(
				dma_buffer,
				IMX_DMA_BUFFER_MAPPING_FLAG_READ |
				IMX_DMA_BUFFER_MAPPING_FLAG_WRITE |
				IMX_DMA_BUFFER_MAPPING_FLAG_MANUAL_SYNC,
				&err
			);
			if (G_UNLIKELY(mapped_virtual_address == NULL))
			{
				GST_ERROR_OBJECT(imx_v4l2_object, "imx_dma_buffer_map() failure: %s (%d)", strerror(err), err);
				flow_ret = GST_FLOW_ERROR;
				goto finish;
			}

			gst_mini_object_set_qdata(
				GST_MINI_OBJECT_CAST(buffer),
				gst_imx_v4l2_object_internal_imxdmabuffer_map_quark,
				mapped_virtual_address,
				NULL
			);
		}

		v4l2_buf.m.userptr = (unsigned long)mapped_virtual_address;
		v4l2_buf.length = imx_dma_buffer_get_size(dma_buffer);
	}

	/* XXX: The mxc_vout driver expects the buffer length to be
	 * page aligned. However, it does not actually do anything
	 * with the extra bytes. It is unclear why this page alignment
	 * requirement is present at all in the mxc_vout driver.
	 * (The alignment is applied in the mxc_vout_buffer_setup()
	 * function in mxc_vout.c.) We have to align the size here
	 * accordingly. Otherwise, displaying the frame may not work.
	 * Aligning the size here like that is clearly questionable,
	 * but the only alternative would be to allocate custom buffers
	 * and copy each and every frame, causing high CPU usage and
	 * increased bandwidth usage. And as said, the driver does
	 * not seem to actually try to access any extra bytes beyond
	 * the actual frame size. */
	if (imx_v4l2_object->device_type == GST_IMX_V4L2_DEVICE_TYPE_OUTPUT)
		v4l2_buf.length = GST_IMX_V4L2_PAGE_ALIGN(v4l2_buf.length);

	if (imx_v4l2_object->probe_result.capture_chip != GST_IMX_V4L2_CAPTURE_CHIP_UNIDENTIFIED)
	{
		struct v4l2_buffer temp_v4l2_buf = v4l2_buf;

		/* NOTE: We have to call QUERYBUF always before each QBUF.
		 * This is an NXP mxc_v4l2 driver issue. QUERYBUF triggers
		 * an internal update that is necessary to make the capture
		 * work properly (field values like index and m.offset from
		 * the buffer may not be propagated internally otherwise).
		 * The output is not important, which is why we don't use
		 * temp_v4l2_buf afterwards. All we want is to trigger that
		 * internal update. */
		if (G_UNLIKELY(ioctl(imx_v4l2_object->v4l2_fd, VIDIOC_QUERYBUF, &temp_v4l2_buf) < 0))
		{
			GST_LOG_OBJECT(imx_v4l2_object, "could not query V4L2 buffer with index %d: %s (%d)", v4l2_buf_index, strerror(errno), errno);
			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}
	}

	if (G_UNLIKELY(ioctl(imx_v4l2_object->v4l2_fd, VIDIOC_QBUF, &v4l2_buf) < 0))
	{
		GST_LOG_OBJECT(imx_v4l2_object, "could not queue V4L2 buffer with index %d: %s (%d)", v4l2_buf_index, strerror(errno), errno);
		flow_ret = GST_FLOW_ERROR;
		goto finish;
	}

	GST_LOG_OBJECT(imx_v4l2_object, "queued buffer: %" GST_PTR_FORMAT, (gpointer)buffer);

	imx_v4l2_object->queued_gstbuffers[v4l2_buf_index] = buffer;
	gst_buffer_ref(buffer);

	/* Check if we can enable the stream now if it isn't already on. */
	if (!(imx_v4l2_object->stream_on))
	{
		/* We can enable the stream if either we are outputting frames
		 * (output devices do not need a number of frames pre-queued)
		 * or the queue has been fully pre-filled with buffers. */
		if ((imx_v4l2_object->device_type == GST_IMX_V4L2_DEVICE_TYPE_OUTPUT) || is_v4l2_queue_full(imx_v4l2_object))
		{
			if (!start_v4l2_stream(imx_v4l2_object, TRUE))
			{
				flow_ret = GST_FLOW_ERROR;
				goto finish;
			}
		}
	}

finish:
	return flow_ret;
}


GstFlowReturn gst_imx_v4l2_object_dequeue_buffer(GstImxV4L2Object *imx_v4l2_object, GstBuffer **buffer)
{
	GstFlowReturn flow_ret = GST_FLOW_OK;
	struct pollfd pfd[2];
	struct v4l2_buffer v4l2_buf;
	gint v4l2_buf_index;

	g_assert(imx_v4l2_object != NULL);
	g_assert(buffer != NULL);

	GST_LOG_OBJECT(imx_v4l2_object, "attempting to dequeue a buffer");


	/* First checks. */

	/* If we are currently unlocked, we are not supposed to dequeue anything, so exit early. */
	if (g_atomic_int_get(&(imx_v4l2_object->unlocked)))
	{
		GST_DEBUG_OBJECT(imx_v4l2_object, "we are currently unlocked, probably due to flushing; not dequeuing nything");
		flow_ret = GST_FLOW_FLUSHING;
		goto finish;
	}

	/* Can't dequeue anything without a running stream. */
	if (!(imx_v4l2_object->stream_on))
	{
		GST_DEBUG_OBJECT(imx_v4l2_object, "stream did not yet start; need to queue more buffers first");
		return GST_IMX_V4L2_FLOW_NEEDS_MORE_BUFFERS_QUEUED;
	}

	/* Can't dequeue anything if there are no buffers to dequeue. */
	if (is_v4l2_queue_empty(imx_v4l2_object))
	{
		GST_LOG_OBJECT(imx_v4l2_object, "no buffers queued; requesting more buffers");
		return GST_IMX_V4L2_FLOW_NEEDS_MORE_BUFFERS_QUEUED;
	}


	/* From this moment on, the rest of the code runs
	 * with the dequeuing_mutex locked. That's also
	 * why the checks above return immediately instead
	 * of going to the finish: label below - down there,
	 * the mutex is unlocked. Trying to unlock a mutex
	 * that's not locked crashes the program. */

	g_mutex_lock(&(imx_v4l2_object->dequeuing_mutex));
	imx_v4l2_object->dequeuing_finished = FALSE;

	/* Prepare the v4l2_buffer. We'll use the USERPTR IO method
	 * for informing V4L2 to write to our buffer, since that's
	 * the method used in gst_imx_v4l2_object_queue_buffer(). */
	memset(&v4l2_buf, 0, sizeof(v4l2_buf));
	v4l2_buf.type = imx_v4l2_object->v4l2_buffer_type;
	v4l2_buf.memory = V4L2_MEMORY_USERPTR;

	/* Prepare the pollfd array. The first entry will contain the
	 * control pipe that we'll use to wake up a poll() call
	 * if necessary. The second entry will contain the V4L2 FD
	 * that we'll wait on for frames to capture. */
	pfd[0].fd = CONTROL_PIPE_READ_FD(imx_v4l2_object);
	pfd[0].events = POLLIN | POLLERR;
	pfd[1].fd = imx_v4l2_object->v4l2_fd;
	pfd[1].events = POLLIN | POLLERR;

	GST_LOG_OBJECT(imx_v4l2_object, "waiting for available buffer");

	GST_LOG_OBJECT(imx_v4l2_object, "entering poll() loop");
	while (TRUE)
	{
		GST_LOG_OBJECT(imx_v4l2_object, "poll() loop");
		if (poll(pfd, sizeof(pfd) / sizeof(struct pollfd), -1) < 0)
		{
			switch (errno)
			{
				case EINTR:
					GST_DEBUG_OBJECT(imx_v4l2_object, "poll() was interrupted by signal; retrying");
					break;
				default:
					GST_ERROR_OBJECT(imx_v4l2_object, "poll() failure: %s (%d)", strerror(errno), errno);
					flow_ret = GST_FLOW_ERROR;
					goto finish;
			}
		}
		else
			break;
	}
	GST_LOG_OBJECT(imx_v4l2_object, "leaving poll() loop");

	if (G_UNLIKELY(pfd[0].revents & (POLLIN | POLLERR)))
	{
		GST_DEBUG_OBJECT(imx_v4l2_object, "dequeue operation was canceled by unlock() call");
		flow_ret = GST_FLOW_FLUSHING;
		goto finish;
	}

	if (G_LIKELY(pfd[1].revents & (POLLIN | POLLERR)))
	{
		GstClockTime timestamp;

		if (G_UNLIKELY(pfd[1].revents & POLLERR))
		{
			GST_ERROR_OBJECT(imx_v4l2_object, "poll() reports error from the V4L2 device FD - this usually indicates missing QBUF calls before the stream was enabled");
			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}

		GST_LOG_OBJECT(imx_v4l2_object, "retrieving newly dequeued frame");

		/* Do the actual dequeuing. */
		if (G_UNLIKELY(ioctl(imx_v4l2_object->v4l2_fd, VIDIOC_DQBUF, &v4l2_buf) < 0))
		{
			GST_ERROR_OBJECT(imx_v4l2_object, "could not dequeue V4L2 buffer: %s (%d)", strerror(errno), errno);
			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}

		/* Retrieve the V4L2 buffer index (which we set in gst_imx_v4l2_object_queue_buffer())
		 * to associate this dequeued v4l2_buffer with one of the queued GstBuffers.
		 * That's done by looking into the queued_gstbuffers at this index. The
		 * GstBuffer there is the one that is associated with the dequeued V4L2 buffer. */
		v4l2_buf_index = v4l2_buf.index;

		/* V4L2 also tells us the timestamp of the captured frame.
		 * Get it so we can use it for the GstBuffer.. */
		timestamp = GST_TIMEVAL_TO_TIME(v4l2_buf.timestamp);

		GST_LOG_OBJECT(imx_v4l2_object, "retrieved dequeued frame with V4L2 buffer index %d and timestamp %" GST_TIME_FORMAT, v4l2_buf_index, GST_TIME_ARGS(timestamp));

		/* Sanity check to see that the index is OK. */
		g_assert(v4l2_buf_index < imx_v4l2_object->num_buffers);

		/* The index of the dequeued buffer is no longer in used, so put it back
		 * in the unused_v4l2_buffer_indices queue to be able to reuse it later. */
		g_queue_push_tail(&(imx_v4l2_object->unused_v4l2_buffer_indices), GINT_TO_POINTER(v4l2_buf_index));

		/* Retrieve the GstBuffer associated with the dequeued V4L2 buffer,
		 * and set its timestamp to the V4L2 timestamp. */
		*buffer = imx_v4l2_object->queued_gstbuffers[v4l2_buf_index];
		GST_BUFFER_PTS(*buffer) = timestamp;

		/* Set the buffer's interlace flags. */
		if (imx_v4l2_object->interlaced_video)
		{
			GST_BUFFER_FLAG_SET(*buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
			if (imx_v4l2_object->interlace_top_field_first)
				GST_BUFFER_FLAG_SET(*buffer, GST_VIDEO_BUFFER_FLAG_TFF);
			else
				GST_BUFFER_FLAG_UNSET(*buffer, GST_VIDEO_BUFFER_FLAG_TFF);
		}
		else
			GST_BUFFER_FLAG_UNSET(*buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);

		/* Clear the entry where the queued GstBuffer used to be. */
		imx_v4l2_object->queued_gstbuffers[v4l2_buf_index] = NULL;

		GST_LOG_OBJECT(imx_v4l2_object, "got buffer for dequeued frame: %" GST_PTR_FORMAT, (gpointer)(*buffer));
	}

finish:
	GST_LOG_OBJECT(imx_v4l2_object, "dequeue attempt finished with return value %s", gst_flow_get_name(flow_ret));

	/* Notify any waiting party that dequeuing just finished. */
	imx_v4l2_object->dequeuing_finished = TRUE;
	g_cond_signal(&(imx_v4l2_object->dequeuing_cond));

	g_mutex_unlock(&(imx_v4l2_object->dequeuing_mutex));

	return flow_ret;
}


void gst_imx_v4l2_object_unlock(GstImxV4L2Object *imx_v4l2_object)
{
	gint i;
	int ret;
	static char const dummy = 0;

	GST_DEBUG_OBJECT(imx_v4l2_object, "unlocking imxv4l2 object %" GST_PTR_FORMAT, (gpointer)(imx_v4l2_object));

	/* Mark ourselves as unlocked. */
	g_atomic_int_set(&(imx_v4l2_object->unlocked), 1);

	/* Send a request to any blocking dequeue call to wake up
	 * so we can proceed with the unlock operation. */
	GST_DEBUG_OBJECT(imx_v4l2_object, "sending request to any ongoing blocking dqbuf call to wake up");
	ret = write(CONTROL_PIPE_WRITE_FD(imx_v4l2_object), &dummy, 1);
	g_assert(ret >= 0);

	/* Now wait. */
	GST_DEBUG_OBJECT(imx_v4l2_object, "waiting for any ongoing blocking dqbuf call to wake up");
	g_mutex_lock(&(imx_v4l2_object->dequeuing_mutex));
	while (!imx_v4l2_object->dequeuing_finished)
		g_cond_wait(&(imx_v4l2_object->dequeuing_cond), &(imx_v4l2_object->dequeuing_mutex));
	g_mutex_unlock(&(imx_v4l2_object->dequeuing_mutex));

	/* If any blocking dequeue call was ongoing, it stopped by now.
	 * It is now safe to shut down the stream. */
	GST_DEBUG_OBJECT(imx_v4l2_object, "turning off V4L2 stream");
	if (imx_v4l2_object->stream_on)
		start_v4l2_stream(imx_v4l2_object, FALSE);

	/* Reset the unused_v4l2_buffer_indices queue to its initial value
	 * when no frames were in the V4L2 queue, and then unref any buffers
	 * that may still be in the queued_gstbuffers array. That's because
	 * we just flushed the V4L2 queue by shutting down stream, so these
	 * buffers are not in use by V4L2 anymore, so their references that
	 * are stored in the queued_gstbuffers array need to be removed. */
	GST_DEBUG_OBJECT(imx_v4l2_object, "unref any queued gstbuffers");
	g_queue_clear(&(imx_v4l2_object->unused_v4l2_buffer_indices));
	for (i = 0; i < imx_v4l2_object->num_buffers; ++i)
	{
		g_queue_push_tail(&(imx_v4l2_object->unused_v4l2_buffer_indices), GINT_TO_POINTER(i));
		if (imx_v4l2_object->queued_gstbuffers[i] != NULL)
		{
			GstBuffer *queued_gstbuffer = imx_v4l2_object->queued_gstbuffers[i];

			GST_DEBUG_OBJECT(
				imx_v4l2_object,
				"unref'ing queued buffer with refcount %d during unlock: %" GST_PTR_FORMAT,
				GST_MINI_OBJECT_REFCOUNT_VALUE(queued_gstbuffer),
				(gpointer)queued_gstbuffer
			);
			gst_buffer_unref(queued_gstbuffer);

			imx_v4l2_object->queued_gstbuffers[i] = NULL;
		}
	}

	GST_DEBUG_OBJECT(imx_v4l2_object, "unlocking done");
}


void gst_imx_v4l2_object_unlock_stop(GstImxV4L2Object *imx_v4l2_object)
{
	GST_DEBUG_OBJECT(imx_v4l2_object, "undoing unlock of imxv4l2 object %" GST_PTR_FORMAT, (gpointer)(imx_v4l2_object));
	g_atomic_int_set(&(imx_v4l2_object->unlocked), 0);
}


static gboolean setup_device(GstImxV4L2Object *self)
{
	gboolean retval = TRUE;
	gint std_fps_n = 0, std_fps_d = 0;


	/* Perform initital checks and store the type we'll
	 * use for v4l2_buffers that we queue.. */

	switch (self->device_type)
	{
		case GST_IMX_V4L2_DEVICE_TYPE_CAPTURE:
			if (!(self->probe_result.v4l2_device_capabilities & V4L2_CAP_VIDEO_CAPTURE))
			{
				GST_ERROR_OBJECT(self, "device does not handle video capture");
				goto error;
			}

			self->v4l2_buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

			break;

		case GST_IMX_V4L2_DEVICE_TYPE_OUTPUT:
			if (!(self->probe_result.v4l2_device_capabilities & V4L2_CAP_VIDEO_OUTPUT))
			{
				GST_ERROR_OBJECT(self, "device does not handle video output");
				goto error;
			}

			self->v4l2_buffer_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

			break;

		default:
			g_assert_not_reached();
	}

	if (!(self->probe_result.v4l2_device_capabilities & V4L2_CAP_STREAMING))
	{
		GST_ERROR_OBJECT(self, "device does not handle frame streaming");
		goto error;
	}


	/* Check if we can detect any particular video standard (NTSC, PAL etc.) */

	if (self->device_type == GST_IMX_V4L2_DEVICE_TYPE_CAPTURE)
	{
		v4l2_std_id video_standard_id = V4L2_STD_UNKNOWN;
		gint count = 10;

		/* VIDIOC_QUERYSTD instructs the driver to try to probe the video standard.
		 * Failure is not a fatal error; the operation may be unsupported or
		 * simply not be implemented, so we continue regardless. */
		if (ioctl(self->v4l2_fd, VIDIOC_QUERYSTD, &video_standard_id) < 0)
		{
			GST_DEBUG_OBJECT(self, "could not query video standard: %s (%d)", strerror(errno), errno);
		}
		else
		{
			/* Now try to get the current video standard (if there is any).
			 * Some devices may need a while to configure themselves, so we
			 * have to try several times to get the video standard. */
			video_standard_id = V4L2_STD_ALL;
			while ((video_standard_id == V4L2_STD_ALL) && (count > 0))
			{
				if (ioctl(self->v4l2_fd, VIDIOC_G_STD, &video_standard_id) < 0)
				{
					switch (errno)
					{
						case ENODATA:
							video_standard_id = V4L2_STD_UNKNOWN;
							break;

						default:
							GST_ERROR_OBJECT(self, "could not get video standard: %s (%d)", strerror(errno), errno);
							goto error;
					}
				}

				if (video_standard_id != V4L2_STD_ALL)
					break;

				g_usleep(G_USEC_PER_SEC / 10);

				--count;
			}

			/* If video_standard_id is still set to V4L2_STD_ALL, it
			 * means the loop above exited without successfully getting
			 * the video standard. It is therefore unknown if there is
			 * any standard present. */
			if (video_standard_id == V4L2_STD_ALL)
				video_standard_id = V4L2_STD_UNKNOWN;
		}

		if (video_standard_id != V4L2_STD_UNKNOWN)
		{
			/* Make sure this video standard is actually used by the driver. */
			if (ioctl(self->v4l2_fd, VIDIOC_S_STD, &video_standard_id) < 0)
			{
				GST_ERROR_OBJECT(self, "could not set video standard: %s (%d)", strerror(errno), errno);
				goto error;
			}


			/* If a specific video standard is used, we have to override
			 * any framerate specified in video_info, since the video
			 * standards themselves dictate what framerate to use. To
			 * that end, record the standard's frame rate here. */

			if (video_standard_id & V4L2_STD_525_60)
			{
				std_fps_n = 30;
				std_fps_d = 1;
			}
			else
			{
				std_fps_n = 25;
				std_fps_d = 1;
			}

			/* M/NTSC transmits the bottom field first,
			 * all other standards the top field first. */
			self->interlace_top_field_first = !(video_standard_id & V4L2_STD_NTSC);

			GST_DEBUG_OBJECT(self, "will use the video standard's frame rate %d/%d", std_fps_n, std_fps_d);
			GST_DEBUG_OBJECT(self, "standard uses top-field-first interlace: %d", self->interlace_top_field_first);
		}
		else 
		{
			self->interlace_top_field_first = FALSE;
			GST_DEBUG_OBJECT(self, "standard video timings are not supported or could not be detected");
		}
	}


	/* Fill and use the v4l2_streaming_parm structure.
	 * We only have to care about the timeperframe field here,
	 * since all the others are unused here, are reserved, or
	 * are filled by the driver (and not by us). */

	if (self->device_type == GST_IMX_V4L2_DEVICE_TYPE_CAPTURE)
	{
		struct v4l2_streamparm v4l2_streaming_parm;
		struct v4l2_fract *timeperframe;
		gint width, height;

		memset(&v4l2_streaming_parm, 0, sizeof(v4l2_streaming_parm));
		v4l2_streaming_parm.type = self->v4l2_buffer_type;

		switch (self->device_type)
		{
			case GST_IMX_V4L2_DEVICE_TYPE_CAPTURE:
				timeperframe = &(v4l2_streaming_parm.parm.capture.timeperframe);
				break;

			case GST_IMX_V4L2_DEVICE_TYPE_OUTPUT:
				timeperframe = &(v4l2_streaming_parm.parm.output.timeperframe);
				break;

			default:
				g_assert_not_reached();
		}

		/* NOTE: fps_n and fps_d are reversed, because
		 * V4L2 uses time-per-frame, while "fps" is
		 * essentially the reverse (frames per time). */

		switch (self->video_info.type)
		{
			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			{
				GstVideoInfo *gst_info = &(self->video_info.info.gst_info);

				if (std_fps_n != 0)
					GST_VIDEO_INFO_FPS_N(gst_info) = std_fps_n;
				if (std_fps_d != 0)
					GST_VIDEO_INFO_FPS_D(gst_info) = std_fps_d;

				timeperframe->denominator = GST_VIDEO_INFO_FPS_N(gst_info);
				timeperframe->numerator = GST_VIDEO_INFO_FPS_D(gst_info);
				width = GST_VIDEO_INFO_WIDTH(gst_info);
				height = GST_VIDEO_INFO_HEIGHT(gst_info);

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			{
				GstImxV4L2BayerInfo *bayer_info = &(self->video_info.info.bayer_info);

				if (std_fps_n != 0)
					bayer_info->fps_n = std_fps_n;
				if (std_fps_d != 0)
					bayer_info->fps_d = std_fps_d;

				timeperframe->denominator = bayer_info->fps_n;
				timeperframe->numerator = bayer_info->fps_d;
				width = bayer_info->width;
				height = bayer_info->height;

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
			{
				GstImxV4L2CodecInfo *codec_info = &(self->video_info.info.codec_info);

				if (std_fps_n != 0)
					codec_info->fps_n = std_fps_n;
				if (std_fps_d != 0)
					codec_info->fps_d = std_fps_d;

				timeperframe->denominator = codec_info->fps_n;
				timeperframe->numerator = codec_info->fps_d;
				width = codec_info->width;
				height = codec_info->height;

				break;
			}

			default:
				g_assert_not_reached();
		}

		if (self->device_type == GST_IMX_V4L2_DEVICE_TYPE_CAPTURE)
		{
			if (!set_streaming_parm_capture_mode(self, width, height, &(v4l2_streaming_parm.parm.capture)))
					goto error;
		}

		if (ioctl(self->v4l2_fd, VIDIOC_S_PARM, &v4l2_streaming_parm) < 0)
		{
			GST_ERROR_OBJECT(self, "could not set video parameters: %s (%d)", strerror(errno), errno);
			goto error;
		}
	}

	if ((self->device_type == GST_IMX_V4L2_DEVICE_TYPE_CAPTURE)
	 && (self->probe_result.capture_chip != GST_IMX_V4L2_CAPTURE_CHIP_UNIDENTIFIED))
	{
		/* Select input #1. This is the input with the image converter (IC)
		 * inserted. Without it, it is not possible to capture 720p and 1080p
		 * video, so enable it always. This is mxc_v4l2 specific behavior. */

		int input = 1;

		if (ioctl(self->v4l2_fd, VIDIOC_S_INPUT, &input) < 0)
		{
			GST_ERROR_OBJECT(self, "could not set input: %s (%d)", strerror(errno), errno);
			goto error;
		}
	}

	/* Fill and use the v4l2_fmt structure. */

	{
		struct v4l2_format v4l2_fmt;
		GstImxV4L2VideoFormat const *actual_imxv4l2_vidfmt;
		GstVideoInterlaceMode requested_interlace_mode;
		GstVideoInterlaceMode actual_interlace_mode;

		memset(&v4l2_fmt, 0, sizeof(v4l2_fmt));
		v4l2_fmt.type = self->v4l2_buffer_type;

		/* Query current format parameters so we retain
		 * them all, since we only modify some of them. */
		if (ioctl(self->v4l2_fd, VIDIOC_G_FMT, &v4l2_fmt) < 0)
		{
			GST_ERROR_OBJECT(self, "could not get video format: %s (%d)", strerror(errno), errno);
			goto error;
		}

		switch (self->video_info.type)
		{
			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			{
				GstVideoInfo *gst_info = &(self->video_info.info.gst_info);
				GstImxV4L2VideoFormat const *imxv4l2_vidfmt = gst_imx_v4l2_get_by_gst_video_format_from_probe_result(
					&(self->probe_result),
					GST_VIDEO_INFO_FORMAT(gst_info)
				);

				if (imxv4l2_vidfmt == NULL)
				{
					GST_ERROR_OBJECT(self, "could not find imxv4l2 video format for GStreamer video format %s", gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(gst_info)));
					goto error;
				}

				requested_interlace_mode = GST_VIDEO_INFO_INTERLACE_MODE(gst_info);

				v4l2_fmt.fmt.pix.pixelformat = imxv4l2_vidfmt->v4l2_pixelformat;
				v4l2_fmt.fmt.pix.width = GST_VIDEO_INFO_WIDTH(gst_info);
				v4l2_fmt.fmt.pix.height = GST_VIDEO_INFO_HEIGHT(gst_info);
				v4l2_fmt.fmt.pix.bytesperline = GST_VIDEO_INFO_PLANE_STRIDE(gst_info, 0);
				v4l2_fmt.fmt.pix.sizeimage = GST_VIDEO_INFO_SIZE(gst_info);

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			{
				GstImxV4L2BayerInfo *bayer_info = &(self->video_info.info.bayer_info);
				GstImxV4L2VideoFormat const *imxv4l2_vidfmt = gst_imx_v4l2_get_by_bayer_video_format(bayer_info->format);

				if (imxv4l2_vidfmt == NULL)
				{
					GST_ERROR_OBJECT(self, "could not find imxv4l2 video format for Bayer video format %s", gst_imx_v4l2_bayer_format_to_string(bayer_info->format));
					goto error;
				}

				requested_interlace_mode = bayer_info->interlace_mode;

				v4l2_fmt.fmt.pix.pixelformat = imxv4l2_vidfmt->v4l2_pixelformat;
				v4l2_fmt.fmt.pix.width = bayer_info->width;
				v4l2_fmt.fmt.pix.height = bayer_info->height;
				v4l2_fmt.fmt.pix.bytesperline = 0;
				v4l2_fmt.fmt.pix.sizeimage = 0;

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
			{
				GstImxV4L2CodecInfo *codec_info = &(self->video_info.info.codec_info);
				GstImxV4L2VideoFormat const *imxv4l2_vidfmt = gst_imx_v4l2_get_by_codec_video_format(codec_info->format);

				if (imxv4l2_vidfmt == NULL)
				{
					GST_ERROR_OBJECT(self, "could not find imxv4l2 video format for codec with media type %s", gst_imx_v4l2_codec_format_to_media_type(codec_info->format));
					goto error;
				}

				requested_interlace_mode = codec_info->interlace_mode;

				v4l2_fmt.fmt.pix.pixelformat = imxv4l2_vidfmt->v4l2_pixelformat;
				v4l2_fmt.fmt.pix.width = codec_info->width;
				v4l2_fmt.fmt.pix.height = codec_info->height;
				v4l2_fmt.fmt.pix.bytesperline = 0;
				v4l2_fmt.fmt.pix.sizeimage = 0;

				break;
			}

			default:
				g_assert_not_reached();
		}

		if (self->device_type == GST_IMX_V4L2_DEVICE_TYPE_OUTPUT)
			v4l2_fmt.fmt.pix.field = (requested_interlace_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED) ? V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
		else
			v4l2_fmt.fmt.pix.field = V4L2_FIELD_ANY;

		if (ioctl(self->v4l2_fd, VIDIOC_S_FMT, &v4l2_fmt) < 0)
		{
			GST_ERROR_OBJECT(self, "could not set video format: %s (%d)", strerror(errno), errno);
			goto error;
		}

		/* Look at the contents from v4l2_fmt, since the VIDIOC_S_FMT
		 * call above may have been changed by the driver. */

		actual_imxv4l2_vidfmt = gst_imx_v4l2_get_by_v4l2_pixelformat(v4l2_fmt.fmt.pix.pixelformat);

		if (actual_imxv4l2_vidfmt == NULL)
		{
			GST_ERROR_OBJECT(self, "could not find imxv4l2 video format for V4L2 pixel format %#08" G_GINT32_MODIFIER "x", (guint32)(v4l2_fmt.fmt.pix.pixelformat));
			goto error;
		}

		/* Only INTERLACED and NONE are supported by the NXP driver. */
		switch (v4l2_fmt.fmt.pix.field)
		{
			case V4L2_FIELD_INTERLACED:
				actual_interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
				self->interlaced_video = TRUE;
				break;

			case V4L2_FIELD_NONE:
			default:
				actual_interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
				self->interlaced_video = FALSE;
		}

		switch (self->video_info.type)
		{
			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			{
				GstVideoInfo *gst_info = &(self->video_info.info.gst_info);

				/* Cannot use gst_video_info_set_format() here, since that function
				 * resets all other fields that aren't defined by the arguments.
				 * For example, fps_n is set to 0 and fps_n is set to 1 by that
				 * function, which we do not want to happen. */
				gst_info->finfo = gst_video_format_get_info(actual_imxv4l2_vidfmt->format.gst_format);
				GST_VIDEO_INFO_WIDTH(gst_info) = v4l2_fmt.fmt.pix.width;
				GST_VIDEO_INFO_HEIGHT(gst_info) = v4l2_fmt.fmt.pix.height;

				GST_VIDEO_INFO_INTERLACE_MODE(gst_info) = actual_interlace_mode;

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			{
				GstImxV4L2BayerInfo *bayer_info = &(self->video_info.info.bayer_info);
				GstImxV4L2VideoFormat const *imxv4l2_vidfmt = gst_imx_v4l2_get_by_v4l2_pixelformat(v4l2_fmt.fmt.pix.pixelformat);

				bayer_info->format = imxv4l2_vidfmt->format.bayer_format;
				bayer_info->width = v4l2_fmt.fmt.pix.width;
				bayer_info->height = v4l2_fmt.fmt.pix.height;
				bayer_info->interlace_mode = actual_interlace_mode;

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
			{
				GstImxV4L2CodecInfo *codec_info = &(self->video_info.info.codec_info);
				GstImxV4L2VideoFormat const *imxv4l2_vidfmt = gst_imx_v4l2_get_by_v4l2_pixelformat(v4l2_fmt.fmt.pix.pixelformat);

				codec_info->format = imxv4l2_vidfmt->format.codec_format;
				codec_info->width = v4l2_fmt.fmt.pix.width;
				codec_info->height = v4l2_fmt.fmt.pix.height;
				codec_info->interlace_mode = actual_interlace_mode;

				break;
			}

			default:
				g_assert_not_reached();
		}
	}


	/* Request v4l2 buffers. */

	{
		/* We request USERPTR buffers. This allows us
		 * to use an NXP specific hack for passing
		 * physical addresses to the driver. See the
		 * gst_imx_v4l2_object_queue_buffer() code
		 * for more details about this. */

		struct v4l2_requestbuffers v4l2_bufrequest;

		memset(&v4l2_bufrequest, 0, sizeof(v4l2_bufrequest));
		v4l2_bufrequest.type = self->v4l2_buffer_type;
		v4l2_bufrequest.memory = V4L2_MEMORY_USERPTR;
		v4l2_bufrequest.count = self->num_buffers;

		if (ioctl(self->v4l2_fd, VIDIOC_REQBUFS, &v4l2_bufrequest) < 0)
		{
			GST_ERROR_OBJECT(self, "could not request %d buffer(s): %s (%d)", self->num_buffers, strerror(errno), errno);
			goto error;
		}

		GST_DEBUG_OBJECT(self, "requested %d buffer(s)", self->num_buffers);
	}


finish:
	return retval;

error:
	retval = FALSE;
	goto finish;
}


static gboolean start_v4l2_stream(GstImxV4L2Object *self, gboolean do_start)
{
	enum v4l2_buf_type type = self->v4l2_buffer_type;

	if (G_UNLIKELY(ioctl(self->v4l2_fd, do_start ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type) < 0))
	{
		GST_ERROR_OBJECT(self, "could not %s stream: %s (%d)", do_start ? "start" : "stop", strerror(errno), errno);
		return FALSE;
	}

	GST_DEBUG_OBJECT(self, "%s stream", do_start ? "started" : "stopped");

	self->stream_on = do_start;

	return TRUE;
}


static gboolean set_streaming_parm_capture_mode(GstImxV4L2Object *self, gint width, gint height, struct v4l2_captureparm *capture_parm)
{
	/* The mxc_v4l2 driver may require v4l2_captureparm's capturemode
	 * field to be set to a resolution specific value. This is not
	 * standards compliant; the driver uses this field for a different
	 * purpose instead. We therefore are forced to look at the resolution
	 * to set the appropriate capturemode value, otherwise capturing
	 * won't work properly. The capturemode value must be set to the
	 * corresponding framesize index when we enumerate the framesizes
	 * via VIDIOC_ENUM_FRAMESIZES. In the the probe_device_caps()
	 * function in gstimxv4l2context.c , the result of that ioctl is
	 * stored in the probe_result for this purpose. */

	gint i;
	GstImxV4L2ProbeResult *probe_result = &(self->probe_result);

	capture_parm->capturemode = 0;

	for (i = 0; i < probe_result->num_chip_specific_frame_sizes; ++i)
	{
		if ((width == probe_result->chip_specific_frame_sizes[i].width)
		 && (height == probe_result->chip_specific_frame_sizes[i].height))
		{
			capture_parm->capturemode = i;

			GST_DEBUG_OBJECT(
				self,
				"setting v4l2_captureparm capturemode value to %" G_GUINT32_FORMAT " to match resolution %d x %d",
				(guint32)(capture_parm->capturemode),
				width, height
			);
		}
	}

	return TRUE;
}


static gboolean is_v4l2_queue_empty(GstImxV4L2Object *self)
{
	/* If all indices are unused, it means that there' no
	 * currently queued v4l2_buffer instance, since these need
	 * to use indices. In other words, in this case, the V4L2
	 * queue is empty. */
	return (self->unused_v4l2_buffer_indices.length == (guint)(self->num_buffers));
}


static gboolean is_v4l2_queue_full(GstImxV4L2Object *self)
{
	/* If there are no unused indices left, it means that all
	 * indices are currently used by queued v4l2_buffer instances.
	 * In other words, the queue is full. Therefore, this comparison
	 * tells us whether or not the V4L2 queue is full. */
	return (self->unused_v4l2_buffer_indices.length == 0);
}
