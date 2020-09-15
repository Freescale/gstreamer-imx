g2d_dep = cc.find_library(
	'g2d',
	required : true,
	has_headers : ['g2d.h']
)

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
