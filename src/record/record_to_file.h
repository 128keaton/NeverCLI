//
// Created by Keaton Burleson on 11/1/23.
//

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavutil/timestamp.h>
}

#include <sys/time.h>
#include <cstdlib>
#include <string>
#include <iostream>
#include <csignal>
#include <thread>
#include "nlohmann/json.hpp"


#ifndef NEVER_CLI_RECORD_TO_FILE_H
#define NEVER_CLI_RECORD_TO_FILE_H


class RecordToFile {
public:
    RecordToFile(const char *_stream_url, const char *_output_path, const char *_stream_name, long _clip_runtime);
    int start(int &did_finish);

private:
    int error_count;
    int network_init;
    int take_snapshot = 0;
    const char *stream_url;
    const char *output_path;
    const char *stream_name;
    long clip_runtime;
    static time_t get_time();
    std::string generate_output_filename(int is_video);

};


#endif //NEVER_CLI_RECORD_TO_FILE_H
