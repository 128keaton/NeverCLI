//
// Created by Keaton Burleson on 11/2/23.
//
#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"

#include "camera.h"
#include "common.h"
#include <regex>

namespace never {
    Camera::Camera(const string &camera_name, const string &stream_url, const string &snapshot_url, const string &output_path) {
        this->error_count = 0;
        this->camera_name = camera_name.c_str();
        this->input_format_context = avformat_alloc_context();
        this->stream_url = stream_url.c_str();
        this->snapshot_url = snapshot_url.c_str();
        this->output_path = output_path.c_str();
        this->output_stream = nullptr;
        this->output_format = nullptr;
        this->output_format_context = nullptr;
        this->input_codec_context = nullptr;
        this->input_stream = nullptr;
        this->curl_handle = nullptr;
        av_log_set_level(AV_LOG_QUIET);
        this->setupLogger();
    }

    void Camera::setupLogger() {
        string log_file_output = generateOutputFilename(this->camera_name, this->output_path, log);
        try {
            std::vector<spdlog::sink_ptr> sinks;

            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::trace);

            sinks.push_back(console_sink);

            // Disables log file output if using systemd
            if (!getenv("INVOCATION_ID")) {
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_file_output,1024 * 1024 * 10, 3);
                file_sink->set_level(spdlog::level::trace);
                sinks.push_back(file_sink);
            }


            logger = std::make_shared<spdlog::logger>(camera_name, sinks.begin(), sinks.end());
            logger->info("Initializing never-camera");

            logger->flush();
            logger->flush_on(spdlog::level::err);
        }
        catch (const spdlog::spdlog_ex &ex) {
            std::cout << "Log init failed: " << ex.what() << std::endl;
            logger = spdlog::stdout_color_mt("console");
        }
    }

    bool Camera::connect() {
        if (this->connected) return this->connected;

        this->input_index = -1;

        AVDictionary *params = nullptr;
        av_dict_set(&params, "rtsp_flags", "prefer_tcp", AV_DICT_APPEND);

        if (avformat_open_input(&input_format_context, stream_url, nullptr, &params) != 0)
            return handleError("Cannot open input file", false);


        // Get RTSP stream info
        if (avformat_find_stream_info(input_format_context, nullptr) < 0)
            return handleError("Cannot find stream info");

        // Find the video stream
        for (int i = 0; i < input_format_context->nb_streams; i++) {
            // Check if this is a video
            if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                // Set the input stream
                this->input_stream = input_format_context->streams[i];
                this->input_index = i;

                // Initialize the codec context
                this->input_codec_context = avcodec_alloc_context3(nullptr);
                break;
            }
        }


        if (this->input_index < 0)
            return handleError("Cannot find input video stream");


        this->connected = true;
        return true;
    }

    bool Camera::handleError(const string &message, bool close_input) {
        this->error_count += 1;
        logger->error(message);

        if (close_input)
            avformat_close_input(&this->input_format_context);

        return error_count > 10 ? EXIT_FAILURE : connect();
    }


    static size_t handleSnapshot(void *ptr, size_t size, size_t nmemb, void *stream) {
        size_t written = fwrite(ptr, size, nmemb, (FILE *) stream);
        return written;
    }

    /**
     * Take a camera snapshot
     */
    void Camera::takeSnapshot() {
        FILE *snapshot_file;
        string snapshot_file_str = generateOutputFilename(this->camera_name, this->output_path, image);

        this->logger->info("Taking snapshot");
        if (curl_handle == nullptr) {
            curl_global_init(CURL_GLOBAL_ALL);
            curl_handle = curl_easy_init();

            // Probably not the best way to handle this all but its w/e
            std::regex rgx("(\\w+?:.+@)?");
            std::smatch matches;

            string snapshot_url_str = string(this->snapshot_url);
            replaceFirst(snapshot_url_str, "http://", "");

            string username, password;

            if (std::regex_search(snapshot_url_str, matches, rgx)) {
                username = getUsername(matches[0]);
                password = getPassword(matches[0]);
            }

            curl_easy_setopt(curl_handle, CURLOPT_USERNAME, username.c_str());
            curl_easy_setopt(curl_handle, CURLOPT_PASSWORD, password.c_str());
            curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, handleSnapshot);

            curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
            curl_easy_setopt(curl_handle, CURLOPT_TRANSFERTEXT, 1);
            curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1);
        }

        curl_easy_setopt(curl_handle, CURLOPT_URL, this->snapshot_url);
        snapshot_file = fopen(snapshot_file_str.c_str(), "wb");

        if (snapshot_file) {
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, snapshot_file);
            curl_easy_perform(curl_handle);

            fclose(snapshot_file);
            this->logger->info("Validating snapshot");
            this->validateSnapshot(snapshot_file_str);
        }

    }


    /**
     * Validate a snapshot given at the file path
     * @param snapshot_file_path
     */
    void Camera::validateSnapshot(string snapshot_file_path) {
        FILE *snapshot_file = fopen(snapshot_file_path.c_str(), "r");

        unsigned char bytes[3];
        fread(bytes, 3, 1, snapshot_file);

        if (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) {
            fclose(snapshot_file);
            this->logger->debug("Wrote snapshot to {}", snapshot_file_path);
        } else {
            this->logger->error("Invalid JPEG!\r\n");
            remove(snapshot_file_path.c_str());
        }
    }

    int Camera::startRecording(long _clip_runtime) {
        if (this->clip_runtime != _clip_runtime) this->clip_runtime = _clip_runtime;
        if (!this->connected) {
            bool did_connect = connect();
            if (!did_connect)
                return handleError("Could not connect to camera", false);
        }

        this->logger->info("{} starting to record", this->camera_name);
        return this->record();
    }


    int Camera::setupMuxer() {
        string output_file_str = generateOutputFilename(this->camera_name, this->output_path, video);

        // Segment muxer
        output_format = (AVOutputFormat *) av_guess_format("segment", output_file_str.c_str(), nullptr);

        // Allocate output format context
        avformat_alloc_output_context2(&this->output_format_context, output_format, nullptr, output_file_str.c_str());

        // Create stream with context
        output_stream = avformat_new_stream(output_format_context, nullptr);

        // Copy the input stream codec parameters to the output stream
        if (avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar) < 0)
            return handleError("Cannot copy parameters to stream");

        output_format_context->strict_std_compliance = -1;

        // Allow macOS/iOS to play this natively
        output_stream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');

        // Set flags on output format context
        if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER)
            output_format_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // Customize stream rates/timing/aspect ratios/etc
        output_stream->sample_aspect_ratio.num = input_codec_context->sample_aspect_ratio.num;
        output_stream->sample_aspect_ratio.den = input_codec_context->sample_aspect_ratio.den;
        output_stream->r_frame_rate = input_stream->r_frame_rate;
        output_stream->avg_frame_rate = output_stream->r_frame_rate;

        AVDictionary *params = nullptr;

        // Set our muxer options
        av_dict_set(&params, "strftime", "true", 0);
        av_dict_set(&params, "reset_timestamps", "true", 0);
        av_dict_set(&params, "segment_time", std::to_string(clip_runtime).c_str(), 0);

        // Write the AVFormat header
        if (avformat_write_header(output_format_context, &params) < 0)
            return handleError("Cannot write header");

        return EXIT_SUCCESS;
    }

    int Camera::record() {
        double duration_counter = 0;
        AVPacket *packet;

        // Initialize the AVPacket
        packet = av_packet_alloc();

        // Build the muxer
        this->setupMuxer();

        // Take first snapshot
        this->takeSnapshot();

        // Keep track of last packet's pts
        int64_t last_pts = 0;

        // Keep track of packet's duration
        int64_t  duration = 0;

        // Read the packets incoming
        while (av_read_frame(input_format_context, packet) >= 0) {
            if (packet->pts < 0) {
                av_packet_unref(packet);
                continue;
            }

            if (packet->stream_index != input_index) {
                logger->error("Not right index");
                continue;
            }

            // This is _literally_ just to keep clang happy i.e. not marking it as unreachable
            duration += packet->duration;

            if (duration == 0) {
                packet->duration = packet->pts - last_pts;
            }

            // Keeps track of clip duration
            duration_counter += (double) (packet->duration) * av_q2d(input_stream->time_base);
            last_pts = packet->pts;

            packet->stream_index = output_stream->id;
            packet->pos = -1;

            av_interleaved_write_frame(output_format_context, packet);

            if (duration_counter >= (double) this->clip_runtime) {
                this->logger->info("Finished clip with duration {}, starting new clip", duration_counter);
                this->takeSnapshot();
                this->logger->flush();
                duration_counter = 0.0;
            }

            av_packet_unref(packet);
        }


        this->error_count = 0;

        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();

        avformat_close_input(&input_format_context);
        avcodec_close(input_codec_context);
        avformat_flush(output_format_context);
        av_packet_free(&packet);

        this->logger->info("{} stopping recording", this->camera_name);
        return EXIT_SUCCESS;
    }

    string Camera::getName() {
        return this->camera_name;
    }

    int Camera::clipCount() {
        return countClips(output_path, camera_name);
    }
} // never
#pragma clang diagnostic pop