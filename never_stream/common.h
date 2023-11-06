//
// Created by Keaton Burleson on 11/2/23.
//


#include <sys/time.h>
#include <string>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include "nlohmann/json.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
}


#ifndef NEVER_CLI_COMMON_H
#define NEVER_CLI_COMMON_H

using string = std::string;

namespace never {
    enum FileType { video, image, log };
    string generateOutputFilename(const string& name, const string& output_path, FileType file_type);
    void replaceFirst(string &s, string const &to_replace, string const &with);
    string getUsername(std::string const &value);
    string getPassword(std::string const &value);
    time_t getTime();
    int countClips(const string &output_path, const string &camera_name);

} // never

#endif //NEVER_CLI_COMMON_H
