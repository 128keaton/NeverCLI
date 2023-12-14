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
        GMainLoop *main_loop;
        GstStateChangeReturn ret;

        gst_init(nullptr, nullptr);

        GList *plugins, *p;

        plugins = gst_registry_get_plugin_list(gst_registry_get());
        for (p = plugins; p; p = p->next) {
            auto *plugin = static_cast<GstPlugin *>(p->data);
            // Check for vaapi
            if (strcmp(gst_plugin_get_name(plugin), "vaapi") == 0) {
                has_vaapi = gst_registry_check_feature_version(gst_registry_get(), "vaapidecodebin", 1, 22, 6);

                if (has_vaapi) {
                    logger->info("Found vaapi plugin");
                    break;
                }
            }

            // Check for nvcodec
            if (strcmp(gst_plugin_get_name(plugin), "nvcodec") == 0) {
                has_nvidia = gst_registry_check_feature_version(gst_registry_get(), "nvh265dec", 1, 22, 4);

                if (has_nvidia) {
                    logger->info("Found nvcodec plugin");
                    break;
                }
            }
        }

        gst_plugin_list_free(plugins);

        string rtsp_stream_location = buildStreamURL(this->stream_url, this->ip_address, this->port,
                                                     this->rtsp_password, this->rtsp_username, this->type);
        string sanitized_stream_location = sanitizeStreamURL(rtsp_stream_location, this->rtsp_password);

        logger->info("Opening connection to '{}'", sanitized_stream_location);

        int64_t delay = toNanoseconds(5);
        int64_t latency = 6000;
        int64_t bitrate = 375;
        gint config_interval = -1;



        // initialize pipeline
        appData.pipeline = gst_pipeline_new("pipeline");
        g_object_set(GST_BIN(appData.pipeline), "message-forward", true, nullptr);

        // rtsp source
        appData.rtspSrc = gst_element_factory_make("rtspsrc", "src");
        g_object_set(G_OBJECT(appData.rtspSrc), "latency", latency, nullptr); // 200ms latency
        g_object_set(G_OBJECT(appData.rtspSrc), "timeout", 0, nullptr); // disable timeout
        g_object_set(G_OBJECT(appData.rtspSrc), "tcp-timeout", 0, nullptr); // disable tcp timeout
        g_object_set(G_OBJECT(appData.rtspSrc), "location", rtsp_stream_location.c_str(), nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "message-forward", true, nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "ntp-sync", true, nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "udp-buffer-size", 1073741823, nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "udp-reconnect", true, nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "user-id", this->rtsp_username.c_str(), nullptr);
        g_object_set(G_OBJECT(appData.rtspSrc), "user-pw", this->rtsp_password.c_str(), nullptr);


        // vp8 final payloader
        appData.payloader = gst_element_factory_make("rtpvp8pay", "pay");
        g_object_set(G_OBJECT(appData.payloader), "mtu", 600, nullptr);

        // udp output sink
        appData.sink = gst_element_factory_make("udpsink", "udp");
        g_object_set(G_OBJECT(appData.sink), "host", "127.0.0.1", nullptr);
        g_object_set(G_OBJECT(appData.sink), "port", rtp_port, nullptr);


        // queues for buffering
        appData.initialQueue = gst_element_factory_make("queue", "initial_queue");
        g_object_set(G_OBJECT(appData.initialQueue), "max-size-bytes", 0, nullptr);
        g_object_set(G_OBJECT(appData.initialQueue), "max-size-time", toNanoseconds(120) * 2, nullptr);
        g_object_set(G_OBJECT(appData.initialQueue), "max-size-buffers", 0, nullptr);


        appData.finalQueue = gst_element_factory_make("queue", "final_queue");
        g_object_set(G_OBJECT(appData.finalQueue), "min-threshold-time", delay, nullptr);
        g_object_set(G_OBJECT(appData.finalQueue), "max-size-bytes", 0, nullptr);
        g_object_set(G_OBJECT(appData.finalQueue), "max-size-time", toNanoseconds(120) * 2, nullptr);
        g_object_set(G_OBJECT(appData.finalQueue), "max-size-buffers", 0, nullptr);


        if (this->type == h265) {
            logger->info("Starting h265->vp8 pipeline on port {}", rtp_port);

            // h265 parser
            appData.parser = gst_element_factory_make("h265parse", nullptr);
            g_object_set(G_OBJECT(appData.parser), "config-interval", config_interval, nullptr);

            // h265 de-payload
            appData.dePayloader = gst_element_factory_make("rtph265depay", "depay");
            g_object_set(G_OBJECT(appData.dePayloader), "source-info", true, nullptr);

        } else {
            logger->info("Starting h264->vp8 pipeline on port {}", rtp_port);

            // h264 parser
            appData.parser = gst_element_factory_make("h264parse", nullptr);
            g_object_set(G_OBJECT(appData.parser), "config-interval", config_interval, nullptr);

            // h264 de-payload
            appData.dePayloader = gst_element_factory_make("rtph264depay", "depay");
        }

        if (!this->has_vaapi && !this->has_nvidia) {
            logger->warn("Not using vaapi/nvidia for encoding/decoding");

            if (this->type == h265)
                appData.decoder = gst_element_factory_make("libde265dec", "dec");
            else
                appData.decoder = gst_element_factory_make("avdec_h264", "dec");


            appData.encoder = gst_element_factory_make("vp8enc", "enc");
            g_object_set(G_OBJECT(appData.encoder), "tune", 0x00000002, nullptr);
            g_object_set(G_OBJECT(appData.encoder), "threads", 2, nullptr);
            g_object_set(G_OBJECT(appData.encoder), "buffer-size", 2147483647, nullptr);
            g_object_set(G_OBJECT(appData.encoder), "target-bitrate", bitrate, nullptr);
        } else if (this->has_nvidia) {
            logger->info("Using nvidia hardware acceleration");

            if (this->type == h265)
                appData.decoder = gst_element_factory_make("nvh265dec", "dec");
            else
                appData.decoder = gst_element_factory_make("nvh264dec", "dec");

            appData.encoder = gst_element_factory_make("vp8enc", "enc");
            g_object_set(G_OBJECT(appData.encoder), "tune", 0x00000002, nullptr);
            g_object_set(G_OBJECT(appData.encoder), "threads", 2, nullptr);
            g_object_set(G_OBJECT(appData.encoder), "buffer-size", 2147483647, nullptr);
            g_object_set(G_OBJECT(appData.encoder), "target-bitrate", bitrate, nullptr);
        } else {
            logger->info("Using vaapi for encoding");

            if (this->type == h265)
                appData.decoder = gst_element_factory_make("vaapih265dec", "dec");
            else
                appData.decoder = gst_element_factory_make("vaapih264dec", "dec");

            appData.encoder = gst_element_factory_make("vaapivp8enc", "enc");
            g_object_set(G_OBJECT(appData.encoder), "rate-control", 2, nullptr);
            g_object_set(G_OBJECT(appData.encoder), "bitrate", bitrate, nullptr);
        }


        // add everything
        gst_bin_add_many(
                GST_BIN(appData.pipeline),
                appData.rtspSrc,
                appData.initialQueue,
                appData.dePayloader,
                appData.parser,
                appData.decoder,
                appData.encoder,
                appData.payloader,
                appData.finalQueue,
                appData.sink,
                nullptr
        );

        // link everything except source
        gst_element_link_many(
                appData.initialQueue,
                appData.dePayloader,
                appData.parser,
                appData.decoder,
                appData.encoder,
                appData.payloader,
                appData.finalQueue,
                appData.sink,
                NULL);


        g_signal_connect(appData.rtspSrc, "pad-added", G_CALLBACK(nvr::Streamer::padAddedHandler), &appData);

        ret = gst_element_set_state(appData.pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            logger->error("Unable to set pipeline's state to PLAYING");
            gst_object_unref(appData.pipeline);
            return -1;
        } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
            appData.is_live = TRUE;
        }


        bus = gst_element_get_bus(appData.pipeline);

        main_loop = g_main_loop_new(nullptr, FALSE);
        appData.loop = main_loop;
        gst_bus_add_signal_watch(bus);
        g_signal_connect(bus, "message", G_CALLBACK(callbackMessage), &appData);
        g_main_loop_run(main_loop);

        /* Free resources */
        g_main_loop_unref(main_loop);

        /* Free resources */
        quit();
        return 0;
    }

    void Streamer::callbackMessage(GstBus *bus, GstMessage *msg, StreamData *data) {
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError *err;
                gchar *debug;

                gst_message_parse_error(msg, &err, &debug);


                if (strcmp(err->message, "Could not read from resource.") == 0) {
                    data->logger->warn("Could not read from resource, retrying");
                } else {
                    data->logger->error("Error received from element {}: {}", GST_OBJECT_NAME(msg->src), err->message);
                    data->logger->error("Debugging information: {}", debug ? debug : "none");


                    gst_element_set_state(data->pipeline, GST_STATE_READY);
                    g_main_loop_quit(data->loop);
                }

                g_error_free(err);
                g_free(debug);

                break;
            }
            case GST_MESSAGE_EOS:
                data->logger->warn("End-Of-Stream reached.");
                gst_element_set_state(data->pipeline, GST_STATE_READY);
                g_main_loop_quit(data->loop);
                break;
            case GST_MESSAGE_BUFFERING: {
                gint percent = 0;

                /* If the stream is live, we do not care about buffering. */
                if (data->is_live) break;

                gst_message_parse_buffering(msg, &percent);
                data->logger->info("Buffering ({}%)", percent);
                /* Wait until buffering is complete before start/resume playing */
                if (percent < 100)
                    gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
                else
                    gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
                break;
            }
            case GST_MESSAGE_CLOCK_LOST:
                /* Get a new clock */
                gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
                gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
                break;
            case GST_MESSAGE_TAG:
            case GST_MESSAGE_STATE_CHANGED:
            case GST_MESSAGE_STREAM_STATUS:
            case GST_MESSAGE_ELEMENT:
            case GST_MESSAGE_ASYNC_DONE:
            case GST_MESSAGE_NEED_CONTEXT:
            case GST_MESSAGE_HAVE_CONTEXT:
            case GST_MESSAGE_NEW_CLOCK:
                break;
            case GST_MESSAGE_LATENCY:
                if (!gst_bin_recalculate_latency(GST_BIN(data->pipeline)))
                    data->logger->error("Could not reconfigure latency");

                break;
            case GST_MESSAGE_STREAM_START:
                data->logger->info("Stream started");

                break;
            case GST_MESSAGE_PROGRESS:
                GstProgressType type;
                gchar *code, *text;

                gst_message_parse_progress(msg, &type, &code, &text);

                data->logger->info("Progress: ({}) {}", code, text);
                g_free(code);
                g_free(text);
                break;
            default:
                /* Unhandled message */
                break;
        }
    }

    void Streamer::padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *data) {
        GstPad *sink_pad = gst_element_get_static_pad(data->initialQueue, "sink");
        GstPadLinkReturn ret;
        GstCaps *new_pad_caps = nullptr;
        GstStructure *new_pad_struct;
        const gchar *new_pad_type;


        bool janus_connected = data->janus.connect();


        data->logger->info("Received new pad '{}' from '{}'", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

        /* Check the new pad's name */
        if (!g_str_has_prefix(GST_PAD_NAME(new_pad), "recv_rtp_src_")) {
            data->logger->error("Incorrect pad.  Need recv_rtp_src_. Ignoring.");
            //    goto exit;
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
                if (data->janus.createStream(data->stream_name, data->rtp_port)) {
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

    int64_t Streamer::toNanoseconds(int64_t seconds) {
        return seconds * 1000000000;
    }

    int64_t Streamer::toBytes(int64_t megabytes) {
        return megabytes * 1024 * 1024;
    }

    Streamer::Streamer() {
        this->logger = nullptr;
    }
}
