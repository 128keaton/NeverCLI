//
// Created by Keaton Burleson on 11/2/23.
//

#include "common.h"

using string = std::string;
using path = std::filesystem::path;

namespace never {
    string generate_output_filename(const string &name, const string &output_path, bool is_video) {
        string file_name;

        file_name.append(name);
        file_name.append("-");
        file_name.append(std::to_string(get_time()));
        file_name.append(is_video == 1 ? ".mp4" : ".jpeg");

        path file_path = output_path;
        file_path /= file_name;

        return file_path.string();
    }

    void replace_first(string &s, string const &to_replace, string const &with) {
        std::size_t pos = s.find(to_replace);
        if (pos == string::npos) return;
        s.replace(pos, to_replace.length(), with);
    }

    string get_username(std::string const &value) {
        string::size_type pos = value.find(':');
        if (pos == std::string::npos) {
            return value;
        } else {
            return value.substr(0, pos);
        }
    }

    string get_password(string const &value) {
        string::size_type pos = value.find(':');
        string return_val = pos == string::npos ? value : value.substr(pos + 1, value.length() - pos - 1);

        replace_first(return_val, "@", "");
        return return_val;
    }

    time_t get_time() {
        struct timeval tv = {};

        gettimeofday(&tv, nullptr);

        return tv.tv_sec;
    }

} // never