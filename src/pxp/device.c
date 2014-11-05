/* Common functions for the Freescale PxP device
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
#include <linux/pxp_device.h>
#include "device.h"


GST_DEBUG_CATEGORY_EXTERN(imx_pxp_device_debug);
#define GST_CAT_DEFAULT imx_pxp_device_debug


static GMutex inst_counter_mutex;
static int inst_counter = 0;
static int pxp_fd = -1;



gboolean gst_imx_pxp_open(void)
{
	g_mutex_lock(&inst_counter_mutex);
	if (inst_counter == 0)
	{
		g_assert(pxp_fd == -1);
		pxp_fd = open("/dev/pxp_device", O_RDWR, 0);
		if (pxp_fd < 0)
		{
			GST_ERROR("could not open /dev/pxp_device: %s", strerror(errno));
			return FALSE;
		}

		GST_INFO("PxP device opened");
	}
	++inst_counter;
	g_mutex_unlock(&inst_counter_mutex);

	return TRUE;
}


void gst_imx_pxp_close(void)
{
	g_mutex_lock(&inst_counter_mutex);
	if (inst_counter > 0)
	{
		--inst_counter;
		if (inst_counter == 0)
		{
			g_assert(pxp_fd != -1);
			close(pxp_fd);
			pxp_fd = -1;

			GST_INFO("PxP device closed");
		}
	}
	g_mutex_unlock(&inst_counter_mutex);
}


int gst_imx_pxp_get_fd(void)
{
	return pxp_fd;
}
