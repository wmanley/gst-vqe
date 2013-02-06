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

/**
 * SECTION:element-vqesrc
 *
 * vqesrc is a network source that implements RTP including rapid channel
 * change and RTP retransmission.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v vqesrc sdp="xyz" ! decodebin2 ! xvimagesink
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvqesrc.h"

#include <gst/net/gstnetaddressmeta.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include <vqec_ifclient_defs.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

GST_DEBUG_CATEGORY_STATIC (vqesrc_debug);
#define GST_CAT_DEFAULT (vqesrc_debug)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/mpegts"));

#define VQE_DEFAULT_SDP                 ""
#define VQE_DEFAULT_CFG                 ""

/*
 * A word of explanation here...
 * Cisco's VQEC was designed in sucha way that it makes it quite a pain
 * to turn it into a GST element. This is mainly because of a global shared
 * state. The fact that there's a global  vqec_ifclient_init function or 
 * global vqec_ifclient_start event loop illustrates my point.
 * To make it work as gstreamer elements are expected to work I've hammered
 * in a mutex and a refcount that are used to pass the ownership of the event
 * loop worker thread. This isn't pretty, but it works. 
 * 
 * Thanks for understanding,
 * Anonymouse.
 */

GMutex vqe_owner_mutex;              /* refcount and task access lock */
GRecMutex vqe_owner_task_mutex;      /* gstreamer task lock           */
GstTask *vqe_owner_task = NULL;      /* worker thread task            */
size_t  vqe_owner_refcount = 0;      /* shared state refcout          */

static const size_t buffer_size = 4096; /* 4KB */

enum
{
  PROP_0,

  PROP_SDP,
  PROP_CFG,

  PROP_VQEC_PRIMARY_UDP_INPUTS,
  PROP_VQEC_PRIMARY_UDP_DROPS,
  PROP_VQEC_PRIMARY_RTP_INPUTS,
  PROP_VQEC_PRIMARY_RTP_DROPS,
  PROP_VQEC_PRIMARY_RTP_DROPS_LATE,
  PROP_VQEC_PRIMARY_RTCP_INPUTS,
  PROP_VQEC_PRIMARY_RTCP_OUTPUTS,
  PROP_VQEC_REPAIR_RTP_INPUTS,
  PROP_VQEC_REPAIR_RTP_DROPS,
  PROP_VQEC_REPAIR_RTP_DROPS_LATE,
  PROP_VQEC_REPAIR_RTCP_INPUTS,
  PROP_VQEC_FEC_INPUTS,
  PROP_VQEC_FEC_DROPS,
  PROP_VQEC_FEC_DROPS_LATE,
  PROP_VQEC_REPAIR_RTP_STUN_INPUTS,
  PROP_VQEC_REPAIR_RTP_STUN_OUTPUTS,
  PROP_VQEC_REPAIR_RTCP_STUN_INPUTS,
  PROP_VQEC_REPAIR_RTCP_STUN_OUTPUTS,
  PROP_VQEC_POST_REPAIR_OUTPUTS,
  PROP_VQEC_TUNER_QUEUE_DROPS,
  PROP_VQEC_UNDERRUNS,
  PROP_VQEC_PRE_REPAIR_LOSSES,
  PROP_VQEC_POST_REPAIR_LOSSES,
  PROP_VQEC_POST_REPAIR_LOSSES_RCC,
  PROP_VQEC_REPAIRS_REQUESTED,
  PROP_VQEC_REPAIRS_POLICED,
  PROP_VQEC_FEC_RECOVERED_PAKS,
  /* TODO: not implemented yet */
  PROP_VQEC_CHANNEL_CHANGE_REQUESTS,
  PROP_VQEC_RCC_REQUESTS,
  PROP_VQEC_CONCURRENT_RCCS_LIMITED,
  PROP_VQEC_RCC_WITH_LOSS,
  PROP_VQEC_RCC_ABORTS_TOTAL,
  
  PROP_TR135_OVERRUNS,
  PROP_TR135_UNDERRUNS,
  PROP_TR135_PACKETS_EXPECTED,
  PROP_TR135_PACKETS_RECEIVED,
  PROP_TR135_PACKETS_LOST,
  PROP_TR135_PACKETS_LOST_BEFORE_EC,
  PROP_TR135_LOSS_EVENTS,
  PROP_TR135_LOSS_EVENTS_BEFORE_EC,
  PROP_TR135_SEVERE_LOSS_INDEX_COUNT,
  PROP_TR135_MINIMUM_LOSS_DISTANCE,
  PROP_TR135_MAXIMUM_LOSS_PERIOD,
  PROP_TR135_BUFFER_SIZE,
  PROP_TR135_GMIN,
  PROP_TR135_SEVERE_LOSS_MIN_DISTANCE,

  PROP_LAST
};

static GstFlowReturn gst_vqesrc_create (GstPushSrc * psrc, GstBuffer ** buf);

static gboolean gst_vqesrc_start (GstBaseSrc * bsrc);

static gboolean gst_vqesrc_stop (GstBaseSrc * bsrc);

static gboolean gst_vqesrc_unlock (GstBaseSrc * bsrc);

static gboolean gst_vqesrc_unlock_stop (GstBaseSrc * bsrc){ return FALSE; }

static void gst_vqesrc_finalize (GObject * object);

static void gst_vqesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vqesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_vqesrc_parent_class parent_class
G_DEFINE_TYPE (GstVQESrc, gst_vqesrc, GST_TYPE_PUSH_SRC);

static void
gst_vqesrc_class_init (GstVQESrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;
  int err;
  char* vqec_config = NULL;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  GST_DEBUG_CATEGORY_INIT (vqesrc_debug, "vqesrc", 0, "VQE src");

  gobject_class->set_property = gst_vqesrc_set_property;
  gobject_class->get_property = gst_vqesrc_get_property;
  gobject_class->finalize = gst_vqesrc_finalize;

  g_object_class_install_property (gobject_class, PROP_SDP,
      g_param_spec_string ("sdp", "SDP",
          "Stream description in SDP format", VQE_DEFAULT_SDP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CFG,
      g_param_spec_string ("cfg", "Server Configuration file",
          "cfg in form of file path /tmp/sample-vqec.config", VQE_DEFAULT_CFG,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRIMARY_UDP_INPUTS,
      g_param_spec_uint64 ("primary-udp-inputs", "PRIMARY_UDP_INPUTS",
          "primary udp mpeg pkts received", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRIMARY_UDP_DROPS,
      g_param_spec_uint64 ("primary-udp-drops", "PROP_VQEC_PRIMARY_UDP_DROPS",
          "primary udp mpeg pkts dropped, due to a failure to have a valid MPEG sync byte as the first byte in the pkt payload", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRIMARY_RTP_INPUTS,
      g_param_spec_uint64 ("primary-rtp-inputs", "PROP_VQEC_PRIMARY_RTP_INPUTS",
          "primary rtp pkts received ", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRIMARY_RTP_DROPS,
      g_param_spec_uint64 ("primary-rtp-drops", "PROP_VQEC_PRIMARY_RTP_DROPS",
          "primary rtp pkts dropped, due to reasons such as: o RTP parse failure o too early (before join) o too late for playout o drop simulator tool This counter EXCLUDES drops due to duplicate pkts being recvd", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRIMARY_RTP_DROPS_LATE,
      g_param_spec_uint64 ("primary-rtp-drops-late", "PROP_VQEC_PRIMARY_RTP_DROPS_LATE",
          "primary rtp pkts dropped due to arriving too late (after time needed by output scheduler)", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRIMARY_RTCP_INPUTS,
      g_param_spec_uint64 ("primary-rtcp-inputs", "PROP_VQEC_PRIMARY_RTCP_INPUTS",
          "primary rtcp pkts received", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRIMARY_RTCP_OUTPUTS,
      g_param_spec_uint64 ("primary-rtcp-outputs", "PROP_VQEC_PRIMARY_RTCP_OUTPUTS",
          "primary rtcp packets sent", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTP_INPUTS,
      g_param_spec_uint64 ("repair-rtp-inputs", "PROP_VQEC_REPAIR_RTP_INPUTS",
          "repair/rcc rtp pkts received", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTP_DROPS,
      g_param_spec_uint64 ("repair-rtp-drops", "PROP_VQEC_REPAIR_RTP_DROPS",
          "repair/rcc rtp pkts dropped due to reasons such as:\n"
          "                         o RTP parse failure\n"
          "                         o too early (before join)\n"   
          "                         o too late for playout\n"
          "                         o repairs preceed first sequence number from RCC APP\n"
          "                         o drop simulator tool\n"         
          "                        This counter EXCLUDES drops due to duplicate pkts being received."
          , 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTP_DROPS_LATE,
      g_param_spec_uint64 ("repair-rtp-drops-late", "PROP_VQEC_REPAIR_RTP_DROPS_LATE",
          "repair/rcc rtp pkts dropped due to arriving too late (after time needed by output scheduler)", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTCP_INPUTS,
      g_param_spec_uint64 ("repair-rtcp-input", "PROP_VQEC_REPAIR_RTCP_INPUTS",
          "repair/rcc rtcp pkts received", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_FEC_INPUTS,
      g_param_spec_uint64 ("fec-inputs", "PROP_VQEC_FEC_INPUTS",
          "fec pkts receive", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_FEC_DROPS,
      g_param_spec_uint64 ("fec-drops", "PROP_VQEC_FEC_DROPS",
           "fec pkts dropped, due to reasons such as:             \n"
           "                         o invalid RTP header       \n"
           "                         o invalid FEC header       \n"
           "                         o packet arrived too late  \n"
           "                         o memory allocation error while processing FEC pkt o etc."
          , 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_FEC_DROPS_LATE,
      g_param_spec_uint64 ("fec-drops-late", "PROP_VQEC_FEC_DROPS_LATE",
          "fec pkts which arrived too late (a primary packet to which it"
          " refers has already been scheduled for output)", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTP_STUN_INPUTS,
      g_param_spec_uint64 ("repair-rtp-stun-inputs", "PROP_VQEC_REPAIR_RTP_STUN_INPUTS",
          "STUN pts rcvd on repair rtp port" , 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTP_STUN_OUTPUTS,
      g_param_spec_uint64 ("repair-rtp-stun-outputs", "PROP_VQEC_REPAIR_RTP_STUN_OUTPUTS",
          "STUN pkts sent on repair rtp port" , 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTCP_STUN_INPUTS,
      g_param_spec_uint64 ("repair-rtcp-stun-inputs", "PROP_VQEC_REPAIR_RTCP_STUN_INPUTS",
          "STUN pkts rcvd on repair rtcp port" , 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIR_RTCP_STUN_OUTPUTS,
      g_param_spec_uint64 ("repair-rtcp-stun-outputs", "PROP_VQEC_REPAIR_RTCP_STUN_OUTPUTS",
          "STUN pkts sent on repair rtcp port" , 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_POST_REPAIR_OUTPUTS,
      g_param_spec_uint64 ("post-repair-outputs", "PROP_VQEC_POST_REPAIR_OUTPUTS",
          "post repair stream packets (common to all tuners which are tuned to the same channel)" ,
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_TUNER_QUEUE_DROPS,
      g_param_spec_uint64 ("tuner-queue-drops", "PROP_VQEC_TUNER_QUEUE_DROPS",
          "drops during pak enqueue on tuner/sink (e.g. due to queue limit reached)" ,
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_UNDERRUNS,
      g_param_spec_uint64 ("underruns", "PROP_VQEC_UNDERRUNS",
          "underruns upon inserting packets of the input streams" ,
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_PRE_REPAIR_LOSSES,
      g_param_spec_uint64 ("pre-repair-losses", "PROP_VQEC_PRE_REPAIR_LOSSES",
           "number of primary rtp pkts not arriving within the stream\n"
           "                        E.g. an arriving pkt stream of sequence numbers\n"
           "                        1,4,5,7,8 will bump this counter 3 times",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_POST_REPAIR_LOSSES,
      g_param_spec_uint64 ("post-repair-losses", "PROP_VQEC_POST_REPAIR_LOSSES",
          "number of rtp packets which were missing (not repaired) upon output to the tuner", 
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_POST_REPAIR_LOSSES_RCC,
      g_param_spec_uint64 ("post-repair-losses-rcc", "PROP_VQEC_POST_REPAIR_LOSSES_RCC",
           "number of rcc rtp pkts which were missing (not repaired) upon output to the tuner\n"      
           "                        I.e. any packets missing from an RCC burst and not repaired would be counted by this counter", 
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIRS_REQUESTED,
      g_param_spec_uint64 ("repairs-requested", "PROP_VQEC_REPAIRS_REQUESTED",
           "number of repair packets requested by VQE-C",      
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_REPAIRS_POLICED,
      g_param_spec_uint64 ("repairs-policed", "PROP_VQEC_REPAIRS_POLICED",
           "number of repair requests not sent due to rate limiting",      
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VQEC_FEC_RECOVERED_PAKS,
      g_param_spec_uint64 ("fec-recovered-paks", "PROP_VQEC_FEC_RECOVERED_PAKS",
           "packets successfully regenerated/repaired by FEC",      
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /*
   *    TR135 stats
   */


  g_object_class_install_property (gobject_class, PROP_TR135_OVERRUNS,
      g_param_spec_uint64 ("tr135-overruns", "PROP_TR135_OVERRUNS",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_UNDERRUNS,
      g_param_spec_uint64 ("tr135-underruns", "PROP_TR135_UNDERRUNS",
           "some tr135 nonsense", 
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_PACKETS_EXPECTED,
      g_param_spec_uint64 ("tr135-packets-expected", "PROP_TR135_PACKETS_EXPECTED",
           "some tr135 nonsense", 
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_PACKETS_RECEIVED,
      g_param_spec_uint64 ("tr135-packets-received", "PROP_TR135_PACKETS_RECEIVED",
           "some tr135 nonsense", 
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_PACKETS_LOST,
      g_param_spec_uint64 ("tr135-packets-lost", "PROP_TR135_PACKETS_LOST",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_PACKETS_LOST_BEFORE_EC,
      g_param_spec_uint64 ("tr135-packets-lost-before-ec", "PROP_TR135_PACKETS_LOST_BEFORE_EC",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_LOSS_EVENTS,
      g_param_spec_uint64 ("tr135-loss-events", "PROP_TR135_LOSS_EVENTS",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_LOSS_EVENTS_BEFORE_EC,
      g_param_spec_uint64 ("tr135-loss-events-before-ec", "PROP_TR135_LOSS_EVENTS_BEFORE_EC",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_SEVERE_LOSS_INDEX_COUNT,
      g_param_spec_uint64 ("tr135-severe-loss-index-count", "PROP_TR135_SEVERE_LOSS_INDEX_COUNT",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_MINIMUM_LOSS_DISTANCE,
      g_param_spec_uint64 ("tr135-minimum-loss-distance", "PROP_TR135_MINIMUM_LOSS_DISTANCE",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_MAXIMUM_LOSS_PERIOD,
      g_param_spec_uint64 ("tr135-maximum-loss-period", "PROP_TR135_MAXIMUM_LOSS_PERIOD",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_BUFFER_SIZE,
      g_param_spec_uint64 ("tr135-buffer-size", "PROP_TR135_BUFFER_SIZE",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_GMIN,
      g_param_spec_uint64 ("tr135-gmin", "PROP_TR135_GMIN",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TR135_SEVERE_LOSS_MIN_DISTANCE,
      g_param_spec_uint64 ("tr135-severe-loss-min-distance", "PROP_TR135_SEVERE_LOSS_MIN_DISTANCE",
           "some tr135 nonsense",
           0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));


  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP Receiver", "Source/Network",
      "Tune to multicast RTP streams",
      "William Manley <william.manley@youview.com>");

  gstbasesrc_class->start = gst_vqesrc_start;
  gstbasesrc_class->stop = gst_vqesrc_stop;
  gstbasesrc_class->unlock = gst_vqesrc_unlock;
  gstbasesrc_class->unlock_stop = gst_vqesrc_unlock_stop;

  gstpushsrc_class->create = gst_vqesrc_create;

  vqec_config = getenv("GSTVQE_CFG_PATH");

  if ( !vqec_config )
  {
    vqec_config = CONFIG_DIR "/vqe-c/vqe-c.cfg";
  }
  GST_INFO(stderr, "VQEC: initialising with config file: %s\n", vqec_config );
  err = vqec_ifclient_init( vqec_config );
  if (err) {
    GST_INFO(stderr, "Failed to initialise VQE-C: %s\n", vqec_err2str(err));
  }

  g_mutex_init ( &vqe_owner_mutex );
  g_rec_mutex_init ( &vqe_owner_task_mutex );
}

static void
gst_vqesrc_init (GstVQESrc * vqesrc)
{
  vqesrc->sdp = g_strdup (VQE_DEFAULT_SDP);
  vqesrc->cfg = g_strdup (VQE_DEFAULT_CFG);

  /* configure basesrc to be a live source */
  gst_base_src_set_live (GST_BASE_SRC (vqesrc), TRUE);
  /* make basesrc output a segment in time */
  gst_base_src_set_format (GST_BASE_SRC (vqesrc), GST_FORMAT_TIME);
  /* make basesrc set timestamps on outgoing buffers based on the running_time
   * when they were captured */
  gst_base_src_set_do_timestamp (GST_BASE_SRC (vqesrc), TRUE);
}

static void
gst_vqesrc_finalize (GObject * object)
{
  GstVQESrc *vqesrc;

  vqesrc = GST_VQESRC (object);

  GST_OBJECT_LOCK (vqesrc);

  g_free (vqesrc->sdp);
  vqesrc->sdp = NULL;

  g_free (vqesrc->cfg);
  vqesrc->cfg = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);

  GST_OBJECT_UNLOCK (vqesrc);
}

static GstFlowReturn
gst_vqesrc_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstVQESrc *vqesrc;
  GstBuffer *outbuf;

  vqesrc = GST_VQESRC_CAST (psrc);

  GST_OBJECT_LOCK (vqesrc);

  /* TODO: deal with cancellation somehow... Probably need to return
     GST_FLOW_FLUSHING */
  int32_t bytes_read = 0;
  vqec_iobuf_t buflist[1];
  memset(buflist, 0, sizeof(buflist));

  buflist[0].buf_ptr = g_malloc(buffer_size);
  buflist[0].buf_len = buffer_size;
  if (!buflist[0].buf_ptr) {
    GST_ELEMENT_ERROR(GST_ELEMENT(vqesrc), RESOURCE,
                      FAILED, (NULL),
                      ("g_malloc(%i) failed", buffer_size));
    goto error;
  }

  vqec_error_t err = vqec_ifclient_tuner_recvmsg(vqesrc->tuner, buflist, 1, &bytes_read, 1000000);
  if (err) {
    GST_ELEMENT_ERROR(GST_ELEMENT(vqesrc), RESOURCE,
                      READ, (NULL),
                      ("Error receiving data from VQE: %s", vqec_err2str(err)));
    goto buf_error;
  }

  outbuf = gst_buffer_new_wrapped_full(0, buflist[0].buf_ptr, buffer_size, 0,
                                       bytes_read, buflist[0].buf_ptr, g_free);
  if (!outbuf) {
    GST_ELEMENT_ERROR(GST_ELEMENT(vqesrc), RESOURCE,
                      FAILED, (NULL),
                      ("gst_buffer_new_wrapped_full failed!"));
    goto buf_error;
  }

  /* Perhaps this is also a good idea: */
#if 0
  /* use buffer metadata so receivers can also track the address */
  if (saddr) {
    gst_buffer_add_net_address_meta (outbuf, saddr);
    g_object_unref (saddr);
  }
  saddr = NULL;
#endif
  GST_OBJECT_UNLOCK (vqesrc);
  *buf = outbuf;
  return GST_FLOW_OK;
buf_error:
    g_free(buflist[0].buf_ptr);
error:
    GST_OBJECT_UNLOCK (vqesrc);
    return GST_FLOW_ERROR;
}

static gboolean
gst_vqesrc_set_sdp (GstVQESrc * src, const gchar * sdp, GError ** error)
{
  /* Won't have any affect until the source is stopped and restarted. */
  /* TODO: A bit of preliminary validation of the SDP contents */
  g_free(src->sdp);
  src->sdp = g_strdup(sdp);
  return TRUE;
}

static gboolean
gst_vqesrc_set_cfg (GstVQESrc * src, const gchar * cfg, GError ** error)
{
  /* Won't have any affect until the source is stopped and restarted. */
  g_free(src->cfg);
  src->cfg = g_strdup(cfg);
  return TRUE;
}

static void
gst_vqesrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVQESrc *vqesrc = GST_VQESRC (object);

  GST_OBJECT_LOCK (vqesrc);

  switch (prop_id) {
    case PROP_SDP:
      gst_vqesrc_set_sdp (vqesrc, g_value_get_string (value), NULL);
      break;
    case PROP_CFG:
      gst_vqesrc_set_cfg (vqesrc, g_value_get_string (value), NULL);
      break;
    default:
      break;
  }
  GST_OBJECT_UNLOCK (vqesrc);
}

static void
gst_vqesrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  vqec_ifclient_stats_channel_t stats;
  vqec_error_t error;

  GstVQESrc *vqesrc = GST_VQESRC (object);
  GST_OBJECT_LOCK (vqesrc);

  memset( &stats, 0, sizeof ( stats ) );

  error = vqec_ifclient_get_stats_channel( vqesrc->stream_uri, &stats );
  
  if ( error != VQEC_OK )
  {
    GST_ELEMENT_ERROR(GST_ELEMENT(vqesrc), STREAM, FAILED, (NULL),
                      ("Failed to get VQE-C tuner stats"));
    goto error;
  } else {
    switch (prop_id) {
      case PROP_SDP:
        g_value_set_string (value, vqesrc->sdp);
        break;
      case PROP_CFG:
        g_value_set_string (value, vqesrc->cfg);
        break;
      case PROP_VQEC_PRIMARY_UDP_INPUTS:
        g_value_set_uint64 ( value, stats.primary_udp_inputs );
        break;
      case PROP_VQEC_PRIMARY_UDP_DROPS:
        g_value_set_uint64 ( value, stats.primary_udp_drops );
        break;
      case PROP_VQEC_PRIMARY_RTP_INPUTS:
        g_value_set_uint64 ( value, stats.primary_rtp_inputs );
        break;
      case PROP_VQEC_PRIMARY_RTP_DROPS:
        g_value_set_uint64 ( value, stats.primary_rtp_drops );
        break;
      case PROP_VQEC_PRIMARY_RTP_DROPS_LATE:
        g_value_set_uint64 ( value, stats.primary_rtp_drops_late );
        break;
      case PROP_VQEC_PRIMARY_RTCP_INPUTS:
        g_value_set_uint64 ( value, stats.primary_rtcp_inputs );
        break;
      case PROP_VQEC_PRIMARY_RTCP_OUTPUTS:
        g_value_set_uint64 ( value, stats.primary_rtcp_outputs );
        break;
      case PROP_VQEC_REPAIR_RTP_INPUTS:
        g_value_set_uint64 ( value, stats.repair_rtp_inputs );
        break;
      case PROP_VQEC_REPAIR_RTP_DROPS:
        g_value_set_uint64 ( value, stats.repair_rtp_drops );
        break;
      case PROP_VQEC_REPAIR_RTP_DROPS_LATE:
        g_value_set_uint64 ( value, stats.repair_rtp_drops_late );
        break;
      case PROP_VQEC_REPAIR_RTCP_INPUTS:
        g_value_set_uint64 ( value, stats.repair_rtcp_inputs );
        break;
      case PROP_VQEC_FEC_INPUTS:
        g_value_set_uint64 ( value, stats.fec_inputs );
        break;
      case PROP_VQEC_FEC_DROPS:
        g_value_set_uint64 ( value, stats.fec_drops );
        break;
      case PROP_VQEC_FEC_DROPS_LATE:
        g_value_set_uint64 ( value, stats.fec_drops_late );
        break;
      case PROP_VQEC_REPAIR_RTP_STUN_INPUTS:
        g_value_set_uint64 ( value, stats.repair_rtp_stun_inputs );
        break;
      case PROP_VQEC_REPAIR_RTP_STUN_OUTPUTS:
        g_value_set_uint64 ( value, stats.repair_rtp_stun_outputs );
        break;
      case PROP_VQEC_REPAIR_RTCP_STUN_INPUTS:
        g_value_set_uint64 ( value, stats.repair_rtcp_stun_inputs );
        break;
      case PROP_VQEC_REPAIR_RTCP_STUN_OUTPUTS:
        g_value_set_uint64 ( value, stats.repair_rtcp_stun_outputs );
        break;
      case PROP_VQEC_POST_REPAIR_OUTPUTS:
        g_value_set_uint64 ( value, stats.post_repair_outputs );
        break;
      case PROP_VQEC_TUNER_QUEUE_DROPS:
        g_value_set_uint64 ( value, stats.tuner_queue_drops );
        break;
      case PROP_VQEC_UNDERRUNS:
        g_value_set_uint64 ( value, stats.underruns );
        break;
      case PROP_VQEC_PRE_REPAIR_LOSSES:
        g_value_set_uint64 ( value, stats.pre_repair_losses );
        break;
      case PROP_VQEC_POST_REPAIR_LOSSES:
        g_value_set_uint64 ( value, stats.post_repair_losses );
        break;
      case PROP_VQEC_POST_REPAIR_LOSSES_RCC:
        g_value_set_uint64 ( value, stats.post_repair_losses_rcc );
        break;
      case PROP_VQEC_REPAIRS_REQUESTED:
        g_value_set_uint64 ( value, stats.repairs_requested );
        break;
      case PROP_VQEC_REPAIRS_POLICED:
        g_value_set_uint64 ( value, stats.repairs_policed );
        break;
      case PROP_VQEC_FEC_RECOVERED_PAKS:
        g_value_set_uint64 ( value, stats.fec_recovered_paks );
        break;

      /*
       * TR135
       */

      case PROP_TR135_OVERRUNS:
        g_value_set_uint64 ( value, stats.tr135_overruns );
        break;
      case PROP_TR135_UNDERRUNS:
        g_value_set_uint64 ( value, stats.tr135_underruns );
        break;
      case PROP_TR135_PACKETS_EXPECTED:
        g_value_set_uint64 ( value, stats.tr135_packets_expected );
        break;
      case PROP_TR135_PACKETS_RECEIVED:
        g_value_set_uint64 ( value, stats.tr135_packets_received );
        break;
      case PROP_TR135_PACKETS_LOST:
        g_value_set_uint64 ( value, stats.tr135_packets_lost );
        break;
      case PROP_TR135_PACKETS_LOST_BEFORE_EC:
        g_value_set_uint64 ( value, stats.tr135_packets_lost_before_ec );
        break;
      case PROP_TR135_LOSS_EVENTS:
        g_value_set_uint64 ( value, stats.tr135_loss_events );
        break;
      case PROP_TR135_LOSS_EVENTS_BEFORE_EC:
        g_value_set_uint64 ( value, stats.tr135_loss_events_before_ec );
        break;
      case PROP_TR135_SEVERE_LOSS_INDEX_COUNT:
        g_value_set_uint64 ( value, stats.tr135_severe_loss_index_count );
        break;
      case PROP_TR135_MINIMUM_LOSS_DISTANCE:
        g_value_set_uint64 ( value, stats.tr135_minimum_loss_distance );
        break;
      case PROP_TR135_MAXIMUM_LOSS_PERIOD:
        g_value_set_uint64 ( value, stats.tr135_maximum_loss_period );
        break;
      case PROP_TR135_BUFFER_SIZE:
        g_value_set_uint64 ( value, stats.tr135_buffer_size );
        break;
      case PROP_TR135_GMIN:
        g_value_set_uint64 ( value, stats.tr135_gmin );
        break;
      case PROP_TR135_SEVERE_LOSS_MIN_DISTANCE:
        g_value_set_uint64 ( value, stats.tr135_severe_loss_min_distance );
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
error:
  GST_OBJECT_UNLOCK (vqesrc);
}

static gboolean
gst_vqesrc_tune (GstVQESrc * src, gchar* sdp)
{
  gboolean success = FALSE;
  vqec_chan_cfg_t cfg;
  /* bind params probably correspond to gstreamer properties?: */
  vqec_bind_params_t *bp = NULL;
  vqec_error_t err = 0;
  uint8_t res;

  bp = vqec_ifclient_bind_params_create();
  if (!bp) {
    GST_ELEMENT_ERROR(GST_ELEMENT(src), STREAM, FAILED, (NULL),
                      ("Failed to create bind params"));
    goto out;
  }
  res = vqec_ifclient_chan_cfg_parse_sdp(&cfg, sdp,
                                         VQEC_CHAN_TYPE_LINEAR);
  if (!res) {
    GST_ELEMENT_ERROR(GST_ELEMENT(src), STREAM, FAILED, (NULL),
                      ("Failed to parse SDP file:\n===BEGIN SDP===\n%s\n===END SDP===", sdp));
    goto out;
  }
  err = vqec_ifclient_tuner_bind_chan_cfg(src->tuner, &cfg, bp);
  if (err) {
    GST_ELEMENT_ERROR(GST_ELEMENT(src), STREAM, FAILED, (NULL),
                      ("Failed to bind channel: %s", vqec_err2str(err)));
    goto out;
  }

  /* format a stream uri to be used for per channel stats queries */
  snprintf( src->stream_uri, sizeof ( src->stream_uri ),  "rtp://%s:%d",  
            inet_ntoa( cfg.primary_dest_addr ), (int)ntohs(cfg.primary_dest_port) );

  success = TRUE;
out:
  if (bp) {
    vqec_ifclient_bind_params_destroy(bp);
  }
  return success;
}

/*
 *  Manage global VQE-C state
 */

static void
vqe_worker(void)
{
  vqec_ifclient_start();
}

static void
setup_worker(void)
{
  g_mutex_lock ( &vqe_owner_mutex );

  if (vqe_owner_refcount == 0 ) 
  {
    vqe_owner_task = gst_task_new ((GstTaskFunction) vqe_worker, NULL, NULL);
    gst_task_set_lock (vqe_owner_task, &vqe_owner_task_mutex );
    gst_task_start (vqe_owner_task);
  }

  vqe_owner_refcount++;

  g_mutex_unlock ( &vqe_owner_mutex );
}

static void
destroy_worker(void)
{
  g_mutex_lock ( &vqe_owner_mutex );
  
  if ( vqe_owner_refcount == 1 ) {
    gst_task_stop(vqe_owner_task);
    vqec_ifclient_stop();
    gst_task_join(vqe_owner_task);
    g_object_unref(vqe_owner_task);
    vqe_owner_task = NULL;
  }    

  vqe_owner_refcount--;

  g_mutex_unlock ( &vqe_owner_mutex );
}

/* create a socket for sending to remote machine */
static gboolean
gst_vqesrc_start (GstBaseSrc * bsrc)
{
  GstVQESrc *src;
  vqec_error_t err = 0;
  char tunerName[64];

  src = GST_VQESRC (bsrc);

  GST_OBJECT_LOCK (src);

  /* Create unique tuner name. 
    Unique at least in this process, which is what we care about. */

  snprintf( tunerName, sizeof(tunerName), "tuner%p", src );
  err = vqec_ifclient_tuner_create(&src->tuner, tunerName );
  if (err) {
    GST_INFO(stderr, "Failed to create tuner: %s\n", vqec_err2str(err));
    goto err;
  }
  gst_vqesrc_tune(src, src->sdp);

  setup_worker();

  GST_OBJECT_UNLOCK (src);

  return TRUE;

task_error:
  vqec_ifclient_tuner_destroy(src->tuner);
err:
  return FALSE;
}

static gboolean
gst_vqesrc_unlock (GstBaseSrc * bsrc)
{
  GstVQESrc *src;

  src = GST_VQESRC (bsrc);

  return TRUE;
}

static gboolean
gst_vqesrc_stop (GstBaseSrc * bsrc)
{
  GstVQESrc *src = GST_VQESRC (bsrc);

  /* attempt to shutdown vqe worker thread
    this is a global refcounted resource  */
  
  GST_OBJECT_LOCK (src);
  destroy_worker();  
  vqec_ifclient_tuner_unbind_chan(src->tuner);
  vqec_ifclient_tuner_destroy(src->tuner);
  GST_OBJECT_UNLOCK (src);

  /* sadly we have to leak global context of vqec
     vqec_ifclient_deinit(); */

  return TRUE;
}

