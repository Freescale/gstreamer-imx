/* VPU registered framebuffers structure
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
#include <gst/video/video.h>

#include "vpu_framebuffers.h"
#include "vpu_utils.h"


GST_DEBUG_CATEGORY_STATIC(vpu_framebuffers_debug);
#define GST_CAT_DEFAULT vpu_framebuffers_debug


#define ALIGN_VAL_TO(LENGTH, ALIGN_SIZE)  ( ((guintptr)((LENGTH) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )
#define FRAME_ALIGN 16


G_DEFINE_TYPE(GstFslVpuFramebuffers, gst_fsl_vpu_framebuffers, GST_TYPE_OBJECT)


static gboolean gst_fsl_vpu_framebuffers_configure(GstFslVpuFramebuffers *framebuffers, VpuDecHandle handle, VpuDecInitInfo *init_info);
static void gst_fsl_vpu_framebuffers_finalize(GObject *object);




void gst_fsl_vpu_framebuffers_class_init(GstFslVpuFramebuffersClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_fsl_vpu_framebuffers_finalize);

	GST_DEBUG_CATEGORY_INIT(vpu_framebuffers_debug, "vpuframebuffers", 0, "Freescale VPU framebuffer memory blocks");
}


void gst_fsl_vpu_framebuffers_init(GstFslVpuFramebuffers *framebuffers)
{
	framebuffers->decoder_open = FALSE;

	framebuffers->framebuffers = NULL;
	framebuffers->num_framebuffers = 0;
	framebuffers->num_available_framebuffers = 0;
	framebuffers->fb_mem_blocks = NULL;

	framebuffers->y_stride = framebuffers->uv_stride = 0;
	framebuffers->y_size = framebuffers->u_size = framebuffers->v_size = framebuffers->mv_size = 0;
	framebuffers->total_size = 0;

	g_mutex_init(&(framebuffers->available_fb_mutex));
}


GstFslVpuFramebuffers * gst_fsl_vpu_framebuffers_new(VpuDecHandle handle, VpuDecInitInfo *init_info)
{
	GstFslVpuFramebuffers *framebuffers;
	framebuffers = g_object_new(gst_fsl_vpu_framebuffers_get_type(), NULL);
	if (gst_fsl_vpu_framebuffers_configure(framebuffers, handle, init_info))
		return framebuffers;
	else
		return NULL;
}


static gboolean gst_fsl_vpu_framebuffers_configure(GstFslVpuFramebuffers *framebuffers, VpuDecHandle handle, VpuDecInitInfo *init_info)
{
	int alignment;
	unsigned char *phys_ptr, *virt_ptr;
	guint i;
	VpuDecRetCode vpu_ret;

	framebuffers->num_reserve_framebuffers = init_info->nMinFrameBufferCount;
	framebuffers->num_framebuffers = MAX((guint)(init_info->nMinFrameBufferCount), (guint)10) + framebuffers->num_reserve_framebuffers;
	framebuffers->num_available_framebuffers = framebuffers->num_framebuffers - framebuffers->num_reserve_framebuffers;
	framebuffers->framebuffers = (VpuFrameBuffer *)g_slice_alloc(sizeof(VpuFrameBuffer) * framebuffers->num_framebuffers);

	framebuffers->handle = handle;

	framebuffers->y_stride = ALIGN_VAL_TO(init_info->nPicWidth, FRAME_ALIGN);
	if (init_info->nInterlace)
		framebuffers->y_size = framebuffers->y_stride * ALIGN_VAL_TO(init_info->nPicHeight, (2 * FRAME_ALIGN));
	else
		framebuffers->y_size = framebuffers->y_stride * ALIGN_VAL_TO(init_info->nPicHeight, FRAME_ALIGN);

	framebuffers->uv_stride = framebuffers->y_stride / 2;
	framebuffers->u_size = framebuffers->v_size = framebuffers->mv_size = framebuffers->y_size / 4;

	alignment = init_info->nAddressAlignment;
	if (alignment > 1)
	{
		framebuffers->y_size = ALIGN_VAL_TO(framebuffers->y_size, alignment);
		framebuffers->u_size = ALIGN_VAL_TO(framebuffers->u_size, alignment);
		framebuffers->v_size = ALIGN_VAL_TO(framebuffers->v_size, alignment);
		framebuffers->mv_size = ALIGN_VAL_TO(framebuffers->mv_size, alignment);
	}

	framebuffers->pic_width = init_info->nPicWidth;
	framebuffers->pic_height = init_info->nPicHeight;

	framebuffers->total_size = framebuffers->y_size + framebuffers->u_size + framebuffers->v_size + framebuffers->mv_size + alignment;
	GST_DEBUG_OBJECT(framebuffers, "num framebuffers:  total: %u  reserved: %u  available: %d", framebuffers->num_framebuffers, framebuffers->num_reserve_framebuffers, framebuffers->num_available_framebuffers);
	GST_DEBUG_OBJECT(framebuffers, "framebuffer memory block size:  total: %d  Y: %d  U: %d  V: %d  Mv:  %d  alignment: %d", framebuffers->total_size, framebuffers->y_size, framebuffers->u_size, framebuffers->v_size, framebuffers->mv_size, alignment);

	for (i = 0; i < framebuffers->num_framebuffers; ++i)
	{
		VpuMemDesc *mem_block;
		VpuFrameBuffer *framebuffer;

		framebuffer = &(framebuffers->framebuffers[i]);

		if (!gst_fsl_vpu_alloc_phys_mem_block(&mem_block, framebuffers->total_size))
			return FALSE;
		gst_fsl_vpu_append_phys_mem_block(mem_block, &(framebuffers->fb_mem_blocks));

		phys_ptr = (unsigned char*)(mem_block->nPhyAddr);
		virt_ptr = (unsigned char*)(mem_block->nVirtAddr);

		if (alignment > 1)
		{
			phys_ptr = (unsigned char*)ALIGN_VAL_TO(phys_ptr, alignment);
			virt_ptr = (unsigned char*)ALIGN_VAL_TO(virt_ptr, alignment);
		}

		framebuffer->nStrideY = framebuffers->y_stride;
		framebuffer->nStrideC = framebuffers->uv_stride;	

		/* fill phy addr*/
		framebuffer->pbufY     = phys_ptr;
		framebuffer->pbufCb    = phys_ptr + framebuffers->y_size;
		framebuffer->pbufCr    = phys_ptr + framebuffers->y_size + framebuffers->u_size;
		framebuffer->pbufMvCol = phys_ptr + framebuffers->y_size + framebuffers->u_size + framebuffers->v_size;

		/* fill virt addr */
		framebuffer->pbufVirtY     = virt_ptr;
		framebuffer->pbufVirtCb    = virt_ptr + framebuffers->y_size;
		framebuffer->pbufVirtCr    = virt_ptr + framebuffers->y_size + framebuffers->u_size;
		framebuffer->pbufVirtMvCol = virt_ptr + framebuffers->y_size + framebuffers->u_size + framebuffers->v_size;

		framebuffer->pbufY_tilebot = 0;
		framebuffer->pbufCb_tilebot = 0;
		framebuffer->pbufVirtY_tilebot = 0;
		framebuffer->pbufVirtCb_tilebot = 0;
	}

	vpu_ret = VPU_DecRegisterFrameBuffer(framebuffers->handle, framebuffers->framebuffers, framebuffers->num_framebuffers);
	if (vpu_ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(framebuffers, "registering framebuffers failed: %s", gst_fsl_vpu_strerror(vpu_ret));
		return FALSE;
	}

	framebuffers->decoder_open = TRUE;

	return TRUE;
}


static void gst_fsl_vpu_framebuffers_finalize(GObject *object)
{
	GstFslVpuFramebuffers *framebuffers = GST_FSL_VPU_FRAMEBUFFERS(object);

	GST_DEBUG_OBJECT(framebuffers, "freeing framebuffer memory");

	if (framebuffers->framebuffers != NULL)
	{
		g_slice_free1(sizeof(VpuFrameBuffer) * framebuffers->num_framebuffers, framebuffers->framebuffers);
		framebuffers->framebuffers = NULL;
	}

	gst_fsl_vpu_free_phys_mem_blocks(&(framebuffers->fb_mem_blocks));

	G_OBJECT_CLASS(gst_fsl_vpu_framebuffers_parent_class)->finalize(object);
}

