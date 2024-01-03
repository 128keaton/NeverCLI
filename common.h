//
// Created by Keaton Burleson on 11/2/23.
//

#include <sys/time.h>
#include <string>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include "nlohmann/json.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <fstream>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <curl/curl.h>
#include <regex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
}


#ifndef NEVER_CLI_COMMON_H
#define NEVER_CLI_COMMON_H

using string = std::string;
using nvr_logger = std::shared_ptr<spdlog::logger>;

// TODO: cleanup this file
namespace nvr {
    enum StreamType {
        h265,
        h264
    };

    enum FileType {
        video,
        image,
        log
    };


    struct CameraConfig {
        string stream_url;
        string sub_stream_url;
        string snapshot_url;
        string output_path;
        string stream_name;
        string ip_address;
        string rtsp_username;
        string rtsp_password;
        StreamType type;
        string stream_id;
        const long clip_runtime;
        const long snapshot_interval;
        const int port;
    };

    string buildStreamURL(const string&url, const string&ip_address, int port, const string&password,
                          const string&username, bool use_tcp = false);

    string sanitizeStreamURL(const string&stream_url, const string&password);

    CameraConfig getConfig(const char* config_file);

    string generateOutputFilename(const string&camera_id, const string&output_path, FileType file_type, bool temporary = false);

    int countClips(const string&output_path, const string&camera_name);

    nvr_logger buildLogger(const CameraConfig&config);
} // nvr

#endif
