//
// Created by Keaton Burleson on 11/2/23.
//
#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"

#include "recorder.h"
#include "../common.h"

using path = std::filesystem::path;
namespace fs = std::filesystem;

namespace nvr {

    std::shared_ptr<Recorder> Recorder::instance(){
        static std::shared_ptr<Recorder> s{new Recorder};
        return s;
    }

    void Recorder::logCallback([[maybe_unused]] void *ptr, int level, const char *fmt, va_list vargs) {
        if (level <= AV_LOG_ERROR) {
            vprintf(fmt, vargs);
        } else {
            string string_fmt = string(fmt);

            if (string_fmt.find(string("segment")) != std::string::npos) {
                auto segment_file_name = va_arg(vargs, char*);

                if (string_fmt.find(string("ended")) != std::string::npos) {
                    auto segment_source_file_name = string(segment_file_name);
                    auto segment_dest_file_name = string(segment_file_name);
                    auto tmp_path = string("/tmp/");
                    auto dot_path = string("./");

                    // Change destination from original (/tmp/./videos/blah/x.mp4)
                    std::size_t tmp_pos = segment_dest_file_name.find(tmp_path);
                    if (tmp_pos == std::string::npos) return;
                    segment_dest_file_name.replace(tmp_pos, tmp_path.length(), "");

                    std::size_t dot_pos = segment_source_file_name.find(dot_path);
                    if (dot_pos == std::string::npos) return;
                    segment_source_file_name.replace(dot_pos, dot_path.length(), "");

                    path segment_file_dest_path = (segment_dest_file_name.c_str());
                    path segment_file_src_path = (segment_source_file_name.c_str());

                    instance()->last_clip = segment_file_dest_path;
                    instance()->logger->info("Finished clip '{}' with runtime of {} seconds", segment_file_dest_path.filename().c_str(), instance()->clip_runtime);
                    instance()->logger->info("Copying clip from '{}' to '{}'", segment_file_src_path.string(), segment_file_dest_path.string());

                    fs::create_directories(segment_file_dest_path.parent_path());
                    fs::copy(segment_file_src_path, segment_file_dest_path);

                    instance()->logger->info("Removing temporary clip from '{}'", segment_file_src_path.string());
                    fs::remove(segment_file_src_path);
                } else if (string_fmt.find(string("starts")) != std::string::npos) {
                    auto segment_file_name_string = string(segment_file_name);
                    auto dot_path = string("./");

                    std::size_t pos = segment_file_name_string.find(dot_path);
                    if (pos == std::string::npos) return;
                    segment_file_name_string.replace(pos, dot_path.length(), "");

                    instance()->logger->info("Starting clip '{}' with predicted runtime of {} seconds", segment_file_name_string, instance()->clip_runtime);
                }
            }
        }
    }


    Recorder::Recorder() {
        av_log_set_level(AV_LOG_DEBUG);
        av_log_set_callback(logCallback);
    }

    void Recorder::configure(const CameraConfig &config) {
        this->type = config.type;
        this->error_count = 0;
        this->camera_id = config.stream_id;
        this->input_format_context = avformat_alloc_context();
        this->stream_url = config.sub_stream_url;
        this->snapshot_url = config.snapshot_url;
        this->output_path = config.output_path;
        this->rtsp_username = config.rtsp_username;
        this->rtsp_password = config.rtsp_password;
        this->ip_address = config.ip_address;
        this->output_stream = nullptr;
        this->output_format = nullptr;
        this->output_format_context = nullptr;
        this->input_codec_context = nullptr;
        this->input_stream = nullptr;
        this->curl_handle = nullptr;
        this->port = config.port;
        this->snapshot_interval = config.snapshot_interval;
        this->logger = buildLogger(config);
        this->configured = true;
    }

    void Recorder::quit() {
        this->logger->info("Exiting...");

        if (this->curl_handle != nullptr) {
            curl_easy_cleanup(this->curl_handle);
            curl_global_cleanup();
        }

        if (this->input_format_context != nullptr)
            avformat_close_input(&this->input_format_context);

        if (this->input_codec_context != nullptr)
            avcodec_close(this->input_codec_context);

        if (this->output_format_context != nullptr)
            avformat_flush(this->output_format_context);
    }

    bool Recorder::valid() {
        return this->logger != nullptr && this->configured;
    }

    bool Recorder::connect() {
        if (this->connected) return this->connected;

        this->input_index = -1;

        AVDictionary *params = nullptr;
        av_dict_set(&params, "rtsp_flags", "prefer_tcp", AV_DICT_APPEND);

        string full_stream_url = buildStreamURL(this->stream_url, this->ip_address, this->port, this->rtsp_password,
                                                this->rtsp_username);
        string sanitized_stream_url = sanitizeStreamURL(full_stream_url, this->rtsp_password);

        this->logger->info("Opening connection to '{}'", sanitized_stream_url);

        if (avformat_open_input(&input_format_context, full_stream_url.c_str(), nullptr, &params) != 0)
            return handleError("Cannot open input file", false);


        // Get RTSP stream info
        if (avformat_find_stream_info(input_format_context, nullptr) < 0)
            return handleError("Cannot find stream info");

        this->logger->info("Connected to '{}'", full_stream_url);

        // Find the video stream
        for (int i = 0; i < input_format_context->nb_streams; i++) {
            // Check if this is a video
            if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                // Set the input stream
                this->input_stream = input_format_context->streams[i];
                this->input_index = i;
                this->logger->info("Using stream index '{}'", input_index);

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

    bool Recorder::handleError(const string &message, bool close_input) {
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
    void Recorder::takeSnapshot() {
        FILE *snapshot_file;
        string snapshot_file_str = generateOutputFilename(this->camera_id, this->output_path, image);
        string full_snapshot_url = string("http://").append(this->ip_address).append(this->snapshot_url);

        this->logger->info("Taking snapshot from URL '{}'", full_snapshot_url);

        if (curl_handle == nullptr) {
            curl_global_init(CURL_GLOBAL_ALL);
            curl_handle = curl_easy_init();


            curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, handleSnapshot);

            curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
            curl_easy_setopt(curl_handle, CURLOPT_TRANSFERTEXT, 1);
            curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1);
        }

        curl_easy_setopt(curl_handle, CURLOPT_USERNAME, this->rtsp_username.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_PASSWORD, this->rtsp_password.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_URL, full_snapshot_url.c_str());

        snapshot_file = fopen(snapshot_file_str.c_str(), "wb");

        this->logger->info("Saving snapshot to '{}'", snapshot_file_str);

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
    void Recorder::validateSnapshot(string snapshot_file_path) {
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

    int Recorder::startRecording(long _clip_runtime) {
        if (this->clip_runtime != _clip_runtime) this->clip_runtime = _clip_runtime;
        if (!this->connected) {
            bool did_connect = connect();
            if (!did_connect)
                return handleError("Could not connect to camera", false);
        }

        this->logger->info("{} starting to record", this->camera_id);
        return this->record();
    }


    int Recorder::setupMuxer() {
        string output_file_str = generateOutputFilename(this->camera_id, this->output_path, video, true);

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
        av_dict_set(&params, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);

        // Write the AVFormat header
        if (avformat_write_header(output_format_context, &params) < 0)
            return handleError("Cannot write header");

        return EXIT_SUCCESS;
    }

    int Recorder::record() {
        double duration_counter = 0;
        double snapshot_duration_counter = 0;
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
        int64_t duration = 0;

        // Read the packets incoming
        while (av_read_frame(input_format_context, packet) >= 0) {
            if (packet->pts < 0) {
                av_packet_unref(packet);
                continue;
            }

            if (packet->stream_index != input_index) {
                logger->warn("Not right index: '{}' (from packet), input_index: '{}'", packet->stream_index,
                              input_index);
            }

            // This is _literally_ just to keep clang happy i.e. not marking it as unreachable
            duration += packet->duration;

            if (duration == 0) {
                packet->duration = packet->pts - last_pts;
            }

            // Keeps track of clip duration
            duration_counter += (double) (packet->duration) * av_q2d(input_stream->time_base);

            // Keeps track of time between snapshots
            snapshot_duration_counter += (double) (packet->duration) * av_q2d(input_stream->time_base);

            last_pts = packet->pts;

            packet->stream_index = output_stream->id;
            packet->pos = -1;

            av_interleaved_write_frame(output_format_context, packet);

            // Finished writing clip
            if (duration_counter >= (double) this->clip_runtime)
                duration_counter = 0.0;

            // Take snapshot
            if (snapshot_duration_counter >= (double) this->snapshot_interval) {
                this->takeSnapshot();
                snapshot_duration_counter = 0.0;
            }

            av_packet_unref(packet);
        }

        this->logger->warn("Recording loop exited");

        quit();
        av_packet_free(&packet);

        return EXIT_SUCCESS;
    }

} // nvr
#pragma clang diagnostic pop