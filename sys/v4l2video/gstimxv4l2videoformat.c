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

#include <time.h>
#include <linux/videodev2.h>
#include "gstimxv4l2videoformat.h"


GST_DEBUG_CATEGORY_EXTERN(imx_v4l2_format_debug);
#define GST_CAT_DEFAULT imx_v4l2_format_debug


GstImxV4L2BayerFormat gst_imx_v4l2_bayer_format_from_string(gchar const *str)
{
	g_assert(str != NULL);

	if      (g_strcmp0(str, "rggb") == 0) return GST_IMX_V4L2_BAYER_FORMAT_RGGB;
	else if (g_strcmp0(str, "grbg") == 0) return GST_IMX_V4L2_BAYER_FORMAT_GRBG;
	else if (g_strcmp0(str, "gbrg") == 0) return GST_IMX_V4L2_BAYER_FORMAT_GBRG;
	else if (g_strcmp0(str, "bggr") == 0) return GST_IMX_V4L2_BAYER_FORMAT_BGGR;
	else return GST_IMX_V4L2_BAYER_FORMAT_UNKNOWN;
}


gchar const * gst_imx_v4l2_bayer_format_to_string(GstImxV4L2BayerFormat bayer_format)
{
	switch (bayer_format)
	{
		case GST_IMX_V4L2_BAYER_FORMAT_RGGB: return "rggb";
		case GST_IMX_V4L2_BAYER_FORMAT_GRBG: return "grbg";
		case GST_IMX_V4L2_BAYER_FORMAT_GBRG: return "gbrg";
		case GST_IMX_V4L2_BAYER_FORMAT_BGGR: return "bggr";
		default: return NULL;
	}
}


GstImxV4L2CodecFormat gst_imx_v4l2_codec_format_from_media_type(gchar const *media_type)
{
	g_assert(media_type != NULL);

	if (g_strcmp0(media_type, "image/jpeg") == 0) return GST_IMX_V4L2_CODEC_FORMAT_JPEG;
	else return GST_IMX_V4L2_CODEC_FORMAT_UNKNOWN;
}


gchar const * gst_imx_v4l2_codec_format_to_media_type(GstImxV4L2CodecFormat codec_format)
{
	switch (codec_format)
	{
		case GST_IMX_V4L2_CODEC_FORMAT_JPEG: return "image/jpeg";
		default: return NULL;
	}
}


gchar const * gst_imx_v4l2_video_format_to_string(GstImxV4L2VideoFormat const *format)
{
	g_assert(format != NULL);

	switch (format->type)
	{
		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			return gst_video_format_to_string(format->format.gst_format);

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			return gst_imx_v4l2_bayer_format_to_string(format->format.bayer_format);

		default:
			return NULL;
	}
}


static GstImxV4L2VideoFormat const gst_imxv4l2_video_formats[] =
{
	{ V4L2_PIX_FMT_NV12, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV12 } },
	{ V4L2_PIX_FMT_NV12M, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV12 } },
	{ V4L2_PIX_FMT_NV12MT, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV12_64Z32 } },
	{ V4L2_PIX_FMT_NV21, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV21 } },
	{ V4L2_PIX_FMT_NV21M, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV21 } },

	{ V4L2_PIX_FMT_NV16, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV16 } },
#ifdef V4L2_PIX_FMT_NV16M
	{ V4L2_PIX_FMT_NV16M, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV16 } },
#endif
#ifdef GST_VIDEO_FORMAT_NV61
	{ V4L2_PIX_FMT_NV61, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV61 } },
#endif
#ifdef V4L2_PIX_FMT_NV61M
	{ V4L2_PIX_FMT_NV61M, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV61 } },
#endif

	{ V4L2_PIX_FMT_NV24, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_NV24 } },

	{ V4L2_PIX_FMT_YUV420, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_I420 } },
	{ V4L2_PIX_FMT_YUV420M, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_I420 } },
	{ V4L2_PIX_FMT_YVU420, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_YV12 } },

	{ V4L2_PIX_FMT_YUV422P, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_Y42B } },

	{ V4L2_PIX_FMT_YVU410, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_YVU9 } },
	{ V4L2_PIX_FMT_YUV410, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_YUV9 } },
	{ V4L2_PIX_FMT_YUV411P, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_Y41B } },

	{ V4L2_PIX_FMT_UYVY, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_UYVY } },
	{ V4L2_PIX_FMT_YUYV, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_YUY2 } },
	{ V4L2_PIX_FMT_YVYU, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_YVYU } },

#ifdef V4L2_PIX_FMT_XRGB32
	{ V4L2_PIX_FMT_XRGB32, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_xRGB } },
#endif
	{ V4L2_PIX_FMT_RGB32, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_xRGB } },
#ifdef V4L2_PIX_FMT_XBGR32
	{ V4L2_PIX_FMT_XBGR32, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_BGRx } },
#endif
	{ V4L2_PIX_FMT_BGR32, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_BGRx } },
#ifdef V4L2_PIX_FMT_ABGR32
	{ V4L2_PIX_FMT_ABGR32, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_BGRA } },
#endif
#ifdef V4L2_PIX_FMT_ARGB32
	{ V4L2_PIX_FMT_ARGB32, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_ARGB } },
#endif

	{ V4L2_PIX_FMT_RGB24, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_RGB } },
	{ V4L2_PIX_FMT_BGR24, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_BGR } },

#ifdef V4L2_PIX_FMT_XRGB555
	{ V4L2_PIX_FMT_XRGB555, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_RGB15 } },
#endif
	{ V4L2_PIX_FMT_RGB555, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_RGB15 } },
#ifdef V4L2_PIX_FMT_XRGB555X
	{ V4L2_PIX_FMT_XRGB555X, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_BGR15 } },
#endif
	{ V4L2_PIX_FMT_RGB555X, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_BGR15 } },
	{ V4L2_PIX_FMT_RGB565, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_RGB16 } },

	{ V4L2_PIX_FMT_GREY, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_GRAY8 } },
	{ V4L2_PIX_FMT_Y16, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_GRAY16_LE } },
#ifdef V4L2_PIX_FMT_Y16_BE
	{ V4L2_PIX_FMT_Y16_BE, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW, { .gst_format = GST_VIDEO_FORMAT_GRAY16_BE } },
#endif

	{ V4L2_PIX_FMT_SRGGB8, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER, { .bayer_format = GST_IMX_V4L2_BAYER_FORMAT_RGGB } },
	{ V4L2_PIX_FMT_SGRBG8, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER, { .bayer_format = GST_IMX_V4L2_BAYER_FORMAT_GRBG } },
	{ V4L2_PIX_FMT_SGBRG8, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER, { .bayer_format = GST_IMX_V4L2_BAYER_FORMAT_GBRG } },
	{ V4L2_PIX_FMT_SBGGR8, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER, { .bayer_format = GST_IMX_V4L2_BAYER_FORMAT_BGGR } },


	{ V4L2_PIX_FMT_MJPEG, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC, { .codec_format = GST_IMX_V4L2_CODEC_FORMAT_JPEG } },
	{ V4L2_PIX_FMT_JPEG, GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC, { .codec_format = GST_IMX_V4L2_CODEC_FORMAT_JPEG } }
};


static gint const num_gst_imxv4l2_video_formats = sizeof(gst_imxv4l2_video_formats) / sizeof(GstImxV4L2VideoFormat);


GstImxV4L2VideoFormat const * gst_imx_v4l2_get_video_formats()
{
	return gst_imxv4l2_video_formats;
}


gsize gst_imx_v4l2_get_num_video_formats()
{
	return num_gst_imxv4l2_video_formats;
}


GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_gst_video_format(GstVideoFormat gst_format)
{
	gint i;

	for (i = 0; i < num_gst_imxv4l2_video_formats; ++i)
	{
		GstImxV4L2VideoFormat const *format = &(gst_imxv4l2_video_formats[i]);
		if ((format->type == GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW) && (format->format.gst_format == gst_format))
			return format;
	}

	return NULL;
}


GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_bayer_video_format(GstImxV4L2BayerFormat bayer_format)
{
	gint i;

	for (i = 0; i < num_gst_imxv4l2_video_formats; ++i)
	{
		GstImxV4L2VideoFormat const *format = &(gst_imxv4l2_video_formats[i]);
		if ((format->type == GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER) && (format->format.bayer_format == bayer_format))
			return format;
	}

	return NULL;
}


GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_codec_video_format(GstImxV4L2CodecFormat codec_format)
{
	gint i;

	for (i = 0; i < num_gst_imxv4l2_video_formats; ++i)
	{
		GstImxV4L2VideoFormat const *format = &(gst_imxv4l2_video_formats[i]);
		if ((format->type == GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC) && (format->format.codec_format == codec_format))
			return format;
	}

	return NULL;
}


GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_v4l2_pixelformat(guint32 v4l2_pixelformat)
{
	gint i;

	for (i = 0; i < num_gst_imxv4l2_video_formats; ++i)
	{
		GstImxV4L2VideoFormat const *format = &(gst_imxv4l2_video_formats[i]);
		if (format->v4l2_pixelformat == v4l2_pixelformat)
			return format;
	}

	return NULL;
}


gboolean gst_imx_v4l2_video_info_from_caps(GstImxV4L2VideoInfo *info, GstCaps const *caps)
{
	gchar const *media_type;
	GstStructure *structure;

	g_assert(info != NULL);
	g_assert(caps != NULL);
	g_assert(gst_caps_is_fixed(caps));

	structure = gst_caps_get_structure(caps, 0);
	media_type = gst_structure_get_name(structure);

	if (g_strcmp0(media_type, "video/x-raw") == 0)
	{
		info->type = GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW;
		return gst_video_info_from_caps(&(info->info.gst_info), caps);
	}
	else if (g_strcmp0(media_type, "video/x-bayer") == 0)
	{
		GstImxV4L2BayerInfo *bayer_info = &(info->info.bayer_info);
		gchar const *format_str;

		if (((format_str = gst_structure_get_string(structure, "format")) == NULL)
		 || !gst_structure_get_int(structure, "width", &(bayer_info->width))
		 || !gst_structure_get_int(structure, "height", &(bayer_info->height))
		 || !gst_structure_get_fraction(structure, "framerate", &(bayer_info->fps_n), &(bayer_info->fps_d)))
		{
			GST_ERROR("Could not convert caps %" GST_PTR_FORMAT " to GstImxV4L2BayerInfo", (gpointer)caps);
			return FALSE;
		}

		if ((bayer_info->format = gst_imx_v4l2_bayer_format_from_string(format_str)) == GST_IMX_V4L2_BAYER_FORMAT_UNKNOWN)
		{
			GST_ERROR("Could not convert format string %s to GstImxV4L2BayerFormat", format_str);
			return FALSE;
		}

		info->type = GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER;

		return TRUE;
	}
	else
	{
		gint i;
		GstImxV4L2CodecInfo *codec_info = &(info->info.codec_info);

		if (!gst_structure_get_int(structure, "width", &(codec_info->width))
		 || !gst_structure_get_int(structure, "height", &(codec_info->height))
		 || !gst_structure_get_fraction(structure, "framerate", &(codec_info->fps_n), &(codec_info->fps_d)))
		{
			GST_ERROR("Could not convert caps %" GST_PTR_FORMAT " to GstImxV4L2CodecInfo", (gpointer)caps);
			return FALSE;
		}

		for (i = 0; i < GST_IMX_V4L2_NUM_CODEC_FORMATS; ++i)
		{
			gchar const *codec_media_type = gst_imx_v4l2_codec_format_to_media_type((GstImxV4L2CodecFormat)i);
			if (g_strcmp0(media_type, codec_media_type) == 0)
			{
				codec_info->format = (GstImxV4L2CodecFormat)i;
				info->type = GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC;
				return TRUE;
			}
		}

		GST_ERROR("Unsupported media type \"%s\"", media_type);
		return FALSE;
	}

	return FALSE;
}


GstCaps* gst_imx_v4l2_video_info_to_caps(GstImxV4L2VideoInfo *info)
{
	GstCaps *caps = NULL;

	g_assert(info != NULL);

	switch (info->type)
	{
		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
		{
			caps = gst_video_info_to_caps(&(info->info.gst_info));
			g_assert(caps != NULL);
			break;
		}

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
		{
			caps = gst_caps_new_simple(
				"video/x-bayer",
				"format", G_TYPE_STRING, gst_imx_v4l2_bayer_format_to_string(info->info.bayer_info.format),
				"width", G_TYPE_INT, info->info.bayer_info.width,
				"height", G_TYPE_INT, info->info.bayer_info.height,
				"framerate", GST_TYPE_FRACTION, info->info.bayer_info.fps_n, info->info.bayer_info.fps_d,
				NULL
			);
			break;
		}

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
		{
			caps = gst_caps_new_simple(
				gst_imx_v4l2_codec_format_to_media_type(info->info.codec_info.format),
				"width", G_TYPE_INT, info->info.bayer_info.width,
				"height", G_TYPE_INT, info->info.bayer_info.height,
				"framerate", GST_TYPE_FRACTION, info->info.bayer_info.fps_n, info->info.bayer_info.fps_d,
				NULL
			);
			break;
		}

		default:
			GST_ERROR("Unknown GstImxV4L2VideoInfo type %d", (gint)(info->type));
			break;
	}

	return caps;
}


gchar const * gst_imx_v4l2_get_media_type_for_format(GstImxV4L2VideoFormat const *format)
{
	g_assert(format != NULL);

	switch (format->type)
	{
		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			return "video/x-raw";

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			return "video/x-bayer";

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
			return gst_imx_v4l2_codec_format_to_media_type(format->format.codec_format);

		default:
			return NULL;
	}
}


guint gst_imx_v4l2_calculate_buffer_size_from_video_info(GstImxV4L2VideoInfo const *info)
{
	guint buffer_size = 0;

	g_assert(info != NULL);

	switch (info->type)
	{
		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
		{
			buffer_size = GST_VIDEO_INFO_SIZE(&(info->info.gst_info));
			break;
		}

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
		{
			/* Bayer frames always contain 32 bits per pixel.
			 * These bits can be organized as RGGB, GRBG etc.
			 * tuples. See the GST_IMX_V4L2_BAYER_FORMAT_*
			 * enums for the whole list of such tuples. */
			buffer_size = info->info.bayer_info.width * info->info.bayer_info.height * 4;
			break;
		}

		case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
		{
			/* Codecs compress frames, and the compressed frame size
			 * can vary significantly, so figuring out one optimal
			 * fixed frame size just isn't viable. For this reason,
			 * we pick the worst case scenario instead, which is that
			 * the codec didn't compress at all, and that this is
			 * a 10-bit RGBx frame. */
			// XXX: Is there a better way?
			buffer_size = info->info.codec_info.width * info->info.codec_info.height * 4 * 10 / 8;
			break;
		}

		default:
			GST_ERROR("Unknown GstImxV4L2VideoInfo type %d", (gint)(info->type));
			break;
	}

	/* The mxc_v4l2 driver expects page aligned buffer sizes. */
	buffer_size = GST_IMX_V4L2_PAGE_ALIGN(buffer_size);

	return buffer_size;
}


GstCaps* gst_imx_v4l2_get_all_possible_caps(void)
{
	/* Here, we walk through the gst_imxv4l2_video_formats
	 * array and generate caps out of it. */

	GstCaps *caps;
	GstStructure *str;
	GValue raw_formats_gvalue_list = G_VALUE_INIT;
	GValue bayer_formats_gvalue_list = G_VALUE_INIT;
	GValue format_value = G_VALUE_INIT;
	GstVideoFormat last_raw_video_format = GST_VIDEO_FORMAT_UNKNOWN;
	gchar const *last_codec_media_type = NULL;
	GstCaps *all_codec_caps = NULL;

	caps = gst_caps_new_empty();
	all_codec_caps = gst_caps_new_empty();

	/* Raw and Bayer formats are stored in lists, so we
	 * need to initialize the corresponding GValues here. */
	g_value_init(&raw_formats_gvalue_list, GST_TYPE_LIST);
	g_value_init(&bayer_formats_gvalue_list, GST_TYPE_LIST);

	GST_DEBUG("going through all possible %d formats to create all possible caps", num_gst_imxv4l2_video_formats);

	/* Gather the formats and codec caps structures from
	 * walking through gst_imxv4l2_video_formats. The end
	 * result are in raw_formats_gvalue_list,
	 * bayer_formats_gvalue_list, and all_codec_caps. */
	for (gint format_index = 0; format_index < num_gst_imxv4l2_video_formats; ++format_index)
	{
		GstImxV4L2VideoFormat const *v4l2_video_format = &gst_imxv4l2_video_formats[format_index];

		switch (v4l2_video_format->type)
		{
			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW:
			{
				GstVideoFormat gst_raw_format = v4l2_video_format->format.gst_format;

				/* Several entries in the array above have different
				 * raw V4L2 formats but the same GstVideo format.
				 * GST_VIDEO_FORMAT_BGR15 is one example.
				 * Check for such cases - we do not want duplicate
				 * formats in our raw_formats_gvalue_list. */
				if (gst_raw_format == last_raw_video_format)
				{
					GST_DEBUG("format #%d is a raw format \"%s\" which was already observed - skipping duplicate", format_index, gst_video_format_to_string(gst_raw_format));
					continue;
				}

				/* Convert the GstVideo format to a string, and append it to the list. */
				g_value_init(&format_value, G_TYPE_STRING);
				g_value_set_string(&format_value, gst_video_format_to_string(gst_raw_format));
				gst_value_list_append_and_take_value(&raw_formats_gvalue_list, &format_value);

				GST_DEBUG("format #%d is a raw format \"%s\"", format_index, gst_video_format_to_string(gst_raw_format));

				last_raw_video_format = gst_raw_format;

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER:
			{
				gchar const *format_string = gst_imx_v4l2_bayer_format_to_string(v4l2_video_format->format.bayer_format);

				g_value_init(&format_value, G_TYPE_STRING);
				g_value_set_string(&format_value, format_string);
				gst_value_list_append_and_take_value(&bayer_formats_gvalue_list, &format_value);

				GST_DEBUG("format #%d is a Bayer format \"%s\"", format_index, format_string);

				break;
			}

			case GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC:
			{
				gchar const *media_type = gst_imx_v4l2_codec_format_to_media_type(v4l2_video_format->format.codec_format);

				if ((last_codec_media_type != NULL) && (g_strcmp0(last_codec_media_type, media_type) == 0))
				{
					GST_DEBUG("format #%d is a codec format with media type \"%s\" which was already observed - skipping duplicate", format_index, media_type);
					continue;
				}

				GST_DEBUG("format #%d is a codec format with media type \"%s\"", format_index, media_type);

				gst_caps_append(all_codec_caps, gst_caps_new_empty_simple(media_type));

				last_codec_media_type = media_type;

				break;
			}

			default:
				g_assert_not_reached();
		}
	}

	str = gst_structure_new(
		"video/x-raw",
		"width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
		"height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
		"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
		NULL
	);
	gst_structure_take_value(str, "format", &raw_formats_gvalue_list);
	gst_caps_append_structure(caps, str);

	str = gst_structure_new(
		"video/x-bayer",
		"width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
		"height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
		"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
		NULL
	);
	gst_structure_take_value(str, "format", &bayer_formats_gvalue_list);
	gst_caps_append_structure(caps, str);

	gst_caps_append(caps, all_codec_caps);

	GST_DEBUG("result: all possible caps: %" GST_PTR_FORMAT, (gpointer)caps);

	return caps;
}
