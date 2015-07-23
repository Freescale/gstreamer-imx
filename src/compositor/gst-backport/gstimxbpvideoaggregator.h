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

#ifndef __GST_IMXBP_VIDEO_AGGREGATOR_H__
#define __GST_IMXBP_VIDEO_AGGREGATOR_H__

#if 0
#ifndef GST_USE_UNSTABLE_API
#warning "The Video library from gst-plugins-bad is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstimxbpaggregator.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_AGGREGATOR (gst_imxbp_videoaggregator_get_type())
#define GST_IMXBP_VIDEO_AGGREGATOR(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_AGGREGATOR, GstImxBPVideoAggregator))
#define GST_IMXBP_VIDEO_AGGREGATOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_AGGREGATOR, GstImxBPVideoAggregatorClass))
#define GST_IS_VIDEO_AGGREGATOR(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_AGGREGATOR))
#define GST_IS_VIDEO_AGGREGATOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_AGGREGATOR))
#define GST_IMXBP_VIDEO_AGGREGATOR_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VIDEO_AGGREGATOR,GstImxBPVideoAggregatorClass))

typedef struct _GstImxBPVideoAggregator GstImxBPVideoAggregator;
typedef struct _GstImxBPVideoAggregatorClass GstImxBPVideoAggregatorClass;
typedef struct _GstImxBPVideoAggregatorPrivate GstImxBPVideoAggregatorPrivate;

#include "gstimxbpvideoaggregatorpad.h"

/**
 * GstImxBPVideoAggregator:
 * @info: The #GstVideoInfo representing the currently set
 * srcpad caps.
 */
struct _GstImxBPVideoAggregator
{
  GstImxBPAggregator aggregator;

  /*< public >*/
  /* Output caps */
  GstVideoInfo info;

  /* < private > */
  GstImxBPVideoAggregatorPrivate *priv;
  gpointer          _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstImxBPVideoAggregatorClass:
 * @update_caps:              Optional.
 *                            Lets subclasses update the #GstCaps representing
 *                            the src pad caps before usage.  Return %NULL to indicate failure.
 * @aggregate_frames:         Lets subclasses aggregate frames that are ready. Subclasses
 *                            should iterate the GstElement.sinkpads and use the already
 *                            mapped #GstVideoFrame from GstImxBPVideoAggregatorPad.aggregated_frame
 *                            or directly use the #GstBuffer from GstImxBPVideoAggregatorPad.buffer
 *                            if it needs to map the buffer in a special way. The result of the
 *                            aggregation should land in @outbuffer.
 * @get_output_buffer:        Optional.
 *                            Lets subclasses provide a #GstBuffer to be used as @outbuffer of
 *                            the #aggregate_frames vmethod.
 * @negotiated_caps:          Optional.
 *                            Notifies subclasses what caps format has been negotiated
 * @find_best_format:         Optional.
 *                            Lets subclasses decide of the best common format to use.
 * @preserve_update_caps_result: Sub-classes should set this to true if the return result
 *                               of the update_caps() method should not be further modified
 *                               by GstImxBPVideoAggregator by removing fields.
 **/
struct _GstImxBPVideoAggregatorClass
{
  /*< private >*/
  GstImxBPAggregatorClass parent_class;

  /*< public >*/
  GstCaps *          (*update_caps)               (GstImxBPVideoAggregator *  videoaggregator,
                                                   GstCaps            *  caps);
  GstFlowReturn      (*aggregate_frames)          (GstImxBPVideoAggregator *  videoaggregator,
                                                   GstBuffer          *  outbuffer);
  GstFlowReturn      (*get_output_buffer)         (GstImxBPVideoAggregator *  videoaggregator,
                                                   GstBuffer          ** outbuffer);
  gboolean           (*negotiated_caps)           (GstImxBPVideoAggregator *  videoaggregator,
                                                   GstCaps            *  caps);
  void               (*find_best_format)          (GstImxBPVideoAggregator *  vagg,
                                                   GstCaps            *  downstream_caps,
                                                   GstVideoInfo       *  best_info,
                                                   gboolean           *  at_least_one_alpha);

  gboolean           preserve_update_caps_result;

  /* < private > */
  gpointer            _gst_reserved[GST_PADDING_LARGE];
};

GType gst_imxbp_videoaggregator_get_type       (void);

G_END_DECLS
#endif /* __GST_IMXBP_VIDEO_AGGREGATOR_H__ */
