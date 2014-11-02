/* PxP-based i.MX blitter class
 * Copyright (C) 2014  Carlos Rafael Giani
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
#include <pxp_lib.h>
#include <errno.h>
#include "blitter.h"
#include "allocator.h"
#include "device.h"
#include "../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(imx_pxp_blitter_debug);
#define GST_CAT_DEFAULT imx_pxp_blitter_debug


G_DEFINE_TYPE(GstImxPxPBlitter, gst_imx_pxp_blitter, GST_TYPE_IMX_BASE_BLITTER)


/* Private structure storing IPU specific data */
struct _GstImxPxPBlitterPrivate
{
	struct pxp_config_data pxp_config;
	pxp_chan_handle_t pxp_channel;
	gboolean pxp_channel_requested;
};


typedef struct
{
	unsigned int format;
	guint bits_per_pixel;
}
GstImxPxPFormatDetails;


static void gst_imx_pxp_blitter_finalize(GObject *object);

static gboolean gst_imx_pxp_blitter_set_input_video_info(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info);
static gboolean gst_imx_pxp_blitter_set_input_frame(GstImxBaseBlitter *base_blitter, GstBuffer *input_frame);
static gboolean gst_imx_pxp_blitter_set_output_frame(GstImxBaseBlitter *base_blitter, GstBuffer *output_frame);
static gboolean gst_imx_pxp_blitter_set_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region);
static GstAllocator* gst_imx_pxp_blitter_get_phys_mem_allocator(GstImxBaseBlitter *base_blitter);
static gboolean gst_imx_pxp_blitter_blit_frame(GstImxBaseBlitter *base_blitter);

static GstImxPxPFormatDetails const * gst_imx_pxp_blitter_get_pxp_format_details(GstVideoFormat gstfmt);
static void gst_imx_pxp_blitter_set_layer_params(GstImxPxPBlitter *pxp_blitter, GstBuffer *video_frame, struct pxp_layer_param *layer_params);




GType gst_imx_pxp_blitter_rotation_mode_get_type(void)
{
	static GType gst_imx_pxp_blitter_rotation_mode_type = 0;

	if (!gst_imx_pxp_blitter_rotation_mode_type)
	{
		static GEnumValue rotation_mode_values[] =
		{
			{ GST_IMX_PXP_BLITTER_ROTATION_NONE, "No rotation", "none" },
			{ GST_IMX_PXP_BLITTER_ROTATION_HFLIP, "Flip horizontally", "horizontal-flip" },
			{ GST_IMX_PXP_BLITTER_ROTATION_VFLIP, "Flip vertically", "vertical-flip" },
			{ GST_IMX_PXP_BLITTER_ROTATION_90, "Rotate clockwise 90 degrees", "rotate-90" },
			{ GST_IMX_PXP_BLITTER_ROTATION_180, "Rotate 180 degrees", "rotate-180" },
			{ GST_IMX_PXP_BLITTER_ROTATION_270, "Rotate clockwise 270 degrees", "rotate-270" },
			{ 0, NULL, NULL },
		};

		gst_imx_pxp_blitter_rotation_mode_type = g_enum_register_static(
			"ImxPxPBlitterRotationMode",
			rotation_mode_values
		);
	}

	return gst_imx_pxp_blitter_rotation_mode_type;
}




void gst_imx_pxp_blitter_class_init(GstImxPxPBlitterClass *klass)
{
	GObjectClass *object_class;
	GstImxBaseBlitterClass *base_class;

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BASE_BLITTER_CLASS(klass);

	object_class->finalize             = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_finalize);
	base_class->set_input_video_info   = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_input_video_info);
	base_class->set_input_frame        = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_input_frame);
	base_class->set_output_frame       = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_output_frame);
	base_class->set_regions            = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_regions);
	base_class->get_phys_mem_allocator = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_get_phys_mem_allocator);
	base_class->blit_frame             = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_blit_frame);

	GST_DEBUG_CATEGORY_INIT(imx_pxp_blitter_debug, "imxpxpblitter", 0, "Freescale i.MX PxP blitter class");
}


void gst_imx_pxp_blitter_init(GstImxPxPBlitter *pxp_blitter)
{
	int ret;

	pxp_blitter->priv = g_slice_alloc(sizeof(GstImxPxPBlitterPrivate));
	pxp_blitter->priv->pxp_channel_requested = FALSE;

	struct pxp_proc_data *proc_data = &(pxp_blitter->priv->pxp_config.proc_data);
	memset(&(pxp_blitter->priv->pxp_config), 0, sizeof(struct pxp_config_data));
	proc_data->overlay_state = 0;
	proc_data->scaling = 0;
	proc_data->hflip = 0;
	proc_data->vflip = 0;
	proc_data->bgcolor = 0;
	proc_data->rotate = 0;
	proc_data->rot_pos = 0;
	proc_data->lut_transform = PXP_LUT_NONE;

	if (!gst_imx_pxp_init())
		GST_ERROR_OBJECT(pxp_blitter, "could not initialize PxP: %s", strerror(errno));

	ret = pxp_request_channel(&(pxp_blitter->priv->pxp_channel));
	if (ret != 0)
		GST_ERROR_OBJECT(pxp_blitter, "could not request PxP channel: %s", strerror(errno));
	else
		pxp_blitter->priv->pxp_channel_requested = TRUE;

	gst_imx_pxp_blitter_set_output_rotation(pxp_blitter, GST_IMX_PXP_BLITTER_OUTPUT_ROTATION_DEFAULT);
	pxp_blitter->apply_crop_metadata = GST_IMX_PXP_BLITTER_CROP_DEFAULT;
}


GstImxPxPBlitter* gst_imx_pxp_blitter_new(void)
{
	GstImxPxPBlitter* pxp_blitter = (GstImxPxPBlitter *)g_object_new(gst_imx_pxp_blitter_get_type(), NULL);

	return pxp_blitter;
}


void gst_imx_pxp_blitter_enable_crop(GstImxPxPBlitter *pxp_blitter, gboolean crop)
{
	GST_TRACE_OBJECT(pxp_blitter, "set crop to %d", crop);
	pxp_blitter->apply_crop_metadata = crop;
}


gboolean gst_imx_pxp_blitter_is_crop_enabled(GstImxPxPBlitter *pxp_blitter)
{
	return pxp_blitter->apply_crop_metadata;
}


GstImxPxPBlitterRotationMode gst_imx_pxp_blitter_get_output_rotation(GstImxPxPBlitter *pxp_blitter)
{
	struct pxp_proc_data *proc_data = &(pxp_blitter->priv->pxp_config.proc_data);

	if (proc_data->hflip)
		return GST_IMX_PXP_BLITTER_ROTATION_HFLIP;
	if (proc_data->vflip)
		return GST_IMX_PXP_BLITTER_ROTATION_VFLIP;

	switch (proc_data->rotate)
	{
		case 0: return GST_IMX_PXP_BLITTER_ROTATION_NONE;
		case 90: return GST_IMX_PXP_BLITTER_ROTATION_90;
		case 180: return GST_IMX_PXP_BLITTER_ROTATION_180;
		case 270: return GST_IMX_PXP_BLITTER_ROTATION_270;
		default: break;
	}

	return GST_IMX_PXP_BLITTER_ROTATION_NONE;
}


void gst_imx_pxp_blitter_set_output_rotation(GstImxPxPBlitter *pxp_blitter, GstImxPxPBlitterRotationMode rotation)
{
	struct pxp_proc_data *proc_data = &(pxp_blitter->priv->pxp_config.proc_data);

	switch (rotation)
	{
		case GST_IMX_PXP_BLITTER_ROTATION_NONE:
			proc_data->rotate = 0;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_PXP_BLITTER_ROTATION_90:
			proc_data->rotate = 90;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_PXP_BLITTER_ROTATION_180:
			proc_data->rotate = 180;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_PXP_BLITTER_ROTATION_270:
			proc_data->rotate = 270;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_PXP_BLITTER_ROTATION_HFLIP:
			proc_data->rotate = 0;
			proc_data->hflip = 1;
			proc_data->vflip = 0;
			break;

		case GST_IMX_PXP_BLITTER_ROTATION_VFLIP:
			proc_data->rotate = 0;
			proc_data->hflip = 0;
			proc_data->vflip = 1;
			break;
	}
}


static void gst_imx_pxp_blitter_finalize(GObject *object)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(object);
	g_assert(pxp_blitter != NULL);

	if (pxp_blitter->priv->pxp_channel_requested)
	{
		pxp_release_channel(&(pxp_blitter->priv->pxp_channel));
		pxp_blitter->priv->pxp_channel_requested = FALSE;
	}

	if (pxp_blitter->priv != NULL)
		g_slice_free1(sizeof(GstImxPxPBlitterPrivate), pxp_blitter->priv);

	gst_imx_pxp_shutdown();

	G_OBJECT_CLASS(gst_imx_pxp_blitter_parent_class)->finalize(object);
}


static gboolean gst_imx_pxp_blitter_set_input_video_info(G_GNUC_UNUSED GstImxBaseBlitter *base_blitter, G_GNUC_UNUSED GstVideoInfo *input_video_info)
{
	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_input_frame(GstImxBaseBlitter *base_blitter, GstBuffer *input_frame)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(base_blitter);
	struct pxp_config_data *pxp_config;
	GstVideoCropMeta *video_crop_meta;

	g_assert(pxp_blitter != NULL);

	pxp_config = &(pxp_blitter->priv->pxp_config);

	gst_imx_pxp_blitter_set_layer_params(pxp_blitter, input_frame, &(pxp_config->s0_param));

	if (pxp_blitter->apply_crop_metadata && ((video_crop_meta = gst_buffer_get_video_crop_meta(input_frame)) != NULL))
	{
		// TODO: set flag if crop rectangle is outside of the bounds of the frame

		pxp_config->proc_data.srect.left = video_crop_meta->x;
		pxp_config->proc_data.srect.top = video_crop_meta->y;
		pxp_config->proc_data.srect.width = MIN(video_crop_meta->x + video_crop_meta->width, pxp_config->s0_param.width);
		pxp_config->proc_data.srect.height = MIN(video_crop_meta->y + video_crop_meta->height, pxp_config->s0_param.height);
	}
	else
	{
		pxp_config->proc_data.srect.left = 0;
		pxp_config->proc_data.srect.top = 0;
		pxp_config->proc_data.srect.width = pxp_config->s0_param.width;
		pxp_config->proc_data.srect.height = pxp_config->s0_param.height;
	}

	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_output_frame(GstImxBaseBlitter *base_blitter, GstBuffer *output_frame)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(base_blitter);
	struct pxp_config_data *pxp_config;

	g_assert(pxp_blitter != NULL);

	pxp_config = &(pxp_blitter->priv->pxp_config);

	gst_imx_pxp_blitter_set_layer_params(pxp_blitter, output_frame, &(pxp_config->out_param));

	if ((pxp_config->proc_data.rotate == 90) || (pxp_config->proc_data.rotate == 270))
	{
		unsigned int i = pxp_config->out_param.width;
		pxp_config->out_param.width = pxp_config->out_param.height;
		pxp_config->out_param.height = i;
	}

	pxp_blitter->output_buffer_region.x1 = 0;
	pxp_blitter->output_buffer_region.y1 = 0;
	pxp_blitter->output_buffer_region.x2 = pxp_config->out_param.width;
	pxp_blitter->output_buffer_region.y2 = pxp_config->out_param.height;
	pxp_config->proc_data.drect.left = 0;
	pxp_config->proc_data.drect.top = 0;
	pxp_config->proc_data.drect.width = pxp_config->out_param.width;
	pxp_config->proc_data.drect.height = pxp_config->out_param.height;

	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(base_blitter);
	struct pxp_config_data *pxp_config;

	pxp_config = &(pxp_blitter->priv->pxp_config);

	if (output_region == NULL)
		output_region = &(pxp_blitter->output_buffer_region);

	if (video_region == NULL)
		video_region = output_region;

	if ((pxp_config->proc_data.rotate == 90) || (pxp_config->proc_data.rotate == 270))
	{
		pxp_config->proc_data.drect.left = video_region->y1;
		pxp_config->proc_data.drect.top = video_region->x1;
		pxp_config->proc_data.drect.width = video_region->y2 - video_region->y1;
		pxp_config->proc_data.drect.height = video_region->x2 - video_region->x1;
	}
	else
	{
		pxp_config->proc_data.drect.left = video_region->x1;
		pxp_config->proc_data.drect.top = video_region->y1;
		pxp_config->proc_data.drect.width = video_region->x2 - video_region->x1;
		pxp_config->proc_data.drect.height = video_region->y2 - video_region->y1;
	}

	return TRUE;
}


static GstAllocator* gst_imx_pxp_blitter_get_phys_mem_allocator(G_GNUC_UNUSED GstImxBaseBlitter *base_blitter)
{
	return gst_imx_pxp_allocator_new();
}


static gboolean gst_imx_pxp_blitter_blit_frame(GstImxBaseBlitter *base_blitter)
{
	int ret;
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(base_blitter);

	g_assert(pxp_blitter != NULL);

	if (!(pxp_blitter->priv->pxp_channel_requested))
	{
		GST_ERROR_OBJECT(pxp_blitter, "PxP channel handle wasn't requested - cannot blit");
		return FALSE;
	}

	pxp_blitter->priv->pxp_config.proc_data.scaling = (pxp_blitter->priv->pxp_config.proc_data.srect.width != pxp_blitter->priv->pxp_config.proc_data.drect.width) || (pxp_blitter->priv->pxp_config.proc_data.srect.height != pxp_blitter->priv->pxp_config.proc_data.drect.height);

	ret = pxp_config_channel(&(pxp_blitter->priv->pxp_channel), &(pxp_blitter->priv->pxp_config));
	if (ret != 0)
	{
		GST_ERROR_OBJECT(pxp_blitter, "could not configure PxP channel: %s", strerror(errno));
		return FALSE;
	}

	ret = pxp_start_channel(&(pxp_blitter->priv->pxp_channel));
	if (ret != 0)
	{
		GST_ERROR_OBJECT(pxp_blitter, "could not start PxP channel: %s", strerror(errno));
		return FALSE;
	}

	ret = pxp_wait_for_completion(&(pxp_blitter->priv->pxp_channel), 3);
	if (ret != 0)
	{
		GST_ERROR_OBJECT(pxp_blitter, "could not wait for PxP channel completion: %s", strerror(errno));
		return FALSE;
	}

	return TRUE;
}


static GstImxPxPFormatDetails const * gst_imx_pxp_blitter_get_pxp_format_details(GstVideoFormat gstfmt)
{
#define FORMAT_DETAILS(PXPFMT, BPP) \
		do { static GstImxPxPFormatDetails const d = { PXPFMT, BPP }; return &d; } while (0)

	/* NOTE: for the 32-bit RGB(A) formats, PxP RGB == Gst BGR , and vice versa */
	switch (gstfmt)
	{
		/* packed-pixel formats */
		case GST_VIDEO_FORMAT_RGBx: FORMAT_DETAILS(PXP_PIX_FMT_BGR32, 32);
		case GST_VIDEO_FORMAT_BGRx: FORMAT_DETAILS(PXP_PIX_FMT_RGB32, 32);
		case GST_VIDEO_FORMAT_RGBA: FORMAT_DETAILS(PXP_PIX_FMT_RGBA32, 32);
		case GST_VIDEO_FORMAT_BGRA: FORMAT_DETAILS(PXP_PIX_FMT_BGRA32, 32);
		case GST_VIDEO_FORMAT_ARGB: FORMAT_DETAILS(PXP_PIX_FMT_ABGR32, 32);
		case GST_VIDEO_FORMAT_RGB: FORMAT_DETAILS(PXP_PIX_FMT_RGB24, 24);
		case GST_VIDEO_FORMAT_BGR: FORMAT_DETAILS(PXP_PIX_FMT_BGR24, 24);
		case GST_VIDEO_FORMAT_RGB16: FORMAT_DETAILS(PXP_PIX_FMT_RGB565, 16);
		case GST_VIDEO_FORMAT_RGB15: FORMAT_DETAILS(PXP_PIX_FMT_RGB555, 16);
		case GST_VIDEO_FORMAT_GRAY8: FORMAT_DETAILS(PXP_PIX_FMT_GREY, 8);
		case GST_VIDEO_FORMAT_YUY2: FORMAT_DETAILS(PXP_PIX_FMT_YUYV, 16);
		case GST_VIDEO_FORMAT_UYVY: FORMAT_DETAILS(PXP_PIX_FMT_UYVY, 16);
		case GST_VIDEO_FORMAT_YVYU: FORMAT_DETAILS(PXP_PIX_FMT_YVYU, 16);
		case GST_VIDEO_FORMAT_v308: FORMAT_DETAILS(PXP_PIX_FMT_YUV444, 24);
		case GST_VIDEO_FORMAT_IYU1: FORMAT_DETAILS(PXP_PIX_FMT_Y41P, 12);

		/* planar formats; bits per pixel is always 8 for these */
		case GST_VIDEO_FORMAT_I420: FORMAT_DETAILS(PXP_PIX_FMT_YUV420P, 8);
		case GST_VIDEO_FORMAT_YV12: FORMAT_DETAILS(PXP_PIX_FMT_YVU420P, 8);
		case GST_VIDEO_FORMAT_Y42B: FORMAT_DETAILS(PXP_PIX_FMT_YUV422P, 8);
		case GST_VIDEO_FORMAT_NV12: FORMAT_DETAILS(PXP_PIX_FMT_NV12, 8);
		case GST_VIDEO_FORMAT_NV21: FORMAT_DETAILS(PXP_PIX_FMT_NV21, 8);
		case GST_VIDEO_FORMAT_NV16: FORMAT_DETAILS(PXP_PIX_FMT_NV16, 8);
		case GST_VIDEO_FORMAT_YUV9: FORMAT_DETAILS(PXP_PIX_FMT_YUV410P, 8);
		case GST_VIDEO_FORMAT_YVU9: FORMAT_DETAILS(PXP_PIX_FMT_YVU410P, 8);

		default: return NULL;
	}

#undef FORMAT_DETAILS
}


static void gst_imx_pxp_blitter_set_layer_params(G_GNUC_UNUSED GstImxPxPBlitter *pxp_blitter, GstBuffer *video_frame, struct pxp_layer_param *layer_params)
{
	GstVideoMeta *video_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstImxPxPFormatDetails const *format_details;

	g_assert(video_frame != NULL);

	video_meta = gst_buffer_get_video_meta(video_frame);
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(video_frame);

	g_assert((video_meta != NULL) && (phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0));

	format_details = gst_imx_pxp_blitter_get_pxp_format_details(video_meta->format);
	g_assert(format_details != NULL);

	layer_params->width = video_meta->width;
	layer_params->height = video_meta->height;
	layer_params->stride = video_meta->stride[0] * 8 / format_details->bits_per_pixel;
	layer_params->pixel_fmt = format_details->format;

	layer_params->combine_enable = false;

	layer_params->color_key_enable = 0;
	layer_params->color_key = 0x00000000;
	layer_params->global_alpha_enable = false;
	layer_params->global_override = false;
	layer_params->global_alpha = 0;
	layer_params->alpha_invert = false;
	layer_params->local_alpha_enable = false;

	layer_params->paddr = (dma_addr_t)(phys_mem_meta->phys_addr);
}
