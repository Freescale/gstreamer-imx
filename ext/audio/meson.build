if not gstreamer_audio_dep.found()
	# TODO: if the audio element options are set to "enabled", and gstaudio was not found, print an error and exit
	subdir_done()
endif

# Check for the libfslaudiocodec library to build the uniaudiodec element
libfslaudiocodec_dep = dependency('libfslaudiocodec', required : get_option('uniaudiodec'))
fslcodec_libdirvar = ''
if libfslaudiocodec_dep.found()
	fslcodec_libdirvar = libimxdmabuffer_dep.get_pkgconfig_variable('libdir', default : '')
	if fslcodec_libdirvar != ''
		message('Found installed fslaudiocodec library - uniaudio decoder will be built')
		conf_data.set_quoted('IMX_AUDIO_CODEC_PATH', fslcodec_libdirvar)
	else
		message('Found installed fslaudiocodec library, but could not find fsl codec path - uniaudio decoder will not be built')
	endif
else
	message('Installed fslaudiocodec library not found, or user disabled uniaudiodec element compilation - uniaudio decoder will not be built')
endif

# Check for the mp3 encoder library to build the mp3encoder element
mp3encoder_dep = cc.find_library('_mp3_enc_arm12_elinux', dirs : [fslcodec_libdirvar], required : get_option('mp3encoder'))
if mp3encoder_dep.found()
	message('Found installed _mp3_enc_arm12_elinux library - mp3 encoder will be built')
else
	message('Installed _mp3_enc_arm12_elinux library not found, or user disabled mp3encoder element compilation - mp3 encoder will not be built')
endif


source = []
if libfslaudiocodec_dep.found()
	source += ['gstimxuniaudiocodec.c', 'gstimxuniaudiodecoder.c']
endif
if mp3encoder_dep.found()
	source += ['gstimxmp3encoder.c']
endif

if source != []
	library(
		'gstimxaudio',
		source,
		install : true,
		install_dir: plugins_install_dir,
		include_directories: [configinc, libsinc],
		dependencies : [gstreamer_dep, gstreamer_base_dep, gstreamer_audio_dep, libdl_dep, libfslaudiocodec_dep, mp3encoder_dep],
		link_with : [gstimxcommon]
	)
endif

