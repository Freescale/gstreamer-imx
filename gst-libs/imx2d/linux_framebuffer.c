#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "imx2d.h"
#include "imx2d_priv.h"
#include "linux_framebuffer.h"


struct _Imx2dLinuxFramebuffer
{
	int fd;

	ImxWrappedDmaBuffer dma_buffer;
	Imx2dSurface *surface;

	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	BOOL use_vsync;
	int current_fb_page;
	int old_fb_page;
};


static Imx2dPixelFormat imx_2d_linux_framebuffer_get_format_from_fb(struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix)
{
	Imx2dPixelFormat fmt = IMX_2D_PIXEL_FORMAT_UNKNOWN;
	unsigned int rlen = fb_var->red.length, glen = fb_var->green.length, blen = fb_var->blue.length, alen = fb_var->transp.length;
	unsigned int rofs = fb_var->red.offset, gofs = fb_var->green.offset, bofs = fb_var->blue.offset, aofs = fb_var->transp.offset;

	if (fb_fix->type != FB_TYPE_PACKED_PIXELS)
	{
		IMX_2D_LOG(DEBUG, "unknown framebuffer type %d", fb_fix->type);
		return fmt;
	}

	switch (fb_var->bits_per_pixel)
	{
		case 16:
		{
			if ((rlen == 5) && (glen == 6) && (blen == 5))
				fmt = IMX_2D_PIXEL_FORMAT_RGB565;
			break;
		}
		case 24:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16))
					fmt = IMX_2D_PIXEL_FORMAT_RGB888;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0))
					fmt = IMX_2D_PIXEL_FORMAT_BGR888;
			}
			break;
		}
		case 32:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8) && (alen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16) && (aofs == 24))
					fmt = IMX_2D_PIXEL_FORMAT_RGBA8888;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0) && (aofs == 24))
					fmt = IMX_2D_PIXEL_FORMAT_BGRA8888;
				else if ((rofs == 24) && (gofs == 16) && (bofs == 8) && (aofs == 0))
					fmt = IMX_2D_PIXEL_FORMAT_ABGR8888;
			}
			else if ((rlen == 8) && (glen == 8) && (blen == 8) && (alen == 0))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16))
					fmt = IMX_2D_PIXEL_FORMAT_RGBX8888;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0))
					fmt = IMX_2D_PIXEL_FORMAT_BGRX8888;
				else if ((rofs == 24) && (gofs == 16) && (bofs == 8))
					fmt = IMX_2D_PIXEL_FORMAT_XBGR8888;
			}
			break;
		}
		default:
			break;
	}

	IMX_2D_LOG(
		DEBUG,
		"framebuffer uses %u bpp (sizes: r %u g %u b %u a %u  offsets: r %u g %u b %u a %u) => format %s",
		fb_var->bits_per_pixel,
		rlen, glen, blen, alen,
		rofs, gofs, bofs, aofs,
		(fmt == IMX_2D_PIXEL_FORMAT_UNKNOWN) ? "<UNKNOWN>" : imx_2d_pixel_format_to_string(fmt)
	);

	return fmt;
}


Imx2dLinuxFramebuffer* imx_2d_linux_framebuffer_create(char const *device_name, int use_vsync)
{
	Imx2dLinuxFramebuffer *linux_framebuffer;
	Imx2dSurfaceDesc desc;

	assert(device_name != NULL);
	assert(device_name[0] != '\0');

	linux_framebuffer = malloc(sizeof(Imx2dLinuxFramebuffer));
	assert(linux_framebuffer != NULL);

	memset(linux_framebuffer, 0, sizeof(Imx2dLinuxFramebuffer));

	linux_framebuffer->fd = open(device_name, O_RDWR, 0);

	if (linux_framebuffer->fd < 0)
	{
		IMX_2D_LOG(ERROR, "could not open %s: %s (%d)", device_name, strerror(errno), errno);
		goto error;
	}

	if (ioctl(linux_framebuffer->fd, FBIOGET_FSCREENINFO, &(linux_framebuffer->fb_fix)) == -1)
	{
		IMX_2D_LOG(ERROR, "could not get fixed screen info: %s (%d)", device_name, strerror(errno), errno);
		goto error;
	}

	if (ioctl(linux_framebuffer->fd, FBIOGET_VSCREENINFO, &(linux_framebuffer->fb_var)) == -1)
	{
		IMX_2D_LOG(ERROR, "could not get variable screen info: %s (%d)", device_name, strerror(errno), errno);
		goto error;
	}

	memset(&desc, 0, sizeof(desc));
	desc.width = linux_framebuffer->fb_var.xres;
	desc.height = linux_framebuffer->fb_var.yres;
	desc.format = imx_2d_linux_framebuffer_get_format_from_fb(&(linux_framebuffer->fb_var), &(linux_framebuffer->fb_fix));

	if (desc.format == IMX_2D_PIXEL_FORMAT_UNKNOWN)
	{
		IMX_2D_LOG(ERROR, "unsupported framebuffer format");
		goto error;
	}

	desc.plane_stride[0] = linux_framebuffer->fb_fix.line_length;

	imx_dma_buffer_init_wrapped_buffer(&(linux_framebuffer->dma_buffer));
	linux_framebuffer->dma_buffer.fd = -1;
	linux_framebuffer->dma_buffer.physical_address = (imx_physical_address_t)(linux_framebuffer->fb_fix.smem_start);
	if (linux_framebuffer->dma_buffer.physical_address == 0)
	{
		IMX_2D_LOG(ERROR, "framebuffer physical address is not available");
		goto error;
	}

	IMX_2D_LOG(
		DEBUG,
		"framebuffer surface desc: width: %d height: %d stride: %d format: %s",
		desc.width, desc.height,
		desc.plane_stride[0],
		imx_2d_pixel_format_to_string(desc.format)
	);
	IMX_2D_LOG(DEBUG, "framebuffer physical address: %" IMX_PHYSICAL_ADDRESS_FORMAT, linux_framebuffer->dma_buffer.physical_address);

	linux_framebuffer->surface = imx_2d_surface_create((ImxDmaBuffer *)&(linux_framebuffer->dma_buffer), &desc);
	if (linux_framebuffer->surface == NULL)
	{
		IMX_2D_LOG(ERROR, "could not create framebuffer surface");
		goto error;
	}


finish:
	return linux_framebuffer;

error:
	if (linux_framebuffer != NULL)
	{
		if (linux_framebuffer->surface != NULL)
			imx_2d_surface_destroy(linux_framebuffer->surface);

		if (linux_framebuffer->fd > 0)
			close(linux_framebuffer->fd);

		free(linux_framebuffer);
		linux_framebuffer = NULL;
	}

	goto finish;
}


Imx2dSurface* imx_2d_linux_framebuffer_get_surface(Imx2dLinuxFramebuffer *linux_framebuffer)
{
	assert(linux_framebuffer != NULL);
	return linux_framebuffer->surface;
}


void imx_2d_linux_framebuffer_destroy(Imx2dLinuxFramebuffer *linux_framebuffer)
{
	if (linux_framebuffer == NULL)
		return;

	if (linux_framebuffer->fd > 0)
		close(linux_framebuffer->fd);

	free(linux_framebuffer);
}
