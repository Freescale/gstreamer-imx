/* GStreamer aggregator base class
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@oencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
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

#ifndef __GST_IMXBP_AGGREGATOR_H__
#define __GST_IMXBP_AGGREGATOR_H__

#if 0
#ifndef GST_USE_UNSTABLE_API
#warning "The Base library from gst-plugins-bad is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

/**************************
 * GstImxBPAggregator Structs  *
 *************************/

typedef struct _GstImxBPAggregator GstImxBPAggregator;
typedef struct _GstImxBPAggregatorPrivate GstImxBPAggregatorPrivate;
typedef struct _GstImxBPAggregatorClass GstImxBPAggregatorClass;

/************************
 * GstImxBPAggregatorPad API *
 ***********************/

#define GST_TYPE_AGGREGATOR_PAD            (gst_imxbp_aggregator_pad_get_type())
#define GST_IMXBP_AGGREGATOR_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AGGREGATOR_PAD, GstImxBPAggregatorPad))
#define GST_IMXBP_AGGREGATOR_PAD_CAST(obj)       ((GstImxBPAggregatorPad *)(obj))
#define GST_IMXBP_AGGREGATOR_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AGGREGATOR_PAD, GstImxBPAggregatorPadClass))
#define GST_IMXBP_AGGREGATOR_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_AGGREGATOR_PAD, GstImxBPAggregatorPadClass))
#define GST_IS_AGGREGATOR_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AGGREGATOR_PAD))
#define GST_IS_AGGREGATOR_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AGGREGATOR_PAD))

/****************************
 * GstImxBPAggregatorPad Structs *
 ***************************/

typedef struct _GstImxBPAggregatorPad GstImxBPAggregatorPad;
typedef struct _GstImxBPAggregatorPadClass GstImxBPAggregatorPadClass;
typedef struct _GstImxBPAggregatorPadPrivate GstImxBPAggregatorPadPrivate;

/**
 * GstImxBPAggregatorPad:
 * @buffer: currently queued buffer.
 * @segment: last segment received.
 *
 * The implementation the GstPad to use with #GstImxBPAggregator
 */
struct _GstImxBPAggregatorPad
{
  GstPad                       parent;

  /* Protected by the OBJECT_LOCK */
  GstSegment segment;

  /* < Private > */
  GstImxBPAggregatorPadPrivate   *  priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstImxBPAggregatorPadClass:
 * @flush:    Optional
 *            Called when the pad has received a flush stop, this is the place
 *            to flush any information specific to the pad, it allows for individual
 *            pads to be flushed while others might not be.
 *
 */
struct _GstImxBPAggregatorPadClass
{
  GstPadClass   parent_class;

  /* NOTE: return value changed from GstFlowReturn to gboolean for gstreamer-imx
   * to avoid incorrect return values (bug in upstream?) */
  gboolean (*flush)     (GstImxBPAggregatorPad * aggpad, GstImxBPAggregator * aggregator);

  /*< private >*/
  gpointer      _gst_reserved[GST_PADDING_LARGE];
};

GType gst_imxbp_aggregator_pad_get_type           (void);

/****************************
 * GstImxBPAggregatorPad methods *
 ***************************/

GstBuffer * gst_imxbp_aggregator_pad_steal_buffer (GstImxBPAggregatorPad *  pad);
GstBuffer * gst_imxbp_aggregator_pad_get_buffer   (GstImxBPAggregatorPad *  pad);
gboolean    gst_imxbp_aggregator_pad_drop_buffer  (GstImxBPAggregatorPad *  pad);
gboolean    gst_imxbp_aggregator_pad_is_eos       (GstImxBPAggregatorPad *  pad);

/*********************
 * GstImxBPAggregator API *
 ********************/

#define GST_TYPE_AGGREGATOR            (gst_imxbp_aggregator_get_type())
#define GST_IMXBP_AGGREGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AGGREGATOR,GstImxBPAggregator))
#define GST_IMXBP_AGGREGATOR_CAST(obj)       ((GstImxBPAggregator *)(obj))
#define GST_IMXBP_AGGREGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AGGREGATOR,GstImxBPAggregatorClass))
#define GST_IMXBP_AGGREGATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_AGGREGATOR,GstImxBPAggregatorClass))
#define GST_IS_AGGREGATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AGGREGATOR))
#define GST_IS_AGGREGATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AGGREGATOR))

#define GST_FLOW_NOT_HANDLED           GST_FLOW_CUSTOM_SUCCESS

/**
 * GstImxBPAggregator:
 * @srcpad: the aggregator's source pad
 * @segment: the output segment
 *
 * Aggregator base class object structure.
 */
struct _GstImxBPAggregator
{
  GstElement               parent;

  GstPad                *  srcpad;

  /* Only access with the object lock held */
  GstSegment               segment;

  /*< private >*/
  GstImxBPAggregatorPrivate  *  priv;

  gpointer                 _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstImxBPAggregatorClass:
 * @sinkpads_type:  Optional.
 *                  The type of the pads that should be created when
 *                  GstElement.request_new_pad is called.
 * @flush:          Optional.
 *                  Called after a succesful flushing seek, once all the flush
 *                  stops have been received. Flush pad-specific data in
 *                  #GstImxBPAggregatorPad->flush.
 * @clip:           Optional.
 *                  Called when a buffer is received on a sink pad, the task
 *                  of clipping it and translating it to the current segment
 *                  falls on the subclass.
 * @sink_event:     Optional.
 *                  Called when an event is received on a sink pad, the subclass
 *                  should always chain up.
 * @sink_query:     Optional.
 *                  Called when a query is received on a sink pad, the subclass
 *                  should always chain up.
 * @src_event:      Optional.
 *                  Called when an event is received on the src pad, the subclass
 *                  should always chain up.
 * @src_query:      Optional.
 *                  Called when a query is received on the src pad, the subclass
 *                  should always chain up.
 * @src_activate:   Optional.
 *                  Called when the src pad is activated, it will start/stop its
 *                  pad task right after that call.
 * @aggregate:      Mandatory.
 *                  Called when buffers are queued on all sinkpads. Classes
 *                  should iterate the GstElement->sinkpads and peek or steal
 *                  buffers from the #GstImxBPAggregatorPads. If the subclass returns
 *                  GST_FLOW_EOS, sending of the eos event will be taken care
 *                  of. Once / if a buffer has been constructed from the
 *                  aggregated buffers, the subclass should call _finish_buffer.
 * @stop:           Optional.
 *                  Called when the element goes from PAUSED to READY.
 *                  The subclass should free all resources and reset its state.
 * @start:          Optional.
 *                  Called when the element goes from READY to PAUSED.
 *                  The subclass should get ready to process
 *                  aggregated buffers.
 * @get_next_time:  Optional.
 *                  Called when the element needs to know the time of the next
 *                  rendered buffer for live pipelines. This causes deadline
 *                  based aggregation to occur. Defaults to returning
 *                  GST_CLOCK_TIME_NONE causing the element to wait for buffers
 *                  on all sink pads before aggregating.
 *
 * The aggregator base class will handle in a thread-safe way all manners of
 * concurrent flushes, seeks, pad additions and removals, leaving to the
 * subclass the responsibility of clipping buffers, and aggregating buffers in
 * the way the implementor sees fit.
 *
 * It will also take care of event ordering (stream-start, segment, eos).
 *
 * Basically, a basic implementation will override @aggregate, and call
 * _finish_buffer from inside that function.
 */
struct _GstImxBPAggregatorClass {
  GstElementClass   parent_class;

  GType             sinkpads_type;

  GstFlowReturn     (*flush)          (GstImxBPAggregator    *  aggregator);

  GstFlowReturn     (*clip)           (GstImxBPAggregator    *  aggregator,
                                       GstImxBPAggregatorPad *  aggregator_pad,
                                       GstBuffer        *  buf,
                                       GstBuffer        ** outbuf);

  /* sinkpads virtual methods */
  gboolean          (*sink_event)     (GstImxBPAggregator    *  aggregator,
                                       GstImxBPAggregatorPad *  aggregator_pad,
                                       GstEvent         *  event);

  gboolean          (*sink_query)     (GstImxBPAggregator    *  aggregator,
                                       GstImxBPAggregatorPad *  aggregator_pad,
                                       GstQuery         *  query);

  /* srcpad virtual methods */
  gboolean          (*src_event)      (GstImxBPAggregator    *  aggregator,
                                       GstEvent         *  event);

  gboolean          (*src_query)      (GstImxBPAggregator    *  aggregator,
                                       GstQuery         *  query);

  gboolean          (*src_activate)   (GstImxBPAggregator    *  aggregator,
                                       GstPadMode          mode,
                                       gboolean            active);

  GstFlowReturn     (*aggregate)      (GstImxBPAggregator    *  aggregator,
                                       gboolean            timeout);

  gboolean          (*stop)           (GstImxBPAggregator    *  aggregator);

  gboolean          (*start)          (GstImxBPAggregator    *  aggregator);

  GstClockTime      (*get_next_time)  (GstImxBPAggregator    *  aggregator);

  /*< private >*/
  gpointer          _gst_reserved[GST_PADDING_LARGE];
};

/************************************
 * GstImxBPAggregator convenience macros *
 ***********************************/

/**
 * GST_IMXBP_AGGREGATOR_SRC_PAD:
 * @agg: a #GstImxBPAggregator
 *
 * Convenience macro to access the source pad of #GstImxBPAggregator
 *
 * Since: 1.6
 */
#define GST_IMXBP_AGGREGATOR_SRC_PAD(agg) (((GstImxBPAggregator *)(agg))->srcpad)

/*************************
 * GstImxBPAggregator methods *
 ************************/

GstFlowReturn  gst_imxbp_aggregator_finish_buffer         (GstImxBPAggregator                *  agg,
                                                     GstBuffer                    *  buffer);
void           gst_imxbp_aggregator_set_src_caps          (GstImxBPAggregator                *  agg,
                                                     GstCaps                      *  caps);

void           gst_imxbp_aggregator_set_latency           (GstImxBPAggregator                *  self,
                                                     GstClockTime                    min_latency,
                                                     GstClockTime                    max_latency);

GType gst_imxbp_aggregator_get_type(void);

/* API that should eventually land in GstElement itself (FIXME) */
typedef gboolean (*GstImxBPAggregatorPadForeachFunc)    (GstImxBPAggregator                 *  aggregator,
                                                    GstImxBPAggregatorPad              *  aggregator_pad,
                                                    gpointer                         user_data);

gboolean gst_imxbp_aggregator_iterate_sinkpads           (GstImxBPAggregator                 *  self,
                                                    GstImxBPAggregatorPadForeachFunc      func,
                                                    gpointer                         user_data);

GstClockTime  gst_imxbp_aggregator_get_latency           (GstImxBPAggregator                 *  self);

G_END_DECLS

#endif /* __GST_IMXBP_AGGREGATOR_H__ */
