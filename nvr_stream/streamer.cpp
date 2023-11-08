//
// Created by Keaton Burleson on 11/7/23.
//

#include "../common.h"
#include <gst/gst.h>
#include <string>
#include <gst/gstpad.h>

using string = std::string;
using std::ifstream;
using json = nlohmann::json;

typedef struct StreamData {
    GstElement *pipeline;
    GstElement *rtspSrc;
    GstElement *dePayloader;
    GstElement *decoder;
    GstElement *encoder;
    GstElement *payloader;
    GstElement *sink;
} StreamData;

StreamData appData;
std::shared_ptr<spdlog::logger> logger;

static void padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *pThis) {
    GstPad *sink_pad = gst_element_get_static_pad(pThis->dePayloader, "sink");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = nullptr;
    GstStructure *new_pad_struct;
    const gchar *new_pad_type;

    logger->info("Received new pad '{}' from '{}'", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    /* Check the new pad's name */
    if (!g_str_has_prefix(GST_PAD_NAME(new_pad), "recv_rtp_src_")) {
        logger->error("Incorrect pad.  Need recv_rtp_src_. Ignoring.");
        goto exit;
    }

    /* If our converter is already linked, we have nothing to do here */
    if (gst_pad_is_linked(sink_pad)) {
        logger->error("Sink pad from {} is already linked, ignoring", GST_ELEMENT_NAME(src));
        goto exit;
    }

    /* Check the new pad's type */
    new_pad_caps = gst_pad_query_caps(new_pad, nullptr);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);

    /* Attempt the link */
    ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        logger->error("Type dictated is '{}', but link failed", new_pad_type);
    } else {
        logger->info("Link of type '{}' succeeded", new_pad_type);
    }

    exit:
    if (new_pad_caps != nullptr)
        gst_caps_unref(new_pad_caps);

    gst_object_unref(sink_pad);
}

int startPipeline(const string &rtsp_url, const int rtp_port) {
    GstStateChangeReturn ret;
    GstMessage *msg;
    GstBus *bus;


    gst_init(nullptr, nullptr);

    logger->info("Starting h265->h264 pipeline on port {}", rtp_port);

    appData.pipeline = gst_pipeline_new("pipeline");

    appData.rtspSrc = gst_element_factory_make("rtspsrc", "src");
    g_object_set(G_OBJECT(appData.rtspSrc), "location", rtsp_url.c_str(), nullptr);

    appData.dePayloader = gst_element_factory_make("rtph265depay", "depay");
    appData.decoder = gst_element_factory_make("avdec_h265", "dec");
    g_object_set(G_OBJECT(appData.decoder), "max-threads", 1, nullptr);
    g_object_set(G_OBJECT(appData.decoder), "lowres", 1, nullptr);
    g_object_set(G_OBJECT(appData.decoder), "skip-frame", 1, nullptr);

    appData.encoder = gst_element_factory_make("x264enc", "enc");
    g_object_set(G_OBJECT(appData.encoder), "tune", 0x00000002, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "speed-preset", 1, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "threads", 1, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "ref", 1, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "bitrate", 1024, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "cabac", false, nullptr);
    g_object_set(G_OBJECT(appData.encoder), "rc-lookahead", 0, nullptr);

    appData.payloader = gst_element_factory_make("rtph264pay", "pay");
    g_object_set(G_OBJECT(appData.payloader), "config-interval", 1, nullptr);
    g_object_set(G_OBJECT(appData.payloader), "pt", 96, nullptr);
    g_object_set(G_OBJECT(appData.payloader), "aggregate-mode", 1, nullptr);

    appData.sink = gst_element_factory_make("udpsink", "udp");
    g_object_set(G_OBJECT(appData.sink), "host", "127.0.0.1", nullptr);
    g_object_set(G_OBJECT(appData.sink), "port", rtp_port, nullptr);

    gst_bin_add_many(GST_BIN(appData.pipeline), appData.rtspSrc, appData.dePayloader, appData.decoder, appData.encoder,
                     appData.payloader, appData.sink, nullptr);

    gst_element_link_many(appData.dePayloader, appData.decoder, appData.encoder, appData.payloader, appData.sink, NULL);

    g_signal_connect(appData.rtspSrc, "pad-added", G_CALLBACK(padAddedHandler), &appData);


    ret = gst_element_set_state(appData.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        logger->error("Unable to set pipeline's state to PLAYING");
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
                logger->error("Error received from element {}: {}", GST_OBJECT_NAME(msg->src), err->message);
                logger->error("Debugging information: {}", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                break;
            case GST_MESSAGE_EOS:
                logger->info("End-Of-Stream reached.");
                break;
            default:
                /* We should not reach here because we only asked for ERRORs and EOS */
                logger->info("Unexpected message received.");
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

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 2) {
        spdlog::error("usage: {} camera-config.json\n"
                      "i.e. {} ./cameras/camera-1.json\n"
                      "Stream an RTSP camera to an RTP port.\n", argv[0], argv[0]);
        return 1;
    }

    const char *config_file = argv[1];
    const auto config = nvr::getConfig(config_file);

    nvr::buildLogger(config);

    return startPipeline(config.stream_url, config.rtp_port);
}
