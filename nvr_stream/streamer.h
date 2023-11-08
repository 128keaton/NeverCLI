//
// Created by Keaton Burleson on 11/8/23.
//

#ifndef NEVER_CLI_STREAMER_H
#define NEVER_CLI_STREAMER_H

#include "../common.h"
#include <gst/gst.h>
#include <gst/gstpad.h>

namespace nvr {
    typedef struct StreamData {
        GstElement *pipeline;
        GstElement *rtspSrc;
        GstElement *dePayloader;
        GstElement *decoder;
        GstElement *encoder;
        GstElement *payloader;
        GstElement *sink;
    } StreamData;


    class Streamer {
    public:
        explicit Streamer(const CameraConfig &config);
        int start();

    private:
        StreamType type;
        std::shared_ptr<spdlog::logger> logger;
        int rtp_port;
        string camera_name;
        string stream_url;
        string rtsp_username;
        string rtsp_password;
        string ip_address;
        bool has_vaapi;
        StreamData appData{};
        static void padAddedHandler(GstElement *src, GstPad *new_pad, StreamData *data);
    };
}

#endif //NEVER_CLI_STREAMER_H
