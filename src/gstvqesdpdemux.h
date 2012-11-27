/* GStreamer VQE SDP Demuxer element
 *
 * Copyright (C) 2012 YouView TV Ltd.
 *
 * Author: William Manley <william.manley@youview.com>
 */


#ifndef __GST_VQE_SDP_DEMUX_H__
#define __GST_VQE_SDP_DEMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VQE_SDP_DEMUX \
  (gst_vqe_sdp_demux_get_type())
#define GST_VQE_SDP_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VQE_SDP_DEMUX,GstVQESDPDemux))
#define GST_VQE_SDP_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VQE_SDP_DEMUX,GstVQESDPDemuxClass))
#define GST_IS_VQE_SDP_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VQE_SDP_DEMUX))
#define GST_IS_VQE_SDP_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VQE_SDP_DEMUX))
#define GST_VQE_SDP_DEMUX_CAST(obj) ((GstVQESDPDemux *) (obj))

typedef struct _GstVQESDPDemux GstVQESDPDemux;
typedef struct _GstVQESDPDemuxClass GstVQESDPDemuxClass;

struct _GstVQESDPDemux
{
  GstBin parent_instance;

  GstBuffer * sdpfile;
  gboolean sdpfile_complete;

  GstElement * vqesrc;
};

struct _GstVQESDPDemuxClass
{
  GstBinClass parent_class;
};

GType gst_vqe_sdp_demux_get_type (void);

G_END_DECLS


#endif /* __GST_VQE_SDP_DEMUX_H__ */
