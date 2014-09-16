#ifndef GST_IMX_COMMON_FD_OBJECT_H
#define GST_IMX_COMMON_FD_OBJECT_H

#include <gst/gst.h>


G_BEGIN_DECLS


GST_EXPORT GType _gst_imx_fd_object_type;

typedef struct _GstImxFDObject GstImxFDObject;

#define GST_IMX_FD_OBJECT_TYPE                        (gst_imx_fd_object_api_get_type())
#define GST_IMX_IS_FD_OBJECT(obj)                     (GST_IS_MINI_OBJECT_TYPE((obj), GST_IMX_FD_OBJECT_TYPE))
#define GST_IMX_FD_OBJECT_CAST(obj)                   ((GstImxFDObject *)(obj))
#define GST_IMX_FD_OBJECT(obj)                        (GST_IMX_FD_OBJECT_CAST(obj))

#define GST_IMX_FD_OBJECT_GET_FD(obj)                 (GST_IMX_FD_OBJECT_CAST(obj)->fd)


/* This is a miniobject that handles the lifetime of a file descriptor
 * with refcounting. Once the refcount reaches zero, the specified file
 * descriptor is closed. Useful for resources that may be used by
 * multiple entities, since then, the refcounting ensures the FD is
 * closed only when all these entities are shut down.
 */


struct _GstImxFDObject
{
	GstMiniObject mini_object;
	int fd;
};


static inline GstImxFDObject* gst_imx_fd_object_ref(GstImxFDObject *fd_object)
{
	return (GstImxFDObject *)gst_mini_object_ref(GST_MINI_OBJECT_CAST(fd_object));
}


static inline void gst_imx_fd_object_unref(GstImxFDObject *fd_object)
{
	gst_mini_object_unref(GST_MINI_OBJECT_CAST(fd_object));
}


GType gst_imx_fd_object_api_get_type(void);
GstImxFDObject* gst_fd_object_new(int fd);


G_END_DECLS


#endif
