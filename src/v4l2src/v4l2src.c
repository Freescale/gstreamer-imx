/*
 * Copyright (c) 2013-2014, Black Moth Technologies
 *   Author: Philip Craig <phil@blackmoth.com.au>
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

#include <config.h>

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/interfaces/photography.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/videodev2.h>
#include "v4l2src.h"
#include "v4l2_buffer_pool.h"

#define DEFAULT_CAPTURE_MODE 0
#define DEFAULT_FRAMERATE_NUM 30
#define DEFAULT_FRAMERATE_DEN 1
#define DEFAULT_INPUT 1
#define DEFAULT_DEVICE "/dev/video0"
#define DEFAULT_QUEUE_SIZE 6
#define DEFAULT_CROP_META_X 0
#define DEFAULT_CROP_META_Y 0
#define DEFAULT_CROP_META_WIDTH 0
#define DEFAULT_CROP_META_HEIGHT 0

enum
{
	IMX_V4L2SRC_0,
	IMX_V4L2SRC_CAPTURE_MODE,
	IMX_V4L2SRC_FRAMERATE_NUM,
	IMX_V4L2SRC_INPUT,
	IMX_V4L2SRC_DEVICE,
	IMX_V4L2SRC_QUEUE_SIZE,
	IMX_V4L2SRC_CROP_META_X,
	IMX_V4L2SRC_CROP_META_Y,
	IMX_V4L2SRC_CROP_META_WIDTH,
	IMX_V4L2SRC_CROP_META_HEIGHT,

	/* Properties required to be recongnized by GstPhotography implementor */
	PROP_WB_MODE,
	PROP_COLOR_TONE,
	PROP_SCENE_MODE,
	PROP_FLASH_MODE,
	PROP_FLICKER_MODE,
	PROP_FOCUS_MODE,
	PROP_CAPABILITIES,
	PROP_EV_COMP,
	PROP_ISO_SPEED,
	PROP_APERTURE,
	PROP_EXPOSURE_TIME,
	PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
	PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
	PROP_ZOOM,
	PROP_COLOR_TEMPERATURE,
	PROP_WHITE_POINT,
	PROP_ANALOG_GAIN,
	PROP_LENS_FOCUS,
	PROP_MIN_EXPOSURE_TIME,
	PROP_MAX_EXPOSURE_TIME,
	PROP_NOISE_REDUCTION,
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw"
	)
);

GST_DEBUG_CATEGORY_STATIC(gst_imx_v4l2src_debug_category);
#define GST_CAT_DEFAULT gst_imx_v4l2src_debug_category

#define DEBUG_INIT \
	GST_DEBUG_CATEGORY_INIT(gst_imx_v4l2src_debug_category, \
			"imxv4l2videosrc", 0, "V4L2 CSI video source");

static void gst_imx_v4l2src_uri_handler_init(gpointer g_iface,
	gpointer iface_data);
static void gst_imx_v4l2src_photography_init(gpointer g_iface,
	gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(GstImxV4l2VideoSrc, gst_imx_v4l2src, GST_TYPE_PUSH_SRC,
	G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, gst_imx_v4l2src_uri_handler_init);
	G_IMPLEMENT_INTERFACE(GST_TYPE_PHOTOGRAPHY, gst_imx_v4l2src_photography_init);
	DEBUG_INIT)

static void gst_imx_v4l2src_apply_focus_settings(GstImxV4l2VideoSrc *v4l2src,
		gboolean activate);
static gboolean gst_imx_v4l2src_set_focus_mode(GstPhotography *photo,
		GstPhotographyFocusMode focus_mode);
static gboolean gst_imx_v4l2src_get_focus_mode(GstPhotography *photo,
		GstPhotographyFocusMode *focus_mode);

static gboolean gst_imx_v4l2src_is_tvin(GstImxV4l2VideoSrc *v4l2src, gint fd_v4l)
{
	v4l2_std_id std_id = V4L2_STD_UNKNOWN;
	gint count = 10;

	if (ioctl(fd_v4l, VIDIOC_QUERYSTD, &std_id) < 0)
		GST_WARNING_OBJECT(v4l2src, "VIDIOC_QUERYSTD failed: %s", strerror(errno));

	if (ioctl(fd_v4l, VIDIOC_G_STD, &std_id) < 0)
	{
		GST_WARNING_OBJECT(v4l2src, "VIDIOC_G_STD failed: %s", strerror(errno));
		return FALSE;
	}

	while (std_id == V4L2_STD_ALL && --count >= 0)
	{
		g_usleep(G_USEC_PER_SEC / 10);
		if (ioctl(fd_v4l, VIDIOC_G_STD, &std_id) < 0)
			break;
	}

	if (ioctl(fd_v4l, VIDIOC_S_STD, &std_id) < 0)
		GST_WARNING_OBJECT(v4l2src, "VIDIOC_S_STD failed: %s", strerror(errno));

	if (std_id == V4L2_STD_UNKNOWN)
		return FALSE;

	if (std_id & V4L2_STD_525_60)
		v4l2src->fps_n = (!v4l2src->fps_n || v4l2src->fps_n > 30) ? 30 : v4l2src->fps_n;
	else
		v4l2src->fps_n = (!v4l2src->fps_n || v4l2src->fps_n > 25) ? 25 : v4l2src->fps_n;

	GST_DEBUG_OBJECT(v4l2src,
		"found TV decoder: adjusted fps = %d/%d, std_id = %#" G_GINT64_MODIFIER "x",
		v4l2src->fps_n, v4l2src->fps_d, std_id);

	return TRUE;
}

static gint gst_imx_v4l2src_capture_setup(GstImxV4l2VideoSrc *v4l2src)
{
	struct v4l2_format fmt = {0};
	struct v4l2_fmtdesc fmtdesc = {0};
	struct v4l2_streamparm parm = {0};
	struct v4l2_frmsizeenum fszenum = {0};
	struct v4l2_capability cap;
	guint32 pixelformat;
	gint input;
	gint fd_v4l;

	fd_v4l = open(v4l2src->devicename, O_RDWR, 0);
	if (fd_v4l < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "Unable to open %s", v4l2src->devicename);
		return -1;
	}

	if (ioctl (fd_v4l, VIDIOC_QUERYCAP, &cap) < 0) {
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_QUERYCAP failed: %s", strerror(errno));
		goto fail;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		GST_ERROR_OBJECT(v4l2src, "%s is no video capture device", v4l2src->devicename);
		goto fail;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		GST_ERROR_OBJECT(v4l2src, "%s does not support streaming i/o", v4l2src->devicename);
		goto fail;
	}

	v4l2src->is_tvin = gst_imx_v4l2src_is_tvin(v4l2src, fd_v4l);

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd_v4l, VIDIOC_G_FMT, &fmt) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_G_FMT failed: %s", strerror(errno));
		goto fail;
	}

	if (!fmt.fmt.pix.pixelformat)
	{
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmtdesc.index = 0;
		if (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &fmtdesc) < 0)
		{
			GST_ERROR_OBJECT(v4l2src, "VIDIOC_ENUM_FMT failed: %s", strerror(errno));
			goto fail;
		}

		fmt.fmt.pix.pixelformat = fmtdesc.pixelformat;
	}

	GST_DEBUG_OBJECT(v4l2src, "pixelformat = %d  field = %d",
		fmt.fmt.pix.pixelformat, fmt.fmt.pix.field);

	fszenum.index = v4l2src->capture_mode;
	fszenum.pixel_format = fmt.fmt.pix.pixelformat;
	if (ioctl(fd_v4l, VIDIOC_ENUM_FRAMESIZES, &fszenum) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_ENUM_FRAMESIZES failed: %s", strerror(errno));
		goto fail;
	}

	v4l2src->capture_width = fszenum.discrete.width;
	v4l2src->capture_height = fszenum.discrete.height;
	GST_INFO_OBJECT(v4l2src, "capture mode %d: %dx%d",
			v4l2src->capture_mode,
			v4l2src->capture_width, v4l2src->capture_height);

	input = v4l2src->input;
	if (ioctl(fd_v4l, VIDIOC_S_INPUT, &input) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_S_INPUT failed: %s", strerror(errno));
		goto fail;
	}

	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = v4l2src->fps_d;
	parm.parm.capture.timeperframe.denominator = v4l2src->fps_n;
	parm.parm.capture.capturemode = v4l2src->capture_mode;
	if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_S_PARM failed: %s", strerror(errno));
		goto fail;
	}

	/* Get the actual frame period if possible */
	if (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)
	{
		v4l2src->fps_n = parm.parm.capture.timeperframe.denominator;
		v4l2src->fps_d = parm.parm.capture.timeperframe.numerator;

		GST_DEBUG_OBJECT(v4l2src, "V4L2_CAP_TIMEPERFRAME capability present: fps = %d/%d",
			v4l2src->fps_n, v4l2src->fps_d);
	}

	/* Determine the desired input pixelformat (UYVY or I420)
	 * by looking at the allowed srccaps */
	{
		GstCaps *allowed_src_caps, *available_format_caps, *allowed_format_caps;

		pixelformat = V4L2_PIX_FMT_YUV420;

		available_format_caps = gst_caps_from_string("video/x-raw, format = { UYVY, I420 }");
		allowed_src_caps = gst_pad_get_allowed_caps(GST_BASE_SRC_PAD(v4l2src));

		/* Apply intersection to get caps with a valid pixelformat */
		allowed_format_caps = gst_caps_intersect(allowed_src_caps, available_format_caps);
		GST_DEBUG_OBJECT(v4l2src, "allowed src caps: %" GST_PTR_FORMAT " -> allowed formats: %" GST_PTR_FORMAT, (gpointer)allowed_src_caps, (gpointer)allowed_format_caps);

		gst_caps_unref(allowed_src_caps);
		gst_caps_unref(available_format_caps);

		if ((allowed_format_caps != NULL) && !gst_caps_is_empty(allowed_format_caps) && (gst_caps_get_size(allowed_format_caps) > 0))
		{
			GstStructure const *structure;
			gchar const *format_str;

			allowed_format_caps = gst_caps_fixate(allowed_format_caps);
			if (allowed_format_caps != NULL)
			{
				structure = gst_caps_get_structure(allowed_format_caps, 0);
				if ((structure != NULL) && gst_structure_has_field_typed(structure, "format", G_TYPE_STRING) && (format_str = gst_structure_get_string(structure, "format")))
				{
					if (g_strcmp0(format_str, "UYVY") == 0)
						pixelformat = V4L2_PIX_FMT_UYVY;
					else if (g_strcmp0(format_str, "I420") == 0)
						pixelformat = V4L2_PIX_FMT_YUV420;
					else
					{
						GST_ERROR_OBJECT(v4l2src, "pixel format \"%s\" is unsupported", format_str);
						goto fail;
					}

					GST_DEBUG_OBJECT(v4l2src, "using \"%s\" pixel format", format_str);
				}
			}
		}

		if (allowed_format_caps != NULL)
			gst_caps_unref(allowed_format_caps);
	}

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.bytesperline = 0;
	fmt.fmt.pix.priv = 0;
	fmt.fmt.pix.sizeimage = 0;
	fmt.fmt.pix.width = v4l2src->capture_width;
	fmt.fmt.pix.height = v4l2src->capture_height;
	fmt.fmt.pix.pixelformat = pixelformat;
	if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_S_FMT failed: %s", strerror(errno));
		goto fail;
	}

	return fd_v4l;

fail:
	close(fd_v4l);
	return -1;
}

static gboolean gst_imx_v4l2src_start(GstBaseSrc *src)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(src);
	struct v4l2_format fmt;
	int fd_v4l;

	GST_LOG_OBJECT(v4l2src, "start");

	fd_v4l = gst_imx_v4l2src_capture_setup(v4l2src);
	if (fd_v4l < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "capture_setup failed");
		return FALSE;
	}

	v4l2src->fd_obj_v4l = gst_fd_object_new(fd_v4l);

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(v4l2src->fd_obj_v4l), VIDIOC_G_FMT, &fmt) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_G_FMT failed: %s", strerror(errno));
		return FALSE;
	}

	GST_DEBUG_OBJECT(v4l2src, "width = %d", fmt.fmt.pix.width);
	GST_DEBUG_OBJECT(v4l2src, "height = %d", fmt.fmt.pix.height);
	GST_DEBUG_OBJECT(v4l2src, "sizeimage = %d", fmt.fmt.pix.sizeimage);
	GST_DEBUG_OBJECT(v4l2src, "pixelformat = %d", fmt.fmt.pix.pixelformat);

	/* Explanation for this line:
	 * fps = fps_n/fps_d
	 * time_per_frame = 1s / fps
	 * -> time_per_frame = 1s / (fps_n/fps_d) = 1s * fps_d / fps_n
	 */
	v4l2src->time_per_frame = gst_util_uint64_scale_int(GST_SECOND, v4l2src->fps_d, v4l2src->fps_n);
	v4l2src->count = 0;

	g_mutex_lock(&v4l2src->af_mutex);
	gst_imx_v4l2src_apply_focus_settings(v4l2src, TRUE);
	g_mutex_unlock(&v4l2src->af_mutex);

	return TRUE;
}

static gboolean gst_imx_v4l2src_stop(GstBaseSrc *src)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(src);

	GST_LOG_OBJECT(v4l2src, "stop");

	g_mutex_lock(&v4l2src->af_mutex);
	gst_imx_v4l2src_apply_focus_settings(v4l2src, FALSE);
	g_mutex_unlock(&v4l2src->af_mutex);

	gst_imx_fd_object_unref(v4l2src->fd_obj_v4l);

	return TRUE;
}

static gboolean gst_imx_v4l2src_decide_allocation(GstBaseSrc *bsrc,
		GstQuery *query)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(bsrc);
	struct v4l2_format fmt;
	GstBufferPool *pool;
	guint size, min, max;
	gboolean update;
	GstStructure *config;
	GstCaps *caps;

	gst_query_parse_allocation(query, &caps, NULL);

	/* Determine min and max */
	if (gst_query_get_n_allocation_pools(query) > 0)
	{
		gst_query_parse_nth_allocation_pool(query, 0, NULL, NULL,
				&min, &max);
		update = TRUE;
	}
	else
	{
		min = max = 0;
		update = FALSE;
	}

	if (min != 0)
		/* Need an extra buffer to capture while other buffers
		 * are downstream */
		min += 1;
	else
		min = v4l2src->queue_size;

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(v4l2src->fd_obj_v4l), VIDIOC_G_FMT, &fmt) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_G_FMT failed: %s", strerror(errno));
		return FALSE;
	}

	size = fmt.fmt.pix.sizeimage;

	/* no repooling; leads to stream off situation due to pool start/stop */
	pool = gst_base_src_get_buffer_pool(bsrc);
	if (!pool)
	{
		pool = gst_imx_v4l2_buffer_pool_new(v4l2src->fd_obj_v4l, v4l2src->metaCropX,
						    v4l2src->metaCropY, v4l2src->metaCropWidth,
						    v4l2src->metaCropHeight);
		config = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_set_params(config, caps, size, min, max);
		gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
		gst_buffer_pool_set_config(pool, config);
	}

	if (update)
		gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
	else
		gst_query_add_allocation_pool(query, pool, size, min, max);

	gst_object_unref(pool);

	return TRUE;
}

static GstFlowReturn gst_imx_v4l2src_fill(GstPushSrc *src, GstBuffer *buf)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(src);
	GstClockTime ts;

	GST_LOG_OBJECT(v4l2src, "fill");

	ts = gst_clock_get_time(GST_ELEMENT(v4l2src)->clock);
	if (ts != GST_CLOCK_TIME_NONE)
		ts -= gst_element_get_base_time(GST_ELEMENT(v4l2src));
	else
		ts = v4l2src->count * v4l2src->time_per_frame;
	v4l2src->count++;

	GST_BUFFER_TIMESTAMP(buf) = ts;
	GST_BUFFER_DURATION(buf) = v4l2src->time_per_frame;
	return GST_FLOW_OK;
}

static GstCaps *gst_imx_v4l2src_caps_for_current_setup(GstImxV4l2VideoSrc *v4l2src)
{
	GstVideoFormat gst_fmt;
	const gchar *pixel_format = NULL;
	const gchar *interlace_mode = NULL;
	struct v4l2_format fmt;

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(v4l2src->fd_obj_v4l), VIDIOC_G_FMT, &fmt) < 0)
	{
		GST_ERROR_OBJECT(v4l2src, "VIDIOC_G_FMT failed: %s", strerror(errno));
		return NULL;
	}

	/* switch/case table taken from gst-plugins-good/sys/v4l2/gstv4l2object.c */
	switch (fmt.fmt.pix.pixelformat)
	{
		case V4L2_PIX_FMT_GREY:
			gst_fmt = GST_VIDEO_FORMAT_GRAY8;
			break;
		case V4L2_PIX_FMT_Y16:
			gst_fmt = GST_VIDEO_FORMAT_GRAY16_LE;
			break;
#ifdef V4L2_PIX_FMT_Y16_BE
		case V4L2_PIX_FMT_Y16_BE:
			gst_fmt = GST_VIDEO_FORMAT_GRAY16_BE;
			break;
#endif
#ifdef V4L2_PIX_FMT_XRGB555
		case V4L2_PIX_FMT_XRGB555:
#endif
		case V4L2_PIX_FMT_RGB555:
			gst_fmt = GST_VIDEO_FORMAT_RGB15;
			break;
#ifdef V4L2_PIX_FMT_XRGB555X
		case V4L2_PIX_FMT_XRGB555X:
#endif
		case V4L2_PIX_FMT_RGB555X:
			gst_fmt = GST_VIDEO_FORMAT_BGR15;
			break;
		case V4L2_PIX_FMT_RGB565:
			gst_fmt = GST_VIDEO_FORMAT_RGB16;
			break;
		case V4L2_PIX_FMT_RGB24:
			gst_fmt = GST_VIDEO_FORMAT_RGB;
			break;
		case V4L2_PIX_FMT_BGR24:
			gst_fmt = GST_VIDEO_FORMAT_BGR;
			break;
#ifdef V4L2_PIX_FMT_XRGB32
		case V4L2_PIX_FMT_XRGB32:
#endif
		case V4L2_PIX_FMT_RGB32:
			gst_fmt = GST_VIDEO_FORMAT_xRGB;
			break;
#ifdef V4L2_PIX_FMT_XBGR32
		case V4L2_PIX_FMT_XBGR32:
#endif
		case V4L2_PIX_FMT_BGR32:
			gst_fmt = GST_VIDEO_FORMAT_BGRx;
			break;
#ifdef V4L2_PIX_FMT_ABGR32
		case V4L2_PIX_FMT_ABGR32:
			gst_fmt = GST_VIDEO_FORMAT_BGRA;
			break;
#endif
#ifdef V4L2_PIX_FMT_ARGB32
		case V4L2_PIX_FMT_ARGB32:
			gst_fmt = GST_VIDEO_FORMAT_ARGB;
			break;
#endif
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV12M:
			gst_fmt = GST_VIDEO_FORMAT_NV12;
			break;
		case V4L2_PIX_FMT_NV12MT:
			gst_fmt = GST_VIDEO_FORMAT_NV12_64Z32;
			break;
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV21M:
			gst_fmt = GST_VIDEO_FORMAT_NV21;
			break;
		case V4L2_PIX_FMT_YVU410:
			gst_fmt = GST_VIDEO_FORMAT_YVU9;
			break;
		case V4L2_PIX_FMT_YUV410:
			gst_fmt = GST_VIDEO_FORMAT_YUV9;
			break;
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YUV420M:
			gst_fmt = GST_VIDEO_FORMAT_I420;
			break;
		case V4L2_PIX_FMT_YUYV:
			gst_fmt = GST_VIDEO_FORMAT_YUY2;
			break;
		case V4L2_PIX_FMT_YVU420:
			gst_fmt = GST_VIDEO_FORMAT_YV12;
			break;
		case V4L2_PIX_FMT_UYVY:
			gst_fmt = GST_VIDEO_FORMAT_UYVY;
			break;
		case V4L2_PIX_FMT_YUV411P:
			gst_fmt = GST_VIDEO_FORMAT_Y41B;
			break;
		case V4L2_PIX_FMT_YUV422P:
			gst_fmt = GST_VIDEO_FORMAT_Y42B;
			break;
		case V4L2_PIX_FMT_YVYU:
			gst_fmt = GST_VIDEO_FORMAT_YVYU;
			break;
		case V4L2_PIX_FMT_NV16:
#ifdef V4L2_PIX_FMT_NV16M
		case V4L2_PIX_FMT_NV16M:
#endif
			gst_fmt = GST_VIDEO_FORMAT_NV16;
			break;
#ifdef GST_VIDEO_FORMAT_NV61
		case V4L2_PIX_FMT_NV61:
#ifdef V4L2_PIX_FMT_NV61M
		case V4L2_PIX_FMT_NV61M:
#endif
			gst_fmt = GST_VIDEO_FORMAT_NV61;
			break;
#endif
		case V4L2_PIX_FMT_NV24:
			gst_fmt = GST_VIDEO_FORMAT_NV24;
			break;
		default:
			gst_fmt = gst_video_format_from_fourcc(fmt.fmt.pix.pixelformat);
	}

	pixel_format = gst_video_format_to_string(gst_fmt);

	if (v4l2src->is_tvin && !fmt.fmt.pix.field)
	{
		fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

		GST_DEBUG_OBJECT(v4l2src, "TV decoder fix up: field = V4L2_FIELD_INTERLACED");
	}

	switch (fmt.fmt.pix.field)
	{
		case V4L2_FIELD_INTERLACED:
		case V4L2_FIELD_INTERLACED_TB:
		case V4L2_FIELD_INTERLACED_BT:
			interlace_mode = "interleaved";
			break;
		default:
			interlace_mode = "progressive";
	}

	return gst_caps_new_simple("video/x-raw",
			"format", G_TYPE_STRING, pixel_format,
			"width", G_TYPE_INT, v4l2src->capture_width,
			"height", G_TYPE_INT, v4l2src->capture_height,
			"interlace-mode", G_TYPE_STRING, interlace_mode,
			"framerate", GST_TYPE_FRACTION, v4l2src->fps_n, v4l2src->fps_d,
			"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
			NULL);
}

static gboolean gst_imx_v4l2src_negotiate(GstBaseSrc *src)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(src);
	GstCaps *caps;

	/* not much to negotiate;
	 * we already performed setup, so that is what will be streamed */
	caps = gst_imx_v4l2src_caps_for_current_setup(v4l2src);
	if (!caps)
		return FALSE;

	GST_INFO_OBJECT(src, "negotiated caps %" GST_PTR_FORMAT, (gpointer)caps);

	return gst_base_src_set_caps(src, caps);
}

static GstCaps *gst_imx_v4l2src_get_caps(GstBaseSrc *src, GstCaps *filter)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(src);
	GstCaps *caps;

	GST_INFO_OBJECT(v4l2src, "get caps filter %" GST_PTR_FORMAT, (gpointer)filter);

	caps = gst_caps_from_string(
		"video/x-raw"
		", format = (string) { UYVY, I420 }"
		", width = (gint) [ 16, MAX ]"
		", height = (gint) [ 16, MAX ]"
		", interlace-mode = (string) { progressive, interleaved }"
		", framerate = (fraction) [ 0/1, 100/1 ]"
		", pixel-aspect-ratio = (fraction) [ 0/1, 100/1 ]"
		";"
	);

	GST_INFO_OBJECT(v4l2src, "get caps %" GST_PTR_FORMAT, (gpointer)caps);

	return caps;
}

static gboolean gst_imx_v4l2src_set_caps(GstBaseSrc *src, GstCaps *caps)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(src);

	GST_INFO_OBJECT(v4l2src, "set caps %" GST_PTR_FORMAT, (gpointer)caps);

	return TRUE;
}

static void gst_imx_v4l2src_set_property(GObject *object, guint prop_id,
		const GValue *value, GParamSpec *pspec)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(object);

	switch (prop_id)
	{
		case IMX_V4L2SRC_CAPTURE_MODE:
			v4l2src->capture_mode = g_value_get_int(value);
			break;

		case IMX_V4L2SRC_FRAMERATE_NUM:
			v4l2src->fps_n = g_value_get_int(value);
			break;

		case IMX_V4L2SRC_INPUT:
			v4l2src->input = g_value_get_int(value);
			break;

		case IMX_V4L2SRC_DEVICE:
			if (v4l2src->devicename)
				g_free(v4l2src->devicename);
			v4l2src->devicename = g_strdup(g_value_get_string(value));
			break;

		case IMX_V4L2SRC_QUEUE_SIZE:
			v4l2src->queue_size = g_value_get_int(value);
			break;

		case IMX_V4L2SRC_CROP_META_X:
			v4l2src->metaCropX = g_value_get_int(value);
			break;

		case IMX_V4L2SRC_CROP_META_Y:
			v4l2src->metaCropY = g_value_get_int(value);
			break;

		case IMX_V4L2SRC_CROP_META_WIDTH:
			v4l2src->metaCropWidth = g_value_get_int(value);
			break;

		case IMX_V4L2SRC_CROP_META_HEIGHT:
			v4l2src->metaCropHeight = g_value_get_int(value);
			break;

		case PROP_FOCUS_MODE:
			gst_imx_v4l2src_set_focus_mode(GST_PHOTOGRAPHY(v4l2src), g_value_get_enum(value));
			break;

		case PROP_WB_MODE:
		case PROP_COLOR_TONE:
		case PROP_SCENE_MODE:
		case PROP_FLASH_MODE:
		case PROP_FLICKER_MODE:
		case PROP_CAPABILITIES:
		case PROP_EV_COMP:
		case PROP_ISO_SPEED:
		case PROP_APERTURE:
		case PROP_EXPOSURE_TIME:
		case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
		case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
		case PROP_ZOOM:
		case PROP_COLOR_TEMPERATURE:
		case PROP_WHITE_POINT:
		case PROP_ANALOG_GAIN:
		case PROP_LENS_FOCUS:
		case PROP_MIN_EXPOSURE_TIME:
		case PROP_MAX_EXPOSURE_TIME:
		case PROP_NOISE_REDUCTION:
			GST_WARNING_OBJECT(v4l2src, "setting GstPhotography properties is not supported");
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void gst_imx_v4l2src_get_property(GObject *object, guint prop_id,
		GValue *value, GParamSpec *pspec)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(object);

	switch (prop_id)
	{
		case IMX_V4L2SRC_CAPTURE_MODE:
			g_value_set_int(value, v4l2src->capture_mode);
			break;

		case IMX_V4L2SRC_FRAMERATE_NUM:
			g_value_set_int(value, v4l2src->fps_n);
			break;

		case IMX_V4L2SRC_INPUT:
			g_value_set_int(value, v4l2src->input);
			break;

		case IMX_V4L2SRC_DEVICE:
			g_value_set_string(value, v4l2src->devicename);
			break;

		case IMX_V4L2SRC_QUEUE_SIZE:
			g_value_set_int(value, v4l2src->queue_size);
			break;

		case IMX_V4L2SRC_CROP_META_X:
			g_value_set_int(value, v4l2src->metaCropX);
			break;

		case IMX_V4L2SRC_CROP_META_Y:
			g_value_set_int(value, v4l2src->metaCropY);
			break;

		case IMX_V4L2SRC_CROP_META_WIDTH:
			g_value_set_int(value, v4l2src->metaCropWidth);
			break;

		case IMX_V4L2SRC_CROP_META_HEIGHT:
			g_value_set_int(value, v4l2src->metaCropHeight);
			break;

		case PROP_FOCUS_MODE:
			{
				GstPhotographyFocusMode focus_mode;
				gst_imx_v4l2src_get_focus_mode(GST_PHOTOGRAPHY(v4l2src), &focus_mode);
				g_value_set_enum(value, focus_mode);
			}
			break;

		case PROP_WB_MODE:
			g_value_set_enum(value, GST_PHOTOGRAPHY_WB_MODE_AUTO);
			break;

		case PROP_COLOR_TONE:
			g_value_set_enum(value, GST_PHOTOGRAPHY_COLOR_TONE_MODE_NORMAL);
			break;

		case PROP_SCENE_MODE:
			g_value_set_enum(value, GST_TYPE_PHOTOGRAPHY_SCENE_MODE);
			break;

		case PROP_FLASH_MODE:
			g_value_set_enum(value, GST_PHOTOGRAPHY_FLASH_MODE_AUTO);
			break;

		case PROP_FLICKER_MODE:
			g_value_set_enum(value, GST_PHOTOGRAPHY_FLICKER_REDUCTION_OFF);
			break;

		case PROP_CAPABILITIES:
			g_value_set_ulong(value, GST_PHOTOGRAPHY_CAPS_NONE);
			break;

		case PROP_EV_COMP:
			g_value_set_float(value, 0.0f);
			break;

		case PROP_ISO_SPEED:
			g_value_set_uint(value, 0);
			break;

		case PROP_APERTURE:
			g_value_set_uint(value, 0);
			break;

		case PROP_EXPOSURE_TIME:
			g_value_set_uint(value, 0);
			break;

		case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
		case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
			if (v4l2src->fd_obj_v4l)
				gst_value_set_caps(value, gst_imx_v4l2src_caps_for_current_setup(v4l2src));
			else
				GST_DEBUG_OBJECT(v4l2src, "not connected to hardware, don't know supported caps");
			break;

		case PROP_ZOOM:
			g_value_set_float(value, 1.0f);
			break;

		case PROP_COLOR_TEMPERATURE:
			g_value_set_uint(value, 0);
			break;

		case PROP_WHITE_POINT:
			g_value_set_boxed(value, NULL);
			break;

		case PROP_ANALOG_GAIN:
			g_value_set_float(value, 1.0f);
			break;

		case PROP_LENS_FOCUS:
			g_value_set_float(value, 0.0f);
			break;

		case PROP_MIN_EXPOSURE_TIME:
			g_value_set_uint(value, 0);
			break;

		case PROP_MAX_EXPOSURE_TIME:
			g_value_set_uint(value, 0);
			break;

		case PROP_NOISE_REDUCTION:
			g_value_set_flags(value, 0);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void gst_imx_v4l2src_init(GstImxV4l2VideoSrc *v4l2src)
{
	v4l2src->capture_mode = DEFAULT_CAPTURE_MODE;
	v4l2src->fps_n = DEFAULT_FRAMERATE_NUM;
	v4l2src->fps_d = DEFAULT_FRAMERATE_DEN;
	v4l2src->input = DEFAULT_INPUT;
	v4l2src->devicename = g_strdup(DEFAULT_DEVICE);
	v4l2src->queue_size = DEFAULT_QUEUE_SIZE;
	v4l2src->fd_obj_v4l = NULL;
	v4l2src->metaCropX = DEFAULT_CROP_META_X;
	v4l2src->metaCropY = DEFAULT_CROP_META_Y;
	v4l2src->metaCropWidth = DEFAULT_CROP_META_WIDTH;
	v4l2src->metaCropHeight = DEFAULT_CROP_META_HEIGHT;

	g_mutex_init(&v4l2src->af_mutex);
	v4l2src->focus_mode = GST_PHOTOGRAPHY_FOCUS_MODE_AUTO;
	v4l2src->af_clock_id = NULL;

	gst_base_src_set_format(GST_BASE_SRC(v4l2src), GST_FORMAT_TIME);
	gst_base_src_set_live(GST_BASE_SRC(v4l2src), TRUE);
}

static void gst_imx_v4l2src_finalize(GObject *object)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(object);

	g_free(v4l2src->devicename);
	g_mutex_clear(&v4l2src->af_mutex);

	G_OBJECT_CLASS(gst_imx_v4l2src_parent_class)->finalize(object);
}

static void gst_imx_v4l2src_class_init(GstImxV4l2VideoSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *element_class;
	GstBaseSrcClass *basesrc_class;
	GstPushSrcClass *pushsrc_class;

	gobject_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	basesrc_class = GST_BASE_SRC_CLASS(klass);
	pushsrc_class = GST_PUSH_SRC_CLASS(klass);

	gobject_class->set_property = gst_imx_v4l2src_set_property;
	gobject_class->get_property = gst_imx_v4l2src_get_property;
	gobject_class->finalize = gst_imx_v4l2src_finalize;

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_CAPTURE_MODE,
			g_param_spec_int("imx-capture-mode", "Capture mode",
				"Capture mode of camera, varies with each v4l2 driver,\n"
				"\t\t\t\tfor example ov5460:\n   "
				"\t\t\t\tov5640_mode_VGA_640_480 = 0,\n"
				"\t\t\t\tov5640_mode_QVGA_320_240 = 1,\n"
				"\t\t\t\tov5640_mode_NTSC_720_480 = 2,\n"
				"\t\t\t\tov5640_mode_PAL_720_576 = 3,\n"
				"\t\t\t\tov5640_mode_720P_1280_720 = 4,\n"
				"\t\t\t\tov5640_mode_1080P_1920_1080 = 5",
				0, G_MAXINT, DEFAULT_CAPTURE_MODE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_FRAMERATE_NUM,
			g_param_spec_int("fps-n", "FPS numerator",
				"Numerator of the framerate at which"
				"the input stream is to be captured",
				0, G_MAXINT, DEFAULT_FRAMERATE_NUM,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_INPUT,
			g_param_spec_int("input", "Input",
				"Video input selected with VIDIOC_S_INPUT",
				0, G_MAXINT, DEFAULT_INPUT,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_DEVICE,
			g_param_spec_string("device", "Device", "Device location",
				DEFAULT_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_QUEUE_SIZE,
			g_param_spec_int("queue-size", "Queue size",
				"Number of V4L2 buffers to request",
				0, G_MAXINT, DEFAULT_QUEUE_SIZE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_CROP_META_X,
			g_param_spec_int("crop-meta-x", "Crop meta X",
				"X value for crop metadata",
				0, G_MAXINT, DEFAULT_CROP_META_X,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_CROP_META_Y,
			g_param_spec_int("crop-meta-y", "Crop meta Y",
				"Y value for crop metadata",
				0, G_MAXINT, DEFAULT_CROP_META_Y,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_CROP_META_WIDTH,
			g_param_spec_int("crop-meta-width", "Crop meta WIDTH",
				"WIDTH value for crop metadata",
				0, G_MAXINT, DEFAULT_CROP_META_WIDTH,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property(gobject_class, IMX_V4L2SRC_CROP_META_HEIGHT,
			g_param_spec_int("crop-meta-height", "Crop meta HEIGHT",
				"HEIGHT value for crop metadata",
				0, G_MAXINT, DEFAULT_CROP_META_HEIGHT,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/* Being GstPhotography implementation implies overriding all properties
	 * defined by GstPhotography */
	g_object_class_override_property(gobject_class,
			PROP_WB_MODE, GST_PHOTOGRAPHY_PROP_WB_MODE);
	g_object_class_override_property(gobject_class,
			PROP_COLOR_TONE, GST_PHOTOGRAPHY_PROP_COLOR_TONE);
	g_object_class_override_property(gobject_class,
			PROP_SCENE_MODE, GST_PHOTOGRAPHY_PROP_SCENE_MODE);
	g_object_class_override_property(gobject_class,
			PROP_FLASH_MODE, GST_PHOTOGRAPHY_PROP_FLASH_MODE);
	g_object_class_override_property(gobject_class,
			PROP_FLICKER_MODE, GST_PHOTOGRAPHY_PROP_FLICKER_MODE);
	g_object_class_override_property(gobject_class,
			PROP_FOCUS_MODE, GST_PHOTOGRAPHY_PROP_FOCUS_MODE);
	g_object_class_override_property(gobject_class,
			PROP_CAPABILITIES, GST_PHOTOGRAPHY_PROP_CAPABILITIES);
	g_object_class_override_property(gobject_class,
			PROP_EV_COMP, GST_PHOTOGRAPHY_PROP_EV_COMP);
	g_object_class_override_property(gobject_class,
			PROP_ISO_SPEED, GST_PHOTOGRAPHY_PROP_ISO_SPEED);
	g_object_class_override_property(gobject_class,
			PROP_APERTURE, GST_PHOTOGRAPHY_PROP_APERTURE);
	g_object_class_override_property(gobject_class,
			PROP_EXPOSURE_TIME, GST_PHOTOGRAPHY_PROP_EXPOSURE_TIME);
	g_object_class_override_property(gobject_class,
			PROP_IMAGE_CAPTURE_SUPPORTED_CAPS, GST_PHOTOGRAPHY_PROP_IMAGE_CAPTURE_SUPPORTED_CAPS);
	g_object_class_override_property(gobject_class,
			PROP_IMAGE_PREVIEW_SUPPORTED_CAPS, GST_PHOTOGRAPHY_PROP_IMAGE_PREVIEW_SUPPORTED_CAPS);
	g_object_class_override_property(gobject_class,
			PROP_ZOOM, GST_PHOTOGRAPHY_PROP_ZOOM);
	g_object_class_override_property(gobject_class,
			PROP_COLOR_TEMPERATURE, GST_PHOTOGRAPHY_PROP_COLOR_TEMPERATURE);
	g_object_class_override_property(gobject_class,
			PROP_WHITE_POINT, GST_PHOTOGRAPHY_PROP_WHITE_POINT);
	g_object_class_override_property(gobject_class,
			PROP_ANALOG_GAIN, GST_PHOTOGRAPHY_PROP_ANALOG_GAIN);
	g_object_class_override_property(gobject_class,
			PROP_LENS_FOCUS, GST_PHOTOGRAPHY_PROP_LENS_FOCUS);
	g_object_class_override_property(gobject_class,
			PROP_MIN_EXPOSURE_TIME, GST_PHOTOGRAPHY_PROP_MIN_EXPOSURE_TIME);
	g_object_class_override_property(gobject_class,
			PROP_MAX_EXPOSURE_TIME, GST_PHOTOGRAPHY_PROP_MAX_EXPOSURE_TIME);
	g_object_class_override_property(gobject_class,
			PROP_NOISE_REDUCTION, GST_PHOTOGRAPHY_PROP_NOISE_REDUCTION);

	basesrc_class->negotiate = gst_imx_v4l2src_negotiate;
	basesrc_class->get_caps = gst_imx_v4l2src_get_caps;
	basesrc_class->set_caps = gst_imx_v4l2src_set_caps;
	basesrc_class->start = gst_imx_v4l2src_start;
	basesrc_class->stop = gst_imx_v4l2src_stop;
	basesrc_class->decide_allocation = gst_imx_v4l2src_decide_allocation;
	pushsrc_class->fill = gst_imx_v4l2src_fill;

	gst_element_class_set_static_metadata(element_class,
			"V4L2 CSI Video Source",
			"Source/Video",
			"Capture video streams using V4L2 CSI interface",
			"Philip Craig <phil@blackmoth.com.au>");

	gst_element_class_add_pad_template(element_class,
			gst_static_pad_template_get(&src_template));

	return;
}

/* GstURIHandler interface */
static GstURIType gst_imx_v4l2src_uri_get_type(GType type)
{
	return GST_URI_SRC;
}

static const gchar *const * gst_imx_v4l2src_uri_get_protocols(GType type)
{
	static const gchar *protocols[] = { "imxv4l2", NULL };

	return protocols;
}

static gchar * gst_imx_v4l2src_uri_get_uri(GstURIHandler * handler)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(handler);

	if (v4l2src->devicename != NULL)
		return g_strdup_printf("imxv4l2://%s", v4l2src->devicename);

	return g_strdup("imxv4l2://");
}

static gboolean gst_imx_v4l2src_uri_set_uri(GstURIHandler * handler,
		const gchar * uri, GError ** error)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(handler);
	const gchar *device = "/dev/video0";

	if (strcmp (uri, "imxv4l2://") != 0)
		device = uri + 10;

	g_object_set(v4l2src, "device", device, NULL);

	return TRUE;
}

static void gst_imx_v4l2src_uri_handler_init(gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = gst_imx_v4l2src_uri_get_type;
	iface->get_protocols = gst_imx_v4l2src_uri_get_protocols;
	iface->get_uri = gst_imx_v4l2src_uri_get_uri;
	iface->set_uri = gst_imx_v4l2src_uri_set_uri;
}

/* GstPhotographyFocusMode actually incapsulates two independent parameters:
 * - where to focus (infinity/normal/macro)
 * - when to focus (single/continuous)
 *
 * What is implemented:
 * - if GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_* is set:
 *   - V4L2 focus range is set to NORMAL, and continuous autofocus is enabled
 *     and kept on while element is in PLAYING state,
 *   - set_autofocus(TRUE) locks focus via V4L2_CID_3A_LOCK
 *   - set_autofocus(FALSE) unlocks focus via V4L2_CID_3A_LOCK
 * - if other supported mode is set:
 *   - set_autofocus(TRUE) triggers autofocus via V4L2_CID_AUTO_FOCUS_START
 *   - set_autofocus(FALSE) stops autofocus via V4L2_CID_AUTO_FOCUS_STOP
 *   - GST_PHOTOGRAPHY_FOCUS_DONE message is generated when focused
 *   - mode is mapped to V4L2 focus range as follows:
 *     GST_PHOTOGRAPHY_FOCUS_MODE_AUTO		=> V4L2_AUTO_FOCUS_RANGE_AUTO
 *     GST_PHOTOGRAPHY_FOCUS_MODE_MACRO		=> V4L2_AUTO_FOCUS_RANGE_MACRO
 *     GST_PHOTOGRAPHY_FOCUS_MODE_PORTRAIT	=> V4L2_AUTO_FOCUS_RANGE_NORMAL
 *     GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY	=> V4L2_AUTO_FOCUS_RANGE_INFINITY
 * - not supported:
 *   - GST_PHOTOGRAPHY_FOCUS_MODE_HYPERFOCAL
 *   - GST_PHOTOGRAPHY_FOCUS_MODE_EXTENDED
 *   - GST_PHOTOGRAPHY_FOCUS_MODE_MANUAL
 */

static inline const char *ctrl_name(int id)
{
	switch (id)
	{
		case V4L2_CID_FOCUS_AUTO:
			return "V4L2_CID_FOCUS_AUTO";
		case V4L2_CID_AUTO_FOCUS_RANGE:
			return "V4L2_CID_FOCUS_RANGE";
		case V4L2_CID_AUTO_FOCUS_START:
			return "V4L2_CID_AUTO_FOCUS_START";
		case V4L2_CID_AUTO_FOCUS_STOP:
			return "V4L2_CID_AUTO_FOCUS_STOP";
		case V4L2_CID_AUTO_FOCUS_STATUS:
			return "V4L2_CID_AUTO_FOCUS_STATUS";
		case V4L2_CID_3A_LOCK:
			return "V4L2_CID_3A_LOCK";
		default:
			return "<fixme>";
	}
}

static int v4l2_g_ctrl(GstImxV4l2VideoSrc *v4l2src, int id, int *value)
{
	struct v4l2_control control;
	int ret;

	control.id = id;
	ret = ioctl(GST_IMX_FD_OBJECT_GET_FD(v4l2src->fd_obj_v4l), VIDIOC_G_CTRL, &control);

	if (ret < 0)
		GST_LOG_OBJECT(v4l2src, "VIDIOC_G_CTRL(%s) failed", ctrl_name(id));
	else
	{
		GST_LOG_OBJECT(v4l2src, "VIDIOC_G_CTRL(%s) returned %d", ctrl_name(id), control.value);
		*value = control.value;
	}

	return ret;
}

static inline int v4l2_s_ctrl(GstImxV4l2VideoSrc *v4l2src, int id, int value)
{
	struct v4l2_control control;
	int ret;

	GST_LOG_OBJECT(v4l2src, "VIDIOC_S_CTRL(%s, %d)", ctrl_name(id), value);

	control.id = id;
	control.value = value;
	ret = ioctl(GST_IMX_FD_OBJECT_GET_FD(v4l2src->fd_obj_v4l), VIDIOC_S_CTRL, &control);

	if (ret < 0)
		GST_LOG_OBJECT(v4l2src, "VIDIOC_S_CTRL(%s, %d) failed", ctrl_name(id), value);
	else
		GST_LOG_OBJECT(v4l2src, "VIDIOC_S_CTRL(%s, %d) succeed", ctrl_name(id), value);

	return ret;
}

static void gst_imx_v4l2src_apply_focus_settings(GstImxV4l2VideoSrc *v4l2src,
		gboolean activate)
{
	int locks, range;

	/* even when activating, first ensure that it is not running */

	/* ensure that continuous autofocus is not running */
	v4l2_s_ctrl(v4l2src, V4L2_CID_FOCUS_AUTO, 0);
	/* ensure that single shot AF is not running */
	v4l2_s_ctrl(v4l2src, V4L2_CID_AUTO_FOCUS_STOP, 0);
	if (v4l2src->af_clock_id)
	{
		gst_clock_id_unschedule(v4l2src->af_clock_id);
		gst_clock_id_unref(v4l2src->af_clock_id);
		v4l2src->af_clock_id = NULL;
	}
	/* ensure that focus is not locked */
	if (v4l2_g_ctrl(v4l2src, V4L2_CID_3A_LOCK, &locks) == 0 && (locks & V4L2_LOCK_FOCUS))
		v4l2_s_ctrl(v4l2src, V4L2_CID_3A_LOCK, locks & ~V4L2_LOCK_FOCUS);

	if (activate)
	{
		/* set focus range */

		switch (v4l2src->focus_mode)
		{
			case GST_PHOTOGRAPHY_FOCUS_MODE_AUTO:
				range = V4L2_AUTO_FOCUS_RANGE_AUTO;
				break;
			case GST_PHOTOGRAPHY_FOCUS_MODE_MACRO:
				range = V4L2_AUTO_FOCUS_RANGE_MACRO;
				break;
			case GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY:
				range = V4L2_AUTO_FOCUS_RANGE_INFINITY;
				break;
			default:
				range = V4L2_AUTO_FOCUS_RANGE_NORMAL;
				break;
		}

		v4l2_s_ctrl(v4l2src, V4L2_CID_AUTO_FOCUS_RANGE, range);

		/* enable continuous autofocus if requested */

		if (v4l2src->focus_mode == GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL)
			v4l2_s_ctrl(v4l2src, V4L2_CID_FOCUS_AUTO, 1);
	}
}

static gboolean gst_imx_v4l2src_set_focus_mode(GstPhotography *photo,
		GstPhotographyFocusMode focus_mode)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(photo);

	GST_LOG_OBJECT(v4l2src, "setting focus mode to %d", focus_mode);

	switch (focus_mode)
	{
		case GST_PHOTOGRAPHY_FOCUS_MODE_AUTO:
		case GST_PHOTOGRAPHY_FOCUS_MODE_MACRO:
		case GST_PHOTOGRAPHY_FOCUS_MODE_PORTRAIT:
		case GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY:
			break;
		case GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL:
		case GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_EXTENDED:
			focus_mode = GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL;
			break;
		default:
			GST_WARNING_OBJECT(v4l2src, "focus mode %d is not supported", focus_mode);
			return FALSE;
	}

	g_mutex_lock(&v4l2src->af_mutex);

	if (v4l2src->focus_mode != focus_mode)
	{
		v4l2src->focus_mode = focus_mode;

		if (GST_STATE(v4l2src) == GST_STATE_PAUSED || GST_STATE(v4l2src) == GST_STATE_PLAYING)
			gst_imx_v4l2src_apply_focus_settings(v4l2src, TRUE);
	}

	g_mutex_unlock(&v4l2src->af_mutex);

	return TRUE;
}

static gboolean gst_imx_v4l2src_get_focus_mode(GstPhotography *photo,
		GstPhotographyFocusMode *focus_mode)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(photo);

	g_mutex_lock(&v4l2src->af_mutex);
	*focus_mode = v4l2src->focus_mode;
	g_mutex_unlock(&v4l2src->af_mutex);

	return TRUE;
}


static gboolean gst_imx_v4l2src_af_status_cb(GstClock *clock, GstClockTime time,
		GstClockID id, gpointer user_data);

static void gst_imx_v4l2src_af_check_status(GstImxV4l2VideoSrc *v4l2src)
{
	int status;
	gboolean send_message;
	GstPhotographyFocusStatus message_status;
	gboolean schedule_recheck;

	if (v4l2_g_ctrl(v4l2src, V4L2_CID_AUTO_FOCUS_STATUS, &status) < 0)
		goto none;

	switch (status)
	{
		case V4L2_AUTO_FOCUS_STATUS_IDLE:
		default:
		none:
			send_message = TRUE;
			message_status = GST_PHOTOGRAPHY_FOCUS_STATUS_NONE;
			schedule_recheck = FALSE;
			break;
		case V4L2_AUTO_FOCUS_STATUS_BUSY:
			send_message = FALSE;
			schedule_recheck = TRUE;
			break;
		case V4L2_AUTO_FOCUS_STATUS_REACHED:
			send_message = TRUE;
			message_status = GST_PHOTOGRAPHY_FOCUS_STATUS_SUCCESS;
			schedule_recheck = FALSE;
			break;
		case V4L2_AUTO_FOCUS_STATUS_FAILED:
			send_message = TRUE;
			message_status = GST_PHOTOGRAPHY_FOCUS_STATUS_FAIL;
			schedule_recheck = FALSE;
			break;
	}

	if (send_message)
	{
		GstStructure *s;
		GstMessage *m;

		s = gst_structure_new(GST_PHOTOGRAPHY_AUTOFOCUS_DONE,
				"status", G_TYPE_INT, message_status,
				NULL);
		m = gst_message_new_custom(GST_MESSAGE_ELEMENT,
				GST_OBJECT(v4l2src), s);

		if (!gst_element_post_message(GST_ELEMENT(v4l2src), m))
			GST_ERROR_OBJECT(v4l2src, "failed to post message");
	}

	if (schedule_recheck)
	{
		GstClock *c;
		GstClockTime t;

		c = gst_system_clock_obtain();
		t = gst_clock_get_time(c) + 50 * GST_MSECOND;
		v4l2src->af_clock_id = gst_clock_new_single_shot_id(c, t);
		gst_object_unref(c);

		if (gst_clock_id_wait_async(v4l2src->af_clock_id,
					gst_imx_v4l2src_af_status_cb,
					v4l2src, NULL) != GST_CLOCK_OK)
			GST_ERROR_OBJECT(v4l2src, "failed to schedule recheck");
	}
}

static gboolean gst_imx_v4l2src_af_status_cb(GstClock *clock, GstClockTime time,
		GstClockID id, gpointer user_data)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(user_data);

	g_mutex_lock(&v4l2src->af_mutex);

	if (v4l2src->af_clock_id == id)
	{
		gst_clock_id_unref(v4l2src->af_clock_id);
		v4l2src->af_clock_id = NULL;

		gst_imx_v4l2src_af_check_status(v4l2src);
	}

	g_mutex_unlock(&v4l2src->af_mutex);
	return TRUE;
}

void gst_imx_v4l2src_set_autofocus(GstPhotography *photo, gboolean on)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(photo);
	int locks;

	g_mutex_lock(&v4l2src->af_mutex);

	if (v4l2src->af_clock_id)
	{
		gst_clock_id_unschedule(v4l2src->af_clock_id);
		gst_clock_id_unref(v4l2src->af_clock_id);
		v4l2src->af_clock_id = NULL;
	}

	if (v4l2src->focus_mode == GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL)
	{
		if (v4l2_g_ctrl(v4l2src, V4L2_CID_3A_LOCK, &locks) == 0)
		{
			if (on && !(locks & V4L2_LOCK_FOCUS))
				v4l2_s_ctrl(v4l2src, V4L2_CID_3A_LOCK, locks | V4L2_LOCK_FOCUS);
			else if (!on && (locks & V4L2_LOCK_FOCUS))
				v4l2_s_ctrl(v4l2src, V4L2_CID_3A_LOCK, locks & ~V4L2_LOCK_FOCUS);
		}
	}
	else
	{
		if (on)
		{
			if (v4l2_s_ctrl(v4l2src, V4L2_CID_AUTO_FOCUS_START, 0) == 0)
				gst_imx_v4l2src_af_check_status(v4l2src);
		}
		else
			v4l2_s_ctrl(v4l2src, V4L2_CID_AUTO_FOCUS_STOP, 0);
	}

	g_mutex_unlock(&v4l2src->af_mutex);
}

static gboolean gst_imx_v4lsrc_prepare_for_capture(GstPhotography *photo,
		GstPhotographyCapturePrepared func, GstCaps *capture_caps, gpointer user_data)
{
	GstImxV4l2VideoSrc *v4l2src = GST_IMX_V4L2SRC(photo);

	GST_LOG_OBJECT(v4l2src, "capture_caps: %" GST_PTR_FORMAT, capture_caps);

	func(user_data, capture_caps);
	return TRUE;
}

static void gst_imx_v4l2src_photography_init(gpointer g_iface, gpointer iface_data)
{
	GstPhotographyInterface *iface = (GstPhotographyInterface *) g_iface;

	iface->set_focus_mode = gst_imx_v4l2src_set_focus_mode;
	iface->get_focus_mode = gst_imx_v4l2src_get_focus_mode;
	iface->set_autofocus = gst_imx_v4l2src_set_autofocus;
	iface->prepare_for_capture = gst_imx_v4lsrc_prepare_for_capture;
}

static gboolean plugin_init(GstPlugin *plugin)
{
	return gst_element_register(plugin, "imxv4l2videosrc", GST_RANK_PRIMARY,
			gst_imx_v4l2src_get_type());
}

GST_PLUGIN_DEFINE(
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		imxv4l2videosrc,
		"GStreamer i.MX V4L2 CSI video source",
		plugin_init,
		VERSION,
		"LGPL",
		GST_PACKAGE_NAME,
		GST_PACKAGE_ORIGIN
)
