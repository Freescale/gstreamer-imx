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
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstimxv4l2context.h"


/* i.MX kernels use this ioctl to communicate the chip tyoe that
 * is used for capturing. Newer V4L2 headers do not have this ioctl
 * anymore though, so we have to keep a copy here.
 *
 * Note that we do this due to the badly broken imxv4l2 capture driver
 * that requires several chip specific workarounds. See the comments
 * at the GstImxV4L2CaptureChip definition for more details. */

#ifndef VIDIOC_DBG_G_CHIP_IDENT

struct v4l2_dbg_chip_ident {
	struct v4l2_dbg_match match;
	__u32 ident;       /* chip identifier as specified in <media/v4l2-chip-ident.h> */
	__u32 revision;    /* chip revision, chip specific */
} __attribute__ ((packed));

#define VIDIOC_DBG_G_CHIP_IDENT _IOWR('V', 81, struct v4l2_dbg_chip_ident)

#endif


GST_DEBUG_CATEGORY_STATIC(imx_v4l2_context_debug);
#define GST_CAT_DEFAULT imx_v4l2_context_debug


struct _GstImxV4L2Context
{
	GstObject parent;

	GstImxV4L2DeviceType device_type;

	gchar *device_node;
	gint num_buffers;

	GstImxV4L2ProbeResult probe_result;
	gboolean did_successfully_probe;
};


struct _GstImxV4L2ContextClass
{
	GstObjectClass parent_class;
};


G_DEFINE_TYPE(GstImxV4L2Context, gst_imx_v4l2_context, GST_TYPE_OBJECT)


static void gst_imx_v4l2_context_finalize(GObject *object);

static gboolean enum_v4l2_format(GstImxV4L2Context *self, int fd, struct v4l2_fmtdesc *v4l2_format_desc, gboolean *reached_end);
static gboolean probe_device_caps(GstImxV4L2Context *self, int fd);
static void get_gst_framerate_from_v4l2_frameinterval(guint32 v4l2_num, guint32 v4l2_denom, gint *num, gint *denom);
static gboolean fill_caps_with_probed_info(GstImxV4L2Context *self, int fd, GstCaps *probed_device_caps, guint width, guint height, GstImxV4L2VideoFormat const *imx_v4l2_format);
static void log_capabilities(GstObject *object, guint32 capabilities);


static void gst_imx_v4l2_context_class_init(GstImxV4L2ContextClass *klass)
{
	GObjectClass *object_class;

	GST_DEBUG_CATEGORY_INIT(imx_v4l2_context_debug, "imxv4l2context", 0, "NXP i.MX V4L2 context");

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_v4l2_context_finalize);
}


static void gst_imx_v4l2_context_init(GstImxV4L2Context *self)
{
	memset(&(self->probe_result), 0, sizeof(self->probe_result));
}


static void gst_imx_v4l2_context_finalize(GObject *object)
{
	GstImxV4L2Context *self = GST_IMX_V4L2_CONTEXT(object);

	gst_imx_v4l2_clear_probe_result(&(self->probe_result));
	g_free(self->device_node);

	G_OBJECT_CLASS(gst_imx_v4l2_context_parent_class)->finalize(object);
}


GstImxV4L2Context* gst_imx_v4l2_context_new(GstImxV4L2DeviceType device_type)
{
	GstImxV4L2Context *imx_v4l2_context = (GstImxV4L2Context *)g_object_new(gst_imx_v4l2_context_get_type(), NULL);

	GST_DEBUG_OBJECT(imx_v4l2_context, "created new imxv4l2 context %" GST_PTR_FORMAT, (gpointer)(imx_v4l2_context));

	imx_v4l2_context->device_type = device_type;

	return imx_v4l2_context;
}


GstImxV4L2DeviceType gst_imx_v4l2_context_get_device_type(GstImxV4L2Context const *imx_v4l2_context)
{
	g_assert(imx_v4l2_context != NULL);
	return imx_v4l2_context->device_type;
}


void gst_imx_v4l2_context_set_device_node(GstImxV4L2Context *imx_v4l2_context, gchar const *device_node)
{
	g_assert(imx_v4l2_context != NULL);
	g_assert(device_node != NULL);

	g_free(imx_v4l2_context->device_node);
	imx_v4l2_context->device_node = g_strdup(device_node);

	GST_DEBUG_OBJECT(imx_v4l2_context, "set device node to \"%s\"", device_node);
}


gchar const *gst_imx_v4l2_context_get_device_node(GstImxV4L2Context const *imx_v4l2_context)
{
	g_assert(imx_v4l2_context != NULL);
	return imx_v4l2_context->device_node;
}


void gst_imx_v4l2_context_set_num_buffers(GstImxV4L2Context *imx_v4l2_context, gint num_buffers)
{
	g_assert(imx_v4l2_context != NULL);
	g_assert(num_buffers >= 2);

	imx_v4l2_context->num_buffers = num_buffers;

	GST_DEBUG_OBJECT(imx_v4l2_context, "set num buffers to %d", num_buffers);
}


gint gst_imx_v4l2_context_get_num_buffers(GstImxV4L2Context const *imx_v4l2_context)
{
	g_assert(imx_v4l2_context != NULL);
	return imx_v4l2_context->num_buffers;
}


gboolean gst_imx_v4l2_context_probe_device(GstImxV4L2Context *imx_v4l2_context)
{
	gboolean retval = TRUE;
	int fd = -1;
	struct v4l2_capability v4l2_caps;
	GstImxV4L2ProbeResult *probe_result = &(imx_v4l2_context->probe_result);

	g_assert(imx_v4l2_context != NULL);


	/* Get the device FD. */

	fd = gst_imx_v4l2_context_open_fd(imx_v4l2_context);
	if (fd < 0)
		goto error;


	/* Fetch and print basic device capabilities. */

	if (G_UNLIKELY(ioctl(fd, VIDIOC_QUERYCAP, &v4l2_caps) < 0))
	{
		GST_ERROR_OBJECT(imx_v4l2_context, "could not query capabilities: %s (%d)", strerror(errno), errno);
		goto error;
	}

	GST_DEBUG_OBJECT(imx_v4l2_context, "device node:    [%s]", imx_v4l2_context->device_node);
	GST_DEBUG_OBJECT(imx_v4l2_context, "driver:         [%s]", v4l2_caps.driver);
	GST_DEBUG_OBJECT(imx_v4l2_context, "card:           [%s]", v4l2_caps.card);
	GST_DEBUG_OBJECT(imx_v4l2_context, "bus info:       [%s]", v4l2_caps.bus_info);
	GST_DEBUG_OBJECT(imx_v4l2_context, "driver version: %d.%d.%d", ((v4l2_caps.version >> 16) & 0xFF), ((v4l2_caps.version >> 8) & 0xFF), ((v4l2_caps.version >> 0) & 0xFF));

	probe_result->v4l2_device_capabilities = (v4l2_caps.capabilities & V4L2_CAP_DEVICE_CAPS) ? v4l2_caps.device_caps : v4l2_caps.capabilities;

	GST_DEBUG_OBJECT(imx_v4l2_context, "available capabilities of physical device:");
	log_capabilities(GST_OBJECT_CAST(imx_v4l2_context), v4l2_caps.capabilities);

	if (probe_result->v4l2_device_capabilities & V4L2_CAP_DEVICE_CAPS)
	{
		GST_DEBUG_OBJECT(imx_v4l2_context, "capabilities of opened device:");
		log_capabilities(GST_OBJECT_CAST(imx_v4l2_context), v4l2_caps.device_caps);
	}
	else
		GST_DEBUG_OBJECT(imx_v4l2_context, "no capabilities of opened device set");


	/* Determine the capture chip type. This is needed
	 * for several mxc_v4l2 driver bug workarounds. */

	if (imx_v4l2_context->device_type == GST_IMX_V4L2_DEVICE_TYPE_CAPTURE)
	{
		struct v4l2_dbg_chip_ident chip_identifier;

		memset(&chip_identifier, 0, sizeof(chip_identifier));

		if (strncmp((char const *)(v4l2_caps.driver), "mxc_v4l2", 8) == 0)
		{
			if (G_UNLIKELY(ioctl(fd, VIDIOC_DBG_G_CHIP_IDENT, &chip_identifier) < 0))
			{
				GST_ERROR_OBJECT(imx_v4l2_context, "failed to identify capture chip: %s (%d)", strerror(errno), errno);
				goto error;
			}

			GST_DEBUG_OBJECT(imx_v4l2_context, "chip identifier: [%s]", chip_identifier.match.name);

			if (g_strcmp0(chip_identifier.match.name, "ov5640_camera") == 0)
			{
				GST_DEBUG_OBJECT(imx_v4l2_context, "this is an OmniVision 5640 capture chip");
				probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_OV5640;
			}
			else if (g_strcmp0(chip_identifier.match.name, "ov5640_mipi_camera") == 0)
			{
				GST_DEBUG_OBJECT(imx_v4l2_context, "this is an OmniVision 5640 capture chip with MIPI interface");
				probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI;
			}
			else if (g_strcmp0(chip_identifier.match.name, "ov5645_mipi_camera") == 0)
			{
				GST_DEBUG_OBJECT(imx_v4l2_context, "this is an OmniVision 5645 capture chip with MIPI interface");
				probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_OV5645_MIPI;
			}
			else if (g_strcmp0(chip_identifier.match.name, "adv7180") == 0)
			{
				GST_DEBUG_OBJECT(imx_v4l2_context, "this is an Analog Devices ADV7180 capture chip");
				probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_ADV7180;
			}
			else
			{
				GST_DEBUG_OBJECT(imx_v4l2_context, "unrecognized mxc_v4l2 based capture chip");
				probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_UNRECOGNIZED_MXC_V4L2_BASED;
			}
		}
		else if (strncmp((char const *)(v4l2_caps.card), "tw6869", 6) == 0)
		{
			GST_DEBUG_OBJECT(imx_v4l2_context, "this is an Intersil TW6869 capture chip");
			probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_TW6869;
		}
		else
		{
			GST_DEBUG_OBJECT(imx_v4l2_context, "capture chip cannot be identified; may not be mxc_v4l2 specific hardware");
			probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_UNIDENTIFIED;
		}

		if (!probe_device_caps(imx_v4l2_context, fd))
			goto error;
	}
	else
	{
		probe_result->capture_chip = GST_IMX_V4L2_CAPTURE_CHIP_UNIDENTIFIED;

		if (!probe_device_caps(imx_v4l2_context, fd))
			goto error;
	}

	GST_DEBUG_OBJECT(imx_v4l2_context, "device caps: %" GST_PTR_FORMAT, (gpointer)(probe_result->device_caps));

	imx_v4l2_context->did_successfully_probe = TRUE;

finish:
	if (fd > 0)
		close(fd);

	return retval;

error:
	retval = FALSE;
	imx_v4l2_context->did_successfully_probe = FALSE;
	goto finish;
}


GstImxV4L2ProbeResult const *gst_imx_v4l2_context_get_probe_result(GstImxV4L2Context const *imx_v4l2_context)
{
	g_assert(imx_v4l2_context != NULL);
	return imx_v4l2_context->did_successfully_probe ? &(imx_v4l2_context->probe_result) : NULL;
}


int gst_imx_v4l2_context_open_fd(GstImxV4L2Context *imx_v4l2_context)
{
	int fd;
	struct stat device_stat;


	/* Device node checks to verify that the node is OK. */

	if (stat(imx_v4l2_context->device_node, &device_stat) == -1)
	{
		GST_ERROR_OBJECT(imx_v4l2_context, "cannot identify device \"%s\"", imx_v4l2_context->device_node);
		goto error;
	}

	if (!S_ISCHR(device_stat.st_mode))
	{
		GST_ERROR_OBJECT(imx_v4l2_context, "\"%s\" is not a character device", imx_v4l2_context->device_node);
		goto error;
	}


	/* Open the device. */

	fd = open(imx_v4l2_context->device_node, O_RDWR);
	if (fd < 0)
	{
		GST_ERROR_OBJECT(imx_v4l2_context, "could not open V4L2 device %s: %s (%d)", imx_v4l2_context->device_node, strerror(errno), errno);
		goto error;
	}


finish:
	return fd;

error:
	fd = -1;
	goto finish;
}


void gst_imx_v4l2_copy_probe_result(GstImxV4L2ProbeResult *dest, GstImxV4L2ProbeResult const *src)
{
	dest->device_caps = (src->device_caps != NULL) ? gst_caps_ref(src->device_caps) : NULL;
	dest->capture_chip = src->capture_chip;
	dest->v4l2_device_capabilities = src->v4l2_device_capabilities;

	/* Deep-copy the chip specific framesizes array. */
	if (src->chip_specific_frame_sizes != NULL)
	{
		dest->chip_specific_frame_sizes = g_malloc(src->num_chip_specific_frame_sizes * sizeof(GstImxV4L2EnumeratedFrameSize));
		memcpy(dest->chip_specific_frame_sizes, src->chip_specific_frame_sizes, src->num_chip_specific_frame_sizes * sizeof(GstImxV4L2EnumeratedFrameSize));
		dest->num_chip_specific_frame_sizes = src->num_chip_specific_frame_sizes;
	}
	else
	{
		dest->chip_specific_frame_sizes = NULL;
		dest->num_chip_specific_frame_sizes = 0;
	}

	/* Copy the list of enumerated formats. Note that we do
	 * _not_ deep-copy the individual formats. That's because this
	 * list holds const GstImxV4L2VideoFormat pointers to static
	 * format descriptions, so deep-copying that is pointless. */
	if (src->enumerated_v4l2_formats != NULL)
	{
		GList *list_elem;

		for (list_elem = src->enumerated_v4l2_formats; list_elem != NULL; list_elem = list_elem->next)
			dest->enumerated_v4l2_formats = g_list_append(dest->enumerated_v4l2_formats, list_elem->data);
	}
	else
		dest->enumerated_v4l2_formats = NULL;
}


void gst_imx_v4l2_clear_probe_result(GstImxV4L2ProbeResult *probe_result)
{
	if (probe_result == NULL)
		return;

	if (probe_result->chip_specific_frame_sizes != NULL)
	{
		g_free(probe_result->chip_specific_frame_sizes);
		probe_result->chip_specific_frame_sizes = NULL;
		probe_result->num_chip_specific_frame_sizes = 0;
	}

	gst_caps_replace(&(probe_result->device_caps), NULL);

	if (probe_result->enumerated_v4l2_formats != NULL)
	{
		g_list_free(probe_result->enumerated_v4l2_formats);
		probe_result->enumerated_v4l2_formats = NULL;
	}
}


GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_gst_video_format_from_probe_result(GstImxV4L2ProbeResult const *probe_result, GstVideoFormat gst_format)
{
	GList *list_elem;

	if (probe_result == NULL)
		return NULL;

	for (list_elem = probe_result->enumerated_v4l2_formats; list_elem != NULL; list_elem = list_elem->next)
	{
		GstImxV4L2VideoFormat const *format = (GstImxV4L2VideoFormat const *)(list_elem->data);

		if ((format->type == GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW) && (format->format.gst_format == gst_format))
			return format;
	}

	return NULL;
}


static gboolean enum_v4l2_format(GstImxV4L2Context *self, int fd, struct v4l2_fmtdesc *v4l2_format_desc, gboolean *reached_end)
{
	GstImxV4L2ProbeResult *probe_result = &(self->probe_result);
	guint format_index = v4l2_format_desc->index;

	/* mxc_v4l2 devices do not support more than these formats. */
	static struct v4l2_fmtdesc const default_mxc_v4l2_format_descriptions[] = {
		{
			.description = "I420",
			.pixelformat = V4L2_PIX_FMT_YUV420
		},
		{
			.description = "NV12",
			.pixelformat = V4L2_PIX_FMT_NV12
		},
		{
			.description = "YUY2",
			.pixelformat = V4L2_PIX_FMT_YUYV
		},
		{
			.description = "UYVY",
			.pixelformat = V4L2_PIX_FMT_UYVY
		},
	};
	static int const num_default_mxc_v4l2_format_descriptions = sizeof(default_mxc_v4l2_format_descriptions) / sizeof(struct v4l2_fmtdesc);

	/* The OV5647 provides Bayer data only. */
	static struct v4l2_fmtdesc const ov5647_mxc_v4l2_format_descriptions[] = {
		{
			.description = "Bayer 8-bit BGGR",
			.pixelformat = V4L2_PIX_FMT_SBGGR8
		},
	};
	static int const num_ov5647_mxc_v4l2_format_descriptions = sizeof(ov5647_mxc_v4l2_format_descriptions) / sizeof(struct v4l2_fmtdesc);

	/* The ADV7180 provides UYVY data only. */
	static struct v4l2_fmtdesc const adv7180_mxc_v4l2_format_descriptions[] = {
		{
			.description = "UYVY",
			.pixelformat = V4L2_PIX_FMT_UYVY
		},
	};
	static int const num_adv7180_mxc_v4l2_format_descriptions = sizeof(adv7180_mxc_v4l2_format_descriptions) / sizeof(struct v4l2_fmtdesc);

	struct v4l2_fmtdesc const *selected_mxc_v4l2_format_descriptions = NULL;
	int num_selected_mxc_v4l2_format_descriptions = 0;

	*reached_end = FALSE;

	switch (probe_result->capture_chip)
	{
		/* We cannot use VIDIOC_ENUM_FMT with mxc_v4l2 based devices,
		 * because the VIDIOC_ENUM_FMT implementation in the mxc_v4l2
		 * driver is completely broken. Instead, we must rely on
		 * hard-coded tables if these chips are used. */

		case GST_IMX_V4L2_CAPTURE_CHIP_UNRECOGNIZED_MXC_V4L2_BASED:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5640:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5645_MIPI:
			selected_mxc_v4l2_format_descriptions = default_mxc_v4l2_format_descriptions;
			num_selected_mxc_v4l2_format_descriptions = num_default_mxc_v4l2_format_descriptions;
			break;

		case GST_IMX_V4L2_CAPTURE_CHIP_OV5647:
			selected_mxc_v4l2_format_descriptions = ov5647_mxc_v4l2_format_descriptions;
			num_selected_mxc_v4l2_format_descriptions = num_ov5647_mxc_v4l2_format_descriptions;
			break;

		case GST_IMX_V4L2_CAPTURE_CHIP_ADV7180:
			selected_mxc_v4l2_format_descriptions = adv7180_mxc_v4l2_format_descriptions;
			num_selected_mxc_v4l2_format_descriptions = num_adv7180_mxc_v4l2_format_descriptions;
			break;

		/* For unknown devices, use VIDIOC_ENUM_FMT and return. */
		default:
		{
			if (G_UNLIKELY(ioctl(fd, VIDIOC_ENUM_FMT, v4l2_format_desc) < 0))
			{
				if (errno == EINVAL)
				{
					*reached_end = TRUE;
					break;
				}
				else
				{
					GST_ERROR_OBJECT(self, "failed to enumerate V4L2 format #%u: %s (%d)", format_index, strerror(errno), errno);
					return FALSE;
				}
			}
			else
				return TRUE;
		}
	}

	/* This is reached if capture_chip is set to a known chip type.
	 * In that case, we still have to fill v4l2_format_desc with
	 * data to mimic what VIDIOC_ENUM_FMT would normally do. */
	{
		struct v4l2_fmtdesc const *desc_from_list;

		if ((int)(v4l2_format_desc->index) >= num_selected_mxc_v4l2_format_descriptions)
		{
			*reached_end = TRUE;
			return TRUE;
		}

		desc_from_list = &(selected_mxc_v4l2_format_descriptions[v4l2_format_desc->index]);

		memcpy(v4l2_format_desc->description, desc_from_list->description, sizeof(desc_from_list->description));
		v4l2_format_desc->pixelformat = desc_from_list->pixelformat;
		v4l2_format_desc->flags = 0;
	}

	return TRUE;
}


static gboolean probe_device_caps(GstImxV4L2Context *self, int fd)
{
	gboolean retval = TRUE;
	GstCaps *probed_device_caps;
	guint format_index;
	struct v4l2_fmtdesc v4l2_format_desc;
	struct v4l2_frmsizeenum v4l2_framesize;
	GstImxV4L2ProbeResult *probe_result = &(self->probe_result);
	gint num_framesizes = 0;

	probed_device_caps = gst_caps_new_empty();

	probe_result->chip_specific_frame_sizes = NULL;
	probe_result->num_chip_specific_frame_sizes = 0;

	GST_DEBUG_OBJECT(self, "enumerating supported V4L2 pixel formats");

	/* Enumerate all supported video formats. We use a helper
	 * enum_v4l2_format() function instead of using VIDIOC_ENUM_FMT
	 * directly, since the latter is broken in the mxc_v4l2 driver.
	 * The helper function applies workarounds for that. */
	for (format_index = 0; ; ++format_index)
	{
		gboolean reached_end;
		v4l2_format_desc.index = format_index;
		GstImxV4L2VideoFormat const *imx_v4l2_format;

		switch (self->device_type)
		{
			case GST_IMX_V4L2_DEVICE_TYPE_CAPTURE:
				v4l2_format_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				break;

			case GST_IMX_V4L2_DEVICE_TYPE_OUTPUT:
				v4l2_format_desc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
				break;

			default:
				g_assert_not_reached();
		}

		if (!enum_v4l2_format(self, fd, &v4l2_format_desc, &reached_end))
			goto error;

		if (reached_end)
		{
			GST_DEBUG_OBJECT(self, "no more pixel formats to enumerate");
			break;
		}

		GST_DEBUG_OBJECT(self, "format index:        %u",   v4l2_format_desc.index);
		GST_DEBUG_OBJECT(self, "flags:               %08x", v4l2_format_desc.flags);
		GST_DEBUG_OBJECT(self, "description:         '%s'", v4l2_format_desc.description);
		GST_DEBUG_OBJECT(self, "pixel format fourCC: %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS(v4l2_format_desc.pixelformat));

		/* We are only interested in directly supported formats. */
		if (v4l2_format_desc.flags & V4L2_FMT_FLAG_EMULATED)
		{
			GST_DEBUG_OBJECT(self, "skipping format since it is emulated");
			continue;
		}

		imx_v4l2_format = gst_imx_v4l2_get_by_v4l2_pixelformat(v4l2_format_desc.pixelformat);
		if (G_UNLIKELY(imx_v4l2_format == NULL))
		{
			GST_DEBUG_OBJECT(self, "skipping this format since it is not supported/recognized");
			continue;
		}

		probe_result->enumerated_v4l2_formats = g_list_append(probe_result->enumerated_v4l2_formats, (gpointer *)imx_v4l2_format);

		if (self->device_type == GST_IMX_V4L2_DEVICE_TYPE_OUTPUT)
		{
			/* For output devices, we already have everything we
			 * need to construct a structure for the GstCaps. */

			GstStructure *structure;

			switch (imx_v4l2_format->type)
			{
				case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
					structure = gst_structure_new(
						gst_imx_v4l2_get_media_type_for_format(imx_v4l2_format),
						"format", G_TYPE_STRING, gst_video_format_to_string(imx_v4l2_format->format.gst_format),
						"width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
						"height", GST_TYPE_INT_RANGE, 16, G_MAXINT,
						"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
						"pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
						"interlace-mode", G_TYPE_STRING, "progressive",
						NULL
					);
					GST_DEBUG_OBJECT(self, "gst video format:    %s", gst_video_format_to_string(imx_v4l2_format->format.gst_format));
					break;

				case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
					structure = gst_structure_new(
						gst_imx_v4l2_get_media_type_for_format(imx_v4l2_format),
						"format", G_TYPE_STRING, gst_imx_v4l2_bayer_format_to_string(imx_v4l2_format->format.bayer_format),
						"width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
						"height", GST_TYPE_INT_RANGE, 16, G_MAXINT,
						"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
						"pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
						"interlace-mode", G_TYPE_STRING, "progressive",
						NULL
					);
					GST_DEBUG_OBJECT(self, "Bayer video format:  %s", gst_imx_v4l2_bayer_format_to_string(imx_v4l2_format->format.bayer_format));
					break;

				case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
					structure = gst_structure_new(
						gst_imx_v4l2_get_media_type_for_format(imx_v4l2_format),
						"width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
						"height", GST_TYPE_INT_RANGE, 16, G_MAXINT,
						"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
						"pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
						NULL
					);
					GST_DEBUG_OBJECT(self, "Codec media type:    %s", gst_imx_v4l2_get_media_type_for_format(imx_v4l2_format));
					break;

				default:
					g_assert_not_reached();
			}

			gst_caps_append_structure(probed_device_caps, structure);

			continue;
		}
		else
		{
			/* For capture devices, we still have to enumerate
			 * what frame sizes and frame rates are supported for
			 * the current pixel format. */

			memset(&v4l2_framesize, 0, sizeof(v4l2_framesize));
			v4l2_framesize.index = 0;
			v4l2_framesize.pixel_format = v4l2_format_desc.pixelformat;

			/* Enumerate the first framesize. */
			if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &v4l2_framesize) < 0)
			{
				GST_ERROR_OBJECT(self, "could not enumerate frame sizes for pixel format %" GST_FOURCC_FORMAT ": %s (%d)", GST_FOURCC_ARGS(v4l2_format_desc.pixelformat), strerror(errno), errno);
				goto error;
			}

			/* This is an mxc_v4l2 driver bug workaround. That driver
			 * does not se the type field properly. However, the framesizes
			 * it returns are always discrete ones, so we can hardcode this. */
			switch (probe_result->capture_chip)
			{
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5640:
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI:
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5645_MIPI:
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5647:
					v4l2_framesize.type = V4L2_FRMSIZE_TYPE_DISCRETE;
					break;

				default:
					break;
			}

			/* Enumerate all other framesizes based on the type. */
			switch (v4l2_framesize.type)
			{
				case V4L2_FRMSIZE_TYPE_DISCRETE:
				{
					while (TRUE)
					{
						guint width = v4l2_framesize.discrete.width;
						guint height = v4l2_framesize.discrete.height;

						GST_DEBUG_OBJECT(self, 
							"got discrete frame size #%" G_GUINT32_FORMAT " with %" G_GUINT32_FORMAT " x %" G_GUINT32_FORMAT " pixels",
							(guint32)(v4l2_framesize.index),
							width, height
						);

						if ((width > 0) && (height > 0))
						{
							width = CLAMP(width, 1, G_MAXINT);
							height = CLAMP(height, 1, G_MAXINT);

							if (!fill_caps_with_probed_info(self, fd, probed_device_caps, width, height, imx_v4l2_format))
								goto error;
						}
						else
							GST_DEBUG_OBJECT(self, "skipping frame size since it contains 0 pixels");

						v4l2_framesize.index++;

						if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &v4l2_framesize) < 0)
						{
							if (errno == EINVAL)
							{
								GST_DEBUG_OBJECT(self, "no more framesizes to enumerate");
								num_framesizes = v4l2_framesize.index;
								break;
							}
							else
							{
								GST_ERROR_OBJECT(self, "error while enumerating discrete frame sizes: %s (%d)", strerror(errno), errno);
								goto error;
							}
						}
					}

					break;
				}

				case V4L2_FRMSIZE_TYPE_STEPWISE:
				case V4L2_FRMSIZE_TYPE_CONTINUOUS:
				{
					gboolean is_stepwise = (v4l2_framesize.type == V4L2_FRMSIZE_TYPE_STEPWISE);
					guint min_width = 0, min_height = 0;
					guint max_width = 0, max_height = 0;
					guint width_step = 1, height_step = 1;

					min_width   = CLAMP(v4l2_framesize.stepwise.min_width,   1, G_MAXINT);
					min_height  = CLAMP(v4l2_framesize.stepwise.min_height,  1, G_MAXINT);
					max_width   = CLAMP(v4l2_framesize.stepwise.max_width,   1, G_MAXINT);
					max_height  = CLAMP(v4l2_framesize.stepwise.max_height,  1, G_MAXINT);

					if (is_stepwise)
					{
						width_step  = CLAMP(v4l2_framesize.stepwise.step_width,  1, G_MAXINT);
						height_step = CLAMP(v4l2_framesize.stepwise.step_height, 1, G_MAXINT);
					}

					GST_DEBUG_OBJECT(self, "got %s frame sizes", is_stepwise ? "step-wise" : "continuous");
					GST_DEBUG_OBJECT(self, "min width/height: %" G_GUINT32_FORMAT "/%" G_GUINT32_FORMAT, min_width, min_height);
					GST_DEBUG_OBJECT(self, "max width/height: %" G_GUINT32_FORMAT "/%" G_GUINT32_FORMAT, max_width, max_height);
					if (is_stepwise)
						GST_DEBUG_OBJECT(self, "width/height step sizes: %" G_GUINT32_FORMAT "/%" G_GUINT32_FORMAT, width_step, height_step);

					if (!fill_caps_with_probed_info(self, fd, probed_device_caps, max_width, max_height, imx_v4l2_format))
						goto error;

					break;
				}

				default:
					GST_ERROR_OBJECT(self, "got unknown size type %" G_GUINT32_FORMAT " while trying to get frame sizes for V4L2 pixelformat %" GST_FOURCC_FORMAT, (guint32)(v4l2_framesize.type), GST_FOURCC_ARGS(v4l2_format_desc.pixelformat));
					goto error;
			}
		}
	}

	/* Fill the num_chip_specific_frame_sizes array. We insert the frame
	 * sizes in the order they are enumerated. The VIDIOC_ENUM_FRAMESIZES
	 * result with v4l2_framesize.index set to 0 is placed in entry #0 in
	 * the array. v4l2_framesize.index 1 -> result goes in entry #1 etc.
	 * This is important to be able to know what to set the value of
	 * v4l2_captureparm's capturemode field to when initializing a V4L2
	 * capture device (it must be set to the index in this array that has
	 * the format the device shall use for capturing frames). */
	switch (probe_result->capture_chip)
	{
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5640:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5645_MIPI:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5647:
		{
			gint i;

			probe_result->chip_specific_frame_sizes = g_malloc(num_framesizes * sizeof(GstImxV4L2EnumeratedFrameSize));
			probe_result->num_chip_specific_frame_sizes = num_framesizes;

			for (i = 0; i < num_framesizes; ++i)
			{
				/* We have to set the pixel format to UYVY in order
				 * for the ioctl to succeed (this is mxc_v4l2 specific). */
				memset(&v4l2_framesize, 0, sizeof(v4l2_framesize));
				v4l2_framesize.index = i;
				v4l2_framesize.pixel_format = V4L2_PIX_FMT_UYVY;

				if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &v4l2_framesize) < 0)
				{
					GST_ERROR_OBJECT(self, "could not enumerate frame sizes for pixel format %" GST_FOURCC_FORMAT ": %s (%d)", GST_FOURCC_ARGS(v4l2_format_desc.pixelformat), strerror(errno), errno);
					goto error;
				}

				probe_result->chip_specific_frame_sizes[i].width = v4l2_framesize.discrete.width;
				probe_result->chip_specific_frame_sizes[i].height = v4l2_framesize.discrete.height;
			}

			break;
		}

		default:
			break;
	}

finish:
	if (probed_device_caps != NULL)
		probed_device_caps = gst_caps_simplify(probed_device_caps);
	probe_result->device_caps = probed_device_caps;
	return retval;

error:
	gst_imx_v4l2_clear_probe_result(probe_result);
	retval = FALSE;
	goto finish;
}


static void get_gst_framerate_from_v4l2_frameinterval(guint32 v4l2_num, guint32 v4l2_denom, gint *num, gint *denom)
{
	g_assert(num != NULL);
	g_assert(denom != NULL);

	if ((v4l2_num > G_MAXINT) || (v4l2_denom > G_MAXINT))
	{
		v4l2_num >>= 1;
		v4l2_denom >>= 1;
	}

	// V4L2 defines "frame intervals", which are the inverse
	// of frame rates. As a result, we have to switch the
	// numerator and denominator to get a frame rate.
	*num = v4l2_denom;
	*denom = v4l2_num;
}


static gboolean fill_caps_with_probed_info(GstImxV4L2Context *self, int fd, GstCaps *probed_device_caps, guint width, guint height, GstImxV4L2VideoFormat const *imx_v4l2_format)
{
	gboolean retval = TRUE;
	GstStructure *structure;
	gchar const *media_type_str;
	gchar const *format_str;
	GstImxV4L2ProbeResult *probe_result = &(self->probe_result);

	media_type_str = gst_imx_v4l2_get_media_type_for_format(imx_v4l2_format);
	g_assert(media_type_str != NULL);

	switch (imx_v4l2_format->type)
	{
		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			format_str = gst_video_format_to_string(imx_v4l2_format->format.gst_format);
			break;

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			format_str = gst_imx_v4l2_bayer_format_to_string(imx_v4l2_format->format.bayer_format);
			break;

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
			format_str = NULL;
			break;

		default:
			g_assert_not_reached();
	}

	structure = gst_structure_new(
		media_type_str,
		"width", G_TYPE_INT, (gint)width,
		"height", G_TYPE_INT, (gint)height,
		NULL
	);

	if (format_str != NULL)
		gst_structure_set(structure, "format", G_TYPE_STRING, format_str, NULL);

	switch (probe_result->capture_chip)
	{
		case GST_IMX_V4L2_CAPTURE_CHIP_UNRECOGNIZED_MXC_V4L2_BASED:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5640:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5645_MIPI:
		case GST_IMX_V4L2_CAPTURE_CHIP_OV5647:
		{
			gboolean can_handle_30fps = TRUE;

			/* The VIDIOC_ENUM_FRAMEINTERVALS implementation
			 * in the mxc_v4l2 driver is utterly broken. Fortunately,
			 * all sensors that are operated by that driver have the
			 * same list of available frame rates, so work around
			 * the broken implementation by manually specifying the
			 * frame rates here. */

			GValue framerates_gvalue = G_VALUE_INIT;
			GValue framerate_gvalue = G_VALUE_INIT;

			GST_DEBUG_OBJECT(self, "using hard coded mxv_v4l2 framerate as workaround for driver bug");

			g_value_init(&framerates_gvalue, GST_TYPE_LIST);
			g_value_init(&framerate_gvalue, GST_TYPE_FRACTION);

			switch (probe_result->capture_chip)
			{
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5640:
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI:
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5645_MIPI:
				case GST_IMX_V4L2_CAPTURE_CHIP_OV5647:
				{
					if (width == 2592)
					{
						/* The ov564x driver cannot handle 30 fps capture
						 * when the 2592 x 1944 resolution is selected. */
						can_handle_30fps = FALSE;
					}
					else if ((probe_result->capture_chip == GST_IMX_V4L2_CAPTURE_CHIP_OV5640) && (width == 1920))
					{
						/* The non-MIPI ov5640 driver cannot handle 30 fps capture
						 * when the 1920 x 1080 resolution is selected. */
						can_handle_30fps = FALSE;
					}
					break;
				}

				default:
					break;
			}

			if (can_handle_30fps)
			{
				gst_value_set_fraction(&framerate_gvalue, 30, 1);
				gst_value_list_append_value(&framerates_gvalue, &framerate_gvalue);
			}

			gst_value_set_fraction(&framerate_gvalue, 15, 1);
			gst_value_list_append_value(&framerates_gvalue, &framerate_gvalue);

			gst_structure_set_value(structure, "framerate", &framerates_gvalue);

			g_value_unset(&framerates_gvalue);
			g_value_unset(&framerate_gvalue);

			break;
		}

		default:
		{
			struct v4l2_frmivalenum v4l2_frame_interval;

			memset(&v4l2_frame_interval, 0, sizeof(v4l2_frame_interval));
			v4l2_frame_interval.index = 0;
			v4l2_frame_interval.pixel_format = imx_v4l2_format->v4l2_pixelformat;
			v4l2_frame_interval.width = width;
			v4l2_frame_interval.height = height;

			if (G_UNLIKELY(ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &v4l2_frame_interval) < 0))
			{
				GST_ERROR_OBJECT(self, "could not enumerate frame intervals: %s (%d)", strerror(errno), errno);
				goto error;
			}

			switch (v4l2_frame_interval.type)
			{
				case V4L2_FRMIVAL_TYPE_DISCRETE:
				{
					gint fps_num, fps_denom;
					GValue framerates_gvalue = G_VALUE_INIT;
					GValue framerate_gvalue = G_VALUE_INIT;
					gboolean error_occurred = FALSE;

					g_value_init(&framerates_gvalue, GST_TYPE_LIST);
					g_value_init(&framerate_gvalue, GST_TYPE_FRACTION);

					while (TRUE)
					{
						get_gst_framerate_from_v4l2_frameinterval(
							v4l2_frame_interval.discrete.numerator, v4l2_frame_interval.discrete.denominator,
							&fps_num, &fps_denom
						);

						GST_DEBUG_OBJECT(
							self,
							"got discrete frame interval #%" G_GUINT32_FORMAT " with frame rate %d/%d",
							(guint32)(v4l2_frame_interval.index),
							fps_num, fps_denom
						);

						gst_value_set_fraction(&framerate_gvalue, fps_num, fps_denom);
						gst_value_list_append_value(&framerates_gvalue, &framerate_gvalue);

						if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &v4l2_frame_interval) < 0)
						{
							if (errno == EINVAL)
							{
								GST_DEBUG_OBJECT(self, "no more frame intervals to enumerate");
								break;
							}
							else
							{
								GST_ERROR_OBJECT(self, "error while enumerating discrete frame intervals: %s (%d)", strerror(errno), errno);
								error_occurred = TRUE;
								break;
							}
						}
						v4l2_frame_interval.index++;
					}

					gst_structure_set_value(structure, "framerate", &framerates_gvalue);

					g_value_unset(&framerates_gvalue);
					g_value_unset(&framerate_gvalue);

					if (error_occurred)
						goto error;

					break;
				}

				case V4L2_FRMIVAL_TYPE_STEPWISE:
				{
					GST_FIXME_OBJECT(self, "stepwise frame intervals are currently not supported");
					break;
				}

				case V4L2_FRMIVAL_TYPE_CONTINUOUS:
				{
					gint min_fps_num, min_fps_denom;
					gint max_fps_num, max_fps_denom;
					GValue framerate_gvalue = G_VALUE_INIT;

					g_value_init(&framerate_gvalue, GST_TYPE_FRACTION);

					/* Note that "min frame rate = max frame interval" and vice versa,
					 * because a frame rate is the inverse of a frame interval. */

					get_gst_framerate_from_v4l2_frameinterval(
						v4l2_frame_interval.stepwise.min.numerator, v4l2_frame_interval.stepwise.min.denominator,
						&max_fps_num, &max_fps_denom
					);

					get_gst_framerate_from_v4l2_frameinterval(
						v4l2_frame_interval.stepwise.max.numerator, v4l2_frame_interval.stepwise.max.denominator,
						&min_fps_num, &min_fps_denom
					);

					GST_DEBUG_OBJECT(
						self,
						"got continuous frame interval from frame rate %d/%d to frame rate %d/%d",
						min_fps_num, min_fps_denom,
						max_fps_num, max_fps_denom
					);

					gst_value_set_fraction_range_full(&framerate_gvalue, min_fps_num, min_fps_denom, max_fps_num, max_fps_denom);

					gst_structure_set_value(structure, "framerate", &framerate_gvalue);

					g_value_unset(&framerate_gvalue);

					break;
				}

				default:
					GST_ERROR_OBJECT(self, "got unknown size type %" G_GUINT32_FORMAT " while trying to get frame sizes for V4L2 pixelformat %" GST_FOURCC_FORMAT " and width/height %u/%u", (guint32)(v4l2_frame_interval.type), GST_FOURCC_ARGS(imx_v4l2_format->v4l2_pixelformat), width, height);
					goto error;
			}
		}
	}

	gst_caps_append_structure(probed_device_caps, structure);
	structure = NULL;


finish:
	return retval;

error:
	gst_structure_free(structure);

	retval = FALSE;
	goto finish;
}


static void log_capabilities(GstObject *object, guint32 capabilities)
{
	if ((capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0)        GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_CAPTURE");
	if ((capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0) GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_CAPTURE_MPLANE");
	if ((capabilities & V4L2_CAP_VIDEO_OUTPUT) != 0)         GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_OUTPUT");
	if ((capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) != 0)  GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_OUTPUT_MPLANE");
	if ((capabilities & V4L2_CAP_VIDEO_M2M) != 0)            GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_M2M");
	if ((capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) != 0)     GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_M2M_MPLANE");
	if ((capabilities & V4L2_CAP_VIDEO_OVERLAY) != 0)        GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_OVERLAY");
	if ((capabilities & V4L2_CAP_VBI_CAPTURE) != 0)          GST_DEBUG_OBJECT(object, "    V4L2_CAP_VBI_CAPTURE");
	if ((capabilities & V4L2_CAP_VBI_OUTPUT) != 0)           GST_DEBUG_OBJECT(object, "    V4L2_CAP_VBI_OUTPUT");
	if ((capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) != 0)   GST_DEBUG_OBJECT(object, "    V4L2_CAP_SLICED_VBI_CAPTURE");
	if ((capabilities & V4L2_CAP_SLICED_VBI_OUTPUT) != 0)    GST_DEBUG_OBJECT(object, "    V4L2_CAP_SLICED_VBI_OUTPUT");
	if ((capabilities & V4L2_CAP_RDS_CAPTURE) != 0)          GST_DEBUG_OBJECT(object, "    V4L2_CAP_RDS_CAPTURE");
	if ((capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY) != 0) GST_DEBUG_OBJECT(object, "    V4L2_CAP_VIDEO_OUTPUT_OVERLAY");
	if ((capabilities & V4L2_CAP_HW_FREQ_SEEK) != 0)         GST_DEBUG_OBJECT(object, "    V4L2_CAP_HW_FREQ_SEEK");
	if ((capabilities & V4L2_CAP_RDS_OUTPUT) != 0)           GST_DEBUG_OBJECT(object, "    V4L2_CAP_RDS_OUTPUT");
	if ((capabilities & V4L2_CAP_TUNER) != 0)                GST_DEBUG_OBJECT(object, "    V4L2_CAP_TUNER");
	if ((capabilities & V4L2_CAP_AUDIO) != 0)                GST_DEBUG_OBJECT(object, "    V4L2_CAP_AUDIO");
	if ((capabilities & V4L2_CAP_RADIO) != 0)                GST_DEBUG_OBJECT(object, "    V4L2_CAP_RADIO");
	if ((capabilities & V4L2_CAP_MODULATOR) != 0)            GST_DEBUG_OBJECT(object, "    V4L2_CAP_MODULATOR");
	if ((capabilities & V4L2_CAP_SDR_CAPTURE) != 0)          GST_DEBUG_OBJECT(object, "    V4L2_CAP_SDR_CAPTURE");
	if ((capabilities & V4L2_CAP_EXT_PIX_FORMAT) != 0)       GST_DEBUG_OBJECT(object, "    V4L2_CAP_EXT_PIX_FORMAT");
#ifdef V4L2_CAP_SDR_OUTPUT
	if ((capabilities & V4L2_CAP_SDR_OUTPUT) != 0)           GST_DEBUG_OBJECT(object, "    V4L2_CAP_SDR_OUTPUT");
#endif
#ifdef V4L2_CAP_META_CAPTURE
	if ((capabilities & V4L2_CAP_META_CAPTURE) != 0)         GST_DEBUG_OBJECT(object, "    V4L2_CAP_META_CAPTURE");
#endif
	if ((capabilities & V4L2_CAP_READWRITE) != 0)            GST_DEBUG_OBJECT(object, "    V4L2_CAP_READWRITE");
	if ((capabilities & V4L2_CAP_ASYNCIO) != 0)              GST_DEBUG_OBJECT(object, "    V4L2_CAP_ASYNCIO");
	if ((capabilities & V4L2_CAP_STREAMING) != 0)            GST_DEBUG_OBJECT(object, "    V4L2_CAP_STREAMING");
#ifdef V4L2_CAP_META_OUTPUT
	if ((capabilities & V4L2_CAP_META_OUTPUT) != 0)          GST_DEBUG_OBJECT(object, "    V4L2_CAP_META_OUTPUT");
#endif
#ifdef V4L2_CAP_TOUCH
	if ((capabilities & V4L2_CAP_TOUCH) != 0)                GST_DEBUG_OBJECT(object, "    V4L2_CAP_TOUCH");
#endif
	if ((capabilities & V4L2_CAP_DEVICE_CAPS) != 0)          GST_DEBUG_OBJECT(object, "    V4L2_CAP_DEVICE_CAPS");
}
