source = ['gstimxdmabufferallocator.c', 'gstimxdefaultallocator.c', 'gstimxdmabufferuploader.c']
public_headers = ['gstimxdmabufferallocator.h', 'gstimxdefaultallocator.h', 'gstimxdmabufferuploader.h']

if ion_support
	source += ['gstimxionallocator.c']
	public_headers += ['gstimxionallocator.h']
endif

gstimxcommon = library(
	'gstimxcommon',
	source,
	version : meson.project_version(),
	install : true,
	include_directories: [configinc],
	dependencies : [gstreamer_dep, gstreamer_base_dep, gstreamer_allocators_dep, libm_dep, libimxdmabuffer_dep]
)

gstimxcommon_dep = declare_dependency(
	dependencies : [gstreamer_dep, gstreamer_base_dep, gstreamer_allocators_dep, libm_dep, libimxdmabuffer_dep],
	include_directories : libsinc,
	link_with : [gstimxcommon]
)

install_headers(public_headers, subdir : 'gstreamer-1.0/gst/imx/common')

pkg.generate(
	gstimxcommon,
	name : 'gstimxcommon',
	description : 'GStreamer IMX common code'
)
