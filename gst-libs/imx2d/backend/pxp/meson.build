pxp_option = get_option('pxp')

pxp_specific_c_args = []

if imx_headers_path != ''
	pxp_specific_c_args += ['-I' + imx_headers_path]
	message('Looking for PxP headers in path "' + imx_headers_path + '"')
elif sys_root != ''
	# Also try this path, since linux/pxp_device.h includes linux/pxp_dma.h,
	# and the latter won't be found if the path to the imx headers is not present.
	pxp_sys_root_path = join_paths(sys_root , 'usr', 'include', 'imx')
	pxp_specific_c_args += ['-I' + pxp_sys_root_path]
	message('Looking for PxP headers in path "' + pxp_sys_root_path + '"')
endif

pxp_header_found = cc.check_header(
	'linux/pxp_device.h',
	required : pxp_option,
	args : pxp_specific_c_args
)

if not pxp_header_found
	pxp_header_found = cc.check_header(
		'imx/linux/pxp_device.h',
		required : pxp_option,
		args : pxp_specific_c_args
	)
	conf_data.set('PXP_HEADER_IS_IN_IMX_SUBDIR', 1)
endif

if pxp_header_found
	imx2d_backend_pxp = static_library(
		'imx2d_backend_pxp',
		['pxp_blitter.c'],
		install : false,
		include_directories: [configinc],
		c_args : pxp_specific_c_args,
		dependencies : [imx2d_dep]
	)

	imx2d_backend_pxp_dep = declare_dependency(
		dependencies : [imx2d_dep],
		link_with : [imx2d_backend_pxp]
	)

	conf_data.set('WITH_IMX2D_PXP_BACKEND', 1)

	message('imx2d PxP backend enabled')
else
	imx2d_backend_pxp_dep = dependency('', required: false)
	if pxp_option.disabled()
		message('imx2d PxP backend disabled explicitely by command line option')
	else
		message('imx2d PxP backend disabled due to missing PxP dependency')
	endif
endif
