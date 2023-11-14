//
// Created by Keaton Burleson on 11/7/23.
//

#include "../common.h"
#include "streamer.h"
#include "janus.h"
#include <gst/gst.h>
#include <string>
#include <gst/gstpad.h>

using string = std::string;
using std::ifstream;
using json = nlohmann::json;


namespace nvr {
    Streamer::Streamer(const CameraConfig &config) {
        this->has_vaapi = false;
        this->type = config.type;
        this->logger = nvr::buildLogger(config);
        this->camera_name = config.stream_name;
        this->ip_address = config.ip_address;
        this->rtsp_password = config.rtsp_password;
        this->rtsp_username = config.rtsp_username;
        this->rtp_port = config.rtp_port;
        this->port = config.port;
        this->stream_url = config.stream_url;
        this->appData.rtp_port = this->rtp_port;
        this->appData.stream_name = this->camera_name;
        this->bus = nullptr;
        this->appData.stream_id = config.stream_id;
        this->appData.logger = this->logger;
        this->appData.janus = Janus(this->logger);
    }

    bool Streamer::valid() {
        return this->logger != nullptr;
    }

    void Streamer::quit() {
        if (appData.janus.isConnected() && appData.janus.isStreaming()) {
            appData.janus.disconnect();
        }

        if (!quitting) {
            quitting = true;
            logger->info("Exiting...");

            gst_object_unref(bus);

            GstStateChangeReturn return_state = gst_element_set_state(appData.pipeline, GST_STATE_NULL);

            if (return_state == 0) {
                logger->error("Could not set pipeline state");
            } else {
                logger->info("Pipeline state set to null");
                gst_object_unref(appData.pipeline);
            }
        }
    }

    int Streamer::start() {
        int return_state = 0;
        GstStateChangeReturn ret;
        GstMessage *msg;


        gst_init(nullptr, nullptr);

        GList *plugins, *p;

        plugins = gst_registry_get_plugin_list(gst_registry_get());
        for (p = plugins; p; p = p->next) {
            auto *plugin = static_cast<GstPlugin *>(p->data);
            if (strcmp(gst_plugin_get_name(plugin), "vaapi") == 0) {
                has_vaapi = true;
                logger->info("Found vaapi plugin");
                break;
            }
        }

        gst_plugin_list_free(plugins);

        logger->info("Opening connection to '{}'", buildStreamURL("HIDDEN"));

        // initialize pipeline
        appData.pipeline = gst_pipeline_new("pipeline");

        // rtsp source
        appData.rtspSrc = gst_element_factory_make("rtspsrc", "src");
        g_object_set(G_OBJECT(appData.rtspSrc), "location", buildStreamURL(this->rtsp_password).c_str(), nullptr);

        if (this->port == 80) {
            g_object_set(G_OBJECT(appData.rtspSrc), "user-id",this->rtsp_username.c_str(), nullptr);
            g_object_set(G_OBJECT(appData.rtspSrc), "user-pw",this->rtsp_password.c_str(), nullptr);
            g_object_set(G_OBJECT(appData.rtspSrc), "location", buildStreamURL("").c_str(), nullptr);
        } else {
            g_object_set(G_OBJECT(appData.rtspSrc), "location", buildStreamURL(this->rtsp_password).c_str(), nullptr);
        }

        // h264 final payloader
        appData.payloader = gst_element_factory_make("rtph264pay", "pay");
        g_object_set(G_OBJECT(appData.payloader), "config-interval", 1, nullptr);
        g_object_set(G_OBJECT(appData.payloader), "pt", 96, nullptr);
        g_object_set(G_OBJECT(appData.payloader), "aggregate-mode", 1, nullptr);

        // udp output sink
        appData.sink = gst_element_factory_make("udpsink", "udp");
        g_object_set(G_OBJECT(appData.sink), "host", "127.0.0.1", nullptr);
        g_object_set(G_OBJECT(appData.sink), "port", rtp_port, nullptr);

        if (this->type == h265) {
            logger->info("Starting h265->h264 pipeline on port {}", rtp_port);

            // h265 de-payload
            appData.dePayloader = gst_element_factory_make("rtph265depay", "depay");

            // decoding/encoding queue
            appData.queue = gst_element_factory_make("queue2", nullptr);


            if (!this->has_vaapi) {
                logger->warn("Not using vaapi for encoding/decoding");

                // h265 decode without vaapi
                appData.decoder = gst_element_factory_make("avdec_h265", "dec");
                g_object_set(G_OBJECT(appData.decoder), "max-threads", 1, nullptr);
                g_object_set(G_OBJECT(appData.decoder), "lowres", 1, nullptr);


                // h264 encode without vaapi
                appData.encoder = gst_element_factory_make("x264enc", "enc");
                g_object_set(G_OBJECT(appData.encoder), "tune", 0x00000002, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "speed-preset", 1, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "threads", 1, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "ref", 1, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "bitrate", 1024, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "cabac", false, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "rc-lookahead", 0, nullptr);
            } else {
                logger->info("Using vaapi for encoding");

                // h265 decode with vaapi
                appData.decoder = gst_element_factory_make("vaapih265dec", "dec");

                // h264 encode with vaapi
                appData.encoder = gst_element_factory_make("vaapih264enc", "enc");

                g_object_set(G_OBJECT(appData.encoder), "tune", 1, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "rate-control", 1, nullptr);

                g_object_set(G_OBJECT(appData.encoder), "max-qp", 50, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "init-qp", 41, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "min-qp", 38, nullptr);

                g_object_set(G_OBJECT(appData.encoder), "keyframe-period", 1, nullptr);
            }


            // add everything
            gst_bin_add_many(
                    GST_BIN(appData.pipeline),
                    appData.rtspSrc,
                    appData.dePayloader,
                    appData.decoder,
                    appData.queue,
                    appData.encoder,
                    appData.payloader,
                    appData.sink,
                    nullptr
            );

            // link everything except source
            gst_element_link_many(
                    appData.dePayloader,
                    appData.decoder,
                    appData.queue,
                    appData.encoder,
                    appData.payloader,
                    appData.sink,
                    NULL);

        } else {
            logger->info("Starting h264->h264 pipeline on port {}", rtp_port);

            // h264 de-payload
            appData.dePayloader = gst_element_factory_make("rtph264depay", "depay");

            // add everything
            gst_bin_add_many(GST_BIN(appData.pipeline), appData.rtspSrc, appData.dePayloader, appData.payloader,
                             appData.sink, nullptr);

            // link everything except source
            gst_element_link_many(appData.dePayloader, appData.payloader, appData.sink, NULL);
        }

        g_signal_connect(appData.rtspSrc, "pad-added", G_CALLBACK(nvr::Streamer::padAddedHandler), &appData);

        ret = gst_element_set_state(appData.pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            logger->error("Unable to set pipeline's state to PLAYING");
            gst_object_unref(appData.pipeline);
            return -1;
        }


        bus = gst_element_get_bus(appData.pipeline);

        while (true) {
            msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                             (GstMessageType) (GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

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
                        return_state = EXIT_FAILURE;
                        break;
                    case GST_MESSAGE_EOS:
                        logger->info("End-Of-Stream reached.");
                        return_state = EXIT_SUCCESS;
                        break;
                    default:
                        logger->info("Unexpected message received: {}", msg->src->name);
                        return_state = EXIT_SUCCESS;
                        break;
                }
                gst_message_unref(msg);
            } else {
                break;
            }

            if (return_state == EXIT_FAILURE)
                break;
        }

        /* Free resources */
        quit();
        return return_state;
    }

    void Streamer::padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *data) {
        GstPad *sink_pad = gst_element_get_static_pad(data->dePayloader, "sink");
        GstPadLinkReturn ret;
        GstCaps *new_pad_caps = nullptr;
        GstStructure *new_pad_struct;
        const gchar *new_pad_type;


        bool janus_connected = data->janus.connect();


        data->logger->info("Received new pad '{}' from '{}'", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

        /* Check the new pad's name */
        if (!g_str_has_prefix(GST_PAD_NAME(new_pad), "recv_rtp_src_")) {
            data->logger->error("Incorrect pad.  Need recv_rtp_src_. Ignoring.");
            goto exit;
        }

        /* If our converter is already linked, we have nothing to do here */
        if (gst_pad_is_linked(sink_pad)) {
            data->logger->error("Sink pad from {} is already linked, ignoring", GST_ELEMENT_NAME(src));
            goto exit;
        }

        /* Check the new pad's type */
        new_pad_caps = gst_pad_query_caps(new_pad, nullptr);
        new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
        new_pad_type = gst_structure_get_name(new_pad_struct);

        /* Attempt the link */
        ret = gst_pad_link(new_pad, sink_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            data->logger->error("Type dictated is '{}', but link failed", new_pad_type);
        } else {
            data->logger->info("Link of type '{}' succeeded", new_pad_type);

            if (janus_connected)
                if (data->janus.createStream(data->stream_name, data->stream_id, data->rtp_port)) {
                    data->logger->info("Stream created and live on Janus");
                    data->janus.keepAlive();
                } else
                    data->logger->warn("Not streaming because we were not able to create a stream endpoint on Janus");
            else
                data->logger->warn("Not streaming because we were not able to connect to Janus");

        }

        exit:
        if (new_pad_caps != nullptr)
            gst_caps_unref(new_pad_caps);

        gst_object_unref(sink_pad);
    }

    Streamer::Streamer() {
        this->logger = nullptr;
    }

    string Streamer::buildStreamURL(const string &password) {
        if (password.empty()) {
            return string("rtsp://")
                    .append(this->ip_address)
                    .append(":")
                    .append(std::to_string(this->port))
                    .append(this->stream_url);
        }

        return string("rtsp://")
                .append(this->rtsp_username)
                .append(":")
                .append(password)
                .append("@")
                .append(this->ip_address)
                .append(":")
                .append(std::to_string(this->port))
                .append(this->stream_url);
    }
}







