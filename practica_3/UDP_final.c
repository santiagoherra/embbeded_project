#include <gst/gst.h>
#include <glib.h>

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *)data;

  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      g_print("End of stream\n");
      g_main_loop_quit(loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar *debug;
      GError *error;
      gst_message_parse_error(msg, &error, &debug);
      g_free(debug);
      g_printerr("Error: %s\n", error->message);
      g_error_free(error);
      g_main_loop_quit(loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

int main(int argc, char *argv[]) {
  GMainLoop *loop;

  GstElement *pipeline, *source, *queue1, *mux, *queue2, *convert,
             *nvinfer, *encoder, *parser, *payloader, *udpsink;
  GstBus *bus;
  guint bus_watch_id;

  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);

  if (argc != 2) {
    g_printerr("Usage: %s <destination_ip>\n", argv[0]);
    return -1;
  }

  char *destination_ip = argv[1];

  // Crear elementos
  pipeline   = gst_pipeline_new("deepstream-pipeline");
  source     = gst_element_factory_make("nvarguscamerasrc", "camera-source");
  queue1     = gst_element_factory_make("queue", "camera-queue");
  mux        = gst_element_factory_make("nvstreammux", "stream-muxer");
  queue2     = gst_element_factory_make("queue", "post-mux-queue");
  convert    = gst_element_factory_make("nvvideoconvert", "video-convert");
  nvinfer    = gst_element_factory_make("nvinfer", "primary-infer");
  encoder    = gst_element_factory_make("nvv4l2h264enc", "h264-encoder");
  parser     = gst_element_factory_make("h264parse", "h264-parser");
  payloader  = gst_element_factory_make("rtph264pay", "rtp-payloader");
  udpsink    = gst_element_factory_make("udpsink", "udp-sink");

  if (!pipeline || !source || !queue1 || !mux || !queue2 || !convert || !nvinfer ||
      !encoder || !parser || !payloader || !udpsink) {
    g_printerr("One or more elements could not be created. Exiting.\n");
    return -1;
  }

  // Configurar elementos
  g_object_set(G_OBJECT(source), "bufapi-version", TRUE, NULL);
  g_object_set(G_OBJECT(mux),
               "batch-size", 1,
               "width", 1920,
               "height", 1080,
               NULL);
  g_object_set(G_OBJECT(nvinfer),
               "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt",
               "model-engine-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Primary_Detector/resnet10.caffemodel_b1_gpu0_fp16.engine",
               NULL);
  g_object_set(G_OBJECT(encoder),
               "insert-sps-pps", TRUE,
               "bitrate", 4000000,
               NULL);
  g_object_set(G_OBJECT(payloader),
               "config-interval", 1,
               "pt", 96,
               NULL);
  g_object_set(G_OBJECT(udpsink),
               "host", destination_ip,
               "port", 8001,
               "sync", FALSE,
               "async", FALSE,
               NULL);

  // Agregar al pipeline
  gst_bin_add_many(GST_BIN(pipeline),
                   source, queue1, mux, queue2, convert, nvinfer,
                   encoder, parser, payloader, udpsink, NULL);

  // Enlazar source -> queue -> mux (entrada dinámica)
  GstPad *sinkpad, *srcpad;
  gchar pad_name[16];

  gst_element_link(source, queue1);

  srcpad = gst_element_get_static_pad(queue1, "src");
  sinkpad = gst_element_get_request_pad(mux, "sink_0");
  if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
    g_printerr("Failed to link source to mux\n");
    return -1;
  }
  gst_object_unref(sinkpad);
  gst_object_unref(srcpad);

  // Enlazar resto del pipeline
  gst_element_link_many(mux, queue2, convert, nvinfer, encoder, parser, payloader, udpsink, NULL);

  // Buses
  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  // Ejecutar
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Streaming DeepStream inference to %s:8001\n", destination_ip);
  g_main_loop_run(loop);

  // Limpiar
  g_print("Stopping pipeline...\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);

  return 0;
}
