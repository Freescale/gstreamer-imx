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

#ifndef GST_IMX_2D_VIDEO_OVERLAY_HANDLER_H
#define GST_IMX_2D_VIDEO_OVERLAY_HANDLER_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gst/imx/common/gstimxdmabufferuploader.h"
#include "imx2d/imx2d.h"


G_BEGIN_DECLS


/* The GstImx2dVideoOverlayHandler is an internal object used in
 * imx2d based GStreamer elements. Its purpose is to render overlays
 * with an imx2d blitter. Said overlays are specified by an instance
 * of GstVideoOverlayComposition, which is contained in GstBuffers
 * in a GstVideoOverlayCompositionMeta.
 *
 * GstVideoOverlayComposition contains a number of "rectangles" -
 * GstVideoOverlayRectangle instances that specify the individual
 * overlays. Each rectangle defines the overlay with a gstbuffer
 * (which contains the pixels), coordinates, and a global alpha
 * value. Since overlays often do not render at every frame, it
 * is useful to cache all that information. Such caching is one
 * of the tasks of GstImx2dVideoOverlayHandler. In particular, the
 * contents of these gstbuffers have to be uploaded into a form
 * that can be used with imx2d blitters. GstImxDmaBufferUploader
 * is used for this purpose. Uploaded versions of overlay gstbuffers
 * are kept in the cache. Also, the GstVideoOverlayComposition from
 * the meta is ref'd until either a new GstVideoOverlayComposition
 * is detected (in an incoming frame that has such a meta), or if
 * gst_imx_2d_video_overlay_handler_clear_cached_overlays() is called,
 * or until the GstImx2dVideoOverlayHandler instance is destroyed.
 * Ref'ing the GstVideoOverlayComposition makes sure that it cannot
 * be modified in-place by someone else, and also allows for checking
 * if incoming buffers contain the exact same composition. If so,
 * the cached data can be reused, otherwise the cache has to be
 * repopulated with the new composition's data.
 *
 * The overlays are drawn with gst_imx_2d_video_overlay_handler_render().
 * Note that imx_2d_blitter_start() must have been called before
 * that function can be used, since it does not start an imx2d
 * blitter operation sequence on its own. (This is intentional;
 * it allows for combining operations from somewhere else with
 * blitter operations performed by that function without having
 * to start/finish multiple sequences.)
 */


#define GST_TYPE_IMX_2D_VIDEO_OVERLAY_HANDLER             (gst_imx_2d_video_overlay_handler_get_type())
#define GST_IMX_2D_VIDEO_OVERLAY_HANDLER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_2D_VIDEO_OVERLAY_HANDLER, GstImx2dVideoOverlayHandler))
#define GST_IMX_2D_VIDEO_OVERLAY_HANDLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_2D_VIDEO_OVERLAY_HANDLER, GstImx2dVideoOverlayHandlerClass))
#define GST_IMX_2D_VIDEO_OVERLAY_HANDLER_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_2D_VIDEO_OVERLAY_HANDLER, GstImx2dVideoOverlayHandlerClass))
#define GST_IMX_2D_VIDEO_OVERLAY_HANDLER_CAST(obj)        ((GstImx2dVideoOverlayHandler *)(obj))
#define GST_IS_IMX_2D_VIDEO_OVERLAY_HANDLER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_2D_VIDEO_OVERLAY_HANDLER))
#define GST_IS_IMX_2D_VIDEO_OVERLAY_HANDLER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_2D_VIDEO_OVERLAY_HANDLER))


typedef struct _GstImx2dVideoOverlayHandler GstImx2dVideoOverlayHandler;
typedef struct _GstImx2dVideoOverlayHandlerClass GstImx2dVideoOverlayHandlerClass;


GstImx2dVideoOverlayHandler* gst_imx_2d_video_overlay_handler_new(GstImxDmaBufferUploader *uploader, Imx2dBlitter *blitter);

void gst_imx_2d_video_overlay_handler_clear_cached_overlays(GstImx2dVideoOverlayHandler *video_overlay_handler);

gboolean gst_imx_2d_video_overlay_handler_render(GstImx2dVideoOverlayHandler *video_overlay_handler, GstBuffer *buffer);


G_END_DECLS


#endif /* GST_IMX_2D_VIDEO_OVERLAY_HANDLER_H */
