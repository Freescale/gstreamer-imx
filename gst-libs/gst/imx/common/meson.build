source = ['gstimxdmabufferallocator.c', 'gstimxdefaultallocator.c']
public_headers = ['gstimxdmabufferallocator.h', 'gstimxdefaultallocator.h']

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

install_headers(public_headers, subdir : 'gstreamer-1.0/gst/imx/common')

pkg.generate(
	gstimxcommon,
	name : 'gstimxcommon',
	description : 'GStreamer IMX common code'
)
