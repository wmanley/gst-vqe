# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstvqe.la

# sources used to compile this plug-in
libgstvqe_la_SOURCES = gstvqe.c gstvqesrc.c gstvqesdpdemux.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstvqe_la_CFLAGS = $(GST_CFLAGS) @VQEC_CFLAGS@ -DCONFIG_DIR=\"$(prefix)/etc\"
libgstvqe_la_LIBADD = $(GST_LIBS) @VQEC_LIBS@
libgstvqe_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvqe_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstvqesrc.h gstvqesdpdemux.h
