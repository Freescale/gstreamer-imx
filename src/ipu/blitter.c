/* Common Freescale IPU blitter code for GStreamer
 * Copyright (C) 2013  Carlos Rafael Giani
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


#include "blitter.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/fb.h>
#include <linux/ipu.h>
#include <gst/video/gstvideometa.h>
#include "../common/phys_mem_meta.h"
#include "../common/phys_mem_buffer_pool.h"
#include "allocator.h"



/* Information about the blitter:
 *
 * The Freescale i.MX IPU provides 2D blitting functionality. It can scale, convert between
 * colorspaces, deinterlace, rotate in one step. Frames are read from and blitted into DMA buffers
 * (= physically contiguous memory blocks).
 * It is much more efficient to use the IPU for the aforementioned operations, since it avoids
 * transfers through the main bus, and avoids having to deal with DMA page cache invalidations.
 *
 * In GStreamer, the IPU is useful video frame output to the Linux framebuffer, and for
 * video transformation operations (the ones mentioned earlier). As a consequence, a sink and a
 * videotransform element have been created. Both essentially do the same thing (blitting with
 * transformation operations). The only difference is the destination: in the sink, it is
 * the Linux framebuffer; in the videotransform element, it is an output DMA buffer wrapped in a
 * GstBuffer. To reuse code, the GstImxIpuBlitter object was introduced. It takes care of setting
 * up the IPU and performing the blitting. All the sink and videotransform element have to do is
 * to set up the input and output frames.
 *
 * The GstImxIpuBlitter object also takes care of a special case: when the input isn't a DMA buffer.
 * Then, a memory copy has to be done. For this reason, the object identifies three types of frames:
 * incoming, input, and output frames. Input and output frames are tied directly to the IPU and
 * must be backed by a DMA buffer. Incoming frames will be tested; if they are backed by a DMA
 * buffer, then they are set as input frames. If not, an internal input frame is allocated, and the
 * contents of the incoming frame are copied into it.
 * The reason for this is that in GStreamer, elements can freely decide how they create their own
 * buffers and what allocators they use for this, but cannot force upstream elements to use a
 * specific allocator. In other words, downstream can only accept or refuse incoming GstBuffers;
 * it cannot dictate upstream how these shall allocate the memory inside the buffers. Upstream can
 * respect proposals sent by downstream, but is free to ignore them.
 *
 * Incoming, input, and output data is expected in form of GstVideoFrame instances. The GstImxIpuBlitter
 * object does not take ownership over these frames. Therefore, if a gst_imx_ipu_blitter_blit() call is to
 * be made, the frames must continue exist at least until then. (The one exception is the internal temporary
 * input frame, which is managed by GstImxIpuBlitter.)
 *
 * TODO: Currently, this code includes the linux/ipu.h header, which resides in the kernel.
 * Responses from Freescale indicate there is currently no other way how to do it. Fix this once there is#
 * a better mechanism.
 */



GST_DEBUG_CATEGORY_STATIC(imx_ipu_blitter_debug);
#define GST_CAT_DEFAULT imx_ipu_blitter_debug


G_DEFINE_TYPE(GstImxIpuBlitter, gst_imx_ipu_blitter, GST_TYPE_OBJECT)


/* Private structure storing IPU specific data */
struct _GstImxIpuBlitterPrivate
{
	int ipu_fd;
	struct ipu_task task;
};


/* Linux Framebuffer data; this struct is used with the gst_imx_ipu_blitter_unmap_wrapped_framebuffer()
 * call */
typedef struct
{
	guint fb_size;
	void* mapped_fb_address;
}
FBMapData;


static void gst_imx_ipu_blitter_finalize(GObject *object);
static guint32 gst_imx_ipu_blitter_get_v4l_format(GstVideoFormat format);
static GstVideoFormat gst_imx_ipu_blitter_get_format_from_fb(GstImxIpuBlitter *ipu_blitter, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix);
static int gst_imx_ipu_video_bpp(GstVideoFormat fmt);
gboolean gst_imx_ipu_blitter_set_actual_input_buffer(GstImxIpuBlitter *ipu_blitter, GstBuffer *actual_input_buffer);





GType gst_imx_ipu_blitter_rotation_mode_get_type(void)
{
	static GType gst_imx_ipu_blitter_rotation_mode_type = 0;

	if (!gst_imx_ipu_blitter_rotation_mode_type)
	{
		static GEnumValue rotation_mode_values[] =
		{
			{ GST_IMX_IPU_BLITTER_ROTATION_NONE, "No rotation", "none" },
			{ GST_IMX_IPU_BLITTER_ROTATION_HFLIP, "Flip horizontally", "horizontal-flip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_VFLIP, "Flip vertically", "vertical-flip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_180, "Rotate 180 degrees", "rotate-180" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CW, "Rotate clockwise 90 degrees", "rotate-90cw" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP, "Rotate 180 degrees and flip horizontally", "rotate-90cw-hflip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP, "Rotate 180 degrees and flip vertically", "rotate-90cw-vflip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CCW, "Rotate counter-clockwise 90 degrees", "rotate-90ccw" },
			{ 0, NULL, NULL },
		};

		gst_imx_ipu_blitter_rotation_mode_type = g_enum_register_static(
			"ImxIpuBlitterRotationMode",
			rotation_mode_values
		);
	}

	return gst_imx_ipu_blitter_rotation_mode_type;
}


GType gst_imx_ipu_blitter_deinterlace_mode_get_type(void)
{
	static GType gst_imx_ipu_blitter_deinterlace_mode_type = 0;

	if (!gst_imx_ipu_blitter_deinterlace_mode_type)
	{
		static GEnumValue deinterlace_mode_values[] =
		{
			{ GST_IMX_IPU_BLITTER_DEINTERLACE_NONE, "No deinterlacing", "none" },
			{ GST_IMX_IPU_BLITTER_DEINTERLACE_FAST_MOTION, "Fast-motion deinterlacing (uses one input frames for one output frame)", "fast-motion" },
			{ 0, NULL, NULL },
		};

		gst_imx_ipu_blitter_deinterlace_mode_type = g_enum_register_static(
			"ImxIpuBlitterDeinterlaceMode",
			deinterlace_mode_values
		);
	}

	return gst_imx_ipu_blitter_deinterlace_mode_type;
}





void gst_imx_ipu_blitter_class_init(GstImxIpuBlitterClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_finalize);

	GST_DEBUG_CATEGORY_INIT(imx_ipu_blitter_debug, "imxipublitter", 0, "Freescale i.MX IPU blitter operations");
}


void gst_imx_ipu_blitter_init(GstImxIpuBlitter *ipu_blitter)
{
	ipu_blitter->priv = g_slice_alloc(sizeof(GstImxIpuBlitterPrivate));

	/* This FD is necessary for using the IPU ioctls */
	ipu_blitter->priv->ipu_fd = open("/dev/mxc_ipu", O_RDWR, 0);
	if (ipu_blitter->priv->ipu_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_blitter, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/mxc_ipu: %s", strerror(errno)), (NULL));
		return;
	}

	ipu_blitter->internal_bufferpool = NULL;
	ipu_blitter->actual_input_buffer = NULL;
	ipu_blitter->apply_crop_metadata = GST_IMX_IPU_BLITTER_CROP_DEFAULT;
	ipu_blitter->deinterlace_mode = GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT;

	memset(&(ipu_blitter->priv->task), 0, sizeof(struct ipu_task));
}


static void gst_imx_ipu_blitter_finalize(GObject *object)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(object);

	if (ipu_blitter->actual_input_buffer != NULL)
		gst_buffer_unref(ipu_blitter->actual_input_buffer);
	if (ipu_blitter->internal_bufferpool != NULL)
		gst_object_unref(ipu_blitter->internal_bufferpool);

	if (ipu_blitter->priv != NULL)
	{
		if (ipu_blitter->priv->ipu_fd >= 0)
			close(ipu_blitter->priv->ipu_fd);
		g_slice_free1(sizeof(GstImxIpuBlitterPrivate), ipu_blitter->priv);
	}

	G_OBJECT_CLASS(gst_imx_ipu_blitter_parent_class)->finalize(object);
}


void gst_imx_ipu_blitter_enable_crop(GstImxIpuBlitter *ipu_blitter, gboolean crop)
{
	ipu_blitter->apply_crop_metadata = crop;
}


gboolean gst_imx_ipu_blitter_is_crop_enabled(GstImxIpuBlitter *ipu_blitter)
{
	return ipu_blitter->apply_crop_metadata;
}


void gst_imx_ipu_blitter_set_output_rotation_mode(GstImxIpuBlitter *ipu_blitter, GstImxIpuBlitterRotationMode rotation_mode)
{
	switch (rotation_mode)
	{
		case GST_IMX_IPU_BLITTER_ROTATION_NONE:       ipu_blitter->priv->task.output.rotate = IPU_ROTATE_NONE; break;
		case GST_IMX_IPU_BLITTER_ROTATION_HFLIP:      ipu_blitter->priv->task.output.rotate = IPU_ROTATE_HORIZ_FLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_VFLIP:      ipu_blitter->priv->task.output.rotate = IPU_ROTATE_VERT_FLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_180:        ipu_blitter->priv->task.output.rotate = IPU_ROTATE_180; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CW:       ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_RIGHT; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP: ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_RIGHT_HFLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP: ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_RIGHT_VFLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CCW:      ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_LEFT; break;
	}
}


GstImxIpuBlitterRotationMode gst_imx_ipu_blitter_get_output_rotation_mode(GstImxIpuBlitter *ipu_blitter)
{
	switch (ipu_blitter->priv->task.output.rotate)
	{
		case IPU_ROTATE_NONE:           return GST_IMX_IPU_BLITTER_ROTATION_NONE;
		case IPU_ROTATE_HORIZ_FLIP:     return GST_IMX_IPU_BLITTER_ROTATION_HFLIP;
		case IPU_ROTATE_VERT_FLIP:      return GST_IMX_IPU_BLITTER_ROTATION_VFLIP;
		case IPU_ROTATE_180:            return GST_IMX_IPU_BLITTER_ROTATION_180;
		case IPU_ROTATE_90_RIGHT:       return GST_IMX_IPU_BLITTER_ROTATION_90CW;
		case IPU_ROTATE_90_RIGHT_HFLIP: return GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP;
		case IPU_ROTATE_90_RIGHT_VFLIP: return GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP;
		case IPU_ROTATE_90_LEFT:        return GST_IMX_IPU_BLITTER_ROTATION_90CCW;
		default:                        return GST_IMX_IPU_BLITTER_ROTATION_NONE;
	}
}


void gst_imx_ipu_blitter_set_deinterlace_mode(GstImxIpuBlitter *ipu_blitter, GstImxIpuBlitterDeinterlaceMode deinterlace_mode)
{
	ipu_blitter->deinterlace_mode = deinterlace_mode;
}


GstImxIpuBlitterDeinterlaceMode gst_imx_ipu_blitter_get_deinterlace_mode(GstImxIpuBlitter *ipu_blitter)
{
	return ipu_blitter->deinterlace_mode;
}


gboolean gst_imx_ipu_blitter_are_transforms_enabled(GstImxIpuBlitter *ipu_blitter)
{
	return
		(ipu_blitter->deinterlace_mode != GST_IMX_IPU_BLITTER_DEINTERLACE_NONE) ||
		(ipu_blitter->priv->task.output.rotate != IPU_ROTATE_NONE) ||
		ipu_blitter->apply_crop_metadata;
}


static guint32 gst_imx_ipu_blitter_get_v4l_format(GstVideoFormat format)
{
	switch (format)
	{
		/* These formats are defined in ipu.h , but the IPU reports them as
		 * being unsupported.
		 * TODO: It is currently not known how to find out which formats are supported,
		 * or if different i.MX versions support different formats.
		 */
#if 0
		case GST_VIDEO_FORMAT_RGB15: return IPU_PIX_FMT_RGB555;
		case GST_VIDEO_FORMAT_GBR: return IPU_PIX_FMT_GBR24;
		case GST_VIDEO_FORMAT_YVYU: return IPU_PIX_FMT_YVYU;
		case GST_VIDEO_FORMAT_IYU1: return IPU_PIX_FMT_Y41P;
		case GST_VIDEO_FORMAT_GRAY8: return IPU_PIX_FMT_GREY;
		case GST_VIDEO_FORMAT_YVU9: return IPU_PIX_FMT_YVU410P;
		case GST_VIDEO_FORMAT_YUV9: return IPU_PIX_FMT_YUV410P;
#endif
		case GST_VIDEO_FORMAT_RGB16: return IPU_PIX_FMT_RGB565;
		case GST_VIDEO_FORMAT_BGR: return IPU_PIX_FMT_BGR24;
		case GST_VIDEO_FORMAT_RGB: return IPU_PIX_FMT_RGB24;
		case GST_VIDEO_FORMAT_BGRx: return IPU_PIX_FMT_BGR32;
		case GST_VIDEO_FORMAT_BGRA: return IPU_PIX_FMT_BGRA32;
		case GST_VIDEO_FORMAT_RGBx: return IPU_PIX_FMT_RGB32;
		case GST_VIDEO_FORMAT_RGBA: return IPU_PIX_FMT_RGBA32;
		case GST_VIDEO_FORMAT_ABGR: return IPU_PIX_FMT_ABGR32;
		case GST_VIDEO_FORMAT_UYVY: return IPU_PIX_FMT_UYVY;
		case GST_VIDEO_FORMAT_v308: return IPU_PIX_FMT_YUV444;
		case GST_VIDEO_FORMAT_NV12: return IPU_PIX_FMT_NV12;
		case GST_VIDEO_FORMAT_YV12: return IPU_PIX_FMT_YVU420P;
		case GST_VIDEO_FORMAT_I420: return IPU_PIX_FMT_YUV420P;
		case GST_VIDEO_FORMAT_Y42B: return IPU_PIX_FMT_YUV422P;
		case GST_VIDEO_FORMAT_Y444: return IPU_PIX_FMT_YUV444P;
		default:
			GST_WARNING("Unknown format %d (%s)", (gint)format, gst_video_format_to_string(format));
			return 0;
	}
}


static GstVideoFormat gst_imx_ipu_blitter_get_format_from_fb(GstImxIpuBlitter *ipu_blitter, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix)
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

	/* TODO: Some cases are commented out, corresponding to the disabled pixel formats in
	 * gst_imx_ipu_blitter_get_v4l_format(). Re-enable them if and when there is a way to autodetect
	 * supported formats. */
	switch (fb_var->bits_per_pixel)
	{
#if 0
		case 15:
		{
			if ((rlen == 5) && (glen == 5) && (blen == 5))
				fmt = GST_VIDEO_FORMAT_RGB15;
			break;
		}
#endif
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


/* Determines the number of bytes per pixel used by the IPU for the given formats;
 * necessary for calculations in the GST_IMX_FILL_IPU_TASK macro */
static int gst_imx_ipu_video_bpp(GstVideoFormat fmt)
{
	switch (fmt)
	{
		case GST_VIDEO_FORMAT_RGBx: return 4;
		case GST_VIDEO_FORMAT_BGRx: return 4;
		case GST_VIDEO_FORMAT_xRGB: return 4;
		case GST_VIDEO_FORMAT_xBGR: return 4;
		case GST_VIDEO_FORMAT_RGBA: return 4;
		case GST_VIDEO_FORMAT_BGRA: return 4;
		case GST_VIDEO_FORMAT_ARGB: return 4;
		case GST_VIDEO_FORMAT_ABGR: return 4;
		case GST_VIDEO_FORMAT_RGB: return 3;
		case GST_VIDEO_FORMAT_BGR: return 3;
		case GST_VIDEO_FORMAT_RGB16: return 2;
		case GST_VIDEO_FORMAT_BGR16: return 2;
		case GST_VIDEO_FORMAT_RGB15: return 2;
		case GST_VIDEO_FORMAT_BGR15: return 2;
		case GST_VIDEO_FORMAT_ARGB64: return 8;
		case GST_VIDEO_FORMAT_UYVY: return 2;
		default: return 1;
	}
}


/* Since the steps for setting input and output buffers are the same,
 * their code is put in a macro, to be able to use one generic version.
 * In C++, templates would allow for cleaner generic programming, but
 * we are using C here.
 */
/* Of special note is the IPU task width & height values. The IPU does not
 * allow for setting stride and padding values directly. In fact, it assumes
 * a stride that equals the frame width, and zero padding.
 * However, the IPU task has the crop struct, making it possible to
 * read from & write to a rectangular region inside a frame.
 * A trick is used: width is set to the stride value, height is set to include
 * padding rows. crop width & height are set to the actual width and height
 * of the frame. (Or, if the video crop metadata is defined, it is set to the
 * metadata's coordinates.)
 * One limitation of this trick is that it assumes a specific video frame
 * layout. In particular, planes with nonstandard positions inside the buffer
 * are not supported. But this is circumvented by copying frames that do not
 * contain DMA buffers (frames that do are very unlikely to use such nonstandard
 * layouts).
 * Since the stride is given in bytes, not pixels, it needs to be divided by
 * whatever gst_imx_ipu_video_bpp() returns.
 */
#define GST_IMX_FILL_IPU_TASK(ipu_blitter, buffer, taskio) \
do { \
 \
	guint num_extra_lines; \
	GstVideoMeta *video_meta; \
	GstVideoCropMeta *video_crop_meta; \
	GstImxPhysMemMeta *phys_mem_meta; \
 \
	video_meta = gst_buffer_get_video_meta(buffer); \
	video_crop_meta = gst_buffer_get_video_crop_meta(buffer); \
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(buffer); \
 \
	g_assert((video_meta != NULL) && (phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0)); \
 \
	num_extra_lines = phys_mem_meta->padding / video_meta->stride[0]; \
	(taskio).width = video_meta->stride[0] / gst_imx_ipu_video_bpp(video_meta->format); \
	(taskio).height = video_meta->height + num_extra_lines; \
 \
	if (ipu_blitter->apply_crop_metadata && (video_crop_meta != NULL)) \
	{ \
		if ((video_crop_meta->x >= (guint)(video_meta->width)) || (video_crop_meta->y >= (guint)(video_meta->height))) \
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
	(taskio).format = gst_imx_ipu_blitter_get_v4l_format(video_meta->format); \
} while (0)


gboolean gst_imx_ipu_blitter_set_actual_input_buffer(GstImxIpuBlitter *ipu_blitter, GstBuffer *actual_input_buffer)
{
	g_assert(actual_input_buffer != NULL);

	GST_IMX_FILL_IPU_TASK(ipu_blitter, actual_input_buffer, ipu_blitter->priv->task.input);
	ipu_blitter->actual_input_buffer = actual_input_buffer;

	return TRUE;
}


gboolean gst_imx_ipu_blitter_set_output_buffer(GstImxIpuBlitter *ipu_blitter, GstBuffer *output_buffer)
{
	g_assert(output_buffer != NULL);

	GST_IMX_FILL_IPU_TASK(ipu_blitter, output_buffer, ipu_blitter->priv->task.output);

	return TRUE;
}


gboolean gst_imx_ipu_blitter_set_input_buffer(GstImxIpuBlitter *ipu_blitter, GstBuffer *input_buffer)
{
	GstImxPhysMemMeta *phys_mem_meta;

	g_assert(input_buffer != NULL);

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(input_buffer);

	/* Test if the input buffer uses DMA memory */
	if ((phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0))
	{
		/* DMA memory present - the input buffer can be used as an actual input buffer */
		gst_imx_ipu_blitter_set_actual_input_buffer(ipu_blitter, gst_buffer_ref(input_buffer));

		GST_TRACE_OBJECT(ipu_blitter, "input buffer uses DMA memory - setting it as actual input buffer");
	}
	else
	{
		/* No DMA memory present; the input buffer needs to be copied to an internal
		 * temporary input buffer */

		GstBuffer *temp_input_buffer;
		GstFlowReturn flow_ret;

		GST_TRACE_OBJECT(ipu_blitter, "input buffer does not use DMA memory - need to copy it to an internal input DMA buffer");

		{
			/* The internal input buffer is the temp input frame's DMA memory.
			 * If it does not exist yet, it needs to be created here. The temp input
			 * frame is then mapped. */

			if (ipu_blitter->internal_bufferpool == NULL)
			{
				/* Internal bufferpool does not exist yet - create it now,
				 * so that it can in turn create the internal input buffer */

				GstCaps *caps = gst_video_info_to_caps(&(ipu_blitter->input_video_info));

				ipu_blitter->internal_bufferpool = gst_imx_ipu_blitter_create_bufferpool(
					ipu_blitter,
					caps,
					ipu_blitter->input_video_info.size,
					2, 0,
					NULL,
					NULL
				);

				gst_caps_unref(caps);

				if (ipu_blitter->internal_bufferpool == NULL)
				{
					GST_ERROR_OBJECT(ipu_blitter, "failed to create internal bufferpool");
					return FALSE;
				}
			}

			/* Future versions of this code may propose the internal bufferpool upstream;
			 * hence the is_active check */
			if (!gst_buffer_pool_is_active(ipu_blitter->internal_bufferpool))
				gst_buffer_pool_set_active(ipu_blitter->internal_bufferpool, TRUE);
		}

		/* Create the internal input buffer */
		flow_ret = gst_buffer_pool_acquire_buffer(ipu_blitter->internal_bufferpool, &temp_input_buffer, NULL);
		if (flow_ret != GST_FLOW_OK)
		{
			GST_ERROR_OBJECT(ipu_blitter, "error acquiring input frame buffer: %s", gst_pad_mode_get_name(flow_ret));
			return FALSE;
		}

		{
			GstVideoFrame input_frame, temp_input_frame;

			gst_video_frame_map(&input_frame, &(ipu_blitter->input_video_info), input_buffer, GST_MAP_READ);
			gst_video_frame_map(&temp_input_frame, &(ipu_blitter->input_video_info), temp_input_buffer, GST_MAP_WRITE);

			/* Copy the input buffer's pixels to the temp input frame
			 * The gst_video_frame_copy() makes sure stride and plane offset values from both
			 * frames are respected */
			gst_video_frame_copy(&temp_input_frame, &input_frame);

			gst_video_frame_unmap(&temp_input_frame);
			gst_video_frame_unmap(&input_frame);
		}

		/* Finally, set the temp input buffer as the actual input buffer */
		gst_imx_ipu_blitter_set_actual_input_buffer(ipu_blitter, temp_input_buffer);
	}

	/* Configure interlacing */
	ipu_blitter->priv->task.input.deinterlace.enable = 0;
	if (ipu_blitter->deinterlace_mode != GST_IMX_IPU_BLITTER_DEINTERLACE_NONE)
	{
		switch (ipu_blitter->input_video_info.interlace_mode)
		{
			case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
				GST_TRACE_OBJECT(ipu_blitter, "input stream uses interlacing -> deinterlacing enabled");
				ipu_blitter->priv->task.input.deinterlace.enable = 1;
				break;
			case GST_VIDEO_INTERLACE_MODE_MIXED:
			{
				GstVideoMeta *video_meta;

				GST_TRACE_OBJECT(ipu_blitter, "input stream uses mixed interlacing -> need to check video metadata deinterlacing flag");

				video_meta = gst_buffer_get_video_meta(input_buffer);
				if (video_meta != NULL)
				{
					if (video_meta->flags & GST_VIDEO_FRAME_FLAG_INTERLACED)
					{
						GST_TRACE_OBJECT(ipu_blitter, "frame has video metadata and deinterlacing flag");
						ipu_blitter->priv->task.input.deinterlace.enable = 1;
					}
					else
						GST_TRACE_OBJECT(ipu_blitter, "frame has video metadata but no deinterlacing flag");
				}
				else
					GST_TRACE_OBJECT(ipu_blitter, "frame has no video metadata -> no deinterlacing done");

				break;
			}
			case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
			{
				GST_TRACE_OBJECT(ipu_blitter, "input stream is progressive -> no deinterlacing necessary");
				break;
			}
			case GST_VIDEO_INTERLACE_MODE_FIELDS:
				GST_FIXME_OBJECT(ipu_blitter, "2-fields deinterlacing not supported (yet)");
				break;
			default:
				break;
		}
	}

	return TRUE;
}


void gst_imx_ipu_blitter_set_input_info(GstImxIpuBlitter *ipu_blitter, GstVideoInfo *info)
{
	ipu_blitter->input_video_info = *info;

	/* New videoinfo means new frame sizes, new strides etc.
	 * making existing internal bufferpools and temp video frames unusable
	 * -> shut them down; they will be recreated on-demand in the
	 * gst_imx_ipu_blitter_set_incoming_frame() call */
	if (ipu_blitter->internal_bufferpool != NULL)
	{
		gst_object_unref(ipu_blitter->internal_bufferpool);
		ipu_blitter->internal_bufferpool = NULL;
	}
}


gboolean gst_imx_ipu_blitter_blit(GstImxIpuBlitter *ipu_blitter)
{
	int ret;

	g_assert(ipu_blitter->actual_input_buffer != NULL);

	/* Motion must be set to MED_MOTION if no interlacing shall be used
	 * mixed mode bitstreams contain interlaced and non-interlaced pictures,
	 * so set the motion value here */
	if (ipu_blitter->priv->task.input.deinterlace.enable)
	{
		switch (ipu_blitter->deinterlace_mode)
		{
			case GST_IMX_IPU_BLITTER_DEINTERLACE_NONE:
				ipu_blitter->priv->task.input.deinterlace.motion = MED_MOTION;
				break;
			case GST_IMX_IPU_BLITTER_DEINTERLACE_FAST_MOTION:
				ipu_blitter->priv->task.input.deinterlace.motion = HIGH_MOTION;
				break;
		}
	}
	else
		ipu_blitter->priv->task.input.deinterlace.motion = MED_MOTION;

	GST_LOG_OBJECT(
		ipu_blitter,
		"task input:  width:  %u  height: %u  format: 0x%x  crop: %u,%u %ux%u  paddr 0x%x  deinterlace enable %u motion 0x%x",
		ipu_blitter->priv->task.input.width, ipu_blitter->priv->task.input.height,
		ipu_blitter->priv->task.input.format,
		ipu_blitter->priv->task.input.crop.pos.x, ipu_blitter->priv->task.input.crop.pos.y, ipu_blitter->priv->task.input.crop.w, ipu_blitter->priv->task.input.crop.h,
		ipu_blitter->priv->task.input.paddr,
		ipu_blitter->priv->task.input.deinterlace.enable, ipu_blitter->priv->task.input.deinterlace.motion
	);
	GST_LOG_OBJECT(
		ipu_blitter,
		"task output:  width:  %u  height: %u  format: 0x%x  crop: %u,%u %ux%u  paddr 0x%x  rotate: %u",
		ipu_blitter->priv->task.output.width, ipu_blitter->priv->task.output.height,
		ipu_blitter->priv->task.output.format,
		ipu_blitter->priv->task.output.crop.pos.x, ipu_blitter->priv->task.output.crop.pos.y, ipu_blitter->priv->task.output.crop.w, ipu_blitter->priv->task.output.crop.h,
		ipu_blitter->priv->task.output.paddr,
		ipu_blitter->priv->task.output.rotate
	);

	/* The actual blit operation
	 * Input and output frame are assumed to be set up properly at this point
	 */
	ret = ioctl(ipu_blitter->priv->ipu_fd, IPU_QUEUE_TASK, &(ipu_blitter->priv->task));

	gst_buffer_unref(ipu_blitter->actual_input_buffer);
	ipu_blitter->actual_input_buffer = NULL;

	if (ret == -1)
	{
		GST_ERROR_OBJECT(ipu_blitter, "queuing IPU task failed: %s", strerror(errno));
		return FALSE;
	}
	else
		return TRUE;
}


GstBufferPool* gst_imx_ipu_blitter_create_bufferpool(GstImxIpuBlitter *ipu_blitter, GstCaps *caps, guint size, guint min_buffers, guint max_buffers, GstAllocator *allocator, GstAllocationParams *alloc_params)
{
	GstBufferPool *pool;
	GstStructure *config;
	
	pool = gst_imx_phys_mem_buffer_pool_new(FALSE);

	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, caps, size, min_buffers, max_buffers);
	/* If the allocator value is NULL, create an allocator */
	if (allocator == NULL)
	{
		allocator = gst_imx_ipu_allocator_new(ipu_blitter->priv->ipu_fd);
		gst_buffer_pool_config_set_allocator(config, allocator, alloc_params);
	}
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(pool, config);

	return pool;
}


GstBufferPool* gst_imx_ipu_blitter_get_internal_bufferpool(GstImxIpuBlitter *ipu_blitter)
{
	return ipu_blitter->internal_bufferpool;
}


/* The produced buffer contains only metadata, no memory blocks - the IPU sink does not need anything more
 * TODO: add some logic to wrap the framebuffer memory block, including map/unmap code etc. */
GstBuffer* gst_imx_ipu_blitter_wrap_framebuffer(GstImxIpuBlitter *ipu_blitter, int framebuffer_fd, guint x, guint y, guint width, guint height)
{
	guint fb_width, fb_height;
	GstVideoFormat fb_format;
	GstBuffer *buffer;
	GstImxPhysMemMeta *phys_mem_meta;
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
	fb_format = gst_imx_ipu_blitter_get_format_from_fb(ipu_blitter, &fb_var, &fb_fix);

	GST_DEBUG_OBJECT(ipu_blitter, "framebuffer resolution is %u x %u", fb_width, fb_height);

	buffer = gst_buffer_new();
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

	phys_mem_meta = GST_IMX_PHYS_MEM_META_ADD(buffer);
	phys_mem_meta->phys_addr = (guintptr)(fb_fix.smem_start);

	return buffer;
}

