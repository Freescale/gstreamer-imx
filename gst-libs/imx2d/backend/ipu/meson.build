ipu_option = get_option('ipu')

ipu_specific_c_args = ['-std=gnu99']

if imx_headers_path != ''
	ipu_specific_c_args += ['-I' + imx_headers_path]
	message('Looking for IPU headers in path "' + imx_headers_path + '"')
endif

ipu_header_found = cc.check_header(
	'linux/ipu.h',
	required : ipu_option,
	args : ipu_specific_c_args
)

if not ipu_header_found
	ipu_header_found = cc.check_header(
		'imx/linux/ipu.h',
		required : ipu_option,
		args : ipu_specific_c_args
	)
	conf_data.set('IPU_HEADER_IS_IN_IMX_SUBDIR', 1)
endif

if ipu_header_found
	imx2d_backend_ipu = static_library(
		'imx2d_backend_ipu',
		['ipu_blitter.c'],
		install : false,
		include_directories: [configinc],
		c_args : ipu_specific_c_args,
		dependencies : [imx2d_dep]
	)

	imx2d_backend_ipu_dep = declare_dependency(
		dependencies : [imx2d_dep],
		link_with : [imx2d_backend_ipu]
	)

	conf_data.set('WITH_IMX2D_IPU_BACKEND', 1)

	message('imx2d IPU backend enabled')
else
	imx2d_backend_ipu_dep = dependency('', required: false)
	if ipu_option.disabled()
		message('imx2d IPU backend disabled explicitely by command line option')
	else
		message('imx2d IPU backend disabled due to missing IPU dependency')
	endif
endif
