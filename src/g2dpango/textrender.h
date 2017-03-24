#ifndef GST_IMX_G2D_TEXT_RENDER_H
#define GST_IMX_G2D_TEXT_RENDER_H

#include <gst/gst.h>
#include <pango/pangocairo.h>

G_BEGIN_DECLS

#define GST_TYPE_IMX_G2D_TEXT_RENDER            (gst_imx_g2d_text_render_get_type())
#define GST_IMX_G2D_TEXT_RENDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                        GST_TYPE_IMX_G2D_TEXT_RENDER, GstImxG2DTextRender))
#define GST_IMX_G2D_TEXT_RENDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
                                        GST_TYPE_IMX_G2D_TEXT_RENDER, GstImxG2DTextRenderClass))
#define GST_IMX_G2D_TEXT_RENDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                        GST_TYPE_IMX_G2D_TEXT_RENDER, GstImxG2DTextRenderClass))
#define GST_IS_TEXT_RENDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                        GST_TYPE_IMX_G2D_TEXT_RENDER))
#define GST_IS_TEXT_RENDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                        GST_TYPE_IMX_G2D_TEXT_RENDER))

typedef struct _GstImxG2DTextRender      GstImxG2DTextRender;
typedef struct _GstImxG2DTextRenderClass GstImxG2DTextRenderClass;

/**
 * GstImxG2DTextRenderVAlign:
 * @GST_IMX_G2D_TEXT_RENDER_VALIGN_BASELINE: draw text on the baseline
 * @GST_IMX_G2D_TEXT_RENDER_VALIGN_BOTTOM: draw text on the bottom
 * @GST_IMX_G2D_TEXT_RENDER_VALIGN_TOP: draw test on top
 *
 * Vertical alignment of the text.
 */
typedef enum {
    GST_IMX_G2D_TEXT_RENDER_VALIGN_BASELINE,
    GST_IMX_G2D_TEXT_RENDER_VALIGN_BOTTOM,
    GST_IMX_G2D_TEXT_RENDER_VALIGN_TOP
} GstImxG2DTextRenderVAlign;

/**
 * GstImxG2DTextRenderHAlign:
 * @GST_IMX_G2D_TEXT_RENDER_HALIGN_LEFT: align text left
 * @GST_IMX_G2D_TEXT_RENDER_HALIGN_CENTER: align text center
 * @GST_IMX_G2D_TEXT_RENDER_HALIGN_RIGHT: align text right
 *
 * Horizontal alignment of the text.
 */
typedef enum {
    GST_IMX_G2D_TEXT_RENDER_HALIGN_LEFT,
    GST_IMX_G2D_TEXT_RENDER_HALIGN_CENTER,
    GST_IMX_G2D_TEXT_RENDER_HALIGN_RIGHT
} GstImxG2DTextRenderHAlign;

/**
 * GstImxG2DTextRenderLineAlign:
 * @GST_IMX_G2D_TEXT_RENDER_LINE_ALIGN_LEFT: lines are left-aligned
 * @GST_IMX_G2D_TEXT_RENDER_LINE_ALIGN_CENTER: lines are center-aligned
 * @GST_IMX_G2D_TEXT_RENDER_LINE_ALIGN_RIGHT: lines are right-aligned
 *
 * Alignment of text lines relative to each other
 */
typedef enum {
    GST_IMX_G2D_TEXT_RENDER_LINE_ALIGN_LEFT = PANGO_ALIGN_LEFT,
    GST_IMX_G2D_TEXT_RENDER_LINE_ALIGN_CENTER = PANGO_ALIGN_CENTER,
    GST_IMX_G2D_TEXT_RENDER_LINE_ALIGN_RIGHT = PANGO_ALIGN_RIGHT
} GstImxG2DTextRenderLineAlign;

/**
 * GstImxG2DTextRender:
 *
 * Opaque textrender data structure.
 */
struct _GstImxG2DTextRender {
    GstElement            element;

    GstPad               *sinkpad, *srcpad;
    gint                  width;
    gint                  height;
    PangoLayout          *layout;
    gdouble               shadow_offset;
    gdouble               outline_offset;
    guchar               *text_image;
    gint                  image_width;
    gint                  image_height;
    gint                  baseline_y;
    gboolean              use_ARGB;

    GstImxG2DTextRenderVAlign     valign;
    GstImxG2DTextRenderHAlign     halign;
    GstImxG2DTextRenderLineAlign  line_align;

    gint xpad;
    gint ypad;

    GstEvent             *segment_event;
};

struct _GstImxG2DTextRenderClass {
    GstElementClass parent_class;

    PangoContext *pango_context;
};

GType gst_imx_g2d_text_render_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_IMX_G2D_TEXT_RENDER_H */
