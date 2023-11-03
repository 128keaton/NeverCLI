//
// Created by Keaton Burleson on 11/2/23.
//

#include <libavformat/avformat.h>
#include "common.h"

using json = nlohmann::json;
using string = std::string;
using path = std::filesystem::path;
namespace fs = std::filesystem;

namespace never {
    string generateOutputFilename(const string &name, const string &output_path, bool is_video) {
        string file_name;

        file_name.append(name);
        file_name.append("-");

        if (!is_video) {
            char buf[1024];
            time_t now0;
            struct tm *tm, tmpbuf{};
            time(&now0);
            tm = localtime_r(&now0, &tmpbuf);
            strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", tm);
            file_name.append(buf);
        } else {
            file_name.append("%Y-%m-%d_%H-%M-%S");
        }


        file_name.append(is_video == 1 ? ".mp4" : ".jpeg");

        path file_path = output_path;
        file_path /= (is_video ? "videos" : "snapshots");

        fs::create_directory(file_path);

        file_path /= name;

        fs::create_directory(file_path);

        file_path /= file_name;

        return file_path.string();
    }

    void replaceFirst(string &s, string const &to_replace, string const &with) {
        std::size_t pos = s.find(to_replace);
        if (pos == string::npos) return;
        s.replace(pos, to_replace.length(), with);
    }

    string getUsername(const string &value) {
        string::size_type pos = value.find(':');
        if (pos == std::string::npos) {
            return value;
        } else {
            return value.substr(0, pos);
        }
    }

    string getPassword(const string &value) {
        string::size_type pos = value.find(':');
        string return_val = pos == string::npos ? value : value.substr(pos + 1, value.length() - pos - 1);

        replaceFirst(return_val, "@", "");
        return return_val;
    }

    time_t getTime() {
        struct timeval tv = {};

        gettimeofday(&tv, nullptr);

        return tv.tv_sec;
    }

    int countClips(const string &output_path, const string &camera_name) {
        fs::path videos_path { output_path };
        videos_path /= "videos";

        fs::create_directory(videos_path);
        videos_path /= camera_name;
        fs::create_directory(videos_path);

        int clip_count = 0;
        for (auto const& dir_entry : std::filesystem::directory_iterator{videos_path}) {
            std::cout << dir_entry.file_size() << '\n';
            clip_count += 1;
        }

        return clip_count;
    }



} // never