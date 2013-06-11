/* GStreamer VQE element
 *
 * Copyright (C) 2012 YouView TV Ltd.
 *
 * Author: William Manley <william.manley@youview.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * or under the terms of the Cisco style BSD license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
  vqec_ifclient_tr135_params_t tr135_params;

  /* parsed stream uri used for stats queries */
  char stream_uri[128];
};

struct _GstVQESrcClass {
  GstPushSrcClass parent_class;
};

GType gst_vqesrc_get_type(void);

G_END_DECLS


#endif /* __GST_VQESRC_H__ */
