#include <assert.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <config.h>

/* This prevents warnings about unnamed structs not being supported by C99.
 * We cannot do anything about these structs, so just turn the warnings off,
 * but only do that for the code in those headers. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

#ifdef IPU_HEADER_IS_IN_IMX_SUBDIR
#include <imx/linux/ipu.h>
#else
#include <linux/ipu.h>
#endif

#pragma GCC diagnostic pop

#include "imx2d/imx2d_priv.h"
#include "ipu_blitter.h"


/* IMPORTANT: The IPU is limited in significant ways:
 *
 * - Arbitrary alpha blending of source surfaces is not possible. The IPU
 *   can do that only with a specific subset of formats and frame sizes.
 * - Exact XY positioning of fill rectangles and frames is not supported.
 *   As a result, fill regions are not supported.
 * - With some rotation modes, the IPU cannot handle frames that are too
 *   large. This code then has to perform manual tiling.
 */


static Imx2dPixelFormat const supported_source_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_RGBX8888,
	IMX_2D_PIXEL_FORMAT_RGBA8888,
	IMX_2D_PIXEL_FORMAT_ABGR8888,
	IMX_2D_PIXEL_FORMAT_BGR888,
	IMX_2D_PIXEL_FORMAT_RGB888,
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y444,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV444
};


static Imx2dPixelFormat const supported_dest_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_RGBX8888,
	IMX_2D_PIXEL_FORMAT_RGBA8888,
	IMX_2D_PIXEL_FORMAT_ABGR8888,
	IMX_2D_PIXEL_FORMAT_BGR888,
	IMX_2D_PIXEL_FORMAT_RGB888,
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y444,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV444
};


static BOOL get_ipu_format(Imx2dPixelFormat imx_2d_format, uint32_t *ipu_format)
{
	BOOL ret = TRUE;

	assert(ipu_format != NULL);

	switch (imx_2d_format)
	{
		/* XXX: There are more formats defined in ipu.h, but these
		 * are either not (yet) supported by imx2d, or do not work. */

		case IMX_2D_PIXEL_FORMAT_RGB565: *ipu_format = IPU_PIX_FMT_RGB565; break;
		case IMX_2D_PIXEL_FORMAT_BGR888: *ipu_format = IPU_PIX_FMT_BGR24; break;
		case IMX_2D_PIXEL_FORMAT_RGB888: *ipu_format = IPU_PIX_FMT_RGB24; break;
		case IMX_2D_PIXEL_FORMAT_BGRX8888: *ipu_format = IPU_PIX_FMT_BGR32; break;
		case IMX_2D_PIXEL_FORMAT_BGRA8888: *ipu_format = IPU_PIX_FMT_BGRA32; break;
		case IMX_2D_PIXEL_FORMAT_RGBX8888: *ipu_format = IPU_PIX_FMT_RGB32; break;
		case IMX_2D_PIXEL_FORMAT_RGBA8888: *ipu_format = IPU_PIX_FMT_RGBA32; break;
		case IMX_2D_PIXEL_FORMAT_ABGR8888: *ipu_format = IPU_PIX_FMT_ABGR32; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV: *ipu_format = IPU_PIX_FMT_YUYV; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY: *ipu_format = IPU_PIX_FMT_UYVY; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV444: *ipu_format = IPU_PIX_FMT_YUV444; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12: *ipu_format = IPU_PIX_FMT_NV12; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12: *ipu_format = IPU_PIX_FMT_YVU420P; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420: *ipu_format = IPU_PIX_FMT_YUV420P; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B: *ipu_format = IPU_PIX_FMT_YUV422P; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y444: *ipu_format = IPU_PIX_FMT_YUV444P; break;

		default: ret = FALSE;
	}

	return ret;
}


static BOOL check_if_single_buffer_surface(Imx2dSurface *surface)
{
	int i;
	ImxDmaBuffer *dma_buffer;

	dma_buffer = imx_2d_surface_get_dma_buffer(surface, 0);

	for (i = 1; i < imx_2d_get_pixel_format_info(surface->desc.format)->num_planes; ++i)
	{
		/* Check that we do not have a multi-buffer surface.
		 * If so, exit, since we cannot handle this. */
		if (imx_2d_surface_get_dma_buffer(surface, i) != dma_buffer)
		{
			IMX_2D_LOG(ERROR, "IPU does not support multi-buffer surfaces");
			return FALSE;
		}
	}

	return TRUE;
}




typedef struct _Imx2dIPUBlitter Imx2dIPUBlitter;


struct _Imx2dIPUBlitter
{
	Imx2dBlitter parent;

	int ipu_fd;

	struct ipu_task main_task;
};


static void imx_2d_backend_ipu_blitter_destroy(Imx2dBlitter *blitter);

static int imx_2d_backend_ipu_blitter_start(Imx2dBlitter *blitter);
static int imx_2d_backend_ipu_blitter_finish(Imx2dBlitter *blitter);

static int imx_2d_backend_ipu_blitter_do_blit(Imx2dBlitter *blitter, Imx2dInternalBlitParams *internal_blit_params);
static int imx_2d_backend_ipu_blitter_fill_region(Imx2dBlitter *blitter, Imx2dInternalFillRegionParams *internal_fill_region_params);

static Imx2dHardwareCapabilities const * imx_2d_backend_ipu_blitter_get_hardware_capabilities(Imx2dBlitter *blitter);


static Imx2dBlitterClass imx_2d_backend_ipu_blitter_class =
{
	imx_2d_backend_ipu_blitter_destroy,

	imx_2d_backend_ipu_blitter_start,
	imx_2d_backend_ipu_blitter_finish,

	imx_2d_backend_ipu_blitter_do_blit,
	imx_2d_backend_ipu_blitter_fill_region,

	imx_2d_backend_ipu_blitter_get_hardware_capabilities
};


static void imx_2d_backend_ipu_blitter_destroy(Imx2dBlitter *blitter)
{
	Imx2dIPUBlitter *ipu_blitter = (Imx2dIPUBlitter *)blitter;

	assert(blitter != NULL);

	if (ipu_blitter->ipu_fd > 0)
	{
		close(ipu_blitter->ipu_fd);
		ipu_blitter->ipu_fd = -1;
	}

	free(blitter);
}


static int imx_2d_backend_ipu_blitter_start(Imx2dBlitter *blitter)
{
	Imx2dIPUBlitter *ipu_blitter = (Imx2dIPUBlitter *)blitter;
	Imx2dSurfaceDesc const *dest_surface_desc;
	Imx2dPixelFormatInfo const *fmt_info;
	imx_physical_address_t phys_address;
	ImxDmaBuffer *dest_dma_buffer;
	uint32_t ipu_format;

	assert(blitter->dest);

	dest_surface_desc = imx_2d_surface_get_desc(blitter->dest);
	fmt_info = imx_2d_get_pixel_format_info(dest_surface_desc->format);

	dest_dma_buffer = imx_2d_surface_get_dma_buffer(blitter->dest, 0);
	assert(dest_dma_buffer != NULL);

	if (!check_if_single_buffer_surface(blitter->dest))
	{
		IMX_2D_LOG(ERROR, "destination surface uses multiple DMA buffers; IPU only supports single-buffer surfaces");
		return FALSE;
	}

	phys_address = imx_dma_buffer_get_physical_address(dest_dma_buffer);
	assert(phys_address != 0);

	/* Set up the output width/height of main_task. The output width & height
	 * contain the width & height _and_ any existing additional padding
	 * rows & columns (so, the width is actually the stride). The padding
	 * rows and columns are excluded later when setting the output crop rectangle. */
	memset(&(ipu_blitter->main_task), 0, sizeof(struct ipu_task));
	/* The IPU expects the stride in pixels, not bytes. Perform a bytes->pixels conversion. */
	ipu_blitter->main_task.output.width = dest_surface_desc->plane_strides[0] / fmt_info->pixel_stride;
	ipu_blitter->main_task.output.height = dest_surface_desc->height + dest_surface_desc->num_padding_rows;

	ipu_blitter->main_task.output.paddr = (dma_addr_t)(phys_address);

	if (!get_ipu_format(dest_surface_desc->format, &ipu_format))
	{
		IMX_2D_LOG(
			ERROR,
			"could not convert imx2d format %s to a format the IPU can handle",
			imx_2d_pixel_format_to_string(dest_surface_desc->format)
		);
		return FALSE;
	}
	ipu_blitter->main_task.output.format = ipu_format;

	return TRUE;
}


static int imx_2d_backend_ipu_blitter_finish(Imx2dBlitter *blitter)
{
	IMX_2D_UNUSED_PARAM(blitter);
	return TRUE;
}


static char const * ipu_error_to_string(int error)
{
	switch (error)
	{
		case IPU_CHECK_OK: return "IPU_CHECK_OK";
		case IPU_CHECK_WARN_INPUT_OFFS_NOT8ALIGN: return "IPU_CHECK_WARN_INPUT_OFFS_NOT8ALIGN";
		case IPU_CHECK_WARN_OUTPUT_OFFS_NOT8ALIGN: return "IPU_CHECK_WARN_OUTPUT_OFFS_NOT8ALIGN";
		case IPU_CHECK_WARN_OVERLAY_OFFS_NOT8ALIGN: return "IPU_CHECK_WARN_OVERLAY_OFFS_NOT8ALIGN";
		case IPU_CHECK_ERR_MIN: return "IPU_CHECK_ERR_MIN";
		case IPU_CHECK_ERR_INPUT_CROP: return "IPU_CHECK_ERR_INPUT_CROP";
		case IPU_CHECK_ERR_OUTPUT_CROP: return "IPU_CHECK_ERR_OUTPUT_CROP";
		case IPU_CHECK_ERR_OVERLAY_CROP: return "IPU_CHECK_ERR_OVERLAY_CROP";
		case IPU_CHECK_ERR_INPUT_OVER_LIMIT: return "IPU_CHECK_ERR_INPUT_OVER_LIMIT";
		case IPU_CHECK_ERR_OV_OUT_NO_FIT: return "IPU_CHECK_ERR_OV_OUT_NO_FIT";
		case IPU_CHECK_ERR_OVERLAY_WITH_VDI: return "IPU_CHECK_ERR_OVERLAY_WITH_VDI";
		case IPU_CHECK_ERR_PROC_NO_NEED: return "IPU_CHECK_ERR_PROC_NO_NEED";
		case IPU_CHECK_ERR_SPLIT_INPUTW_OVER: return "IPU_CHECK_ERR_SPLIT_INPUTW_OVER";
		case IPU_CHECK_ERR_SPLIT_INPUTH_OVER: return "IPU_CHECK_ERR_SPLIT_INPUTH_OVER";
		case IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER: return "IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER";
		case IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER: return "IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER";
		case IPU_CHECK_ERR_SPLIT_WITH_ROT: return "IPU_CHECK_ERR_SPLIT_WITH_ROT";
		case IPU_CHECK_ERR_NOT_SUPPORT: return "IPU_CHECK_ERR_NOT_SUPPORT";
		case IPU_CHECK_ERR_NOT16ALIGN: return "IPU_CHECK_ERR_NOT16ALIGN";
		case IPU_CHECK_ERR_W_DOWNSIZE_OVER: return "IPU_CHECK_ERR_W_DOWNSIZE_OVER";
		case IPU_CHECK_ERR_H_DOWNSIZE_OVER: return "IPU_CHECK_ERR_H_DOWNSIZE_OVER";
		default: return "<unknown>";
	}
}


static int imx_2d_backend_ipu_blitter_do_blit(Imx2dBlitter *blitter, Imx2dInternalBlitParams *internal_blit_params)
{
	Imx2dIPUBlitter *ipu_blitter = (Imx2dIPUBlitter *)blitter;
	Imx2dSurfaceDesc const *src_surface_desc;
	Imx2dPixelFormatInfo const *fmt_info;
	imx_physical_address_t phys_address;
	Imx2dRegion const *source_region;
	Imx2dRegion const *dest_region;
	ImxDmaBuffer *src_dma_buffer;
	int input_width, input_height;
	int output_width, output_height;
	uint32_t ipu_format;
	int ioctl_ret;

	src_surface_desc = imx_2d_surface_get_desc(internal_blit_params->source);
	fmt_info = imx_2d_get_pixel_format_info(src_surface_desc->format);

	src_dma_buffer = imx_2d_surface_get_dma_buffer(internal_blit_params->source, 0);
	assert(src_dma_buffer != NULL);

	if (!check_if_single_buffer_surface(internal_blit_params->source))
	{
		IMX_2D_LOG(ERROR, "source surface uses multiple DMA buffers; IPU only supports single-buffer surfaces");
		return FALSE;
	}

	phys_address = imx_dma_buffer_get_physical_address(src_dma_buffer);
	assert(phys_address != 0);

	source_region = (internal_blit_params->source_region != NULL) ? internal_blit_params->source_region : &(internal_blit_params->source->region);
	dest_region = internal_blit_params->dest_region;

	/* Set up the input width/height of main_task. The input width & height
	 * contain the width & height _and_ any existing additional padding
	 * rows & columns (so, the width is actually the stride). The padding
	 * rows and columns are excluded later when setting the input crop rectangle. */
	memset(&(ipu_blitter->main_task.input), 0, sizeof(struct ipu_input));
	/* The IPU expects the stride in pixels, not bytes. Perform a bytes->pixels conversion. */
	ipu_blitter->main_task.input.width = src_surface_desc->plane_strides[0] / fmt_info->pixel_stride;
	ipu_blitter->main_task.input.height = src_surface_desc->height + src_surface_desc->num_padding_rows;

	ipu_blitter->main_task.input.paddr = (dma_addr_t)(phys_address);

	switch (internal_blit_params->rotation)
	{
		case IMX_2D_ROTATION_90:  ipu_blitter->main_task.output.rotate = IPU_ROTATE_90_RIGHT; break;
		case IMX_2D_ROTATION_180: ipu_blitter->main_task.output.rotate = IPU_ROTATE_180; break;
		case IMX_2D_ROTATION_270: ipu_blitter->main_task.output.rotate = IPU_ROTATE_90_LEFT; break;
		case IMX_2D_ROTATION_FLIP_HORIZONTAL: ipu_blitter->main_task.output.rotate = IPU_ROTATE_HORIZ_FLIP; break;
		case IMX_2D_ROTATION_FLIP_VERTICAL: ipu_blitter->main_task.output.rotate = IPU_ROTATE_VERT_FLIP; break;
		case IMX_2D_ROTATION_UL_LR: ipu_blitter->main_task.output.rotate = IPU_ROTATE_90_RIGHT_HFLIP; break;
		case IMX_2D_ROTATION_UR_LL: ipu_blitter->main_task.output.rotate = IPU_ROTATE_90_RIGHT_VFLIP; break;
		default: ipu_blitter->main_task.output.rotate = IPU_ROTATE_NONE; break;
	}

	input_width = source_region->x2 - source_region->x1;
	input_height = source_region->y2 - source_region->y1;
	output_width = dest_region->x2 - dest_region->x1;
	output_height = dest_region->y2 - dest_region->y1;

	ipu_blitter->main_task.input.crop.pos.x = source_region->x1;
	ipu_blitter->main_task.input.crop.pos.y = source_region->y1;
	ipu_blitter->main_task.input.crop.w = input_width;
	ipu_blitter->main_task.input.crop.h = input_height;

	ipu_blitter->main_task.output.crop.pos.x = dest_region->x1;
	ipu_blitter->main_task.output.crop.pos.y = dest_region->y1;
	ipu_blitter->main_task.output.crop.w = output_width;
	ipu_blitter->main_task.output.crop.h = output_height;

	IMX_2D_LOG(
		TRACE,
		"IPU blitter: regions: source: %" IMX_2D_REGION_FORMAT " dest: %" IMX_2D_REGION_FORMAT,
		IMX_2D_REGION_ARGS(source_region), IMX_2D_REGION_ARGS(dest_region)
	);

	if (!get_ipu_format(src_surface_desc->format, &ipu_format))
	{
		IMX_2D_LOG(
			ERROR,
			"could not convert imx2d format %s to a format the IPU can handle",
			imx_2d_pixel_format_to_string(src_surface_desc->format)
		);
		return FALSE;
	}

	ipu_blitter->main_task.input.format = ipu_format;

	if ((internal_blit_params->rotation == IMX_2D_ROTATION_NONE) || (internal_blit_params->rotation == IMX_2D_ROTATION_180))
	{
		IMX_2D_LOG(TRACE, "rotation \"%s\" requested; the IPU can handle this in one ioctl, no manual tiling required", imx_2d_rotation_to_string(internal_blit_params->rotation));

		/* Do a task check before actually trying to queue the task for blitting.
		 * This gives us more detailed feedback if something is wrong with the task. */
		ioctl_ret = ioctl(ipu_blitter->ipu_fd, IPU_CHECK_TASK, &(ipu_blitter->main_task));
		if (ioctl_ret != IPU_CHECK_OK)
		{
			IMX_2D_LOG(ERROR, "check-task ioctl detected error: %s (%d)", ipu_error_to_string(ioctl_ret), ioctl_ret);
			return FALSE;
		}

		if (ioctl(ipu_blitter->ipu_fd, IPU_QUEUE_TASK, &(ipu_blitter->main_task)) < 0)
		{
			IMX_2D_LOG(ERROR, "queuing IPU task failed: %s (%d)", strerror(errno), errno);
			return FALSE;
		}
	}
	else
	{
		int tile_x, tile_y;
		int num_x_tiles, num_y_tiles;
		int last_tile_width, last_tile_height;

		/* The IPU expects tiles with up to 1024x1024 pixels. */
		int const max_tile_width = 1024;
		int const max_tile_height = 1024;

		/* Calculate number of tiling with rounding up (so, for example, "5.1 tiles" -> "6 tiles").
		 * The last "partial" tiles are handled separately by the code below. */
		num_x_tiles = (output_width + (max_tile_width - 1)) / max_tile_width;
		num_y_tiles = (output_height + (max_tile_height - 1)) / max_tile_height;

		last_tile_width = output_width - (num_x_tiles - 1) * max_tile_width;
		last_tile_height = output_height - (num_y_tiles - 1) * max_tile_height;

		IMX_2D_LOG(TRACE, "rotation \"%s\" requested; the IPU cannot handle this in one ioctl; manual tiling required", imx_2d_rotation_to_string(internal_blit_params->rotation));
		IMX_2D_LOG(
			TRACE,
			"max tile width/height: %d/%d  last tile width/height: %d/%d  num x/y tiles: %d/%d",
			max_tile_width, max_tile_height,
			last_tile_width, last_tile_height,
			num_x_tiles, num_y_tiles
		);

		for (tile_y = 0; tile_y < num_y_tiles; ++tile_y)
		{
			int output_y = tile_y * max_tile_height;
			int tile_height = (tile_y == (num_y_tiles - 1)) ? last_tile_height : max_tile_height;

			ipu_blitter->main_task.output.crop.pos.y = tile_y * max_tile_height;
			ipu_blitter->main_task.output.crop.h = tile_height;

			for (tile_x = 0; tile_x < num_x_tiles; ++tile_x)
			{
				int output_x = tile_x * max_tile_width;
				int tile_width = (tile_x == (num_x_tiles - 1)) ? last_tile_width : max_tile_width;

				ipu_blitter->main_task.output.crop.pos.x = tile_x * max_tile_width;
				ipu_blitter->main_task.output.crop.w = tile_width;

				/* Calculate the region in the source surface that corresponds
				 * to this tile. The source region coordinates have to be tweaked
				 * depending on the rotation mode. We also calculate the y coordinate
				 * and height of the source region here, even though it only needs
				 * to be calculated once per tile row. This is because (a) there are
				 * not many tiles anyway (at most 2x2 tiles) and (b) keeping all
				 * calculations here makes for clearer code. */
				switch (internal_blit_params->rotation)
				{
					case IMX_2D_ROTATION_90:
						ipu_blitter->main_task.input.crop.w = input_width * tile_height / output_height;
						ipu_blitter->main_task.input.crop.h = input_height * tile_width / output_width;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + input_width * output_y / output_height;
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + (input_height - ipu_blitter->main_task.input.crop.h - input_height * output_x / output_width);
						break;

					case IMX_2D_ROTATION_180:
						ipu_blitter->main_task.input.crop.w = input_width * tile_width / output_width;
						ipu_blitter->main_task.input.crop.h = input_height * tile_height / output_height;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + (input_width - ipu_blitter->main_task.input.crop.w - input_width * output_x / output_width);
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + (input_height - ipu_blitter->main_task.input.crop.h - input_height * output_y / output_height);
						break;

					case IMX_2D_ROTATION_270:
						ipu_blitter->main_task.input.crop.w = input_width * tile_height / output_height;
						ipu_blitter->main_task.input.crop.h = input_height * tile_width / output_width;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + (input_width - ipu_blitter->main_task.input.crop.w - input_width * output_y / output_height);
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + input_height * output_x / output_width;
						break;

					case IMX_2D_ROTATION_FLIP_HORIZONTAL:
						ipu_blitter->main_task.input.crop.w = input_width * tile_width / output_width;
						ipu_blitter->main_task.input.crop.h = input_height * tile_height / output_height;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + (input_width - ipu_blitter->main_task.input.crop.w - input_width * output_x / output_width);
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + input_height * output_y / output_height;
						break;

					case IMX_2D_ROTATION_FLIP_VERTICAL:
						ipu_blitter->main_task.input.crop.w = input_width * tile_width / output_width;
						ipu_blitter->main_task.input.crop.h = input_height * tile_height / output_height;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + input_width * output_x / output_width;
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + (input_height - ipu_blitter->main_task.input.crop.h - input_height * output_y / output_height);
						break;

					case IMX_2D_ROTATION_UL_LR:
						ipu_blitter->main_task.input.crop.w = input_width * tile_height / output_height;
						ipu_blitter->main_task.input.crop.h = input_height * tile_width / output_width;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + input_width * output_y / output_height;
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + input_height * output_x / output_width;
						break;

					case IMX_2D_ROTATION_UR_LL:
						ipu_blitter->main_task.input.crop.w = input_width * tile_height / output_height;
						ipu_blitter->main_task.input.crop.h = input_height * tile_width / output_width;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + (input_width - ipu_blitter->main_task.input.crop.w - input_width * output_y / output_height);
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + (input_height - ipu_blitter->main_task.input.crop.h - input_height * output_x / output_width);
						break;

					default:
						ipu_blitter->main_task.input.crop.w = input_width * tile_width / output_width;
						ipu_blitter->main_task.input.crop.h = input_height * tile_height / output_height;
						ipu_blitter->main_task.input.crop.pos.x = source_region->x1 + input_width * output_x / output_width;
						ipu_blitter->main_task.input.crop.pos.y = source_region->y1 + input_height * output_y / output_height;
						break;
				}

				IMX_2D_LOG(
					TRACE,
					"tile x/y %d/%d  coordinates:  input crop x/y/width/height %d/%d/%d/%d  output crop x/y/width/height %d/%d/%d/%d",
					tile_x, tile_y,
					(int)(ipu_blitter->main_task.input.crop.pos.x),
					(int)(ipu_blitter->main_task.input.crop.pos.y),
					(int)(ipu_blitter->main_task.input.crop.w),
					(int)(ipu_blitter->main_task.input.crop.h),
					(int)(ipu_blitter->main_task.output.crop.pos.x),
					(int)(ipu_blitter->main_task.output.crop.pos.y),
					(int)(ipu_blitter->main_task.output.crop.w),
					(int)(ipu_blitter->main_task.output.crop.h)
				);

				/* Do a task check before actually trying to queue the task for blitting.
				 * This gives us more detailed feedback if something is wrong with the task. */
				ioctl_ret = ioctl(ipu_blitter->ipu_fd, IPU_CHECK_TASK, &(ipu_blitter->main_task));
				if (ioctl_ret != IPU_CHECK_OK)
				{
					IMX_2D_LOG(ERROR, "check-task ioctl detected error for tile (%d, %d): %s (%d)", tile_x, tile_y, ipu_error_to_string(ioctl_ret), ioctl_ret);
					return FALSE;
				}

				if (ioctl(ipu_blitter->ipu_fd, IPU_QUEUE_TASK, &(ipu_blitter->main_task)) < 0)
				{
					IMX_2D_LOG(ERROR, "queuing IPU task for tile (%d, %d) failed: %s (%d)", tile_x, tile_y, strerror(errno), errno);
					return FALSE;
				}
			}
		}
	}

	return TRUE;
}


static int imx_2d_backend_ipu_blitter_fill_region(Imx2dBlitter *blitter, Imx2dInternalFillRegionParams *internal_fill_region_params)
{
	/* Fill regions are not implemented because exact XY positioning is not possible with the IPU. */
	IMX_2D_UNUSED_PARAM(blitter);
	IMX_2D_UNUSED_PARAM(internal_fill_region_params);
	return TRUE;
}


static Imx2dHardwareCapabilities const * imx_2d_backend_ipu_blitter_get_hardware_capabilities(Imx2dBlitter *blitter)
{
	IMX_2D_UNUSED_PARAM(blitter);
	return imx_2d_backend_ipu_get_hardware_capabilities();
}


Imx2dBlitter* imx_2d_backend_ipu_blitter_create(void)
{
	Imx2dIPUBlitter *ipu_blitter;

	ipu_blitter = malloc(sizeof(Imx2dIPUBlitter));
	assert(ipu_blitter != NULL);

	memset(ipu_blitter, 0, sizeof(Imx2dIPUBlitter));

	ipu_blitter->parent.blitter_class = &imx_2d_backend_ipu_blitter_class;

	ipu_blitter->ipu_fd = open("/dev/mxc_ipu", O_RDWR, 0);
	if (ipu_blitter->ipu_fd < 0)
	{
		IMX_2D_LOG(ERROR, "could not open /dev/mxc_ipu: %s (%d)", strerror(errno), errno);
		goto error;
	}

finish:
	return (Imx2dBlitter *)ipu_blitter;

error:
	imx_2d_backend_ipu_blitter_destroy((Imx2dBlitter *)ipu_blitter);
	ipu_blitter = NULL;
	goto finish;
}


static Imx2dHardwareCapabilities const capabilities = {
	.supported_source_pixel_formats = supported_source_pixel_formats,
	.num_supported_source_pixel_formats = sizeof(supported_source_pixel_formats) / sizeof(Imx2dPixelFormat),

	.supported_dest_pixel_formats = supported_dest_pixel_formats,
	.num_supported_dest_pixel_formats = sizeof(supported_dest_pixel_formats) / sizeof(Imx2dPixelFormat),

	.min_width = 64, .max_width = INT_MAX, .width_step_size = 1,
	.min_height = 64, .max_height = INT_MAX, .height_step_size = 1,

	.stride_alignment = 16,
	.total_row_count_alignment = 8,

	.can_handle_multi_buffer_surfaces = 0,

	.special_format_stride_alignments = NULL,
	.num_special_format_stride_alignments = 0
};

Imx2dHardwareCapabilities const * imx_2d_backend_ipu_get_hardware_capabilities(void)
{
	return &capabilities;
}
