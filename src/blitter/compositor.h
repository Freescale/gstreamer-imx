#ifndef GST_IMX_BLITTER_COMPOSITOR_H
#define GST_IMX_BLITTER_COMPOSITOR_H

#include "blitter.h"
#include "../compositor/compositor.h"


G_BEGIN_DECLS


typedef struct _GstImxBlitterCompositor GstImxBlitterCompositor;
typedef struct _GstImxBlitterCompositorClass GstImxBlitterCompositorClass;


#define GST_TYPE_IMX_BLITTER_COMPOSITOR             (gst_imx_blitter_compositor_get_type())
#define GST_IMX_BLITTER_COMPOSITOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_BLITTER_COMPOSITOR, GstImxBlitterCompositor))
#define GST_IMX_BLITTER_COMPOSITOR_CAST(obj)        ((GstImxBlitterCompositor *)(obj))
#define GST_IMX_BLITTER_COMPOSITOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_BLITTER_COMPOSITOR, GstImxBlitterCompositorClass))
#define GST_IS_IMX_BLITTER_COMPOSITOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_BLITTER_COMPOSITOR))
#define GST_IS_IMX_BLITTER_COMPOSITOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_BLITTER_COMPOSITOR))


struct _GstImxBlitterCompositor
{
	GstImxCompositor parent;
	GstImxBlitter *blitter;
};


/**
 * GstImxBlitterCompositorClass:
 *
 * Blitter-based implementation of compositor vfuncs. The class takes care of
 * setting the blitter's input/output frames, video infos, regions, canvases etc.
 * Subclasses only need to create a blitter that this class can use.
 *
 * @parent_class:   The parent class structure
 * @start:          Optional.
 *                  Called during the NULL->READY state change. Note that this is
 *                  called before @create_blitter.
 *                  If this returns FALSE, then the state change is considered to
 *                  have failed, and the sink's change_state function will return
 *                  GST_STATE_CHANGE_FAILURE.
 * @stop:           Optional.
 *                  Called during the READY->NULL state change.
 *                  Returns FALSE if stopping failed, TRUE otherwise.
 * @create_blitter: Required.
 *                  Instructs the subclass to create a new blitter instance and
 *                  return it. If the subclass is supposed to create the blitter
 *                  only once, then create it in @start, ref it here, and then
 *                  return it. It will be unref'd in the READY->NULL state change.
 */
struct _GstImxBlitterCompositorClass
{
	GstImxCompositorClass parent_class;
	gboolean (*start)(GstImxBlitterCompositor *blitter_video_sink);
	gboolean (*stop)(GstImxBlitterCompositor *blitter_video_sink);
	GstImxBlitter* (*create_blitter)(GstImxBlitterCompositor *blitter_compositor);
};


GType gst_imx_blitter_compositor_get_type(void);


G_END_DECLS


#endif
