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
        GstElement *decoder;
        GstElement *encoder;
        GstElement *payloader;
        GstElement *initialQueue;
        GstElement *finalBufferQueue;
        GstElement *finalQueue;
        GstElement *sink;
        string stream_name;
        int64_t rtp_port;
        int stream_id;
        std::shared_ptr<spdlog::logger> logger;
        Janus janus;
        GMainLoop *loop;
        gboolean is_live;
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
        StreamType type;
        StreamData appData{};
        std::shared_ptr<spdlog::logger> logger;
        int rtp_port{};
        int port{};
        string camera_name;
        string stream_url;
        string rtsp_username;
        string rtsp_password;
        string ip_address;
        StreamQualityConfig quality_config;
        bool has_vaapi = false;
        bool has_nvidia = false;
        bool quitting = false;
        static int64_t toNanoseconds(int64_t seconds);
        static int64_t toBytes(int64_t megabytes);
        static void callbackMessage (GstBus *bus, GstMessage *msg, StreamData *data);
        static void padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *data);
    };
}

#endif //NEVER_CLI_STREAMER_H
