#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define GST_PIPELINE_NONPAUSING_DESC "uridecodebin caps=video/mpegts name=src" \
   " ! queue ! fdsink fd=1"

typedef struct Control_t
{
  GstElement* pipeline;
  GstElement* source;

  GstElement* vqesrc;

  guint child_added_signal_id;
  gboolean table_header_printed;
} Control;

static void proxy_child_added (GstChildProxy *child_proxy,
        GObject *object, gchar *name, gpointer data);


static void
print_table_row (GstElement* vqesrc)
{
  GParamSpec **property_specs;
  guint num_properties, i;

  property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (vqesrc), &num_properties);

  GValue value = { 0, };
  g_value_init ( &value, G_TYPE_UINT64 );
  for (i = 0; i < num_properties; i++) 
  {
    GParamSpec *param = property_specs[i];

    if ( (param->flags & G_PARAM_READABLE) && !(param->flags & G_PARAM_WRITABLE) )  
    {
      g_object_get_property (G_OBJECT (vqesrc), param->name, &value);
      
      if  ( G_VALUE_TYPE (&value) == G_TYPE_UINT64  )
        fprintf(stderr, "%llu\t", g_value_get_uint64 (&value));
      else
        fprintf(stderr, "error\t");
    }
  }
  g_value_unset(&value);
  g_free(property_specs);
}

static void
property_name_to_source_information_key (gchar * str)
{
  size_t n, len;
  len = strlen(str);
  for (n=0; n<len; n++) {
    if (str[n] == '-')
      str[n] = '_';
    if (str[n] <= 'z' && str[n] >= 'a')
      str[n] += 'A' - 'a';
  }
}

static void
print_table_header (GstElement* vqesrc)
{
  GParamSpec **property_specs;
  guint num_properties, i;

  property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (vqesrc), &num_properties);
  for (i = 0; i < num_properties; i++)
  {
    GValue value = { 0, };
    g_value_init ( &value, G_TYPE_UINT64 );
    GParamSpec *param = property_specs[i];

    if ( (param->flags & G_PARAM_READABLE) && !(param->flags & G_PARAM_WRITABLE) )  
    {
      gchar * name = g_strdup(g_param_spec_get_name(param));
      property_name_to_source_information_key(name);
      fprintf(stderr, "%s\t", name);
      g_free(name);
    }
  }
  fprintf(stderr, "\n");
}

static void
print_source_information(Control * control)
{
  if (!control->vqesrc) {
    return;
  }
  if (!control->table_header_printed) {
    fprintf(stderr, "CONFIGURATION\tTYPE\t");
    print_table_header(control->vqesrc);
    control->table_header_printed = TRUE;
  }

  fprintf(stderr, "LINEAR\tRTP\t");
  print_table_row(control->vqesrc);
  fprintf(stderr, "\n");
}

#define g_free_ptr(ptr) \
  do { \
    g_free(*ptr); \
    *ptr = NULL; \
  } while (0);

#define g_object_unref_ptr(ptr) \
  do { \
    GObject * gptr = G_OBJECT(*ptr); \
    if (gptr) \
      g_object_unref(gptr); \
    *ptr = NULL; \
  } while(0);

static gboolean
bus_callback (GstBus * bus, GstMessage * msg, gpointer data);

static Control*
createLinearSourceControl(const char* uri)
{
  GError* gerr = NULL;
  GstBus* bus = NULL;
  Control * control = g_malloc0(sizeof(Control));

  control->pipeline = gst_parse_launch(GST_PIPELINE_NONPAUSING_DESC, &gerr);

  if ( gerr != NULL )
  {
    fprintf(stderr, "ERROR: Could not create GST pipeline because of the "
        "following gstreamer error: %s\n", gerr->message);
    goto error;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (control->pipeline));
  gst_bus_add_watch (bus, bus_callback, control);
  gst_object_unref (bus);

  control->source = gst_bin_get_by_name(GST_BIN(control->pipeline), "src");

  if (!control->source)
  {
    fprintf(stderr, "ERROR: Couldn't get source out of the pipeline.\n");
    goto error;
  }

  g_object_set(G_OBJECT (control->source), "uri", uri, NULL);

  control->child_added_signal_id = g_signal_connect(control->source,
    "child-added", G_CALLBACK(proxy_child_added), control);

  gst_element_set_state (control->pipeline, GST_STATE_PLAYING);

  return control;
error:
  g_object_unref_ptr(&control->pipeline);
  g_object_unref_ptr(&control->source);
  g_free_ptr(&control);
  g_free_ptr(&gerr);
  return NULL;
}

static gboolean
bus_callback (GstBus * bus, GstMessage * msg, gpointer data)
{
  switch (GST_MESSAGE_TYPE (msg)) 
  {
  case GST_MESSAGE_EOS:
    fprintf(stderr, "EOS\n");
    exit(EXIT_SUCCESS);
    break;

  case GST_MESSAGE_ERROR: {
    gchar  *debug;
    GError *error;

    gst_message_parse_error (msg, &error, &debug);
    fprintf(stderr, "vqe-launch ERROR: %s\nAdditional debug:\n%s\n", error->message, debug);
    g_free (debug);
    g_error_free (error);
    exit(EXIT_FAILURE);
    break;
  }
  case GST_MESSAGE_WARNING: {
    gchar  *debug;
    GError *error;

    gst_message_parse_warning (msg, &error, &debug);
    fprintf(stderr, "vqe-launch WARNING: %s\nAdditional debug:\n%s\n", error->message, debug);
    g_free (debug);
    g_error_free (error);
    break;
  }
  default:
    break;
  }
  return TRUE;
}

/*
 * A little explanation here: for getSourceInformation to return anything
 * usefull about VQE-C stats it's required to actually get to the GstVQESrc
 * which in our case is buried inside UriDecodeBin which in turn contains
 * an instance of vqesdpdemuxer which finally holds the GstVQESrc. Getting
 * it our is actually a bit tricky that's why we need this multi-hoop callback.
 * At first we add a signal handler for "child-added" with UriDecodeBin.
 * This will fire whenever new elements are added into the bin. Then, basically,
 * we try to matcha ny element we see with what we want and continue deeper 
 * until we reach our GstVQESrc.
 */
static gboolean
providesIface(GType type, gchar* iface_name)
{
  guint n_ifaces;
  GType *iface, *ifaces = g_type_interfaces (type, &n_ifaces);

  if (ifaces && n_ifaces) 
  {
    iface = ifaces;
    while (*iface) 
    {
      if ( strcmp ( g_type_name (*iface), iface_name  ) == 0  )
      {
        return TRUE;
      }
      iface++;
    }
    g_free (ifaces);
  }
  return FALSE;
}

static void
proxy_child_added (GstChildProxy *child_proxy, GObject *object, gchar *name,
                   gpointer data)
{
  /* TODO: Locking
  boost::lock_guard<boost::recursive_mutex>  lock( ctrlLock ); */
  Control* control = (Control*)data;

  if ( strcmp( G_OBJECT_TYPE_NAME( object ), "GstVQESrc" ) == 0 )
  {
    g_signal_handler_disconnect ( GST_ELEMENT(child_proxy), control->child_added_signal_id);
    control->child_added_signal_id = 0;
    gst_object_ref ( object );
    control->vqesrc = GST_ELEMENT(object);
  }
  else if ( providesIface ( G_OBJECT_TYPE (object) , "GstChildProxy" ) )
  {
    g_signal_handler_disconnect ( GST_ELEMENT(child_proxy), control->child_added_signal_id);
    control->child_added_signal_id = g_signal_connect( object, "child-added",
      G_CALLBACK(proxy_child_added), (gpointer)control);
  }

}

int
main(int argc, char * argv[])
{
  if (argc != 2) {
    fprintf(stderr, "Usage:\n\t%s [uri]\n", argv[0]);
    return EXIT_FAILURE;
  }
  gst_init(&argc, &argv);
  Control * control = createLinearSourceControl(argv[1]);
  if (!control) {
    return EXIT_FAILURE;
  }
  while (TRUE) {
    /* Interruptable with Ctrl-C */
    sleep(1);
    print_source_information(control);
  }
}
