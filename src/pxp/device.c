/* Common functions for the Freescale IPU device
 * Copyright (C) 2014  Carlos Rafael Giani
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


#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pxp_lib.h>
#include "device.h"


GST_DEBUG_CATEGORY_EXTERN(imx_pxp_device_debug);
#define GST_CAT_DEFAULT imx_pxp_device_debug


/* The IPU device is opened/closed globally.
 * While it could be opened in each GstImxIpuBlitter instance, bugs in the
 * IPU kernel driver make it preferable to open/close the IPU device just once,
 * globally, for all blitter instances.
 *
 * The IPU is opened/closed for each blitter instance, and for each IPU allocator.
 * The latter, to make sure the IPU FD is not closed before all blitter instances
 * *and* all allocators (and therefore all IPU-allocated DMA buffer blocks) are
 * finalized.
 */


static GMutex inst_counter_mutex;
static int inst_counter = 0;



gboolean gst_imx_pxp_init(void)
{
	g_mutex_lock(&inst_counter_mutex);
	if (inst_counter == 0)
	{
		if (pxp_init() != 0)
		{
			GST_ERROR("could not initialize PxP: %s", strerror(errno));
			g_mutex_unlock(&inst_counter_mutex);
			return FALSE;
		}
		else
			GST_INFO("PxP initialized");
	}
	++inst_counter;
	g_mutex_unlock(&inst_counter_mutex);

	return TRUE;
}


void gst_imx_pxp_shutdown(void)
{
	g_mutex_lock(&inst_counter_mutex);
	if (inst_counter > 0)
	{
		--inst_counter;
		if (inst_counter == 0)
		{
			pxp_uninit();
			GST_INFO("PxP shut down");
		}
	}
	g_mutex_unlock(&inst_counter_mutex);
}
