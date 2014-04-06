#include <config.h>
#include <string.h>
#include <stdlib.h>
#include "gl_headers.h"
#include "gles2_renderer.h"
#include "egl_platform.h"
#include "../common/phys_mem_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_gles2renderer_debug);
#define GST_CAT_DEFAULT imx_gles2renderer_debug


struct _GstImxEglVivSinkGLES2Renderer
{
	guintptr window_handle;
	guint window_width, window_height;
	gboolean event_handling;
	guint display_ratio_n, display_ratio_d;
	GstVideoInfo video_info;
	gboolean video_info_updated;
	gboolean fullscreen;
	gint manual_x_coord, manual_y_coord, manual_width, manual_height;
	gboolean borderless;

	GstBuffer *current_frame;

	GstImxEglVivSinkEGLPlatform *egl_platform;

	gboolean thread_started, force_aspect_ratio;
	GstFlowReturn loop_flow_retval;
	GThread *thread;
	GMutex mutex;

	GLuint vertex_shader, fragment_shader, program;
	GLuint vertex_buffer;
	GLuint texture;
	GLint tex_uloc, frame_rect_uloc, uv_scale_uloc;
	GLint position_aloc, texcoords_aloc;

	GLvoid* viv_planes[3];
};


#define GLES2_RENDERER_LOCK(renderer) g_mutex_lock(&((renderer)->mutex))
#define GLES2_RENDERER_UNLOCK(renderer) g_mutex_unlock(&((renderer)->mutex))


static gpointer gst_imx_egl_viv_sink_gles2_renderer_thread(gpointer thread_data);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_render_frame(GstImxEglVivSinkEGLPlatform *platform, gpointer user_context);

static gboolean gst_imx_egl_viv_sink_gles2_renderer_check_gl_error(char const *category, char const *label);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_build_shader(GLuint *shader, GLenum shader_type, char const *code);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_destroy_shader(GLuint *shader, GLenum shader_type);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_link_program(GLuint *program, GLuint vertex_shader, GLuint fragment_shader);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_destroy_program(GLuint *program, GLuint vertex_shader, GLuint fragment_shader);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_build_vertex_buffer(GLuint *vertex_buffer);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_destroy_vertex_buffer(GLuint *vertex_buffer);
static GLenum gst_imx_egl_viv_sink_gles2_renderer_get_viv_format(GstVideoFormat format);
static gint gst_imx_egl_viv_sink_gles2_renderer_bpp(GstVideoFormat fmt);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_search_extension(GLubyte const *extensions);

static gboolean gst_imx_egl_viv_sink_gles2_renderer_setup_resources(GstImxEglVivSinkGLES2Renderer *renderer);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_teardown_resources(GstImxEglVivSinkGLES2Renderer *renderer);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_fill_texture(GstImxEglVivSinkGLES2Renderer *renderer, GstBuffer *buffer);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_render_current_frame(GstImxEglVivSinkGLES2Renderer *renderer);
static void gst_imx_egl_viv_sink_gles2_renderer_resize_callback(GstImxEglVivSinkEGLPlatform *platform, guint window_width, guint window_height, gpointer user_context);
static gboolean gst_imx_egl_viv_sink_gles2_renderer_update_display_ratio(GstImxEglVivSinkGLES2Renderer *renderer, GstVideoInfo *video_info);



static char const * simple_vertex_shader =
	"attribute vec2 position; \n"
	"attribute vec2 texcoords; \n"
	"varying vec2 uv; \n"
	"uniform vec2 frame_rect; \n"
	"void main(void) \n"
	"{ \n"
	"	uv = texcoords; \n"
	"	gl_Position = vec4(position * frame_rect.xy, 1.0, 1.0); \n"
	"} \n"
	;

static char const * simple_fragment_shader =
	"precision mediump float;\n"
	"varying vec2 uv; \n"
	"uniform sampler2D tex; \n"
	"uniform vec2 uv_scale; \n"
	"void main(void) \n"
	"{ \n"
	"	vec4 texel = texture2D(tex, uv * uv_scale); \n"
	"	gl_FragColor = vec4(texel.rgb, 1.0); \n"
	"} \n"
	;


static GLfloat const vertex_data[] = {
	-1, -1,  0, 1,
	-1,  1,  0, 0,
	 1, -1,  1, 1,
	 1,  1,  1, 0,
};
static unsigned int const vertex_data_size = sizeof(GLfloat)*16;
static unsigned int const vertex_size = sizeof(GLfloat)*4;
static unsigned int const vertex_position_num = 2;
static unsigned int const vertex_position_offset = sizeof(GLfloat)*0;
static unsigned int const vertex_texcoords_num = 2;
static unsigned int const vertex_texcoords_offset = sizeof(GLfloat)*2;






static void init_debug_category(void)
{
	static gboolean initialized = FALSE;
	if (!initialized)
	{
		GST_DEBUG_CATEGORY_INIT(imx_gles2renderer_debug, "imx_gles2_renderer", 0, "imxeglvivsink OpenGL ES 2 renderer");
		initialized = TRUE;
	}
}




static gpointer gst_imx_egl_viv_sink_gles2_renderer_thread(gpointer thread_data)
{
	GstImxEglVivSinkMainloopRetval mainloop_retval;
	GstImxEglVivSinkGLES2Renderer *renderer = (GstImxEglVivSinkGLES2Renderer *)thread_data;

	{
		GLubyte const *extensions;

		if (!gst_imx_egl_viv_sink_egl_platform_init_window(
			renderer->egl_platform,
			renderer->window_handle,
			renderer->event_handling,
			&(renderer->video_info),
			renderer->fullscreen,
			renderer->manual_x_coord, renderer->manual_y_coord,
			renderer->manual_width, renderer->manual_height,
			renderer->borderless
		))
		{
			GST_ERROR("could not open window");
			GLES2_RENDERER_LOCK(renderer);
			renderer->loop_flow_retval = GST_FLOW_ERROR;
			GLES2_RENDERER_UNLOCK(renderer);
			return 0;
		}

		extensions = glGetString(GL_EXTENSIONS);
		if (extensions == NULL)
		{
			GST_ERROR("OpenGL ES extension string is NULL");
			GLES2_RENDERER_LOCK(renderer);
			renderer->loop_flow_retval = GST_FLOW_ERROR;
			GLES2_RENDERER_UNLOCK(renderer);
			return 0;
		}

		if (gst_imx_egl_viv_sink_gles2_renderer_search_extension(extensions))
			GST_INFO("Vivante direct texture extension (GL_VIV_direct_texture) present");
		else
		{
			GST_ERROR("Vivante direct texture extension (GL_VIV_direct_texture) missing");
			GLES2_RENDERER_LOCK(renderer);
			renderer->loop_flow_retval = GST_FLOW_ERROR;
			GLES2_RENDERER_UNLOCK(renderer);
			return 0;
		}
	}

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	GLES2_RENDERER_LOCK(renderer);

	if (!gst_imx_egl_viv_sink_gles2_renderer_setup_resources(renderer))
	{
		renderer->loop_flow_retval = GST_FLOW_ERROR;
		GLES2_RENDERER_UNLOCK(renderer);

		GST_ERROR("setting up resources failed - stopping thread");

		return 0;
	}

	glUseProgram(renderer->program);
	glBindBuffer(GL_ARRAY_BUFFER, renderer->vertex_buffer);
	glBindTexture(GL_TEXTURE_2D, renderer->texture);

	GLES2_RENDERER_UNLOCK(renderer);

	GST_INFO("starting GLES2 renderer loop");

	mainloop_retval = gst_imx_egl_viv_sink_egl_platform_mainloop(renderer->egl_platform);

	GLES2_RENDERER_LOCK(renderer);
	switch (mainloop_retval)
	{
		case GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_OK:
			renderer->loop_flow_retval = GST_FLOW_OK;
			break;
		case GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_WINDOW_CLOSED:
			GST_INFO("Window closed - stopping thread");
			renderer->loop_flow_retval = GST_FLOW_EOS;
			break;
		case GST_IMX_EGL_VIV_SINK_MAINLOOP_RETVAL_ERROR:
			renderer->loop_flow_retval = GST_FLOW_ERROR;
			break;
	}

	if (!gst_imx_egl_viv_sink_gles2_renderer_teardown_resources(renderer))
	{
		GST_ERROR("tearing down resources failed");
	}
	GLES2_RENDERER_UNLOCK(renderer);

	{
		if (!gst_imx_egl_viv_sink_egl_platform_shutdown_window(renderer->egl_platform))
			GST_ERROR("could not close window");
	}

	GST_LOG("thread function finished");

	return 0;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_render_frame(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, gpointer user_context)
{
	gboolean ret = TRUE;
	GstImxEglVivSinkGLES2Renderer *renderer = (GstImxEglVivSinkGLES2Renderer *)user_context;

	GLES2_RENDERER_LOCK(renderer);

	if (!(ret = gst_imx_egl_viv_sink_gles2_renderer_render_current_frame(renderer)))
		GST_ERROR("could not render frame");

	GLES2_RENDERER_UNLOCK(renderer);

	return ret;
}




static gboolean gst_imx_egl_viv_sink_gles2_renderer_check_gl_error(char const *category, char const *label)
{
	GLenum err = glGetError();
	if (err == GL_NO_ERROR)
		return TRUE;

	switch (err)
	{
		case GL_INVALID_ENUM:                  GST_ERROR("[%s] [%s] error: invalid enum", category, label); break;
		case GL_INVALID_VALUE:                 GST_ERROR("[%s] [%s] error: invalid value", category, label); break;
		case GL_INVALID_OPERATION:             GST_ERROR("[%s] [%s] error: invalid operation", category, label); break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: GST_ERROR("[%s] [%s] error: invalid framebuffer operation", category, label); break;
		case GL_OUT_OF_MEMORY:                 GST_ERROR("[%s] [%s] error: out of memory", category, label); break;
		case GL_STACK_UNDERFLOW:               GST_ERROR("[%s] [%s] error: stack underflow", category, label); break;
		case GL_STACK_OVERFLOW:                GST_ERROR("[%s] [%s] error: stack overflow", category, label); break;
		default:                               GST_ERROR("[%s] [%s] error: unknown GL error 0x%x", category, label, err);
	}

	return FALSE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_build_shader(GLuint *shader, GLenum shader_type, char const *code)
{
	GLint compilation_status, info_log_length;
	GLchar *info_log;
	char const *shader_type_name;

	switch (shader_type)
	{
		case GL_VERTEX_SHADER: shader_type_name = "vertex shader"; break;
		case GL_FRAGMENT_SHADER: shader_type_name = "fragment shader"; break;
		default:
			GST_ERROR("unknown shader type 0x%x", shader_type);
			return FALSE;
	}

	glGetError(); /* clear out any existing error */

	*shader = glCreateShader(shader_type);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error(shader_type_name, "glCreateShader"))
		return FALSE;

	glShaderSource(*shader, 1, &code, NULL);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error(shader_type_name, "glShaderSource"))
		return FALSE;

	glCompileShader(*shader);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error(shader_type_name, "glCompileShader"))
		return FALSE;

	glGetShaderiv(*shader, GL_COMPILE_STATUS, &compilation_status);
	if (compilation_status == GL_FALSE)
	{
		GST_ERROR("compiling %s failed", shader_type_name);
		glGetShaderiv(*shader, GL_INFO_LOG_LENGTH, &info_log_length);
		info_log = g_new0(GLchar, info_log_length);
		glGetShaderInfoLog(*shader, info_log_length, NULL, info_log);
		GST_INFO("compilation log:\n%s", info_log);
		g_free(info_log);
		return FALSE;
	}
	else
		GST_LOG("successfully compiled %s", shader_type_name);

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_destroy_shader(GLuint *shader, GLenum shader_type)
{
	char const *shader_type_name;

	if ((*shader) == 0)
		return TRUE;

	switch (shader_type)
	{
		case GL_VERTEX_SHADER: shader_type_name = "vertex shader"; break;
		case GL_FRAGMENT_SHADER: shader_type_name = "fragment shader"; break;
		default:
			GST_ERROR("unknown shader type 0x%x", shader_type);
			return FALSE;
	}

	glGetError(); /* clear out any existing error */

	glDeleteShader(*shader);
	*shader = 0;
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error(shader_type_name, "glDeleteShader"))
		return FALSE;

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_link_program(GLuint *program, GLuint vertex_shader, GLuint fragment_shader)
{
	GLint link_status, info_log_length;
	GLchar *info_log;

	glGetError(); /* clear out any existing error */

	*program = glCreateProgram();
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program", "glCreateProgram"))
		return FALSE;

	glAttachShader(*program, vertex_shader);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program vertex", "glAttachShader"))
		return FALSE;

	glAttachShader(*program, fragment_shader);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program fragment", "glAttachShader"))
		return FALSE;

	glLinkProgram(*program);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program", "glLinkProgram"))
		return FALSE;

	glGetProgramiv(*program, GL_LINK_STATUS, &link_status);
	if (link_status == GL_FALSE)
	{
		GST_ERROR("linking program failed");
		glGetProgramiv(*program, GL_INFO_LOG_LENGTH, &info_log_length);
		info_log = g_new0(GLchar, info_log_length);
		glGetProgramInfoLog(*program, info_log_length, NULL, info_log);
		GST_INFO("linker log:\n%s", info_log);
		g_free(info_log);
		return FALSE;
	}
	else
		GST_LOG("successfully linked program");

	glUseProgram(*program);

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_destroy_program(GLuint *program, GLuint vertex_shader, GLuint fragment_shader)
{
	if ((*program) == 0)
		return TRUE;

	glGetError(); /* clear out any existing error */

	glUseProgram(0);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program", "glUseProgram"))
		return FALSE;

	glDetachShader(*program, vertex_shader);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program vertex", "glDetachShader"))
		return FALSE;

	glDetachShader(*program, fragment_shader);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program fragment", "glDetachShader"))
		return FALSE;

	glDeleteProgram(*program);
	*program = 0;
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("program", "glDeleteProgram"))
		return FALSE;

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_build_vertex_buffer(GLuint *vertex_buffer)
{
	glGetError(); /* clear out any existing error */

	glGenBuffers(1, vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, *vertex_buffer);
	/* TODO: This has to be called twice, otherwise the vertex data gets corrupted after the first few
	 * rendered frames. Is this a Vivante driver bug? */
	glBufferData(GL_ARRAY_BUFFER, vertex_data_size, vertex_data, GL_STATIC_DRAW);
	glBufferData(GL_ARRAY_BUFFER, vertex_data_size, vertex_data, GL_STATIC_DRAW);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("vertex buffer", "glBufferData"))
		return FALSE;

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_destroy_vertex_buffer(GLuint *vertex_buffer)
{
	glGetError(); /* clear out any existing error */

	if ((*vertex_buffer) != 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDeleteBuffers(1, vertex_buffer);
		*vertex_buffer = 0;
	}

	return TRUE;
}


static GLenum gst_imx_egl_viv_sink_gles2_renderer_get_viv_format(GstVideoFormat format)
{
	switch (format)
	{
#ifdef HAVE_VIV_I420
		case GST_VIDEO_FORMAT_I420:  return GL_VIV_I420;
#endif
#ifdef HAVE_VIV_YV12
		case GST_VIDEO_FORMAT_YV12:  return GL_VIV_YV12;
#endif
#ifdef HAVE_VIV_NV12
		case GST_VIDEO_FORMAT_NV12:  return GL_VIV_NV12;
#endif
#ifdef HAVE_VIV_NV21
		case GST_VIDEO_FORMAT_NV21:  return GL_VIV_NV21;
#endif
#ifdef HAVE_VIV_YUY2
		case GST_VIDEO_FORMAT_YUY2:  return GL_VIV_YUY2;
#endif
#ifdef HAVE_VIV_UYVY
		case GST_VIDEO_FORMAT_UYVY:  return GL_VIV_UYVY;
#endif
		case GST_VIDEO_FORMAT_RGB16: return GL_RGB565;
		case GST_VIDEO_FORMAT_RGB:   return GL_RGB;
		case GST_VIDEO_FORMAT_RGBA:  return GL_RGBA;
		case GST_VIDEO_FORMAT_BGRA:  return GL_BGRA_EXT;
		case GST_VIDEO_FORMAT_RGBx:  return GL_RGBA;
		case GST_VIDEO_FORMAT_BGRx:  return GL_BGRA_EXT;
		default: return 0;
	}
}


static gint gst_imx_egl_viv_sink_gles2_renderer_bpp(GstVideoFormat fmt)
{
	switch (fmt)
	{
		case GST_VIDEO_FORMAT_RGB16: return 2;
		case GST_VIDEO_FORMAT_RGB: return 3;
		case GST_VIDEO_FORMAT_RGBA: return 4;
		case GST_VIDEO_FORMAT_BGRA: return 4;
		case GST_VIDEO_FORMAT_RGBx: return 4;
		case GST_VIDEO_FORMAT_BGRx: return 4;
		case GST_VIDEO_FORMAT_YUY2: return 2;
		case GST_VIDEO_FORMAT_UYVY: return 2;
		default: return 1;
	}
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_search_extension(GLubyte const *extensions)
{
	char *buf = NULL;
	int buf_len = 0;
	char const *start, *end;
	start = end = (char const *)extensions;
	gboolean viv_direct_ext_found = FALSE;

	/* go through the space-separated extension list */

	while (1)
	{
		if ((*end == ' ') || (*end == 0))
		{
			if (start != end)
			{
				int token_len = end - start; /* string: [start, end-1] */

				/* enlarge token buffer if it is too small */
				if (buf_len < token_len)
				{
					char *new_buf = realloc(buf, token_len + 1);
					if (new_buf == NULL)
					{
						if (buf != NULL)
							free(buf);
						GST_ERROR("could not (re)allocate %d bytes for token buffer", token_len);
						return FALSE;
					}
					buf = new_buf;
					buf_len = token_len;
				}

				/* copy token to buffer, and add null terminator */
				memcpy(buf, start, token_len);
				buf[token_len] = 0;

				GST_LOG("found extension: %s", buf);

				/* this sink needs direct texture extension is necessary for playback */
				if (strcmp("GL_VIV_direct_texture", buf) == 0)
					viv_direct_ext_found = TRUE;
			}

			start = end + 1;
		}

		if (*end == 0)
			break;

		++end;
	}

	if (buf != NULL)
		free(buf);

	return viv_direct_ext_found;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_setup_resources(GstImxEglVivSinkGLES2Renderer *renderer)
{
	/* must be called with lock */

	/* build shaders and program */
	if (!gst_imx_egl_viv_sink_gles2_renderer_build_shader(&(renderer->vertex_shader), GL_VERTEX_SHADER, simple_vertex_shader))
		return FALSE;
	if (!gst_imx_egl_viv_sink_gles2_renderer_build_shader(&(renderer->fragment_shader), GL_FRAGMENT_SHADER, simple_fragment_shader))
		return FALSE;
	if (!gst_imx_egl_viv_sink_gles2_renderer_link_program(&(renderer->program), renderer->vertex_shader, renderer->fragment_shader))
		return FALSE;
	/* get uniform and attribute locations */
	renderer->tex_uloc = glGetUniformLocation(renderer->program, "tex");
	renderer->frame_rect_uloc = glGetUniformLocation(renderer->program, "frame_rect");
	renderer->uv_scale_uloc = glGetUniformLocation(renderer->program, "uv_scale");
	renderer->position_aloc = glGetAttribLocation(renderer->program, "position");
	renderer->texcoords_aloc = glGetAttribLocation(renderer->program, "texcoords");

	/* create texture */
	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &(renderer->texture));
	glBindTexture(GL_TEXTURE_2D, renderer->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	/* set texture unit value for tex uniform */
	glUniform1i(renderer->tex_uloc, 0);

	glUniform2f(renderer->frame_rect_uloc, 1.0f, 1.0f);

	/* build vertex and index buffer objects */
	if (!gst_imx_egl_viv_sink_gles2_renderer_build_vertex_buffer(&(renderer->vertex_buffer)))
		return FALSE;

	/* enable vertex attrib array and set up pointers */
	glEnableVertexAttribArray(renderer->position_aloc);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("position vertex attrib", "glEnableVertexAttribArray"))
		return FALSE;
	glEnableVertexAttribArray(renderer->texcoords_aloc);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("texcoords vertex attrib", "glEnableVertexAttribArray"))
		return FALSE;

	glVertexAttribPointer(renderer->position_aloc,  vertex_position_num, GL_FLOAT, GL_FALSE, vertex_size, (GLvoid const*)((uintptr_t)vertex_position_offset));
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("position vertex attrib", "glVertexAttribPointer"))
		return FALSE;
	glVertexAttribPointer(renderer->texcoords_aloc, vertex_texcoords_num, GL_FLOAT, GL_FALSE, vertex_size, (GLvoid const*)((uintptr_t)vertex_texcoords_offset));
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("texcoords vertex attrib", "glVertexAttribPointer"))
		return FALSE;

	if (!gst_imx_egl_viv_sink_gles2_renderer_update_display_ratio(renderer, &(renderer->video_info)))
		return FALSE;

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_teardown_resources(GstImxEglVivSinkGLES2Renderer *renderer)
{
	/* must be called with lock */

	gboolean ret = TRUE;

	/* && ret instead of ret && to avoid early termination */

	/* disable vertex attrib array and set up pointers */
	glDisableVertexAttribArray(renderer->position_aloc);
	ret = gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("position vertex attrib", "glDisableVertexAttribArray") && ret;
	glDisableVertexAttribArray(renderer->texcoords_aloc);
	ret = gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("texcoords vertex attrib", "glDisableVertexAttribArray") && ret;
	/* destroy vertex and index buffer objects */
	ret = gst_imx_egl_viv_sink_gles2_renderer_destroy_vertex_buffer(&(renderer->vertex_buffer)) && ret;

	/* destroy texture */
	glBindTexture(GL_TEXTURE_2D, 0);
	glDeleteTextures(1, &(renderer->texture));

	/* destroy shaders and program */
	ret = gst_imx_egl_viv_sink_gles2_renderer_destroy_program(&(renderer->program), renderer->vertex_shader, renderer->fragment_shader) && ret;
	ret = gst_imx_egl_viv_sink_gles2_renderer_destroy_shader(&(renderer->vertex_shader), GL_VERTEX_SHADER) && ret;
	ret = gst_imx_egl_viv_sink_gles2_renderer_destroy_shader(&(renderer->fragment_shader), GL_FRAGMENT_SHADER) && ret;

	renderer->tex_uloc = -1;
	renderer->frame_rect_uloc = -1;
	renderer->uv_scale_uloc = -1;
	renderer->position_aloc = -1;
	renderer->texcoords_aloc = -1;

	return ret;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_fill_texture(GstImxEglVivSinkGLES2Renderer *renderer, GstBuffer *buffer)
{
	GstVideoMeta *video_meta;
	GstMapInfo map_info;
	guint num_extra_lines, stride[3], offset[3], is_phys_buf;
	GstImxPhysMemMeta *phys_mem_meta;
	GstVideoFormat fmt;
	GLenum gl_format;
	GLuint w, h, total_w, total_h;
	
	phys_mem_meta = NULL;
	fmt = renderer->video_info.finfo->format;

	gl_format = gst_imx_egl_viv_sink_gles2_renderer_get_viv_format(fmt);
	w = renderer->video_info.width;
	h = renderer->video_info.height;

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(buffer);
	is_phys_buf = (phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0);

	/* Get the stride and number of extra lines */
	video_meta = gst_buffer_get_video_meta(buffer);
	if (video_meta != NULL)
	{
		for (guint i = 0; i < MIN(video_meta->n_planes, 3); ++i)
		{
			stride[i] = video_meta->stride[i];
			offset[i] = video_meta->offset[i];
		}
	}
	else
	{
		for (guint i = 0; i < MIN(GST_VIDEO_INFO_N_PLANES(&(renderer->video_info)), 3); ++i)
		{
			stride[i] = GST_VIDEO_INFO_PLANE_STRIDE(&(renderer->video_info), i);
			offset[i] = GST_VIDEO_INFO_PLANE_OFFSET(&(renderer->video_info), i);
		}
	}

	num_extra_lines = is_phys_buf ? (phys_mem_meta->padding / stride[0]) : 0;

	/* stride is in bytes, we need pixels */
	total_w = stride[0] / gst_imx_egl_viv_sink_gles2_renderer_bpp(fmt);
	total_h = h + num_extra_lines;

	GST_LOG("w/h: %d/%d total_w/h: %d/%d", w, h, total_w, total_h);

	glUniform2f(renderer->uv_scale_uloc, (float)w / (float)total_w, (float)h / (float)total_h);

	/* Only update texture if the video frame actually changed */
	if ((renderer->viv_planes[0] == NULL) || (renderer->video_info_updated))
	{
		GST_LOG("video frame did change");

		if (is_phys_buf)
		{
			GLvoid *virt_addr;
			GLuint phys_addr;

			phys_addr = (GLuint)(phys_mem_meta->phys_addr);

			GST_LOG("mapping physical address 0x%x of video frame in buffer %p into VIV texture", phys_addr, (gpointer)buffer);

			gst_buffer_map(buffer, &map_info, GST_MAP_READ);
			virt_addr = map_info.data;

			/* Just set to make sure the == NULL check above is false */
			renderer->viv_planes[0] = virt_addr;

			glTexDirectVIVMap(
				GL_TEXTURE_2D,
				total_w, total_h,
				gl_format,
				(GLvoid **)(&virt_addr), &phys_addr
			);

			gst_buffer_unmap(buffer, &map_info);
			GST_LOG("done showing frame in buffer %p with physical address 0x%x", (gpointer)buffer, phys_addr);

			if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("render", "glTexDirectVIVMap"))
				return FALSE;
		}
		else
		{
			glTexDirectVIV(
				GL_TEXTURE_2D,
				total_w, total_h,
				gl_format,
				(GLvoid **) &(renderer->viv_planes)
			);
			if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("render", "glTexDirectVIV"))
				return FALSE;

			GST_LOG("copying pixels into VIV direct texture buffer");

			gst_buffer_map(buffer, &map_info, GST_MAP_READ);
			switch (fmt)
			{
				case GST_VIDEO_FORMAT_I420:
				case GST_VIDEO_FORMAT_YV12:
					memcpy(renderer->viv_planes[0], map_info.data + offset[0], stride[0] * total_h);
					memcpy(renderer->viv_planes[1], map_info.data + offset[1], stride[1] * total_h / 2);
					memcpy(renderer->viv_planes[2], map_info.data + offset[2], stride[2] * total_h / 2);
					break;
				case GST_VIDEO_FORMAT_NV12:
				case GST_VIDEO_FORMAT_NV21:
					memcpy(renderer->viv_planes[0], map_info.data + offset[0], stride[0] * total_h);
					memcpy(renderer->viv_planes[1], map_info.data + offset[1], stride[1] * total_h / 2);
					break;
				default:
					memcpy(renderer->viv_planes[0], map_info.data, stride[0] * total_h);
			}
			gst_buffer_unmap(buffer, &map_info);

		}

		glTexDirectInvalidateVIV(GL_TEXTURE_2D);
		if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("render", "glTexDirectInvalidateVIV"))
			return FALSE;

		renderer->video_info_updated = FALSE;
	}
	else
	{
		GST_LOG("video frame did not change - not doing anything");
	}

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_render_current_frame(GstImxEglVivSinkGLES2Renderer *renderer)
{
	/* must be called with mutex lock */

	GST_LOG("rendering frame");

	glGetError(); /* clear out any existing error */

	glClear(GL_COLOR_BUFFER_BIT);
	if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("render", "glClear"))
		return FALSE;

	if (renderer->current_frame != NULL)
	{
		if (!gst_imx_egl_viv_sink_gles2_renderer_fill_texture(renderer, renderer->current_frame))
			return FALSE;

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		if (!gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("render", "glDrawElements"))
			return FALSE;
	}

	return TRUE;
}


static void gst_imx_egl_viv_sink_gles2_renderer_resize_callback(G_GNUC_UNUSED GstImxEglVivSinkEGLPlatform *platform, guint window_width, guint window_height, gpointer user_context)
{
	GstImxEglVivSinkGLES2Renderer *renderer = (GstImxEglVivSinkGLES2Renderer *)user_context;

	GST_TRACE("resize_callback w/h: %d/%d", window_width, window_height);

	GLES2_RENDERER_LOCK(renderer);

	if ((window_width != 0) && (window_height != 0))
	{
		glGetError(); /* clear out any existing error */

		renderer->window_width = window_width;
		renderer->window_height = window_height;

		glViewport(0, 0, renderer->window_width, renderer->window_height);

		GST_LOG("resizing viewport to %ux%u pixel", renderer->window_width, renderer->window_height);

		gst_imx_egl_viv_sink_gles2_renderer_update_display_ratio(renderer, &(renderer->video_info));

		gst_imx_egl_viv_sink_gles2_renderer_check_gl_error("viewport", "glViewport");
	}

	GLES2_RENDERER_UNLOCK(renderer);
}




GstImxEglVivSinkGLES2Renderer* gst_imx_egl_viv_sink_gles2_renderer_create(char const *native_display_name)
{
	GstImxEglVivSinkGLES2Renderer *renderer;

	init_debug_category();

	renderer = g_slice_alloc(sizeof(GstImxEglVivSinkGLES2Renderer));

	renderer->window_handle = 0;
	renderer->window_width = 0;
	renderer->window_height = 0;
	renderer->event_handling = TRUE;
	renderer->display_ratio_n = 1;
	renderer->display_ratio_d = 1;
	renderer->video_info_updated = TRUE;
	renderer->fullscreen = FALSE;

	renderer->manual_x_coord = 0;
	renderer->manual_y_coord = 0;
	renderer->manual_width = 0;
	renderer->manual_height = 0;
	renderer->borderless = FALSE;

	renderer->current_frame = NULL;

	renderer->egl_platform = gst_imx_egl_viv_sink_egl_platform_create(
		native_display_name,
		gst_imx_egl_viv_sink_gles2_renderer_resize_callback,
		gst_imx_egl_viv_sink_gles2_renderer_render_frame,
		renderer
	);
	if (renderer->egl_platform == NULL)
	{
		g_slice_free1(sizeof(GstImxEglVivSinkGLES2Renderer), renderer);
		return NULL;
	}

	renderer->thread_started = FALSE;
	renderer->force_aspect_ratio = TRUE;
	renderer->loop_flow_retval = GST_FLOW_OK;
	renderer->thread = NULL;
	g_mutex_init(&(renderer->mutex));

	renderer->vertex_shader = 0;
	renderer->fragment_shader = 0;
	renderer->program = 0;
	renderer->vertex_buffer = 0;
	renderer->texture = 0;

	renderer->tex_uloc = -1;
	renderer->frame_rect_uloc = -1;
	renderer->uv_scale_uloc = -1;
	renderer->position_aloc = -1;
	renderer->texcoords_aloc = -1;

	renderer->viv_planes[0] = NULL;

	return renderer;
}


void gst_imx_egl_viv_sink_gles2_renderer_destroy(GstImxEglVivSinkGLES2Renderer *renderer)
{
	if (renderer == NULL)
		return;

	GST_INFO("stopping renderer");
	gst_imx_egl_viv_sink_gles2_renderer_stop(renderer);

	if (renderer->egl_platform != NULL)
	{
		GST_INFO("destroying EGL platform");
		gst_imx_egl_viv_sink_egl_platform_destroy(renderer->egl_platform);
	}

	g_mutex_clear(&(renderer->mutex));

	g_slice_free1(sizeof(GstImxEglVivSinkGLES2Renderer), renderer);
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_start(GstImxEglVivSinkGLES2Renderer *renderer)
{
	GError *error;

	if (renderer->thread_started)
		return TRUE;

	renderer->loop_flow_retval = GST_FLOW_OK;
	renderer->video_info_updated = TRUE;

	renderer->thread = g_thread_try_new("eglvivsink-gles2-renderer", gst_imx_egl_viv_sink_gles2_renderer_thread, renderer, &error);
	if (renderer->thread == NULL)
	{
		if ((error != NULL) && (error->message != NULL))
			GST_ERROR("could not start thread: %s", error->message);
		else
			GST_ERROR("could not start thread: unknown error");

		if (error != NULL)
			g_error_free(error);

		return FALSE;
	}

	renderer->thread_started = TRUE;

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_stop(GstImxEglVivSinkGLES2Renderer *renderer)
{
	gboolean ret = TRUE;

	if (renderer->thread_started)
	{
		gst_imx_egl_viv_sink_egl_platform_stop_mainloop(renderer->egl_platform);

		GST_LOG("waiting for thread to finish");

		g_thread_join(renderer->thread);
		/* no need to explicitely unref the thread, since g_thread_join() does this already */

		if (renderer->current_frame != NULL)
			gst_buffer_unref(renderer->current_frame);
		renderer->current_frame = NULL;

		renderer->thread_started = FALSE;
		renderer->thread = NULL;
	}
	else
	{
		GST_LOG("thread not running - nothing to stop");
	}

	return ret;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_is_started(GstImxEglVivSinkGLES2Renderer *renderer)
{
	return renderer->thread_started;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_window_handle(GstImxEglVivSinkGLES2Renderer *renderer, guintptr window_handle)
{
	gboolean ret = TRUE;

	if (renderer->window_handle == window_handle)
		return TRUE;

	renderer->window_handle = window_handle;

	if (renderer->thread_started)
	{
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_stop(renderer);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_start(renderer);
	}

	return ret;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_event_handling(GstImxEglVivSinkGLES2Renderer *renderer, gboolean event_handling)
{
	if (renderer->event_handling == event_handling)
		return TRUE;

	renderer->event_handling = event_handling;
	if (renderer->thread_started)
		gst_imx_egl_viv_sink_egl_platform_set_event_handling(renderer->egl_platform, renderer->event_handling);

	return TRUE;
}


static gboolean gst_imx_egl_viv_sink_gles2_renderer_update_display_ratio(GstImxEglVivSinkGLES2Renderer *renderer, GstVideoInfo *video_info)
{
	/* must be called with lock */

	float display_scale_w, display_scale_h;

	if (renderer->force_aspect_ratio && (renderer->window_width != 0) && (renderer->window_height != 0))
	{
		gint video_par_n, video_par_d, window_par_n, window_par_d;
		float norm_ratio;

		video_par_n = video_info->par_n;
		video_par_d = video_info->par_d;
		window_par_n = 1;
		window_par_d = 1;

		if (!gst_video_calculate_display_ratio(&(renderer->display_ratio_n), &(renderer->display_ratio_d), video_info->width, video_info->height, video_par_n, video_par_d, window_par_n, window_par_d))
		{
			GLES2_RENDERER_UNLOCK(renderer);
			GST_ERROR("could not calculate display ratio");
			return FALSE;
		}

		norm_ratio = (float)(renderer->display_ratio_n) / (float)(renderer->display_ratio_d) * (float)(renderer->window_height) / (float)(renderer->window_width);

		GST_LOG(
			"video width/height: %dx%d  video pixel aspect ratio: %d/%d  window pixel aspect ratio: %d/%d  calculated display ratio: %d/%d  window width/height: %dx%d  norm ratio: %f",
			video_info->width, video_info->height,
			video_par_n, video_par_d,
			window_par_n, window_par_d,
			renderer->display_ratio_n, renderer->display_ratio_d,
			renderer->window_width, renderer->window_height,
			norm_ratio
		);

		if (norm_ratio >= 1.0f)
		{
			display_scale_w = 1.0f;
			display_scale_h = 1.0f / norm_ratio;
		}
		else
		{
			display_scale_w = norm_ratio;
			display_scale_h = 1.0f;
		}
	}
	else
	{
		renderer->display_ratio_n = 1;
		renderer->display_ratio_d = 1;
		display_scale_w = 1.0f;
		display_scale_h = 1.0f;
	}

	if (renderer->frame_rect_uloc != -1)
	{
		GST_LOG(
			"display scale: %f/%f",
			display_scale_w, display_scale_h
		);
		glUniform2f(renderer->frame_rect_uloc, display_scale_w, display_scale_h);
	}

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_video_info(GstImxEglVivSinkGLES2Renderer *renderer, GstVideoInfo *video_info)
{
	GLES2_RENDERER_LOCK(renderer);

	if (!gst_imx_egl_viv_sink_gles2_renderer_update_display_ratio(renderer, video_info))
	{
		GLES2_RENDERER_UNLOCK(renderer);
		return FALSE;
	}

	renderer->video_info = *video_info;
	renderer->video_info_updated = TRUE;

	GLES2_RENDERER_UNLOCK(renderer);

	gst_imx_egl_viv_sink_egl_platform_set_video_info(renderer->egl_platform, video_info);

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_fullscreen(GstImxEglVivSinkGLES2Renderer *renderer, gboolean fullscreen)
{
	gboolean ret = TRUE;

	if (renderer->fullscreen == fullscreen)
		return TRUE;

	renderer->fullscreen = fullscreen;

	if (renderer->thread_started)
	{
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_stop(renderer);
		ret = ret && gst_imx_egl_viv_sink_gles2_renderer_start(renderer);
	}

	if (!ret)
		GST_ERROR("%s fullscreen mode failed", fullscreen ? "enabling" : "disabling");

	return ret;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_force_aspect_ratio(GstImxEglVivSinkGLES2Renderer *renderer, gboolean force_aspect_ratio)
{
	GLES2_RENDERER_LOCK(renderer);

	renderer->force_aspect_ratio = force_aspect_ratio;
	if (!gst_imx_egl_viv_sink_gles2_renderer_update_display_ratio(renderer, &(renderer->video_info)))
	{
		GLES2_RENDERER_UNLOCK(renderer);
		return FALSE;
	}

	GLES2_RENDERER_UNLOCK(renderer);

	return TRUE;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_window_coords(GstImxEglVivSinkGLES2Renderer *renderer, gint window_x_coord, gint window_y_coord)
{
	renderer->manual_x_coord = window_x_coord;
	renderer->manual_y_coord = window_y_coord;
	return renderer->thread_started ? gst_imx_egl_viv_sink_egl_platform_set_coords(renderer->egl_platform, window_x_coord, window_y_coord) : TRUE;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_window_size(GstImxEglVivSinkGLES2Renderer *renderer, guint window_width, guint window_height)
{
	renderer->manual_width = window_width;
	renderer->manual_height = window_height;
	return renderer->thread_started ? gst_imx_egl_viv_sink_egl_platform_set_size(renderer->egl_platform, window_width, window_height) : TRUE;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_set_borderless_window(GstImxEglVivSinkGLES2Renderer *renderer, gboolean borderless_window)
{
	renderer->borderless = borderless_window;
	return renderer->thread_started ? gst_imx_egl_viv_sink_egl_platform_set_borderless(renderer->egl_platform, borderless_window) : TRUE;
}


GstFlowReturn gst_imx_egl_viv_sink_gles2_renderer_show_frame(GstImxEglVivSinkGLES2Renderer *renderer, GstBuffer *buf)
{
	GstFlowReturn ret;

	GLES2_RENDERER_LOCK(renderer);

	ret = renderer->loop_flow_retval;

	if (ret == GST_FLOW_OK)
	{
		if (renderer->current_frame != buf)
		{
			if (renderer->current_frame != NULL)
				gst_buffer_unref(renderer->current_frame);
			renderer->current_frame = buf;
			if (renderer->current_frame != NULL)
				gst_buffer_ref(renderer->current_frame);
			renderer->viv_planes[0] = NULL;
		}

	}

	GLES2_RENDERER_UNLOCK(renderer);

	if (ret == GST_FLOW_OK)
	{
		if (!gst_imx_egl_viv_sink_gles2_renderer_expose(renderer))
			ret = GST_FLOW_ERROR;
	}

	return ret;
}


gboolean gst_imx_egl_viv_sink_gles2_renderer_expose(GstImxEglVivSinkGLES2Renderer *renderer)
{
	return gst_imx_egl_viv_sink_egl_platform_expose(renderer->egl_platform);
}

