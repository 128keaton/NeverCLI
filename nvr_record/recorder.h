//
// Created by Keaton Burleson on 11/2/23.
//

#ifndef NEVER_CLI_RECORDER_H
#define NEVER_CLI_RECORDER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
}

#include "nlohmann/json.hpp"
#include "../common.h"
#include <string>
#include <iostream>
#include <thread>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/sinks/rotating_file_sink.h>

using string = std::string;

namespace nvr {
    class Recorder {
    public:
        Recorder(const CameraConfig &config);

        bool connect();

        int startRecording(long _clip_runtime);

        int clipCount();

        string getName();

    private:
        AVCodecContext *input_codec_context;
        AVFormatContext *input_format_context;
        AVStream *input_stream;
        AVOutputFormat *output_format;
        AVFormatContext *output_format_context;
        AVStream *output_stream;
        CURL *curl_handle;
        std::shared_ptr<spdlog::logger> logger;

        string camera_name;
        string stream_url;
        string snapshot_url;
        string output_path;
        string rtsp_username;
        string rtsp_password;
        string ip_address;


        bool connected = false;
        int input_index = -1;
        long clip_runtime = 0;
        int error_count = 0;

        int record();

        int setupMuxer();

        void takeSnapshot();

        void validateSnapshot(string snapshot_file_path);

        bool handleError(const string &message, bool close_input = true);
    };

} // never

#endif //NEVER_CLI_RECORDER_H
