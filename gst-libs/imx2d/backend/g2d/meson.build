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
		dependencies : [imx2d_dep, g2d_dep]
	)

	imx2d_backend_g2d_dep = declare_dependency(
		dependencies : [imx2d_dep, g2d_dep],
		link_with : [imx2d_backend_g2d]
	)

	conf_data.set('WITH_IMX2D_G2D_BACKEND', 1)

	message('imx2d G2D backend enabled')
else
	imx2d_backend_g2d_dep = dependency('', required: false)
	if g2d_option.disabled()
		message('imx2d G2D backend disabled explicitely by command line option')
	else
		message('imx2d G2D backend disabled due to missing G2D dependency')
	endif
endif
