/* GstVQESDPDemux
 * Copyright (C) 2012 YouView TV Ltd. <william.manley@youview.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
 * SECTION:element-vqesdpdemux
 *
 *
 */

#include "gstvqesdpdemux.h"
#include <gst/gst.h>

#include <assert.h>
#include <stdlib.h>

#define GST_VQE_SDP_DEMUX_GET_LOCK(demux) (((GstVQESDPDemux*)(demux))->lock)
#define GST_VQE_SDP_DEMUX_LOCK(demux) (g_mutex_lock(GST_VQE_SDP_DEMUX_GET_LOCK(demux)))
#define GST_VQE_SDP_DEMUX_UNLOCK(demux) (g_mutex_unlock(GST_VQE_SDP_DEMUX_GET_LOCK(demux)))

GST_DEBUG_CATEGORY_STATIC (gst_vqe_sdp_demux_debug);
#define GST_CAT_DEFAULT gst_vqe_sdp_demux_debug

G_DEFINE_TYPE (GstVQESDPDemux, gst_vqe_sdp_demux, GST_TYPE_BIN);

static void gst_vqe_sdp_demux_dispose (GObject * obj);

static GstStateChangeReturn gst_vqe_sdp_demux_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_vqe_sdp_demux_sink_chain(
    GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean gst_vqe_sdp_demux_sink_event(
    GstPad *pad, GstObject *parent, GstEvent *event);
static void gst_vqe_sdp_demux_reset(GstVQESDPDemux * demux);
static void gst_vqe_sdp_demux_create_vqesrc(GstVQESDPDemux * demux, gchar * sdp);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/mpegts"));

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/sdp"));

static void
gst_vqe_sdp_demux_class_init (GstVQESDPDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = gst_vqe_sdp_demux_dispose;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vqe_sdp_demux_change_state);

  gst_element_class_set_metadata (gstelement_class,
      "VQE SDP Demux", "Demuxer/URIList",
      "Interpret SDP files to read data from the sources they point to",
      "William Manley <william.manley@youview.com>");
}

static void
gst_vqe_sdp_demux_init (GstVQESDPDemux * demux)
{
  GstPad * sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_vqe_sdp_demux_sink_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_vqe_sdp_demux_sink_event));
  gst_element_add_pad (GST_ELEMENT (demux), sinkpad);
}

static void
gst_vqe_sdp_demux_dispose (GObject * obj)
{
  GstVQESDPDemux *demux = GST_VQE_SDP_DEMUX (obj);
  gst_vqe_sdp_demux_reset(demux);
  if (demux->vqesrc) {
    g_object_unref(demux->vqesrc);
    demux->vqesrc = NULL;
  }
  G_OBJECT_CLASS (gst_vqe_sdp_demux_parent_class)->dispose (obj);
}

static GstFlowReturn gst_vqe_sdp_demux_sink_chain(
    GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
  GstVQESDPDemux * demux = GST_VQE_SDP_DEMUX(parent);
  if (!demux->sdpfile_complete) {
    if (!demux->sdpfile)
      demux->sdpfile = gst_buffer_new();
    demux->sdpfile = gst_buffer_append(demux->sdpfile, buffer);
    return GST_FLOW_OK;
  }
  else {
    return GST_FLOW_EOS;
  }
}

static gchar *
gst_vqe_buf_to_utf8_sdp (GstBuffer * buf)
{
  gsize size = gst_buffer_get_size(buf);
  gchar *sdp = g_malloc(size + 1);
  sdp[size] = '\0';

  gst_buffer_extract(buf, 0, sdp, size);
  if (!g_utf8_validate (sdp, size, NULL))
    goto validate_error;

  return sdp;
validate_error:
  g_free(sdp);
  return NULL;
}

static gboolean gst_vqe_sdp_demux_sink_event(
    GstPad *pad, GstObject *parent, GstEvent *event)
{
  GstVQESDPDemux * demux = GST_VQE_SDP_DEMUX(parent);
  switch (event->type) {
    case GST_EVENT_EOS: {
      gchar *sdp = NULL;
      demux->sdpfile_complete = TRUE;

      if (demux->sdpfile == NULL) {
        GST_WARNING_OBJECT (demux, "Received EOS without an SDP file.");
        break;
      }

      GST_DEBUG_OBJECT (demux,
          "Got EOS on the sink pad: SDP file fetched");

      sdp = gst_vqe_buf_to_utf8_sdp (demux->sdpfile);
      gst_vqe_sdp_demux_create_vqesrc(demux, sdp);
      g_free(sdp);

      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_SEGMENT:
      /* Swallow newsegments, we'll push our own */
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static void
gst_vqe_sdp_demux_create_vqesrc(GstVQESDPDemux * demux, gchar * sdp)
{
  GstPad * vqesrcpad = NULL;
  GstPad * ghostpad = NULL;
  GstPadTemplate * template = NULL;
  gboolean success;
  assert(!demux->vqesrc);
  assert(sdp);
  demux->vqesrc = gst_element_factory_make ("vqesrc", "src");
  if (!demux->vqesrc) {
    /* TODO: Handle this error */
    abort();
  }
  g_object_set(G_OBJECT(demux->vqesrc), "sdp", sdp, NULL);

  /* We must hold a seperate reference to the fdsink than our GstBin base class
     as we have a member that points to it */
  g_object_ref(demux->vqesrc);
  gst_bin_add (GST_BIN (demux), demux->vqesrc);

  vqesrcpad = gst_element_get_static_pad (GST_ELEMENT(demux->vqesrc), "src");
  template = gst_static_pad_template_get (&srctemplate);
  ghostpad = gst_ghost_pad_new_from_template("src", vqesrcpad, template);
  gst_pad_set_active(ghostpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (demux), ghostpad);
  ghostpad = NULL;
  gst_object_unref(template);
  template = NULL;
  gst_object_unref (vqesrcpad);
  vqesrcpad = NULL;

  success = gst_element_sync_state_with_parent(demux->vqesrc);
  if (!success) {
    GST_ELEMENT_ERROR(GST_ELEMENT(demux), STREAM, FAILED, (NULL), ("Failed to sync state with parent."));
  }
  gst_element_set_state(demux->vqesrc, GST_STATE_PLAYING);
}

static void
gst_vqe_sdp_demux_reset(GstVQESDPDemux * demux)
{
  if (demux->sdpfile) {
    gst_buffer_unref(demux->sdpfile);
    demux->sdpfile = NULL;
  }
  demux->sdpfile_complete = FALSE;
  if (demux->vqesrc) {
    g_object_unref(demux->vqesrc);
    demux->vqesrc = NULL;
  }
  /* TODO: Remove source pad? */
}

static GstStateChangeReturn
gst_vqe_sdp_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstVQESDPDemux *demux = GST_VQE_SDP_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_vqe_sdp_demux_reset(demux);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_vqe_sdp_demux_parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (ret == GST_STATE_CHANGE_SUCCESS) {
        /* We (will) contain a live source, so preroll is not available
           TODO: Create vqesrc before entering the PLAYING state so GstBin
           takes care of this. */
        ret = GST_STATE_CHANGE_NO_PREROLL;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_vqe_sdp_demux_reset(demux);
      break;
    default:
      break;
  }

  return ret;
}

