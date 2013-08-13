/* GStreamer video decoder using the Freescale VPU hardware video engine
 * Copyright (C) 2013  Carlos Rafael Giani
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


#include <gst/gst.h>
#include "config.h"
#include "vpu_decoder.h"
#include "../common/vpu_utils.h"



static gboolean plugin_init(GstPlugin *plugin)
{
	gst_fsl_vpu_init_alloc_debug();

	gboolean ret = TRUE;
	ret = ret && gst_element_register(plugin, "fslvpudec", GST_RANK_PRIMARY + 1, gst_fsl_vpu_dec_get_type());
	return ret;
}



GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	fslvpudec,
	"hardware-accelerated video decoding using the Freescale VPU engine",
	plugin_init,
	VERSION,
	"LGPL",
	GST_PACKAGE_NAME,
	GST_PACKAGE_ORIGIN
)

