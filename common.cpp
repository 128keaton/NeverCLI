//
// Created by Keaton Burleson on 11/2/23.
//

#include <libavformat/avformat.h>
#include "common.h"

using std::ifstream;
using string = std::string;
using path = std::filesystem::path;
using json = nlohmann::json;
namespace fs = std::filesystem;


namespace nvr {
    CameraConfig getConfig(const char *config_file) {
        const string config_file_path = string(config_file);

        size_t last_path_index = config_file_path.find_last_of('/');
        string config_file_name = config_file_path.substr(last_path_index + 1);
        size_t last_ext_index = config_file_name.find_last_of('.');
        string stream_name = config_file_name.substr(0, last_ext_index);

        if (access(config_file, F_OK) != 0) {
            spdlog::error("Cannot read config file: {}", config_file);
            exit(-1);
        }

        ifstream config_stream(config_file);
        json config = json::parse(config_stream);

        const long clip_runtime = config["splitEvery"];
        const int rtp_port = config["rtpPort"];
        const string stream_url = config["streamURL"];
        const string snapshot_url = config["snapshotURL"];
        const string output_path = config["outputPath"];
        const string ip_address = config["ipAddress"];
        const string rtsp_username = config["rtspUsername"];
        const string rtsp_password = config["rtspPassword"];

        return {
                stream_url,
                snapshot_url,
                output_path,
                stream_name,
                ip_address,
                rtsp_username,
                rtsp_password,
                clip_runtime,
                rtp_port
        };
    }

    bool isReachable(const string &ip_addr) {
        CURL *connection;
        CURLcode res;
        bool reachable = false;

        connection = curl_easy_init();

        string url = string("http://").append(ip_addr);


        if (connection) {
            curl_easy_setopt(connection, CURLOPT_URL, url.c_str());
            curl_easy_setopt(connection, CURLOPT_CUSTOMREQUEST, "OPTIONS");
            /* issue an OPTIONS * request (no leading slash) */
            curl_easy_setopt(connection, CURLOPT_REQUEST_TARGET, "*");

            /* if this operation fails, allow risking a memory leak and do quick exit
               from libcurl as this will exit() anyway */
            curl_easy_setopt(connection, CURLOPT_QUICK_EXIT, 1L);
            res = curl_easy_perform(connection);

            if (res == CURLE_OK)
                reachable = true;

            curl_easy_cleanup(connection);
        }

        return reachable;
    }

    void writePID(pid_t pid, const string &pid_file_name) {
        FILE *pid_file;

        pid_file = fopen(pid_file_name.c_str(), "w");
        fprintf(pid_file, "%d", pid);
        fclose(pid_file);
    }

    std::shared_ptr<spdlog::logger> buildLogger(const CameraConfig &config) {
        string log_file_output = generateOutputFilename(config.stream_name, config.output_path, log);
        try {
            std::vector<spdlog::sink_ptr> sinks;

            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::trace);

            sinks.push_back(console_sink);

            // Disables log file output if using systemd
            if (!getenv("INVOCATION_ID")) {
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file_output,
                                                                                        1024 * 1024 * 10, 3);
                file_sink->set_level(spdlog::level::trace);
                sinks.push_back(file_sink);
            }

            auto logger = std::make_shared<spdlog::logger>(config.stream_name, sinks.begin(), sinks.end());
            logger->flush_on(spdlog::level::err);
            return logger;
        }
        catch (const spdlog::spdlog_ex &ex) {
            std::cout << "Log init failed: " << ex.what() << std::endl;
            return spdlog::stdout_color_mt("console");
        }
    }

    string appendTimestamp(string &current) {
        char buf[1024];
        time_t now0;
        struct tm *tm, temp_buffer{};
        time(&now0);
        tm = localtime_r(&now0, &temp_buffer);
        strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", tm);
        current.append(buf);
        return current;
    }

    string generateOutputFilename(const string &name, const string &output_path, FileType file_type) {
        string file_name;

        file_name.append(name);
        file_name.append("-");

        switch (file_type) {
            case video:
                file_name.append("%Y-%m-%d_%H-%M-%S");
                file_name.append(".mp4");
                break;
            case image:
                file_name = appendTimestamp(file_name);
                file_name.append(".jpeg");
                break;
            case log:
                file_name.append("log.txt");
                break;
        }


        path file_path = output_path;

        switch (file_type) {
            case video:
                file_path /= ("videos");
                break;
            case image:
                file_path /= ("snapshots");
                break;
            case log:
                file_path /= ("logs");
        }

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
        fs::path videos_path{output_path};
        videos_path /= "videos";

        fs::create_directory(videos_path);
        videos_path /= camera_name;
        fs::create_directory(videos_path);

        int clip_count = 0;
        for (auto const &dir_entry: std::filesystem::directory_iterator{videos_path}) {
            std::cout << dir_entry.file_size() << '\n';
            clip_count += 1;
        }

        return clip_count;
    }


} // never