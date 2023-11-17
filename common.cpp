//
// Created by Keaton Burleson on 11/2/23.
//

#include <libavformat/avformat.h>
#include "common.h"

using std::ifstream;
using string = std::string;
using path = std::filesystem::path;
using json = nlohmann::json;
using nvr_logger = std::shared_ptr<spdlog::logger>;

namespace fs = std::filesystem;

namespace nvr {
    CameraConfig getConfig(const char* config_file) {
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

        json config;

        try {
            config = json::parse(config_stream);
        }
        catch (json::exception&exception) {
            spdlog::error("Could not parse JSON: {}", exception.what());
            exit(EXIT_FAILURE);
        }

        string requiredFields[] = {
            "splitEvery",
            "rtpPort",
            "id",
            "streamURL",
            "snapshotURL",
            "outputPath",
            "ipAddress",
            "rtspUsername",
            "rtspPassword",
            "type",
        };

        for (auto field: requiredFields) {
            if (!config.contains(field)) {
                spdlog::error("Configuration is missing field", field);
                exit(EXIT_FAILURE);
            }
        }

        // Get required fields
        const long clip_runtime = config["splitEvery"];
        const int rtp_port = config["rtpPort"];
        const int stream_id = config["id"];
        const string stream_url = config["streamURL"];
        const string snapshot_url = config["snapshotURL"];
        const string output_path = config["outputPath"];
        const string ip_address = config["ipAddress"];
        const string rtsp_username = config["rtspUsername"];
        const string rtsp_password = config["rtspPassword"];

        const string raw_type = config["type"];
        const StreamType type = raw_type == "h264" ? h264 : h265;

        // Get optional fields
        string sub_stream_url =  config["streamURL"];
        int port = 554;
        int quality_level = 5;
        int quality_factor = 32;
        int max_bframes = 10;
        int keyframe_period = 5;
        long snapshot_interval = config["splitEvery"];

        if (config.contains("port"))
            port = config["port"];

        if (config.contains("subStreamURL"))
            sub_stream_url = config["subStreamURL"];

        if (config.contains("snapshotInterval"))
            snapshot_interval = config["snapshotInterval"];

        if (config.contains("qualityFactor"))
            quality_factor = config["qualityFactor"];

        if (config.contains("qualityLevel"))
            quality_level = config["qualityLevel"];

        if (config.contains("maxBFrames"))
            max_bframes = config["maxBFrames"];

        if (config.contains("keyframePeriod"))
            keyframe_period = config["keyframePeriod"];

        StreamQualityConfig quality_config = {
            quality_level,
            quality_factor,
            max_bframes,
            keyframe_period
        };

        return {
            stream_url,
            sub_stream_url,
            snapshot_url,
            output_path,
            stream_name,
            ip_address,
            rtsp_username,
            rtsp_password,
            type,
            stream_id,
            clip_runtime,
            snapshot_interval,
            rtp_port,
            port,
            quality_config,
        };
    }

    bool isReachable(const string&ip_addr) {
        CURL* connection;
        CURLcode res;
        bool reachable = false;

        connection = curl_easy_init();

        string url = string("http://").append(ip_addr);


        if (connection) {
            curl_easy_setopt(connection, CURLOPT_URL, url.c_str());
            curl_easy_setopt(connection, CURLOPT_CUSTOMREQUEST, "OPTIONS");
            /* issue an OPTIONS * request (no leading slash) */
            curl_easy_setopt(connection, CURLOPT_REQUEST_TARGET, "*");

            res = curl_easy_perform(connection);

            if (res == CURLE_OK)
                reachable = true;

            curl_easy_cleanup(connection);
        }

        return reachable;
    }


    nvr_logger buildLogger(const CameraConfig&config) {
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
        catch (const spdlog::spdlog_ex&ex) {
            std::cout << "Log init failed: " << ex.what() << std::endl;
            return spdlog::stdout_color_mt("console");
        }
    }

    string appendTimestamp(string&current) {
        char buf[1024];
        time_t now0;
        struct tm *tm, temp_buffer{};
        time(&now0);
        tm = localtime_r(&now0, &temp_buffer);
        strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", tm);
        current.append(buf);
        return current;
    }

    string generateOutputFilename(const string&name, const string&output_path, FileType file_type) {
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


    int countClips(const string&output_path, const string&camera_name) {
        fs::path videos_path{output_path};
        videos_path /= "videos";

        fs::create_directory(videos_path);
        videos_path /= camera_name;
        fs::create_directory(videos_path);

        int clip_count = 0;
        for (auto const&dir_entry: std::filesystem::directory_iterator{videos_path}) {
            std::cout << dir_entry.file_size() << '\n';
            clip_count += 1;
        }

        return clip_count;
    }

    string buildStreamURL(const string&url, const string&ip_address, const int port, const string&password,
                          const string&username, const StreamType&codec) {
        string base = string("rtsp://");

        if (password.empty()) {
            base = base.append(ip_address);
        }
        else {
            base = base.append(username)
                    .append(":")
                    .append(password)
                    .append("@")
                    .append(ip_address);
        }

        if (port == 80) {
            if (url == "/axis-media/media.amp") {
                switch (codec) {
                    case h265:
                        return base.append(url).append("?videocodec=h265");
                    case h264:
                        return base.append(url).append("?videocodec=h264");
                }
            }
            else {
                return base.append(url);
            }
        }


        return base.append(":").append(std::to_string(port)).append(url);
    }

    string sanitizeStreamURL(const string&stream_url, const string&password) {
        return std::regex_replace(string(stream_url), std::regex(password), string(password.length(), '*'));
    }
} // never
