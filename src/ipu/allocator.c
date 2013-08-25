#include "allocator.h"
#include <string.h>
#include <sys/ioctl.h>
#include <linux/ipu.h>


gpointer gst_fsl_ipu_alloc_phys_mem(int ipu_fd, gsize size)
{
	dma_addr_t m;
	int ret;

	m = (dma_addr_t)size;
	ret = ioctl(ipu_fd, IPU_ALLOC, &m);
	if (ret < 0)
	{
		GST_ERROR("could not allocate %u bytes of physical memory: %s", size, strerror(errno));
		return NULL;
	}
	else
	{
		gpointer mem = (gpointer)m;
		GST_DEBUG("allocated %u bytes of physical memory at address %p", size, mem);
		return mem;
	}
}


gboolean gst_fsl_ipu_free_phys_mem(int ipu_fd, gpointer mem)
{
	dma_addr_t m;
	int ret;

	m = (dma_addr_t)mem;
	ret = ioctl(ipu_fd, IPU_FREE, &(m));
	if (ret < 0)
	{
		GST_ERROR("could not free physical memory at address %p: %s", mem, strerror(errno));
		return FALSE;
	}
	else
	{
		GST_ERROR("freed physical memory at address %p", mem);
		return TRUE;
	}
}

