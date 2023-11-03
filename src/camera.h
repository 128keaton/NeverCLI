//
// Created by Keaton Burleson on 11/2/23.
//

#ifndef NEVER_CLI_CAMERA_H
#define NEVER_CLI_CAMERA_H

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
#include <string>
#include <iostream>
#include <thread>
#include <curl/curl.h>

using string = std::string;

namespace never {
    class Camera {
    public:
        Camera(const char *camera_name, const char *stream_url, const char *snapshot_url, const char *output_path);
        bool connect();
        int startRecording(long _clip_runtime, int &did_finish);
        int clipCount();

    private:
        AVCodecContext *input_codec_context;
        AVFormatContext *input_format_context;
        AVStream *input_stream;
        AVOutputFormat *output_format;
        AVFormatContext *output_format_context;
        AVStream *output_stream;
        CURL *curl_handle;

        const char *camera_name;
        const char *stream_url;
        const char *snapshot_url;
        const char *output_path;


        bool connected = false;
        int input_index = -1;
        long clip_runtime = 0;
        int error_count = 0;

        int record();
        int setupMuxer();
        void takeSnapshot();
        bool handleError(const string &message, bool close_input = true);
    };

} // never

#endif //NEVER_CLI_CAMERA_H
