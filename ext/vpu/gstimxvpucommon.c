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

#include <config.h>
#include <stdarg.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <imxvpuapi2/imxvpuapi2.h>
#include "gstimxvpucommon.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_api_debug);
GST_DEBUG_CATEGORY_EXTERN(gst_imx_vpu_common_debug);


static gboolean gst_imx_vpu_h264_is_frame_reordering_required(GstStructure *format);


static GstImxVpuCodecDetails const decoder_details_table[NUM_IMX_VPU_API_COMPRESSION_FORMATS] =
{
	{ "jpeg",   "Jpeg",   "JPEG",                                              GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_JPEG,           NULL, FALSE },
	{ "webp",   "WebP",   "WebP",                                              GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_WEBP,           NULL, FALSE },
	{ "mpeg2",  "Mpeg2",  "MPEG-1 & 2",                                        GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_MPEG2,          NULL, TRUE  },
	{ "mpeg4",  "Mpeg4",  "MPEG-4",                                            GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_MPEG4,          NULL, TRUE  },
	{ "h263",   "H263",   "h.263",                                             GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_H263,           NULL, FALSE },
	{ "h264",   "H264",   "h.264 / AVC",                                       GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_H264,           gst_imx_vpu_h264_is_frame_reordering_required, FALSE },
	{ "h265",   "H265",   "h.265 / HEVC",                                      GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_H265,           NULL, FALSE },
	{ "wmv3",   "Wmv3",   "WMV3 / Window Media Video 9 / VC-1 simple profile", GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_WMV3,           NULL, TRUE  },
	{ "vc1",    "Vc1",    "VC-1 advanced profile",                             GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_WVC1,           NULL, TRUE  },
	{ "vp6",    "Vp6",    "VP6",                                               GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_VP6,            NULL, FALSE },
	{ "vp7",    "Vp7",    "VP7",                                               GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_VP7,            NULL, FALSE },
	{ "vp8",    "Vp8",    "VP8",                                               GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_VP8,            NULL, FALSE },
	{ "vp9",    "Vp9",    "VP9",                                               GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_VP9,            NULL, FALSE },
	{ "cavs",   "Avs",    "AVS (Audio and Video Coding Standard)",             GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_AVS,            NULL, FALSE },
	{ "rv30",   "Rv30",   "RealVideo 8",                                       GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_RV30,           NULL, TRUE  },
	{ "rv40",   "Rv40",   "RealVideo 9 & 10",                                  GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_RV40,           NULL, TRUE  },
	{ "divx3",  "DivX3" , "DivX 3",                                            GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_DIVX3,          NULL, FALSE },
	{ "divx4",  "DivX4",  "DivX 4",                                            GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_DIVX4,          NULL, FALSE },
	{ "divx5",  "DivX5",  "DivX 5 & 6",                                        GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_DIVX5,          NULL, FALSE },
	{ "sspark", "SSpark", "Sorenson Spark",                                    GST_RANK_PRIMARY + 1, IMX_VPU_API_COMPRESSION_FORMAT_SORENSON_SPARK, NULL, FALSE }
};


GQuark gst_imx_vpu_compression_format_quark(void)
{
	return g_quark_from_static_string("gst-imx-vpu-compression-format-quark");
}


GstImxVpuCodecDetails const * gst_imx_vpu_get_codec_details(ImxVpuApiCompressionFormat compression_format)
{
	g_assert(compression_format < NUM_IMX_VPU_API_COMPRESSION_FORMATS);
	return &(decoder_details_table[compression_format]);
}


gboolean gst_imx_vpu_get_caps_for_format(ImxVpuApiCompressionFormat compression_format, ImxVpuApiCompressionFormatSupportDetails const *details, GstCaps **encoded_caps, GstCaps **raw_caps, gboolean for_encoder)
{
	size_t i;
	GstStructure *structure = NULL;
	GValue list_value = G_VALUE_INIT;
	GValue string_value = G_VALUE_INIT;

	g_assert(details != NULL);
	g_assert(encoded_caps != NULL);
	g_assert(raw_caps != NULL);

	/* Encoded caps */
	{
		switch (compression_format)
		{
			case IMX_VPU_API_COMPRESSION_FORMAT_JPEG:
				structure = gst_structure_new(
					"image/jpeg",
					"parsed", G_TYPE_BOOLEAN, TRUE,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_WEBP:
				structure = gst_structure_new_empty("image/webp");
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_MPEG2:
				structure = gst_structure_new(
					"video/mpeg",
					"parsed", G_TYPE_BOOLEAN, TRUE,
					"systemstream", G_TYPE_BOOLEAN, FALSE,
					"mpegversion", GST_TYPE_INT_RANGE, 1, 2,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_MPEG4:
				structure = gst_structure_new(
					"video/mpeg",
					"parsed", G_TYPE_BOOLEAN, TRUE,
					"mpegversion", G_TYPE_INT, 4,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_H263:
				structure = gst_structure_new(
					"video/x-h263",
					"parsed", G_TYPE_BOOLEAN, TRUE,
					"variant", G_TYPE_STRING, "itu",
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_H264:
			{
				ImxVpuApiH264SupportDetails const *h264_support_details = (ImxVpuApiH264SupportDetails const *)details;
				GValue list_value = G_VALUE_INIT;
				GValue string_value = G_VALUE_INIT;

				structure = gst_structure_new(
					"video/x-h264",
					"parsed", G_TYPE_BOOLEAN, TRUE,
					"stream-format", G_TYPE_STRING, "byte-stream",
					NULL
				);

				{
					g_value_init(&list_value, GST_TYPE_LIST);
					g_value_init(&string_value, G_TYPE_STRING);

					/* Add au alignment. All known i.MX decoders support access units. */
					if (h264_support_details->flags & IMX_VPU_API_H264_FLAG_ACCESS_UNITS_SUPPORTED)
					{
						g_value_set_static_string(&string_value, "au");
						gst_value_list_append_value(&list_value, &string_value);
					}

					/* Only add nal alignment to encoders. That's because nal alignment
					 * does not guarantee that upstream delivers complete h.264 frames.
					 * In case of a decoder, complete frames are a requirement, so we
					 * disable nal alignment in decoders to always meet that requirement. */
					if (for_encoder)
					{
						if (!(h264_support_details->flags & IMX_VPU_API_H264_FLAG_ACCESS_UNITS_REQUIRED))
						{
							g_value_set_static_string(&string_value, "nal");
							gst_value_list_append_value(&list_value, &string_value);
						}
					}

					gst_structure_set_value(structure, "alignment", &list_value);

					g_value_unset(&list_value);
					g_value_unset(&string_value);
				}

				{
					g_value_init(&list_value, GST_TYPE_LIST);
					g_value_init(&string_value, G_TYPE_STRING);

#define CHECK_AND_ADD_H264_PROFILE(PROFILE_LEVEL_FIELD, PROFILE_NAME) \
					if (h264_support_details->PROFILE_LEVEL_FIELD != IMX_VPU_API_H264_LEVEL_UNDEFINED) \
					{ \
						g_value_set_static_string(&string_value, PROFILE_NAME); \
						gst_value_list_append_value(&list_value, &string_value); \
					}

					// TODO: put the profile strings into constants to keep them in one central place
					CHECK_AND_ADD_H264_PROFILE(max_constrained_baseline_profile_level, "constrained-baseline");
					CHECK_AND_ADD_H264_PROFILE(max_baseline_profile_level, "baseline");
					CHECK_AND_ADD_H264_PROFILE(max_main_profile_level, "main");
					CHECK_AND_ADD_H264_PROFILE(max_high_profile_level, "high");
					CHECK_AND_ADD_H264_PROFILE(max_high10_profile_level, "high-10");

#undef CHECK_AND_ADD_H264_PROFILE

					gst_structure_set_value(structure, "profile", &list_value);

					g_value_unset(&list_value);
					g_value_unset(&string_value);
				}

				break;
			}

			case IMX_VPU_API_COMPRESSION_FORMAT_H265:
			{
				ImxVpuApiH265SupportDetails const *h265_support_details = (ImxVpuApiH265SupportDetails const *)details;
				GValue list_value = G_VALUE_INIT;
				GValue string_value = G_VALUE_INIT;

				structure = gst_structure_new(
					"video/x-h265",
					"parsed", G_TYPE_BOOLEAN, TRUE,
					"stream-format", G_TYPE_STRING, "byte-stream",
					NULL
				);

				{
					g_value_init(&list_value, GST_TYPE_LIST);
					g_value_init(&string_value, G_TYPE_STRING);

					if (h265_support_details->flags & IMX_VPU_API_H265_FLAG_ACCESS_UNITS_SUPPORTED)
					{
						g_value_set_static_string(&string_value, "au");
						gst_value_list_append_value(&list_value, &string_value);
					}

					if (!(h265_support_details->flags & IMX_VPU_API_H265_FLAG_ACCESS_UNITS_REQUIRED))
					{
						g_value_set_static_string(&string_value, "nal");
						gst_value_list_append_value(&list_value, &string_value);
					}

					gst_structure_set_value(structure, "alignment", &list_value);

					g_value_unset(&list_value);
					g_value_unset(&string_value);
				}

				{
					g_value_init(&list_value, GST_TYPE_LIST);
					g_value_init(&string_value, G_TYPE_STRING);

#define CHECK_AND_ADD_H265_PROFILE(PROFILE_LEVEL_FIELD, PROFILE_NAME) \
					if (h265_support_details->PROFILE_LEVEL_FIELD != IMX_VPU_API_H265_LEVEL_UNDEFINED) \
					{ \
						g_value_set_static_string(&string_value, PROFILE_NAME); \
						gst_value_list_append_value(&list_value, &string_value); \
					}

					CHECK_AND_ADD_H265_PROFILE(max_main_profile_level, "main");
					CHECK_AND_ADD_H265_PROFILE(max_main10_profile_level, "main-10");

#undef CHECK_AND_ADD_H265_PROFILE

					gst_structure_set_value(structure, "profile", &list_value);

					g_value_unset(&list_value);
					g_value_unset(&string_value);
				}

				break;
			}

			case IMX_VPU_API_COMPRESSION_FORMAT_WMV3:
				structure = gst_structure_new(
					"video/x-wmv",
					"wmvversion", G_TYPE_INT, 3,
					"format", G_TYPE_STRING, "WMV3",
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_WVC1:
				structure = gst_structure_new(
					"video/x-wmv",
					"wmvversion", G_TYPE_INT, 3,
					"format", G_TYPE_STRING, "WVC1",
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_VP6:
				structure = gst_structure_new_empty("video/x-vp6");
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_VP7:
				structure = gst_structure_new_empty("video/x-vp7");
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_VP8:
			{
				structure = gst_structure_new_empty("video/x-vp8");

				if (for_encoder)
				{
					gchar const **profile_string;
					gchar const *profile_strings[] = { "0", "1", "2", "3", NULL };
					GValue value = G_VALUE_INIT;
					GValue profile_list_value = G_VALUE_INIT;

					g_value_init(&profile_list_value, GST_TYPE_LIST);
					for (profile_string = profile_strings; (*profile_string) != NULL; ++profile_string)
					{
						g_value_init(&value, G_TYPE_STRING);
						g_value_set_string(&value, *profile_string);
						gst_value_list_append_value(&profile_list_value, &value);
						g_value_unset(&value);
					}
					gst_structure_set_value(structure, "profile", &profile_list_value);
					g_value_unset(&profile_list_value);
				}

				break;
			}

			case IMX_VPU_API_COMPRESSION_FORMAT_VP9:
				structure = gst_structure_new_empty("video/x-vp9");
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_AVS:
				structure = gst_structure_new_empty("video/x-cavs");
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_RV30:
				structure = gst_structure_new(
					"video/x-pn-realvideo",
					"rmversion", G_TYPE_INT, 3,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_RV40:
				structure = gst_structure_new(
					"video/x-pn-realvideo",
					"rmversion", G_TYPE_INT, 4,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_DIVX3:
				structure = gst_structure_new(
					"video/x-divx",
					"divxversion", G_TYPE_INT, 3,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_DIVX4:
				structure = gst_structure_new(
					"video/x-divx",
					"divxversion", G_TYPE_INT, 4,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_DIVX5:
				structure = gst_structure_new(
					"video/x-divx",
					"divxversion", G_TYPE_INT, 5,
					NULL
				);
				break;

			case IMX_VPU_API_COMPRESSION_FORMAT_SORENSON_SPARK:
				structure = gst_structure_new(
					"video/x-flash-video",
					"flvversion", G_TYPE_INT, 1,
					NULL
				);
				break;

			default:
				gst_structure_free(structure);
				return FALSE;
		}

		g_assert(structure != NULL);
		*encoded_caps = gst_caps_new_full(structure, NULL);
	}

	/* Raw caps */
	{
		structure = gst_structure_new(
			"video/x-raw",
			"width", GST_TYPE_INT_RANGE, (gint)(details->min_width), (gint)(details->max_width),
			"height", GST_TYPE_INT_RANGE, (gint)(details->min_height), (gint)(details->max_height),
			NULL
		);

		/* Interlace modes. */
		g_value_init(&list_value, GST_TYPE_LIST);
		g_value_init(&string_value, G_TYPE_STRING);
		g_value_set_static_string(&string_value, "progressive");
		gst_value_list_append_value(&list_value, &string_value);
		g_value_set_static_string(&string_value, "mixed");
		gst_value_list_append_value(&list_value, &string_value);
		gst_structure_set_value(structure, "interlace-mode", &list_value);
		g_value_unset(&list_value);
		g_value_unset(&string_value);

		/* Color formats. */

		g_value_init(&list_value, GST_TYPE_LIST);
		g_value_init(&string_value, G_TYPE_STRING);

		for (i = 0; i < details->num_supported_color_formats; ++i)
		{
			GstVideoFormat video_format;
			ImxVpuApiColorFormat color_format = details->supported_color_formats[i];

			if (!gst_imx_vpu_color_format_to_gstvidfmt(&video_format, color_format))
				continue;

			g_value_set_static_string(&string_value, gst_video_format_to_string(video_format));
			gst_value_list_append_value(&list_value, &string_value);
		}

		gst_structure_set_value(structure, "format", &list_value);

		g_value_unset(&list_value);
		g_value_unset(&string_value);

		*raw_caps = gst_caps_new_full(structure, NULL);
	}

	return TRUE;
}


static gboolean gst_imx_vpu_h264_is_frame_reordering_required(GstStructure *format)
{
	gchar const *media_type_str;
	gchar const *profile_str;

	g_assert(format != NULL);

	/* Disable frame reordering if we are handling h.264 baseline / constrained baseline.
	 * These h.264 profiles do not use frame reodering, and some decoders (Amphion Malone,
	 * most notably) seem to actually have lower latency when it is disabled. */

	media_type_str = gst_structure_get_name(format);
	g_assert(g_strcmp0(media_type_str, "video/x-h264") == 0);

	profile_str = gst_structure_get_string(format, "profile");

	return (profile_str == NULL) || ((g_strcmp0(profile_str, "constrained-baseline") != 0) && (g_strcmp0(profile_str, "baseline") != 0));
}


guint gst_imx_vpu_get_default_quantization(ImxVpuApiCompressionFormatSupportDetails const *details)
{
	/* Pick a value that is a reasonable default. To choose something that
	 * delivers acceptable quality without producing too much data, pick a
	 * quantization value that is at ~33.3% of the full quantization range. */
	return (details->max_quantization - details->min_quantization) * 1 / 3 + details->min_quantization;
}


gboolean gst_imx_vpu_color_format_to_gstvidfmt(GstVideoFormat *gst_video_format, ImxVpuApiColorFormat imxvpuapi_format)
{
	gboolean ret = TRUE;

	g_assert(gst_video_format != NULL);

	switch (imxvpuapi_format)
	{
		case IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV420_8BIT: *gst_video_format = GST_VIDEO_FORMAT_I420; break;
		case IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV420_10BIT: *gst_video_format = GST_VIDEO_FORMAT_I420_10LE; break;
		case IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV420_8BIT: *gst_video_format = GST_VIDEO_FORMAT_NV12; break;
#ifdef GST_IMX_VPU_SUPPORTS_SEMI_PLANAR_10BIT_FRAMES
		case IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV420_10BIT: *gst_video_format = GST_VIDEO_FORMAT_NV12_10LE40; break;
#endif
		case IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV411_8BIT: *gst_video_format = GST_VIDEO_FORMAT_Y41B; break;
		case IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV422_HORIZONTAL_8BIT: *gst_video_format = GST_VIDEO_FORMAT_Y42B; break;
		case IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV422_HORIZONTAL_10BIT: *gst_video_format = GST_VIDEO_FORMAT_I422_10LE; break;
		case IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV422_HORIZONTAL_8BIT: *gst_video_format = GST_VIDEO_FORMAT_NV16; break;
		case IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV444_8BIT: *gst_video_format = GST_VIDEO_FORMAT_Y444; break;
		case IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV444_10BIT: *gst_video_format = GST_VIDEO_FORMAT_Y444_10LE; break;
		case IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV444_8BIT: *gst_video_format = GST_VIDEO_FORMAT_NV24; break;
		case IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_P010_10BIT: *gst_video_format = GST_VIDEO_FORMAT_P010_10LE; break;
		case IMX_VPU_API_COLOR_FORMAT_YUV400_8BIT: *gst_video_format = GST_VIDEO_FORMAT_GRAY8; break;

		case IMX_VPU_API_COLOR_FORMAT_PACKED_YUV422_UYVY_8BIT: *gst_video_format = GST_VIDEO_FORMAT_UYVY; break;
		case IMX_VPU_API_COLOR_FORMAT_PACKED_YUV422_YUYV_8BIT: *gst_video_format = GST_VIDEO_FORMAT_YUY2; break;

		case IMX_VPU_API_COLOR_FORMAT_RGB565: *gst_video_format = GST_VIDEO_FORMAT_RGB16; break;
		case IMX_VPU_API_COLOR_FORMAT_BGR565: *gst_video_format = GST_VIDEO_FORMAT_BGR16; break;
		case IMX_VPU_API_COLOR_FORMAT_RGBA8888: *gst_video_format = GST_VIDEO_FORMAT_RGBA; break;
		case IMX_VPU_API_COLOR_FORMAT_BGRA8888: *gst_video_format = GST_VIDEO_FORMAT_BGRA; break;

		default: ret = FALSE; break;
	}

	return ret;
}


gboolean gst_imx_vpu_color_format_from_gstvidfmt(ImxVpuApiColorFormat *imxvpuapi_format, GstVideoFormat gst_video_format)
{
	gboolean ret = TRUE;

	g_assert(imxvpuapi_format != NULL);

	switch (gst_video_format)
	{
		case GST_VIDEO_FORMAT_I420: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV420_8BIT; break;
		case GST_VIDEO_FORMAT_I420_10LE: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV420_10BIT; break;
		case GST_VIDEO_FORMAT_NV12: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV420_8BIT; break;
#ifdef GST_IMX_VPU_SUPPORTS_SEMI_PLANAR_10BIT_FRAMES
		case GST_VIDEO_FORMAT_NV12_10LE40: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV420_10BIT; break;
#endif
		case GST_VIDEO_FORMAT_Y41B: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV411_8BIT; break;
		case GST_VIDEO_FORMAT_Y42B: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV422_HORIZONTAL_8BIT; break;
		case GST_VIDEO_FORMAT_I422_10LE: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV422_HORIZONTAL_10BIT; break;
		case GST_VIDEO_FORMAT_NV16: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV422_HORIZONTAL_8BIT; break;
		case GST_VIDEO_FORMAT_Y444: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV444_8BIT; break;
		case GST_VIDEO_FORMAT_Y444_10LE: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_FULLY_PLANAR_YUV444_10BIT; break;
		case GST_VIDEO_FORMAT_NV24: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_YUV444_8BIT; break;
		case GST_VIDEO_FORMAT_P010_10LE: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_SEMI_PLANAR_P010_10BIT; break;
		case GST_VIDEO_FORMAT_GRAY8: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_YUV400_8BIT; break;

		case GST_VIDEO_FORMAT_UYVY: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_PACKED_YUV422_UYVY_8BIT; break;
		case GST_VIDEO_FORMAT_YUY2: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_PACKED_YUV422_YUYV_8BIT; break;

		case GST_VIDEO_FORMAT_RGB16: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_RGB565; break;
		case GST_VIDEO_FORMAT_BGR16: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_BGR565; break;
		case GST_VIDEO_FORMAT_RGBA: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_RGBA8888; break;
		case GST_VIDEO_FORMAT_BGRA: *imxvpuapi_format = IMX_VPU_API_COLOR_FORMAT_BGRA8888; break;

		default: ret = FALSE; break;
	}

	return ret;
}


gboolean gst_imx_vpu_color_format_is_semi_planar(GstVideoFormat gst_video_format)
{
	gboolean is_semi_planar;

	switch (gst_video_format)
	{
		case GST_VIDEO_FORMAT_GRAY8:
			is_semi_planar = FALSE;
			break;

		default:
		{
			GstVideoFormatInfo const *format_info = gst_video_format_get_info(gst_video_format);
			/* We support YUV formats, so if there are less than 3 planes,
			 * it means that U and V are packed in the same plane. */
			is_semi_planar = GST_VIDEO_FORMAT_INFO_N_PLANES(format_info) < 3;
			break;
		}
	}

	return is_semi_planar;
}


gboolean gst_imx_vpu_color_format_has_10bit(GstVideoFormat gst_video_format)
{
	GstVideoFormatInfo const *format_info = gst_video_format_get_info(gst_video_format);
	return GST_VIDEO_FORMAT_INFO_DEPTH(format_info, 0) >= 10;
}


gchar const * gst_imx_vpu_get_string_from_structure_field(GstStructure *s, gchar const *field_name)
{
	/* Extract a string from the specified field. If the value of that
	 * field is a list of strings, pick the first string in that list. */

	GValue const *field_value = gst_structure_get_value(s, field_name);
	GType field_type = (field_value == NULL) ? G_TYPE_INVALID : G_VALUE_TYPE(field_value);

	if (field_type == G_TYPE_INVALID)
		return NULL;

	if (field_type == G_TYPE_STRING)
	{
		return g_value_get_string(field_value);
	}
	else if (field_type == GST_TYPE_LIST)
	{
		GValue const *list_entry_value;

		if (G_UNLIKELY(gst_value_list_get_size(field_value) == 0))
		{
			GST_ERROR("structure has list field \"%s\" which is empty (expected at least one string inside)", field_name);
			return NULL;
		}

		list_entry_value = gst_value_list_get_value(field_value, 0);
		if (G_UNLIKELY((list_entry_value == NULL) || (G_VALUE_TYPE(list_entry_value) != G_TYPE_STRING)))
		{
			GST_ERROR("structure has list field \"%s\" which does not hold strings", field_name);
			return NULL;
		}

		return g_value_get_string(list_entry_value);
	}


	GST_ERROR("structure has field \"%s\" which is neither a string nor a list of strings", field_name);
	return NULL;
}




/* GLib mutexes are implicitely initialized if they are global */
static GMutex logging_mutex;
static gboolean logging_set_up = FALSE;


static void imx_vpu_api_logging_func(ImxVpuApiLogLevel level, char const *file, int const line, char const *fn, const char *format, ...);


void gst_imx_vpu_api_setup_logging(void)
{
	g_mutex_lock(&logging_mutex);
	if (!logging_set_up)
	{
		ImxVpuApiLogLevel level;

		GST_DEBUG_CATEGORY_INIT(imx_vpu_api_debug, "imxvpuapi", 0, "imxvpuapi library for controlling the NXP i.MX VPU");
		GstDebugLevel gst_level = gst_debug_category_get_threshold(imx_vpu_api_debug);

		switch (gst_level)
		{
			case GST_LEVEL_ERROR:   level = IMX_VPU_API_LOG_LEVEL_ERROR;   break;
			case GST_LEVEL_WARNING: level = IMX_VPU_API_LOG_LEVEL_WARNING; break;
			case GST_LEVEL_INFO:    level = IMX_VPU_API_LOG_LEVEL_INFO;    break;
			case GST_LEVEL_DEBUG:   level = IMX_VPU_API_LOG_LEVEL_DEBUG;   break;
			case GST_LEVEL_LOG:     level = IMX_VPU_API_LOG_LEVEL_LOG;     break;
			case GST_LEVEL_TRACE:   level = IMX_VPU_API_LOG_LEVEL_TRACE;   break;
			default: level = IMX_VPU_API_LOG_LEVEL_TRACE;
		}

		imx_vpu_api_set_logging_threshold(level);
		imx_vpu_api_set_logging_function(imx_vpu_api_logging_func);

		logging_set_up = TRUE;
	}
	g_mutex_unlock(&logging_mutex);
}


static void imx_vpu_api_logging_func(ImxVpuApiLogLevel level, char const *file, int const line, char const *fn, const char *format, ...)
{
#ifndef GST_DISABLE_GST_DEBUG
	GstDebugLevel gst_level;
	va_list args;

	switch (level)
	{
		case IMX_VPU_API_LOG_LEVEL_ERROR:   gst_level = GST_LEVEL_ERROR;   break;
		case IMX_VPU_API_LOG_LEVEL_WARNING: gst_level = GST_LEVEL_WARNING; break;
		case IMX_VPU_API_LOG_LEVEL_INFO:    gst_level = GST_LEVEL_INFO;    break;
		case IMX_VPU_API_LOG_LEVEL_DEBUG:   gst_level = GST_LEVEL_DEBUG;   break;
		case IMX_VPU_API_LOG_LEVEL_LOG:     gst_level = GST_LEVEL_LOG;     break;
		case IMX_VPU_API_LOG_LEVEL_TRACE:   gst_level = GST_LEVEL_TRACE;   break;
		default: gst_level = GST_LEVEL_LOG;
	}

	va_start(args, format);
	gst_debug_log_valist(imx_vpu_api_debug, gst_level, file, fn, line, NULL, format, args);
	va_end(args);
#else
	(void)level;
	(void)file;
	(void)line;
	(void)fn;
	(void)format;
#endif
}
