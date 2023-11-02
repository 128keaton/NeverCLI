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
    static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
        AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

        printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
               tag,
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
               av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
               av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
               pkt->stream_index);
    }


    Camera::Camera(const char *camera_name, const char *stream_url, const char *snapshot_url, const char *output_path) {
        this->error_count = 0;
        this->camera_name = camera_name;

        this->input_format_context = avformat_alloc_context();
        this->stream_url = stream_url;
        this->snapshot_url = snapshot_url;
        this->output_path = output_path;
    }

    bool Camera::connect() {
        if (this->connected) return connected;

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
        size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
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

        if(std::regex_search(snapshot_url_str, matches, rgx)) {
            username = get_username(matches[0]);
            password = get_password(matches[0]);
        }

        curl_easy_setopt(curl_handle, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_PASSWORD, password.c_str());
        curl_easy_setopt(curl_handle, CURLOPT_URL, this->snapshot_url);
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, handleSnapshot);

        curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
        curl_easy_setopt(curl_handle, CURLOPT_TRANSFERTEXT , 1);
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1);

        snapshot_file = fopen(snapshot_file_str.c_str(), "wb");

        if(snapshot_file) {
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

    int Camera::record(int &did_finish) {
        const AVOutputFormat *output_format;
        AVFormatContext *output_format_context;
        AVStream *output_stream;
        AVPacket *packet;

        time_t time_now, time_start;

        string output_file_str = generate_output_filename(this->camera_name, this->output_path, true);
        const char *output_file = output_file_str.c_str();


        output_format = av_guess_format(nullptr, output_file, nullptr);


        // Initialize output context
        avformat_alloc_output_context2(&output_format_context, output_format, nullptr, output_file);

        // Get file handle for writing
        if (avio_open2(&output_format_context->pb, output_file, AVIO_FLAG_WRITE, nullptr, nullptr) != 0)
            return error_return("Cannot open output");


        // Create output stream
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
        output_stream->time_base = av_inv_q(output_stream->r_frame_rate);

        // Set time_start to whatever current time since epoch
        time_start = time_now = get_time();

        // Initialize the AVPacket
        packet = av_packet_alloc();

        // Write the file header
        if (avformat_write_header(output_format_context, nullptr) < 0)
            return error_return("Cannot write header");

        string snapshot_file_str = generate_output_filename(this->camera_name, this->output_path, false);
        takeSnapshot(snapshot_file_str);

        printStatus(output_file_str, snapshot_file_str);

        while (av_read_frame(input_format_context, packet) >= 0 && time_now - time_start <= clip_runtime &&
               did_finish < 1) {
            if (packet->stream_index == input_index) {
                // Discard invalid packets?
                if (time_start == time_now && packet->stream_index >= input_format_context->nb_streams) {
                    time_start = time_now = get_time();
                    av_packet_unref(packet);
                    continue;
                }

                if (packet->pts <= 0) {
                    time_start = time_now = get_time();
                    av_packet_unref(packet);
                    continue;
                }


                //    log_packet(input_format_context, packet, "out");

                packet->stream_index = output_stream->id;

                // This is where the magic happens, our rate is "calculated" so this is how we scale
                av_packet_rescale_ts(packet, input_stream->time_base, output_stream->time_base);
                packet->pos = -1;

                av_interleaved_write_frame(output_format_context, packet);

                time_now = get_time();

            }
        }

        // Close the context and write file handle but async, so we can start next process immediately
        thread([&output_format_context]() {
            av_write_trailer(output_format_context);
            avio_close(output_format_context->pb);
            avformat_free_context(output_format_context);
        }).detach();

        avformat_close_input(&input_format_context);
        this->connected = false;
        this->error_count = 0;


        if (did_finish == 0)
            return startRecording(this->clip_runtime, did_finish);


        return EXIT_SUCCESS;
    }
} // never
#pragma clang diagnostic pop