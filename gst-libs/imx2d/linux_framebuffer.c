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

	imx_physical_address_t basic_physical_address;

	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	BOOL enable_page_flipping;

	int current_fb_virt_height;
	int original_fb_virt_height;

	int page_size_in_bytes;
};


/* The i.MX MXC framebuffer driver contains a hard-coded
 * assumption that either one or three pages are used.
 * If we want to use page flipping, we have to use 3 pages,
 * even though 2 would be enough in theory. */
#define NUM_PAGE_FLIPPING_PAGES 3


static Imx2dPixelFormat imx_2d_linux_framebuffer_get_format_from_fb(struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix);
static BOOL imx_2d_linux_framebuffer_set_virtual_fb_height(Imx2dLinuxFramebuffer *linux_framebuffer, int virtual_fb_height);
static BOOL imx_2d_linux_framebuffer_restore_original_fb_height(Imx2dLinuxFramebuffer *linux_framebuffer);


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


static BOOL imx_2d_linux_framebuffer_set_virtual_fb_height(Imx2dLinuxFramebuffer *linux_framebuffer, int virtual_fb_height)
{
	linux_framebuffer->fb_var.yres_virtual = virtual_fb_height;
	linux_framebuffer->current_fb_virt_height = virtual_fb_height;

	if (ioctl(linux_framebuffer->fd, FBIOPUT_VSCREENINFO, &(linux_framebuffer->fb_var)) == -1)
	{
		IMX_2D_LOG(ERROR, "could not set variable screen info with updated virtual y resolution: %s (%d)", strerror(errno), errno);
		return FALSE;
	}
	else
		return TRUE;
}


static BOOL imx_2d_linux_framebuffer_restore_original_fb_height(Imx2dLinuxFramebuffer *linux_framebuffer)
{
	if (linux_framebuffer->fd < 0)
		return TRUE;

	IMX_2D_LOG(DEBUG, "resetting framebuffer display Y offset to 0 and physical address for writing back to basic physical address");
	imx_2d_linux_framebuffer_set_write_fb_page(linux_framebuffer, 0);
	imx_2d_linux_framebuffer_set_display_fb_page(linux_framebuffer, 0);


	if (linux_framebuffer->current_fb_virt_height == linux_framebuffer->original_fb_virt_height)
	{
		IMX_2D_LOG(DEBUG, "virtual height of framebuffer already set to its original value %d ; no need to reconfigure", linux_framebuffer->original_fb_virt_height);
		return TRUE;
	}

	IMX_2D_LOG(INFO, "restoring configuration: virtual height %u", linux_framebuffer->original_fb_virt_height);

	return imx_2d_linux_framebuffer_set_virtual_fb_height(linux_framebuffer, linux_framebuffer->original_fb_virt_height);
}


Imx2dLinuxFramebuffer* imx_2d_linux_framebuffer_create(char const *device_name, int enable_page_flipping)
{
	Imx2dLinuxFramebuffer *linux_framebuffer;
	Imx2dSurfaceDesc desc;

	assert(device_name != NULL);
	assert(device_name[0] != '\0');

	linux_framebuffer = malloc(sizeof(Imx2dLinuxFramebuffer));
	assert(linux_framebuffer != NULL);

	memset(linux_framebuffer, 0, sizeof(Imx2dLinuxFramebuffer));

	linux_framebuffer->fd = open(device_name, O_RDWR, 0);
	linux_framebuffer->enable_page_flipping = enable_page_flipping;

	if (linux_framebuffer->fd < 0)
	{
		IMX_2D_LOG(ERROR, "could not open framebuffer device \"%s\": %s (%d)", device_name, strerror(errno), errno);
		goto error;
	}

	if (ioctl(linux_framebuffer->fd, FBIOGET_FSCREENINFO, &(linux_framebuffer->fb_fix)) == -1)
	{
		IMX_2D_LOG(ERROR, "could not get fixed screen info: %s (%d)", strerror(errno), errno);
		goto error;
	}

	if (ioctl(linux_framebuffer->fd, FBIOGET_VSCREENINFO, &(linux_framebuffer->fb_var)) == -1)
	{
		IMX_2D_LOG(ERROR, "could not get variable screen info: %s (%d)", strerror(errno), errno);
		goto error;
	}

	/* Store the virtual FB height that is currently present
	 * so we can restore it later when the application exits. */
	linux_framebuffer->original_fb_virt_height = linux_framebuffer->current_fb_virt_height = linux_framebuffer->fb_var.yres_virtual;

	memset(&desc, 0, sizeof(desc));
	desc.width = linux_framebuffer->fb_var.xres;
	desc.height = linux_framebuffer->fb_var.yres;
	desc.format = imx_2d_linux_framebuffer_get_format_from_fb(&(linux_framebuffer->fb_var), &(linux_framebuffer->fb_fix));

	if (desc.format == IMX_2D_PIXEL_FORMAT_UNKNOWN)
	{
		IMX_2D_LOG(ERROR, "unsupported framebuffer format");
		goto error;
	}

	desc.plane_strides[0] = linux_framebuffer->fb_fix.line_length;

	IMX_2D_LOG(INFO, "page flipping enabled: %d", enable_page_flipping);

	if (enable_page_flipping)
	{
		unsigned int min_required_virtual_height = linux_framebuffer->fb_var.yres * NUM_PAGE_FLIPPING_PAGES;

		if (linux_framebuffer->fb_var.yres_virtual < min_required_virtual_height)
		{
			IMX_2D_LOG(
				INFO,
				"min required virtual framebuffer height for %d pages: %d  current height: %u  => not enough room for pages; reconfiguring framebuffer",
				NUM_PAGE_FLIPPING_PAGES,
				min_required_virtual_height,
				linux_framebuffer->fb_var.yres_virtual
			);

			if (!imx_2d_linux_framebuffer_set_virtual_fb_height(linux_framebuffer, min_required_virtual_height))
			{
				IMX_2D_LOG(ERROR, "could not reconfigure framebuffer virtual height");
				goto error;
			}
		}
		else
		{
			IMX_2D_LOG(
				INFO,
				"min required virtual framebuffer height for %d pages: %d  current height: %u  => enough room for pages; no need to reconfigure framebuffer",
				NUM_PAGE_FLIPPING_PAGES,
				min_required_virtual_height,
				linux_framebuffer->fb_var.yres_virtual
			);
		}
	}

	linux_framebuffer->page_size_in_bytes = desc.plane_strides[0] * desc.height;

	/* Store the "basic" physical address to the framebuffer.
	 * We need this to be able to later pick which page to
	 * write to, since that is accomplished simply by writing
	 * at a certain offset in the framebuffer. */
	linux_framebuffer->basic_physical_address = (imx_physical_address_t)(linux_framebuffer->fb_fix.smem_start);
	if (linux_framebuffer->basic_physical_address == 0)
	{
		IMX_2D_LOG(ERROR, "framebuffer physical address is not available");
		goto error;
	}

	imx_dma_buffer_init_wrapped_buffer(&(linux_framebuffer->dma_buffer));
	linux_framebuffer->dma_buffer.fd = -1;
	linux_framebuffer->dma_buffer.physical_address = linux_framebuffer->basic_physical_address;

	IMX_2D_LOG(
		DEBUG,
		"framebuffer surface desc: width: %d height: %d stride: %d format: %s",
		desc.width, desc.height,
		desc.plane_strides[0],
		imx_2d_pixel_format_to_string(desc.format)
	);
	IMX_2D_LOG(DEBUG, "framebuffer physical address: %" IMX_PHYSICAL_ADDRESS_FORMAT, linux_framebuffer->dma_buffer.physical_address);

	linux_framebuffer->surface = imx_2d_surface_create(&desc);
	if (linux_framebuffer->surface == NULL)
	{
		IMX_2D_LOG(ERROR, "could not create framebuffer surface");
		goto error;
	}

	imx_2d_surface_set_dma_buffer(linux_framebuffer->surface, (ImxDmaBuffer *)&(linux_framebuffer->dma_buffer), 0, 0);


finish:
	return linux_framebuffer;

error:
	if (linux_framebuffer != NULL)
	{
		imx_2d_linux_framebuffer_restore_original_fb_height(linux_framebuffer);

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
	{
		imx_2d_linux_framebuffer_restore_original_fb_height(linux_framebuffer);
		close(linux_framebuffer->fd);
	}

	free(linux_framebuffer);
}


int imx_2d_linux_framebuffer_get_num_fb_pages(Imx2dLinuxFramebuffer *linux_framebuffer)
{
	return linux_framebuffer->enable_page_flipping ? NUM_PAGE_FLIPPING_PAGES : 1;
}


void imx_2d_linux_framebuffer_set_write_fb_page(Imx2dLinuxFramebuffer *linux_framebuffer, int page)
{
	int page_offset_in_bytes;

	assert(linux_framebuffer != NULL);
	assert(linux_framebuffer->fd > 0);
	assert((page >= 0) && (page < imx_2d_linux_framebuffer_get_num_fb_pages(linux_framebuffer)));

	page_offset_in_bytes = linux_framebuffer->page_size_in_bytes * page;
	linux_framebuffer->dma_buffer.physical_address = linux_framebuffer->basic_physical_address + page_offset_in_bytes;

	IMX_2D_LOG(
		TRACE,
		"setting new physical address for writing to %" IMX_PHYSICAL_ADDRESS_FORMAT " (= basic physical address %" IMX_PHYSICAL_ADDRESS_FORMAT " plus offset %d for page %d)",
		linux_framebuffer->dma_buffer.physical_address,
		linux_framebuffer->basic_physical_address,
		page_offset_in_bytes,
		page
	);
}


int imx_2d_linux_framebuffer_set_display_fb_page(Imx2dLinuxFramebuffer *linux_framebuffer, int page)
{
	assert(linux_framebuffer != NULL);
	assert(linux_framebuffer->fd > 0);
	assert((page >= 0) && (page < imx_2d_linux_framebuffer_get_num_fb_pages(linux_framebuffer)));

	linux_framebuffer->fb_var.yoffset = linux_framebuffer->surface->desc.height * page;

	IMX_2D_LOG(TRACE, "shifting framebuffer display Y offset to %u to show page %d", linux_framebuffer->fb_var.yoffset, page);

	if (ioctl(linux_framebuffer->fd, FBIOPAN_DISPLAY, &(linux_framebuffer->fb_var)) == -1)
	{
		IMX_2D_LOG(ERROR, "FBIOPAN_DISPLAY error: %s (%d)", strerror(errno), errno);
		return FALSE;
	}

	return TRUE;
}
