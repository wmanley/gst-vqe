/* GStreamer VQE element
 *
 * Copyright (C) 2012 YouView TV Ltd.
 *
 * Author: William Manley <william.manley@youview.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvqesrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "vqesrc", GST_RANK_NONE, GST_TYPE_VQESRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    udp,
    "Receive RTP over UDP with retransmission and rapid channel change",
    plugin_init, "0.11", "BSD", "gst-vqe",
    "http://youview.com/")
