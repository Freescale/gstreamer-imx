#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "imx2d.h"
#include "imx2d_priv.h"


/***********************/
/******* LOGGING *******/
/***********************/


static void default_logging_fn(Imx2dLogLevel level, char const *file, int const line, char const *function_name, const char *format, ...)
{
	IMX_2D_UNUSED_PARAM(level);
	IMX_2D_UNUSED_PARAM(file);
	IMX_2D_UNUSED_PARAM(line);
	IMX_2D_UNUSED_PARAM(function_name);
	IMX_2D_UNUSED_PARAM(format);
}

Imx2dLogLevel imx_2d_cur_log_level_threshold = IMX_2D_LOG_LEVEL_ERROR;
Imx2dLoggingFunc imx_2d_cur_logging_fn = default_logging_fn;

void imx_2d_set_logging_threshold(Imx2dLogLevel threshold)
{
	imx_2d_cur_log_level_threshold = threshold;
}

void imx_2d_set_logging_function(Imx2dLoggingFunc logging_function)
{
	imx_2d_cur_logging_fn = (logging_function != NULL) ? logging_function : default_logging_fn;
}




char const * imx_2d_pixel_format_to_string(Imx2dPixelFormat format)
{
	Imx2dPixelFormatInfo const *info = imx_2d_get_pixel_format_info(format);
	return (info != NULL) ? info->description : "<unknown>";
}


char const * imx_2d_flip_mode_to_string(Imx2dFlipMode flip_mode)
{
	switch (flip_mode)
	{
		case IMX_2D_FLIP_MODE_NONE: return "none";
		case IMX_2D_FLIP_MODE_HORIZONTAL: return "horizontal";
		case IMX_2D_FLIP_MODE_VERTICAL: return "vertical";
		default: return "<unknown>";
	}
}


char const * imx_2d_rotation_string(Imx2dRotation rotation)
{
	switch (rotation)
	{
		case IMX_2D_ROTATION_NONE: return "none";
		case IMX_2D_ROTATION_90: return "90-degree rotation";
		case IMX_2D_ROTATION_180: return "180-degree rotation";
		case IMX_2D_ROTATION_270: return "270-degree rotation";
		default: return "<unknown>";
	}
}


Imx2dPixelFormatInfo const * imx_2d_get_pixel_format_info(Imx2dPixelFormat format)
{
#define PIXEL_FORMAT_DESC(DESC, FMT, NUM_PLANES, FIRST_PLANE_BPP, X_SS, Y_SS) \
	case (IMX_2D_PIXEL_FORMAT_##FMT): \
	{ \
		static Imx2dPixelFormatInfo const info = { \
			(DESC), (NUM_PLANES), (FIRST_PLANE_BPP), (X_SS), (Y_SS) \
		}; \
		return &info; \
	}

	switch (format)
	{
		PIXEL_FORMAT_DESC("RGB 5:6:5", RGB565, 1, 16, 1, 1)
		PIXEL_FORMAT_DESC("BGR 5:6:5", BGR565, 1, 16, 1, 1)
		PIXEL_FORMAT_DESC("RGB 8:8:8", RGB888, 1, 24, 1, 1)
		PIXEL_FORMAT_DESC("BGR 8:8:8", BGR888, 1, 24, 1, 1)
		PIXEL_FORMAT_DESC("RGBX 8:8:8:8", RGBX8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("RGBA 8:8:8:8", RGBA8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("BGRX 8:8:8:8", BGRX8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("BGRA 8:8:8:8", BGRA8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("XRGB 8:8:8:8", XRGB8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("ARGB 8:8:8:8", ARGB8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("XBGR 8:8:8:8", XBGR8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("ABGR 8:8:8:8", ABGR8888, 1, 32, 1, 1)
		PIXEL_FORMAT_DESC("grayscale 8", GRAY8, 1, 8, 1, 1)

		PIXEL_FORMAT_DESC("YUV 4:2:2 packed UYVY", PACKED_YUV422_UYVY, 1, 16, 2, 1)
		PIXEL_FORMAT_DESC("YUV 4:2:2 packed YUYV", PACKED_YUV422_YUYV, 1, 16, 2, 1)
		PIXEL_FORMAT_DESC("YUV 4:2:2 packed YVYU", PACKED_YUV422_YVYU, 1, 16, 2, 1)
		PIXEL_FORMAT_DESC("YUV 4:2:2 packed VYUY", PACKED_YUV422_VYUY, 1, 16, 2, 1)
		PIXEL_FORMAT_DESC("YUV 4:4:4 packed", PACKED_YUV444, 1, 24, 1, 1)

		PIXEL_FORMAT_DESC("YUV 4:2:0 semi planar NV12", SEMI_PLANAR_NV12, 2, 8, 2, 2)
		PIXEL_FORMAT_DESC("YUV 4:2:0 semi planar NV21", SEMI_PLANAR_NV21, 2, 8, 2, 2)
		PIXEL_FORMAT_DESC("YUV 4:2:2 semi planar NV16", SEMI_PLANAR_NV16, 2, 8, 2, 1)
		PIXEL_FORMAT_DESC("YUV 4:2:2 semi planar NV61", SEMI_PLANAR_NV61, 2, 8, 2, 1)

		PIXEL_FORMAT_DESC("YUV 4:2:0 fully planar YV12", FULLY_PLANAR_YV12, 3, 8, 2, 2)
		PIXEL_FORMAT_DESC("YUV 4:2:0 fully planar I420", FULLY_PLANAR_I420, 3, 8, 2, 2)
		PIXEL_FORMAT_DESC("YUV 4:2:2 fully planar Y42B", FULLY_PLANAR_Y42B, 3, 8, 2, 1)
		PIXEL_FORMAT_DESC("YUV 4:4:4 fully planar Y444", FULLY_PLANAR_Y444, 3, 8, 1, 1)

		default: return NULL;
	}

#undef PIXEL_FORMAT_DESC
}




void imx_2d_surface_desc_calculate_strides_and_offsets(Imx2dSurfaceDesc *desc, Imx2dHardwareCapabilities const *capabilities)
{
	Imx2dPixelFormatInfo const *fmt_info;
	int offset;
	int plane_nr;

	assert(desc != NULL);
	assert(desc->width > 0);
	assert(desc->height > 0);
	assert(desc->format != IMX_2D_PIXEL_FORMAT_UNKNOWN);
	assert(capabilities != NULL);

	fmt_info = imx_2d_get_pixel_format_info(desc->format);

	offset = 0;
	for (plane_nr = 0; plane_nr < fmt_info->num_planes; ++plane_nr)
	{
		int stride;

		int x_subsampling = (plane_nr == 0) ? 1 : fmt_info->x_subsampling;
		int y_subsampling = (plane_nr == 0) ? 1 : fmt_info->y_subsampling;

		stride = desc->width * fmt_info->num_first_plane_bpp / 8 / x_subsampling;

		desc->plane_stride[plane_nr] = stride;
		desc->plane_offset[plane_nr] = offset;

		offset += stride * desc->height / y_subsampling;
	}
}


int imx_2d_surface_desc_calculate_framesize(Imx2dSurfaceDesc const *desc)
{
	int y_subsampling;
	int last_plane_nr;
	Imx2dPixelFormatInfo const * fmt_info;

	assert(desc != NULL);

	fmt_info = imx_2d_get_pixel_format_info(desc->format);
	if (fmt_info == NULL)
		return 0;

	assert(fmt_info->num_planes >= 1);

	last_plane_nr = fmt_info->num_planes - 1;
	y_subsampling = (last_plane_nr == 0) ? 1 : fmt_info->y_subsampling;

	/* Use the offset of the last plane when computing the frame size.
	 * This is because there may be padding bytes in between planes.
	 * By using the last plane's offset, we implicitely factor in these
	 * padding bytes into our calculations. */
	return desc->plane_offset[last_plane_nr] + desc->plane_stride[last_plane_nr] * desc->height / y_subsampling;
}


Imx2dSurface* imx_2d_surface_create(ImxDmaBuffer *dma_buffer, Imx2dSurfaceDesc const *desc)
{
	Imx2dSurface *surface = malloc(sizeof(Imx2dSurface));
	assert(surface != NULL);

	surface->dma_buffer = dma_buffer;
	if (desc != NULL)
		memcpy(&(surface->desc), desc, sizeof(surface->desc));
	else
		memset(&(surface->desc), 0, sizeof(surface->desc));

	return surface;
}


void imx_2d_surface_destroy(Imx2dSurface *surface)
{
	free(surface);
}


void imx_2d_surface_set_desc(Imx2dSurface *surface, Imx2dSurfaceDesc const *desc)
{
	assert(surface != NULL);
	assert(desc != NULL);
	memcpy(&(surface->desc), desc, sizeof(Imx2dSurfaceDesc));
}


Imx2dSurfaceDesc const * imx_2d_surface_get_desc(Imx2dSurface *surface)
{
	return &(surface->desc);
}


void imx_2d_surface_set_dma_buffer(Imx2dSurface *surface, ImxDmaBuffer *dma_buffer)
{
	assert(surface != NULL);
	assert(dma_buffer != NULL);
	surface->dma_buffer = dma_buffer;
}


ImxDmaBuffer* imx_2d_surface_get_dma_buffer(Imx2dSurface *surface)
{
	assert(surface != NULL);
	return surface->dma_buffer;
}




void imx_2d_blitter_destroy(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->destroy != NULL));
	blitter->blitter_class->destroy(blitter);
}


int imx_2d_blitter_start(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->start != NULL));
	return blitter->blitter_class->start(blitter);
}


int imx_2d_blitter_finish(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->start != NULL));
	return blitter->blitter_class->finish(blitter);
}


int imx_2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dSurface *source, Imx2dSurface *dest, Imx2dBlitParams const *params)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->do_blit != NULL));
	return blitter->blitter_class->do_blit(blitter, source, dest, params);
}


int imx_2d_blitter_fill_rect(Imx2dBlitter *blitter, Imx2dSurface *dest, Imx2dRect const *dest_rect, uint32_t fill_color)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->fill_rect != NULL));
	return blitter->blitter_class->fill_rect(blitter, dest, dest_rect, fill_color);
}


Imx2dHardwareCapabilities const * imx_2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter)
{
	assert((blitter != NULL) && (blitter->blitter_class != NULL) && (blitter->blitter_class->get_hardware_capabilities != NULL));
	return blitter->blitter_class->get_hardware_capabilities(blitter);
}