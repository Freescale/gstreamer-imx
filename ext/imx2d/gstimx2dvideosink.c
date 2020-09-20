/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2020  Carlos Rafael Giani
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

#include <gst/gst.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimx2dvideosink.h"
#include "gstimx2dmisc.h"


GST_DEBUG_CATEGORY_STATIC(imx_2d_video_sink_debug);
#define GST_CAT_DEFAULT imx_2d_video_sink_debug


enum
{
	PROP_0,
	PROP_INPUT_CROP,
	PROP_OUTPUT_ROTATION,
	PROP_FORCE_ASPECT_RATIO
};


#define DEFAULT_INPUT_CROP TRUE
#define DEFAULT_OUTPUT_ROTATION IMX_2D_ROTATION_NONE
#define DEFAULT_FORCE_ASPECT_RATIO TRUE


G_DEFINE_ABSTRACT_TYPE(GstImx2dVideoSink, gst_imx_2d_video_sink, GST_TYPE_VIDEO_SINK)


/* Base class function overloads. */

/* General element operations. */
static void gst_imx_2d_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_2d_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_imx_2d_video_sink_change_state(GstElement *element, GstStateChange transition);

/* Caps handling. */
static gboolean gst_imx_2d_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps);

/* Frame output. */
static GstFlowReturn gst_imx_blitter_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *input_buffer);

/* GstImx2dVideoSink specific functions. */
static gboolean gst_imx_2d_video_sink_start(GstImx2dVideoSink *self);
static void gst_imx_2d_video_sink_stop(GstImx2dVideoSink *self);
static gboolean gst_imx_2d_video_sink_create_blitter(GstImx2dVideoSink *self);




static void gst_imx_2d_video_sink_class_init(GstImx2dVideoSinkClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstBaseSinkClass *base_sink_class;
	GstVideoSinkClass *video_sink_class;

	gst_imx_2d_setup_logging();

	GST_DEBUG_CATEGORY_INIT(imx_2d_video_sink_debug, "imx2dvideosink", 0, "NXP i.MX 2D video sink base class");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	base_sink_class = GST_BASE_SINK_CLASS(klass);
	video_sink_class = GST_VIDEO_SINK_CLASS(klass);

	object_class->set_property   = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_set_property);
	object_class->get_property   = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_get_property);

	element_class->change_state  = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_change_state);

	base_sink_class->set_caps    = GST_DEBUG_FUNCPTR(gst_imx_2d_video_sink_set_caps);

	video_sink_class->show_frame = GST_DEBUG_FUNCPTR(gst_imx_blitter_video_sink_show_frame);

	g_object_class_install_property(
		object_class,
		PROP_INPUT_CROP,
		g_param_spec_boolean(
			"input-crop",
			"Input crop",
			"Whether or not to crop input frames based on their video crop metadata",
			DEFAULT_INPUT_CROP,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_OUTPUT_ROTATION,
		g_param_spec_enum(
			"output-rotation",
			"Output rotation",
			"Output rotation in 90-degree steps",
			gst_imx_2d_rotation_get_type(),
			DEFAULT_OUTPUT_ROTATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_FORCE_ASPECT_RATIO,
		g_param_spec_boolean(
			"force-aspect-ratio",
			"Force aspect ratio",
			"When enabled, scaling will respect original aspect ratio",
			DEFAULT_FORCE_ASPECT_RATIO,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_2d_video_sink_init(GstImx2dVideoSink *self)
{
	self->uploader = NULL;
	self->imx_dma_buffer_allocator = NULL;

	self->blitter = NULL;

	gst_video_info_init(&(self->input_video_info));
	self->input_surface = NULL;

	self->framebuffer = NULL;

	self->input_crop = DEFAULT_INPUT_CROP;
	self->output_rotation = DEFAULT_OUTPUT_ROTATION;
	self->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
}


static void gst_imx_2d_video_sink_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_INPUT_CROP:
		{
			GST_OBJECT_LOCK(self);
			self->input_crop = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_OUTPUT_ROTATION:
		{
			GST_OBJECT_LOCK(self);
			self->output_rotation = g_value_get_enum(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_FORCE_ASPECT_RATIO:
		{
			GST_OBJECT_LOCK(self);
			self->force_aspect_ratio = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_2d_video_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(object);

	switch (prop_id)
	{
		case PROP_INPUT_CROP:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->input_crop);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_OUTPUT_ROTATION:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_enum(value, self->output_rotation);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		case PROP_FORCE_ASPECT_RATIO:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->force_aspect_ratio);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_2d_video_sink_change_state(GstElement *element, GstStateChange transition)
{
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(element);
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

	g_assert(self != NULL);

	switch (transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if (!gst_imx_2d_video_sink_start(self))
				return GST_STATE_CHANGE_FAILURE;
			break;
		}

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(gst_imx_2d_video_sink_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition)
	{
		case GST_STATE_CHANGE_READY_TO_NULL:
			gst_imx_2d_video_sink_stop(self);
			break;

		default:
			break;
	}

	return ret;
}


static gboolean gst_imx_2d_video_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	GstVideoInfo input_video_info;
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK(sink);

	g_assert(self->blitter != NULL);

	/* Convert the caps to video info structures for easier acess. */

	GST_DEBUG_OBJECT(self, "setting caps %" GST_PTR_FORMAT, (gpointer)caps);

	if (G_UNLIKELY(!gst_video_info_from_caps(&input_video_info, caps)))
	{
		GST_ERROR_OBJECT(self, "could not set caps %" GST_PTR_FORMAT, (gpointer)caps);
		goto error;
	}

	memcpy(&(self->input_video_info), &input_video_info, sizeof(GstVideoInfo));

	/* Fill the input surface description with values that can't change
	 * in between buffers. (Plane stride and offset values can change.
	 * This is unlikely to happen, but it is not impossible.) */
	self->input_surface_desc.width = GST_VIDEO_INFO_WIDTH(&input_video_info);
	self->input_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&input_video_info);
	self->input_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&input_video_info));

	return TRUE;

error:
	return FALSE;
}


static GstFlowReturn gst_imx_blitter_video_sink_show_frame(GstVideoSink *video_sink, GstBuffer *input_buffer)
{
	Imx2dBlitParams blit_params;
	Imx2dBlitMargin blit_margin;
	GstFlowReturn flow_ret;
	ImxDmaBuffer *in_dma_buffer;
	gboolean input_crop;
	Imx2dRegion crop_rectangle;
	Imx2dRegion dest_region;
	Imx2dRotation output_rotation;
	gboolean force_aspect_ratio;
	GstVideoMeta *videometa;
	guint plane_index;
	GstBuffer *uploading_result;
	GstImx2dVideoSink *self = GST_IMX_2D_VIDEO_SINK_CAST(video_sink);
	GstVideoInfo *input_video_info = &(self->input_video_info);
	guint video_width, video_height;

	g_assert(self->blitter != NULL);

	/* Begin by uploading the buffer. This may actually be a form
	 * of "passthrough". If the input buffer already uses an ImxDmaBuffer, then
	 * there is no point in doing a proper upload (which typically would imply
	 * a CPU based frame copy). Instead, we can then just use the input buffer
	 * as-is. The uploader can configure itself automatically based on the input
	 * caps, and has a method for this case, where the input buffer is ref'd, but
	 * otherwise just passed through. In other cases, such as when an upstream
	 * element outputs buffers that have their memory in sysmem, the uploader
	 * will have chosen a method that does copy the buffer contents. */
	flow_ret = gst_imx_dma_buffer_uploader_perform(self->uploader, input_buffer, &uploading_result);
	if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
		return flow_ret;

	/* From this moment on, use the uploaded version as our "input buffer".
	 * As explained above, depending on the caps, this may really be still
	 * our original input buffer. */
	input_buffer = uploading_result;

	/* The rest of the code in this function expects buffers with ImxDmaBuffer inside. */
	g_assert(gst_imx_has_imx_dma_buffer_memory(input_buffer));
	in_dma_buffer = gst_imx_get_dma_buffer_from_buffer(input_buffer);
	g_assert(in_dma_buffer != NULL);

	/* Create local copies of the property values so that we can use them
	 * without risking race conditions if another thread is setting new
	 * values while this function is running. */
	GST_OBJECT_LOCK(self);
	input_crop = self->input_crop;
	output_rotation = self->output_rotation;
	force_aspect_ratio = self->force_aspect_ratio;
	GST_OBJECT_UNLOCK(self);

	GST_LOG_OBJECT(self, "filling input surface description with input buffer plane stride and -info");

	/* Fill plane offset and stride values into input_surface_desc. As explained
	 * in gst_imx_2d_video_transform_set_caps(), these values _can_ in theory
	 * change between incoming buffers. Prefer getting them from a videometa,
	 * because those can carry values with them that deviate from what could
	 * be calculated out of the caps. For example, if width = 100 and bytes
	 * per pixel = 3, then one could calculate a stride value of 100*3 = 300 byte.
	 * But the underlying hardware may require alignment to 16-byte increments,
	 * and the actual stride value is then 304 bytes - impossible to determine
	 * with the caps alone. The videometa would then contain this stride value
	 * of 304 bytes. Consequently, it is better to look at the videometa and
	 * use its values instead of relying on computed ones. */
	videometa = gst_buffer_get_video_meta(input_buffer);
	if (videometa != NULL)
	{
		for (plane_index = 0; plane_index < videometa->n_planes; ++plane_index)
		{
			self->input_surface_desc.plane_stride[plane_index] = videometa->stride[plane_index];
			self->input_surface_desc.plane_offset[plane_index] = videometa->offset[plane_index];

			GST_LOG_OBJECT(
				self,
				"input plane #%u info from videometa:  stride: %d  offset: %d",
				plane_index,
				self->input_surface_desc.plane_stride[plane_index],
				self->input_surface_desc.plane_offset[plane_index]
			);
		}
	}
	else
	{
		for (plane_index = 0; plane_index < GST_VIDEO_INFO_N_PLANES(input_video_info); ++plane_index)
		{
			self->input_surface_desc.plane_stride[plane_index] = GST_VIDEO_INFO_PLANE_STRIDE(input_video_info, plane_index);
			self->input_surface_desc.plane_offset[plane_index] = GST_VIDEO_INFO_PLANE_OFFSET(input_video_info, plane_index);

			GST_LOG_OBJECT(
				self,
				"input plane #%u info from videoinfo:  stride: %d  offset: %d",
				plane_index,
				self->input_surface_desc.plane_stride[plane_index],
				self->input_surface_desc.plane_offset[plane_index]
			);
		}
	}

	imx_2d_surface_set_desc(self->input_surface, &(self->input_surface_desc));

	GST_LOG_OBJECT(self, "setting ImxDmaBuffer %p as input DMA buffer", (gpointer)(in_dma_buffer));

	imx_2d_surface_set_dma_buffer(self->input_surface, in_dma_buffer);

	memset(&blit_params, 0, sizeof(blit_params));
	blit_params.source_region = NULL;
	blit_params.dest_region = NULL;
	blit_params.rotation = output_rotation;
	blit_params.alpha = 255;

	video_width = GST_VIDEO_INFO_WIDTH(input_video_info);
	video_height = GST_VIDEO_INFO_HEIGHT(input_video_info);

	if (input_crop)
	{
		GstVideoCropMeta *crop_meta = gst_buffer_get_video_crop_meta(input_buffer);

		if (crop_meta != NULL)
		{
			crop_rectangle.x1 = crop_meta->x;
			crop_rectangle.y1 = crop_meta->y;
			crop_rectangle.x2 = crop_meta->x + crop_meta->width;
			crop_rectangle.y2 = crop_meta->y + crop_meta->height;

			blit_params.source_region = &crop_rectangle;

			GST_LOG_OBJECT(
				self,
				"using crop rectangle (%d, %d) - (%d, %d)",
				crop_rectangle.x1, crop_rectangle.y1,
				crop_rectangle.x2, crop_rectangle.y2
			);

			video_width = crop_meta->width;
			video_height = crop_meta->height;
		}
	}

	if (force_aspect_ratio)
	{
		gboolean transposed = (output_rotation == IMX_2D_ROTATION_90)
		                   || (output_rotation == IMX_2D_ROTATION_270);

		gst_imx_2d_canvas_calculate_letterbox_margin(
			&blit_margin,
			&dest_region,
			imx_2d_surface_get_region(self->framebuffer_surface),
			transposed,
			video_width,
			video_height,
			GST_VIDEO_INFO_PAR_N(input_video_info),
			GST_VIDEO_INFO_PAR_D(input_video_info)
		);

		//GST_LOG_OBJECT(self, "computed margin %d/%d/%d/%d", blit_margin.left_margin, blit_margin.top_margin, blit_margin.right_margin, blit_margin.bottom_margin);

		blit_margin.color = 0xFF000000;
		blit_params.margin = &blit_margin;
		blit_params.dest_region = &dest_region;
	}

	GST_LOG_OBJECT(self, "beginning blitting procedure to transform the frame");

	if (!imx_2d_blitter_start(self->blitter))
	{
		GST_ERROR_OBJECT(self, "starting blitter failed");
		goto error;
	}

	if (!imx_2d_blitter_do_blit(self->blitter, self->input_surface, self->framebuffer_surface, &blit_params))
	{
		GST_ERROR_OBJECT(self, "blitting failed");
		goto error;
	}

	if (!imx_2d_blitter_finish(self->blitter))
	{
		GST_ERROR_OBJECT(self, "finishing blitter failed");
		goto error;
	}

	GST_LOG_OBJECT(self, "blitting procedure finished successfully; frame transform complete");

finish:
	gst_buffer_unref(input_buffer);
	return flow_ret;

error:
	if (flow_ret != GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;
}


static gboolean gst_imx_2d_video_sink_start(GstImx2dVideoSink *self)
{
	GstImx2dVideoSinkClass *klass = GST_IMX_2D_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(self));

	self->imx_dma_buffer_allocator = gst_imx_allocator_new();
	self->uploader = gst_imx_dma_buffer_uploader_new(self->imx_dma_buffer_allocator);

	/* We call start _after_ the allocator & uploader were
	 * set up in case these might be needed. Currently,
	 * this is not the case, but it may be in the future. */
	if ((klass->start != NULL) && !(klass->start(self)))
	{
		GST_ERROR_OBJECT(self, "start() failed");
		goto error;
	}

	if (!gst_imx_2d_video_sink_create_blitter(self))
	{
		GST_ERROR_OBJECT(self, "creating blitter failed");
		goto error;
	}

	self->input_surface = imx_2d_surface_create(NULL, NULL);
	if (self->input_surface == NULL)
	{
		GST_ERROR_OBJECT(self, "creating input surface failed");
		goto error;
	}

	self->framebuffer = imx_2d_linux_framebuffer_create("/dev/fb0", 0);
	if (self->framebuffer == NULL)
	{
		GST_ERROR_OBJECT(self, "creating output framebuffer failed");
		goto error;
	}

	self->framebuffer_surface = imx_2d_linux_framebuffer_get_surface(self->framebuffer);
	g_assert(self->framebuffer_surface != NULL);

	return TRUE;

error:
	gst_imx_2d_video_sink_stop(self);
	return FALSE;
}


static void gst_imx_2d_video_sink_stop(GstImx2dVideoSink *self)
{
	GstImx2dVideoSinkClass *klass = GST_IMX_2D_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(self));

	if ((klass->stop != NULL) && !(klass->stop(self)))
		GST_ERROR_OBJECT(self, "stop() failed");

	if (self->input_surface != NULL)
	{
		imx_2d_surface_destroy(self->input_surface);
		self->input_surface = NULL;
	}

	if (self->framebuffer != NULL)
	{
		imx_2d_linux_framebuffer_destroy(self->framebuffer);
		self->framebuffer = NULL;
	}

	if (self->blitter != NULL)
	{
		imx_2d_blitter_destroy(self->blitter);
		self->blitter = NULL;
	}

	if (self->uploader != NULL)
	{
		gst_object_unref(GST_OBJECT(self->uploader));
		self->uploader = NULL;
	}

	if (self->imx_dma_buffer_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(self->imx_dma_buffer_allocator));
		self->imx_dma_buffer_allocator = NULL;
	}
}


static gboolean gst_imx_2d_video_sink_create_blitter(GstImx2dVideoSink *self)
{
	GstImx2dVideoSinkClass *klass = GST_IMX_2D_VIDEO_SINK_CLASS(G_OBJECT_GET_CLASS(self));

	g_assert(klass->create_blitter != NULL);
	g_assert(self->blitter == NULL);

	if (G_UNLIKELY((self->blitter = klass->create_blitter(self)) == NULL))
	{
		GST_ERROR_OBJECT(self, "could not create blitter");
		return FALSE;
	}

	GST_DEBUG_OBJECT(self, "created new blitter %" GST_PTR_FORMAT, (gpointer)(self->blitter));

	return TRUE;
}


void gst_imx_2d_video_sink_common_class_init(GstImx2dVideoSinkClass *klass, Imx2dHardwareCapabilities const *capabilities)
{
	GstElementClass *element_class;
	GstCaps *sink_template_caps;
	GstPadTemplate *sink_template;

	element_class = GST_ELEMENT_CLASS(klass);

	sink_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities(capabilities, GST_PAD_SINK);
	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	gst_element_class_add_pad_template(element_class, sink_template);
}
