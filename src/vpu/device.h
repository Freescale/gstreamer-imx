/* Common load/unload functions for the Freescale VPU device
 * Copyright (C) 2015  Carlos Rafael Giani
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


#ifndef GST_IMX_VPU_DEVICE_H
#define GST_IMX_VPU_DEVICE_H

#include <gst/gst.h>


G_BEGIN_DECLS


/* Loads the decoder's firmware.
 *
 * This needs to be called at least once per process prior to any
 * decoding operations.
 * Repeated calls will not re-load the firmware, but they will increase
 * an internal imxvpuapi reference counter, meaning that if N
 * @gst_imx_vpu_decoder_load calls are made, then @gst_imx_vpu_decoder_unload
 * must also be called N times.
 *
 * See @imx_vpu_dec_load for more.
 *
 * This function is internally protected by mutexes and is thus thread safe.
 *
 * Returns FALSE if something went wrong during loading. The reference
 * counter will not be increased then.
 */
gboolean gst_imx_vpu_decoder_load();
/* Unloads the decoder.
 *
 * This needs to be called for every time @gst_imx_vpu_decoder_load
 * was called. Afterwards, decoding will not be possible until the
 * decoder is loaded again with a @gst_imx_vpu_decoder_load call.
 *
 * This function is internally protected by mutexes and is thus thread safe.
 */
void gst_imx_vpu_decoder_unload();

/* Loads the encoder's firmware.
 *
 * This needs to be called at least once per process prior to any
 * encoding operations.
 * Repeated calls will not re-load the firmware, but they will increase
 * an internal imxvpuapi reference counter, meaning that if N
 * @gst_imx_vpu_encoder_load calls are made, then @gst_imx_vpu_encoder_unload
 * must also be called N times.
 *
 * See @imx_vpu_enc_load for more.
 *
 * This function is internally protected by mutexes and is thus thread safe.
 *
 * Returns FALSE if something went wrong during loading. The reference
 * counter will not be increased then.
 */
gboolean gst_imx_vpu_encoder_load();
/* Unloads the encoder.
 *
 * This needs to be called for every time @gst_imx_vpu_encoder_load
 * was called. Afterwards, encoding will not be possible until the
 * encoder is loaded again with a @gst_imx_vpu_encoder_load call.
 *
 * This function is internally protected by mutexes and is thus thread safe.
 */
void gst_imx_vpu_encoder_unload();

/* Connects the imxvpuapi logger to the GStreamer logging interface.
 *
 * This only needs to be called once per process. Unlike the load/unload
 * functions, no reference counter is used. After logging is set up,
 * calling this function again does nothing.
 * This function is internally protected by mutexes and is thus thread safe.
 */
void imx_vpu_setup_logging(void);


G_END_DECLS


#endif
