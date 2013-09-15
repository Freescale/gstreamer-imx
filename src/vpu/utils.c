/* Miscellanous utility functions for VPU operations
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
#include "utils.h"

// TODO: create one strerror function for the decoder, one for the encoder
gchar const *gst_fsl_vpu_strerror(VpuDecRetCode code)
{
	switch (code)
	{
		case VPU_DEC_RET_SUCCESS: return "success";
		case VPU_DEC_RET_FAILURE: return "failure";
		case VPU_DEC_RET_INVALID_PARAM: return "invalid param";
		case VPU_DEC_RET_INVALID_HANDLE: return "invalid handle";
		case VPU_DEC_RET_INVALID_FRAME_BUFFER: return "invalid frame buffer";
		case VPU_DEC_RET_INSUFFICIENT_FRAME_BUFFERS: return "insufficient frame buffers";
		case VPU_DEC_RET_INVALID_STRIDE: return "invalid stride";
		case VPU_DEC_RET_WRONG_CALL_SEQUENCE: return "wrong call sequence";
		case VPU_DEC_RET_FAILURE_TIMEOUT: return "failure timeout";
		default:
			return NULL;
	}
}

