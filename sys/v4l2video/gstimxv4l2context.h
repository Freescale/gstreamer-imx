/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2021  Carlos Rafael Giani
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

#ifndef GST_IMX_V4L2_CONTEXT_H
#define GST_IMX_V4L2_CONTEXT_H

#include <gst/gst.h>
#include "gstimxv4l2videoformat.h"


G_BEGIN_DECLS


#define GST_TYPE_IMX_V4L2_CONTEXT             (gst_imx_v4l2_context_get_type())
#define GST_IMX_V4L2_CONTEXT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_V4L2_CONTEXT, GstImxV4L2Context))
#define GST_IMX_V4L2_CONTEXT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_V4L2_CONTEXT, GstImxV4L2ContextClass))
#define GST_IMX_V4L2_CONTEXT_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_V4L2_CONTEXT, GstImxV4L2ContextClass))
#define GST_IMX_V4L2_CONTEXT_CAST(obj)        ((GstImxV4L2Context *)(obj))
#define GST_IS_IMX_V4L2_CONTEXT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_V4L2_CONTEXT))
#define GST_IS_IMX_V4L2_CONTEXT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_V4L2_CONTEXT))


/**
 * GstImxV4L2DeviceType:
 * @GST_IMX_V4L2_DEVICE_TYPE_CAPTURE: The device captures frame data.
 * @GST_IMX_V4L2_DEVICE_TYPE_OUTPUT: The device outputs frame data.
 *
 * A V4L2 device's type. Used by @gst_imx_v4l2_context_new.
 */
typedef enum
{
	GST_IMX_V4L2_DEVICE_TYPE_CAPTURE,
	GST_IMX_V4L2_DEVICE_TYPE_OUTPUT
}
GstImxV4L2DeviceType;


/**
 * GstImxV4L2CaptureChip:
 * @GST_IMX_V4L2_CAPTURE_CHIP_UNIDENTIFIED: Capture chip could not be identified.
 * @GST_IMX_V4L2_CAPTURE_CHIP_UNRECOGNIZED_MXC_V4L2_BASED: Capture chip could not
 *     be identified, but it is mxc_v4l2 based, so mxc_v4l2 workarounds need to
 *     be applied when capturing frames.
 * @GST_IMX_V4L2_CAPTURE_CHIP_OV5640: Chip is an OmniVision OV5640.
 * @GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI: Chip is an OmniVision OV5640 via MIPI.
 * @GST_IMX_V4L2_CAPTURE_CHIP_OV5647: Chip is an OmniVision OV5647.
 * @GST_IMX_V4L2_CAPTURE_CHIP_TW6869: Chip is an Intersil TW6869.
 * @GST_IMX_V4L2_CAPTURE_CHIP_ADV7180: Chip is an Analog Devices ADV7180.
 *
 * These identifiers are needed when using the NXP i.MX6 V4L2 capture drivers.
 * This is because these drivers are severely broken and do not support format
 * and resolution enumerations, and also do not support buffer sharing mechanisms
 * like DMA-BUF, requiring driver specific hacks to associate V4L2 buffers with
 * physical addresses.
 */
typedef enum
{
	GST_IMX_V4L2_CAPTURE_CHIP_UNIDENTIFIED,
	GST_IMX_V4L2_CAPTURE_CHIP_UNRECOGNIZED_MXC_V4L2_BASED,
	GST_IMX_V4L2_CAPTURE_CHIP_OV5640,
	GST_IMX_V4L2_CAPTURE_CHIP_OV5640_MIPI,
	GST_IMX_V4L2_CAPTURE_CHIP_OV5647,
	GST_IMX_V4L2_CAPTURE_CHIP_TW6869,
	GST_IMX_V4L2_CAPTURE_CHIP_ADV7180
}
GstImxV4L2CaptureChip;


/**
 * GstImxV4L2EnumeratedFrameSize:
 * @width: Enumerated frame width, in pixels.
 * @height: Enumerated frame height, in pixels.
 *
 * Contains one enumerated frame size. See @gst_imx_v4l2_context_probe_device
 * and @GstImxV4L2ProbeResult.
 */
typedef struct _GstImxV4L2EnumeratedFrameSize GstImxV4L2EnumeratedFrameSize;

/**
 * GstImxV4L2ProbeResult:
 * @device_caps: @GstCaps containing all probed caps.
 * @capture_chip: What chip type the probing detected.
 * @v4l2_device_capabilities: Probed imxv4l2 device capabilities.
 * @chip_specific_frame_sizes: Chip-specific Frame sizes the imxv4l2
 *     device supports. This array is used with mxc_v4l2 devices to
 *     set a resolution specific value in v4l2_captureparm's capturemode
 *     field. (This is non-standard, i.MX specific behavior.)
 * @num_chip_specific_frame_sizes: Number of entries in the
 *     chip_specific_frame_sizes array.
 * @enumerated_v4l2_formats: @GList containing all formats
 *     the imxv4l2 device supports. The formats are available as
 *     const @GstImxV4L2VideoFormat pointers that refer to an internal
 *     format table, so do not try to deallocate them.
 */
typedef struct _GstImxV4L2ProbeResult GstImxV4L2ProbeResult;

/**
 * GstImxV4L2Context:
 *
 * This context object contains general, reusable information about an imxv4l2
 * device that can be (re)used by @GstImxV4L2Object instances. It allows for
 * probing an imxv4l2 device and store the probe results, and also stores the
 * number of buffers a V4L2 queue shall hold.
 *
 * This class is necessary because a imxv4l2 device may have to be reopened if
 * for example caps are renegotiated. (It is not possible to reconfigure
 * a V4L2 session once it started.) To not have to probe and store the probe
 * result etc. every time the device is reopenened, this context object is
 * used instead.
 *
 * It is created with @gst_imx_v4l2_context_new. Initially, it does not hold
 * any data. Use the @gst_imx_v4l2_context_set_device_node function to associate
 * it with a specific imxv4l2 device, @gst_imx_v4l2_context_probe_device to probe
 * the device, and @gst_imx_v4l2_context_set_num_buffers to set the number of
 * buffers the queue in @GstImxV4L2Object instances shall hold. These functions
 * only need to be run once after creating the context object. In particular,
 * probing should be done only once. To get the result of the probing process,
 * use @gst_imx_v4l2_context_get_probe_result.
 */
typedef struct _GstImxV4L2Context GstImxV4L2Context;
typedef struct _GstImxV4L2ContextClass GstImxV4L2ContextClass;


struct _GstImxV4L2EnumeratedFrameSize
{
	gint width, height;
};


struct _GstImxV4L2ProbeResult
{
	GstCaps *device_caps;
	GstImxV4L2CaptureChip capture_chip;
	guint32 v4l2_device_capabilities;
	GstImxV4L2EnumeratedFrameSize *chip_specific_frame_sizes;
	gint num_chip_specific_frame_sizes;
	GList *enumerated_v4l2_formats;
};


GType gst_imx_v4l2_context_get_type(void);


/**
 * gst_imx_v4l2_context_new:
 * @device_type: @GstImxV4L2DeviceType to use for this context.
 *
 * Creates a new empty @GstImxV4L2Context of the specified type.
 *
 * "Empty" means that no probing data and no buffer count is present.
 *
 * Returns: Pointer to a newly created context.
 */
GstImxV4L2Context* gst_imx_v4l2_context_new(GstImxV4L2DeviceType device_type);

/**
 * gst_imx_v4l2_context_get_device_type:
 * @imx_v4l2_context: @GstImxV4L2Context to get the type of.
 *
 * Returns: @GstImxV4L2DeviceType that was passed to @gst_imx_v4l2_context_new.
 */
GstImxV4L2DeviceType gst_imx_v4l2_context_get_device_type(GstImxV4L2Context const *imx_v4l2_context);

/**
 * gst_imx_v4l2_context_set_device_node:
 * @imx_v4l2_context: @GstImxV4L2Context to assign the device node string to.
 * @device_node: String with the device to assign.
 *
 * Stores a copy of a string referring to a imxv4l2 device node, like
 * "/dev/video0". Any previously assigned string is replaced.
 */
void gst_imx_v4l2_context_set_device_node(GstImxV4L2Context *imx_v4l2_context, gchar const *device_node);

/**
 * gst_imx_v4l2_context_get_device_node:
 * @imx_v4l2_context: @GstImxV4L2Context to get the device node string of.
 *
 * Returns: String with the associated device node. The string is owned by the context.
 */
gchar const *gst_imx_v4l2_context_get_device_node(GstImxV4L2Context const *imx_v4l2_context);

/**
 * gst_imx_v4l2_context_set_num_buffers:
 * @imx_v4l2_context: @GstImxV4L2Context to set number of buffers of.
 * @num_buffers: Number of buffers to use. Must be at least 2.
 *
 * Sets the number of buffers that shall be used in V4L2 capture/output queues.
 */
void gst_imx_v4l2_context_set_num_buffers(GstImxV4L2Context *imx_v4l2_context, gint num_buffers);

/**
 * gst_imx_v4l2_context_get_num_buffers:
 * @imx_v4l2_context: @GstImxV4L2Context to get the number of buffers of.
 *
 * Returns: Configured number of buffers to use in V4L2 capture/output queues.
 */
gint gst_imx_v4l2_context_get_num_buffers(GstImxV4L2Context const *imx_v4l2_context);

/**
 * gst_imx_v4l2_context_probe_device:
 * @imx_v4l2_context: @GstImxV4L2Context to fill with probed data.
 *
 * Probes the previously specified imxv4l2 device for the frame sizes, frame rates,
 * and video formats it supports. In addition, it also tries to probe what capture
 * chip is used (ignored if this is an output device).
 *
 * The device node must have been set with @gst_imx_v4l2_context_set_device_node
 * before calling this.
 *
 * See the @GstImxV4L2ProbeResult structure for details about the probed data.
 *
 * To get the result of the probing, use @gst_imx_v4l2_context_get_probe_result.
 *
 * Returns: TRUE if probing was successful, FALSE otherwise.
 */
gboolean gst_imx_v4l2_context_probe_device(GstImxV4L2Context *imx_v4l2_context);

/**
 * gst_imx_v4l2_context_get_probe_result:
 * @imx_v4l2_context: @GstImxV4L2Context to get probed data from.
 *
 * Retrieves a pointer to the @GstImxV4L2ProbeResult that contains the outcome
 * of a successful @gst_imx_v4l2_context_probe_device call. If that call returned
 * FALSE, or if it was not called, the return value is NULL.
 *
 * Returns: Pointer to internal @GstImxV4L2ProbeResult instance with the result
 *     of the probing. This instance is owned by the context; do not try to call
 *     @gst_imx_v4l2_clear_probe_result on it. NULL if no successful probing
 *     was done yet.
 */
GstImxV4L2ProbeResult const *gst_imx_v4l2_context_get_probe_result(GstImxV4L2Context const *imx_v4l2_context);

/**
 * gst_imx_v4l2_context_open_fd:
 * @imx_v4l2_context: @GstImxV4L2Context to use for opening an imxv4l2 device file descriptor.
 *
 * Convenience function to open a Unix file descriptor for the device node that
 * was previously specified by using @gst_imx_v4l2_context_set_device_node.
 * This function performs some safety checks to verify that the device node is OK.
 *
 * Returns: The file descriptor, or -1 if opening the device failed.
 */
int gst_imx_v4l2_context_open_fd(GstImxV4L2Context *imx_v4l2_context);

/**
 * gst_imx_v4l2_copy_probe_result:
 * @dest: @GstImxV4L2ProbeResult instance to copy data to.
 * @src: @GstImxV4L2ProbeResult instance to copy data from.
 *
 * Copies a probe result. This is useful if the probe result shall be retained
 * even after a context object was deallocated for example.
 *
 * Use @gst_imx_v4l2_clear_probe_result to clear the copy if it is not needed anymore.
 */
void gst_imx_v4l2_copy_probe_result(GstImxV4L2ProbeResult *dest, GstImxV4L2ProbeResult const *src);

/**
 * gst_imx_v4l2_clear_probe_result:
 * @probe_result: @GstImxV4L2ProbeResult to clear.
 *
 * Clears the @GstImxV4L2ProbeResult by deallocating all of its arrays and lists
 * and setting them to NULL.
 */
void gst_imx_v4l2_clear_probe_result(GstImxV4L2ProbeResult *probe_result);

/**
 * gst_imx_v4l2_get_by_gst_video_format_from_probe_result:
 * @probe_result @GstImxV4L2ProbeResult to search in.
 * @gst_format @GstVideoFormat to look for in a probe result.
 *
 * Looks for the first @GstImxV4L2VideoFormat entry in the
 * enumerated_v4l2_formats array in the given probe result
 * that matches the given gst_format.
 *
 * Returns: Pointer to the first match, or NULL if there is no match.
 */
GstImxV4L2VideoFormat const * gst_imx_v4l2_get_by_gst_video_format_from_probe_result(GstImxV4L2ProbeResult const *probe_result, GstVideoFormat gst_format);


G_END_DECLS


#endif /* GST_IMX_V4L2_OBJECT_H */
