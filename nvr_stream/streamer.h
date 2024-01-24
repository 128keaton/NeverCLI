//
// Created by Keaton Burleson on 11/8/23.
//

#ifndef NEVER_CLI_STREAMER_H
#define NEVER_CLI_STREAMER_H

#include "../common.h"
#include <gst/gst.h>
#include <gst/gstpad.h>
#include "janus.h"

namespace nvr {

    typedef struct StreamData {
        GstElement *pipeline;
        GstElement *rtspSrc;
        GstElement *dePayloader;
        GstElement *parser;
        GstElement *timestamper;
        GstElement *decoder;
        GstElement *encoder;
        GstElement *payloader;
        GstElement *sink;
        string stream_name;
        int64_t rtp_port;
        int64_t bitrate;
        int64_t buffer_size;
        string stream_id;
        std::shared_ptr<spdlog::logger> logger;
        Janus janus;
        GMainLoop *loop;
        gboolean is_live;
        gboolean is_h265;
        bool needs_codec_switch;
        int error_count;
        string stream_url;
        string rtsp_username;
        string rtsp_password;
    } StreamData;


    class Streamer {

    public:
        Streamer();
        explicit Streamer(const CameraConfig &config);
        int start();
        void quit();
        bool valid();


    private:
        GstBus *bus{};
        StreamType type{};
        StreamData appData{};
        std::shared_ptr<spdlog::logger> logger;
        int rtp_port{};
        int port{};
        string camera_id;
        string stream_url;
        string rtsp_username;
        string rtsp_password;
        string ip_address;
        bool has_vaapi = false;
        bool has_nvidia = false;
        bool quitting = false;
        static void callbackMessage ([[maybe_unused]] GstBus *bus, GstMessage *msg, StreamData *data);
        static void padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *data);
        static void createJanusStream(StreamData *data);
        static void setupStreamInput(StreamData *appData);
        static void setupStreamOutput(StreamData *appData,  bool create_encoder);
        static void teardownStreamCodecs(StreamData *appData);
        static void setupRTSPStream(StreamData *appData);
        static void switchCodecs(StreamData *appData);
        static bool hasVAAPI();
        static bool hasNVIDIA();
        int findOpenPort();
    };
}

#endif //NEVER_CLI_STREAMER_H
