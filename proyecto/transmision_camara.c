#include <gst/gst.h>
#include <glib.h>

// Manejo de mensajes del bus
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *) data;
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
  GstElement *pipeline;
  GstBus *bus;
  guint bus_watch_id;

  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);

  pipeline = gst_parse_launch(
    // Captura de video desde la cámara CSI y envío a dos ramas (UDP + archivo)
    "nvarguscamerasrc ! video/x-raw(memory:NVMM), format=NV12, width=1920, height=1080 "
    "! tee name=splitter "

    // Rama 1: Transmisión por UDP
    "splitter. ! queue ! nvv4l2h264enc insert-sps-pps=true ! h264parse ! rtph264pay pt=96 "
    "! udpsink host=192.168.1.133 port=8001 sync=false "

    // Rama 2: Grabación local a archivo mp4
    "splitter. ! queue ! nvv4l2h264enc ! h264parse ! mp4mux ! filesink location=guardar.mp4 async=false -e",
    NULL);

  if (!pipeline) {
    g_printerr("Failed to create pipeline\n");
    return -1;
  }

  bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  g_print("Running pipeline...\n");
  g_main_loop_run(loop);

  g_print("Stopping pipeline...\n");
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
  g_source_remove(bus_watch_id);
  g_main_loop_unref(loop);

  return 0;
}
