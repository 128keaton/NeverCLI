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
    std::shared_ptr<spdlog::logger> shared_logger;
    Janus shared_janus;

    typedef struct StreamData {
        GstElement *pipeline;
        GstElement *rtspSrc;
        GstElement *dePayloader;
        GstElement *decoder;
        GstElement *queue;
        GstElement *encoder;
        GstElement *payloader;
        GstElement *sink;
        string stream_name;
        int64_t rtp_port;
        int stream_id;
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
        string camera_name;
        string stream_url;
        string rtsp_username;
        string rtsp_password;
        string ip_address;
        bool has_vaapi{};
        bool quitting = false;
        static void padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *data);
    };
}

#endif //NEVER_CLI_STREAMER_H
