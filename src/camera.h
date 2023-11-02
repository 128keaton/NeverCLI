//
// Created by Keaton Burleson on 11/2/23.
//

#ifndef NEVER_CLI_CAMERA_H
#define NEVER_CLI_CAMERA_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavutil/time.h>
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

    private:
        AVCodecContext *input_codec_context;
        AVFormatContext *input_format_context;
        AVStream *input_stream;

        const char *camera_name;
        const char *stream_url;
        const char *snapshot_url;
        const char *output_path;

        bool connected;
        int input_index;
        long clip_runtime;
        int error_count;

        int record(int &did_finish);
        void takeSnapshot(const string &snapshot_file_str);
        void printStatus(const string &output_file, const string &snapshot_file);
        bool error_return(const string &message, bool close_input = true);
    };

} // never

#endif //NEVER_CLI_CAMERA_H
