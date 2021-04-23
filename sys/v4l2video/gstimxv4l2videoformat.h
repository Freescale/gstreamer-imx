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

#ifndef GST_IMX_V4L2_VIDEO_FORMAT_H
#define GST_IMX_V4L2_VIDEO_FORMAT_H

#include <gst/gst.h>
#include <gst/video/video.h>


G_BEGIN_DECLS


/* Some lengths / sizes must be aligned to kernel
 * page sizes. This macro takes care of that. */
#define GST_IMX_V4L2_PAGE_ALIGN(VALUE) (((VALUE) + 4095) & ~4095)


/**
 * GstImxV4L2BayerFormat:
 * @GST_IMX_V4L2_BAYER_FORMAT_RGGB: Bayer RGGB filter layout.
 * @GST_IMX_V4L2_BAYER_FORMAT_GRBG: Bayer GRBG filter layout.
 * @GST_IMX_V4L2_BAYER_FORMAT_GBRG: Bayer GBRG filter layout.
 * @GST_IMX_V4L2_BAYER_FORMAT_BGGR: Bayer BGGR filter layout.
 * @GST_IMX_V4L2_NUM_BAYER_FORMATS: Number of valid Bayer formats.
 * @GST_IMX_V4L2_BAYER_FORMAT_UNKNOWN: Reserved for specifying that
 * the Bayer format is unknown / unrecognized / invalid.
 *
 * Bayer pixel formats a device can support.
 * Typically only supported by capture devices.
 */
typedef enum
{
	GST_IMX_V4L2_BAYER_FORMAT_RGGB = 0,
	GST_IMX_V4L2_BAYER_FORMAT_GRBG,
	GST_IMX_V4L2_BAYER_FORMAT_GBRG,
	GST_IMX_V4L2_BAYER_FORMAT_BGGR,

	GST_IMX_V4L2_NUM_BAYER_FORMATS,
	GST_IMX_V4L2_BAYER_FORMAT_UNKNOWN
}
GstImxV4L2BayerFormat;


/**
 * GstImxV4L2CodecFormat:
 * @GST_IMX_V4L2_CODEC_FORMAT_JPEG: JPEG encoded video frames.
 * @GST_IMX_V4L2_NUM_CODEC_FORMATS: Number of valid codec formats.
 * @GST_IMX_V4L2_CODEC_FORMAT_UNKNOWN: Reserved for specifying that
 * the codec format is unknown / unrecognized / invalid.
 *
 * Video data encoding supported by the device.
 * Typically only supported by capture devices.
 */
typedef enum
{
	GST_IMX_V4L2_CODEC_FORMAT_JPEG = 0,

	GST_IMX_V4L2_NUM_CODEC_FORMATS,
	GST_IMX_V4L2_CODEC_FORMAT_UNKNOWN
}
GstImxV4L2CodecFormat;


/**
 * GstImxV4L2VideoFormatType:
 * @GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW: Raw RGB / YUV data. "Raw" means here
 * that the data is not encoded in any way, and the data is stored in raw
 * RGB / YUV pixel form instead. Bayer formats are handled separately.
 * @GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER: Bayer data, ready to be demosaiced.
 * @GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC: Encoded data.
 *
 * The format used by a device for how to store video frames.
 */
typedef enum
{
	GST_IMX_V4L2_VIDEO_FORMAT_TYPE_RAW = 0,
	GST_IMX_V4L2_VIDEO_FORMAT_TYPE_BAYER,
	GST_IMX_V4L2_VIDEO_FORMAT_TYPE_CODEC
}
GstImxV4L2VideoFormatType;


/**
 * GstImxV4L2VideoFormat:
 * @v4l2_pixelformat: V4L2 32-bit fourCC pixel format. Valid formats
 * can be found in the linux/videodev2.h header.
 * @type: The type (raw / bayer / codec).
 * @format: The type specific format.
 *
 * Structure holding information about the video format an imxv4l2
 * device uses. It is an extended counterpart to GstVideoFormat,
 * since that one cannot represent Bayer and codec formats.
 */
typedef struct
{
	guint32 v4l2_pixelformat;
	GstImxV4L2VideoFormatType type;

	union
	{
		GstVideoFormat gst_format;
		GstImxV4L2BayerFormat bayer_format;
		GstImxV4L2CodecFormat codec_format;
	} format;
}
GstImxV4L2VideoFormat;


/**
 * GstImxV4L2BayerInfo:
 * @format: The Bayer pixel format.
 * @width: Frame width in pixels.
 * @width: Frame height in pixels.
 * @fps_n: Framerate numerator.
 * @fps_d: Framerate denominator.
 * @interlace_mode: The interlace mode.
 *
 * Information about the structure of Bayer video frames.
 */
typedef struct
{
	GstImxV4L2BayerFormat format;
	gint width, height;
	gint fps_n, fps_d;
	GstVideoInterlaceMode interlace_mode;
}
GstImxV4L2BayerInfo;


/**
 * GstImxV4L2CodecInfo:
 * @format: The codec format.
 * @width: Frame width in pixels.
 * @width: Frame height in pixels.
 * @fps_n: Framerate numerator.
 * @fps_d: Framerate denominator.
 * @interlace_mode: The interlace mode.
 *
 * Information about the structure of encoded video frames.
 */
typedef struct
{
	GstImxV4L2CodecFormat format;
	gint width, height;
	gint fps_n, fps_d;
	GstVideoInterlaceMode interlace_mode;
}
GstImxV4L2CodecInfo;


/**
 * GstImxV4L2VideoInfo:
 * @type: The type (raw / bayer / codec).
 * @info: The type specific information.
 *
 * Structure holding imxv4l2 type specific video information.
 * It is an extended counterpart to GstVideoInfo, since that
 * one cannot represent Bayer and encoded video information.
 */
typedef struct
{
	GstImxV4L2VideoFormatType type;

	union
	{
		GstVideoInfo gst_info;
		GstImxV4L2CodecInfo codec_info;
		GstImxV4L2BayerInfo bayer_info;
	} info;
}
GstImxV4L2VideoInfo;


/**
 * gst_imx_v4l2_bayer_format_from_string:
 * @str String representation of a Bayer format.
 *
 * Converts a string representation to @GstImxV4L2BayerFormat.
 * This is the counterpart to @gst_imx_v4l2_bayer_format_to_string.
 *
 * Returns: Format that matches the given string.
 */
GstImxV4L2BayerFormat gst_imx_v4l2_bayer_format_from_string(gchar const *str);

/**
 * gst_imx_v4l2_bayer_format_from_string:
 * @bayer_format Bayer format.
 *
 * Converts a @GstImxV4L2BayerFormat to a string representation.
 * This is the counterpart to @gst_imx_v4l2_bayer_format_from_string.
 *
 * Returns: String representation of the Bayer format.
 */
gchar const * gst_imx_v4l2_bayer_format_to_string(GstImxV4L2BayerFormat bayer_format);

/**
 * gst_imx_v4l2_codec_format_from_media_type:
 * @media_type GStreamer media type string to convert to a codec format.
 *
 * Converts a GStreamer media type string to a @GstImxV4L2CodecFormat.
 * For example, "image/jpeg" is converted to @GST_IMX_V4L2_CODEC_FORMAT_JPEG.
 * This is the counterpart to @gst_imx_v4l2_codec_format_to_media_type.
 *
 * Returns: Codec format corresponding to the media type string.
 */
GstImxV4L2CodecFormat gst_imx_v4l2_codec_format_from_media_type(gchar const *media_type);

/**
 * gst_imx_v4l2_codec_format_to_media_type:
 * @media_type Codec format.
 *
 * Converts a @GstImxV4L2CodecFormat to a GStreamer media type string.
 * For example, @GST_IMX_V4L2_CODEC_FORMAT_JPEG is converted to "image/jpeg".
 * This is the counterpart to @gst_imx_v4l2_codec_format_from_media_type.
 *
 * Returns: Media type string corresponding to the codec format.
 */
gchar const * gst_imx_v4l2_codec_format_to_media_type(GstImxV4L2CodecFormat codec_format);

/**
 * gst_imx_v4l2_get_video_formats:
 * Returns: A list of all supported video formats. Do not try to deallocate.
 */
GstImxV4L2VideoFormat const * gst_imx_v4l2_get_video_formats();

/**
 * gst_imx_v4l2_get_num_video_formats:
 * Returns: The number of video formats in the array returned by @gst_imx_v4l2_get_video_formats.
 */
gsize gst_imx_v4l2_get_num_video_formats();

/**
 * gst_imx_v4l2_get_by_gst_video_format:
 * @gst_format @GstVideoFormat to find a matching imxv4l2 video format for.
 *
 * Looks into the array of supported imxv4l2 video formats for the first entry
 * that matches the given gst_format.
 *
 * Returns: The first matching imxv4l2 video format, or NULL if none match.
 */
GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_gst_video_format(GstVideoFormat gst_format);

/**
 * gst_imx_v4l2_get_by_bayer_video_format:
 * @bayer_format @GstImxV4L2BayerFormat to find a matching imxv4l2 video format for.
 *
 * Looks into the array of supported imxv4l2 video formats for the first entry
 * that matches the given bayer_format.
 *
 * Returns: The first matching imxv4l2 video format, or NULL if none match.
 */
GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_bayer_video_format(GstImxV4L2BayerFormat bayer_format);

/**
 * gst_imx_v4l2_get_by_bayer_video_format:
 * @codec_format @GstImxV4L2CodecFormat to find a matching imxv4l2 video format for.
 *
 * Looks into the array of supported imxv4l2 video formats for the first entry
 * that matches the given codec_format.
 *
 * Returns: The first matching imxv4l2 video format, or NULL if none match.
 */
GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_codec_video_format(GstImxV4L2CodecFormat codec_format);

/**
 * gst_imx_v4l2_get_by_v4l2_pixelformat:
 * @v4l2_pixelformat V4L2 pixel format to find a matching imxv4l2 video format for.
 *
 * Looks into the array of supported imxv4l2 video formats for the first entry
 * that matches the given v4l2_pixelformat.
 *
 * Returns: The first matching imxv4l2 video format, or NULL if none match.
 */
GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_v4l2_pixelformat(guint32 v4l2_pixelformat);

/**
 * gst_imx_v4l2_video_info_from_caps:
 * @info @GstImxV4L2VideoInfo to fill with data from the given caps.
 * @caps @GstCaps to fill the imxv4l2 video info with.
 *
 * Fills the @GstImxV4L2VideoInfo structure with data from the given @CstCaps.
 * It is an extended counterpart to @gst_video_info_from_caps.
 *
 * Returns: TRUE if the caps could be converted, FALSE otherwise.
 */
gboolean gst_imx_v4l2_video_info_from_caps(GstImxV4L2VideoInfo *info, GstCaps const *caps);

/**
 * gst_imx_v4l2_video_info_to_caps:
 * @info @GstImxV4L2VideoInfo to convert to @GstCaps.
 *
 * Creates @GstCaps and fills this with information from the given @GstImxV4L2VideoInfo.
 * It is an extended counterpart to @gst_video_info_to_caps.
 *
 * Returns: The caps.
 */
GstCaps* gst_imx_v4l2_video_info_to_caps(GstImxV4L2VideoInfo *info);

/**
 * gst_imx_v4l2_get_media_type_for_format:
 * @format @GstImxV4L2VideoFormat to get the media type string for.
 *
 * Gets a GStreamer media type string for the given @GstImxV4L2VideoFormat.
 *
 * Returns: Media type string for the given format, or NULL if the format is not
 *          a value of the @GstImxV4L2VideoFormat enum.
 */
gchar const * gst_imx_v4l2_get_media_type_for_format(GstImxV4L2VideoFormat const *format);

/**
 * gst_imx_v4l2_calculate_buffer_size_from_video_info:
 * @info @GstImxV4L2VideoInfo to calculate the buffer size with.
 *
 * Calculates the size in bytes for a buffer that can hold a video frame with
 * the given @GstImxV4L2VideoInfo. This is useful for setting up buffer pools
 * for example.
 *
 * Returns: Size of a buffer that can hold a video frame, in bytes.
 */
guint gst_imx_v4l2_calculate_buffer_size_from_video_info(GstImxV4L2VideoInfo const *info);

/**
 * gst_imx_v4l2_get_all_possible_caps:
 *
 * Returns caps that encompass all caps that could ever possibly happen.
 *
 * These caps do not depend on hardware capabilities. They are simply
 * all the caps that any and all V4L2 devices could ever have.
 *
 * These caps are mainly useful for pad templates and for initial return
 * values for CAPS queries before actual device caps are known.
 *
 * Returns: The caps.
 */
GstCaps* gst_imx_v4l2_get_all_possible_caps(void);


G_END_DECLS


#endif /* GST_IMX_V4L2_VIDEO_FORMAT_H */
