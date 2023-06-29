g2d_option = get_option('g2d')

g2d_dep = cc.find_library(
	'g2d',
	required : g2d_option,
	has_headers : ['g2d.h']
)

if g2d_dep.found()
	imx2d_backend_g2d = static_library(
		'imx2d_backend_g2d',
		['g2d_blitter.c'],
		install : false,
		include_directories: [configinc],
		dependencies : [imx2d_dep, g2d_dep]
	)

	imx2d_backend_g2d_dep = declare_dependency(
		dependencies : [imx2d_dep, g2d_dep],
		link_with : [imx2d_backend_g2d]
	)

	conf_data.set('WITH_IMX2D_G2D_BACKEND', 1)

	g2d_major_version = cc.get_define('G2D_VERSION_MAJOR', prefix : '#include <g2d.h>').to_int()
	g2d_minor_version = cc.get_define('G2D_VERSION_MINOR', prefix : '#include <g2d.h>').to_int()
	g2d_patch_version = cc.get_define('G2D_VERSION_PATCH', prefix : '#include <g2d.h>').to_int()
	message(
		'G2D version: @0@.@1@.@2@'.format(
			g2d_major_version,
			g2d_minor_version,
			g2d_patch_version
		)
	)

	g2d_amphion_support_details = {
		'G2D_AMPHION_TILED' : {'define': 'IMX2D_G2D_AMPHION_TILE_LAYOUT_SUPPORTED', 'name': 'G2D Amphion tile layout'},
		'G2D_AMPHION_TILED_10BIT' : {'define': 'IMX2D_G2D_AMPHION_10BIT_TILE_LAYOUT_SUPPORTED', 'name': 'G2D Amphion 10-bit tile layout'},
		'G2D_AMPHION_INTERLACED' : {'define': 'IMX2D_G2D_AMPHION_INTERLACED_TILE_LAYOUT_SUPPORTED', 'name': 'G2D Amphion interlaced tile layout'},
	}
	foreach enum, details : g2d_amphion_support_details
		g2d_amphion_support = cc.compiles(
			'''
			#include <g2d.h>
			#include <g2dExt.h>
			int main(void)
			{
				int i = @0@;
				return 0;
			}
			'''.format(enum),
			dependencies : [g2d_dep],
			name : '@0@ support'.format(details['name'])
		)
		conf_data.set(details['define'], g2d_amphion_support)
	endforeach

	g2d_colorimetry_support = cc.compiles(
		'''
		#include <g2d.h>
		int main(void)
		{
			int i = G2D_YUV_BT_601;
			return 0;
		}
		''',
		dependencies : [g2d_dep],
		name : 'G2D colorimetry support'
	)
	conf_data.set('IMX2D_G2D_COLORIMETRY_SUPPORTED', g2d_colorimetry_support)

	g2d_based_on_dpu = get_option('g2d-based-on-dpu')
	if g2d_based_on_dpu
		message('G2D implementation is based on the i.MX8qm / i.MX8qxp DPU')
	else
		message('G2D implementation is not based on the i.MX8qm / i.MX8qxp DPU')
	endif
	conf_data.set('IMX2D_G2D_IMPLEMENTATION_BASED_ON_DPU', g2d_based_on_dpu)

	# Starting with G2D 2.0, the previous thread limitation is gone.
	# This means that the various G2D function do not stricly have
	# to be called from the same thread only. In GStreamer, this is
	# important, since then, g2d_open() can be called in a start
	# function that may be called in a different thread. Concurrent
	# G2D calls are still not supported, though.
	# Also, DPU based implementations never had that limitation.
	# Thus, if either G2D is new enough, or if this is a DPU based
	# G2D implementation, do not restrict G2D calls to one thread.

	if g2d_major_version >= 2
		g2d_calls_restricted_to_one_thread = false
		message('G2D implementation is sufficiently new; not restricting calls to one thread')
	elif g2d_based_on_dpu
		g2d_calls_restricted_to_one_thread = false
		message('G2D implementation is base on DPU; not restricting calls to one thread')
	else
		g2d_calls_restricted_to_one_thread = true
		message('G2D implementation is too old and not based on DPU; restricting calls to one thread')
	endif

	if g2d_calls_restricted_to_one_thread
		conf_data.set('IMX2D_G2D_CALLS_RESTRICTED_TO_ONE_THREAD', 1)
	endif

	message('imx2d G2D backend enabled')
else
	imx2d_backend_g2d_dep = dependency('', required: false)
	if g2d_option.disabled()
		message('imx2d G2D backend disabled explicitely by command line option')
	else
		message('imx2d G2D backend disabled due to missing G2D dependency')
	endif
endif
