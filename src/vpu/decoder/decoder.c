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


#include <config.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <vpu_wrapper.h>
#include "decoder.h"
#include "../common/alloc.h"
#include "../common/phys_mem_meta.h"
#include "../utils.h"
#include "../buffer_pool.h"



/* Some notes about the design of this decoder:
 *
 * The VPU wrapper memory model and the GStreamer buffer pool design are fundamentally at odds with each other.
 * The VPU wrapper expects the user to allocate and register a fixed set of framebuffers right after the first
 * VPU_DecDecodeBuf() call returned with an VPU_DEC_INIT_OK code. This allocation happens once, and only once;
 * reallocations or additional allocated buffers are not possible.
 * GStreamer buffer pools, on the other hand, allocate on-demand, and allocate more buffers if necessary.
 * To further complicate matters, the VPU wrapper has its own pooling logic; the user does not pick a framebuffer
 * for the VPU to store decoded frames into, the VPU does that on its own.
 *
 * To bring these two together, an indirection was used: the allocated and registered framebuffers are contained
 * and managed by the vpu_framebuffers structure. This structure derives from GstObject, and is therefore ref-
 * counted. Once VPU_DecDecodeBuf() returns VPU_DEC_INIT_OK , an instance of the vpu_framebuffers structure is
 * created. Internally, this allocates and registers framebuffers. There is also a custom buffer pool, which
 * creates buffers with VPU-specific metadata, but no memory blocks. The buffer pools are always created after
 * the framebuffers, since the buffer pools are created by the gst_video_decoder_allocate_output_buffer() call,
 * which internally triggers the decide_allocation call, which in turn creates buffer pools. Created buffer pools
 * retain a reference to the currently present vpu_framebuffers instance.
 * The idea is to let the buffer pool deliver a buffer when the VPU_DecDecodeBuf() reports that a frame was
 * decoded. If the buffer already has memory blocks, they are removed. Then, the framebuffer that was used by
 * the VPU wrapper to store the decoded frame is retrieved using VPU_DecGetOutputFrame(). Finally, the
 * framebuffer is wrapped inside a GstMemory block, this block is added to the empty buffer, and the buffer is
 * sent downstream. Once the buffer is no longer used downstream, it is returned to the buffer pool. This triggers
 * a release_buffer() call in the buffer pool, which is extended to clear the display flag from the framebuffer.
 * This is necessary to notify the VPU wrapper that this frame is no longer used by anybody, and can be filled
 * with decoded frames safely.
 *
 * In case the caps change for some reason, the set_format() function is invoked. Internally, it unrefs the
 * framebuffers structure, closes the VPU decoder instance, and opens a new one, based on the new caps.
 * Later, VPU_DecDecodeBuf() will return VPU_DEC_INIT_OK again, and a new framebuffers instance will be created
 * etc.
 * Since the framebuffers instance is refcounted, there will be no conflicts between old buffer pools and a
 * new framebuffers structure. Old buffer pools that are kept alive for some reason (for example, because there
 * are some of its buffers still floating around) in turn keep their associated old framebuffers instance alive.
 * This prevents stale states.
 *
 * The fundamental problem with the VPU wrapper memory model is the case where all framebuffers are occupied.
 * Then, the wrapper cannot pick a framebuffer to decode into, and decoding fails. This can easily happen if
 * the GStreamer pipeline uses queues and downstream is not consuming the frames fast enough for some reason.
 * To counter this effect, a trick was devised: the vpu_framebuffers structure allocates an extra set of
 * framebuffers These are called the "reserved" framebuffers, while the others are the "available" ones.
 * A counter is used (called "num_available_framebuffers"). This counts the number of available framebuffers,
 * and initially equals the number of available framebuffers. Every time VPU_DecDecodeBuf() reports that a frame
 * was consumed (NOTE: not to be confused with "a frame was decoded"), the counter is decremented.
 * If the handle_frame() function is entered with a num_available_framebuffers value of zero, it means that all
 * available framebuffers are occupied; only the reserved framebuffers are free. Decoding then switches to a
 * secondary mode; inside handle_frame, do_memcpy is set to TRUE. Decoding continues as usual. But instead of
 * wrapping the framebuffer containing the decoded frame, the decoded frame pixels are copied with memcpy()
 * to a GstMemory block that was allocated on the heap. This block is then added to the buffer. The framebuffer
 * is immediately marked as displayed (as opposed to when the buffer is returned to the buffer pool), and the
 * buffer is sent downstream.
 * In short: instead of sending the framebuffers downstream directly, their contents are copied to the heap,
 * and the *copies* are sent downstream.
 *
 * This would not be necessary if it was possible to allocate new VPU framebuffers on the fly. Hopefully,
 * Freescale will eventually adopt a different model, based on DMABUF.
 */




GST_DEBUG_CATEGORY_STATIC(vpudec_debug);
#define GST_CAT_DEFAULT vpudec_debug


#define ALIGN_VAL_TO(LENGTH, ALIGN_SIZE)  ( ((guintptr)((LENGTH) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )



static GMutex inst_counter_mutex;



static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		/* VPU_V_AVC */
		"video/x-h264, "
		"parsed = (boolean) true, "
		"stream-format = (string) byte-stream, "
		"alignment = (string) au; "

		/* VPU_V_MPEG2 */
		"video/mpeg, "
		"parsed = (boolean) true, "
		"systemstream = (boolean) false, "
		"mpegversion = (int) [ 1, 2 ]; "

		/* VPU_V_MPEG4 */
		"video/mpeg, "
		"parsed = (boolean) true, "
		"mpegversion = (int) 4; "

		/* VPU_V_DIVX3 */
		"video/x-divx, "
		"divxversion = (int) 3; "

		/* VPU_V_DIVX56 */
		"video/x-divx, "
		"divxversion = (int) [ 5, 6 ]; "

		/* VPU_V_XVID */
		"video/x-xvid; "

		/* VPU_V_H263 */
		"video/x-h263, "
		"parsed = (boolean) true; "

/* TODO: WMV disabled for now, since it does not work properly.
 * It is necessary to find out how to prepare WMV3/VC1 data for the VPU first. */
		/* VPU_V_VC1_AP and VPU_V_VC1 */
		/* WVC1 = VC1-AP (VPU_V_VC1_AP) */
		/* WMV3 = VC1-SPMP (VPU_V_VC1) */
		/*"video/x-wmv, "
		"wmvversion = (int) 3, "
		"format = (string) { WVC1, WMV3 }; "*/

		/* VPU_V_VP8 */
		"video/x-vp8; "
	)
);

static GstStaticPadTemplate static_src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) I420, "
		"width = (int) [ 16, 2048 ], "
		"height = (int) [ 16, 2048 ], "
		"framerate = (fraction) [ 0, MAX ]"
	)
);


G_DEFINE_TYPE(GstFslVpuDec, gst_fsl_vpu_dec, GST_TYPE_VIDEO_DECODER)


/* miscellaneous functions */
static gboolean gst_fsl_vpu_dec_alloc_dec_mem_blocks(GstFslVpuDec *vpu_dec);
static gboolean gst_fsl_vpu_dec_fill_param_set(GstFslVpuDec *vpu_dec, GstVideoCodecState *state, VpuDecOpenParam *open_param, GstBuffer **codec_data);
static void gst_fsl_vpu_dec_close_decoder(GstFslVpuDec *vpu_dec);

/* functions for the base class */
static gboolean gst_fsl_vpu_dec_start(GstVideoDecoder *decoder);
static gboolean gst_fsl_vpu_dec_stop(GstVideoDecoder *decoder);
static gboolean gst_fsl_vpu_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state);
static GstFlowReturn gst_fsl_vpu_dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *frame);
static gboolean gst_fsl_vpu_dec_reset(GstVideoDecoder *decoder, gboolean hard);
static gboolean gst_fsl_vpu_dec_decide_allocation(GstVideoDecoder *decoder, GstQuery *query);




/* required function declared by G_DEFINE_TYPE */

void gst_fsl_vpu_dec_class_init(GstFslVpuDecClass *klass)
{
	GstVideoDecoderClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(vpudec_debug, "vpudec", 0, "Freescale VPU video decoder");

	base_class = GST_VIDEO_DECODER_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU video decoder",
		"Codec/Decoder/Video",
		"hardware-accelerated video decoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	base_class->start             = GST_DEBUG_FUNCPTR(gst_fsl_vpu_dec_start);
	base_class->stop              = GST_DEBUG_FUNCPTR(gst_fsl_vpu_dec_stop);
	base_class->set_format        = GST_DEBUG_FUNCPTR(gst_fsl_vpu_dec_set_format);
	base_class->handle_frame      = GST_DEBUG_FUNCPTR(gst_fsl_vpu_dec_handle_frame);
	base_class->reset             = GST_DEBUG_FUNCPTR(gst_fsl_vpu_dec_reset);
	base_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_fsl_vpu_dec_decide_allocation);

	klass->inst_counter = 0;
}


void gst_fsl_vpu_dec_init(GstFslVpuDec *vpu_dec)
{
	vpu_dec->vpu_inst_opened = FALSE;

	vpu_dec->codec_data = NULL;
	vpu_dec->current_framebuffers = NULL;
	vpu_dec->current_output_state = NULL;

	vpu_dec->virt_dec_mem_blocks = NULL;
	vpu_dec->phys_dec_mem_blocks = NULL;
}




/***************************/
/* miscellaneous functions */

static gboolean gst_fsl_vpu_dec_alloc_dec_mem_blocks(GstFslVpuDec *vpu_dec)
{
	int i;
	int size;
	unsigned char *ptr;

	for (i = 0; i < vpu_dec->mem_info.nSubBlockNum; ++i)
 	{
		size = vpu_dec->mem_info.MemSubBlock[i].nAlignment + vpu_dec->mem_info.MemSubBlock[i].nSize;
		GST_DEBUG_OBJECT(vpu_dec, "sub block %d  type: %s  size: %d", i, (vpu_dec->mem_info.MemSubBlock[i].MemType == VPU_MEM_VIRT) ? "virtual" : "phys", size);
 
		if (vpu_dec->mem_info.MemSubBlock[i].MemType == VPU_MEM_VIRT)
		{
			if (!gst_fsl_alloc_virt_mem_block(&ptr, size))
				return FALSE;

			vpu_dec->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO(ptr, vpu_dec->mem_info.MemSubBlock[i].nAlignment);

			gst_fsl_append_virt_mem_block(ptr, &(vpu_dec->virt_dec_mem_blocks));
		}
		else if (vpu_dec->mem_info.MemSubBlock[i].MemType == VPU_MEM_PHY)
		{
			gst_fsl_phys_mem_block *mem_block;
			if (!gst_fsl_alloc_phys_mem_block(&mem_block, size))
				return FALSE;

			vpu_dec->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(mem_block->virt_addr), vpu_dec->mem_info.MemSubBlock[i].nAlignment);
			vpu_dec->mem_info.MemSubBlock[i].pPhyAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(mem_block->phys_addr), vpu_dec->mem_info.MemSubBlock[i].nAlignment);

			gst_fsl_append_phys_mem_block(mem_block, &(vpu_dec->phys_dec_mem_blocks));
		}
		else
		{
			GST_WARNING_OBJECT(vpu_dec, "sub block %d type is unknown - skipping", i);
		}
 	}

	return TRUE;
}


static gboolean gst_fsl_vpu_dec_free_dec_mem_blocks(GstFslVpuDec *vpu_dec)
{
	gboolean ret1, ret2;
	/* NOT using the two calls with && directly, since otherwise an early exit could happen; in other words,
	 * if the first call failed, the second one wouldn't even be invoked
	 * doing the logical AND afterwards fixes this */
	ret1 = gst_fsl_free_virt_mem_blocks(&(vpu_dec->virt_dec_mem_blocks));
	ret2 = gst_fsl_free_phys_mem_blocks(&(vpu_dec->phys_dec_mem_blocks));
	return ret1 && ret2;
}


static gboolean gst_fsl_vpu_dec_fill_param_set(GstFslVpuDec *vpu_dec, GstVideoCodecState *state, VpuDecOpenParam *open_param, GstBuffer **codec_data)
{
	guint structure_nr;
	gboolean format_set;
	gboolean do_codec_data = FALSE;

	memset(open_param, 0, sizeof(VpuDecOpenParam));

	for (structure_nr = 0; structure_nr < gst_caps_get_size(state->caps); ++structure_nr)
	{
		GstStructure *s;
		gchar const *name;

		format_set = TRUE;
		s = gst_caps_get_structure(state->caps, structure_nr);
		name = gst_structure_get_name(s);

		open_param->nReorderEnable = 0;

		if (g_strcmp0(name, "video/x-h264") == 0)
		{
			open_param->CodecFormat = VPU_V_AVC;
			open_param->nReorderEnable = 1;
			GST_INFO_OBJECT(vpu_dec, "setting h.264 as stream format");
		}
		else if (g_strcmp0(name, "video/mpeg") == 0)
		{
			gint mpegversion;
			if (gst_structure_get_int(s, "mpegversion", &mpegversion))
			{
				gboolean is_systemstream;
				switch (mpegversion)
				{
					case 1:
					case 2:
						if (gst_structure_get_boolean(s, "systemstream", &is_systemstream) && !is_systemstream)
						{
							open_param->CodecFormat = VPU_V_MPEG2;
						}
						else
						{
							GST_WARNING_OBJECT(vpu_dec, "MPEG-%d system stream is not supported", mpegversion);
							format_set = FALSE;
						}
						break;
					case 4:
						open_param->CodecFormat = VPU_V_MPEG4;
						break;
					default:
						GST_WARNING_OBJECT(vpu_dec, "unsupported MPEG version: %d", mpegversion);
						format_set = FALSE;
						break;
				}

				if (format_set)
					GST_INFO_OBJECT(vpu_dec, "setting MPEG-%d as stream format", mpegversion);
			}

			do_codec_data = TRUE;
		}
		else if (g_strcmp0(name, "video/x-divx") == 0)
		{
			gint divxversion;
			if (gst_structure_get_int(s, "divxversion", &divxversion))
			{
				switch (divxversion)
				{
					case 3:
						open_param->CodecFormat = VPU_V_DIVX3;
						break;
					case 5:
					case 6:
						open_param->CodecFormat = VPU_V_DIVX56;
						break;
					default:
						format_set = FALSE;
						break;
				}

				if (format_set)
					GST_INFO_OBJECT(vpu_dec, "setting DivX %d as stream format", divxversion);
			}
		}
		else if (g_strcmp0(name, "video/x-xvid") == 0)
		{
			open_param->CodecFormat = VPU_V_XVID;
			GST_INFO_OBJECT(vpu_dec, "setting xvid as stream format");
		}
		else if (g_strcmp0(name, "video/x-h263") == 0)
		{
			open_param->CodecFormat = VPU_V_H263;
			GST_INFO_OBJECT(vpu_dec, "setting h.263 as stream format");
		}
		else if (g_strcmp0(name, "video/x-wmv") == 0)
		{
			gint wmvversion;
			gchar const *format_str;

			if (!gst_structure_get_int(s, "wmvversion", &wmvversion))
			{
				GST_WARNING_OBJECT(vpu_dec, "wmvversion caps is missing");
				format_set = FALSE;
				break;
			}
			if (wmvversion != 3)
			{
				GST_WARNING_OBJECT(vpu_dec, "unsupported WMV version %d (only version 3 is supported)", wmvversion);
				format_set = FALSE;
				break;
			}

			format_str = gst_structure_get_string(s, "format");
			if ((format_str == NULL) || g_str_equal(format_str, "WMV3"))
			{
				GST_INFO_OBJECT(vpu_dec, "setting VC1M (= WMV3, VC1-SPMP) as stream format");
				open_param->CodecFormat = VPU_V_VC1;
			}
			else if (g_str_equal(format_str, "WVC1"))
			{
				GST_INFO_OBJECT(vpu_dec, "setting VC1 (= WVC1, VC1-AP) as stream format");
				open_param->CodecFormat = VPU_V_VC1_AP;
			}
			else
			{
				GST_WARNING_OBJECT(vpu_dec, "unsupported WMV format \"%s\"", format_str);
				format_set = FALSE;
			}

			do_codec_data = TRUE;
		}
		else if (g_strcmp0(name, "video/x-vp8") == 0)
		{
			open_param->CodecFormat = VPU_V_VP8;
			GST_INFO_OBJECT(vpu_dec, "setting VP8 as stream format");
		}

		if  (format_set)
		{
			if (do_codec_data)
			{
				GValue const *value = gst_structure_get_value(s, "codec_data");
				if (value != NULL)
				{
					GST_INFO_OBJECT(vpu_dec, "codec data expected and found in caps");
					*codec_data = gst_value_get_buffer(value);
				}
				else
				{
					GST_WARNING_OBJECT(vpu_dec, "codec data expected, but not found in caps");
					format_set = FALSE;
				}
			}

			break;
		}
	}

	if (!format_set)
		return FALSE;

	open_param->nChromaInterleave = 0;
	open_param->nMapType = 0;
	open_param->nTiled2LinearEnable = 0;
	open_param->nEnableFileMode = 0;
	open_param->nPicWidth = state->info.width;
	open_param->nPicHeight = state->info.height;

	return TRUE;
}


static void gst_fsl_vpu_dec_close_decoder(GstFslVpuDec *vpu_dec)
{
	VpuDecRetCode dec_ret;

	if (vpu_dec->vpu_inst_opened)
	{
		dec_ret = VPU_DecFlushAll(vpu_dec->handle);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
			GST_ERROR_OBJECT(vpu_dec, "flushing decoder failed: %s", gst_fsl_vpu_strerror(dec_ret));

		dec_ret = VPU_DecClose(vpu_dec->handle);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
			GST_ERROR_OBJECT(vpu_dec, "closing decoder failed: %s", gst_fsl_vpu_strerror(dec_ret));

		vpu_dec->vpu_inst_opened = FALSE;
	}
}




/********************************/
/* functions for the base class */

static gboolean gst_fsl_vpu_dec_start(GstVideoDecoder *decoder)
{
	VpuDecRetCode ret;
	GstFslVpuDec *vpu_dec;
	GstFslVpuDecClass *klass;

	vpu_dec = GST_FSL_VPU_DEC(decoder);
	klass = GST_FSL_VPU_DEC_CLASS(G_OBJECT_GET_CLASS(vpu_dec));

#define VPUINIT_ERR(RET, DESC, UNLOAD) \
	if ((RET) != VPU_DEC_RET_SUCCESS) \
	{ \
		g_mutex_unlock(&inst_counter_mutex); \
		GST_ELEMENT_ERROR(vpu_dec, LIBRARY, INIT, ("%s: %s", (DESC), gst_fsl_vpu_strerror(RET)), (NULL)); \
		if (UNLOAD) \
			VPU_DecUnLoad(); \
		return FALSE; \
	}

	g_mutex_lock(&inst_counter_mutex);
	if (klass->inst_counter == 0)
	{
		ret = VPU_DecLoad();
		VPUINIT_ERR(ret, "loading VPU failed", FALSE);

		{
			VpuVersionInfo version;
			VpuWrapperVersionInfo wrapper_version;

			ret = VPU_DecGetVersionInfo(&version);
			VPUINIT_ERR(ret, "getting version info failed", TRUE);

			ret = VPU_DecGetWrapperVersionInfo(&wrapper_version);
			VPUINIT_ERR(ret, "getting wrapper version info failed", TRUE);

			GST_INFO_OBJECT(vpu_dec, "VPU loaded");
			GST_INFO_OBJECT(vpu_dec, "VPU firmware version %d.%d.%d_r%d", version.nFwMajor, version.nFwMinor, version.nFwRelease, version.nFwCode);
			GST_INFO_OBJECT(vpu_dec, "VPU library version %d.%d.%d", version.nLibMajor, version.nLibMinor, version.nLibRelease);
			GST_INFO_OBJECT(vpu_dec, "VPU wrapper version %d.%d.%d %s", wrapper_version.nMajor, wrapper_version.nMinor, wrapper_version.nRelease, wrapper_version.pBinary);
		}
	}
	++klass->inst_counter;
	g_mutex_unlock(&inst_counter_mutex);

	/* mem_info contains information about how to set up memory blocks
	 * the VPU uses as temporary storage (they are "work buffers") */
	memset(&(vpu_dec->mem_info), 0, sizeof(VpuMemInfo));
	ret = VPU_DecQueryMem(&(vpu_dec->mem_info));
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not get VPU memory information: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	/* Allocate the work buffers
	 * Note that these are independent of decoder instances, so they
	 * are allocated before the VPU_DecOpen() call, and are not
	 * recreated in set_format */
	if (!gst_fsl_vpu_dec_alloc_dec_mem_blocks(vpu_dec))
		return FALSE;

#undef VPUINIT_ERR

	/* The decoder is initialized in set_format, not here, since only then the input bitstream
	 * format is known (and this information is necessary for initialization). */

	return TRUE;
}


static gboolean gst_fsl_vpu_dec_stop(GstVideoDecoder *decoder)
{
	gboolean ret;
	VpuDecRetCode dec_ret;
	GstFslVpuDec *vpu_dec;
	GstFslVpuDecClass *klass;

	ret = TRUE;

	vpu_dec = GST_FSL_VPU_DEC(decoder);
	klass = GST_FSL_VPU_DEC_CLASS(G_OBJECT_GET_CLASS(vpu_dec));

	if (vpu_dec->current_framebuffers != NULL)
	{
		/* Using mutexes here to prevent race conditions when decoder_open is set to
		 * FALSE at the same time as it is checked in the buffer pool release() function */
		g_mutex_lock(&(vpu_dec->current_framebuffers->available_fb_mutex));
		vpu_dec->current_framebuffers->decoder_open = FALSE;
		g_mutex_unlock(&(vpu_dec->current_framebuffers->available_fb_mutex));

		gst_object_unref(vpu_dec->current_framebuffers);
		vpu_dec->current_framebuffers = NULL;
	}

	gst_fsl_vpu_dec_close_decoder(vpu_dec);
	gst_fsl_vpu_dec_free_dec_mem_blocks(vpu_dec);

	if (vpu_dec->codec_data != NULL)
	{
		gst_buffer_unref(vpu_dec->codec_data);
		vpu_dec->codec_data = NULL;
	}

	if (vpu_dec->current_output_state != NULL)
	{
		gst_video_codec_state_unref(vpu_dec->current_output_state);
		vpu_dec->current_output_state = NULL;
	}

	g_mutex_lock(&inst_counter_mutex);
	if (klass->inst_counter > 0)
	{
		--klass->inst_counter;
		if (klass->inst_counter == 0)
		{
			dec_ret = VPU_DecUnLoad();
			if (dec_ret != VPU_DEC_RET_SUCCESS)
			{
				GST_ERROR_OBJECT(vpu_dec, "unloading VPU failed: %s", gst_fsl_vpu_strerror(dec_ret));
			}
			else
				GST_INFO_OBJECT(vpu_dec, "VPU unloaded");
		}
	}
	g_mutex_unlock(&inst_counter_mutex);

	return ret;
}


static gboolean gst_fsl_vpu_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state)
{
	VpuDecRetCode ret;
	VpuDecOpenParam open_param;
	int config_param;
	GstBuffer *codec_data = NULL;
	GstFslVpuDec *vpu_dec = GST_FSL_VPU_DEC(decoder);

	/* Clean up existing framebuffers structure;
	 * if some previous and still existing buffer pools depend on this framebuffers
	 * structure, they will extend its lifetime, since they ref'd it
	 */
	if (vpu_dec->current_framebuffers != NULL)
	{
		/* Using mutexes here to prevent race conditions when decoder_open is set to
		 * FALSE at the same time as it is checked in the buffer pool release() function */
		g_mutex_lock(&(vpu_dec->current_framebuffers->available_fb_mutex));
		vpu_dec->current_framebuffers->decoder_open = FALSE;
		g_mutex_unlock(&(vpu_dec->current_framebuffers->available_fb_mutex));

		gst_object_unref(vpu_dec->current_framebuffers);
		vpu_dec->current_framebuffers = NULL;
	}

	/* Clean up old codec data copy */
	if (vpu_dec->codec_data != NULL)
	{
		gst_buffer_unref(vpu_dec->codec_data);
		vpu_dec->codec_data = NULL;
	}

	/* Clean up old output state */
	if (vpu_dec->current_output_state != NULL)
	{
		gst_video_codec_state_unref(vpu_dec->current_output_state);
		vpu_dec->current_output_state = NULL;
	}

	/* Close old decoder instance */
	gst_fsl_vpu_dec_close_decoder(vpu_dec);

	memset(&open_param, 0, sizeof(open_param));

	/* codec_data does not need to be unref'd after use; it is owned by the caps structure */
	if (!gst_fsl_vpu_dec_fill_param_set(vpu_dec, state, &open_param, &codec_data))
	{
		GST_ERROR_OBJECT(vpu_dec, "could not fill open params: state info incompatible");
		return FALSE;
	}

	/* The actual initialization; requires bitstream information (such as the codec type), which
	 * is determined by the fill_param_set call before */
	ret = VPU_DecOpen(&(vpu_dec->handle), &open_param, &(vpu_dec->mem_info));
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "opening new VPU handle failed: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	vpu_dec->vpu_inst_opened = TRUE;

	/* configure AFTER setting vpu_inst_opened to TRUE, to make sure that in case of
	   config failure the VPU handle is closed in the finalizer */

	config_param = VPU_DEC_SKIPNONE;
	ret = VPU_DecConfig(vpu_dec->handle, VPU_DEC_CONF_SKIPMODE, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not configure skip mode: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	config_param = 0;
	ret = VPU_DecConfig(vpu_dec->handle, VPU_DEC_CONF_BUFDELAY, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not configure buffer delay: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	config_param = VPU_DEC_IN_NORMAL;
	ret = VPU_DecConfig(vpu_dec->handle, VPU_DEC_CONF_INPUTTYPE, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not configure input type: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	/* Ref the output state, to be able to add information from the init_info structure to it later */
	vpu_dec->current_output_state = gst_video_codec_state_ref(state);

	/* Copy the buffer, to make sure the codec_data lifetime does not depend on the caps */
	if (codec_data != NULL)
		vpu_dec->codec_data = gst_buffer_copy(codec_data);

	return TRUE;

}


static GstFlowReturn gst_fsl_vpu_dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *frame)
{
	int buffer_ret_code;
	VpuDecRetCode dec_ret;
	VpuBufferNode in_data;
	GstMapInfo in_map_info;
	gboolean do_memcpy;
	guint8 *combined_input;
	gsize combined_input_size;
	GstFslVpuDec *vpu_dec;

	combined_input = NULL;
	vpu_dec = GST_FSL_VPU_DEC(decoder);

	memset(&in_data, 0, sizeof(in_data));

	gst_buffer_map(frame->input_buffer, &in_map_info, GST_MAP_READ);

	/* If there is codec data, prepend it to the input frame data
	 * To do that, allocate a temporary memory block big enough for both
	 * codec and input frame data, copy the codec data first, then the frame data
	 */
	if (vpu_dec->codec_data != NULL)
	{
		GstMapInfo cd_in_map_info;
		gst_buffer_map(vpu_dec->codec_data, &cd_in_map_info, GST_MAP_READ);

		combined_input_size = cd_in_map_info.size + in_map_info.size;
		combined_input = g_slice_alloc(combined_input_size);
		memcpy(combined_input, cd_in_map_info.data, cd_in_map_info.size);
		memcpy(combined_input + cd_in_map_info.size, in_map_info.data, in_map_info.size);

		GST_DEBUG_OBJECT(vpu_dec, "preparing combined input: codec data size: %u  input frame size: %u", cd_in_map_info.size, in_map_info.size);

		gst_buffer_unmap(vpu_dec->codec_data, &cd_in_map_info);

		gst_buffer_unref(vpu_dec->codec_data);
		vpu_dec->codec_data = NULL;

		in_data.pPhyAddr = NULL;
		in_data.pVirAddr = (unsigned char *)(combined_input);
		in_data.nSize = combined_input_size;
	}
	else
	{
		in_data.pPhyAddr = NULL;
		in_data.pVirAddr = (unsigned char *)(in_map_info.data);
		in_data.nSize = in_map_info.size;
	}

	GST_DEBUG_OBJECT(vpu_dec, "total input frame size: %u", in_data.nSize);

	/* Using a mutex here, since the VPU_DecDecodeBuf() call internally picks an
	 * available framebuffer, and at the same time, the bufferpool release() function
	 * might be returning a framebuffer to the list of available ones */
	if (vpu_dec->current_framebuffers != NULL)
		g_mutex_lock(&(vpu_dec->current_framebuffers->available_fb_mutex));
	dec_ret = VPU_DecDecodeBuf(vpu_dec->handle, &in_data, &buffer_ret_code);
	if (vpu_dec->current_framebuffers != NULL)
		g_mutex_unlock(&(vpu_dec->current_framebuffers->available_fb_mutex));

	if (dec_ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "failed to decode frame: %s", gst_fsl_vpu_strerror(dec_ret));
		return GST_FLOW_ERROR;
	}

	GST_LOG_OBJECT(vpu_dec, "VPU_DecDecodeBuf returns: %x", buffer_ret_code);

	/* Cleanup temporary combined block and input frame mapping */
	if (combined_input != NULL)
		g_slice_free1(combined_input_size, combined_input);
	gst_buffer_unmap(frame->input_buffer, &in_map_info);

	if (buffer_ret_code & VPU_DEC_INIT_OK)
	{
		dec_ret = VPU_DecGetInitialInfo(vpu_dec->handle, &(vpu_dec->init_info));
		if (dec_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "could not get init info: %s", gst_fsl_vpu_strerror(dec_ret));
			return GST_FLOW_ERROR;
		}

		/* Allocate and register a new set of framebuffers for decoding
		 * This point is always reached after set_format() was called,
		 * and always before a frame is output */
		vpu_dec->current_framebuffers = gst_fsl_vpu_framebuffers_new(vpu_dec->handle, &(vpu_dec->init_info));

		/* Add information from init_info to the output state and set it to be the output state for this decoder */
		if (vpu_dec->current_output_state != NULL)
		{
			GstVideoCodecState *state = vpu_dec->current_output_state;
			GST_VIDEO_INFO_INTERLACE_MODE(&(state->info)) = vpu_dec->init_info.nInterlace ? GST_VIDEO_INTERLACE_MODE_INTERLEAVED : GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
			gst_video_decoder_set_output_state(decoder, GST_VIDEO_FORMAT_I420, state->info.width, state->info.height, state);
			gst_video_codec_state_unref(vpu_dec->current_output_state);
			vpu_dec->current_output_state = NULL;
		}
	}

	if (buffer_ret_code & VPU_DEC_NO_ENOUGH_INBUF)
	{
		/* Not dropping frame here on purpose; the next input frame may
		 * complete the input */
		GST_DEBUG_OBJECT(vpu_dec, "need more input");
		return GST_FLOW_OK;
	}

	if (vpu_dec->current_framebuffers == NULL)
	{
		GST_ERROR_OBJECT(vpu_dec, "no framebuffers allocated");
		return GST_FLOW_ERROR;
	}

	do_memcpy = FALSE;

	/* The following code block may cause a race condition if not synchronized;
	 * the buffer pool release() function must not run at the same time */
	g_mutex_lock(&(vpu_dec->current_framebuffers->available_fb_mutex));

	if (buffer_ret_code & VPU_DEC_ONE_FRM_CONSUMED)
	{
		VpuDecFrameLengthInfo dec_framelen_info;

		dec_ret = VPU_DecGetConsumedFrameInfo(vpu_dec->handle, &dec_framelen_info);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
			GST_ERROR_OBJECT(vpu_dec, "could not get information about consumed frame: %s", gst_fsl_vpu_strerror(dec_ret));

		GST_DEBUG_OBJECT(vpu_dec, "one frame got consumed: framebuffer: %p  stuff length: %d  frame length: %d", dec_framelen_info.pFrame, dec_framelen_info.nStuffLength, dec_framelen_info.nFrameLength);

		if (vpu_dec->current_framebuffers->num_available_framebuffers > 0)
			vpu_dec->current_framebuffers->num_available_framebuffers--;
		GST_DEBUG_OBJECT(vpu_dec, "number of available buffers is %d", vpu_dec->current_framebuffers->num_available_framebuffers);
	}

	/* There are no available framebuffers left; only the reserved ones
	 * -> instead of sending them downstream, mark their contents to be
	 * copied to memory allocated on the heap */
	if (vpu_dec->current_framebuffers->num_available_framebuffers <= 0)
	{
		GST_WARNING_OBJECT(vpu_dec, "no framebuffers available - copying decoded contents to a heap buffer");
		do_memcpy = TRUE;
	}

	/* Unlock the mutex; the subsequent steps are safe */
	g_mutex_unlock(&(vpu_dec->current_framebuffers->available_fb_mutex));

	if ((buffer_ret_code & VPU_DEC_OUTPUT_DIS) || (buffer_ret_code & VPU_DEC_OUTPUT_MOSAIC_DIS))
	{
		GstBuffer *buffer;
		VpuDecOutFrameInfo out_frame_info;

		/* Retrieve the decoded frame */
		dec_ret = VPU_DecGetOutputFrame(vpu_dec->handle, &out_frame_info);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "could not get decoded output frame: %s", gst_fsl_vpu_strerror(dec_ret));
			return GST_FLOW_ERROR;
		}

		/* Create empty buffer */
		buffer = gst_video_decoder_allocate_output_buffer(decoder);
		/* ... and set its contents; either pass on the framebuffer directly,
		 * or have set_contents() copy its pixels to a memory block on the heap,
		 * depending on do_memcpy */
		if (!gst_fsl_vpu_set_buffer_contents(buffer, vpu_dec->current_framebuffers, out_frame_info.pDisplayFrameBuf, do_memcpy))
		{
			gst_buffer_unref(buffer);
			return GST_FLOW_ERROR;
		}

		GST_DEBUG_OBJECT(vpu_dec, "output frame:  framebuffer addr: %p  gstbuffer addr: %p  pic type: %d  Y stride: %d  CbCr stride: %d", out_frame_info.pDisplayFrameBuf, buffer, out_frame_info.ePicType, out_frame_info.pDisplayFrameBuf->nStrideY, out_frame_info.pDisplayFrameBuf->nStrideC);

		if (!do_memcpy)
		{
			/* If a framebuffer is sent downstream directly, it will
			 * have to be marked later as displayed after it was used,
			 * to allow the VPU wrapper to reuse it for new decoded
			 * frames. Since this is a fresh frame, and it wasn't
			 * used yet, mark it now as undisplayed. */
			gst_fsl_vpu_mark_buf_as_not_displayed(buffer);
		}

		frame->output_buffer = buffer;

		gst_video_decoder_finish_frame(decoder, frame);
	}
	else if (buffer_ret_code & VPU_DEC_OUTPUT_DROPPED)
	{
		GST_DEBUG_OBJECT(vpu_dec, "dropping frame");
		gst_video_decoder_drop_frame(decoder, frame);
	}
	else if (buffer_ret_code & VPU_DEC_NO_ENOUGH_BUF)
		GST_WARNING_OBJECT(vpu_dec, "no free output frame available (ret code: 0x%X)", buffer_ret_code);
	else
		GST_DEBUG_OBJECT(vpu_dec, "nothing to output (ret code: 0x%X)", buffer_ret_code);

	return GST_FLOW_OK;
}


static gboolean gst_fsl_vpu_dec_reset(GstVideoDecoder *decoder, gboolean hard)
{
	VpuDecRetCode ret;
	GstFslVpuDec *vpu_dec = GST_FSL_VPU_DEC(decoder);

	if (!vpu_dec->vpu_inst_opened)
		return TRUE;

	/* TODO: does the num_available_framebuffers counter have to be
	 * reset here? */

	ret = VPU_DecFlushAll(vpu_dec->handle);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "flushing VPU failed: %s", gst_fsl_vpu_strerror(ret));
		return FALSE;
	}

	return TRUE;
}


static gboolean gst_fsl_vpu_dec_decide_allocation(GstVideoDecoder *decoder, GstQuery *query)
{
	GstFslVpuDec *vpu_dec = GST_FSL_VPU_DEC(decoder);
	GstCaps *outcaps;
	GstBufferPool *pool = NULL;
	guint size, min = 0, max = 0;
	GstStructure *config;
	GstVideoInfo vinfo;
	gboolean update_pool;

	g_assert(vpu_dec->current_framebuffers != NULL);

	gst_query_parse_allocation(query, &outcaps, NULL);
	gst_video_info_init(&vinfo);
	gst_video_info_from_caps(&vinfo, outcaps);

	GST_DEBUG_OBJECT(decoder, "num allocation pools: %d", gst_query_get_n_allocation_pools(query));

	/* Look for an allocator which can allocate VPU DMA buffers */
	if (gst_query_get_n_allocation_pools(query) > 0)
	{
		for (guint i = 0; i < gst_query_get_n_allocation_pools(query); ++i)
		{
			gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min, &max);
			if (gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_FSL_VPU_FRAMEBUFFER))
				break;
		}

		size = MAX(size, (guint)(vpu_dec->current_framebuffers->total_size));
		size = MAX(size, vinfo.size);
		update_pool = TRUE;
	}
	else
	{
		pool = NULL;
		size = MAX(vinfo.size, (guint)(vpu_dec->current_framebuffers->total_size));
		min = max = 0;
		update_pool = FALSE;
	}

	/* Either no pool or no pool with the ability to allocate VPU DMA buffers
	 * has been found -> create a new pool */
	if ((pool == NULL) || !gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_FSL_VPU_FRAMEBUFFER))
	{
		if (pool == NULL)
			GST_DEBUG_OBJECT(decoder, "no pool present; creating new pool");
		else
			GST_DEBUG_OBJECT(decoder, "no pool supports VPU buffers; creating new pool");
		pool = gst_fsl_vpu_buffer_pool_new(vpu_dec->current_framebuffers);
	}

	GST_DEBUG_OBJECT(
		pool,
		"pool config:  outcaps: %" GST_PTR_FORMAT "  size: %u  min buffers: %u  max buffers: %u",
		outcaps,
		size,
		min,
		max
	);

	/* Inform the pool about the framebuffers */
	gst_fsl_vpu_buffer_pool_set_framebuffers(pool, vpu_dec->current_framebuffers);

	/* Now configure the pool. */
	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, outcaps, size, min, max);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_FSL_VPU_FRAMEBUFFER);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_set_config(pool, config);

	if (update_pool)
		gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
	else
		gst_query_add_allocation_pool(query, pool, size, min, max);

	if (pool != NULL)
		gst_object_unref(pool);

	return TRUE;
}

