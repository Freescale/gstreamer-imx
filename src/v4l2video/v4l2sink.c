/*
 * GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <config.h>
#include "v4l2sink.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

// Kernels older than 4.2 do not have V4L2 headers with this macro.
// (It was introduced in linux in commit e01dfc01914ab .)
#ifndef V4L2_COLORSPACE_DEFAULT
#define V4L2_COLORSPACE_DEFAULT 0
#endif

#include "../common/phys_mem_allocator.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

enum {
  PROP_0,
  PROP_DEVICE,
};

typedef struct {
  GstBuffer *buf;
  guint index;
} QueuedBuffer;

static void
queued_buffer_free (QueuedBuffer *buf)
{
  gst_buffer_unref (buf->buf);
  g_free (buf);
}

#define DEFAULT_DEVICE "/dev/video0"

GST_DEBUG_CATEGORY_STATIC (gst_imx_v4l2sink_debug_category);
#define GST_CAT_DEFAULT gst_imx_v4l2sink_debug_category

static void gst_imx_v4l2sink_uri_handler_init(gpointer g_iface, gpointer iface_data);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_imx_v4l2sink_debug_category, \
                           "imxv4l2videosink", 0, "V4L2 CSI video sink");

#define parent_class gst_imx_v4l2sink_parent_class
G_DEFINE_TYPE_WITH_CODE (GstImxV4l2VideoSink, gst_imx_v4l2sink, GST_TYPE_VIDEO_SINK,
                         DEBUG_INIT;
                         G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, gst_imx_v4l2sink_uri_handler_init));

static void gst_imx_v4l2sink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (v4l2sink->device);
      v4l2sink->device = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void gst_imx_v4l2sink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, v4l2sink->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_imx_v4l2sink_finalize (GObject *object)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (object);

  g_free (v4l2sink->device);
  v4l2sink->device = NULL;

  G_OBJECT_CLASS (gst_imx_v4l2sink_parent_class)->finalize (object);
}

static gboolean
gst_imx_v4l2sink_open (GstImxV4l2VideoSink *v4l2sink)
{
  int fd = -1;
  struct v4l2_capability cap = {0, };

  fd = open (v4l2sink->device, O_RDWR, 0);
  if (fd < 0) {
    GST_ERROR_OBJECT (v4l2sink, "Failed to open device '%s'", v4l2sink->device);
    goto error;
  }

  if (ioctl (fd, VIDIOC_QUERYCAP, &cap) < 0) {
    GST_ERROR_OBJECT (v4l2sink, "Failed to query device '%s' capabilities", v4l2sink->device);
    goto error;
  }

  if ((cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) == 0) {
    GST_ERROR_OBJECT (v4l2sink, "Device '%s' has no output capability", v4l2sink->device);
    goto error;
  }

  if ((cap.capabilities & V4L2_CAP_STREAMING) == 0) {
    GST_ERROR_OBJECT (v4l2sink, "Device '%s' has no streaming capability", v4l2sink->device);
    goto error;
  }

  // TODO: VIDIOC_ENUM_FMT for populating list of caps we can support
  // For now we just assume that all our formats are supported and fail
  // later when setting caps

  v4l2sink->fd_obj_v4l = gst_fd_object_new (fd);

  return TRUE;
error:
  {
    if (fd != -1)
      close (fd);

    return FALSE;
  }
}

static gboolean
gst_imx_v4l2sink_close (GstImxV4l2VideoSink *v4l2sink)
{
  if (v4l2sink->fd_obj_v4l)
    gst_imx_fd_object_unref (v4l2sink->fd_obj_v4l);
  v4l2sink->fd_obj_v4l = NULL;

  return TRUE;
}

static gboolean
gst_imx_v4l2sink_stop (GstImxV4l2VideoSink *v4l2sink)
{
  guint32 type = V4L2_MEMORY_USERPTR;
  struct v4l2_buffer v4l2buf = {0, };

  if (!v4l2sink->streamon)
    return TRUE;

  v4l2buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  v4l2buf.memory = V4L2_MEMORY_USERPTR;

  while (v4l2sink->queued > 0) {
    if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_DQBUF, &v4l2buf) < 0) {
      GST_ERROR_OBJECT (v4l2sink, "Failed to queue buffer");
      return FALSE;
    }
    GST_DEBUG_OBJECT (v4l2sink, "Dequeued buffer %u", v4l2buf.index);
    v4l2sink->queued--;
  }
  g_queue_foreach (&v4l2sink->last_buffers, (GFunc) queued_buffer_free, NULL);
  g_queue_clear (&v4l2sink->last_buffers);

  v4l2sink->current = 0;
  v4l2sink->allocated = 0;
  v4l2sink->queued = 0;

  if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_STREAMOFF, &type) < 0) {
    GST_ERROR_OBJECT (v4l2sink, "Failed to streamoff");
    return FALSE;
  }

  v4l2sink->streamon = FALSE;

  return TRUE;
}

static GstStateChangeReturn
gst_imx_v4l2sink_change_state (GstElement *element, GstStateChange transition)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_imx_v4l2sink_open (v4l2sink))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_imx_v4l2sink_stop (v4l2sink))
        return GST_STATE_CHANGE_FAILURE;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_imx_v4l2sink_close (v4l2sink))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_imx_v4l2sink_get_caps (GstBaseSink *bsink, GstCaps *filter)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (bsink);
  GstCaps *caps;

  GST_INFO_OBJECT (v4l2sink, "get caps filter %" GST_PTR_FORMAT, (gpointer) filter);

  // TODO: Return caps for the formats queried in open() here if we're open
  caps = gst_caps_from_string ("video/x-raw, "
    "format = (string) { BGRA, BGRx }, "
    "width = (gint) [ 16, MAX ], "
    "height = (gint) [ 16, MAX ], "
    "interlace-mode = (string) progressive, "
    "framerate = (fraction) [ 0/1, 100/1 ], "
    "pixel-aspect-ratio = (fraction) [ 0/1, 100/1 ];"
  );

  GST_INFO_OBJECT (v4l2sink, "get caps %" GST_PTR_FORMAT, (gpointer) caps);

  return caps;
}

static gboolean
gst_imx_v4l2sink_set_caps (GstBaseSink *bsink, GstCaps *caps)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (bsink);
  GstVideoInfo info;
  GstCaps *old_caps;
  struct v4l2_format fmt = {0, };
  struct v4l2_requestbuffers reqbufs = {0, };
  struct v4l2_crop crop = {0, };

  old_caps = gst_pad_get_current_caps (GST_BASE_SINK_PAD (bsink));
  if (old_caps && v4l2sink->streamon && !gst_caps_is_equal (caps, old_caps)) {
    guint32 type = V4L2_MEMORY_USERPTR;
    struct v4l2_buffer v4l2buf = {0, };

    v4l2buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    v4l2buf.memory = V4L2_MEMORY_USERPTR;

    while (v4l2sink->queued > 0) {
      if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_DQBUF, &v4l2buf) < 0) {
        GST_ERROR_OBJECT (v4l2sink, "Failed to queue buffer");
        return FALSE;
      }
      v4l2sink->queued--;
      GST_DEBUG_OBJECT (v4l2sink, "Dequeued buffer %u", v4l2buf.index);
    }
    g_queue_foreach (&v4l2sink->last_buffers, (GFunc) queued_buffer_free, NULL);
    g_queue_clear (&v4l2sink->last_buffers);

    v4l2sink->streamon = FALSE;
    v4l2sink->current = 0;
    v4l2sink->allocated = 0;
    v4l2sink->queued = 0;

    if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_STREAMOFF, &type) < 0) {
      GST_ERROR_OBJECT (v4l2sink, "Failed to streamoff");
      return FALSE;
    }
  }

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  // FIXME: This should only be 8... but
  fmt.fmt.pix.height = GST_ROUND_UP_16 (GST_VIDEO_INFO_HEIGHT (&info));
  fmt.fmt.pix.width = GST_ROUND_UP_16 (GST_VIDEO_INFO_WIDTH (&info));
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR32;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  fmt.fmt.pix.bytesperline = fmt.fmt.pix.width * 4;
  fmt.fmt.pix.sizeimage = fmt.fmt.pix.height * fmt.fmt.pix.bytesperline;
  fmt.fmt.pix.colorspace = V4L2_COLORSPACE_DEFAULT;

  if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_S_FMT, &fmt) < 0) {
    GST_ERROR_OBJECT (v4l2sink, "Failed to set video format");
    return FALSE;
  }

  v4l2sink->fmt = fmt;

  crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  crop.c.top = 0;
  crop.c.left = 0;
  crop.c.height = GST_VIDEO_INFO_HEIGHT (&info);
  crop.c.width = GST_VIDEO_INFO_WIDTH (&info);
  if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_S_CROP, &crop) < 0) {
    GST_ERROR_OBJECT (v4l2sink, "Failed to set cropping");
    return FALSE;
  }

  reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  reqbufs.memory = V4L2_MEMORY_USERPTR;
  reqbufs.count = 4;
  if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_REQBUFS, &reqbufs) < 0) {
    GST_ERROR_OBJECT (v4l2sink, "Failed to request buffers");
    return FALSE;
  }
  g_assert (reqbufs.count == 4);

  v4l2sink->current = 0;
  v4l2sink->allocated = 4;
  v4l2sink->queued = 0;

  return TRUE;
}

static gboolean
gst_imx_v4l2sink_unlock (GstBaseSink *bsink)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (bsink);

  return TRUE;
}

static gboolean
gst_imx_v4l2sink_unlock_stop (GstBaseSink *bsink)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (bsink);

  return TRUE;
}

static GstFlowReturn
gst_imx_v4l2sink_show_frame (GstVideoSink *vsink, GstBuffer *buf)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (vsink);
  struct v4l2_buffer v4l2buf = {0, };
  GstMemory *mem;
  QueuedBuffer *qbuf;

  if (gst_buffer_n_memory (buf) != 1) {
    GST_ERROR_OBJECT (v4l2sink, "Support only 1 memory per buffer");
    return GST_FLOW_ERROR;
  }

  mem = gst_buffer_get_memory (buf, 0);
  if (!gst_imx_is_phys_memory (mem)) {
    GST_ERROR_OBJECT (v4l2sink, "Support only physmem");
    return GST_FLOW_ERROR;
  }

  v4l2buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  v4l2buf.memory = V4L2_MEMORY_USERPTR;
  v4l2buf.m.userptr = gst_imx_phys_memory_get_phys_addr (mem);
  v4l2buf.length = v4l2sink->fmt.fmt.pix.sizeimage;
  v4l2buf.index = v4l2sink->current;

  qbuf = g_new0 (QueuedBuffer, 1);
  qbuf->buf = gst_buffer_ref (buf);
  qbuf->index = v4l2buf.index;
  g_queue_push_tail (&v4l2sink->last_buffers, qbuf);

  v4l2sink->current = (v4l2sink->current + 1) % v4l2sink->allocated;

  if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_QBUF, &v4l2buf) < 0) {
    GST_ERROR_OBJECT (v4l2sink, "Failed to queue buffer");
    return GST_FLOW_ERROR;
  }
  v4l2sink->queued++;

  GST_DEBUG_OBJECT (v4l2sink, "Queued buffer %u", v4l2buf.index);

  if (!v4l2sink->streamon) {
    guint32 type = V4L2_MEMORY_USERPTR;

    v4l2sink->streamon = TRUE;
    if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_STREAMON, &type) < 0) {
      GST_ERROR_OBJECT (v4l2sink, "Failed to streamon");
      return GST_FLOW_ERROR;
    }
  }

  if (v4l2sink->queued >= 2) {
    GList *l;

    if (ioctl (GST_IMX_FD_OBJECT_GET_FD (v4l2sink->fd_obj_v4l), VIDIOC_DQBUF, &v4l2buf) < 0) {
      GST_ERROR_OBJECT (v4l2sink, "Failed to dequeue buffer");
      return GST_FLOW_ERROR;
    }
    v4l2sink->queued--;
    GST_DEBUG_OBJECT (v4l2sink, "Dequeued buffer %u", v4l2buf.index);

    for (l = v4l2sink->last_buffers.head; l; l = l->next) {
      qbuf = l->data;

      if (qbuf->index == v4l2buf.index) {
        queued_buffer_free (qbuf);
        g_queue_delete_link (&v4l2sink->last_buffers, l);
        break;
      }
    }
  }

  return GST_FLOW_OK;
}

static void
gst_imx_v4l2sink_class_init (GstImxV4l2VideoSinkClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *videosink_class = GST_VIDEO_SINK_CLASS (klass);

  gobject_class->finalize = gst_imx_v4l2sink_finalize;
  gobject_class->set_property = gst_imx_v4l2sink_set_property;
  gobject_class->get_property = gst_imx_v4l2sink_get_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
                                   g_param_spec_string ("device", "Device", "Device location",
                                                        DEFAULT_DEVICE,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata(element_class,
                                        "V4L2 CSI Video Sink",
                                        "Sink/Video",
                                        "Display video streams using V4L2 CSI interface",
                                        "Sebastian Dröge <sebastian@centricular.com>");

  element_class->change_state = gst_imx_v4l2sink_change_state;

  basesink_class->get_caps = gst_imx_v4l2sink_get_caps;
  basesink_class->set_caps = gst_imx_v4l2sink_set_caps;
  basesink_class->unlock = gst_imx_v4l2sink_unlock;
  basesink_class->unlock_stop = gst_imx_v4l2sink_unlock_stop;

  videosink_class->show_frame = gst_imx_v4l2sink_show_frame;
}

static void
gst_imx_v4l2sink_init (GstImxV4l2VideoSink *v4l2sink)
{
  g_queue_init (&v4l2sink->last_buffers);
}

static GstURIType
gst_imx_v4l2sink_uri_get_type (GType type)
{
  return GST_URI_SINK;
}

static const gchar *const *
gst_imx_v4l2sink_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "imxv4l2", NULL };

  return protocols;
}

static gchar *
gst_imx_v4l2sink_uri_get_uri (GstURIHandler * handler)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (handler);

  if (v4l2sink->device)
    return g_strdup_printf ("imxv4l2://%s", v4l2sink->device);

  return g_strdup ("imxv4l2://");
}

static gboolean
gst_imx_v4l2sink_uri_set_uri (GstURIHandler * handler, const gchar * uri, GError ** error)
{
  GstImxV4l2VideoSink *v4l2sink = GST_IMX_V4L2SINK (handler);

  if (!g_str_has_prefix (uri, "imxv4l2://")) {
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, "Invalid URI scheme");
    return FALSE;
  }

  g_object_set (v4l2sink, "device", uri + 10, NULL);

  return TRUE;
}

static void
gst_imx_v4l2sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_imx_v4l2sink_uri_get_type;
  iface->get_protocols = gst_imx_v4l2sink_uri_get_protocols;
  iface->get_uri = gst_imx_v4l2sink_uri_get_uri;
  iface->set_uri = gst_imx_v4l2sink_uri_set_uri;
}
