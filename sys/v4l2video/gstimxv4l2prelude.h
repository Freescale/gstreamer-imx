#ifndef GST_IMX_V4L2_PRELUDE_H
#define GST_IMX_V4L2_PRELUDE_H

/* This is needed for enabling CLOCK_MONOTONIC and the timespec struct */
#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif /* __STDC_VERSION__ */

#endif /* GST_IMX_V4L2_PRELUDE_H */
