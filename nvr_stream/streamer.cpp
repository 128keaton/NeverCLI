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
        this->appData.rtp_port = this->rtp_port;
        this->appData.stream_name = this->camera_name;
        this->bus = nullptr;
        this->appData.stream_id = config.stream_id;
        this->appData.logger = this->logger;
        this->appData.janus = Janus(this->logger);

        // Use substream for streaming
        this->stream_url = config.sub_stream_url;
        this->quality_config = config.quality_config;
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

        string rtsp_stream_location = buildStreamURL(this->stream_url, this->ip_address, this->port,
                                                     this->rtsp_password, this->rtsp_username, this->type);
        string sanitized_stream_location = sanitizeStreamURL(rtsp_stream_location, this->rtsp_password);

        logger->info("Opening connection to '{}'", sanitized_stream_location);

        // initialize pipeline
        appData.pipeline = gst_pipeline_new("pipeline");
        //g_object_set(GST_BIN(appData.pipeline), "message-forward", true, nullptr);

        // rtsp source
        appData.rtspSrc = gst_element_factory_make("rtspsrc", "src");
      //  g_object_set(G_OBJECT(appData.rtspSrc), "latency", 6000, nullptr); // buffer 6 seconds
        g_object_set(G_OBJECT(appData.rtspSrc), "timeout", 0, nullptr); // disable timeout
        g_object_set(G_OBJECT(appData.rtspSrc), "tcp-timeout", 0, nullptr); // disable tcp timeout
      //  g_object_set(G_OBJECT(appData.rtspSrc), "buffer-mode", 0, nullptr); // use RTP
        //  g_object_set(G_OBJECT(appData.rtspSrc), "protocols", 0x00000004, nullptr); // tcp
        g_object_set(G_OBJECT(appData.rtspSrc), "location", rtsp_stream_location.c_str(), nullptr);
      //  g_object_set(G_OBJECT(appData.rtspSrc), "udp-buffer-size", 2500000, nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "user-id", this->rtsp_username.c_str(), nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "user-pw", this->rtsp_password.c_str(), nullptr);

        // h264 final payloader
        appData.payloader = gst_element_factory_make("rtph264pay", "pay");
       // g_object_set(G_OBJECT(appData.payloader), "config-interval", 1, nullptr);
        g_object_set(G_OBJECT(appData.payloader), "pt", 96, nullptr);

        // h265 parser
        appData.parser = gst_element_factory_make("h265parse", nullptr);
        g_object_set(G_OBJECT(appData.sink), "config-interval", "-1", nullptr); // send with every IDR frame


        // udp output sink
        appData.sink = gst_element_factory_make("udpsink", "udp");
        g_object_set(G_OBJECT(appData.sink), "host", "127.0.0.1", nullptr);
        g_object_set(G_OBJECT(appData.sink), "port", rtp_port, nullptr);
       // g_object_set(G_OBJECT(appData.sink), "buffer-size", 2500000, nullptr);

        // decoding/encoding queue
        appData.buffer = gst_element_factory_make("rtpjitterbuffer", nullptr);
        g_object_set(G_OBJECT(appData.buffer), "rfc7273-use-system-clock", true, nullptr);
      //  g_object_set(G_OBJECT(appData.buffer), "mode", 2, nullptr); // high/low watermark
        //g_object_set(G_OBJECT(appData.queue), "faststart-min-packets", 25, nullptr);
        //g_object_set(G_OBJECT(appData.queue), "max-misorder-time", 1500, nullptr); // 1.5 seconds

        // rtprtxqueue
        appData.queue = gst_element_factory_make("rtprtxqueue", nullptr);
        g_object_set(G_OBJECT(appData.queue), "max-size-packets", 0, nullptr); //unlimited

        if (this->type == h265) {
            logger->info("Starting h265->h264 pipeline on port {}", rtp_port);

            // h265 de-payload
            appData.dePayloader = gst_element_factory_make("rtph265depay", "depay");


            if (!this->has_vaapi) {
                logger->warn("Not using vaapi for encoding/decoding");

                // h265 decode without vaapi
                appData.decoder = gst_element_factory_make("libde265dec", "dec");
                //   g_object_set(G_OBJECT(appData.decoder), "max-threads", 2, nullptr);

                // h264 encode without vaapi
                appData.encoder = gst_element_factory_make("x264enc", "enc");
                g_object_set(G_OBJECT(appData.encoder), "tune", 0x00000002, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "speed-preset", 1, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "threads", 2, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "ref", 1, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "bitrate", 1024, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "cabac", false, nullptr);
                g_object_set(G_OBJECT(appData.encoder), "rc-lookahead", 0, nullptr);
            } else {
                logger->info("Using vaapi for encoding");

                // h265 decode with vaapi
                appData.decoder = gst_element_factory_make("vaapidecodebin", "dec");
                g_object_set(G_OBJECT(appData.decoder), "max-size-buffers", 4, nullptr);

                // h264 encode with vaapi
                appData.encoder = gst_element_factory_make("vaapih264enc", "enc");

                logger->info("Using encoder parameters: {}", quality_config.toJSON().dump(4));
                g_object_set(G_OBJECT(appData.encoder), "rate-control", 2, nullptr); // cbr (constant bitrate)
                //g_object_set(G_OBJECT(appData.encoder), "keyframe-period", 0, nullptr); // auto (duh)
                 g_object_set(G_OBJECT(appData.encoder), "bitrate", 1024, nullptr); // bitrate (duh)

            }


            // add everything
            gst_bin_add_many(
                    GST_BIN(appData.pipeline),
                    appData.rtspSrc,
                    appData.buffer,
                    appData.dePayloader,
                    appData.parser,
                    appData.decoder,
                    appData.encoder,
                    appData.payloader,
                    appData.queue,
                    appData.sink,
                    nullptr
            );

            // link everything except source
            gst_element_link_many(
                    appData.buffer,
                    appData.dePayloader,
                    appData.parser,
                    appData.decoder,
                    appData.encoder,
                    appData.payloader,
                    appData.queue,
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
        GstPad *sink_pad = gst_element_get_static_pad(data->buffer, "sink");
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

        data->logger->info("Caps '{}'", gst_caps_to_string(new_pad_caps));

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
}
