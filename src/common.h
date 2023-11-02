//
// Created by Keaton Burleson on 11/2/23.
//

#include <sys/time.h>
#include <string>
#include <cstdlib>
#include <iostream>
#include <filesystem>

#ifndef NEVER_CLI_COMMON_H
#define NEVER_CLI_COMMON_H

using string = std::string;

namespace never {
    string generate_output_filename(const string& name, const string& output_path, bool is_video = true);
    void replace_first(string &s, string const &to_replace, string const &with);
    string get_username(std::string const &value);
    string get_password(std::string const &value);
    time_t get_time();

} // never

#endif //NEVER_CLI_COMMON_H
