#ifndef GST_IMX_EGL_VIV_SINK_GL_HEADERS_H
#define GST_IMX_EGL_VIV_SINK_GL_HEADERS_H

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif

#ifndef GL_VIV_direct_texture
#define GL_VIV_direct_texture 1
#endif


#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>


#ifndef GL_APIENTRY
#define GL_APIENTRY KHRONOS_APIENTRY
#endif

#ifndef GL_APIENTRYP
#define GL_APIENTRYP GL_APIENTRY*
#endif


#define GL_VIV_YV12                                             0x8FC0
#define GL_VIV_NV12                                             0x8FC1
#define GL_VIV_YUY2                                             0x8FC2
#define GL_VIV_UYVY                                             0x8FC3
#define GL_VIV_NV21                                             0x8FC4
#define GL_VIV_I420                                             0x8FC5

typedef void (GL_APIENTRYP PFNGLTEXDIRECTVIVMAP) (GLenum, GLsizei, GLsizei, GLenum, GLvoid **, const GLuint *);
typedef void (GL_APIENTRYP PFNGLTEXDIRECTVIV) (GLenum, GLsizei, GLsizei, GLenum, GLvoid **);
typedef void (GL_APIENTRYP PFNGLTEXDIRECTINVALIDATEVIV)(GLenum);

extern PFNGLTEXDIRECTVIVMAP EglVivSink_TexDirectVIVMap;
extern PFNGLTEXDIRECTVIV EglVivSink_TexDirectVIV;
extern PFNGLTEXDIRECTINVALIDATEVIV EglVivSink_TexDirectInvalidateVIV;

#define glTexDirectVIVMap           EglVivSink_TexDirectVIVMap
#define glTexDirectVIV              EglVivSink_TexDirectVIV
#define glTexDirectInvalidateVIV    EglVivSink_TexDirectInvalidateVIV


int gst_imx_egl_viv_sink_init_viv_direct_texture(void);


#endif
