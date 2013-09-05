#include "blitter.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/mxcfb.h>
#include <linux/ipu.h>
#include <gst/video/gstvideometa.h>
#include "../common/phys_mem_meta.h"
#include "buffer_pool.h"


GST_DEBUG_CATEGORY_STATIC(ipu_blitter_debug);
#define GST_CAT_DEFAULT ipu_blitter_debug


G_DEFINE_TYPE(GstFslIpuBlitter, gst_fsl_ipu_blitter, GST_TYPE_OBJECT)


struct _GstFslIpuBlitterPrivate
{
	int ipu_fd;
	struct ipu_task task;
};


typedef struct
{
	guint fb_size;
	void* mapped_fb_address;
}
FBMapData;


static void gst_fsl_ipu_blitter_finalize(GObject *object);
static guint32 gst_fsl_ipu_blitter_get_v4l_format(GstVideoFormat format);
static GstVideoFormat gst_fsl_ipu_blitter_get_format_from_fb(GstFslIpuBlitter *ipu_blitter, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix);
static void gst_fsl_ipu_blitter_unmap_wrapped_framebuffer(gpointer data);





void gst_fsl_ipu_blitter_class_init(GstFslIpuBlitterClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_fsl_ipu_blitter_finalize);

	GST_DEBUG_CATEGORY_INIT(ipu_blitter_debug, "ipublitter", 0, "Freescale IPU blitter operations");
}


void gst_fsl_ipu_blitter_init(GstFslIpuBlitter *ipu_blitter)
{
	ipu_blitter->priv = g_slice_alloc(sizeof(GstFslIpuBlitterPrivate));

	ipu_blitter->priv->ipu_fd = open("/dev/mxc_ipu", O_RDWR, 0);
	if (ipu_blitter->priv->ipu_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_blitter, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/mxc_ipu: %s", strerror(errno)), (NULL));
		return;
	}

	ipu_blitter->internal_bufferpool = NULL;
	ipu_blitter->internal_input_buffer = NULL;
	ipu_blitter->input_frame = NULL;
	ipu_blitter->output_frame = NULL;

	memset(&(ipu_blitter->priv->task), 0, sizeof(struct ipu_task));
}


static void gst_fsl_ipu_blitter_finalize(GObject *object)
{
	GstFslIpuBlitter *ipu_blitter = GST_FSL_IPU_BLITTER(object);

	G_OBJECT_CLASS(gst_fsl_ipu_blitter_parent_class)->finalize(object);

	if (ipu_blitter->input_frame != NULL)
		gst_video_frame_unmap(ipu_blitter->input_frame);
	if (ipu_blitter->output_frame != NULL)
		gst_video_frame_unmap(ipu_blitter->output_frame);
	if (ipu_blitter->internal_input_buffer != NULL)
		gst_buffer_unref(ipu_blitter->internal_input_buffer);
	if (ipu_blitter->internal_bufferpool != NULL)
		gst_object_unref(ipu_blitter->internal_bufferpool);


	if (ipu_blitter->priv != NULL)
	{
		if (ipu_blitter->priv->ipu_fd >= 0)
			close(ipu_blitter->priv->ipu_fd);
		g_slice_free1(sizeof(GstFslIpuBlitterPrivate), ipu_blitter->priv);
	}
}


static guint32 gst_fsl_ipu_blitter_get_v4l_format(GstVideoFormat format)
{
	switch (format)
	{
		case GST_VIDEO_FORMAT_RGB15: return IPU_PIX_FMT_RGB555;
		case GST_VIDEO_FORMAT_RGB16: return IPU_PIX_FMT_RGB565;
		case GST_VIDEO_FORMAT_BGR: return IPU_PIX_FMT_BGR24;
		case GST_VIDEO_FORMAT_RGB: return IPU_PIX_FMT_RGB24;
#if 0
		case GST_VIDEO_FORMAT_GBR: return IPU_PIX_FMT_GBR24;
#endif
		case GST_VIDEO_FORMAT_BGRx: return IPU_PIX_FMT_BGR32;
		case GST_VIDEO_FORMAT_BGRA: return IPU_PIX_FMT_BGRA32;
		case GST_VIDEO_FORMAT_RGBx: return IPU_PIX_FMT_RGB32;
		case GST_VIDEO_FORMAT_RGBA: return IPU_PIX_FMT_RGBA32;
		case GST_VIDEO_FORMAT_ABGR: return IPU_PIX_FMT_ABGR32;
		case GST_VIDEO_FORMAT_UYVY: return IPU_PIX_FMT_UYVY;
		case GST_VIDEO_FORMAT_YVYU: return IPU_PIX_FMT_YVYU;
		case GST_VIDEO_FORMAT_IYU1: return IPU_PIX_FMT_Y41P;
		case GST_VIDEO_FORMAT_v308: return IPU_PIX_FMT_YUV444;
		case GST_VIDEO_FORMAT_NV12: return IPU_PIX_FMT_NV12;
		case GST_VIDEO_FORMAT_GRAY8: return IPU_PIX_FMT_GREY;
		case GST_VIDEO_FORMAT_YVU9: return IPU_PIX_FMT_YVU410P;
		case GST_VIDEO_FORMAT_YUV9: return IPU_PIX_FMT_YUV410P;
		case GST_VIDEO_FORMAT_YV12: return IPU_PIX_FMT_YVU420P;
		case GST_VIDEO_FORMAT_I420: return IPU_PIX_FMT_YUV420P;
		case GST_VIDEO_FORMAT_Y42B: return IPU_PIX_FMT_YUV422P;
		case GST_VIDEO_FORMAT_Y444: return IPU_PIX_FMT_YUV444P;
		default:
			GST_WARNING("Unknown format %d (%s)", (gint)format, gst_video_format_to_string(format));
			return 0;
	}
}


static GstVideoFormat gst_fsl_ipu_blitter_get_format_from_fb(GstFslIpuBlitter *ipu_blitter, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix)
{
	GstVideoFormat fmt = GST_VIDEO_FORMAT_UNKNOWN;
	guint rlen = fb_var->red.length, glen = fb_var->green.length, blen = fb_var->blue.length, alen = fb_var->transp.length;
	guint rofs = fb_var->red.offset, gofs = fb_var->green.offset, bofs = fb_var->blue.offset, aofs = fb_var->transp.offset;

	if (fb_fix->type != FB_TYPE_PACKED_PIXELS)
	{
		GST_DEBUG_OBJECT(ipu_blitter, "unknown framebuffer type %d", fb_fix->type);
		return fmt;
	}

	if (fb_fix->type != FB_TYPE_PACKED_PIXELS)
	{
		GST_DEBUG_OBJECT(ipu_blitter, "unknown framebuffer type %d", fb_fix->type);
		return fmt;
	}

	switch (fb_var->bits_per_pixel)
	{
		case 15:
		{
			if ((rlen == 5) && (glen == 5) && (blen == 5))
				fmt = GST_VIDEO_FORMAT_RGB15;
			break;
		}
		case 16:
		{
			if ((rlen == 5) && (glen == 6) && (blen == 5))
				fmt = GST_VIDEO_FORMAT_RGB16;
			break;
		}
		case 24:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16))
					fmt = GST_VIDEO_FORMAT_RGB;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0))
					fmt = GST_VIDEO_FORMAT_BGR;
#if 0
				else if ((rofs == 16) && (gofs == 0) && (bofs == 8))
					fmt = GST_VIDEO_FORMAT_GBR;
#endif
			}
			break;
		}
		case 32:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8) && (alen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16) && (aofs == 24))
					fmt = GST_VIDEO_FORMAT_RGBA;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0) && (aofs == 24))
					fmt = GST_VIDEO_FORMAT_BGRA;
				else if ((rofs == 24) && (gofs == 16) && (bofs == 8) && (aofs == 0))
					fmt = GST_VIDEO_FORMAT_ABGR;
			}
			break;
		}
		default:
			break;
	}

	GST_DEBUG_OBJECT(
		ipu_blitter,
		"framebuffer uses %u bpp (sizes: r %u g %u b %u  offsets: r %u g %u b %u) => format %s",
		fb_var->bits_per_pixel,
		rlen, glen, blen,
		rofs, gofs, bofs,
		gst_video_format_to_string(fmt)
	);

	return fmt;
}


#define GST_FSL_FILL_IPU_TASK(ipu_blitter, frame, taskio) \
do { \
 \
	guint num_extra_lines; \
	GstVideoCropMeta *video_crop_meta; \
	GstFslPhysMemMeta *phys_mem_meta; \
 \
	video_crop_meta = gst_buffer_get_video_crop_meta((frame)->buffer); \
	phys_mem_meta = GST_FSL_PHYS_MEM_META_GET((frame)->buffer); \
 \
	g_assert(phys_mem_meta != NULL); \
 \
	num_extra_lines = phys_mem_meta->padding / (frame)->info.stride[0]; \
	(taskio).width = (frame)->info.stride[0]; \
	(taskio).height = (frame)->info.height + num_extra_lines; \
 \
	if ((video_crop_meta != NULL)) \
	{ \
		if ((video_crop_meta->x >= (guint)((frame)->info.width)) || (video_crop_meta->y >= (guint)((frame)->info.height))) \
			return FALSE; \
 \
		(taskio).crop.pos.x = video_crop_meta->x; \
		(taskio).crop.pos.y = video_crop_meta->y; \
		(taskio).crop.w = MIN(video_crop_meta->width, (taskio).width - video_crop_meta->x); \
		(taskio).crop.h = MIN(video_crop_meta->height, (taskio).height - video_crop_meta->y); \
	} \
	else \
	{ \
		(taskio).crop.pos.x = 0; \
		(taskio).crop.pos.y = 0; \
		(taskio).crop.w = (taskio).width; \
		(taskio).crop.h = (taskio).height; \
	} \
 \
	(taskio).paddr = (dma_addr_t)(phys_mem_meta->phys_addr); \
	(taskio).format = gst_fsl_ipu_blitter_get_v4l_format(GST_VIDEO_INFO_FORMAT(&((frame)->info))); \
} while (0)


gboolean gst_fsl_ipu_blitter_set_input_frame(GstFslIpuBlitter *ipu_blitter, GstVideoFrame *input_frame)
{
	g_assert(input_frame != NULL);

	if ((ipu_blitter->input_frame != input_frame) && (ipu_blitter->input_frame != NULL))
	{
		gst_video_frame_unmap(ipu_blitter->input_frame);
		ipu_blitter->input_frame = NULL;
	}

	GST_FSL_FILL_IPU_TASK(ipu_blitter, input_frame, ipu_blitter->priv->task.input);

	/* NOT a deep copy - these are pointers! Frames must exist at least until the blit() call is invoked */
	ipu_blitter->input_frame = input_frame; 

	return TRUE;
}


gboolean gst_fsl_ipu_blitter_set_output_frame(GstFslIpuBlitter *ipu_blitter, GstVideoFrame *output_frame)
{
	g_assert(output_frame != NULL);

	if ((ipu_blitter->output_frame != output_frame) && (ipu_blitter->output_frame != NULL))
	{
		gst_video_frame_unmap(ipu_blitter->output_frame);
		ipu_blitter->output_frame = NULL;
	}

	GST_FSL_FILL_IPU_TASK(ipu_blitter, output_frame, ipu_blitter->priv->task.output);

	/* NOT a deep copy - these are pointers! Frames must exist at least until the blit() call is invoked */
	ipu_blitter->output_frame = output_frame; 

	return TRUE;
}


gboolean gst_fsl_ipu_blitter_set_incoming_frame(GstFslIpuBlitter *ipu_blitter, GstVideoFrame *incoming_frame)
{
	GstFslPhysMemMeta *phys_mem_meta;

	g_assert(incoming_frame != NULL);

	phys_mem_meta = GST_FSL_PHYS_MEM_META_GET(incoming_frame->buffer);

	if (phys_mem_meta != NULL)
		gst_fsl_ipu_blitter_set_input_frame(ipu_blitter, incoming_frame);
	else
	{
		/* Create temp input frame using our bufferpool and copy the incoming frame into it */

		if (ipu_blitter->internal_input_buffer == NULL)
		{
			GstFlowReturn flow_ret;

			if (ipu_blitter->internal_bufferpool == NULL)
			{
				GstCaps *caps = gst_video_info_to_caps(&(ipu_blitter->input_video_info));

				ipu_blitter->internal_bufferpool = gst_fsl_ipu_blitter_create_bufferpool(
					ipu_blitter,
					caps,
					ipu_blitter->input_video_info.size,
					2, 0,
					NULL,
					NULL
				);

				gst_caps_unref(caps); // TODO: necessary?

				if (ipu_blitter->internal_bufferpool)
				{
					GST_ERROR_OBJECT(ipu_blitter, "failed to create internal bufferpool");
					return FALSE;
				}
			}


			if (!gst_buffer_pool_is_active(ipu_blitter->internal_bufferpool))
				gst_buffer_pool_set_active(ipu_blitter->internal_bufferpool, TRUE);

			flow_ret = gst_buffer_pool_acquire_buffer(ipu_blitter->internal_bufferpool, &(ipu_blitter->internal_input_buffer), NULL);
			if (flow_ret != GST_FLOW_OK)
			{
				GST_ERROR_OBJECT(ipu_blitter, "error acquiring input frame buffer: %s", gst_pad_mode_get_name(flow_ret));
				return FALSE;
			}
		}

		gst_video_frame_map(&(ipu_blitter->temp_input_video_frame), &(ipu_blitter->input_video_info), ipu_blitter->internal_input_buffer, GST_MAP_WRITE);
		gst_video_frame_copy(&(ipu_blitter->temp_input_video_frame), incoming_frame);
		gst_video_frame_unmap(&(ipu_blitter->temp_input_video_frame));

		gst_fsl_ipu_blitter_set_input_frame(ipu_blitter, &(ipu_blitter->temp_input_video_frame));
	}

	return TRUE;
}


void gst_fsl_ipu_blitter_set_input_info(GstFslIpuBlitter *ipu_blitter, GstVideoInfo *info)
{
	ipu_blitter->input_video_info = *info;

	if (ipu_blitter->internal_bufferpool != NULL)
	{
		gst_object_unref(ipu_blitter->internal_bufferpool);
		ipu_blitter->internal_bufferpool = NULL;
	}
}


gboolean gst_fsl_ipu_blitter_blit(GstFslIpuBlitter *ipu_blitter)
{
	g_assert(ipu_blitter->input_frame != NULL);
	g_assert(ipu_blitter->output_frame != NULL);

	if (ioctl(ipu_blitter->priv->ipu_fd, IPU_QUEUE_TASK, &(ipu_blitter->priv->task)) == -1)
	{
		GST_ERROR_OBJECT(ipu_blitter, "queuing IPU task failed: %s", strerror(errno));
		return FALSE;
	}

	return TRUE;
}


GstBufferPool* gst_fsl_ipu_blitter_create_bufferpool(GstFslIpuBlitter *ipu_blitter, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params)
{
	GstBufferPool *pool;
	GstStructure *config;
	
	pool = gst_fsl_ipu_buffer_pool_new(ipu_blitter->priv->ipu_fd, FALSE);

	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, caps, size, min_buffers, max_buffers);
	if (allocator != NULL)
	{
		g_assert(alloc_params != NULL);
		gst_buffer_pool_config_set_allocator(config, allocator, alloc_params);
	}
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_FSL_PHYS_MEM);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(pool, config);

	return pool;
}


GstBufferPool* gst_fsl_ipu_blitter_get_internal_bufferpool(GstFslIpuBlitter *ipu_blitter)
{
	return ipu_blitter->internal_bufferpool;
}


GstBuffer* gst_fsl_ipu_blitter_wrap_framebuffer(GstFslIpuBlitter *ipu_blitter, int framebuffer_fd, guint x, guint y, guint width, guint height)
{
	guint fb_size, fb_width, fb_height;
	GstVideoFormat fb_format;
	GstBuffer *buffer;
	GstFslPhysMemMeta *phys_mem_meta;
	FBMapData *map_data;
	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	if (ioctl(framebuffer_fd, FBIOGET_FSCREENINFO, &fb_fix) == -1)
	{
		GST_ERROR_OBJECT(ipu_blitter, "could not open get fixed screen info: %s", strerror(errno));
		return NULL;
	}

	if (ioctl(framebuffer_fd, FBIOGET_VSCREENINFO, &fb_var) == -1)
	{
		GST_ERROR_OBJECT(ipu_blitter, "could not open get variable screen info: %s", strerror(errno));
		return NULL;
	}

	fb_width = fb_var.xres;
	fb_height = fb_var.yres;
	fb_format = gst_fsl_ipu_blitter_get_format_from_fb(ipu_blitter, &fb_var, &fb_fix);
	fb_size = fb_var.xres * fb_var.yres * fb_var.bits_per_pixel / 8;

	map_data = g_slice_alloc(sizeof(FBMapData));
	map_data->fb_size = fb_size;
	map_data->mapped_fb_address = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, framebuffer_fd, fb_fix.smem_start);
	if (map_data->mapped_fb_address == MAP_FAILED)
	{
		GST_ERROR_OBJECT(ipu_blitter, "memory-mapping the Linux framebuffer failed: %s", strerror(errno));
		return NULL;
	}

	buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_NO_SHARE, map_data->mapped_fb_address, fb_size, 0, fb_size, map_data, gst_fsl_ipu_blitter_unmap_wrapped_framebuffer);
	gst_buffer_add_video_meta(buffer, GST_VIDEO_FRAME_FLAG_NONE, fb_format, fb_width, fb_height);

	if ((width != 0) && (height != 0))
	{
		GstVideoCropMeta *video_crop_meta;
		
		video_crop_meta = gst_buffer_add_video_crop_meta(buffer);
		video_crop_meta->x = x;
		video_crop_meta->y = y;
		video_crop_meta->width = width;
		video_crop_meta->height = height;
	}

	phys_mem_meta = GST_FSL_PHYS_MEM_META_ADD(buffer);
	phys_mem_meta->phys_addr = (gpointer)(fb_fix.smem_start);

	return buffer;
}


static void gst_fsl_ipu_blitter_unmap_wrapped_framebuffer(gpointer data)
{
	FBMapData *map_data = (FBMapData *)data;
	if (munmap(map_data->mapped_fb_address, map_data->fb_size) == -1)
		GST_ERROR("unmapping memory-mapped Linux framebuffer failed: %s", strerror(errno));
	g_slice_free1(sizeof(FBMapData), data);
}

