/* Generic video aggregator plugin
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_IMXBP_VIDEO_AGGREGATOR_PAD_H__
#define __GST_IMXBP_VIDEO_AGGREGATOR_PAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstimxbpaggregator.h"
#include "gstimxbpvideoaggregator.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_AGGREGATOR_PAD (gst_imxbp_videoaggregator_pad_get_type())
#define GST_IMXBP_VIDEO_AGGREGATOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD, GstImxBPVideoAggregatorPad))
#define GST_IMXBP_VIDEO_AGGREGATOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_AGGREGATOR_PAD, GstImxBPVideoAggregatorPadClass))
#define GST_IS_VIDEO_AGGREGATOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD))
#define GST_IS_VIDEO_AGGREGATOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_AGGREGATOR_PAD))
#define GST_IMXBP_VIDEO_AGGREGATOR_PAD_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD,GstImxBPVideoAggregatorPadClass))

typedef struct _GstImxBPVideoAggregatorPad GstImxBPVideoAggregatorPad;
typedef struct _GstImxBPVideoAggregatorPadClass GstImxBPVideoAggregatorPadClass;
typedef struct _GstImxBPVideoAggregatorPadPrivate GstImxBPVideoAggregatorPadPrivate;

/**
 * GstImxBPVideoAggregatorPad:
 * @info: The #GstVideoInfo currently set on the pad
 * @buffer_vinfo: The #GstVideoInfo representing the type contained
 *                in @buffer
 * @aggregated_frame: The #GstVideoFrame ready to be used for aggregation
 *                    inside the aggregate_frames vmethod.
 * @zorder: The zorder of this pad
 */
struct _GstImxBPVideoAggregatorPad
{
  GstImxBPAggregatorPad parent;

  GstVideoInfo info;

  GstBuffer *buffer;
  /* The caps on the pad may not match the buffer above because of two reasons:
   * 1) When caps change, the info above will get updated, but the buffer might
   *    not since it might be pending on the GstImxBPAggregatorPad
   * 2) We might reject the new buffer in fill_queues() and reuse a previous
   *    buffer which has older GstVideoInfo
   * Hence, we need to maintain a GstVideoInfo for mapping buffers separately */
  GstVideoInfo buffer_vinfo;

  GstVideoFrame *aggregated_frame;

  /* properties */
  guint zorder;
  gboolean ignore_eos;

  /* < private > */
  GstImxBPVideoAggregatorPadPrivate *priv;
  gpointer          _gst_reserved[GST_PADDING];
};

/**
 * GstImxBPVideoAggregatorPadClass:
 *
 * @set_info: Lets subclass set a converter on the pad,
 *                 right after a new format has been negotiated.
 * @prepare_frame: Prepare the frame from the pad buffer (if any)
 *                 and sets it to @aggregated_frame
 * @clean_frame:   clean the frame previously prepared in prepare_frame
 */
struct _GstImxBPVideoAggregatorPadClass
{
  GstImxBPAggregatorPadClass parent_class;
  gboolean           (*set_info)              (GstImxBPVideoAggregatorPad * pad,
                                               GstImxBPVideoAggregator    * videoaggregator,
                                               GstVideoInfo          * current_info,
                                               GstVideoInfo          * wanted_info);

  gboolean           (*prepare_frame)         (GstImxBPVideoAggregatorPad * pad,
                                               GstImxBPVideoAggregator    * videoaggregator);

  void               (*clean_frame)           (GstImxBPVideoAggregatorPad * pad,
                                               GstImxBPVideoAggregator    * videoaggregator);

  gpointer          _gst_reserved[GST_PADDING_LARGE];
};

GType gst_imxbp_videoaggregator_pad_get_type (void);

G_END_DECLS
#endif /* __GST_IMXBP_VIDEO_AGGREGATOR_PAD_H__ */
