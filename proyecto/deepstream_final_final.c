#include <gst/gst.h>
#include <glib.h>

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
    "uridecodebin uri=file:///opt/nvidia/deepstream/deepstream-6.0/samples/streams/sample_1080p_h264.mp4 name=srcbin "
    "srcbin. ! queue ! nvvideoconvert ! video/x-raw\\(memory:NVMM\\),format=NV12 ! mux.sink_0 "
    "nvstreammux name=mux batch-size=1 width=1920 height=1080 live-source=true "
    "! queue ! nvvideoconvert ! queue ! "
    "nvinfer name=primary-infer config-file-path=/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt unique-id=1 "
    "! queue ! nvtracker tracker-width=640 tracker-height=368 "
    "ll-lib-file=/opt/nvidia/deepstream/deepstream-6.0/lib/libnvds_nvmultiobjecttracker.so "
    "ll-config-file=/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_tracker_IOU.yml enable-batch-process=1 "
    "! queue ! nvinfer name=secondary-infer unique-id=2 process-mode=2 infer-on-gie-id=1 infer-on-class-ids=0: batch-size=16 "
    "config-file-path=/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_secondary_carmake.txt "
    "! queue ! nvdsosd process-mode=HW_MODE ! nvvideoconvert ! tee name=t "
    "t. ! queue ! nvv4l2h264enc insert-sps-pps=true bitrate=4000000 ! h264parse ! rtph264pay config-interval=1 pt=96 "
    "! udpsink host=192.168.1.133 port=8001 sync=false async=false "
    "t. ! queue ! nvv4l2h264enc insert-sps-pps=true ! h264parse ! mp4mux ! filesink location=guardar.mp4 async=false -e",
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
