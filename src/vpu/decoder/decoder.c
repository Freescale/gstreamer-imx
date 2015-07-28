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
#include "allocator.h"
#include "../mem_blocks.h"
#include "../common/phys_mem_meta.h"
#include "../utils.h"
#include "../fb_buffer_pool.h"



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
 * The main problem with the VPU's way of handling output buffers is the case where all framebuffers are occupied.
 * Then, the wrapper cannot pick a framebuffer to decode into, and decoding fails. This can easily happen if
 * the GStreamer pipeline uses queues and downstream is not consuming the frames fast enough for some reason.
 * To counter this effect, a condition variable is used, which causes the handle_frame() function to wait until
 * a certain number of buffers are available. (One buffer is not enough sometimes.) A counter is used (called
 * "num_available_framebuffers"). This counts the number of available framebuffers, and initially equals the
 * number of allocated framebuffers. Every time VPU_DecDecodeBuf() reports that a frame was consumed (NOTE: not
 * to be confused with "a frame was decoded"), the counter is decremented. If the handle_frame() function is
 * entered with a num_available_framebuffers value that is less than the required minimum (checked by
 * gst_imx_vpu_framebuffers_wait_until_frames_available() ), the decoder waits until the condition variable is
 * signaled. A release_buffer() implementation inside fb_buffer_pool.c incremens the counter, and signals the
 * condition. The condition is also signaled if for some other reason the decoder should no longer wait
 * (for example, when stopping, or when switching format).
 *
 * Two additional counters exist: decremented_availbuf_counter and num_framebuffers_in_buffers . The former
 * counts the times num_available_framebuffers has been decremented. This is necessary to make sure the
 * value of num_available_framebuffers is not decremented in the release_buffer vfunc (in fb_buffer_pool.c) unless
 * it is required.
 * num_framebuffers_in_buffers counts how many VPU framebuffers are currently inside GstBuffers and have not been
 * made available again by calling VPU_DecOutFrameDisplayed() yet. This counter is necessary to handle the case
 * when recalculate_num_avail_framebuffers is true. Then, the value of num_additional_framebuffers is calculated
 * this way:  num_additional_framebuffers = num_framebuffers - num_framebuffers_in_buffers , because this is how
 * the VPU wrapper internally operates: after VPU_DecFlushAll() and the subsequent VPU_DecDecodeBuf() call, the
 * wrapper will return all framebuffers to the free framebuffer pool except for the ones who contain output data
 * and have not been marked as displayed (= free) yet.
 *
 * (These counters would all not be necessary if it were possible to query the number of free framebuffers from
 * the VPU wrapper directly, but currently, no such function is present in the wrapper API, and without this
 * number, it is not possible to let the decoder wait until enough frames are free..)
 *
 * Currently, the minimum number of free output framebuffers is 6, meaning that the num_available_framebuffers
 * counter must always be at least at that value. Combined with the maximum number of frames h.264 could require
 * with frame reordering (which is 17 frames), this means that up to 23 frames will have to be allocated with the
 * physical memory allocators. For 1080p videos, this means (23*1920*1088*12/8 / 1048576) byte ~ 69 MB.
 * (1088, because the VPU needs width and height values to be aligned to 16-pixel boundaries, and 12/8, because
 * 12 bit is the bit depth for the I420 format, 12/8 is the number of bytes per pixel. While the decoder can also
 * output I42B and Y444 for motion JPEG, it won't use that many frames then, so 12 bit is still a good pick.)
 * Adding the typical sizes of extra decoding buffers requested by the VPU, this sums up to 72 MB.
 * Therefore, it is recommended to make sure at least 72 MB RAM are available for a decoder instance. If multiple
 * streams need to be decoded at the same time, each one must have up to 72 MB available. This ensures the
 * decoder instance(s) can handle any kind of input stream. (In special cases the RAM usage might be
 * substantially less of course.)
 */




GST_DEBUG_CATEGORY_STATIC(imx_vpu_dec_debug);
#define GST_CAT_DEFAULT imx_vpu_dec_debug


enum
{
	PROP_0,
	PROP_NUM_ADDITIONAL_FRAMEBUFFERS
};


#define DEFAULT_NUM_ADDITIONAL_FRAMEBUFFERS 0


#define ALIGN_VAL_TO(LENGTH, ALIGN_SIZE)  ( ((guintptr)((LENGTH) + (ALIGN_SIZE) - 1) / (ALIGN_SIZE)) * (ALIGN_SIZE) )



static GMutex inst_counter_mutex;
static int inst_counter = 0;



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
		"variant = (string) itu; "

		/* VPU_V_MJPG */
		"image/jpeg; "

		/* VPU_V_VC1_AP and VPU_V_VC1 */
		/* WVC1 = VC1-AP (VPU_V_VC1_AP) */
		/* WMV3 = VC1-SPMP (VPU_V_VC1) */
		"video/x-wmv, "
		"wmvversion = (int) 3, "
		"format = (string) { WVC1, WMV3 }; "

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
		"format = (string) { I420, I42B, Y444 }, "
		"width = (int) [ 16, MAX ], "
		"height = (int) [ 16, MAX ], "
		"framerate = (fraction) [ 0, MAX ], "
		"interlace-mode = { progressive, interleaved } "
	)
);


G_DEFINE_TYPE(GstImxVpuDec, gst_imx_vpu_dec, GST_TYPE_VIDEO_DECODER)


/* miscellaneous functions */
static gboolean gst_imx_vpu_dec_alloc_dec_mem_blocks(GstImxVpuDec *vpu_dec);
static gboolean gst_imx_vpu_dec_free_dec_mem_blocks(GstImxVpuDec *vpu_dec);
static gboolean gst_imx_vpu_dec_fill_param_set(GstImxVpuDec *vpu_dec, GstVideoCodecState *state, VpuDecOpenParam *open_param, GstBuffer **codec_data);
static void gst_imx_vpu_dec_close_decoder(GstImxVpuDec *vpu_dec);
static void gst_imx_vpu_dec_add_gst_frame(GstImxVpuDec *vpu_dec, GstVideoCodecFrame *frame);
static void gst_imx_vpu_dec_remove_gst_frame(GstImxVpuDec *vpu_dec, GstVideoCodecFrame *frame);
static void gst_imx_vpu_dec_clear_gst_frames(GstImxVpuDec *vpu_dec);

/* functions for the base class */
static gboolean gst_imx_vpu_dec_start(GstVideoDecoder *decoder);
static gboolean gst_imx_vpu_dec_stop(GstVideoDecoder *decoder);
static gboolean gst_imx_vpu_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state);
static GstFlowReturn gst_imx_vpu_dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *frame);
static gboolean gst_imx_vpu_dec_flush(GstVideoDecoder *decoder);
static GstFlowReturn gst_imx_vpu_dec_finish(GstVideoDecoder *decoder);
static gboolean gst_imx_vpu_dec_decide_allocation(GstVideoDecoder *decoder, GstQuery *query);

static void gst_imx_vpu_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_imx_vpu_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* function for decoder state changes */
static GstStateChangeReturn gst_imx_vpu_dec_change_state (GstElement *element, GstStateChange transition);



/* required function declared by G_DEFINE_TYPE */

void gst_imx_vpu_dec_class_init(GstImxVpuDecClass *klass)
{
	GObjectClass *object_class;
	GstVideoDecoderClass *base_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_dec_debug, "imxvpudec", 0, "Freescale i.MX VPU video decoder");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_VIDEO_DECODER_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_src_template));

	object_class->set_property    = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_set_property);
	object_class->get_property    = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_get_property);

	base_class->start             = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_start);
	base_class->stop              = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_stop);
	base_class->set_format        = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_set_format);
	base_class->handle_frame      = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_handle_frame);
	base_class->flush             = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_flush);
	base_class->finish            = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_finish);
	base_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_decide_allocation);

	element_class->change_state   = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_change_state);

	g_object_class_install_property(
		object_class,
		PROP_NUM_ADDITIONAL_FRAMEBUFFERS,
		g_param_spec_uint(
			"num-additional-framebuffers",
			"Number of additional output framebuffers",
			"Number of output framebuffers to allocate for decoding in addition to the minimum number indicated by the VPU and the necessary number of free buffers",
			0, 32767,
			DEFAULT_NUM_ADDITIONAL_FRAMEBUFFERS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale VPU video decoder",
		"Codec/Decoder/Video",
		"hardware-accelerated video decoding using the Freescale VPU engine",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);
}


void gst_imx_vpu_dec_init(GstImxVpuDec *vpu_dec)
{
	vpu_dec->vpu_inst_opened = FALSE;

	vpu_dec->codec_data = NULL;
	vpu_dec->allocator = NULL;
	vpu_dec->current_framebuffers = NULL;
	vpu_dec->num_additional_framebuffers = DEFAULT_NUM_ADDITIONAL_FRAMEBUFFERS;
	vpu_dec->recalculate_num_avail_framebuffers = FALSE;
	vpu_dec->current_output_state = NULL;

	vpu_dec->virt_dec_mem_blocks = NULL;
	vpu_dec->phys_dec_mem_blocks = NULL;

	vpu_dec->frame_table = NULL;
}




/***************************/
/* miscellaneous functions */

gboolean gst_imx_vpu_dec_load(void)
{
	VpuDecRetCode ret;

#define VPUINIT_ERR(RET, DESC, UNLOAD) \
	if ((RET) != VPU_DEC_RET_SUCCESS) \
	{ \
		g_mutex_unlock(&inst_counter_mutex); \
		GST_ERROR("%s: %s", (DESC), gst_imx_vpu_strerror(RET)); \
		if (UNLOAD) \
			VPU_DecUnLoad(); \
		return FALSE; \
	}

	g_mutex_lock(&inst_counter_mutex);
	if (inst_counter == 0)
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

			GST_INFO("VPU loaded");
			GST_INFO("VPU firmware version %d.%d.%d_r%d", version.nFwMajor, version.nFwMinor, version.nFwRelease, version.nFwCode);
			GST_INFO("VPU library version %d.%d.%d", version.nLibMajor, version.nLibMinor, version.nLibRelease);
			GST_INFO("VPU wrapper version %d.%d.%d %s", wrapper_version.nMajor, wrapper_version.nMinor, wrapper_version.nRelease, wrapper_version.pBinary);
		}
	}
	++inst_counter;
	g_mutex_unlock(&inst_counter_mutex);

#undef VPUINIT_ERR

	return TRUE;
}


void gst_imx_vpu_dec_unload(void)
{
	VpuDecRetCode ret;

	g_mutex_lock(&inst_counter_mutex);
	if (inst_counter > 0)
	{
		--inst_counter;
		if (inst_counter == 0)
		{
			ret = VPU_DecUnLoad();
			if (ret != VPU_DEC_RET_SUCCESS)
			{
				GST_ERROR("unloading VPU failed: %s", gst_imx_vpu_strerror(ret));
			}
			else
				GST_INFO("VPU unloaded");
		}
	}
	g_mutex_unlock(&inst_counter_mutex);
}


static gboolean gst_imx_vpu_dec_alloc_dec_mem_blocks(GstImxVpuDec *vpu_dec)
{
	int i;
	int size;
	unsigned char *ptr;

	GST_INFO_OBJECT(vpu_dec, "need to allocate %d sub blocks for decoding", vpu_dec->mem_info.nSubBlockNum);
	for (i = 0; i < vpu_dec->mem_info.nSubBlockNum; ++i)
 	{
		size = vpu_dec->mem_info.MemSubBlock[i].nAlignment + vpu_dec->mem_info.MemSubBlock[i].nSize;
		GST_INFO_OBJECT(vpu_dec, "sub block %d  type: %s  size: %d", i, (vpu_dec->mem_info.MemSubBlock[i].MemType == VPU_MEM_VIRT) ? "virtual" : "physical", size);
 
		if (vpu_dec->mem_info.MemSubBlock[i].MemType == VPU_MEM_VIRT)
		{
			if (!gst_imx_vpu_alloc_virt_mem_block(&ptr, size))
				return FALSE;

			vpu_dec->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO(ptr, vpu_dec->mem_info.MemSubBlock[i].nAlignment);

			gst_imx_vpu_append_virt_mem_block(ptr, &(vpu_dec->virt_dec_mem_blocks));
		}
		else if (vpu_dec->mem_info.MemSubBlock[i].MemType == VPU_MEM_PHY)
		{
			GstImxPhysMemory *memory = (GstImxPhysMemory *)gst_allocator_alloc(vpu_dec->allocator, size, NULL);
			if (memory == NULL)
				return FALSE;

			/* it is OK to use mapped_virt_addr directly without explicit mapping here,
			 * since the VPU decoder allocation functions define a virtual address upon
			 * allocation, so an actual "mapping" does not exist (map just returns
			 * mapped_virt_addr, unmap does nothing) */
			vpu_dec->mem_info.MemSubBlock[i].pVirtAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(memory->mapped_virt_addr), vpu_dec->mem_info.MemSubBlock[i].nAlignment);
			vpu_dec->mem_info.MemSubBlock[i].pPhyAddr = (unsigned char *)ALIGN_VAL_TO((unsigned char*)(memory->phys_addr), vpu_dec->mem_info.MemSubBlock[i].nAlignment);

			gst_imx_vpu_append_phys_mem_block(memory, &(vpu_dec->phys_dec_mem_blocks));
		}
		else
		{
			GST_WARNING_OBJECT(vpu_dec, "type of sub block %d is unknown - skipping", i);
		}
 	}

	return TRUE;
}


static gboolean gst_imx_vpu_dec_free_dec_mem_blocks(GstImxVpuDec *vpu_dec)
{
	gboolean ret1, ret2;
	/* NOT using the two calls with && directly, since otherwise an early exit could happen; in other words,
	 * if the first call failed, the second one wouldn't even be invoked
	 * doing the logical AND afterwards fixes this */
	ret1 = gst_imx_vpu_free_virt_mem_blocks(&(vpu_dec->virt_dec_mem_blocks));
	ret2 = gst_imx_vpu_free_phys_mem_blocks((GstImxPhysMemAllocator *)(vpu_dec->allocator), &(vpu_dec->phys_dec_mem_blocks));
	return ret1 && ret2;
}


static gboolean gst_imx_vpu_dec_fill_param_set(GstImxVpuDec *vpu_dec, GstVideoCodecState *state, VpuDecOpenParam *open_param, GstBuffer **codec_data)
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
			vpu_dec->use_vpuwrapper_flush_call = TRUE;
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
			vpu_dec->use_vpuwrapper_flush_call = TRUE;
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
			vpu_dec->use_vpuwrapper_flush_call = TRUE;
		}
		else if (g_strcmp0(name, "video/x-xvid") == 0)
		{
			open_param->CodecFormat = VPU_V_XVID;
			vpu_dec->use_vpuwrapper_flush_call = TRUE;
			GST_INFO_OBJECT(vpu_dec, "setting xvid as stream format");
		}
		else if (g_strcmp0(name, "video/x-h263") == 0)
		{
			open_param->CodecFormat = VPU_V_H263;
			vpu_dec->use_vpuwrapper_flush_call = FALSE;
			vpu_dec->no_explicit_frame_boundary = TRUE;
			GST_INFO_OBJECT(vpu_dec, "setting h.263 as stream format");
		}
		else if (g_strcmp0(name, "image/jpeg") == 0)
		{
			open_param->CodecFormat = VPU_V_MJPG;
			vpu_dec->use_vpuwrapper_flush_call = TRUE;
			vpu_dec->no_explicit_frame_boundary = TRUE;
			GST_INFO_OBJECT(vpu_dec, "setting motion JPEG as stream format");
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
			vpu_dec->use_vpuwrapper_flush_call = FALSE;
			vpu_dec->no_explicit_frame_boundary = TRUE;
		}
		else if (g_strcmp0(name, "video/x-vp8") == 0)
		{
			open_param->CodecFormat = VPU_V_VP8;
			vpu_dec->use_vpuwrapper_flush_call = TRUE;
			vpu_dec->no_explicit_frame_boundary = TRUE;
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

	vpu_dec->codec_format = open_param->CodecFormat;

	return TRUE;
}


static void gst_imx_vpu_dec_close_decoder(GstImxVpuDec *vpu_dec)
{
	VpuDecRetCode dec_ret;

	if (vpu_dec->vpu_inst_opened)
	{
		dec_ret = VPU_DecFlushAll(vpu_dec->handle);
		if (dec_ret == VPU_DEC_RET_FAILURE_TIMEOUT)
		{
			GST_WARNING_OBJECT(vpu_dec, "resetting decoder after a timeout occurred");
			dec_ret = VPU_DecReset(vpu_dec->handle);
			if (dec_ret != VPU_DEC_RET_SUCCESS)
				GST_ERROR_OBJECT(vpu_dec, "resetting decoder failed: %s", gst_imx_vpu_strerror(dec_ret));
		}
		else if (dec_ret != VPU_DEC_RET_SUCCESS)
			GST_ERROR_OBJECT(vpu_dec, "flushing decoder failed: %s", gst_imx_vpu_strerror(dec_ret));

		dec_ret = VPU_DecClose(vpu_dec->handle);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
			GST_ERROR_OBJECT(vpu_dec, "closing decoder failed: %s", gst_imx_vpu_strerror(dec_ret));

		GST_INFO_OBJECT(vpu_dec, "VPU decoder closed");

		vpu_dec->vpu_inst_opened = FALSE;
	}
}


static void gst_imx_vpu_dec_add_gst_frame(GstImxVpuDec *vpu_dec, GstVideoCodecFrame *frame)
{
	g_assert(vpu_dec->gst_frame_table != NULL);
	g_hash_table_add(vpu_dec->gst_frame_table, frame);
}


static void gst_imx_vpu_dec_remove_gst_frame(GstImxVpuDec *vpu_dec, GstVideoCodecFrame *frame)
{
	g_assert(vpu_dec->gst_frame_table != NULL);
	g_hash_table_remove(vpu_dec->gst_frame_table, frame);
}


static void gst_imx_vpu_dec_clear_gst_frames(GstImxVpuDec *vpu_dec)
{
	GHashTableIter iter;
	gpointer key;

	GST_DEBUG_OBJECT(vpu_dec, "clearing %u frames", g_hash_table_size(vpu_dec->gst_frame_table));

	g_hash_table_iter_init(&iter, vpu_dec->gst_frame_table);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		GstVideoCodecFrame *frame = (GstVideoCodecFrame *)key;
		if (frame != NULL)
			gst_video_decoder_release_frame(GST_VIDEO_DECODER(vpu_dec), frame);
	}

	g_hash_table_remove_all(vpu_dec->gst_frame_table);
}




/********************************/
/* functions for the base class */

static gboolean gst_imx_vpu_dec_start(GstVideoDecoder *decoder)
{
	VpuDecRetCode ret;
	GstImxVpuDec *vpu_dec;

	vpu_dec = GST_IMX_VPU_DEC(decoder);

	GST_INFO_OBJECT(vpu_dec, "starting VPU decoder");

	if (!gst_imx_vpu_dec_load())
		return FALSE;

	/* mem_info contains information about how to set up memory blocks
	 * the VPU uses as temporary storage (they are "work buffers") */
	memset(&(vpu_dec->mem_info), 0, sizeof(VpuMemInfo));
	ret = VPU_DecQueryMem(&(vpu_dec->mem_info));
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not get VPU memory information: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	vpu_dec->frame_table = g_hash_table_new(NULL, NULL);
	vpu_dec->gst_frame_table = g_hash_table_new(NULL, NULL);

	vpu_dec->allocator = gst_imx_vpu_dec_allocator_new();

	/* Allocate the work buffers
	 * Note that these are independent of decoder instances, so they
	 * are allocated before the VPU_DecOpen() call, and are not
	 * recreated in set_format */
	if (!gst_imx_vpu_dec_alloc_dec_mem_blocks(vpu_dec))
		return FALSE;

	/* The decoder is initialized in set_format, not here, since only then the input bitstream
	 * format is known (and this information is necessary for initialization). */

	GST_INFO_OBJECT(vpu_dec, "VPU decoder started");

	return TRUE;
}


static gboolean gst_imx_vpu_dec_stop(GstVideoDecoder *decoder)
{
	gboolean ret;
	GstImxVpuDec *vpu_dec;

	ret = TRUE;

	vpu_dec = GST_IMX_VPU_DEC(decoder);

	if (vpu_dec->gst_frame_table != NULL)
	{
		gst_imx_vpu_dec_clear_gst_frames(vpu_dec);
		g_hash_table_destroy(vpu_dec->gst_frame_table);
		vpu_dec->gst_frame_table = NULL;
	}

	if (vpu_dec->current_framebuffers != NULL)
	{
		GST_INFO_OBJECT(decoder, "Setting flushing flag of framebuffers object during stop call");

		/* Using mutexes here to prevent race conditions when decoder_open is set to
		 * FALSE at the same time as it is checked in the buffer pool release() function */
		GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);
		gst_imx_vpu_framebuffers_set_flushing(vpu_dec->current_framebuffers, TRUE);
		vpu_dec->current_framebuffers->decenc_states.dec.decoder_open = FALSE;
		GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);

		gst_object_unref(vpu_dec->current_framebuffers);
		vpu_dec->current_framebuffers = NULL;
	}

	gst_imx_vpu_dec_close_decoder(vpu_dec);
	gst_imx_vpu_dec_free_dec_mem_blocks(vpu_dec);

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

	if (vpu_dec->allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(vpu_dec->allocator));
		vpu_dec->allocator = NULL;
	}

	if (vpu_dec->frame_table != NULL)
	{
		g_hash_table_destroy(vpu_dec->frame_table);
		vpu_dec->frame_table = NULL;
	}

	GST_INFO_OBJECT(vpu_dec, "VPU decoder stopped");

	gst_imx_vpu_dec_unload();

	return ret;
}


static gboolean gst_imx_vpu_dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state)
{
	VpuDecRetCode ret;
	VpuDecOpenParam open_param;
	int config_param;
	GstBuffer *codec_data = NULL;
	GstImxVpuDec *vpu_dec = GST_IMX_VPU_DEC(decoder);

	GST_INFO_OBJECT(decoder, "setting decoder format");

	/* Output frames that are already decoded but not yet displayed */
	GST_INFO_OBJECT(decoder, "draining remaining frames from decoder");
	gst_imx_vpu_dec_finish(decoder);

	/* Clean up existing framebuffers structure;
	 * if some previous and still existing buffer pools depend on this framebuffers
	 * structure, they will extend its lifetime, since they ref'd it
	 */
	if (vpu_dec->current_framebuffers != NULL)
	{
		GST_INFO_OBJECT(decoder, "cleaning up existing framebuffers structure");

		/* Using mutexes here to prevent race conditions when decoder_open is set to
		 * FALSE at the same time as it is checked in the buffer pool release() function */
		GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);
		gst_imx_vpu_framebuffers_set_flushing(vpu_dec->current_framebuffers, TRUE);
		vpu_dec->current_framebuffers->decenc_states.dec.decoder_open = FALSE;
		GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);

		gst_object_unref(vpu_dec->current_framebuffers);
		vpu_dec->current_framebuffers = NULL;
	}

	/* Clean up old codec data copy */
	if (vpu_dec->codec_data != NULL)
	{
		GST_INFO_OBJECT(decoder, "cleaning up existing codec data");

		gst_buffer_unref(vpu_dec->codec_data);
		vpu_dec->codec_data = NULL;
	}

	/* Clean up old output state */
	if (vpu_dec->current_output_state != NULL)
	{
		GST_INFO_OBJECT(decoder, "cleaning up existing output state");

		gst_video_codec_state_unref(vpu_dec->current_output_state);
		vpu_dec->current_output_state = NULL;
	}

	/* Close old decoder instance */
	gst_imx_vpu_dec_close_decoder(vpu_dec);

	memset(&open_param, 0, sizeof(open_param));

	/* codec_data does not need to be unref'd after use; it is owned by the caps structure */
	if (!gst_imx_vpu_dec_fill_param_set(vpu_dec, state, &open_param, &codec_data))
	{
		GST_ERROR_OBJECT(vpu_dec, "could not fill open params: state info incompatible");
		return FALSE;
	}
	vpu_dec->is_mjpeg = (open_param.CodecFormat == VPU_V_MJPG);

	/* The actual initialization; requires bitstream information (such as the codec type), which
	 * is determined by the fill_param_set call before */
	ret = VPU_DecOpen(&(vpu_dec->handle), &open_param, &(vpu_dec->mem_info));
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "opening new VPU handle failed: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	vpu_dec->vpu_inst_opened = TRUE;

	/* configure AFTER setting vpu_inst_opened to TRUE, to make sure that in case of
	   config failure the VPU handle is closed in the finalizer */

	config_param = VPU_DEC_SKIPNONE;
	ret = VPU_DecConfig(vpu_dec->handle, VPU_DEC_CONF_SKIPMODE, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not configure skip mode: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	config_param = 0;
	ret = VPU_DecConfig(vpu_dec->handle, VPU_DEC_CONF_BUFDELAY, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not configure buffer delay: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	config_param = VPU_DEC_IN_NORMAL;
	ret = VPU_DecConfig(vpu_dec->handle, VPU_DEC_CONF_INPUTTYPE, &config_param);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "could not configure input type: %s", gst_imx_vpu_strerror(ret));
		return FALSE;
	}

	/* Ref the output state, to be able to add information from the init_info structure to it later */
	vpu_dec->current_output_state = gst_video_codec_state_ref(state);

	/* Copy the buffer, to make sure the codec_data lifetime does not depend on the caps */
	if (codec_data != NULL)
		vpu_dec->codec_data = gst_buffer_copy(codec_data);

	GST_INFO_OBJECT(decoder, "setting format finished");

	return TRUE;
}


static GstFlowReturn gst_imx_vpu_dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *cur_frame)
{
	int buffer_ret_code;
	VpuDecRetCode dec_ret;
	VpuBufferNode in_data;
	GstMapInfo in_map_info;
	GstMapInfo codecdata_map_info;
	GstImxVpuDec *vpu_dec;

	vpu_dec = GST_IMX_VPU_DEC(decoder);

	gst_imx_vpu_dec_add_gst_frame(vpu_dec, cur_frame);

	memset(&in_data, 0, sizeof(in_data));

	if (cur_frame != NULL)
	{
		gst_buffer_map(cur_frame->input_buffer, &in_map_info, GST_MAP_READ);

		in_data.pPhyAddr = NULL;
		in_data.pVirAddr = (unsigned char *)(in_map_info.data);
		in_data.nSize = in_map_info.size;
	}

	/* cur_frame is NULL if handle_frame() is being called inside finish(); in other words,
	 * when the decoder is shutting down, and output frames are being flushed.
	 * This requires the decoder output mode to have been set to DRAIN before, which is
	 * done in finish(). */

	if (vpu_dec->codec_data != NULL)
	{
		gst_buffer_map(vpu_dec->codec_data, &codecdata_map_info, GST_MAP_READ);
		in_data.sCodecData.pData = codecdata_map_info.data;
		in_data.sCodecData.nSize = codecdata_map_info.size;
		GST_LOG_OBJECT(vpu_dec, "setting extra codec data (%d byte)", codecdata_map_info.size);
	}

	/* Using a mutex here, since the VPU_DecDecodeBuf() call internally picks an
	 * available framebuffer, and at the same time, the bufferpool release() function
	 * might be returning a framebuffer to the list of available ones */
	if (vpu_dec->current_framebuffers != NULL)
	{
		GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);
		dec_ret = VPU_DecDecodeBuf(vpu_dec->handle, &in_data, &buffer_ret_code);
		if (vpu_dec->recalculate_num_avail_framebuffers)
		{
			vpu_dec->current_framebuffers->num_available_framebuffers = vpu_dec->current_framebuffers->num_framebuffers - vpu_dec->current_framebuffers->num_framebuffers_in_buffers;
			vpu_dec->recalculate_num_avail_framebuffers = FALSE;
		}
		GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
	}
	else
		dec_ret = VPU_DecDecodeBuf(vpu_dec->handle, &in_data, &buffer_ret_code);

	if (dec_ret != VPU_DEC_RET_SUCCESS)
	{
		GST_ERROR_OBJECT(vpu_dec, "failed to decode frame: %s", gst_imx_vpu_strerror(dec_ret));
		return GST_FLOW_ERROR;
	}

	GST_LOG_OBJECT(vpu_dec, "VPU_DecDecodeBuf returns: %x", buffer_ret_code);

	/* Cleanup temporary input frame and codec data mapping */
	if (cur_frame != NULL)
		gst_buffer_unmap(cur_frame->input_buffer, &in_map_info);
	if (vpu_dec->codec_data != NULL)
		gst_buffer_unmap(vpu_dec->codec_data, &codecdata_map_info);

	if (buffer_ret_code & VPU_DEC_INIT_OK)
	{
		GstVideoFormat fmt;

		dec_ret = VPU_DecGetInitialInfo(vpu_dec->handle, &(vpu_dec->init_info));
		if (dec_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "could not get init info: %s", gst_imx_vpu_strerror(dec_ret));
			return GST_FLOW_ERROR;
		}

		if (vpu_dec->is_mjpeg)
		{
			switch (vpu_dec->init_info.nMjpgSourceFormat)
			{
				case 0: fmt = GST_VIDEO_FORMAT_I420; break;
				case 1: fmt = GST_VIDEO_FORMAT_Y42B; break;
				/* XXX: case 2 would be "4:2:2 vertical" - what is this supposed to be in GStreamer? */
				case 3: fmt = GST_VIDEO_FORMAT_Y444; break;
				case 4: fmt = GST_VIDEO_FORMAT_GRAY8; break;
				default:
					GST_ERROR_OBJECT(vpu_dec, "unsupported MJPEG output format %d", vpu_dec->init_info.nMjpgSourceFormat);
					return GST_FLOW_ERROR;
			}
		}
		else
			fmt = GST_VIDEO_FORMAT_I420;

		GST_LOG_OBJECT(vpu_dec, "using %s as video output format", gst_video_format_to_string(fmt));

		/* Allocate and register a new set of framebuffers for decoding
		 * This point is always reached after set_format() was called,
		 * and always before a frame is output */
		{
			guint min_fbcount_indicated_by_vpu;
			GstImxVpuFramebufferParams fbparams;
			gst_imx_vpu_framebuffers_dec_init_info_to_params(&(vpu_dec->init_info), &fbparams);

			min_fbcount_indicated_by_vpu = (guint)(fbparams.min_framebuffer_count);

			fbparams.min_framebuffer_count = min_fbcount_indicated_by_vpu + GST_IMX_VPU_MIN_NUM_FREE_FRAMEBUFFERS + vpu_dec->num_additional_framebuffers;
			GST_INFO_OBJECT(vpu_dec, "minimum number of framebuffers indicated by the VPU: %u  chosen number: %u", min_fbcount_indicated_by_vpu, fbparams.min_framebuffer_count);
			GST_INFO_OBJECT(vpu_dec, "interlacing: %d", vpu_dec->init_info.nInterlace);

			vpu_dec->current_framebuffers = gst_imx_vpu_framebuffers_new(&fbparams, vpu_dec->allocator);
			if (vpu_dec->current_framebuffers == NULL)
				return GST_FLOW_ERROR;

			if (!gst_imx_vpu_framebuffers_register_with_decoder(vpu_dec->current_framebuffers, vpu_dec->handle))
				return GST_FLOW_ERROR;
		}

		/* Add information from init_info to the output state and set it to be the output state for this decoder */
		if (vpu_dec->current_output_state != NULL)
		{
			GstVideoCodecState *state = vpu_dec->current_output_state;

			/* In some corner cases, width & height are not set in the input caps. If this happens, use the
			 * width & height from the current_framebuffers object that was initialized earlier. It receives
			 * width and height information from the bitstream itself (through the init_info structure). */
			if (state->info.width == 0)
			{
				state->info.width = vpu_dec->current_framebuffers->pic_width;
				GST_INFO_OBJECT(vpu_dec, "output state width is 0 - using the value %u from the framebuffers object instead", state->info.width);
			}
			if (state->info.height == 0)
			{
				state->info.height = vpu_dec->current_framebuffers->pic_height;
				GST_INFO_OBJECT(vpu_dec, "output state height is 0 - using the value %u from the framebuffers object instead", state->info.height);
			}

			GST_VIDEO_INFO_INTERLACE_MODE(&(state->info)) = vpu_dec->init_info.nInterlace ? GST_VIDEO_INTERLACE_MODE_INTERLEAVED : GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
			gst_video_decoder_set_output_state(decoder, fmt, state->info.width, state->info.height, state);
			gst_video_codec_state_unref(vpu_dec->current_output_state);

			vpu_dec->current_output_state = NULL;
		}

		vpu_dec->delay_sys_frame_numbers = TRUE;
		vpu_dec->last_sys_frame_number = cur_frame->system_frame_number;
	}

	if (buffer_ret_code & VPU_DEC_FLUSH)
	{
		dec_ret = VPU_DecFlushAll(vpu_dec->handle);

		if (dec_ret == VPU_DEC_RET_FAILURE_TIMEOUT)
		{
			GST_WARNING_OBJECT(vpu_dec, "resetting decoder after a timeout occurred");
			dec_ret = VPU_DecReset(vpu_dec->handle);
			if (dec_ret != VPU_DEC_RET_SUCCESS)
			{
				GST_ERROR_OBJECT(vpu_dec, "resetting decoder failed: %s", gst_imx_vpu_strerror(dec_ret));
				return GST_FLOW_ERROR;
			}
		}
		else if (dec_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "flushing VPU failed: %s", gst_imx_vpu_strerror(dec_ret));
			return GST_FLOW_ERROR;
		}

		return GST_FLOW_OK;
	}

	if (buffer_ret_code & VPU_DEC_NO_ENOUGH_INBUF)
	{
		/* Not dropping frame here on purpose; the next input frame may
		 * complete the input */
		GST_DEBUG_OBJECT(vpu_dec, "need more input");
		if ((cur_frame != NULL) && vpu_dec->delay_sys_frame_numbers)
			vpu_dec->last_sys_frame_number = cur_frame->system_frame_number;
		return GST_FLOW_OK;
	}

	if (vpu_dec->current_framebuffers == NULL)
	{
		GST_ERROR_OBJECT(vpu_dec, "no framebuffers allocated");
		return GST_FLOW_ERROR;
	}

	/* The following code block may cause a race condition if not synchronized;
	 * the buffer pool release() function must not run at the same time */
	{
		GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);

		if (buffer_ret_code & VPU_DEC_ONE_FRM_CONSUMED)
		{
			VpuDecFrameLengthInfo dec_framelen_info;
			gint frame_number;

			dec_ret = VPU_DecGetConsumedFrameInfo(vpu_dec->handle, &dec_framelen_info);
			if (dec_ret != VPU_DEC_RET_SUCCESS)
				GST_ERROR_OBJECT(vpu_dec, "could not get information about consumed frame: %s", gst_imx_vpu_strerror(dec_ret));

			if (vpu_dec->delay_sys_frame_numbers)
				frame_number = vpu_dec->last_sys_frame_number;
			else if (cur_frame != NULL)
				frame_number = cur_frame->system_frame_number;
			else
				frame_number = -1;

			if (cur_frame != NULL)
				GST_LOG_OBJECT(vpu_dec, "one frame got consumed: cur_frame: %p  framebuffer: %p  system frame number: %u  stuff length: %d  frame length: %d", (gpointer)cur_frame, (gpointer)(dec_framelen_info.pFrame), frame_number, dec_framelen_info.nStuffLength, dec_framelen_info.nFrameLength);
			else if (frame_number != -1)
				GST_LOG_OBJECT(vpu_dec, "one frame got consumed: (no cur_frame)  framebuffer: %p  system frame number: %u  stuff length: %d  frame length: %d", (gpointer)(dec_framelen_info.pFrame), frame_number, dec_framelen_info.nStuffLength, dec_framelen_info.nFrameLength);
			else
				GST_LOG_OBJECT(vpu_dec, "one frame got consumed: (no cur_frame)  framebuffer: %p  (no system frame number)  stuff length: %d  frame length: %d", (gpointer)(dec_framelen_info.pFrame), dec_framelen_info.nStuffLength, dec_framelen_info.nFrameLength);

			/* Association of input and output frames is not always straightforward.
			 * If frame reordering or a significant delay is present, then input frames might
			 * be finished much later. GStreamer provides system frame numbers for this purpose;
			 * they allow for associating an index with an unfinished frame. The idea is to somehow
			 * stuff this index  into the decoder's output buffers, and once they get filled with
			 * data and are ready for display, the decoder can retrieve the associated unfinished
			 * frame and finish it.
			 * Unfortunately, the VPU wrapper API doesn't allow to associate extra data with the
			 * output framebuffer structure. Therefore, a trick is used: the output framebuffer that
			 * gets consumed after the decoder is given input data is the one where the corresponding
			 * decoded frame will end up. Therefore, a hash table is used, which uses the framebuffer's
			 * address as key, and the frame number as value. When the VPU wrapper reports a frame as
			 * available for display, the associated frame number is looked up in this table. */
			if (frame_number != -1)
				g_hash_table_replace(vpu_dec->frame_table, (gpointer)(dec_framelen_info.pFrame), (gpointer)(frame_number + 1));
		}

		/* If VPU_DEC_OUTPUT_DROPPED is set, then the internal counter will not be modified */
		if ((buffer_ret_code & VPU_DEC_ONE_FRM_CONSUMED) && !(buffer_ret_code & VPU_DEC_OUTPUT_DROPPED))
		{
			gint old_num_available_framebuffers = vpu_dec->current_framebuffers->num_available_framebuffers;

			/* wait until frames are available or until flushing occurs */
			gst_imx_vpu_framebuffers_wait_until_frames_available(vpu_dec->current_framebuffers);

			vpu_dec->current_framebuffers->num_available_framebuffers--;
			vpu_dec->current_framebuffers->decremented_availbuf_counter++;
			GST_LOG_OBJECT(vpu_dec, "number of available buffers: %d -> %d -> %d", old_num_available_framebuffers, vpu_dec->current_framebuffers->num_available_framebuffers + 1, vpu_dec->current_framebuffers->num_available_framebuffers);
		}

		/* Unlock the mutex; the subsequent steps are safe */
		GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
	}

	if (cur_frame != NULL)
		vpu_dec->last_sys_frame_number = cur_frame->system_frame_number;
	else
		vpu_dec->last_sys_frame_number = -1;

	if (buffer_ret_code & VPU_DEC_NO_ENOUGH_BUF)
		GST_WARNING_OBJECT(vpu_dec, "no free output frame available (ret code: 0x%X)", buffer_ret_code);

	if (buffer_ret_code & VPU_DEC_OUTPUT_NODIS)
	{
		if (vpu_dec->no_explicit_frame_boundary)
		{
			GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);

			/* wait until frames are available or until flushing occurs */
			gst_imx_vpu_framebuffers_wait_until_frames_available(vpu_dec->current_framebuffers);

			GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
		}

		if ((cur_frame != NULL) && (vpu_dec->codec_format == VPU_V_VP8))
		{
			/* With VP8 data, NODIS is returned for alternate reference frames, which
			 * are not supposed to be shown, only decoded */

			GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY(cur_frame);
			gst_imx_vpu_dec_remove_gst_frame(vpu_dec, cur_frame);
			gst_video_decoder_finish_frame(decoder, cur_frame);

			/* not unref'ing cur_frame here, since it hasn't been
			 * ref'd before (it has not been retrieved by using get_frame()
			 * or get_oldest_frame() here) */
		}
	}

	if (buffer_ret_code & VPU_DEC_OUTPUT_DIS)
	{
		GstBuffer *buffer;
		VpuDecOutFrameInfo out_frame_info;
		GstVideoCodecFrame *out_frame;
		guint32 out_system_frame_number;
		gboolean sys_frame_nr_valid;

		/* Retrieve the decoded frame */
		dec_ret = VPU_DecGetOutputFrame(vpu_dec->handle, &out_frame_info);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "could not get decoded output frame: %s", gst_imx_vpu_strerror(dec_ret));
			return GST_FLOW_ERROR;
		}

		if (vpu_dec->no_explicit_frame_boundary)
		{
			GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);

			/* wait until frames are available or until flushing occurs */
			gst_imx_vpu_framebuffers_wait_until_frames_available(vpu_dec->current_framebuffers);

			vpu_dec->current_framebuffers->num_available_framebuffers--;
			vpu_dec->current_framebuffers->decremented_availbuf_counter++;

			GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
		}

		out_system_frame_number = (guint32)(g_hash_table_lookup(vpu_dec->frame_table, (gpointer)(out_frame_info.pDisplayFrameBuf)));
		sys_frame_nr_valid = FALSE;
		if (vpu_dec->no_explicit_frame_boundary)
		{
			GST_LOG_OBJECT(vpu_dec, "not using system frame numbers with this bitstream format");
		}
		else
		{
			if (out_system_frame_number > 0)
			{
				g_hash_table_remove(vpu_dec->frame_table, (gpointer)(out_frame_info.pDisplayFrameBuf));

				out_system_frame_number--;
				out_frame = gst_video_decoder_get_frame(decoder, out_system_frame_number);
				if (out_frame != NULL)
				{
					GST_LOG_OBJECT(vpu_dec, "system frame number valid and corresponding frame is still pending");
					sys_frame_nr_valid = TRUE;
				}
				else
					GST_WARNING_OBJECT(vpu_dec, "valid system frame number present, but corresponding frame has been handled already");
			}
			else
				GST_LOG_OBJECT(vpu_dec, "display framebuffer is unknown -> no valid system frame number can be retrieved; assuming no reordering is done");
		}

		/* Create empty buffer */
		buffer = gst_video_decoder_allocate_output_buffer(decoder);
		/* ... and set its contents */
		if (!gst_imx_vpu_set_buffer_contents(buffer, vpu_dec->current_framebuffers, out_frame_info.pDisplayFrameBuf))
		{
			gst_buffer_unref(buffer);
			return GST_FLOW_ERROR;
		}

		/* The GST_BUFFER_FLAG_TAG_MEMORY flag will be set, because the
		 * buffer's memory was added after the buffer was acquired from
		 * the pool. (The fbbufferpool produces empty buffers.)
		 * However, at this point, the buffer is ready for use,
		 * so just remove that flag to prevent unnecessary copies.
		 * (new in GStreamer >= 1.3.1 */
#if GST_CHECK_VERSION(1, 3, 1)
		GST_BUFFER_FLAG_UNSET(buffer, GST_BUFFER_FLAG_TAG_MEMORY);
#endif

		if (sys_frame_nr_valid)
		{
			GST_LOG_OBJECT(vpu_dec, "output frame:  codecframe: %p  framebuffer phys addr: %" GST_IMX_PHYS_ADDR_FORMAT "  system frame number: %u  gstbuffer addr: %p  field type: %d  pic type: %d  Y stride: %d  CbCr stride: %d", (gpointer)out_frame, (gst_imx_phys_addr_t)(out_frame_info.pDisplayFrameBuf->pbufY), out_system_frame_number, (gpointer)buffer, out_frame_info.eFieldType, out_frame_info.ePicType, out_frame_info.pDisplayFrameBuf->nStrideY, out_frame_info.pDisplayFrameBuf->nStrideC);
		}
		else
		{
			GST_LOG_OBJECT(vpu_dec, "system frame number invalid or unusable - getting oldest pending frame instead");
			out_frame = gst_video_decoder_get_oldest_frame(decoder);

			GST_LOG_OBJECT(vpu_dec, "output frame:  codecframe: %p  framebuffer phys addr: %" GST_IMX_PHYS_ADDR_FORMAT "  system frame number: <none; oldest frame>  gstbuffer addr: %p  field type: %d  pic type: %d  Y stride: %d  CbCr stride: %d", (gpointer)out_frame, (gst_imx_phys_addr_t)(out_frame_info.pDisplayFrameBuf->pbufY), (gpointer)buffer, out_frame_info.eFieldType, out_frame_info.ePicType, out_frame_info.pDisplayFrameBuf->nStrideY, out_frame_info.pDisplayFrameBuf->nStrideC);
		}

		/* If a framebuffer is sent downstream directly, it will
		 * have to be marked later as displayed after it was used,
		 * to allow the VPU wrapper to reuse it for new decoded
		 * frames. Since this is a fresh frame, and it wasn't
		 * used yet, mark it now as undisplayed. */
		gst_imx_vpu_mark_buf_as_not_displayed(buffer);

		if (vpu_dec->init_info.nInterlace)
		{
			/* Specify field type for deinterlacing */
			switch (out_frame_info.eFieldType)
			{
				case VPU_FIELD_TOP:
					GST_LOG_OBJECT(vpu_dec, "interlaced picture, 1 field, top");
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_ONEFIELD);
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_TFF);
					break;

				case VPU_FIELD_BOTTOM:
					GST_LOG_OBJECT(vpu_dec, "interlaced picture, 1 field, bottom");
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_ONEFIELD);
					break;

				case VPU_FIELD_TB:
					GST_LOG_OBJECT(vpu_dec, "interlaced picture, 2 fields, top first");
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_TFF);
					break;

				case VPU_FIELD_BT:
					GST_LOG_OBJECT(vpu_dec, "interlaced picture, 2 fields, bottom first");
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
					break;

				default:
					GST_LOG_OBJECT(vpu_dec, "interlaced picture, undefined format (using default: 2 fields, bottom first)");
					GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
					break;
			}
		}

		if (out_frame != NULL)
		{
			gst_imx_vpu_dec_remove_gst_frame(vpu_dec, out_frame);

			/* Unref output frame, since get_frame() and get_oldest_frame() ref it */
			gst_video_codec_frame_unref(out_frame);

			out_frame->output_buffer = buffer;
			gst_video_decoder_finish_frame(decoder, out_frame);
		}
		else
		{
			/* In rare cases (mainly with VC-1), there may not be any frames left to handle while flushing
			 * If such a case occurs, just discard the output buffer, since it cannot be used anywhere */
			gst_buffer_unref(buffer);
		}
	}
	else if (buffer_ret_code & VPU_DEC_OUTPUT_MOSAIC_DIS)
	{
		/* XXX: mosaic frames do not seem to be useful for anything, so they are just dropped here */

		VpuDecOutFrameInfo out_frame_info;

		/* Retrieve the decoded frame */
		dec_ret = VPU_DecGetOutputFrame(vpu_dec->handle, &out_frame_info);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "could not get decoded output frame: %s", gst_imx_vpu_strerror(dec_ret));
			return GST_FLOW_ERROR;
		}

		GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);

		dec_ret = VPU_DecOutFrameDisplayed(vpu_dec->handle, out_frame_info.pDisplayFrameBuf);
		if (dec_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "clearing display framebuffer failed: %s", gst_imx_vpu_strerror(dec_ret));
			GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
			return GST_FLOW_ERROR;
		}

		vpu_dec->current_framebuffers->num_available_framebuffers++;
		GST_DEBUG_OBJECT(vpu_dec, "number of available buffers after dropping mosaic frame: %d -> %d", vpu_dec->current_framebuffers->num_available_framebuffers - 1, vpu_dec->current_framebuffers->num_available_framebuffers);
		GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
	}
	else if (buffer_ret_code & VPU_DEC_OUTPUT_DROPPED)
	{
		GstVideoCodecFrame *out_frame = gst_video_decoder_get_oldest_frame(decoder);
		gst_imx_vpu_dec_remove_gst_frame(vpu_dec, out_frame);
		gst_video_codec_frame_unref(out_frame);
		gst_video_decoder_drop_frame(decoder, out_frame);

		GST_DEBUG_OBJECT(vpu_dec, "VPU dropped output frame internally");
	}
	else
		GST_DEBUG_OBJECT(vpu_dec, "nothing to output (ret code: 0x%X)", buffer_ret_code);

	/* In case the VPU didn't use the input and no consumed frame info is available,
	 * drop the input frame to make sure timestamps are okay
	 * (If consumed frame info is present it is still possible it might be used for input-output frame
	 * associations; unlikely to occur thought) */
	if ((cur_frame != NULL) && !(buffer_ret_code & (VPU_DEC_ONE_FRM_CONSUMED | VPU_DEC_INPUT_USED)))
	{
		GST_DEBUG_OBJECT(vpu_dec, "VPU did not use input frame, and no consumed frame info available -> drop input frame");
		gst_imx_vpu_dec_remove_gst_frame(vpu_dec, cur_frame);
		gst_video_decoder_drop_frame(decoder, cur_frame);
	}


	return (buffer_ret_code & VPU_DEC_OUTPUT_EOS) ? GST_FLOW_EOS : GST_FLOW_OK;
}


static gboolean gst_imx_vpu_dec_flush(GstVideoDecoder *decoder)
{
	GstImxVpuDec *vpu_dec = GST_IMX_VPU_DEC(decoder);

	if (!vpu_dec->vpu_inst_opened)
		return TRUE;

	vpu_dec->delay_sys_frame_numbers = FALSE;

	if (vpu_dec->current_framebuffers != NULL)
	{
		VpuDecRetCode ret = VPU_DEC_RET_SUCCESS;

		GST_INFO_OBJECT(decoder, "flushing decoder");

		GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);

		gst_imx_vpu_framebuffers_exit_wait_loop(vpu_dec->current_framebuffers);
		g_cond_signal(&(vpu_dec->current_framebuffers->cond));

		if (vpu_dec->use_vpuwrapper_flush_call)
		{
			ret = VPU_DecFlushAll(vpu_dec->handle);

			if (ret == VPU_DEC_RET_FAILURE_TIMEOUT)
			{
				GST_WARNING_OBJECT(vpu_dec, "flushing decoder after a timeout occurred");
				ret = VPU_DecReset(vpu_dec->handle);
				if (ret != VPU_DEC_RET_SUCCESS)
					GST_ERROR_OBJECT(vpu_dec, "flushing decoder failed: %s", gst_imx_vpu_strerror(ret));
			}
			else if (ret != VPU_DEC_RET_SUCCESS)
				GST_ERROR_OBJECT(vpu_dec, "flushing VPU failed: %s", gst_imx_vpu_strerror(ret));

			vpu_dec->recalculate_num_avail_framebuffers = TRUE;
		}

		GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);

		if (ret != VPU_DEC_RET_SUCCESS)
		{
			GST_ERROR_OBJECT(vpu_dec, "flushing VPU failed: %s", gst_imx_vpu_strerror(ret));
			return FALSE;
		}
	}

	gst_imx_vpu_dec_clear_gst_frames(vpu_dec);

	return TRUE;
}


static GstFlowReturn gst_imx_vpu_dec_finish(GstVideoDecoder *decoder)
{
	GstImxVpuDec *vpu_dec = GST_IMX_VPU_DEC(decoder);

	if (!vpu_dec->vpu_inst_opened)
		return GST_FLOW_OK;

	/* need to flush any output framebuffers present inside the VPU */
	if (vpu_dec->current_framebuffers != NULL)
	{
		int config_param;
		VpuDecRetCode vpu_ret;

		GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);

		/* first, set the output mode to DRAIN, to instruct the VPU to
		 * flush output framebuffers and not expect any more input */
		GST_INFO_OBJECT(vpu_dec, "setting VPU decoder in drain mode");
		config_param = VPU_DEC_IN_DRAIN;
		vpu_ret = VPU_DecConfig(vpu_dec->handle, VPU_DEC_CONF_INPUTTYPE, &config_param);

		if (vpu_ret != VPU_DEC_RET_SUCCESS)
		{
			GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
			GST_ERROR_OBJECT(vpu_dec, "could not configure skip mode: %s", gst_imx_vpu_strerror(vpu_ret));
			return GST_FLOW_ERROR;
		}
		else
		{
			gst_imx_vpu_framebuffers_set_flushing(vpu_dec->current_framebuffers, TRUE);

			GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);

			/* get as many output frames as possible, until the decoder reports the EOS */
			GST_INFO_OBJECT(vpu_dec, "pushing out all remaining unfinished frames");
			while (TRUE)
			{
				GstFlowReturn flow_ret = gst_imx_vpu_dec_handle_frame(decoder, NULL);
				if (flow_ret == GST_FLOW_EOS)
				{
					GST_INFO_OBJECT(vpu_dec, "last remaining unfinished frame pushed");
					break;
				}
				else
					GST_LOG_OBJECT(vpu_dec, "unfinished frame pushed, others remain");
			}
		}
	}

	return GST_FLOW_OK;
}


static gboolean gst_imx_vpu_dec_decide_allocation(GstVideoDecoder *decoder, GstQuery *query)
{
	GstImxVpuDec *vpu_dec = GST_IMX_VPU_DEC(decoder);
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

	GST_INFO_OBJECT(decoder, "number of allocation pools in query: %d", gst_query_get_n_allocation_pools(query));

	/* Look for an allocator which can allocate VPU DMA buffers */
	if (gst_query_get_n_allocation_pools(query) > 0)
	{
		for (guint i = 0; i < gst_query_get_n_allocation_pools(query); ++i)
		{
			gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min, &max);
			if (gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_IMX_VPU_FRAMEBUFFER))
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
	if ((pool == NULL) || !gst_buffer_pool_has_option(pool, GST_BUFFER_POOL_OPTION_IMX_VPU_FRAMEBUFFER))
	{
		if (pool == NULL)
			GST_INFO_OBJECT(decoder, "no pool present; creating new pool");
		else
		{
			gst_object_unref(pool);
			GST_INFO_OBJECT(decoder, "no pool supports VPU buffers; creating new pool");
		}
		pool = gst_imx_vpu_fb_buffer_pool_new(vpu_dec->current_framebuffers);
	}

	GST_INFO_OBJECT(
		pool,
		"pool config:  outcaps: %" GST_PTR_FORMAT "  size: %u  min buffers: %u  max buffers: %u",
		(gpointer)outcaps,
		size,
		min,
		max
	);

	/* Inform the pool about the framebuffers */
	gst_imx_vpu_fb_buffer_pool_set_framebuffers(pool, vpu_dec->current_framebuffers);

	/* Now configure the pool. */
	config = gst_buffer_pool_get_config(pool);
	gst_buffer_pool_config_set_params(config, outcaps, size, min, max);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_VPU_FRAMEBUFFER);
	gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM);
	gst_buffer_pool_set_config(pool, config);

	if (update_pool)
		gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
	else
		gst_query_add_allocation_pool(query, pool, size, min, max);

	if (pool != NULL)
		gst_object_unref(pool);

	return TRUE;
}


static void gst_imx_vpu_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstImxVpuDec *vpu_dec = GST_IMX_VPU_DEC(object);

	switch (prop_id)
	{
		case PROP_NUM_ADDITIONAL_FRAMEBUFFERS:
		{
			guint num;

			if (vpu_dec->vpu_inst_opened)
			{
				GST_ERROR_OBJECT(vpu_dec, "cannot change number of additional framebuffers while a VPU decoder instance is open");
				return;
			}

			num = g_value_get_uint(value);
			vpu_dec->num_additional_framebuffers = num;

			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuDec *vpu_dec = GST_IMX_VPU_DEC(object);

	switch (prop_id)
	{
		case PROP_NUM_ADDITIONAL_FRAMEBUFFERS:
			g_value_set_uint(value, vpu_dec->num_additional_framebuffers);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstStateChangeReturn gst_imx_vpu_dec_change_state (GstElement *element, GstStateChange transition)
{
	GstImxVpuDec *vpu_dec = GST_IMX_VPU_DEC(element);
	GstStateChangeReturn result;

	switch (transition) 
	{
		case GST_STATE_CHANGE_READY_TO_PAUSED:
			if (vpu_dec->current_framebuffers != NULL)
			{
				GST_INFO_OBJECT(element, "Clearing flushing flag of framebuffers object during PAUSED->READY state change");
				GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);
				gst_imx_vpu_framebuffers_set_flushing(vpu_dec->current_framebuffers, FALSE);
				GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
			}
			break;

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			if (vpu_dec->current_framebuffers != NULL)
			{
				GST_INFO_OBJECT(element, "Setting flushing flag of framebuffers object during PAUSED->READY state change");
				GST_IMX_VPU_FRAMEBUFFERS_LOCK(vpu_dec->current_framebuffers);
				gst_imx_vpu_framebuffers_set_flushing(vpu_dec->current_framebuffers, TRUE);
				GST_IMX_VPU_FRAMEBUFFERS_UNLOCK(vpu_dec->current_framebuffers);
			}
			break;

		default:
			break;
	}

	result = GST_ELEMENT_CLASS (gst_imx_vpu_dec_parent_class)->change_state(element, transition);

	return result;
}
