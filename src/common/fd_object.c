#include <unistd.h>
#include "fd_object.h"


GST_DEBUG_CATEGORY_STATIC(imx_fd_object_debug);
#define GST_CAT_DEFAULT imx_fd_object_debug


GType gst_imx_fd_object_api_get_type(void)
{
	static volatile GType type;
	static gchar const *tags[] = { "resource", "filedecriptor", NULL };

	if (g_once_init_enter(&type))
	{
		GType _type = gst_meta_api_type_register("GstImxFDObjectAPI", tags);
		g_once_init_leave(&type, _type);

		GST_DEBUG_CATEGORY_INIT(imx_fd_object_debug, "imxfdobject", 0, "File descriptor object");
	}

	return type;
}


static void gst_fd_object_free(GstImxFDObject *obj)
{
	if (obj->fd != -1)
	{
		GST_LOG("close FD %d in object %p", obj->fd, (gpointer)obj);
		close(obj->fd);
		obj->fd = -1;
	}
}


static void gst_fd_object_init(GstImxFDObject *obj, int fd)
{
	gst_mini_object_init(
		GST_MINI_OBJECT_CAST(obj),
		0,
		gst_imx_fd_object_api_get_type(),
		NULL,
		NULL,
		(GstMiniObjectFreeFunction)gst_fd_object_free
	);

	obj->fd = fd;
}


GstImxFDObject* gst_fd_object_new(int fd)
{
	GstImxFDObject *obj;

	obj = g_slice_new(GstImxFDObject);
	gst_fd_object_init(obj, fd);

	GST_LOG("new %p with FD %d", (gpointer)obj, fd);

	return GST_IMX_FD_OBJECT_CAST(obj);
}
