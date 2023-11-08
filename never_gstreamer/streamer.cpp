//
// Created by Keaton Burleson on 11/7/23.
//

#include <gst/gst.h>
#include <cstring>
#include <gst/gstpad.h>

typedef struct myDataTag {
    GstElement *pipeline;
    GstElement *rtspSrc;
    GstElement *dePayloader;
    GstElement *decoder;
    GstElement *encoder;
    GstElement *payloader;
    GstElement *sink;
} myData_t;

myData_t appData;

static void pad_added_handler(GstElement *src, GstPad *new_pad, myData_t *pThis) {
    GstPad *sink_pad = gst_element_get_static_pad(pThis->dePayloader, "sink");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = nullptr;
    GstStructure *new_pad_struct = nullptr;
    const gchar *new_pad_type = nullptr;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    /* Check the new pad's name */
    if (!g_str_has_prefix(GST_PAD_NAME(new_pad), "recv_rtp_src_")) {
        g_print("  It is not the right pad.  Need recv_rtp_src_. Ignoring.\n");
        goto exit;
    }

    /* If our converter is already linked, we have nothing to do here */
    if (gst_pad_is_linked(sink_pad)) {
        g_print(" Sink pad from %s already linked. Ignoring.\n", GST_ELEMENT_NAME(src));
        goto exit;
    }

    /* Check the new pad's type */
    new_pad_caps = gst_pad_query_caps(new_pad, nullptr);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);

    /* Attempt the link */
    ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        g_print("  Type is '%s' but link failed.\n", new_pad_type);
    } else {
        g_print("  Link succeeded (type '%s').\n", new_pad_type);
    }

    exit:
    if (new_pad_caps != nullptr)
        gst_caps_unref(new_pad_caps);

    gst_object_unref(sink_pad);
}

int main(int argc, char *argv[]) {
    GstStateChangeReturn ret;
    GstMessage *msg;
    GstBus *bus;


    gst_debug_set_active(TRUE);
    GstDebugLevel dbglevel = gst_debug_get_default_threshold();
    dbglevel = GST_LEVEL_ERROR;
    gst_debug_set_default_threshold(dbglevel);


    gst_init(&argc, &argv);

    appData.pipeline = gst_pipeline_new("pipeline");

    appData.rtspSrc = gst_element_factory_make("rtspsrc", "src");
    g_object_set(G_OBJECT(appData.rtspSrc), "location",
                 "rtsp://admin:BlueVip2020@10.2.5.70:554/Streaming/Channels/101/", nullptr);

    appData.dePayloader = gst_element_factory_make("rtph265depay", "depay");
    appData.decoder = gst_element_factory_make("avdec_h265", "dec");

    appData.encoder = gst_element_factory_make("x264enc", "enc");
    g_object_set(G_OBJECT(appData.encoder), "tune", 0x00000002, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "speed-preset", 1, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "threads", 1, nullptr);

    appData.payloader = gst_element_factory_make("rtph264pay", "pay");
    g_object_set(G_OBJECT(appData.payloader), "config-interval", 1, nullptr);
    g_object_set(G_OBJECT(appData.payloader), "pt", 96, nullptr);
    g_object_set(G_OBJECT(appData.payloader), "aggregate-mode", 1, nullptr);

    appData.sink = gst_element_factory_make("udpsink", "udp");
    g_object_set(G_OBJECT(appData.sink), "host", "127.0.0.1", nullptr);
    g_object_set(G_OBJECT(appData.sink), "port", 5123, nullptr);

    gst_bin_add_many(GST_BIN(appData.pipeline), appData.rtspSrc, appData.dePayloader, appData.decoder, appData.encoder,
                     appData.payloader, appData.sink, nullptr);

    gst_element_link_many(appData.dePayloader, appData.decoder, appData.encoder, appData.payloader, appData.sink, NULL);

    g_signal_connect(appData.rtspSrc, "pad-added", G_CALLBACK(pad_added_handler), &appData);


    ret = gst_element_set_state(appData.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(appData.pipeline);
        return -1;
    }

    bus = gst_element_get_bus(appData.pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType) (GST_MESSAGE_ERROR | GST_MESSAGE_EOS));


    /* Parse message */
    if (msg != nullptr) {
        GError *err;
        gchar *debug_info;

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                break;
            case GST_MESSAGE_EOS:
                g_print("End-Of-Stream reached.\n");
                break;
            default:
                /* We should not reach here because we only asked for ERRORs and EOS */
                g_printerr("Unexpected message received.\n");
                break;
        }
        gst_message_unref(msg);
    }

    /* Free resources */
    gst_object_unref(bus);
    gst_element_set_state(appData.pipeline, GST_STATE_NULL);
    gst_object_unref(appData.pipeline);
    return 0;
}
