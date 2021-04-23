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

#ifndef GST_IMX_V4L2_OBJECT_H
#define GST_IMX_V4L2_OBJECT_H

#include <gst/gst.h>
#include "gstimxv4l2context.h"


G_BEGIN_DECLS


#define GST_TYPE_IMX_V4L2_OBJECT             (gst_imx_v4l2_object_get_type())
#define GST_IMX_V4L2_OBJECT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_V4L2_OBJECT, GstImxV4L2Object))
#define GST_IMX_V4L2_OBJECT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_V4L2_OBJECT, GstImxV4L2ObjectClass))
#define GST_IMX_V4L2_OBJECT_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_V4L2_OBJECT, GstImxV4L2ObjectClass))
#define GST_IMX_V4L2_OBJECT_CAST(obj)        ((GstImxV4L2Object *)(obj))
#define GST_IS_IMX_V4L2_OBJECT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_V4L2_OBJECT))
#define GST_IS_IMX_V4L2_OBJECT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_V4L2_OBJECT))


#define GST_IMX_V4L2_FLOW_NEEDS_MORE_BUFFERS_QUEUED (GST_FLOW_CUSTOM_SUCCESS + 0)
#define GST_IMX_V4L2_FLOW_QUEUE_IS_FULL (GST_FLOW_CUSTOM_SUCCESS + 1)


/**
 * GstImxV4L2Object:
 *
 * Contains the main V4L2 capture / output logic including the V4L2 queue handling.
 * Buffers are queued and dequeued with this object.
 *
 * For capturing, a GstBuffer capable of holding a frame is queued by calling
 * @gst_imx_v4l2_object_queue_buffer. V4L2 handles the actual frame capturing
 * and stores the captured pixels in one of the queued frames. To dequeue a frame
 * with captured data, @gst_imx_v4l2_object_dequeue_buffer is called.
 *
 * For output, the polar opposite applies: A frame with data to output is queued
 * by calling @gst_imx_v4l2_object_queue_buffer. To get back an already used
 * (= displayed) frame, @gst_imx_v4l2_object_dequeue_buffer is called.
 *
 * This object opens a file descriptor for the device node that is configured
 * in the context that gets passed to @gst_imx_v4l2_object_new , and keeps that
 * file descriptor open until the object is finalized. It also keeps each
 * @GstBuffer that is queued ref'd. That way, it becomes possible to unref this
 * object without risking stale pointers to the buffers. Unref'ing this object
 * during capture / output is typically done when the caps are reconfigured,
 * because an opened and configured V4L2 device cannot be reconfigured anymore.
 */
typedef struct _GstImxV4L2Object GstImxV4L2Object;
typedef struct _GstImxV4L2ObjectClass GstImxV4L2ObjectClass;


GType gst_imx_v4l2_object_get_type(void);


/**
 * gst_imx_v4l2_object_new:
 * @imx_v4l2_context: @GstImxV4L2Context to use for setting up the object.
 * @video_info: @GstImxV4L2VideoInfo for configuring the capture frame size,
 *     video format etc.
 *
 * Creates a new @GstImxV4L2Object. An internal copy of the the probe result
 * from imx_v4l2_context is made with @gst_imx_v4l2_copy_probe_result,
 * and the number of queue buffers and the device type are read from the
 * context. After this call, imx_v4l2_context is not needed anymore by the
 * object.
 *
 * video_info is used for configuring the capture / output parameters like
 * video format, frame width/height, framerate. The object keeps an internal
 * copy of video_info.
 *
 * Returns: Pointer to a newly created object, or NULL in case of an error.
 */
GstImxV4L2Object* gst_imx_v4l2_object_new(GstImxV4L2Context *imx_v4l2_context, GstImxV4L2VideoInfo const *video_info);

/**
 * gst_imx_v4l2_object_get_video_info:
 * @imx_v4l2_object: @GstImxV4L2Object to get the type of.
 *
 * Returns: Pointer to the internal copy of the @GstImxV4L2VideoInfo that was
 *     passed to @gst_imx_v4l2_object_new.
 */
GstImxV4L2VideoInfo const *gst_imx_v4l2_object_get_video_info(GstImxV4L2Object *imx_v4l2_object);

/**
 * gst_imx_v4l2_object_queue_buffer:
 * @imx_v4l2_object: @GstImxV4L2Object to queue a buffer into.
 * @buffer: @GstBuffer to queue.
 *
 * Queues a @GstBuffer into the V4L2 device that is managed by this object.
 * When capturing, this buffer does not hold any specific data, and instead
 * provides the device with a buffer to write captured pixels into. When
 * outputting, this buffer holds a frame with pixels to display.
 *
 * The @GstBuffer must contain ImxDmabuffer DMA memory.
 *
 * This function also refs the buffer to make sure it is not deallocated
 * while V4L2 uses its memory.
 *
 * @gst_imx_v4l2_object_dequeue_buffer is used to dequeue that buffer again.
 *
 * This will return @GST_FLOW_FLUSHING while it is unlocked. See
 * @gst_imx_v4l2_object_unlock for more.
 *
 * If the V4L2 queue is full, this returns GST_IMX_V4L2_FLOW_QUEUE_IS_FULL.
 * In such a case, try to dequeue some buffers to make some room, then try
 * again to queue the buffer.
 *
 * If the return value is anything other than @GST_FLOW_OK, the buffer
 * will not have been ref'd.
 *
 * Returns:
 *     @GST_FLOW_OK if queuing succeeded.
 *     @GST_FLOW_FLUSHING if this is called while the object is unlocked.
 *     @GST_IMX_V4L2_FLOW_QUEUE_IS_FULL if the queue is full.
 *     @GST_FLOW_ERROR in case of an error.
 */
GstFlowReturn gst_imx_v4l2_object_queue_buffer(GstImxV4L2Object *imx_v4l2_object, GstBuffer *buffer);

/**
 * gst_imx_v4l2_object_dequeue_buffer:
 * @imx_v4l2_object: @GstImxV4L2Object to dequeue a buffer out of.
 * @buffer: @GstBuffer pointer to set to a dequeued buffer.
 *
 * Dequeues a @GstBuffer that was previously queued. When capturing,
 * buffers are dequeued to get the captured pixels. When outputting,
 * buffers are dequeued to get back the frames whose pixels have already
 * been displayed. In both cases, this function blocks until a buffer can
 * be dequeued or @gst_imx_v4l2_object_unlock is called.
 *
 * During the first few calls after the GstImxV4L2Object was created, this
 * call behaves differently: Instead of queuing buffers, it will return
 * @GST_IMX_V4L2_FLOW_NEEDS_MORE_BUFFERS_QUEUED . This is because in order
 * for a V4L2 stream to be able to begin, the queue must contain a minimum
 * set of buffers. As a result, this function will not be able to dequeue
 * anything. Users must call @gst_imx_v4l2_object_queue_buffer if
 * @GST_IMX_V4L2_FLOW_NEEDS_MORE_BUFFERS_QUEUED is returned. Eventually,
 * @GST_FLOW_OK will be returned instead, when enough initial buffers have
 * been queued.
 *
 * *buffer is set to the pointer of a dequeued @GstBuffer. Note that even
 * though @gst_imx_v4l2_object_queue_buffer refs queued buffesr, this
 * function does not unref dequeued buffers. This is done in case the
 * queue was holding the only remaining reference to the buffer. It is
 * up to the user to unref it.
 *
 * Returns:
 *     @GST_FLOW_OK if dequeuing succeeded.
 *     @GST_FLOW_FLUSHING if this is called while the object is unlocked.
 *     @GST_IMX_V4L2_FLOW_NEEDS_MORE_BUFFERS_QUEUED if dequeuing a buffer
 *         is currently not possible because more buffers need to be
 *         queued first.
 *     @GST_FLOW_ERROR in case of an error.
 */
GstFlowReturn gst_imx_v4l2_object_dequeue_buffer(GstImxV4L2Object *imx_v4l2_object, GstBuffer **buffer);

/**
 * gst_imx_v4l2_object_unlock:
 * @imx_v4l2_object: @GstImxV4L2Object to unlock.
 *
 * Unlocks the object. "Unlocking" means that all processing is suspended.
 * Any currently blocking @gst_imx_v4l2_object_dequeue_buffer call is
 * unblocked and returns @GST_FLOW_FLUSHING . All attempts to queue or
 * dequeue after the object was unlocked also returns @GST_FLOW_FLUSHING.
 *
 * Unlocking is necessary when a pipeline is being flushed. @GstBaseSrc
 * and @GstBaseSink have matching "unlock" methods where this function
 * needs to be called. The unlocked state is undone by calling the
 * @gst_imx_v4l2_object_unlock_stop function.
 */
void gst_imx_v4l2_object_unlock(GstImxV4L2Object *imx_v4l2_object);

/**
 * gst_imx_v4l2_object_unlock_stop:
 * @imx_v4l2_object: @GstImxV4L2Object to re-lock.
 *
 * Re-locks the object. This ends the unlocked state @gst_imx_v4l2_object_unlock
 * started. Re-locking is necessary when a pipeline stops flushing.
 * @GstBaseSrc and @GstBaseSink have matching "unlock_stop" methods where
 * this function needs to be called.
 */
void gst_imx_v4l2_object_unlock_stop(GstImxV4L2Object *imx_v4l2_object);


G_END_DECLS


#endif /* GST_IMX_V4L2_OBJECT_H */
