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
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimx2dcompositor.h"
#include "gstimx2dmisc.h"


GST_DEBUG_CATEGORY_STATIC(imx_2d_compositor_debug);
#define GST_CAT_DEFAULT imx_2d_compositor_debug




/********** GstImx2dCompositorPad **********/


#define GST_TYPE_IMX_2D_COMPOSITOR_PAD             (gst_imx_2d_compositor_pad_get_type())
#define GST_IMX_2D_COMPOSITOR_PAD(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_2D_COMPOSITOR_PAD, GstImx2dCompositorPad))
#define GST_IMX_2D_COMPOSITOR_PAD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_2D_COMPOSITOR_PAD, GstImx2dCompositorPadClass))
#define GST_IMX_2D_COMPOSITOR_PAD_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_2D_COMPOSITOR_PAD, GstImx2dCompositorPadClass))
#define GST_IMX_2D_COMPOSITOR_PAD_CAST(obj)        ((GstImx2dCompositorPad *)(obj))
#define GST_IS_IMX_2D_COMPOSITOR_PAD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_2D_COMPOSITOR_PAD))
#define GST_IS_IMX_2D_COMPOSITOR_PAD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_2D_COMPOSITOR_PAD))


typedef struct _GstImx2dCompositorPad GstImx2dCompositorPad;
typedef struct _GstImx2dCompositorPadClass GstImx2dCompositorPadClass;


GType gst_imx_2d_compositor_pad_get_type(void);


struct _GstImx2dCompositorPad
{
	GstVideoAggregatorPad parent;

	/* imx2d input surface used for blending. This is
	 * created once per pad, and has its description
	 * updated as needed & DMA buffers assigned for
	 * each input buffer. */
	Imx2dSurface *input_surface;
	/* Prepared input surface description. "Prepared"
	 * means here that some of its fields are filled
	 * with values that do not change between buffers,
	 * such as width and height. Other values like
	 * plane strides / offsets _can_ change in between
	 * buffers, so those are filled in later. */
	Imx2dSurfaceDesc input_surface_desc;

	/* Terminology:
	 *
	 * inner_region = The region covered by the actual
	 * frame, without any margin.
	 *
	 * outer_region = inner_region plus the margin that
	 * is calculated to draw the letterbox. If the
	 * aspect ratio is not kept (in other words,
	 * force_aspect_ratio is FALSE then), then the
	 * outer_region equals the inner_region.
	 *
	 * total_region = outer_region plus extra margin
	 * specified by the GObject margin properties.
	 *
	 * The inner_region is always centered inside
	 * outer_region, but outer_region may not
	 * necessarily centered in total_region.
	 *
	 * The xpos, ypos, width, height properties define
	 * the total_region. extra_margin defines the margin
	 * that is added around outer_region.
	 */
	Imx2dRegion total_region;
	Imx2dRegion outer_region;
	Imx2dRegion inner_region;

	/* If TRUE, then the inner region's coordinates
	 * encompass the entire output frame. This is used
	 * for determining if the output frame needs to be
	 * cleared with a background color before compositing.
	 * This clearing is unnecessary if a fully opaque
	 * input frame covers the entire output frame. */
	gboolean inner_region_fills_output_frame;
	/* Same as inner_region_fills_output_frame, except
	 * for total_region instead of inner_region. Also,
	 * if inner_region_fills_output_frame is FALSE but
	 * this is TRUE, then additional checks are made,
	 * since the margin may not be opaque. */
	gboolean total_region_fills_output_frame;

	gboolean region_coords_need_update;

	/* letterbox_margin: Margin calculated for producing
	 * a letterbox around the inner_region. inner_region
	 * plus letterbox_margin result in the outer_region.
	 *
	 * extra_margin: Margin defined by the user via the
	 * GObject margin properties. outer_region plus
	 * extra_margin result in total_region.
	 *
	 * combined_margin: letterbox_margin plus extra_margin.
	 * inner_region plus combined_margin result in total_region.
	 *
	 * The GObject margin color property value is stored
	 * in the combined_margin's color field. The color fields
	 * of letterbox_margin and extra_margin are not used.
	 */
	Imx2dBlitMargin letterbox_margin;
	Imx2dBlitMargin combined_margin;

	gint xpos, ypos;
	gint width, height;
	Imx2dBlitMargin extra_margin;
	Imx2dRotation output_rotation;
	gboolean force_aspect_ratio;
	gboolean input_crop;
	gdouble alpha;
};


struct _GstImx2dCompositorPadClass
{
	GstVideoAggregatorPadClass parent_class;
};


enum
{
	PROP_PAD_0,
	PROP_PAD_XPOS,
	PROP_PAD_YPOS,
	PROP_PAD_WIDTH,
	PROP_PAD_HEIGHT,
	PROP_PAD_LEFT_MARGIN,
	PROP_PAD_TOP_MARGIN,
	PROP_PAD_RIGHT_MARGIN,
	PROP_PAD_BOTTOM_MARGIN,
	PROP_PAD_MARGIN_COLOR,
	PROP_PAD_OUTPUT_ROTATION,
	PROP_PAD_FORCE_ASPECT_RATIO,
	PROP_PAD_INPUT_CROP,
	PROP_PAD_ALPHA
};

#define DEFAULT_PAD_XPOS 0
#define DEFAULT_PAD_YPOS 0
#define DEFAULT_PAD_WIDTH 320
#define DEFAULT_PAD_HEIGHT 240
#define DEFAULT_PAD_LEFT_MARGIN 0
#define DEFAULT_PAD_TOP_MARGIN 0
#define DEFAULT_PAD_RIGHT_MARGIN 0
#define DEFAULT_PAD_BOTTOM_MARGIN 0
#define DEFAULT_PAD_MARGIN_COLOR (0xFF000000)
#define DEFAULT_PAD_OUTPUT_ROTATION IMX_2D_ROTATION_NONE
#define DEFAULT_PAD_FORCE_ASPECT_RATIO TRUE
#define DEFAULT_PAD_INPUT_CROP TRUE
#define DEFAULT_PAD_ALPHA 1.0


G_DEFINE_TYPE(GstImx2dCompositorPad, gst_imx_2d_compositor_pad, GST_TYPE_VIDEO_AGGREGATOR_PAD)

static void gst_imx_2d_compositor_pad_finalize(GObject *object);

static void gst_imx_2d_compositor_pad_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_2d_compositor_pad_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstPadProbeReturn gst_imx_2d_compositor_caps_event_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

static void gst_imx_2d_compositor_pad_recalculate_regions_if_needed(GstImx2dCompositorPad *self, GstVideoInfo *output_video_info);


static void gst_imx_2d_compositor_pad_class_init(GstImx2dCompositorPadClass *klass)
{
	GObjectClass *object_class;
	GstVideoAggregatorPadClass *videoaggregator_pad_class;

	object_class = G_OBJECT_CLASS(klass);
	videoaggregator_pad_class = GST_VIDEO_AGGREGATOR_PAD_CLASS(klass);

	object_class->finalize      = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_pad_finalize);
	object_class->set_property  = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_pad_set_property);
	object_class->get_property  = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_pad_get_property);

	/* Explicitely set these to NULL to force the base class
	 * to not try any software-based colorspace conversions
	 * Subclasses use i.MX blitters, which are capable of
	 * hardware-accelerated colorspace conversions */
	videoaggregator_pad_class->prepare_frame = NULL;
	videoaggregator_pad_class->clean_frame   = NULL;

	g_object_class_install_property(
		object_class,
		PROP_PAD_XPOS,
		g_param_spec_int(
			"xpos",
			"X position",
			"Left X coordinate in pixels",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_XPOS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_YPOS,
		g_param_spec_int(
			"ypos",
			"Y position",
			"Top Y coordinate in pixels",
			G_MININT, G_MAXINT,
			DEFAULT_PAD_YPOS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_WIDTH,
		g_param_spec_int(
			"width",
			"Width",
			"Width in pixels",
			0, G_MAXINT,
			DEFAULT_PAD_WIDTH,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_HEIGHT,
		g_param_spec_int(
			"height",
			"Height",
			"Height in pixels",
			0, G_MAXINT,
			DEFAULT_PAD_HEIGHT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_LEFT_MARGIN,
		g_param_spec_int(
			"left-margin",
			"Left margin",
			"Left margin",
			0, G_MAXINT,
			DEFAULT_PAD_LEFT_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_TOP_MARGIN,
		g_param_spec_int(
			"top-margin",
			"Top margin",
			"Top margin",
			0, G_MAXINT,
			DEFAULT_PAD_TOP_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_RIGHT_MARGIN,
		g_param_spec_int(
			"right-margin",
			"Right margin",
			"Right margin",
			0, G_MAXINT,
			DEFAULT_PAD_RIGHT_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_BOTTOM_MARGIN,
		g_param_spec_int(
			"bottom-margin",
			"Bottom margin",
			"Bottom margin",
			0, G_MAXINT,
			DEFAULT_PAD_BOTTOM_MARGIN,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_MARGIN_COLOR,
		g_param_spec_uint(
			"margin-color",
			"Margin color",
			"Margin color (format: 0xAARRGGBB)",
			0, 0xFFFFFFFF,
			DEFAULT_PAD_MARGIN_COLOR,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_OUTPUT_ROTATION,
		g_param_spec_enum(
			"output-rotation",
			"Output rotation",
			"Output rotation in 90-degree steps",
			gst_imx_2d_rotation_get_type(),
			DEFAULT_PAD_OUTPUT_ROTATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_FORCE_ASPECT_RATIO,
		g_param_spec_boolean(
			"force-aspect-ratio",
			"Force aspect ratio",
			"When enabled, scaling will respect original aspect ratio",
			DEFAULT_PAD_FORCE_ASPECT_RATIO,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_INPUT_CROP,
		g_param_spec_boolean(
			"input-crop",
			"Input crop",
			"Whether or not to crop input frames based on their video crop metadata",
			DEFAULT_PAD_INPUT_CROP,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAD_ALPHA,
		g_param_spec_double(
			"alpha",
			"Alpha",
			"Alpha blending factor (range:  0.0 = fully transparent  1.0 = fully opaque)",
			0.0, 1.0,
			DEFAULT_PAD_ALPHA,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE
		)
	);
}


static void gst_imx_2d_compositor_pad_init(GstImx2dCompositorPad *self)
{
	self->xpos = DEFAULT_PAD_XPOS;
	self->ypos = DEFAULT_PAD_YPOS;
	self->width = DEFAULT_PAD_WIDTH;
	self->height = DEFAULT_PAD_HEIGHT;
	self->extra_margin.left_margin = DEFAULT_PAD_LEFT_MARGIN;
	self->extra_margin.top_margin = DEFAULT_PAD_TOP_MARGIN;
	self->extra_margin.right_margin = DEFAULT_PAD_RIGHT_MARGIN;
	self->extra_margin.bottom_margin = DEFAULT_PAD_BOTTOM_MARGIN;
	self->combined_margin.color = DEFAULT_PAD_MARGIN_COLOR;
	self->output_rotation = DEFAULT_PAD_OUTPUT_ROTATION;
	self->force_aspect_ratio = DEFAULT_PAD_FORCE_ASPECT_RATIO;
	self->input_crop = DEFAULT_PAD_INPUT_CROP;
	self->alpha = DEFAULT_PAD_ALPHA;

	self->input_surface = imx_2d_surface_create(NULL, NULL);
	memset(&(self->input_surface_desc), 0, sizeof(self->input_surface_desc));

	self->region_coords_need_update = TRUE;

	self->inner_region_fills_output_frame = TRUE;
	self->total_region_fills_output_frame = TRUE;

	memset(&(self->letterbox_margin), 0, sizeof(Imx2dRegion));
	memcpy(&(self->combined_margin), &(self->extra_margin), sizeof(Imx2dRegion));

	gst_pad_add_probe(GST_PAD(self), GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, gst_imx_2d_compositor_caps_event_probe, NULL, NULL);
}


static void gst_imx_2d_compositor_pad_finalize(GObject *object)
{
	GstImx2dCompositorPad *self = GST_IMX_2D_COMPOSITOR_PAD(object);

	if (self->input_surface != NULL)
		imx_2d_surface_destroy(self->input_surface);

	G_OBJECT_CLASS(gst_imx_2d_compositor_pad_parent_class)->finalize(object);
}


static void gst_imx_2d_compositor_pad_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImx2dCompositorPad *self = GST_IMX_2D_COMPOSITOR_PAD(object);

	switch (prop_id)
	{
		case PROP_PAD_XPOS:
			GST_OBJECT_LOCK(self);
			self->outer_region.x1 = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_YPOS:
			GST_OBJECT_LOCK(self);
			self->outer_region.y1 = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_WIDTH:
			GST_OBJECT_LOCK(self);
			self->width = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_HEIGHT:
			GST_OBJECT_LOCK(self);
			self->height = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_LEFT_MARGIN:
			GST_OBJECT_LOCK(self);
			self->extra_margin.left_margin = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_TOP_MARGIN:
			GST_OBJECT_LOCK(self);
			self->extra_margin.top_margin = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_RIGHT_MARGIN:
			GST_OBJECT_LOCK(self);
			self->extra_margin.right_margin = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_BOTTOM_MARGIN:
			GST_OBJECT_LOCK(self);
			self->extra_margin.bottom_margin = g_value_get_int(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_MARGIN_COLOR:
			GST_OBJECT_LOCK(self);
			self->combined_margin.color = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_OUTPUT_ROTATION:
			GST_OBJECT_LOCK(self);
			self->output_rotation = g_value_get_enum(value);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_FORCE_ASPECT_RATIO:
			GST_OBJECT_LOCK(self);
			self->force_aspect_ratio = g_value_get_boolean(value);
			self->region_coords_need_update = TRUE;
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_INPUT_CROP:
			GST_OBJECT_LOCK(self);
			self->input_crop = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_ALPHA:
			GST_OBJECT_LOCK(self);
			self->alpha = g_value_get_double(value);
			GST_OBJECT_UNLOCK(self);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_2d_compositor_pad_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImx2dCompositorPad *self = GST_IMX_2D_COMPOSITOR_PAD(object);

	switch (prop_id)
	{
		case PROP_PAD_XPOS:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->outer_region.x1);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_YPOS:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->outer_region.y1);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_WIDTH:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->width);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_HEIGHT:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->height);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_LEFT_MARGIN:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->extra_margin.left_margin);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_TOP_MARGIN:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->extra_margin.top_margin);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_RIGHT_MARGIN:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->extra_margin.right_margin);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_BOTTOM_MARGIN:
			GST_OBJECT_LOCK(self);
			g_value_set_int(value, self->extra_margin.bottom_margin);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_MARGIN_COLOR:
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->combined_margin.color);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_OUTPUT_ROTATION:
			GST_OBJECT_LOCK(self);
			g_value_set_enum(value, self->output_rotation);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_FORCE_ASPECT_RATIO:
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->force_aspect_ratio);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_INPUT_CROP:
			GST_OBJECT_LOCK(self);
			g_value_set_boolean(value, self->input_crop);
			GST_OBJECT_UNLOCK(self);
			break;

		case PROP_PAD_ALPHA:
			GST_OBJECT_LOCK(self);
			g_value_set_double(value, self->alpha);
			GST_OBJECT_UNLOCK(self);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstPadProbeReturn gst_imx_2d_compositor_caps_event_probe(GstPad *pad, GstPadProbeInfo *info, G_GNUC_UNUSED gpointer user_data)
{
	/* In this probe, we intercept CAPS events to replace the format string
	 * if necessary. Currently, the Amphion tiled format is not supported in
	 * gstvideo, so we must replace the tiled NV12/NV21 formats with the
	 * regular NV12/NV21 ones, otherwise the gst_video_info_from_caps() call
	 * inside GstVideoAggregator would fail. */

	GstImx2dCompositorPad *compositor_pad = GST_IMX_2D_COMPOSITOR_PAD(pad);
	GstVideoInfo video_info;
	GstEvent *event;
	GstImx2dTileLayout input_video_tile_layout = GST_IMX_2D_TILE_LAYOUT_NONE;

	if (G_UNLIKELY((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) == 0))
		return GST_PAD_PROBE_OK;

	event = GST_PAD_PROBE_INFO_EVENT(info);
	g_assert(event != NULL);

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_CAPS:
		{
			GstCaps *caps;
			GstEvent *new_event;

			gst_event_parse_caps(event, &caps);

			if (!gst_imx_video_info_from_caps(&video_info, caps, &input_video_tile_layout))
			{
				GST_ERROR_OBJECT(pad, "cannot convert caps to video info; caps: %" GST_PTR_FORMAT, (gpointer)caps);
				return GST_PAD_PROBE_OK;
			}

			new_event = gst_event_new_caps(caps);
			gst_event_unref(event);
			GST_PAD_PROBE_INFO_DATA(info) = new_event;

			GST_LOG_OBJECT(pad, "marking pad region coords as in need of an update");
			GST_LOG_OBJECT(pad, "imx2d compositor pad caps: %" GST_PTR_FORMAT, (gpointer)caps);

			GST_OBJECT_LOCK(compositor_pad);

			compositor_pad->input_surface_desc.width = GST_VIDEO_INFO_WIDTH(&video_info);
			compositor_pad->input_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&video_info);
			compositor_pad->input_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&video_info), &input_video_tile_layout);

			compositor_pad->region_coords_need_update = TRUE;

			GST_OBJECT_UNLOCK(compositor_pad);

			break;
		}

		default:
			break;
	}

	return GST_PAD_PROBE_OK;
}


static void gst_imx_2d_compositor_pad_recalculate_regions_if_needed(GstImx2dCompositorPad *self, GstVideoInfo *output_video_info)
{
	GstVideoInfo *input_video_info;
	guint video_width, video_height;

	if (!self->region_coords_need_update)
		return;

	/* Relations between regions and margins:
	 *
	 * total_region = outer_region + extra_margin.
	 * outer_region = inner_region + letterbox_margin.
	 * combined_margin = extra_margin + letterbox_margin.
	 *
	 * Also:
	 * xpos, ypos, width, height define the total_region boundaries.
	 */

	self->total_region.x1 = self->xpos;
	self->total_region.y1 = self->ypos;
	self->total_region.x2 = self->xpos + self->width;
	self->total_region.y2 = self->ypos + self->height;

	self->outer_region.x1 = self->total_region.x1 + self->extra_margin.left_margin;
	self->outer_region.y1 = self->total_region.y1 + self->extra_margin.top_margin;
	self->outer_region.x2 = self->total_region.x2 - self->extra_margin.right_margin;
	self->outer_region.y2 = self->total_region.y2 - self->extra_margin.bottom_margin;

	GST_DEBUG_OBJECT(
		self,
		"pad xpos/ypos: %d/%d  pad width/height: %d/%d  output width/height: %d/%d  inner/total regions fill output frame: %d/%d",
		self->xpos, self->ypos,
		self->width, self->height,
		GST_VIDEO_INFO_WIDTH(output_video_info), GST_VIDEO_INFO_HEIGHT(output_video_info),
		self->inner_region_fills_output_frame, self->total_region_fills_output_frame
	);

	/* This should not happen, and typically indicates invalid user
	 * defined extra margins. */
	if (G_UNLIKELY(self->outer_region.x1 > self->outer_region.x2))
		GST_ERROR_OBJECT(self, "calculated outer region X coordinates are invalid: x1 = %d x2 = %d (x1 must be <= x2)", self->outer_region.x1, self->outer_region.x2);
	if (G_UNLIKELY(self->outer_region.y1 > self->outer_region.y2))
		GST_ERROR_OBJECT(self, "calculated outer region Y coordinates are invalid: y1 = %d y2 = %d (y1 must be <= y2)", self->outer_region.y1, self->outer_region.y2);

	GST_DEBUG_OBJECT(self, "calculated outer region: %" IMX_2D_REGION_FORMAT, IMX_2D_REGION_ARGS(&(self->outer_region)));

	self->combined_margin.left_margin = self->extra_margin.left_margin;
	self->combined_margin.top_margin = self->extra_margin.top_margin;
	self->combined_margin.right_margin = self->extra_margin.right_margin;
	self->combined_margin.bottom_margin = self->extra_margin.bottom_margin;

	input_video_info = &(GST_VIDEO_AGGREGATOR_PAD_CAST(self)->info);
	video_width = GST_VIDEO_INFO_WIDTH(input_video_info);
	video_height = GST_VIDEO_INFO_HEIGHT(input_video_info);

	/* Calculate a letterbox_margin if necessary.
	 *
	 * If force_aspect_ratio is FALSE, then the frame will always
	 * be scaled to fill the outer_region. In other words, in that
	 * case, inner_region == outer_region.
	 *
	 * In rare cases where width and height are initially 0 (can happen
	 * with some broken video input), we cannot calculate letterbox
	 * margins, because this would lead to divisions by zero.
	 */
	if (self->force_aspect_ratio && ((self->outer_region.x1 < self->outer_region.x2))
	                             && ((self->outer_region.y1 < self->outer_region.y2))
	                             && ((video_width > 0))
	                             && ((video_height > 0)))
	{
		gboolean transposed = (self->output_rotation == IMX_2D_ROTATION_90)
			               || (self->output_rotation == IMX_2D_ROTATION_270);

		gst_imx_2d_canvas_calculate_letterbox_margin(
			&(self->letterbox_margin),
			&(self->inner_region),
			&(self->outer_region),
			transposed,
			video_width,
			video_height,
			GST_VIDEO_INFO_PAR_N(input_video_info),
			GST_VIDEO_INFO_PAR_D(input_video_info)
		);

		self->combined_margin.left_margin += self->letterbox_margin.left_margin;
		self->combined_margin.top_margin += self->letterbox_margin.top_margin;
		self->combined_margin.right_margin += self->letterbox_margin.right_margin;
		self->combined_margin.bottom_margin += self->letterbox_margin.bottom_margin;
	}
	else
		memcpy(&(self->inner_region), &(self->outer_region), sizeof(Imx2dRegion));

	/* Determine if inner and/or outer regions fill the entire output frame.
	 * This is used in gst_imx_2d_compositor_aggregate_frames() to decide
	 * whether or not the output frame has to be cleared with the background
	 * color first. Avoiding unnecessary clearing operations saves bandwidth.
	 * NOTE: We do NOT take alpha into account here, since alpha can
	 * be adjusted independently of the region coordinates. If the user
	 * only adjusts alpha, we can still reuse the results from here. */
	self->inner_region_fills_output_frame = (self->inner_region.x1 <= 0)
	                                     && (self->inner_region.y1 <= 0)
	                                     && (self->inner_region.x2 >= GST_VIDEO_INFO_WIDTH(output_video_info))
	                                     && (self->inner_region.y2 >= GST_VIDEO_INFO_HEIGHT(output_video_info));
	self->total_region_fills_output_frame = (self->xpos <= 0)
	                                     && (self->ypos <= 0)
	                                     && (self->width >= GST_VIDEO_INFO_WIDTH(output_video_info))
	                                     && (self->height >= GST_VIDEO_INFO_HEIGHT(output_video_info));

	GST_DEBUG_OBJECT(self, "calculated inner region: %" IMX_2D_REGION_FORMAT, IMX_2D_REGION_ARGS(&(self->inner_region)));

	/* Mark the coordinates as updated so they are not
	 * needlessly recalculated later. */
	self->region_coords_need_update = FALSE;
}




/********** GstImx2dCompositor **********/

enum
{
	PROP_0,
	PROP_BACKGROUND_COLOR
};

#define DEFAULT_BACKGROUND_COLOR 0x000000




/* We must implement the GstChildProxy interface to allow
 * access to the custom pad proprerties (xpos etc.). */
static void gst_imx_2d_compositor_child_proxy_iface_init(gpointer iface, gpointer iface_data);
static GObject* gst_imx_2d_compositor_child_proxy_get_child_by_index(GstChildProxy *child_proxy, guint index);
static guint gst_imx_2d_compositor_child_proxy_get_children_count(GstChildProxy *child_proxy);


G_DEFINE_ABSTRACT_TYPE_WITH_CODE(
	GstImx2dCompositor, gst_imx_2d_compositor, GST_TYPE_VIDEO_AGGREGATOR,
	G_IMPLEMENT_INTERFACE(GST_TYPE_CHILD_PROXY, gst_imx_2d_compositor_child_proxy_iface_init)
)


/* Base class function overloads. */

/* General element operations. */
static void gst_imx_2d_compositor_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_2d_compositor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstPad* gst_imx_2d_compositor_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *req_name, GstCaps const *caps);
static void gst_imx_2d_compositor_release_pad(GstElement *element, GstPad *pad);

/* Allocator. */
static gboolean gst_imx_2d_compositor_decide_allocation(GstAggregator *aggregator, GstQuery *query);

/* Misc aggregator functionality. */
static gboolean gst_imx_2d_compositor_start(GstAggregator *aggregator);
static gboolean gst_imx_2d_compositor_stop(GstAggregator *aggregator);
static gboolean gst_imx_2d_compositor_sink_query(GstAggregator *aggregator, GstAggregatorPad *pad, GstQuery *query);

/* Caps handling. */
static gboolean gst_imx_2d_compositor_negotiated_src_caps(GstAggregator *aggregator, GstCaps *caps);

/* Frame output. */
static GstFlowReturn gst_imx_2d_compositor_aggregate_frames(GstVideoAggregator *videoaggregator, GstBuffer *output_buffer);

/* Misc GstImx2dCompositor functionality. */
static gboolean gst_imx_2d_compositor_create_blitter(GstImx2dCompositor *self);


static void gst_imx_2d_compositor_class_init(GstImx2dCompositorClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstAggregatorClass *aggregator_class;
	GstVideoAggregatorClass *video_aggregator_class;

	gst_imx_2d_setup_logging();

	GST_DEBUG_CATEGORY_INIT(imx_2d_compositor_debug, "imx2dcompositor", 0, "NXP i.MX 2D video compositor base class");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);
	aggregator_class = GST_AGGREGATOR_CLASS(klass);
	video_aggregator_class = GST_VIDEO_AGGREGATOR_CLASS(klass);

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_get_property);

	element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_request_new_pad);
	element_class->release_pad     = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_release_pad);

	aggregator_class->decide_allocation   = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_decide_allocation);
	aggregator_class->start               = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_start);
	aggregator_class->stop                = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_stop);
	aggregator_class->sink_query          = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_sink_query);
	aggregator_class->negotiated_src_caps = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_negotiated_src_caps);

	video_aggregator_class->aggregate_frames = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_aggregate_frames);

	klass->create_blitter = NULL;

	g_object_class_install_property(
		object_class,
		PROP_BACKGROUND_COLOR,
		g_param_spec_uint(
			"background-color",
			"Background color",
			"Background color (format: 0xRRGGBB)",
			0,
			0xFFFFFF,
			DEFAULT_BACKGROUND_COLOR,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_2d_compositor_init(GstImx2dCompositor *self)
{
	self->background_color = DEFAULT_BACKGROUND_COLOR;
}


static void gst_imx_2d_compositor_child_proxy_iface_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
	GstChildProxyInterface *child_proxy_iface = (GstChildProxyInterface *)iface;

	child_proxy_iface->get_child_by_index = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_child_proxy_get_child_by_index);
	child_proxy_iface->get_children_count = GST_DEBUG_FUNCPTR(gst_imx_2d_compositor_child_proxy_get_children_count);
}


static GObject* gst_imx_2d_compositor_child_proxy_get_child_by_index(GstChildProxy *child_proxy, guint index)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(child_proxy);
	GObject *obj = NULL;

	/* Lock the element to make sure that sink pads aren't
	 * added/removed while we access the sinkpads list. */
	GST_OBJECT_LOCK(self);
	obj = g_list_nth_data(GST_ELEMENT_CAST(self)->sinkpads, index);
	if (obj != NULL)
		gst_object_ref(obj);
	GST_OBJECT_UNLOCK(self);

	return obj;
}


static guint gst_imx_2d_compositor_child_proxy_get_children_count(GstChildProxy *child_proxy)
{
	guint count = 0;
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(child_proxy);

	/* Lock the element to make sure that sink pads aren't
	 * added/removed while we access the sinkpads list. */
	GST_OBJECT_LOCK(self);
	count = GST_ELEMENT_CAST(self)->numsinkpads;
	GST_OBJECT_UNLOCK(self);

	return count;	
}


static void gst_imx_2d_compositor_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(object);

	switch (prop_id)
	{
		case PROP_BACKGROUND_COLOR:
		{
			GST_OBJECT_LOCK(self);
			self->background_color = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_2d_compositor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(object);

	switch (prop_id)
	{
		case PROP_BACKGROUND_COLOR:
		{
			GST_OBJECT_LOCK(self);
			g_value_set_uint(value, self->background_color);
			GST_OBJECT_UNLOCK(self);
			break;
		}

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstPad* gst_imx_2d_compositor_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *req_name, GstCaps const *caps)
{
	GstPad *new_pad;

	/* We intercept the new-pad request to add the new pad
	 * to the GstChildProxy interface. Also, this allows
	 * for performing sanity checks on the new pad. */

	new_pad = GST_ELEMENT_CLASS(gst_imx_2d_compositor_parent_class)->request_new_pad(element, templ, req_name, caps);
	if (new_pad == NULL)
	{
		GST_ERROR_OBJECT(element, "could not create new request pad");
		goto error;
	}

	if (G_UNLIKELY(GST_IMX_2D_COMPOSITOR_PAD(new_pad)->input_surface == NULL))
	{
		GST_ERROR_OBJECT(element, "new request pad has no imx2d input surface");
		goto error;
	}

	GST_DEBUG_OBJECT(element, "created and added new request pad %s:%s", GST_DEBUG_PAD_NAME(new_pad));

	gst_child_proxy_child_added(GST_CHILD_PROXY(element), G_OBJECT(new_pad), GST_OBJECT_NAME(new_pad));

	return new_pad;

error:
	if (new_pad != NULL)
		gst_object_unref(GST_OBJECT(new_pad));
	return NULL;
}


static void gst_imx_2d_compositor_release_pad(GstElement *element, GstPad *pad)
{
	GST_DEBUG_OBJECT(element, "releasing request pad %s:%s", GST_DEBUG_PAD_NAME(pad));

	/* We intercept the new-pad request to remove the pad
	 * from the GstChildProxy interface, since this does
	 * not happen automatically. */
	gst_child_proxy_child_removed(GST_CHILD_PROXY(element), G_OBJECT(pad), GST_OBJECT_NAME(pad));

	GST_ELEMENT_CLASS(gst_imx_2d_compositor_parent_class)->release_pad(element, pad);
}


static gboolean gst_imx_2d_compositor_decide_allocation(GstAggregator *aggregator, GstQuery *query)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(aggregator);
	GstStructure *pool_config;
	guint i;
	GstCaps *negotiated_caps;
	GstAllocator *selected_allocator = NULL;
	GstAllocationParams allocation_params;
	GstBufferPool *new_buffer_pool = NULL;
	guint buffer_size;

	/* Chain up to the base class.
	 * We first do that, then modify the query. That way, we can be
	 * sure that our modifications remain, and aren't overwritten. */
	/* XXX: Also, currently, there seems to be a memory leak in the
	 * GstVideoAggregator base class decide_allocation vfunc that
	 * is triggered when an allocator was added to the allocation
	 * params before chaining up. */
	if (!GST_AGGREGATOR_CLASS(gst_imx_2d_compositor_parent_class)->decide_allocation(aggregator, query))
		return FALSE;

	GST_TRACE_OBJECT(self, "attempting to decide what buffer pool and allocator to use");

	gst_query_parse_allocation(query, &negotiated_caps, NULL);

	/* Look for an allocator that is an ImxDmaBuffer allocator. */
	for (i = 0; i < gst_query_get_n_allocation_params(query); ++i)
	{
		GstAllocator *allocator = NULL;

		gst_query_parse_nth_allocation_param(query, i, &allocator, &allocation_params);
		if (allocator == NULL)
			continue;

		if (GST_IS_IMX_DMA_BUFFER_ALLOCATOR(allocator))
		{
			GST_DEBUG_OBJECT(self, "allocator #%u in allocation query can allocate DMA memory", i);
			selected_allocator = allocator;
			break;
		}
		else
			gst_object_unref(GST_OBJECT_CAST(allocator));
	}

	/* If no suitable allocator was found, use our own. */
	if (selected_allocator == NULL)
	{
		GST_DEBUG_OBJECT(self, "found no allocator in query that can allocate DMA memory, using our own");
		gst_allocation_params_init(&allocation_params);
		selected_allocator = gst_object_ref(self->imx_dma_buffer_allocator);
	}

	/* Create our own buffer pool, and use the output video info size as
	 * its buffer size. We do not look at the pools in the query, because
	 * we want to make sure that the pool uses our selected allocator.
	 * Buffer pools may ignore allocators that we pass to them, but for
	 * this element, it is essential that the buffer pool uses the selected
	 * ImxDmaBuffer allocator. */
	GST_DEBUG_OBJECT(self, "creating new buffer pool");
	new_buffer_pool = gst_video_buffer_pool_new();
	/* decide_allocation() is called after negotiated_src_caps(),
	 * so it is safe to use self->output_video_info here. */
	buffer_size = GST_VIDEO_INFO_SIZE(&(self->output_video_info));

	/* Make sure the selected allocator is picked by setting
	 * it as the first entry in the allocation param list. */
	if (gst_query_get_n_allocation_params(query) == 0)
	{
		GST_DEBUG_OBJECT(self, "there are no allocation params in the allocation query; adding our params to it");
		gst_query_add_allocation_param(query, selected_allocator, &allocation_params);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "there are allocation params in the allocation query; setting our params as the first ones in the query");
		gst_query_set_nth_allocation_param(query, 0, selected_allocator, &allocation_params);
	}

	/* Make sure the selected buffer pool is picked by setting
	 * it as the first entry in the allocation pool list. */
	if (gst_query_get_n_allocation_pools(query) == 0)
	{
		GST_DEBUG_OBJECT(self, "there are no allocation pools in the allocation query; adding our buffer pool to it");
		gst_query_add_allocation_pool(query, new_buffer_pool, buffer_size, 0, 0);
	}
	else
	{
		GST_DEBUG_OBJECT(self, "there are allocation pools in the allocation query; setting our buffer pool as the first one in the query");
		gst_query_set_nth_allocation_pool(query, 0, new_buffer_pool, buffer_size, 0, 0);
	}

	/* Enable the videometa option in the buffer pool to make
	 * sure it gets added to newly created buffers. */
	pool_config = gst_buffer_pool_get_config(new_buffer_pool);
	gst_buffer_pool_config_set_params(pool_config, negotiated_caps, buffer_size, 0, 0);
	gst_buffer_pool_config_set_allocator(pool_config, selected_allocator, &allocation_params);
	gst_buffer_pool_config_add_option(pool_config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(new_buffer_pool, pool_config);

	/* Unref these, since we passed them to the query. */
	gst_object_unref(GST_OBJECT(selected_allocator));
	gst_object_unref(GST_OBJECT(new_buffer_pool));

	return TRUE;
}


static gboolean gst_imx_2d_compositor_start(GstAggregator *aggregator)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(aggregator);

	self->imx_dma_buffer_allocator = gst_imx_allocator_new();
	GST_DEBUG_OBJECT(self, "new i.MX DMA buffer allocator %" GST_PTR_FORMAT, (gpointer)(self->imx_dma_buffer_allocator));
	self->uploader = gst_imx_dma_buffer_uploader_new(self->imx_dma_buffer_allocator);

	if (!gst_imx_2d_compositor_create_blitter(self))
	{
		GST_ERROR_OBJECT(self, "creating blitter failed");
		goto error;
	}

	/* Create the output surface, but do not assign any
	 * DMA buffer or description to it yet. This will
	 * happen later in the aggregate_frames() and
	 * negotiated_src_caps() vfuncs, respectively. */
	self->output_surface = imx_2d_surface_create(NULL, NULL);
	if (self->output_surface == NULL)
	{
		GST_ERROR_OBJECT(self, "creating output surface failed");
		goto error;
	}

	return TRUE;

error:
	gst_imx_2d_compositor_stop(aggregator);
	return FALSE;
}


static gboolean gst_imx_2d_compositor_stop(GstAggregator *aggregator)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(aggregator);

	if (self->output_surface != NULL)
	{
		imx_2d_surface_destroy(self->output_surface);
		self->output_surface = NULL;
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

	return TRUE;
}


static gboolean gst_imx_2d_compositor_sink_query(GstAggregator *aggregator, GstAggregatorPad *pad, GstQuery *query)
{
	switch (GST_QUERY_TYPE(query))
	{
		case GST_QUERY_CAPS:
		{
			/* Custom caps query response. Take the sinkpad template caps,
			 * optionally filter them, and return them as the result.
			 * This ensures that the caps that the derived class supports
			 * for input data are actually used (by default, the aggregator
			 * base classes try to keep input and output caps equal) */

			GstCaps *filter, *caps;

			gst_query_parse_caps(query, &filter);
			caps = gst_pad_get_pad_template_caps(GST_PAD(pad));

			if (filter != NULL)
			{
				GstCaps *unfiltered_caps = gst_caps_make_writable(caps);
				caps = gst_caps_intersect(unfiltered_caps, filter);
				gst_caps_unref(unfiltered_caps);
			}

			GST_DEBUG_OBJECT(aggregator, "responding to CAPS query with caps %" GST_PTR_FORMAT, (gpointer)caps);

			gst_query_set_caps_result(query, caps);

			gst_caps_unref(caps);

			return TRUE;
		}

		case GST_QUERY_ACCEPT_CAPS:
		{
			/* Custom accept_caps query response. Simply check if
			 * the supplied caps are a valid subset of the sinkpad's
			 * template caps. This is done for the same reasons
			 * as the caps query response above. */

			GstCaps *accept_caps = NULL, *template_caps = NULL;
			gboolean ret;

			gst_query_parse_accept_caps(query, &accept_caps);
			template_caps = gst_pad_get_pad_template_caps(GST_PAD(pad));

			ret = gst_caps_is_subset(accept_caps, template_caps);
			GST_DEBUG_OBJECT(aggregator, "responding to ACCEPT_CAPS query with value %d  (acceptcaps: %" GST_PTR_FORMAT "  template caps %" GST_PTR_FORMAT ")", ret, (gpointer)accept_caps, (gpointer)template_caps);
			gst_query_set_accept_caps_result(query, ret);

			return TRUE;
		}

		default:
			return GST_AGGREGATOR_CLASS(gst_imx_2d_compositor_parent_class)->sink_query(aggregator, pad, query);
	}
}


static gboolean gst_imx_2d_compositor_negotiated_src_caps(GstAggregator *aggregator, GstCaps *caps)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(aggregator);
	guint i;
	GList *walk;
	GstVideoInfo output_video_info;
	Imx2dSurfaceDesc output_surface_desc;

	g_assert(self->blitter != NULL);

	/* Convert the caps to video info structures for easier acess. */

	GST_DEBUG_OBJECT(self, "setting caps: output caps: %" GST_PTR_FORMAT, (gpointer)caps);

	if (!gst_video_info_from_caps(&output_video_info, caps))
	{
		GST_ERROR_OBJECT(self, "cannot convert output caps to video info; output caps: %" GST_PTR_FORMAT, (gpointer)caps);
		return FALSE;
	}

	/* Fill the output surface description. None of its values can change
	 * in between buffers, since we allocate the output buffers ourselves.
	 * In gst_imx_2d_compositor_decide_allocation(), we set up the
	 * buffer pool that will be used for acquiring output buffers, and
	 * those buffers will always use the same plane stride and plane
	 * offset values. */
	memset(&output_surface_desc, 0, sizeof(output_surface_desc));
	output_surface_desc.width = GST_VIDEO_INFO_WIDTH(&output_video_info);
	output_surface_desc.height = GST_VIDEO_INFO_HEIGHT(&output_video_info);
	output_surface_desc.format = gst_imx_2d_convert_from_gst_video_format(GST_VIDEO_INFO_FORMAT(&output_video_info), NULL);

	/* As said above, we allocate the output buffers ourselves, so we can
	 * define what the plane stride and offset values should be. Do that
	 * by using this utility function to calculate the strides and offsets. */
	imx_2d_surface_desc_calculate_strides_and_offsets(&output_surface_desc, imx_2d_blitter_get_hardware_capabilities(self->blitter));

	imx_2d_surface_set_desc(self->output_surface, &output_surface_desc);

	/* Copy the calculated strides and offsets into the output_video_info
	 * structure so that its values and those in output_surface_desc match. */
	for (i = 0; i < GST_VIDEO_INFO_N_PLANES(&output_video_info); ++i)
	{
		GST_VIDEO_INFO_PLANE_STRIDE(&output_video_info, i) = output_surface_desc.plane_stride[i];
		GST_VIDEO_INFO_PLANE_OFFSET(&output_video_info, i) = output_surface_desc.plane_offset[i];
	}

	/* Also set the output_video_info size to the one that results from the
	 * values in output_surface_desc. This is particularly important for
	 * gst_imx_2d_compositor_decide_allocation(), since that function will
	 * be called once this negotiated_src_caps() function is done, and
	 * it will use the output_video_info values we set here. */
	GST_VIDEO_INFO_SIZE(&output_video_info) = imx_2d_surface_desc_calculate_framesize(&output_surface_desc);

	self->output_video_info = output_video_info;

	GST_OBJECT_LOCK(self);

	GST_LOG_OBJECT(self, "visiting %" G_GUINT16_FORMAT " sinkpad(s) to mark their regions as to be recalculated", GST_ELEMENT_CAST(aggregator)->numsinkpads);
	walk = GST_ELEMENT_CAST(aggregator)->sinkpads;
	for (; walk != NULL; walk = g_list_next(walk))
	{
		GstImx2dCompositorPad *compositor_pad = GST_IMX_2D_COMPOSITOR_PAD_CAST(walk->data);
		compositor_pad->region_coords_need_update = TRUE;
	}

	GST_OBJECT_UNLOCK(self);

	return GST_AGGREGATOR_CLASS(gst_imx_2d_compositor_parent_class)->negotiated_src_caps(aggregator, caps);
}


static GstFlowReturn gst_imx_2d_compositor_aggregate_frames(GstVideoAggregator *videoaggregator, GstBuffer *output_buffer)
{
	GstImx2dCompositor *self = GST_IMX_2D_COMPOSITOR(videoaggregator);
	GstFlowReturn flow_ret = GST_FLOW_OK;
	ImxDmaBuffer *out_dma_buffer;
	GList *walk;
	Imx2dBlitParams blit_params;
	GstVideoMeta *videometa;
	guint plane_index;
	gboolean background_needs_to_be_cleared = TRUE;
	gboolean blitting_started = FALSE;

	GST_LOG_OBJECT(self, "aggregating frames");

	g_assert(self->blitter != NULL);

	/* Sanity check on the output buffer, then retrieve
	 * the ImxDmaBuffer from it so we can pass that buffer
	 * to the output surface. */
	g_assert(gst_imx_has_imx_dma_buffer_memory(output_buffer));
	out_dma_buffer = gst_imx_get_dma_buffer_from_buffer(output_buffer);
	g_assert(out_dma_buffer != NULL);

	GST_LOG_OBJECT(self, "setting ImxDmaBuffer %p as output DMA buffer", (gpointer)(out_dma_buffer));
	imx_2d_surface_set_dma_buffer(self->output_surface, out_dma_buffer);

	/* The videometa of the output buffer needs to be filled
	 * with the correct stride and plane offset values. */
	videometa = gst_buffer_get_video_meta(output_buffer);
	g_assert(videometa != NULL);
	{
		GstVideoInfo *out_info = &(self->output_video_info);
		Imx2dSurfaceDesc const *output_surface_desc = imx_2d_surface_get_desc(self->output_surface);

		for (plane_index = 0; plane_index < GST_VIDEO_INFO_N_PLANES(out_info); ++plane_index)
		{
			videometa->stride[plane_index] = GST_VIDEO_INFO_PLANE_STRIDE(out_info, plane_index);
			videometa->offset[plane_index] = GST_VIDEO_INFO_PLANE_OFFSET(out_info, plane_index);

			GST_LOG_OBJECT(
				self,
				"output plane #%u info:  stride: %d  offset: %d",
				plane_index,
				output_surface_desc->plane_stride[plane_index],
				output_surface_desc->plane_offset[plane_index]
			);
		}
	}

	/* Start the imx2d blit sequence. */
	if (!imx_2d_blitter_start(self->blitter, self->output_surface))
	{
		GST_ERROR_OBJECT(self, "starting blitter failed");
		goto error;
	}

	blitting_started = TRUE;

	memset(&blit_params, 0, sizeof(blit_params));

	/* Lock the compositor to prevent pads from being added/removed
	 * while we are walking over the existing pads. */
	GST_OBJECT_LOCK(self);

	/* In this first walk, we look at each compositor sinkpad,
	 * update their regions if necessary, and determine if at
	 * least one of them produces frames that are 100% opaque
	 * and fully cover the screen. If so, we do not need to clear
	 * the output frame first. */
	GST_LOG_OBJECT(self, "looking at %" G_GUINT16_FORMAT " sinkpad(s) to see if the background needs to be cleared", GST_ELEMENT_CAST(videoaggregator)->numsinkpads);
	walk = GST_ELEMENT_CAST(videoaggregator)->sinkpads;
	for (; walk != NULL; walk = g_list_next(walk))
	{
		GstVideoAggregatorPad *videoaggregator_pad = walk->data;
		GstImx2dCompositorPad *compositor_pad = GST_IMX_2D_COMPOSITOR_PAD_CAST(videoaggregator_pad);
		GstBuffer *input_buffer;

		gst_imx_2d_compositor_pad_recalculate_regions_if_needed(compositor_pad, &(self->output_video_info));

		input_buffer = gst_video_aggregator_pad_get_current_buffer(videoaggregator_pad);
		if (G_UNLIKELY(input_buffer == NULL))
		{
			GST_LOG_OBJECT(
				self,
				"pad %s has no input buffer",
				GST_PAD_NAME(compositor_pad)
			);
			continue;
		}

		GST_LOG_OBJECT(
			self,
			"pad %s:  inner/total regions fill output frame: %d/%d  alpha: %f  margin color: %#08" G_GINT32_MODIFIER "x",
			GST_PAD_NAME(compositor_pad),
			compositor_pad->inner_region_fills_output_frame,
			compositor_pad->total_region_fills_output_frame,
			compositor_pad->alpha,
			compositor_pad->combined_margin.color
		);

		if (compositor_pad->alpha < 1.0)
		{
			GST_LOG_OBJECT(
				self,
				"pad %s's alpha value is %f -> not fully opaque",
				GST_PAD_NAME(compositor_pad),
				compositor_pad->alpha
			);
			continue;
		}

		if (background_needs_to_be_cleared)
		{
			if (GST_VIDEO_INFO_HAS_ALPHA(&(videoaggregator_pad->info)))
			{
				GST_LOG_OBJECT(
					self,
					"pad %s's video format is %s, which contains an alpha channel",
					GST_PAD_NAME(compositor_pad),
					gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&(videoaggregator_pad->info)))
				);

				continue;
			}

			if (compositor_pad->inner_region_fills_output_frame)
			{
				GST_LOG_OBJECT(
					self,
					"pad %s's inner region fully covers the output frame and is fully opaque; no need to clear the background",
					GST_PAD_NAME(compositor_pad)
				);
				background_needs_to_be_cleared = FALSE;
			}
			else
			{
				GST_LOG_OBJECT(
					self,
					"pad %s's inner region does not fully cover the output frame",
					GST_PAD_NAME(compositor_pad)
				);
			}


			if (compositor_pad->total_region_fills_output_frame)
			{
				gint margin_alpha = compositor_pad->combined_margin.color >> 24;
				if (margin_alpha == 255)
				{
					GST_LOG_OBJECT(
						self,
						"pad %s's total region fully covers the output frame, and both the actual frame and the margin are fully opaque; no need to clear the background",
						GST_PAD_NAME(compositor_pad)
					);
					background_needs_to_be_cleared = FALSE;
				}
				else
					GST_LOG_OBJECT(
						self,
						"pad %s's total region fully covers the output frame, but the margin is not fully opaque",
						GST_PAD_NAME(compositor_pad)
					);
			}
			else
			{
				GST_LOG_OBJECT(
					self,
					"pad %s's total region does not fully cover the output frame",
					GST_PAD_NAME(compositor_pad)
				);
			}
		}
	}

	if (background_needs_to_be_cleared)
	{
		GST_LOG_OBJECT(self, "need to clear background with color %#06" G_GINT32_MODIFIER "x", self->background_color & 0xFFFFFF);

		if (!imx_2d_blitter_fill_region(self->blitter, NULL, self->background_color))
		{
			GST_ERROR_OBJECT(self, "could not clear background");
			goto error_while_locked;
		}
	}

	/* In this second walk, we perform the actual blitting.
	 * Blitting order is defined by the zorder values of each sinkpad.
	 * This ordering is taken care of by the GstVideoAggregator base
	 * class, so we just have to visit each sinkpad sequentially. */
	GST_LOG_OBJECT(self, "getting input frames from %" G_GUINT16_FORMAT " sinkpad(s)", GST_ELEMENT_CAST(videoaggregator)->numsinkpads);
	walk = GST_ELEMENT_CAST(videoaggregator)->sinkpads;
	for (; walk != NULL; walk = g_list_next(walk))
	{
		GstVideoAggregatorPad *videoaggregator_pad = walk->data;
		GstImx2dCompositorPad *compositor_pad = GST_IMX_2D_COMPOSITOR_PAD_CAST(videoaggregator_pad);
		GstBuffer *input_buffer;
		ImxDmaBuffer *in_dma_buffer;
		gboolean input_crop;
		Imx2dRegion inner_region;
		Imx2dBlitMargin combined_margin;
		Imx2dRegion crop_rectangle;
		Imx2dRotation output_rotation;
		gint alpha;
		GstBuffer *uploading_result;
		int blit_ret;

		/* Retrieve the input from the sinkpad, and upload it.
		 * The uploader is capable of determining whether or not
		 * it actually needs to upload anything (which would imply
		 * a CPU based copying of the data). If it is not necessary,
		 * it will ref the data instead, thus providing zerocopy
		 * functionality. */

		input_buffer = gst_video_aggregator_pad_get_current_buffer(videoaggregator_pad);

		if (G_UNLIKELY(input_buffer == NULL))
			continue;

		flow_ret = gst_imx_dma_buffer_uploader_perform(self->uploader, input_buffer, &uploading_result);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
			goto error_while_locked;

		input_buffer = uploading_result;

		g_assert(gst_imx_has_imx_dma_buffer_memory(input_buffer));
		in_dma_buffer = gst_imx_get_dma_buffer_from_buffer(input_buffer);
		g_assert(in_dma_buffer != NULL);

		/* At this point, input_buffer is either the original input
		 * buffer (if the uploader just ref'd the buffer), or a new
		 * buffer with an ImxDmaBuffer as memory, and the contents
		 * of the original input_buffer inside. */

		{
			/* Lock the pad so we can get copies of its property
			 * values safely. Otherwise, the pad's set_property()
			 * function may be called concurrently, leading to
			 * race conditions. */
			GST_OBJECT_LOCK(compositor_pad);

			input_crop = compositor_pad->input_crop;
			output_rotation = compositor_pad->output_rotation;

			alpha = (gint)(compositor_pad->alpha * 255);
			alpha = CLAMP(alpha, 0, 255);

			memcpy(&inner_region, &(compositor_pad->inner_region), sizeof(inner_region));
			memcpy(&combined_margin, &(compositor_pad->combined_margin), sizeof(combined_margin));

			GST_OBJECT_UNLOCK(compositor_pad);
		}

		/* As explained in the input_surface_desc description (see
		 * the struct _GstImx2dCompositorPad defintion at the top),
		 * plane stride / offset values _can_ change in between
		 * buffers. The only way we can detect that is by using the
		 * values from the buffer's videometa. If the buffer has no
		 * videometa, we just use the stride/offset values from the
		 * pad's info structure. */
		videometa = gst_buffer_get_video_meta(input_buffer);
		if (videometa != NULL)
		{
			for (plane_index = 0; plane_index < videometa->n_planes; ++plane_index)
			{
				compositor_pad->input_surface_desc.plane_stride[plane_index] = videometa->stride[plane_index];
				compositor_pad->input_surface_desc.plane_offset[plane_index] = videometa->offset[plane_index];

				GST_LOG_OBJECT(
					self,
					"input plane #%u info from videometa:  stride: %d  offset: %d",
					plane_index,
					compositor_pad->input_surface_desc.plane_stride[plane_index],
					compositor_pad->input_surface_desc.plane_offset[plane_index]
				);
			}
		}
		else
		{
			GstVideoInfo *in_info = &(videoaggregator_pad->info);

			for (plane_index = 0; plane_index < GST_VIDEO_INFO_N_PLANES(in_info); ++plane_index)
			{
				compositor_pad->input_surface_desc.plane_stride[plane_index] = GST_VIDEO_INFO_PLANE_STRIDE(in_info, plane_index);
				compositor_pad->input_surface_desc.plane_offset[plane_index] = GST_VIDEO_INFO_PLANE_OFFSET(in_info, plane_index);

				GST_LOG_OBJECT(
					self,
					"input plane #%u info from videoinfo:  stride: %d  offset: %d",
					plane_index,
					compositor_pad->input_surface_desc.plane_stride[plane_index],
					compositor_pad->input_surface_desc.plane_offset[plane_index]
				);
			}
		}

		imx_2d_surface_set_desc(compositor_pad->input_surface, &(compositor_pad->input_surface_desc));
		imx_2d_surface_set_dma_buffer(compositor_pad->input_surface, in_dma_buffer);

		GST_LOG_OBJECT(
			self,
			"combined margin: %d/%d/%d/%d  margin color: %#08" G_GINT32_MODIFIER "x",
			combined_margin.left_margin,
			combined_margin.top_margin,
			combined_margin.right_margin,
			combined_margin.bottom_margin,
			(guint32)(combined_margin.color)
		);

		blit_params.margin = &combined_margin;
		blit_params.source_region = NULL;
		blit_params.dest_region = &inner_region;
		blit_params.rotation = output_rotation;
		blit_params.alpha = alpha;

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
			}
		}

		blit_ret = imx_2d_blitter_do_blit(self->blitter, compositor_pad->input_surface, &blit_params);

		gst_buffer_unref(input_buffer);

		if (!blit_ret)
		{
			GST_ERROR_OBJECT(self, "blitting failed");
			goto error_while_locked;
		}
	}

	GST_OBJECT_UNLOCK(self);

finish:
	if (blitting_started && !imx_2d_blitter_finish(self->blitter))
	{
		GST_ERROR_OBJECT(self, "finishing blitter failed");
		flow_ret = GST_FLOW_ERROR;
	}

	return flow_ret;

error:
	if (flow_ret != GST_FLOW_OK)
		flow_ret = GST_FLOW_ERROR;
	goto finish;

error_while_locked:
	GST_OBJECT_UNLOCK(self);
	goto error;
}


static gboolean gst_imx_2d_compositor_create_blitter(GstImx2dCompositor *self)
{
	GstImx2dCompositorClass *klass = GST_IMX_2D_COMPOSITOR_CLASS(G_OBJECT_GET_CLASS(self));

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


void gst_imx_2d_compositor_common_class_init(GstImx2dCompositorClass *klass, Imx2dHardwareCapabilities const *capabilities)
{
	GstElementClass *element_class;
	GstCaps *sink_template_caps;
	GstCaps *src_template_caps;
	GstPadTemplate *sink_template;
	GstPadTemplate *src_template;

	element_class = GST_ELEMENT_CLASS(klass);

	sink_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities(capabilities, GST_PAD_SINK);
	src_template_caps = gst_imx_2d_get_caps_from_imx2d_capabilities(capabilities, GST_PAD_SRC);

	sink_template = gst_pad_template_new_with_gtype("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST, sink_template_caps, GST_TYPE_IMX_2D_COMPOSITOR_PAD);
	src_template = gst_pad_template_new_with_gtype("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_template_caps, GST_TYPE_AGGREGATOR_PAD);

	gst_element_class_add_pad_template(element_class, sink_template);
	gst_element_class_add_pad_template(element_class, src_template);
}
