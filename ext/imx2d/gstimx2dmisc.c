#include "gstimx2dmisc.h"
#include "imx2d/imx2d.h"


GST_DEBUG_CATEGORY_STATIC(imx2d_debug);
#define GST_CAT_DEFAULT imx2d_debug


static void imx_2d_logging_func(Imx2dLogLevel level, char const *file, int const line, char const *function_name, const char *format, ...);


/* GLib mutexes are implicitely initialized if they are global */
static GMutex logging_mutex;
static gboolean logging_set_up = FALSE;


static gchar const *nv12_amphion_8x128_str = "NV12_AMPHION_8x128";
static gchar const *nv21_amphion_8x128_str = "NV21_AMPHION_8x128";


void gst_imx_2d_setup_logging(void)
{
	g_mutex_lock(&logging_mutex);
	if (!logging_set_up)
	{
		Imx2dLogLevel level;

		GST_DEBUG_CATEGORY_INIT(imx2d_debug, "imx2d", 0, "imx2d 2D graphics code based on NXP i.MX 2D hardware APIs");
		GstDebugLevel gst_level = gst_debug_category_get_threshold(imx2d_debug);

		switch (gst_level)
		{
			case GST_LEVEL_ERROR:   level = IMX_2D_LOG_LEVEL_ERROR;   break;
			case GST_LEVEL_WARNING: level = IMX_2D_LOG_LEVEL_WARNING; break;
			case GST_LEVEL_INFO:    level = IMX_2D_LOG_LEVEL_INFO;    break;
			case GST_LEVEL_DEBUG:   level = IMX_2D_LOG_LEVEL_DEBUG;   break;
			case GST_LEVEL_LOG:
			case GST_LEVEL_TRACE:   level = IMX_2D_LOG_LEVEL_TRACE;   break;
			default: level = IMX_2D_LOG_LEVEL_TRACE;
		}

		imx_2d_set_logging_threshold(level);
		imx_2d_set_logging_function(imx_2d_logging_func);

		logging_set_up = TRUE;
	}
	g_mutex_unlock(&logging_mutex);
}


static void imx_2d_logging_func(Imx2dLogLevel level, char const *file, int const line, char const *function_name, const char *format, ...)
{
#ifndef GST_DISABLE_GST_DEBUG
	GstDebugLevel gst_level;
	va_list args;

	switch (level)
	{
		case IMX_2D_LOG_LEVEL_ERROR:   gst_level = GST_LEVEL_ERROR;   break;
		case IMX_2D_LOG_LEVEL_WARNING: gst_level = GST_LEVEL_WARNING; break;
		case IMX_2D_LOG_LEVEL_INFO:    gst_level = GST_LEVEL_INFO;    break;
		case IMX_2D_LOG_LEVEL_DEBUG:   gst_level = GST_LEVEL_DEBUG;   break;
		case IMX_2D_LOG_LEVEL_TRACE:   gst_level = GST_LEVEL_TRACE;   break;
		default: gst_level = GST_LEVEL_LOG;
	}

	va_start(args, format);
	gst_debug_log_valist(imx2d_debug, gst_level, file, function_name, line, NULL, format, args);
	va_end(args);
#else
	(void)level;
	(void)file;
	(void)line;
	(void)function_name;
	(void)format;
#endif
}


GstCaps* gst_imx_remove_tile_layout_from_caps(GstCaps *caps, GstImx2dTileLayout *tile_layout)
{
	GstStructure *s;
	gchar const *format_str;

	if (G_UNLIKELY(gst_caps_is_empty(caps)))
		return caps;

	if (G_UNLIKELY(gst_caps_is_any(caps)))
		return caps;

	if (G_UNLIKELY(!gst_caps_is_fixed(caps)))
		return caps;

	caps = gst_caps_make_writable(caps);

	s = gst_caps_get_structure(caps, 0);
	g_assert(s != NULL);

	format_str = gst_structure_get_string(s, "format");
	if (G_UNLIKELY(format_str == NULL))
	{
		GST_ERROR("caps have no format string field; caps: %" GST_PTR_FORMAT, (gpointer)caps);
		goto error;
	}

	if (g_strcmp0(format_str, nv12_amphion_8x128_str) == 0)
	{
		gst_structure_set(s, "format", G_TYPE_STRING, "NV12", NULL);

		if (tile_layout != NULL)
			*tile_layout = GST_IMX_2D_TILE_LAYOUT_AMPHION_8x128;
	}
	else if (g_strcmp0(format_str, nv21_amphion_8x128_str) == 0)
	{
		gst_structure_set(s, "format", G_TYPE_STRING, "NV21", NULL);

		if (tile_layout != NULL)
			*tile_layout = GST_IMX_2D_TILE_LAYOUT_AMPHION_8x128;
	}
	else
	{
		if (tile_layout != NULL)
			*tile_layout = GST_IMX_2D_TILE_LAYOUT_NONE;
	}

finish:
	return caps;

error:
	gst_caps_unref(caps);
	goto finish;
}


gboolean gst_imx_video_info_from_caps(GstVideoInfo *info, GstCaps const *caps, GstImx2dTileLayout *tile_layout)
{
	gboolean ret = TRUE;
	GstCaps *edited_caps = gst_caps_copy(caps);

	if (G_UNLIKELY(gst_caps_is_empty(edited_caps)))
	{
		GST_ERROR("caps is empty; cannot convert to video info");
		goto error;
	}

	if (G_UNLIKELY(gst_caps_is_any(edited_caps)))
	{
		GST_ERROR("caps is ANY; cannot convert to video info");
		goto error;
	}

	if (G_UNLIKELY(!gst_caps_is_fixed(edited_caps)))
	{
		GST_ERROR("cannot convert unfixated caps to video info; caps: %" GST_PTR_FORMAT, (gpointer)edited_caps);
		goto error;
	}

	edited_caps = gst_imx_remove_tile_layout_from_caps(edited_caps, tile_layout);

	ret = gst_video_info_from_caps(info, edited_caps);

finish:
	gst_caps_unref(edited_caps);
	return ret;

error:
	ret = FALSE;
	goto finish;
}


Imx2dPixelFormat gst_imx_2d_convert_from_gst_video_format(GstVideoFormat gst_video_format, GstImx2dTileLayout const *tile_layout)
{
	if (tile_layout != NULL)
	{
		switch (*tile_layout)
		{
			case GST_IMX_2D_TILE_LAYOUT_AMPHION_8x128:
			{
				switch (gst_video_format)
				{
					case GST_VIDEO_FORMAT_NV12: return IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128;
					case GST_VIDEO_FORMAT_NV21: return IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128;
					default: break;
				}

				break;
			}

			default:
				break;
		}
	}

	switch (gst_video_format)
	{
		case GST_VIDEO_FORMAT_RGB16: return IMX_2D_PIXEL_FORMAT_RGB565;
		case GST_VIDEO_FORMAT_BGR16: return IMX_2D_PIXEL_FORMAT_BGR565;
		case GST_VIDEO_FORMAT_RGB: return IMX_2D_PIXEL_FORMAT_RGB888;
		case GST_VIDEO_FORMAT_BGR: return IMX_2D_PIXEL_FORMAT_BGR888;
		case GST_VIDEO_FORMAT_RGBx: return IMX_2D_PIXEL_FORMAT_RGBX8888;
		case GST_VIDEO_FORMAT_RGBA: return IMX_2D_PIXEL_FORMAT_RGBA8888;
		case GST_VIDEO_FORMAT_BGRx: return IMX_2D_PIXEL_FORMAT_BGRX8888;
		case GST_VIDEO_FORMAT_BGRA: return IMX_2D_PIXEL_FORMAT_BGRA8888;
		case GST_VIDEO_FORMAT_xRGB: return IMX_2D_PIXEL_FORMAT_XRGB8888;
		case GST_VIDEO_FORMAT_ARGB: return IMX_2D_PIXEL_FORMAT_ARGB8888;
		case GST_VIDEO_FORMAT_xBGR: return IMX_2D_PIXEL_FORMAT_XBGR8888;
		case GST_VIDEO_FORMAT_ABGR: return IMX_2D_PIXEL_FORMAT_ABGR8888;
		case GST_VIDEO_FORMAT_GRAY8: return IMX_2D_PIXEL_FORMAT_GRAY8;

		case GST_VIDEO_FORMAT_UYVY: return IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY;
		case GST_VIDEO_FORMAT_YUY2: return IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV;
		case GST_VIDEO_FORMAT_YVYU: return IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU;
		case GST_VIDEO_FORMAT_VYUY: return IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY;
		case GST_VIDEO_FORMAT_v308: return IMX_2D_PIXEL_FORMAT_PACKED_YUV444;

		case GST_VIDEO_FORMAT_NV12: return IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12;
		case GST_VIDEO_FORMAT_NV21: return IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21;
		case GST_VIDEO_FORMAT_NV16: return IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16;
		case GST_VIDEO_FORMAT_NV61: return IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61;

		case GST_VIDEO_FORMAT_YV12: return IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12;
		case GST_VIDEO_FORMAT_I420: return IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420;
		case GST_VIDEO_FORMAT_Y42B: return IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B;
		case GST_VIDEO_FORMAT_Y444: return IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y444;

		default: return IMX_2D_PIXEL_FORMAT_UNKNOWN;
	}
}


GstVideoFormat gst_imx_2d_convert_to_gst_video_format(Imx2dPixelFormat imx2d_format)
{
	switch (imx2d_format)
	{
		case IMX_2D_PIXEL_FORMAT_RGB565: return GST_VIDEO_FORMAT_RGB16;
		case IMX_2D_PIXEL_FORMAT_BGR565: return GST_VIDEO_FORMAT_BGR16;
		case IMX_2D_PIXEL_FORMAT_RGB888: return GST_VIDEO_FORMAT_RGB;
		case IMX_2D_PIXEL_FORMAT_BGR888: return GST_VIDEO_FORMAT_BGR;
		case IMX_2D_PIXEL_FORMAT_RGBX8888: return GST_VIDEO_FORMAT_RGBx;
		case IMX_2D_PIXEL_FORMAT_RGBA8888: return GST_VIDEO_FORMAT_RGBA;
		case IMX_2D_PIXEL_FORMAT_BGRX8888: return GST_VIDEO_FORMAT_BGRx;
		case IMX_2D_PIXEL_FORMAT_BGRA8888: return GST_VIDEO_FORMAT_BGRA;
		case IMX_2D_PIXEL_FORMAT_XRGB8888: return GST_VIDEO_FORMAT_xRGB;
		case IMX_2D_PIXEL_FORMAT_ARGB8888: return GST_VIDEO_FORMAT_ARGB;
		case IMX_2D_PIXEL_FORMAT_XBGR8888: return GST_VIDEO_FORMAT_xBGR;
		case IMX_2D_PIXEL_FORMAT_ABGR8888: return GST_VIDEO_FORMAT_ABGR;
		case IMX_2D_PIXEL_FORMAT_GRAY8: return GST_VIDEO_FORMAT_GRAY8;

		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY: return GST_VIDEO_FORMAT_UYVY;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV: return GST_VIDEO_FORMAT_YUY2;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU: return GST_VIDEO_FORMAT_YVYU;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY: return GST_VIDEO_FORMAT_VYUY;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV444: return GST_VIDEO_FORMAT_v308;

		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12: return GST_VIDEO_FORMAT_NV12;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21: return GST_VIDEO_FORMAT_NV21;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16: return GST_VIDEO_FORMAT_NV16;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61: return GST_VIDEO_FORMAT_NV61;

		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12: return GST_VIDEO_FORMAT_YV12;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420: return GST_VIDEO_FORMAT_I420;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B: return GST_VIDEO_FORMAT_Y42B;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y444: return GST_VIDEO_FORMAT_Y444;

		case IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128: return GST_VIDEO_FORMAT_NV12;
		case IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128: return GST_VIDEO_FORMAT_NV21;

		default: return GST_VIDEO_FORMAT_UNKNOWN;
	}
}


GstCaps* gst_imx_2d_get_caps_from_imx2d_capabilities(Imx2dHardwareCapabilities const *capabilities, GstPadDirection direction)
{
	GstStructure *structure;
	Imx2dPixelFormat const *supported_formats;
	int i, num_supported_formats;
	GValue format_list_gvalue = G_VALUE_INIT;
	GValue format_string_gvalue = G_VALUE_INIT;
	GValue width_range_gvalue = G_VALUE_INIT;
	GValue height_range_gvalue = G_VALUE_INIT;

	g_assert(capabilities != NULL);

	switch (direction)
	{
		case GST_PAD_SINK:
			supported_formats = capabilities->supported_source_pixel_formats;
			num_supported_formats = capabilities->num_supported_source_pixel_formats;
			break;

		case GST_PAD_SRC:
			supported_formats = capabilities->supported_dest_pixel_formats;
			num_supported_formats = capabilities->num_supported_dest_pixel_formats;
			break;

		default:
			g_assert_not_reached();
	}

	g_value_init(&format_list_gvalue, GST_TYPE_LIST);

	for (i = 0; i < num_supported_formats; ++i)
	{
		gchar const *format_str = NULL;

		g_value_init(&format_string_gvalue, G_TYPE_STRING);

		switch (supported_formats[i])
		{
			case IMX_2D_PIXEL_FORMAT_TILED_NV12_AMPHION_8x128:
				format_str = nv12_amphion_8x128_str;
				break;

			case IMX_2D_PIXEL_FORMAT_TILED_NV21_AMPHION_8x128:
				format_str = nv21_amphion_8x128_str;
				break;

			default:
			{
				GstVideoFormat gst_format = gst_imx_2d_convert_to_gst_video_format(supported_formats[i]);
				if (G_LIKELY(gst_format != GST_VIDEO_FORMAT_UNKNOWN))
					format_str = gst_video_format_to_string(gst_format);
			}
		}

		if (G_LIKELY(format_str != NULL))
		{
			g_value_set_string(&format_string_gvalue, format_str);
			gst_value_list_append_and_take_value(&format_list_gvalue, &format_string_gvalue);
		}
	}

	g_value_init(&width_range_gvalue, GST_TYPE_INT_RANGE);
	gst_value_set_int_range_step(&width_range_gvalue, capabilities->min_width, capabilities->max_width, capabilities->width_step_size);

	g_value_init(&height_range_gvalue, GST_TYPE_INT_RANGE);
	gst_value_set_int_range_step(&height_range_gvalue, capabilities->min_height, capabilities->max_height, capabilities->height_step_size);

	structure = gst_structure_new(
		"video/x-raw",
		"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
		NULL
	);
	gst_structure_take_value(structure, "width", &width_range_gvalue);
	gst_structure_take_value(structure, "height", &height_range_gvalue);
	gst_structure_take_value(structure, "format", &format_list_gvalue);

	return gst_caps_new_full(structure, NULL);
}


void gst_imx_2d_canvas_calculate_letterbox_margin(
	Imx2dBlitMargin *margin,
	Imx2dRegion *inner_region,
	Imx2dRegion const *outer_region,
	gboolean video_transposed,
	guint video_width, guint video_height,
	guint video_par_n, guint video_par_d
)
{
	guint display_ratio_n, display_ratio_d;
	guint window_par_n, window_par_d;
	guint ratio_factor;
	guint outer_w, outer_h, inner_w, inner_h;
	guint combined_w_margin, combined_h_margin;

	g_assert(margin != NULL);
	g_assert(outer_region != NULL);

	window_par_n = 1;
	window_par_d = 1;

	if (G_UNLIKELY((video_width == 0) || (video_height == 0)))
	{
		/* Can't compute a margin if either width
		 * or height are 0. */
		goto set_zero_margin;
	}

	if (G_UNLIKELY(!gst_video_calculate_display_ratio(
		&display_ratio_n, &display_ratio_d,
		video_width, video_height,
		video_par_n, video_par_d,
		window_par_n, window_par_d
	)))
		goto set_zero_margin;

	if (video_transposed)
	{
		guint d = display_ratio_d;
		display_ratio_d = display_ratio_n;
		display_ratio_n = d;
	}

	/* Fit the inner region in the outer one, keeping display ratio.
	 * This means that either its width or its height will be set to the
	 * outer region's width/height, and the other length will be shorter,
	 * scaled accordingly to retain the display ratio
	 *
	 * Setting dn = display_ratio_n , dd = display_ratio_d ,
	 * outer_w = outer_region_width , outer_h = outer_region_height ,
	 * we can identify cases:
	 *
	 * (1) Inner region fits in outer one with its width maximized
	 *     In this case, this holds: outer_w/outer_h < dn/dd
	 * (1) Inner region fits in outer one with its height maximized
	 *     In this case, this holds: outer_w/outer_h > dn/dd
	 *
	 * To simplify comparison, the inequality outer_w/outer_h > dn/dd
	 * is transformed to: outer_w*dd/outer_h > dn
	 * outer_w*dd/outer_h is the ratio_factor
	 */

	outer_w = outer_region->x2 - outer_region->x1;
	outer_h = outer_region->y2 - outer_region->y1;

	ratio_factor = (guint)gst_util_uint64_scale_int(outer_w, display_ratio_d, outer_h);

	if (ratio_factor >= display_ratio_n)
	{
		inner_w = (guint)gst_util_uint64_scale_int(outer_h, display_ratio_n, display_ratio_d);
		inner_h = outer_h;
	}
	else
	{
		inner_w = outer_w;
		inner_h = (guint)gst_util_uint64_scale_int(outer_w, display_ratio_d, display_ratio_n);
	}

	/* Safeguard to ensure width/height aren't out of bounds
	 * (should not happen, but better safe than sorry) */
	inner_w = MIN(inner_w, outer_w);
	inner_h = MIN(inner_h, outer_h);

	combined_w_margin = outer_w - inner_w;
	combined_h_margin = outer_h - inner_h;

	GST_LOG(
		"video w/h: %u/%u  video PAR: %d/%d  window PAR: %d/%d  display ratio: %d/%d  outer w/h: %u/%u  inner w/h: %u/%u  ratio factor: %u  combined margin w/h: %u/%d",
		video_width, video_height,
		video_par_n, video_par_d,
		window_par_n, window_par_d,
		display_ratio_n, display_ratio_d,
		outer_w, outer_h,
		inner_w, inner_h,
		ratio_factor,
		combined_w_margin, combined_h_margin
	);

	margin->left_margin = combined_w_margin / 2;
	margin->right_margin = combined_w_margin - margin->left_margin;
	margin->top_margin = combined_h_margin / 2;
	margin->bottom_margin = combined_h_margin - margin->top_margin;

	inner_region->x1 = outer_region->x1 + margin->left_margin;
	inner_region->y1 = outer_region->y1 + margin->top_margin;
	inner_region->x2 = outer_region->x2 - margin->right_margin;
	inner_region->y2 = outer_region->y2 - margin->bottom_margin;

	return;

set_zero_margin:
	margin->left_margin = 0;
	margin->top_margin = 0;
	margin->right_margin = 0;
	margin->bottom_margin = 0;
}


GType gst_imx_2d_rotation_get_type(void)
{
	static GType gst_imx_2d_rotation_type = 0;

	if (!gst_imx_2d_rotation_type)
	{
		static GEnumValue imx_2d_rotation_values[] =
		{
			{ IMX_2D_ROTATION_NONE, "No rotation", "none" },
			{ IMX_2D_ROTATION_90, "90-degree rotation", "rotation-90" },
			{ IMX_2D_ROTATION_180, "180-degree rotation", "rotation-180" },
			{ IMX_2D_ROTATION_270, "270-degree rotation", "rotation-270" },
			{ IMX_2D_ROTATION_FLIP_HORIZONTAL, "Horizontal flipping", "horizontal" },
			{ IMX_2D_ROTATION_FLIP_VERTICAL, "Vertical flipping", "vertical" },
			{ 0, NULL, NULL }
		};

		gst_imx_2d_rotation_type = g_enum_register_static(
			"Imx2dRotation",
			imx_2d_rotation_values
		);
	}

	return gst_imx_2d_rotation_type;
}
