//
// Created by Keaton Burleson on 11/2/23.
//
#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"

#include "camera.h"
#include "common.h"

#include <regex>

using json = nlohmann::json;
using thread = std::thread;

namespace never {
    Camera::Camera(const char *camera_name, const char *stream_url, const char *snapshot_url, const char *output_path) {
        this->error_count = 0;
        this->camera_name = camera_name;
        this->input_format_context = avformat_alloc_context();
        this->stream_url = stream_url;
        this->snapshot_url = snapshot_url;
        this->output_path = output_path;
        this->output_stream = nullptr;
        this->output_format = nullptr;
        this->output_format_context = nullptr;
        this->input_codec_context = nullptr;
        this->input_stream = nullptr;
    }

    bool Camera::connect() {
        if (this->connected) return connected;

        input_format_context->start_time_realtime = get_time();

        this->input_index = -1;
        if (avformat_open_input(&input_format_context, stream_url, nullptr, nullptr) != 0)
            return error_return("Cannot open input file", false);


        // Get RTSP stream info
        if (avformat_find_stream_info(input_format_context, nullptr) < 0)
            return error_return("Cannot find stream info");



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
            return error_return("Cannot find input video stream");


        this->connected = true;
        return true;
    }

    bool Camera::error_return(const string &message, bool close_input) {
        this->error_count += 1;
        printf("ERROR: %s\n", message.c_str());

        if (close_input)
            avformat_close_input(&this->input_format_context);

        return error_count > 10 ? EXIT_FAILURE : connect();
    }


    void Camera::printStatus(const string &output_file, const string &snapshot_file) {
        json config;
        config["outputFile"] = output_file;
        config["rtspStream"] = this->stream_url;
        config["clipRuntime"] = this->clip_runtime;
        config["snapshotFile"] = snapshot_file;

        std::cout << config.dump(4) << std::endl;
    }

    static size_t handleSnapshot(void *ptr, size_t size, size_t nmemb, void *stream) {
        size_t written = fwrite(ptr, size, nmemb, (FILE *) stream);
        return written;
    }

    void Camera::takeSnapshot(const string &snapshot_file_str) {
        CURL *curl_handle;
        FILE *snapshot_file;

        curl_global_init(CURL_GLOBAL_ALL);

        /* init the curl session */
        curl_handle = curl_easy_init();

        // Probably not the best way to handle this all but its w/e
        std::regex rgx("(\\w+?:.+@)?");
        std::smatch matches;

        string snapshot_url_str = string(this->snapshot_url);
        replace_first(snapshot_url_str, "http://", "");

        string username, password;

        if (std::regex_search(snapshot_url_str, matches, rgx)) {
            username = get_username(matches[0]);
            password = get_password(matches[0]);
        }

        curl_easy_setopt(curl_handle, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_PASSWORD, password.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_URL, this->snapshot_url);
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, handleSnapshot);

        curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
        curl_easy_setopt(curl_handle, CURLOPT_TRANSFERTEXT, 1);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1);

        snapshot_file = fopen(snapshot_file_str.c_str(), "wb");

        if (snapshot_file) {
            /* write the page body to this file handle */
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, snapshot_file);

            /* get it! */
            curl_easy_perform(curl_handle);

            /* close the header file */
            fclose(snapshot_file);
        }

        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
    }

    int Camera::startRecording(long _clip_runtime, int &did_finish) {
        if (did_finish > 0)
            return EXIT_SUCCESS;


        if (this->clip_runtime != _clip_runtime) this->clip_runtime = _clip_runtime;

        if (!this->connected) {
            bool did_connect = connect();

            if (!did_connect)
                return error_return("Could not connect to camera", false);
        }

        return this->record(did_finish);
    }


    int Camera::newClip(bool init) {
        string snapshot_file_str = generate_output_filename(this->camera_name, this->output_path, false);
        string output_file_str = generate_output_filename(this->camera_name, this->output_path, true);

        if (init) {
            output_format = (AVOutputFormat *) av_guess_format(nullptr, output_file_str.c_str(), nullptr);
            avformat_alloc_output_context2(&this->output_format_context, output_format, nullptr,output_file_str.c_str());
            pts_offset = AV_NOPTS_VALUE;
        } else {
            av_write_trailer(output_format_context);
            avio_close(output_format_context->pb);
        }

        if (avio_open2(&output_format_context->pb, output_file_str.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr) != 0)
            return error_return("Cannot open output");

        if (init) {
            output_stream = avformat_new_stream(output_format_context, nullptr);

            // Copy the input stream codec parameters to the output stream
            if (avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar) < 0)
                return error_return("Cannot copy parameters to stream");


            // Set flags on output format context
            if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER)
                output_format_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            // Customize stream rates/timing/aspect ratios/etc
            output_stream->sample_aspect_ratio.num = input_codec_context->sample_aspect_ratio.num;
            output_stream->sample_aspect_ratio.den = input_codec_context->sample_aspect_ratio.den;
            output_stream->r_frame_rate = input_stream->r_frame_rate;
            output_stream->avg_frame_rate = output_stream->r_frame_rate;
        }

        // Write the file header
        if (avformat_write_header(output_format_context, nullptr) < 0)
            return error_return("Cannot write header");

        // Take snapshot
        thread([this]() {
            takeSnapshot(snapshot_url);
        }).detach();


        // Print nice JSON status
        this->printStatus(output_file_str, snapshot_file_str);

        return EXIT_SUCCESS;
    }

    int Camera::record(int &did_finish) {
        AVPacket *packet;

        time_t time_now, time_start;

        // Set time_start to whatever current time since epoch
        time_start = time_now = get_time();

        // Initialize the AVPacket
        packet = av_packet_alloc();

        this->newClip();

        int current_pts = 0;

        // Read the packets incoming
        while (av_read_frame(input_format_context, packet) >= 0 && did_finish < 1) {
            if (packet->stream_index == input_index) {
                if (time_start == time_now && packet->stream_index >= input_format_context->nb_streams) {
                    time_start = time_now = get_time();
                    av_packet_unref(packet);
                    continue;
                }


                packet->stream_index = output_stream->id;
                packet->pts = av_rescale_q_rnd(packet->pts, input_stream->time_base, output_stream->time_base,
                                               (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

                packet->pts = packet->pts - current_pts;

                if (pts_offset == AV_NOPTS_VALUE) {
                    pts_offset = packet->pts;
                }

                packet->pts -= pts_offset;
                packet->dts -= pts_offset;
                packet->dts = av_rescale_q_rnd(packet->dts, input_stream->time_base, output_stream->time_base,
                                               (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                packet->duration = av_rescale_q(packet->duration, input_stream->time_base, output_stream->time_base);
                packet->pos = -1;

                av_interleaved_write_frame(output_format_context, packet);

                if (time_now - time_start >= clip_runtime) {
                    time_start = time_now = get_time();
                    this->newClip(false);
                } else {
                    time_now = get_time();
                }
                av_packet_unref(packet);
            }
        }

        this->error_count = 0;
        return EXIT_SUCCESS;
    }
} // never
#pragma clang diagnostic pop