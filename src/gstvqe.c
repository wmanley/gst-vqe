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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvqesrc.h"
#include "gstvqesdpdemux.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "vqesrc", GST_RANK_NONE, GST_TYPE_VQESRC))
    return FALSE;
  if (!gst_element_register (plugin, "vqesdpdemux", GST_RANK_PRIMARY, GST_TYPE_VQE_SDP_DEMUX))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vqe,
    "Receive RTP over UDP with retransmission and rapid channel change",
    plugin_init, "0.11", "BSD", "gst-vqe",
    "http://youview.com/")
