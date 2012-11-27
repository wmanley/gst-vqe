/* GStreamer VQE element
 *
 * Copyright (C) 2012 YouView TV Ltd.
 *
 * Author: William Manley <william.manley@youview.com>
 */


#ifndef __GST_VQESRC_H__
#define __GST_VQESRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <vqec_ifclient.h>
#include <vqec_ifclient_read.h>

G_BEGIN_DECLS

#define GST_TYPE_VQESRC \
  (gst_vqesrc_get_type())
#define GST_VQESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VQESRC,GstVQESrc))
#define GST_VQESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VQESRC,GstVQESrcClass))
#define GST_IS_VQESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VQESRC))
#define GST_IS_VQESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VQESRC))
#define GST_VQESRC_CAST(obj) ((GstVQESrc *)(obj))

typedef struct _GstVQESrc GstVQESrc;
typedef struct _GstVQESrcClass GstVQESrcClass;

struct _GstVQESrc {
  GstPushSrc parent;

  /* properties */
  gchar     *sdp;
  gchar     *cfg;

  /* VQE resources */
  
  vqec_tunerid_t tuner;
  GstTask *vqe_task;
  GRecMutex vqe_task_lock;

};

struct _GstVQESrcClass {
  GstPushSrcClass parent_class;
};

GType gst_vqesrc_get_type(void);

G_END_DECLS


#endif /* __GST_VQESRC_H__ */
