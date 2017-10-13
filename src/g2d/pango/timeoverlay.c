/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2014 Tim-Philipp Müller <tim@centricular.net>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-timeoverlay
 * @see_also: #GstImxG2DBaseTextOverlay, #GstImxG2DClockOverlay
 *
 * This element overlays the buffer time stamps of a video stream on
 * top of itself. You can position the text and configure the font details
 * using the properties of the #GstImxG2DBaseTextOverlay class. By default, the
 * time stamp is displayed in the top left corner of the picture, with some
 * padding to the left and to the top.
 *
 * <refsect2>
 * |[
 * gst-launch-1.0 -v videotestsrc ! timeoverlay ! autovideosink
 * ]| Display the time stamps in the top left corner of the video picture.
 * |[
 * gst-launch-1.0 -v videotestsrc ! timeoverlay halignment=right valignment=bottom text="Stream time:" shaded-background=true font-desc="Sans, 24" ! autovideosink
 * ]| Another pipeline that displays the time stamps with some leading
 * text in the bottom right corner of the video picture, with the background
 * of the text being shaded in order to make it more legible on top of a
 * bright video background.
 * </refsect2>
 */

#include "config.h"

#include <gst/video/video.h>

#include "timeoverlay.h"

#define DEFAULT_TIME_LINE GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_BUFFER_TIME
#define DEFAULT_PROP_TIMEALIGNMENT GST_IMX_G2D_TIME_OVERLAY_TIME_ALIGNMENT_RIGHT

enum
{
  PROP_0,
  PROP_TIME_LINE,
  PROP_TIMEALIGNMENT,
  PROP_LAST
};

#define gst_imx_g2d_time_overlay_parent_class parent_class
G_DEFINE_TYPE (GstImxG2DTimeOverlay, gst_imx_g2d_time_overlay, GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY);

static void gst_imx_g2d_time_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_imx_g2d_time_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define GST_TYPE_IMX_G2D_TIME_OVERLAY_TIMEALIGN (gst_imx_g2d_time_overlay_timealign_get_type())
static GType
gst_imx_g2d_time_overlay_timealign_get_type (void)
{
  static GType time_overlay_timealign_type = 0;
  static const GEnumValue time_overlay_timealign[] = {
    {GST_IMX_G2D_TIME_OVERLAY_TIME_ALIGNMENT_LEFT, "left", "left"},
    {GST_IMX_G2D_TIME_OVERLAY_TIME_ALIGNMENT_RIGHT, "right", "right"},
    {0, NULL, NULL},
  };

  if (!time_overlay_timealign_type) {
    time_overlay_timealign_type =
        g_enum_register_static ("GstImxG2DTimeOverlayTimeAlign",
        time_overlay_timealign);
  }
  return time_overlay_timealign_type;
}

#define GST_TYPE_IMX_G2D_TIME_OVERLAY_TIME_LINE (gst_imx_g2d_time_overlay_time_line_type())
static GType
gst_imx_g2d_time_overlay_time_line_type (void)
{
  static GType time_line_type = 0;
  static const GEnumValue modes[] = {
    {GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_BUFFER_TIME, "buffer-time", "buffer-time"},
    {GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_STREAM_TIME, "stream-time", "stream-time"},
    {GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_RUNNING_TIME, "running-time", "running-time"},
    {0, NULL, NULL},
  };

  if (!time_line_type) {
    time_line_type = g_enum_register_static ("GstImxG2DTimeOverlayTimeLine", modes);
  }
  return time_line_type;
}

static gchar *
gst_imx_g2d_time_overlay_render_time (GstImxG2DTimeOverlay * overlay, GstClockTime time)
{
  guint hours, mins, secs, msecs;

  if (!GST_CLOCK_TIME_IS_VALID (time))
    return g_strdup ("");

  hours = (guint) (time / (GST_SECOND * 60 * 60));
  mins = (guint) ((time / (GST_SECOND * 60)) % 60);
  secs = (guint) ((time / GST_SECOND) % 60);
  msecs = (guint) ((time % GST_SECOND) / (1000 * 1000));

  return g_strdup_printf ("%u:%02u:%02u.%03u", hours, mins, secs, msecs);
}

/* Called with lock held */
static gchar *
gst_imx_g2d_time_overlay_get_text (GstImxG2DBaseTextOverlay * overlay,
    GstBuffer * video_frame)
{
  GstImxG2DTimeOverlayTimeLine time_line;
  GstClockTime ts, ts_buffer;
  GstSegment *segment = &overlay->segment;
  gchar *time_str, *txt, *ret;
  GstImxG2DTimeOverlay *time_overlay = GST_IMX_G2D_TIME_OVERLAY (overlay);

  overlay->need_render = TRUE;

  ts_buffer = GST_BUFFER_TIMESTAMP (video_frame);

  if (!GST_CLOCK_TIME_IS_VALID (ts_buffer)) {
    GST_DEBUG ("buffer without valid timestamp");
    return g_strdup ("");
  }

  GST_DEBUG ("buffer with timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ts_buffer));

  time_line = g_atomic_int_get (&time_overlay->time_line);
  switch (time_line) {
    case GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_STREAM_TIME:
      ts = gst_segment_to_stream_time (segment, GST_FORMAT_TIME, ts_buffer);
      break;
    case GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_RUNNING_TIME:
      ts = gst_segment_to_running_time (segment, GST_FORMAT_TIME, ts_buffer);
      break;
    case GST_IMX_G2D_TIME_OVERLAY_TIME_LINE_BUFFER_TIME:
    default:
      ts = ts_buffer;
      break;
  }

  txt = g_strdup (overlay->default_text);

  time_str = gst_imx_g2d_time_overlay_render_time (time_overlay, ts);
  if (txt != NULL && *txt != '\0') {
    if (time_overlay->time_alignment == GST_IMX_G2D_TIME_OVERLAY_TIME_ALIGNMENT_RIGHT)
      ret = g_strdup_printf ("%s %s", txt, time_str);
    else
      ret = g_strdup_printf ("%s %s", time_str, txt);
  } else {
    ret = time_str;
    time_str = NULL;
  }

  g_free (txt);
  g_free (time_str);

  return ret;
}

static void
gst_imx_g2d_time_overlay_class_init (GstImxG2DTimeOverlayClass * klass)
{
  GstElementClass *gstelement_class;
  GstImxG2DBaseTextOverlayClass *gsttextoverlay_class;
  GObjectClass *gobject_class;
  PangoContext *context;
  PangoFontDescription *font_description;

  gsttextoverlay_class = (GstImxG2DBaseTextOverlayClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class, "Time overlay",
      "Filter/Editor/Video",
      "Overlays buffer time stamps on a video stream",
      "Tim-Philipp Müller <tim@centricular.net>");

  gsttextoverlay_class->get_text = gst_imx_g2d_time_overlay_get_text;

  gobject_class->set_property = gst_imx_g2d_time_overlay_set_property;
  gobject_class->get_property = gst_imx_g2d_time_overlay_get_property;

  g_object_class_install_property (gobject_class, PROP_TIME_LINE,
      g_param_spec_enum ("time-mode", "Time Mode", "What time to show",
          GST_TYPE_IMX_G2D_TIME_OVERLAY_TIME_LINE, DEFAULT_TIME_LINE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEALIGNMENT,
      g_param_spec_enum ("time-alignment", "Time alignment",
          "Time alignment of the text", GST_TYPE_IMX_G2D_TIME_OVERLAY_TIMEALIGN,
          DEFAULT_PROP_TIMEALIGNMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_mutex_lock (gsttextoverlay_class->pango_lock);
  context = gsttextoverlay_class->pango_context;

  pango_context_set_language (context, pango_language_from_string ("en_US"));
  pango_context_set_base_dir (context, PANGO_DIRECTION_LTR);

  font_description = pango_font_description_new ();
  pango_font_description_set_family_static (font_description, "Monospace");
  pango_font_description_set_style (font_description, PANGO_STYLE_NORMAL);
  pango_font_description_set_variant (font_description, PANGO_VARIANT_NORMAL);
  pango_font_description_set_weight (font_description, PANGO_WEIGHT_NORMAL);
  pango_font_description_set_stretch (font_description, PANGO_STRETCH_NORMAL);
  pango_font_description_set_size (font_description, 18 * PANGO_SCALE);
  pango_context_set_font_description (context, font_description);
  pango_font_description_free (font_description);
  g_mutex_unlock (gsttextoverlay_class->pango_lock);
}

static void
gst_imx_g2d_time_overlay_init (GstImxG2DTimeOverlay * overlay)
{
  GstImxG2DBaseTextOverlay *textoverlay;

  textoverlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (overlay);

  textoverlay->valign = GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP;
  textoverlay->halign = GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_LEFT;

  overlay->time_line = DEFAULT_TIME_LINE;
  overlay->time_alignment = DEFAULT_PROP_TIMEALIGNMENT;
}

static void
gst_imx_g2d_time_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImxG2DTimeOverlay *overlay = GST_IMX_G2D_TIME_OVERLAY (object);

  switch (prop_id) {
    case PROP_TIME_LINE:
      g_atomic_int_set (&overlay->time_line, g_value_get_enum (value));
      break;
    case PROP_TIMEALIGNMENT:
      overlay->time_alignment = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_imx_g2d_time_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImxG2DTimeOverlay *overlay = GST_IMX_G2D_TIME_OVERLAY (object);

  switch (prop_id) {
    case PROP_TIME_LINE:
      g_value_set_enum (value, g_atomic_int_get (&overlay->time_line));
      break;
    case PROP_TIMEALIGNMENT:
      g_value_set_enum (value, overlay->time_alignment);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
