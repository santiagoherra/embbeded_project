#include <gst/gst.h>
#include <glib.h>

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
on_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    data)
{
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;

  /* We can now link this pad with the decoder sink pad */
  g_print ("Dynamic pad created, linking demuxer/decoder\n");

  sinkpad = gst_element_get_static_pad (decoder, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}

int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;

  GstElement *pipeline, *source, *encoder, *payloader, *udpsink;
  GstBus *bus;
  guint bus_watch_id;

  /* Initialisation */
  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* Check input arguments */
  if (argc != 2) {
    g_printerr ("Usage: %s <destination_ip>\n", argv[0]);
    return -1;
  }

  char *destination_ip = argv[1];  // La IP de destino recibida como argumento

  /* Create gstreamer elements */
  pipeline = gst_pipeline_new ("video-streamer");
  source   = gst_element_factory_make ("nvarguscamerasrc", "camera-source"); // Cámara
  encoder  = gst_element_factory_make ("nvv4l2h264enc", "h264-encoder");      // H264 encoder
  payloader = gst_element_factory_make ("rtph264pay", "rtp-payloader");        // RTP payloader
  udpsink  = gst_element_factory_make ("udpsink", "udp-sink");                  // UDP sink

  if (!pipeline || !source || !encoder || !payloader || !udpsink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* Set up the pipeline */
  g_object_set (G_OBJECT (udpsink), "host", destination_ip, NULL);  // Configuramos la IP de destino
  g_object_set (G_OBJECT (udpsink), "port", 8001, NULL);            // Puerto destino (8001)

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* we add all elements into the pipeline */
  gst_bin_add_many (GST_BIN (pipeline),
                    source, encoder, payloader, udpsink, NULL);

  /* we link the elements together */
  gst_element_link_many (source, encoder, payloader, udpsink, NULL);

  /* Set the pipeline to "playing" state */
  g_print ("Now streaming video to UDP at %s\n", destination_ip);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping streaming\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}

