/* GStreamer video sink using the Freescale IPU
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


#include "ipu_sink.h"

#include <config.h>

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/mxcfb.h>
#include <linux/ipu.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "../common/vpu_utils.h"
#include "../common/vpu_bufferpool.h"




GST_DEBUG_CATEGORY_STATIC(ipusink_debug);
#define GST_CAT_DEFAULT ipusink_debug




static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) I420, "
		"width = (int) [ 16, 2048 ], "
		"height = (int) [ 16, 2048 ], "
		"framerate = (fraction) [ 0, MAX ]"
	)
);


struct _GstFslIpuSinkPrivate
{
	int ipu_fd, framebuffer_fd;
	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;
	struct ipu_task task;
};


G_DEFINE_TYPE(GstFslIpuSink, gst_fsl_ipu_sink, GST_TYPE_VIDEO_SINK)


static GstFlowReturn gst_fsl_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf);
static void gst_fsl_ipu_sink_finalize(GObject *object);




/* required function declared by G_DEFINE_TYPE */

void gst_fsl_ipu_sink_class_init(GstFslIpuSinkClass *klass)
{
	GObjectClass *object_class;
	GstVideoSinkClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(ipusink_debug, "ipusink", 0, "Freescale IPU video sink");

	object_class = G_OBJECT_CLASS(klass);
	parent_class = GST_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video sink",
		"Sink/Video",
		"Video output using the Freescale IPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));

	parent_class->show_frame = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_show_frame);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_finalize);
}


void gst_fsl_ipu_sink_init(GstFslIpuSink *ipu_sink)
{
	ipu_sink->priv = g_slice_alloc(sizeof(GstFslIpuSinkPrivate));

	ipu_sink->priv->framebuffer_fd = -1;
	ipu_sink->priv->ipu_fd = -1;

	ipu_sink->priv->ipu_fd = open("/dev/mxc_ipu", O_RDWR, 0);
	if (ipu_sink->priv->ipu_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/mxc_ipu: %s", strerror(errno)), (NULL));
		return;
	}

	ipu_sink->priv->framebuffer_fd = open("/dev/fb0", O_RDWR, 0);
	if (ipu_sink->priv->ipu_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/fb0: %s", strerror(errno)), (NULL));
		return;
	}

	if (ioctl(ipu_sink->priv->framebuffer_fd, FBIOBLANK, FB_BLANK_UNBLANK) == -1)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open unblank framebuffer: %s", strerror(errno)), (NULL));
		return;
	}

	if (ioctl(ipu_sink->priv->framebuffer_fd, FBIOGET_FSCREENINFO, &(ipu_sink->priv->fb_fix)) == -1)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open get fixed screen info: %s", strerror(errno)), (NULL));
		return;
	}

	if (ioctl(ipu_sink->priv->framebuffer_fd, FBIOGET_VSCREENINFO, &(ipu_sink->priv->fb_var)) == -1)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open get variable screen info: %s", strerror(errno)), (NULL));
		return;
	}

	memset(&(ipu_sink->priv->task), 0, sizeof(struct ipu_task));

	ipu_sink->priv->task.input.format = IPU_PIX_FMT_YUV420P;

	ipu_sink->priv->task.output.format = v4l2_fourcc('R', 'G', 'B', 'P');
	ipu_sink->priv->task.output.paddr = (dma_addr_t)(ipu_sink->priv->fb_fix.smem_start);
	ipu_sink->priv->task.output.width = ipu_sink->priv->fb_var.xres;
	ipu_sink->priv->task.output.height = ipu_sink->priv->fb_var.yres;
	ipu_sink->priv->task.output.rotate = 0;
#if 0
	ipu_sink->priv->task.output.crop.pos.x = 10;
	ipu_sink->priv->task.output.crop.pos.y = 10;
	ipu_sink->priv->task.output.crop.w = 400;
	ipu_sink->priv->task.output.crop.h = 300;
#endif

	GST_INFO_OBJECT(ipu_sink, "initialized IPU sink with output screen resolution %d x %d and start phys address %p", ipu_sink->priv->task.output.width, ipu_sink->priv->task.output.height, ipu_sink->priv->task.output.paddr);

	ipu_sink->parent.width = ipu_sink->priv->task.output.width;
	ipu_sink->parent.height = ipu_sink->priv->task.output.height;
}


static GstFlowReturn gst_fsl_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf)
{
	GstFslIpuSink *ipu_sink;
	GstVideoMeta *video_meta;
	GstFslPhysMemMeta *phys_mem_meta;
	unsigned int num_extra_rows;

	ipu_sink = GST_FSL_IPU_SINK(video_sink);
	video_meta = gst_buffer_get_video_meta(buf);
	phys_mem_meta = GST_FSL_PHYS_MEM_META_GET(buf);

	num_extra_rows = phys_mem_meta->padding / video_meta->stride[0];

	ipu_sink->priv->task.input.width = video_meta->stride[0];
	ipu_sink->priv->task.input.height = video_meta->height + num_extra_rows;
	ipu_sink->priv->task.input.crop.w = video_meta->width;
	ipu_sink->priv->task.input.crop.h = video_meta->height;

	ipu_sink->priv->task.input.paddr = (dma_addr_t)(phys_mem_meta->phys_addr);

	GST_DEBUG_OBJECT(
		ipu_sink,
		"input size: %d x %d  (actually: %d x %d  X padding: %d  Y padding: %d)  phys addr: %p",
		video_meta->width, video_meta->height,
		ipu_sink->priv->task.input.width, ipu_sink->priv->task.input.height,
		video_meta->stride[0] - video_meta->width, num_extra_rows,
		ipu_sink->priv->task.input.paddr
	);

	if (ioctl(ipu_sink->priv->ipu_fd, IPU_QUEUE_TASK, &(ipu_sink->priv->task)) == -1)
	{
		GST_ERROR_OBJECT(ipu_sink, "queuing IPU task failed: %s", strerror(errno));
		return GST_FLOW_ERROR;
	}

	return GST_FLOW_OK;
}


static void gst_fsl_ipu_sink_finalize(GObject *object)
{
	GstFslIpuSink *ipu_sink = GST_FSL_IPU_SINK(object);

	if (ipu_sink->priv != NULL)
	{
		if (ipu_sink->priv->framebuffer_fd >= 0)
			close(ipu_sink->priv->framebuffer_fd);
		if (ipu_sink->priv->ipu_fd >= 0)
			close(ipu_sink->priv->ipu_fd);
		g_slice_free1(sizeof(GstFslIpuSinkPrivate), ipu_sink->priv);
	}

	G_OBJECT_CLASS(gst_fsl_ipu_sink_parent_class)->finalize(object);
}





static gboolean plugin_init(GstPlugin *plugin)
{
	return gst_element_register(plugin, "fslipusink", GST_RANK_PRIMARY + 1, gst_fsl_ipu_sink_get_type());
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	fslipusink,
	"Video output using the Freescale IPU",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)

