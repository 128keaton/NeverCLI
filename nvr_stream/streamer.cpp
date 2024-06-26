//
// Created by Keaton Burleson on 11/7/23.
//

#include "../common.h"
#include "streamer.h"
#include "janus.h"
#include <gst/gst.h>
#include <string>
#include <gst/gstpad.h>
#include <netinet/in.h>
#include <stdexcept>

using string = std::string;
using std::ifstream;
using json = nlohmann::json;


namespace nvr {
    Streamer::Streamer(const CameraConfig &config) {
        this->type = config.type;
        this->logger = nvr::buildLogger(config);
        this->camera_id = config.stream_id;
        this->ip_address = config.ip_address;
        this->rtsp_password = config.rtsp_password;
        this->rtsp_username = config.rtsp_username;
        this->rtp_port = nvr::Streamer::findOpenPort();
        this->port = config.port;

        this->appData.rtp_port = this->rtp_port;
        this->appData.bitrate = 900; // The recorded video is ~about~ this
        this->appData.hardware_enc_priority = config.hardware_enc_priority;
        this->appData.stream_name = this->camera_id;
        this->bus = nullptr;
        this->appData.stream_id = config.stream_id;
        this->appData.logger = this->logger;
        this->appData.janus = Janus(this->logger);
        this->appData.error_count = 0;
        this->appData.needs_codec_switch = false;

        // Use sub stream for streaming
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

        appData.stream_url = buildStreamURL(this->stream_url, this->ip_address, this->port,
                                            this->rtsp_password, this->rtsp_username);

        string sanitized_stream_location = sanitizeStreamURL(appData.stream_url, this->rtsp_password);

        logger->info("Stream will be pulled from '{}'", sanitized_stream_location);

        // initialize pipeline
        appData.pipeline = gst_pipeline_new("pipeline");
        g_object_set(GST_BIN(appData.pipeline), "message-forward", true, nullptr);

        // rtsp stream
        setupRTSPStream(&appData);

        if (hasU30()) {
            // h264 final payloader
            appData.payloader = gst_element_factory_make("rtph264pay", "pay");
            logger->info("Using rtph264pay");
            g_object_set(G_OBJECT(appData.payloader), "pt", 96, nullptr);
            g_object_set(G_OBJECT(appData.payloader), "config-interval", -1, nullptr);
        } else {
            // vp8 final payloader
            appData.payloader = gst_element_factory_make("rtpvp8pay", "pay");
            logger->info("Using rtpvp8pay");
        }

        // udp output sink
        appData.sink = gst_element_factory_make("udpsink", "udp");
        g_object_set(G_OBJECT(appData.sink), "host", "0.0.0.0", nullptr);
        g_object_set(G_OBJECT(appData.sink), "port", rtp_port, nullptr);
        g_object_set(G_OBJECT(appData.sink), "sync", false, nullptr);


        appData.is_h265 = type == h265;

        Streamer::setupStreamInput(&appData);
        Streamer::setupStreamOutput(&appData, true);

        // add everything
        if (hasTimestamper()) {
            gst_bin_add_many(
                    GST_BIN(appData.pipeline),
                    appData.rtspSrc,
                    appData.dePayloader,
                    appData.parser,
                    appData.timestamper,
                    appData.decoder,
                    appData.encoder,
                    appData.payloader,
                    appData.sink,
                    nullptr
            );

            // link everything except source
            gst_element_link_many(
                    appData.dePayloader,
                    appData.parser,
                    appData.timestamper,
                    appData.decoder,
                    appData.encoder,
                    appData.payloader,
                    appData.sink,
                    nullptr
            );
        } else {
            gst_bin_add_many(
                    GST_BIN(appData.pipeline),
                    appData.rtspSrc,
                    appData.dePayloader,
                    appData.parser,
                    appData.decoder,
                    appData.encoder,
                    appData.payloader,
                    appData.sink,
                    nullptr
            );

            // link everything except source
            gst_element_link_many(
                    appData.dePayloader,
                    appData.parser,
                    appData.decoder,
                    appData.encoder,
                    appData.payloader,
                    appData.sink,
                    nullptr
            );
        }


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

    void Streamer::callbackMessage([[maybe_unused]] GstBus *bus, GstMessage *msg, StreamData *data) {
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError *err;
                gchar *debug;

                gst_message_parse_error(msg, &err, &debug);


                bool switch_codecs = true;
                if (strcmp(err->message, "Could not read from resource.") == 0) {
                    data->logger->warn("Could not read from resource, retrying");
                } else {
                    data->logger->error("Error received from element {}: {}", GST_OBJECT_NAME(msg->src), err->message);
                    data->logger->error("Debugging information: {}", debug ? debug : "none");

                    switch_codecs = data->needs_codec_switch;

                    if (!switch_codecs) {
                        g_main_loop_quit(data->loop);
                    } else {
                        gst_element_set_state(data->pipeline, GST_STATE_READY);
                    }
                }

                g_error_free(err);
                g_free(debug);

                if (switch_codecs) {
                    data->is_h265 = !data->is_h265;
                    switchCodecs(data);
                }

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
                data->logger->info("Stream has successfully started");

                break;
            case GST_MESSAGE_PROGRESS:
                GstProgressType type;
                gchar *code, *text;

                gst_message_parse_progress(msg, &type, &code, &text);

                data->logger->debug("Progress: ({}) {}", code, text);
                g_free(code);
                g_free(text);
                break;
            default:
                /* Unhandled message */
                break;
        }
    }

    int Streamer::findOpenPort() {
        int current_port = 0;

        while (current_port < 99999) {
            struct sockaddr_in address{};
            int opt = 1;
            int server_fd;
            if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                this->logger->debug("Could not create socket for port {}, unable to initialize", current_port);
                close(server_fd);
                continue;
            }

            if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
                this->logger->debug("Could not create socket for port {}, unable to set socket option", current_port);
                close(server_fd);
                continue;
            }

            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(current_port);

            if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
                this->logger->debug("Could not create socket for port {}, unable to bind", current_port);
                close(server_fd);
                continue;
            }

            if (listen(server_fd, 3) < 0) {
                this->logger->debug("Could not create socket for port {}, unable to listen", current_port);
                close(server_fd);
                continue;
            }

            socklen_t len = sizeof(address);
            getsockname(server_fd, (struct sockaddr *) &address, &len);
            current_port = ntohs(address.sin_port);
            close(server_fd);
            this->logger->debug("Using port {}", current_port);
            break;
        }

        return current_port;
    }

    /**
     * Create the stream on Janus
     * @param data
     */
    void Streamer::createJanusStream(StreamData *data) { // NOLINT(*-no-recursion)
        if (data->error_count > 10) {
            data->logger->warn("Too many Janus errors, not trying again");
            exit(-1);
        }

        string codec = "vp8";

        if (hasU30() && (std::equal(data->hardware_enc_priority.begin(), data->hardware_enc_priority.end(), "u30") ||
                         std::equal(data->hardware_enc_priority.begin(), data->hardware_enc_priority.end(), "none"))) {
            data->logger->info("Using h264 stream since we are using U30");
            codec = "h264";
        }

        if (data->janus.createStream(data->stream_id, data->rtp_port, codec)) {
            data->janus.keepAlive();
            data->error_count = 0;
            return;
        } else {
            data->logger->warn("Stream created, but unable to notify Janus, trying to recreate stream");
            data->error_count += 1;
            data->logger->info("Retrying stream creation");
            data->rtp_port += 1;
            return createJanusStream(data);
        }
    }

    void Streamer::padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *data) {
        GstPad *sink_pad = gst_element_get_static_pad(data->dePayloader, "sink");
        GstPadLinkReturn ret;
        GstCaps *new_pad_caps = nullptr;
        GstStructure *new_pad_struct;
        const gchar *new_pad_type;
        string caps_str;


        bool janus_connected = data->janus.connect();


        data->logger->debug("Received new pad '{}' from '{}'", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

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

        caps_str = string(gst_caps_to_string(new_pad_caps));

        if (data->is_h265 && caps_str.find("H264") != string::npos) {
            data->logger->error("Whoops, you're trying to decode an H264 stream with an H265 decoder");
            data->needs_codec_switch = true;
            goto exit;
        } else if (!data->is_h265 && caps_str.find("H265") != string::npos) {
            data->logger->error("Whoops, you're trying to decode an H265 stream with an H264 decoder");
            data->needs_codec_switch = true;
            goto exit;
        } else
            data->logger->debug("Caps should be setup correctly, continuing");


        /* Attempt3 the link */
        ret = gst_pad_link(new_pad, sink_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            data->logger->error("Type dictated is '{}', but link failed", new_pad_type);
        } else {
            data->logger->debug("Link of type '{}' succeeded", new_pad_type);

            gint port_value;
            g_object_get(G_OBJECT(data->sink), "port", &port_value, nullptr);
            data->logger->debug("Streaming output RTP port: {}", port_value);

            if (janus_connected)
                createJanusStream(data);
            else {
                data->logger->warn("Stream created, but unable to connect to Janus");
                exit(-1);
            }
        }

        exit:
        if (new_pad_caps != nullptr)
            gst_caps_unref(new_pad_caps);

        gst_object_unref(sink_pad);
    }

    Streamer::Streamer() {
        this->logger = nullptr;
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCallsOfFunction"

    void Streamer::switchCodecs(StreamData *appData) {
        auto logger = appData->logger;

        Streamer::teardownStreamCodecs(appData);
        Streamer::setupStreamInput(appData);
        Streamer::setupRTSPStream(appData);
        Streamer::setupStreamOutput(appData, false);

        logger->info("Adding elements");
        if (hasTimestamper()) {
            gst_bin_add_many(
                    GST_BIN(appData->pipeline),
                    appData->rtspSrc,
                    appData->dePayloader,
                    appData->parser,
                    appData->timestamper,
                    appData->decoder,
                    nullptr
            );

            gst_element_link_many(
                    appData->rtspSrc,
                    appData->dePayloader,
                    appData->parser,
                    appData->timestamper,
                    appData->decoder,
                    appData->encoder,
                    nullptr);
        } else {
            gst_bin_add_many(
                    GST_BIN(appData->pipeline),
                    appData->rtspSrc,
                    appData->dePayloader,
                    appData->parser,
                    appData->decoder,
                    nullptr
            );

            gst_element_link_many(
                    appData->rtspSrc,
                    appData->dePayloader,
                    appData->parser,
                    appData->decoder,
                    appData->encoder,
                    nullptr);
        }


        g_signal_connect(appData->rtspSrc, "pad-added", G_CALLBACK(nvr::Streamer::padAddedHandler), appData);

        auto ret = gst_element_set_state(appData->pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            logger->error("Unable to set pipeline's state to PLAYING");
            gst_object_unref(appData->pipeline);
        } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
            appData->is_live = TRUE;
        }
    }

    void Streamer::teardownStreamCodecs(StreamData *appData) {
        gst_element_set_state(appData->rtspSrc, GST_STATE_NULL);
        gst_element_set_state(appData->dePayloader, GST_STATE_NULL);
        gst_element_set_state(appData->parser, GST_STATE_NULL);
        gst_element_set_state(appData->decoder, GST_STATE_NULL);

        if (hasTimestamper())
            gst_element_set_state(appData->timestamper, GST_STATE_NULL);

        gst_bin_remove(GST_BIN(appData->pipeline), appData->rtspSrc);
        gst_bin_remove(GST_BIN(appData->pipeline), appData->dePayloader);
        gst_bin_remove(GST_BIN(appData->pipeline), appData->parser);
        gst_bin_remove(GST_BIN(appData->pipeline), appData->decoder);

        if (hasTimestamper())
            gst_bin_remove(GST_BIN(appData->pipeline), appData->timestamper);

    }

#pragma clang diagnostic pop

    void Streamer::setupStreamInput(StreamData *appData) {
        auto logger = appData->logger;
        bool has_u30 = hasU30();

        if (appData->is_h265) {
            if (!has_u30)
                logger->info("Building h265->vp8 pipeline on port {}", appData->rtp_port);
            else
                logger->info("Building h265->h264 pipeline on port {}", appData->rtp_port);

            // h265 parser
            appData->parser = gst_element_factory_make("h265parse", nullptr);
            g_object_set(G_OBJECT(appData->parser), "config-interval", -1, nullptr);

            // h265 timestamper
            if (hasTimestamper())
                appData->timestamper = gst_element_factory_make("h265timestamper", nullptr);

            // h265 de-payload
            appData->dePayloader = gst_element_factory_make("rtph265depay", "depay");
            g_object_set(G_OBJECT(appData->dePayloader), "source-info", true, nullptr);

        } else {
            if (!has_u30)
                logger->info("Building h264->vp8 pipeline on port {}", appData->rtp_port);
            else
                logger->info("Building h264->h264 pipeline on port {}", appData->rtp_port);

            // h264 parser
            appData->parser = gst_element_factory_make("h264parse", nullptr);
            g_object_set(G_OBJECT(appData->parser), "config-interval", -1, nullptr);

            // h264 timestamper
            if (hasTimestamper())
                appData->timestamper = gst_element_factory_make("h264timestamper", nullptr);

            // h264 de-payload
            appData->dePayloader = gst_element_factory_make("rtph264depay", "depay");
            g_object_set(G_OBJECT(appData->dePayloader), "source-info", true, nullptr);
        }

        logger->info("Done creating input");
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ConstantConditionsOC"
#pragma ide diagnostic ignored "ConstantParameter"

    void Streamer::buildStreamOutput(StreamData *appData, StreamHardwareType type, bool create_encoder) {
        switch (type) {
            case vaapi:
                if (appData->is_h265) {
                    appData->decoder = gst_element_factory_make("vaapih265dec", "dec");
                    g_object_set(G_OBJECT(appData->decoder), "discard-corrupted-frames", true, nullptr);
                } else {
                    appData->decoder = gst_element_factory_make("vaapih264dec", "dec");
                    g_object_set(G_OBJECT(appData->decoder), "low-latency", true, nullptr);
                }

                if (create_encoder) {
                    appData->encoder = gst_element_factory_make("vaapivp8enc", "enc");
                    g_object_set(G_OBJECT(appData->encoder), "rate-control", 2, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "bitrate", appData->bitrate, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "quality-level", 3, nullptr);
                }
                break;
            case nvidia:
                if (appData->is_h265)
                    appData->decoder = gst_element_factory_make("nvh265dec", "dec");
                else
                    appData->decoder = gst_element_factory_make("nvh264dec", "dec");

                if (create_encoder) {
                    appData->encoder = gst_element_factory_make("vp8enc", "enc");
                    g_object_set(G_OBJECT(appData->encoder), "threads", 2, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "target-bitrate", appData->bitrate, nullptr);
                }
                break;
            case u30:
                appData->decoder = gst_element_factory_make("vvas_xvcudec", "dec");
                g_object_set(G_OBJECT(appData->decoder), "dev-idx", 0, nullptr);
                g_object_set(G_OBJECT(appData->decoder), "low-latency", true, nullptr);
                g_object_set(G_OBJECT(appData->decoder), "splitbuff-mode", true, nullptr);

                if (create_encoder) {
                    appData->encoder = gst_element_factory_make("vvas_xvcuenc", "enc");
                    g_object_set(G_OBJECT(appData->encoder), "dev-idx", 0, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "b-frames", 0, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "target-bitrate", appData->bitrate - 75, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "max-bitrate", appData->bitrate, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "gop-mode", 2, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "control-rate", 2, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "gop-length", 120, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "initial-delay", 0, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "periodicity-idr", 120, nullptr);
                }
                break;
            case none:
                if (appData->is_h265)
                    appData->decoder = gst_element_factory_make("avdec_h265", "dec");
                else
                    appData->decoder = gst_element_factory_make("avdec_h264", "dec");


                if (create_encoder) {
                    appData->encoder = gst_element_factory_make("vp8enc", "enc");
                    g_object_set(G_OBJECT(appData->encoder), "threads", 2, nullptr);
                    g_object_set(G_OBJECT(appData->encoder), "target-bitrate", appData->bitrate, nullptr);
                }
                break;
        }
    }

    void Streamer::setupStreamOutput(StreamData *appData, bool create_encoder) {
        auto logger = appData->logger;
        bool has_vaapi = hasVAAPI();
        bool has_nvidia = hasNVIDIA();
        bool has_u30 = hasU30();
        string priority = appData->hardware_enc_priority;

        logger->info("Hardware encoder priority: {}", priority);

        if (!has_u30 && !has_vaapi && !has_nvidia || std::equal(priority.begin(), priority.end(), "software")) {
            logger->info("Using software for encoding/decoding");
            buildStreamOutput(appData, none, create_encoder);
        } else {
            if (has_nvidia && (std::equal(priority.begin(), priority.end(), "nvidia") ||
                               std::equal(priority.begin(), priority.end(), "none"))) {
                logger->info("Using NVidia GPU for encoding/decoding");
                buildStreamOutput(appData, nvidia, create_encoder);
            } else if (has_u30 && (std::equal(priority.begin(), priority.end(), "u30") ||
                                   std::equal(priority.begin(), priority.end(), "none"))) {
                logger->info("Using U30 Media Accelerator for encoding/decoding");
                buildStreamOutput(appData, u30, create_encoder);
            } else if (has_vaapi && (std::equal(priority.begin(), priority.end(), "vaapi") ||
                                     std::equal(priority.begin(), priority.end(), "none"))) {
                logger->info("Using VAAPI for encoding/decoding");
                buildStreamOutput(appData, vaapi, create_encoder);
            } else {
                logger->warn("All cases fell through, we are using software encoder");
                buildStreamOutput(appData, none, create_encoder);
            }
        }
    }

#pragma clang diagnostic pop

    void Streamer::setupRTSPStream(StreamData *appData) {
        appData->rtspSrc = gst_element_factory_make("rtspsrc", "src");
        g_object_set(G_OBJECT(appData->rtspSrc), "location", appData->stream_url.c_str(), nullptr);
        g_object_set(G_OBJECT(appData->rtspSrc), "udp-reconnect", true, nullptr);
        g_object_set(G_OBJECT(appData->rtspSrc), "latency", 200, nullptr);
        g_object_set(G_OBJECT(appData->rtspSrc), "user-id", appData->rtsp_username.c_str(), nullptr);
        g_object_set(G_OBJECT(appData->rtspSrc), "user-pw", appData->rtsp_password.c_str(), nullptr);
    }

    bool Streamer::hasNVIDIA() {
        GList *plugins, *p;

        bool has_nvidia = false;
        plugins = gst_registry_get_plugin_list(gst_registry_get());
        for (p = plugins; p; p = p->next) {
            auto *plugin = static_cast<GstPlugin *>(p->data);

            // Check for nvcodec
            if (strcmp(gst_plugin_get_name(plugin), "nvcodec") == 0) {
                has_nvidia = gst_registry_check_feature_version(gst_registry_get(), "nvh265dec", 1, 22, 0);

                if (has_nvidia)
                    break;
            }
        }

        gst_plugin_list_free(plugins);
        return has_nvidia;
    }

    bool Streamer::hasVAAPI() {
        GList *plugins, *p;

        bool has_vaapi = false;
        plugins = gst_registry_get_plugin_list(gst_registry_get());
        for (p = plugins; p; p = p->next) {
            auto *plugin = static_cast<GstPlugin *>(p->data);
            // Check for vaapi
            if (strcmp(gst_plugin_get_name(plugin), "vaapi") == 0) {
                has_vaapi = gst_registry_check_feature_version(gst_registry_get(), "vaapidecodebin", 1, 22, 0);

                if (has_vaapi)
                    break;
            }
        }

        gst_plugin_list_free(plugins);

        return has_vaapi;
    }

    bool Streamer::hasTimestamper() {
        GList *plugins, *p;

        bool has_timestamper = false;
        plugins = gst_registry_get_plugin_list(gst_registry_get());
        for (p = plugins; p; p = p->next) {
            auto *plugin = static_cast<GstPlugin *>(p->data);
            // Check for codectimestamper
            if (strcmp(gst_plugin_get_name(plugin), "codectimestamper") == 0) {
                has_timestamper = gst_registry_check_feature_version(gst_registry_get(), "h264timestamper", 1, 22, 0);

                if (has_timestamper)
                    break;
            }
        }

        gst_plugin_list_free(plugins);

        return has_timestamper;
    }

    bool Streamer::hasU30() {
        GList *plugins, *p;

        bool has_u30 = false;
        plugins = gst_registry_get_plugin_list(gst_registry_get());
        for (p = plugins; p; p = p->next) {
            auto *plugin = static_cast<GstPlugin *>(p->data);
            // Check for vvas_xvcudec
            if (strcmp(gst_plugin_get_name(plugin), "vvas_xvcuenc") == 0) {
                has_u30 = true;
                break;
            }
        }

        gst_plugin_list_free(plugins);

        return has_u30;
    }
}
