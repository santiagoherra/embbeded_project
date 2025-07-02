
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

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *target = GST_ELEMENT(data);
    GstPad *sinkpad = gst_element_get_static_pad(target, "sink");
    if (gst_pad_is_linked(sinkpad)) {
        g_print("Pad already linked. Skipping.\n");
        gst_object_unref(sinkpad);
        return;
    }
    if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK)
        g_print("Pad link failed.\n");
    else
        g_print("Pad linked successfully.\n");
    gst_object_unref(sinkpad);
}

int main(int argc, char *argv[]) {
    GMainLoop *loop;
    GstElement *pipeline, *src, *queue1, *conv1, *capsfilter, *mux, *queue2, *conv2;
    GstElement *infer1, *tracker, *infer2, *osd, *conv3, *encoder, *parser, *payloader, *sink;
    GstBus *bus;
    guint bus_watch_id;

    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    pipeline = gst_pipeline_new("deepstream-app");

    src = gst_element_factory_make("uridecodebin", "source");
    queue1 = gst_element_factory_make("queue", "queue1");
    conv1 = gst_element_factory_make("nvvideoconvert", "nvvidconv1");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    mux = gst_element_factory_make("nvstreammux", "mux");
    queue2 = gst_element_factory_make("queue", "queue2");
    conv2 = gst_element_factory_make("nvvideoconvert", "nvvidconv2");
    infer1 = gst_element_factory_make("nvinfer", "primary-infer");
    tracker = gst_element_factory_make("nvtracker", "tracker");
    infer2 = gst_element_factory_make("nvinfer", "secondary-infer");
    osd = gst_element_factory_make("nvdsosd", "osd");
    conv3 = gst_element_factory_make("nvvideoconvert", "nvvidconv3");
    encoder = gst_element_factory_make("nvv4l2h264enc", "encoder");
    parser = gst_element_factory_make("h264parse", "parser");
    payloader = gst_element_factory_make("rtph264pay", "payloader");
    sink = gst_element_factory_make("udpsink", "udpsink");

    if (!pipeline || !src || !queue1 || !conv1 || !capsfilter || !mux || !queue2 || !conv2 ||
        !infer1 || !tracker || !infer2 || !osd || !conv3 || !encoder || !parser || !payloader || !sink) {
        g_printerr("One element could not be created. Exiting.\n");
        return -1;
    }

    g_object_set(G_OBJECT(src), "uri", "file:///opt/nvidia/deepstream/deepstream-6.0/samples/streams/sample_1080p_h264.mp4", NULL);
    g_object_set(G_OBJECT(capsfilter), "caps",
                 gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12"), NULL);
    g_object_set(G_OBJECT(mux), "batch-size", 1, "width", 1920, "height", 1080, "live-source", TRUE, NULL);
    g_object_set(G_OBJECT(infer1), "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt", "unique-id", 1, NULL);
    g_object_set(G_OBJECT(tracker), 
                 "tracker-width", 640, 
                 "tracker-height", 368, 
                 "ll-lib-file", "/opt/nvidia/deepstream/deepstream-6.0/lib/libnvds_nvmultiobjecttracker.so", 
                 "ll-config-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_tracker_IOU.yml", 
                 "enable-batch-process", 1, NULL);
    g_object_set(G_OBJECT(infer2), 
                 "unique-id", 2, 
                 "process-mode", 2, 
                 "infer-on-gie-id", 1, 
                 "infer-on-class-ids", "0:", 
                 "batch-size", 16, 
                 "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_secondary_carmake.txt", NULL);
    g_object_set(G_OBJECT(encoder), "insert-sps-pps", TRUE, "bitrate", 4000000, NULL);
    g_object_set(G_OBJECT(payloader), "config-interval", 1, "pt", 96, NULL);
    g_object_set(G_OBJECT(sink), "host", "192.168.1.127", "port", 8001, "sync", FALSE, "async", FALSE, NULL);

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    gst_bin_add_many(GST_BIN(pipeline), src, queue1, conv1, capsfilter, mux, queue2, conv2,
                     infer1, tracker, infer2, osd, conv3, encoder, parser, payloader, sink, NULL);

    gst_element_link_many(queue1, conv1, capsfilter, mux, NULL);
    gst_element_link_many(mux, queue2, conv2, infer1, tracker, infer2, osd, conv3,
                          encoder, parser, payloader, sink, NULL);

    g_signal_connect(src, "pad-added", G_CALLBACK(on_pad_added), queue1);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Running pipeline...\n");
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}
