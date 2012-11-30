gst-vqe
=======

A Gstreamer wrapper around [Cisco VQE Client][1] which implements [RFC 3550][1]
(RTP) and [RFC 4588][2] (RTP Retransmission).  It integrates VQE-Client into
Gstreamer such that it can be autoplugged to handle SDP files.

Two elements are provided:

* vqesrc - A Gstreamer source element which takes the contents of an SDP file
  as a property and streams video from the referenced multicast groups.
* vqesdpdemux - Acts as a "demuxer" which "converts" SDP files to mpeg-ts
  streams.  vqesdpdemux will be autoplugged by Gstreamer to handle SDP files
  which means decodebin will use it when it encounters an SDP file.

[1]:http://www.ietf.org/rfc/rfc3550.txt
[2]:http://www.ietf.org/rfc/rfc4588.txt

Example Use
-----------

Read SDP file from file, connect to the multicast group contained with and
stream mpeg-ts to file:

    gst-launch-1.0 filesrc location=my-channel.sdp \
                 ! application/sdp \
                 ! vqesdpdemux \
                 ! filesink

Demonstrating gstreamer decodebin integration: read SDP file over http and
display it to screen:

    gst-launch-1.0 playbin uri=http://uri.of/my-channel.sdp

Dependencies
------------

This is essentially an integration project between Cisco VQE client and
Gstreamer 1.0 so these are gst-vqe's dependencies:

1. Cisco VQE Client can be found [on github][3].
2. Gstreamer 1.0 can be found at the [Gstreamer website][4]

pkg-config is used to locate these dependecies on your system.

[3]:https://github.com/wmanley/cisco-vqe-client
[4]:http://gstreamer.freedesktop.org/

Building
--------

gst-vqe is a standard autotools package.  To build execute:

    ./autogen.sh
    make
    make install

TODO
----
* Don't assume the stream will be MPEG-TS, adjust caps based upon what is in
  the header file.

License
-------

gst-vqe is dual licensed BSD/LGPL2+ for consistency with VQE and Gstreamer
respectively.  See LICENSE.BSD and LICENSE.LGPL2 for more information.

Contact
-------

gst-vqe can be found on github at https://github.com/wmanley/gst-vqe
