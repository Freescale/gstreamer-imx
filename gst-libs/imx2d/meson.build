imx2d = static_library(
	'imx2d',
	['imx2d.c', 'linux_framebuffer.c'],
	install : false,
	include_directories : libsinc,
	dependencies : [libimxdmabuffer_dep]
)

imx2d_dep = declare_dependency(
	dependencies : [libimxdmabuffer_dep],
	include_directories : libsinc,
	link_with : [imx2d]
)

subdir('backend/g2d')
subdir('backend/ipu')
