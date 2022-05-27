#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <config.h>

#include "imx2d/imx2d_priv.h"
#include "pxp_blitter.h"

#ifdef PXP_HEADER_IS_IN_IMX_SUBDIR
#include <imx/linux/pxp_device.h>
#else
#include <linux/pxp_device.h>
#endif


static Imx2dPixelFormat const supported_source_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU
};


static Imx2dPixelFormat const supported_dest_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_BGR888,
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_GRAY8
};


static BOOL get_pxp_format(Imx2dPixelFormat imx_2d_format, unsigned int *pxp_format)
{
	BOOL ret = TRUE;

	assert(pxp_format != NULL);

	switch (imx_2d_format)
	{
		case IMX_2D_PIXEL_FORMAT_BGRX8888: *pxp_format = PXP_PIX_FMT_RGB32; break;
		case IMX_2D_PIXEL_FORMAT_BGRA8888: *pxp_format = PXP_PIX_FMT_BGRA32; break;
		case IMX_2D_PIXEL_FORMAT_BGR888: *pxp_format = PXP_PIX_FMT_RGB24; break;
		case IMX_2D_PIXEL_FORMAT_RGB565: *pxp_format = PXP_PIX_FMT_RGB565; break;
		case IMX_2D_PIXEL_FORMAT_GRAY8: *pxp_format = PXP_PIX_FMT_GREY; break;

		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420: *pxp_format = PXP_PIX_FMT_YUV420P; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12: *pxp_format = PXP_PIX_FMT_YVU420P; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_Y42B: *pxp_format = PXP_PIX_FMT_YUV422P; break;

		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12: *pxp_format = PXP_PIX_FMT_NV12; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16: *pxp_format = PXP_PIX_FMT_NV16; break;

		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV: *pxp_format = PXP_PIX_FMT_YUYV; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY: *pxp_format = PXP_PIX_FMT_UYVY; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU: *pxp_format = PXP_PIX_FMT_YVYU; break;

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
			IMX_2D_LOG(ERROR, "PxP does not support multi-buffer surfaces");
			return FALSE;
		}
	}

	return TRUE;
}




typedef struct _Imx2dPxPBlitter Imx2dPxPBlitter;


struct _Imx2dPxPBlitter
{
	Imx2dBlitter parent;

	int pxp_fd;

	struct pxp_config_data pxp_config;
	struct pxp_chan_handle pxp_channel;
	BOOL pxp_channel_requested;
};


static void imx_2d_backend_pxp_blitter_destroy(Imx2dBlitter *blitter);

static int imx_2d_backend_pxp_blitter_start(Imx2dBlitter *blitter);
static int imx_2d_backend_pxp_blitter_finish(Imx2dBlitter *blitter);

static int imx_2d_backend_pxp_blitter_do_blit(Imx2dBlitter *blitter, Imx2dInternalBlitParams *internal_blit_params);
static int imx_2d_backend_pxp_blitter_fill_region(Imx2dBlitter *blitter, Imx2dInternalFillRegionParams *internal_fill_region_params);

static Imx2dHardwareCapabilities const * imx_2d_backend_pxp_blitter_get_hardware_capabilities(Imx2dBlitter *blitter);


static Imx2dBlitterClass imx_2d_backend_pxp_blitter_class =
{
	imx_2d_backend_pxp_blitter_destroy,

	imx_2d_backend_pxp_blitter_start,
	imx_2d_backend_pxp_blitter_finish,

	imx_2d_backend_pxp_blitter_do_blit,
	imx_2d_backend_pxp_blitter_fill_region,

	imx_2d_backend_pxp_blitter_get_hardware_capabilities
};


static void imx_2d_backend_pxp_blitter_destroy(Imx2dBlitter *blitter)
{
	Imx2dPxPBlitter *pxp_blitter = (Imx2dPxPBlitter *)blitter;

	assert(blitter != NULL);

	if (pxp_blitter->pxp_channel_requested)
	{
		assert(pxp_blitter->pxp_fd > 0);
		ioctl(pxp_blitter->pxp_fd, PXP_IOC_PUT_CHAN, &(pxp_blitter->pxp_channel.handle));
		pxp_blitter->pxp_channel_requested = FALSE;
	}

	if (pxp_blitter->pxp_fd > 0)
	{
		close(pxp_blitter->pxp_fd);
		pxp_blitter->pxp_fd = -1;
	}

	free(blitter);
}


static int imx_2d_backend_pxp_blitter_start(Imx2dBlitter *blitter)
{
	Imx2dPxPBlitter *pxp_blitter = (Imx2dPxPBlitter *)blitter;
	Imx2dSurfaceDesc const *dest_surface_desc;
	Imx2dPixelFormatInfo const *fmt_info;
	imx_physical_address_t phys_address;
	ImxDmaBuffer *dest_dma_buffer;
	unsigned int pxp_pixel_format;
	struct pxp_layer_param *out_param;

	assert(blitter->dest);

	dest_surface_desc = imx_2d_surface_get_desc(blitter->dest);
	fmt_info = imx_2d_get_pixel_format_info(dest_surface_desc->format);

	dest_dma_buffer = imx_2d_surface_get_dma_buffer(blitter->dest, 0);
	assert(dest_dma_buffer != NULL);

	if (!check_if_single_buffer_surface(blitter->dest))
	{
		IMX_2D_LOG(ERROR, "destination surface uses multiple DMA buffers; PxP only supports single-buffer surfaces");
		return FALSE;
	}

	phys_address = imx_dma_buffer_get_physical_address(dest_dma_buffer);
	assert(phys_address != 0);

	memset(&(pxp_blitter->pxp_config), 0, sizeof(pxp_blitter->pxp_config));
	pxp_blitter->pxp_config.handle = pxp_blitter->pxp_channel.handle;

	out_param = &(pxp_blitter->pxp_config.out_param);

	/* The width & height parameters are set to values that include the
	 * padding columns and rows. The drect rectangle then defines the
	 * region inside the frame where the actual pixels are drawn to. */
	/* The PxP expects the stride in pixels, not bytes. Perform a bytes->pixels conversion. */
	out_param->width = dest_surface_desc->plane_strides[0] / fmt_info->pixel_stride;
	out_param->height = dest_surface_desc->height + dest_surface_desc->num_padding_rows;
	out_param->stride = dest_surface_desc->plane_strides[0] / fmt_info->pixel_stride;

	out_param->paddr = (dma_addr_t)(phys_address);

	if (!get_pxp_format(dest_surface_desc->format, &pxp_pixel_format))
	{
		IMX_2D_LOG(
			ERROR,
			"could not convert imx2d format %s to a format the PxP can handle",
			imx_2d_pixel_format_to_string(dest_surface_desc->format)
		);
		return FALSE;
	}
	out_param->pixel_fmt = pxp_pixel_format;

	return TRUE;
}


static int imx_2d_backend_pxp_blitter_finish(Imx2dBlitter *blitter)
{
	IMX_2D_UNUSED_PARAM(blitter);
	return TRUE;
}


static int imx_2d_backend_pxp_blitter_do_blit(Imx2dBlitter *blitter, Imx2dInternalBlitParams *internal_blit_params)
{
	Imx2dPxPBlitter *pxp_blitter = (Imx2dPxPBlitter *)blitter;
	Imx2dSurfaceDesc const *src_surface_desc;
	Imx2dPixelFormatInfo const *fmt_info;
	imx_physical_address_t phys_address;
	Imx2dRegion const *source_region;
	Imx2dRegion const *dest_region;
	Imx2dRegion const *expanded_dest_region;
	ImxDmaBuffer *src_dma_buffer;
	unsigned int pxp_format;
	struct pxp_config_data *pconf;
	struct pxp_layer_param *src_param;

	if (!pxp_blitter->pxp_channel_requested)
	{
		IMX_2D_LOG(ERROR, "PxP channel handle wasn't requested - cannot blit");
		return FALSE;
	}

	src_surface_desc = imx_2d_surface_get_desc(internal_blit_params->source);
	fmt_info = imx_2d_get_pixel_format_info(src_surface_desc->format);

	src_dma_buffer = imx_2d_surface_get_dma_buffer(internal_blit_params->source, 0);
	assert(src_dma_buffer != NULL);

	if (!check_if_single_buffer_surface(internal_blit_params->source))
	{
		IMX_2D_LOG(ERROR, "source surface uses multiple DMA buffers; PxP only supports single-buffer surfaces");
		return FALSE;
	}

	phys_address = imx_dma_buffer_get_physical_address(src_dma_buffer);
	assert(phys_address != 0);

	source_region = (internal_blit_params->source_region != NULL) ? internal_blit_params->source_region : &(internal_blit_params->source->region);
	dest_region = internal_blit_params->dest_region;
	expanded_dest_region = (internal_blit_params->expanded_dest_region != NULL) ? internal_blit_params->expanded_dest_region : dest_region;

	pconf = &(pxp_blitter->pxp_config);

	src_param = &(pconf->s0_param);
	/* The width & height parameters are set to values that include the
	 * padding columns and rows. The srect rectangle then defines the
	 * region inside the frame where the actual pixels are drawn from. */
	/* The PxP expects the stride in pixels, not bytes. Perform a bytes->pixels conversion. */
	src_param->width = src_surface_desc->plane_strides[0] / fmt_info->pixel_stride;
	src_param->height = src_surface_desc->height + src_surface_desc->num_padding_rows;
	src_param->stride = src_surface_desc->plane_strides[0] / fmt_info->pixel_stride;
	src_param->paddr = (dma_addr_t)(phys_address);
	pconf->proc_data.srect.left = source_region->x1;
	pconf->proc_data.srect.top = source_region->y1;
	pconf->proc_data.srect.width = source_region->x2 - source_region->x1;
	pconf->proc_data.srect.height = source_region->y2 - source_region->y1;

	pconf->proc_data.drect.left = dest_region->x1 - expanded_dest_region->x1;
	pconf->proc_data.drect.top = dest_region->y1 - expanded_dest_region->y1;
	pconf->proc_data.drect.width = dest_region->x2 - dest_region->x1;
	pconf->proc_data.drect.height = dest_region->y2 - dest_region->y1;

	pconf->proc_data.bgcolor = internal_blit_params->margin_fill_color;
	pconf->proc_data.fill_en = 0;

	pconf->proc_data.scaling = (pconf->proc_data.srect.width != pconf->proc_data.drect.width)
	                        || (pconf->proc_data.srect.height != pconf->proc_data.drect.height);

	switch (internal_blit_params->rotation)
	{
		case IMX_2D_ROTATION_90:
			pconf->proc_data.rotate = 90;
			pconf->proc_data.hflip = 0;
			pconf->proc_data.vflip = 0;
			break;

		case IMX_2D_ROTATION_180:
			pconf->proc_data.rotate = 180;
			pconf->proc_data.hflip = 0;
			pconf->proc_data.vflip = 0;
			break;

		case IMX_2D_ROTATION_270:
			pconf->proc_data.rotate = 270;
			pconf->proc_data.hflip = 0;
			pconf->proc_data.vflip = 0;
			break;

		case IMX_2D_ROTATION_FLIP_HORIZONTAL:
			pconf->proc_data.rotate = 0;
			pconf->proc_data.hflip = 1;
			pconf->proc_data.vflip = 0;
			break;

		case IMX_2D_ROTATION_FLIP_VERTICAL:
			pconf->proc_data.rotate = 0;
			pconf->proc_data.hflip = 0;
			pconf->proc_data.vflip = 1;
			break;

		case IMX_2D_ROTATION_UL_LR:
			pconf->proc_data.rotate = 90;
			pconf->proc_data.hflip = 0;
			pconf->proc_data.vflip = 1;
			break;

		case IMX_2D_ROTATION_UR_LL:
			pconf->proc_data.rotate = 90;
			pconf->proc_data.hflip = 1;
			pconf->proc_data.vflip = 0;
			break;

		default:
			pconf->proc_data.rotate = 0;
			pconf->proc_data.hflip = 0;
			pconf->proc_data.vflip = 0;
	}

	IMX_2D_LOG(
		TRACE,
		"PxP blitter: regions: source: %" IMX_2D_REGION_FORMAT " dest: %" IMX_2D_REGION_FORMAT,
		IMX_2D_REGION_ARGS(source_region), IMX_2D_REGION_ARGS(dest_region)
	);

	if (!get_pxp_format(src_surface_desc->format, &pxp_format))
	{
		IMX_2D_LOG(
			ERROR,
			"could not convert imx2d format %s to a format the PxP can handle",
			imx_2d_pixel_format_to_string(src_surface_desc->format)
		);
		return FALSE;
	}
	src_param->pixel_fmt = pxp_format;

	if (ioctl(pxp_blitter->pxp_fd, PXP_IOC_CONFIG_CHAN, pconf) != 0)
	{
		IMX_2D_LOG(ERROR, "could not configure PxP channel: %s", strerror(errno));
		return FALSE;
	}

	if (ioctl(pxp_blitter->pxp_fd, PXP_IOC_START_CHAN, &(pxp_blitter->pxp_channel.handle)) != 0)
	{
		IMX_2D_LOG(ERROR, "could not start PxP channel: %s", strerror(errno));
		return FALSE;
	}

	if (ioctl(pxp_blitter->pxp_fd, PXP_IOC_WAIT4CMPLT, &(pxp_blitter->pxp_channel)) != 0)
	{
		IMX_2D_LOG(ERROR, "could not wait for PxP channel completion: %s", strerror(errno));
		return FALSE;
	}

	return TRUE;
}


static int imx_2d_backend_pxp_blitter_fill_region(Imx2dBlitter *blitter, Imx2dInternalFillRegionParams *internal_fill_region_params)
{
	Imx2dPxPBlitter *pxp_blitter = (Imx2dPxPBlitter *)blitter;
	struct pxp_config_data *pconf;
	Imx2dRegion const *dest_region;

	if (!pxp_blitter->pxp_channel_requested)
	{
		IMX_2D_LOG(ERROR, "PxP channel handle wasn't requested - cannot blit");
		return FALSE;
	}

	dest_region = internal_fill_region_params->dest_region;

	pconf = &(pxp_blitter->pxp_config);

	pconf->proc_data.drect.left = dest_region->x1;
	pconf->proc_data.drect.top = dest_region->y1;
	pconf->proc_data.drect.width = dest_region->x2 - dest_region->x1;
	pconf->proc_data.drect.height = dest_region->y2 - dest_region->y1;

	pconf->proc_data.bgcolor = internal_fill_region_params->fill_color;
	pconf->proc_data.fill_en = 1;

	if (ioctl(pxp_blitter->pxp_fd, PXP_IOC_CONFIG_CHAN, pconf) != 0)
	{
		IMX_2D_LOG(ERROR, "could not configure PxP channel: %s", strerror(errno));
		return FALSE;
	}

	if (ioctl(pxp_blitter->pxp_fd, PXP_IOC_START_CHAN, &(pxp_blitter->pxp_channel.handle)) != 0)
	{
		IMX_2D_LOG(ERROR, "could not start PxP channel: %s", strerror(errno));
		return FALSE;
	}

	if (ioctl(pxp_blitter->pxp_fd, PXP_IOC_WAIT4CMPLT, &(pxp_blitter->pxp_channel)) != 0)
	{
		IMX_2D_LOG(ERROR, "could not wait for PxP channel completion: %s", strerror(errno));
		return FALSE;
	}

	return TRUE;
}


static Imx2dHardwareCapabilities const * imx_2d_backend_pxp_blitter_get_hardware_capabilities(Imx2dBlitter *blitter)
{
	IMX_2D_UNUSED_PARAM(blitter);
	return imx_2d_backend_pxp_get_hardware_capabilities();
}




Imx2dBlitter* imx_2d_backend_pxp_blitter_create(void)
{
	Imx2dPxPBlitter *pxp_blitter;

	pxp_blitter = malloc(sizeof(Imx2dPxPBlitter));
	assert(pxp_blitter != NULL);

	memset(pxp_blitter, 0, sizeof(Imx2dPxPBlitter));
	pxp_blitter->pxp_fd = -1;

	pxp_blitter->parent.blitter_class = &imx_2d_backend_pxp_blitter_class;

	pxp_blitter->pxp_fd = open("/dev/pxp_device", O_RDWR, 0);
	if (pxp_blitter->pxp_fd < 0)
	{
		IMX_2D_LOG(ERROR, "could not open /dev/pxp_device: %s", strerror(errno));
		goto error;
	}

	if (ioctl(pxp_blitter->pxp_fd, PXP_IOC_GET_CHAN, &(pxp_blitter->pxp_channel.handle)) != 0)
	{
		IMX_2D_LOG(ERROR, "could not request PxP channel: %s",  strerror(errno));
		goto error;
	}
	else
		pxp_blitter->pxp_channel_requested = TRUE;

finish:
	return (Imx2dBlitter *)pxp_blitter;

error:
	imx_2d_backend_pxp_blitter_destroy((Imx2dBlitter *)pxp_blitter);
	pxp_blitter = NULL;
	goto finish;
}


static Imx2dHardwareCapabilities const capabilities = {
	.supported_source_pixel_formats = supported_source_pixel_formats,
	.num_supported_source_pixel_formats = sizeof(supported_source_pixel_formats) / sizeof(Imx2dPixelFormat),

	.supported_dest_pixel_formats = supported_dest_pixel_formats,
	.num_supported_dest_pixel_formats = sizeof(supported_dest_pixel_formats) / sizeof(Imx2dPixelFormat),

	.min_width = 4, .max_width = INT_MAX, .width_step_size = 1,
	.min_height = 4, .max_height = INT_MAX, .height_step_size = 1,

	.stride_alignment = 16,
	.total_row_count_alignment = 8,

	.can_handle_multi_buffer_surfaces = 0
};

Imx2dHardwareCapabilities const * imx_2d_backend_pxp_get_hardware_capabilities(void)
{
	return &capabilities;
}
