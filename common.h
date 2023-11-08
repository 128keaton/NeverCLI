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

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
}


#ifndef NEVER_CLI_COMMON_H
#define NEVER_CLI_COMMON_H

using string = std::string;

namespace nvr {
    enum StreamType { h265, h264 };
    struct CameraConfig{
        string stream_url;
        string snapshot_url;
        string output_path;
        string stream_name;
        string ip_address;
        string rtsp_username;
        string rtsp_password;
        StreamType type;
        int stream_id;
        const long clip_runtime;
        const int rtp_port;
    };
    CameraConfig getConfig(const char *config_file);
    enum FileType { video, image, log };
    string generateOutputFilename(const string& name, const string& output_path, FileType file_type);
    void replaceFirst(string &s, string const &to_replace, string const &with);
    string getUsername(std::string const &value);
    string getPassword(std::string const &value);
    time_t getTime();
    int countClips(const string &output_path, const string &camera_name);
    pid_t readPID(const string &pid_file_name);
    void writePID(pid_t pid, const string &pid_file_name);
    std::shared_ptr<spdlog::logger> buildLogger(const CameraConfig &config);
    int spawnTask(const string &pid_file_name);
} // nvr

#endif
