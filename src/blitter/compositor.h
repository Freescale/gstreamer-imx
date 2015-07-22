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


struct _GstImxBlitterCompositorClass
{
	GstImxCompositorClass parent_class;
	GstImxBlitter* (*create_blitter)(GstImxBlitterCompositor *blitter_compositor);
};


GType gst_imx_blitter_compositor_get_type(void);


G_END_DECLS


#endif
