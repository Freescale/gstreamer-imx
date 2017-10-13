/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Tim-Philipp Müller <tim@centricular.net>
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
 * SECTION:element-clockoverlay
 * @see_also: #GstImxG2DBaseTextOverlay, #GstImxG2DTimeOverlay
 *
 * This element overlays the current clock time on top of a video
 * stream. You can position the text and configure the font details
 * using the properties of the #GstImxG2DBaseTextOverlay class. By default, the
 * time is displayed in the top left corner of the picture, with some
 * padding to the left and to the top.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! clockoverlay ! autovideosink
 * ]| Display the current wall clock time in the top left corner of the video picture
 * |[
 * gst-launch-1.0 -v videotestsrc ! clockoverlay halignment=right valignment=bottom text="Edge City" shaded-background=true font-desc="Sans, 36" ! videoconvert ! autovideosink
 * ]| Another pipeline that displays the current time with some leading
 * text in the bottom right corner of the video picture, with the background
 * of the text being shaded in order to make it more legible on top of a
 * bright video background.
 * </refsect2>
 */

#include "config.h"

#include "clockoverlay.h"
#include <gst/video/video.h>
#include <time.h>


#define DEFAULT_PROP_TIMEFORMAT 	"%H:%M:%S"
#define DEFAULT_PROP_TIMEALIGNMENT GST_IMX_G2D_CLOCK_OVERLAY_TIME_ALIGNMENT_RIGHT

enum
{
  PROP_0,
  PROP_TIMEFORMAT,
  PROP_TIMEALIGNMENT,
  PROP_LAST
};

#define GST_TYPE_IMX_G2D_CLOCK_OVERLAY_TIMEALIGN (gst_imx_g2d_clock_overlay_timealign_get_type())
static GType
gst_imx_g2d_clock_overlay_timealign_get_type (void)
{
  static GType clock_overlay_timealign_type = 0;
  static const GEnumValue clock_overlay_timealign[] = {
    {GST_IMX_G2D_CLOCK_OVERLAY_TIME_ALIGNMENT_LEFT, "left", "left"},
    {GST_IMX_G2D_CLOCK_OVERLAY_TIME_ALIGNMENT_RIGHT, "right", "right"},
    {0, NULL, NULL},
  };

  if (!clock_overlay_timealign_type) {
    clock_overlay_timealign_type =
        g_enum_register_static ("GstImxG2DClockOverlayTimeAlign",
        clock_overlay_timealign);
  }
  return clock_overlay_timealign_type;
}

#define gst_imx_g2d_clock_overlay_parent_class parent_class
G_DEFINE_TYPE (GstImxG2DClockOverlay, gst_imx_g2d_clock_overlay, GST_TYPE_IMX_G2D_BASE_TEXT_OVERLAY);

static void gst_imx_g2d_clock_overlay_finalize (GObject * object);
static void gst_imx_g2d_clock_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_imx_g2d_clock_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gchar *
gst_imx_g2d_clock_overlay_render_time (GstImxG2DClockOverlay * overlay)
{
  struct tm *t;
  time_t now;
  gchar buf[256];

#ifdef HAVE_LOCALTIME_R
  struct tm dummy;
#endif

  now = time (NULL);

#ifdef HAVE_LOCALTIME_R
  /* Need to call tzset explicitly when calling localtime_r for changes
     to the timezone between calls to be visible.  */
  tzset ();
  t = localtime_r (&now, &dummy);
#else
  /* on win32 this apparently returns a per-thread struct which would be fine */
  t = localtime (&now);
#endif

  if (t == NULL)
    return g_strdup ("--:--:--");

  if (strftime (buf, sizeof (buf), overlay->format, t) == 0)
    return g_strdup ("");
  return g_strdup (buf);
}

/* Called with lock held */
static gchar *
gst_imx_g2d_clock_overlay_get_text (GstImxG2DBaseTextOverlay * overlay,
    GstBuffer * video_frame)
{
  gchar *time_str, *txt, *ret;
  GstImxG2DClockOverlay *clock_overlay = GST_IMX_G2D_CLOCK_OVERLAY (overlay);

  txt = g_strdup (overlay->default_text);

  time_str = gst_imx_g2d_clock_overlay_render_time (clock_overlay);
  if (txt != NULL && *txt != '\0') {
    if (clock_overlay->time_alignment == GST_IMX_G2D_CLOCK_OVERLAY_TIME_ALIGNMENT_RIGHT)
      ret = g_strdup_printf ("%s %s", txt, time_str);
    else
      ret = g_strdup_printf ("%s %s", time_str, txt);
  } else {
    ret = time_str;
    time_str = NULL;
  }

  if (g_strcmp0 (ret, clock_overlay->text)) {
    overlay->need_render = TRUE;
    g_free (clock_overlay->text);
    clock_overlay->text = g_strdup (ret);
  }

  g_free (txt);
  g_free (time_str);

  return ret;
}

static void
gst_imx_g2d_clock_overlay_class_init (GstImxG2DClockOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstImxG2DBaseTextOverlayClass *gsttextoverlay_class;
  PangoContext *context;
  PangoFontDescription *font_description;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gsttextoverlay_class = (GstImxG2DBaseTextOverlayClass *) klass;

  gobject_class->finalize = gst_imx_g2d_clock_overlay_finalize;
  gobject_class->set_property = gst_imx_g2d_clock_overlay_set_property;
  gobject_class->get_property = gst_imx_g2d_clock_overlay_get_property;

  gst_element_class_set_static_metadata (gstelement_class, "Clock overlay",
      "Filter/Editor/Video",
      "Overlays the current clock time on a video stream",
      "Tim-Philipp Müller <tim@centricular.net>");

  gsttextoverlay_class->get_text = gst_imx_g2d_clock_overlay_get_text;

  g_object_class_install_property (gobject_class, PROP_TIMEFORMAT,
      g_param_spec_string ("time-format", "Date/Time Format",
          "Format to use for time and date value, as in strftime.",
          DEFAULT_PROP_TIMEFORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEALIGNMENT,
      g_param_spec_enum ("time-alignment", "Date/Time alignment",
          "Date/Time alignment of the text", GST_TYPE_IMX_G2D_CLOCK_OVERLAY_TIMEALIGN,
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
gst_imx_g2d_clock_overlay_finalize (GObject * object)
{
  GstImxG2DClockOverlay *overlay = GST_IMX_G2D_CLOCK_OVERLAY (object);

  g_free (overlay->format);
  g_free (overlay->text);
  overlay->format = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gst_imx_g2d_clock_overlay_init (GstImxG2DClockOverlay * overlay)
{
  GstImxG2DBaseTextOverlay *textoverlay;

  textoverlay = GST_IMX_G2D_BASE_TEXT_OVERLAY (overlay);

  textoverlay->valign = GST_IMX_G2D_BASE_TEXT_OVERLAY_VALIGN_TOP;
  textoverlay->halign = GST_IMX_G2D_BASE_TEXT_OVERLAY_HALIGN_LEFT;

  overlay->format = g_strdup (DEFAULT_PROP_TIMEFORMAT);
  overlay->time_alignment = DEFAULT_PROP_TIMEALIGNMENT;
}


static void
gst_imx_g2d_clock_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstImxG2DClockOverlay *overlay = GST_IMX_G2D_CLOCK_OVERLAY (object);

  GST_OBJECT_LOCK (overlay);
  switch (prop_id) {
    case PROP_TIMEFORMAT:
      g_free (overlay->format);
      overlay->format = g_value_dup_string (value);
      break;
    case PROP_TIMEALIGNMENT:
      overlay->time_alignment = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (overlay);
}


static void
gst_imx_g2d_clock_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstImxG2DClockOverlay *overlay = GST_IMX_G2D_CLOCK_OVERLAY (object);

  GST_OBJECT_LOCK (overlay);
  switch (prop_id) {
    case PROP_TIMEFORMAT:
      g_value_set_string (value, overlay->format);
      break;
    case PROP_TIMEALIGNMENT:
      g_value_set_enum (value, overlay->time_alignment);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (overlay);
}
