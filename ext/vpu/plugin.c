/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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

#include <config.h>
#include <gst/gst.h>
#include <imxvpuapi2/imxvpuapi2.h>
#include "gstimxvpudec.h"
#include "gstimxvpuench263.h"
#include "gstimxvpuench264.h"
#include "gstimxvpuencjpeg.h"
#include "gstimxvpuencmpeg4.h"
#include "gstimxvpuencvp8.h"
#include "gstimxvpucommon.h"


GST_DEBUG_CATEGORY(gst_imx_vpu_common_debug);


static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = TRUE;
	ImxVpuApiDecGlobalInfo const *dec_global_info = imx_vpu_api_dec_get_global_info();
	ImxVpuApiEncGlobalInfo const *enc_global_info = imx_vpu_api_enc_get_global_info();

	GST_DEBUG_CATEGORY_INIT(gst_imx_vpu_common_debug, "imxvpucommon", 0, "common code for the GStreamer i.MX elements");

	if (dec_global_info->flags & IMX_VPU_API_DEC_GLOBAL_INFO_FLAG_HAS_DECODER)
	{
		size_t i;
		for (i = 0; ret && (i < dec_global_info->num_supported_compression_formats); ++i)
		{
			ret = gst_imx_vpu_dec_register_decoder_type(plugin, dec_global_info->supported_compression_formats[i]);
		}
	}

	if (enc_global_info->flags & IMX_VPU_API_ENC_GLOBAL_INFO_FLAG_HAS_ENCODER)
	{
		size_t i;
		for (i = 0; ret && (i < enc_global_info->num_supported_compression_formats); ++i)
		{
			switch (enc_global_info->supported_compression_formats[i])
			{
				case IMX_VPU_API_COMPRESSION_FORMAT_H263:
					ret = gst_element_register(plugin, "imxvpuenc_h263", GST_RANK_PRIMARY + 1, gst_imx_vpu_enc_h263_get_type());
					break;

				case IMX_VPU_API_COMPRESSION_FORMAT_H264:
					ret = gst_element_register(plugin, "imxvpuenc_h264", GST_RANK_PRIMARY + 1, gst_imx_vpu_enc_h264_get_type());
					break;

				case IMX_VPU_API_COMPRESSION_FORMAT_JPEG:
					ret = gst_element_register(plugin, "imxvpuenc_jpeg", GST_RANK_PRIMARY + 1, gst_imx_vpu_enc_jpeg_get_type());
					break;

				case IMX_VPU_API_COMPRESSION_FORMAT_MPEG4:
					ret = gst_element_register(plugin, "imxvpuenc_mpeg4", GST_RANK_PRIMARY + 1, gst_imx_vpu_enc_mpeg4_get_type());
					break;

				case IMX_VPU_API_COMPRESSION_FORMAT_VP8:
					ret = gst_element_register(plugin, "imxvpuenc_vp8", GST_RANK_PRIMARY + 1, gst_imx_vpu_enc_vp8_get_type());
					break;

				default:
					break;
			}
		}
	}

	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	imxvpu,
	"video en- and decoder elements using the NXP i.MX VPU",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)
