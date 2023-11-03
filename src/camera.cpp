//
// Created by Keaton Burleson on 11/2/23.
//
#pragma clang diagnostic push
#pragma ide diagnostic ignored "misc-no-recursion"

#include "camera.h"
#include "common.h"
#include <regex>

using json = nlohmann::json;


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
        this->curl_handle = nullptr;
    }

    bool Camera::connect() {
        if (this->connected) return this->connected;

        this->input_index = -1;

        AVDictionary *params = nullptr;
        av_dict_set(&params, "rtsp_flags", "prefer_tcp", AV_DICT_APPEND);

        if (avformat_open_input(&input_format_context, stream_url, nullptr,&params) != 0)
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
        printf("ERROR: %s\n", message.c_str());

        if (close_input)
            avformat_close_input(&this->input_format_context);

        return error_count > 10 ? EXIT_FAILURE : connect();
    }


    static size_t handleSnapshot(void *ptr, size_t size, size_t nmemb, void *stream) {
        size_t written = fwrite(ptr, size, nmemb, (FILE *) stream);
        return written;
    }

    void Camera::takeSnapshot() {
        FILE *snapshot_file;
        string snapshot_file_str = generateOutputFilename(this->camera_name, this->output_path, false);

        if (curl_handle == nullptr) {
            curl_global_init(CURL_GLOBAL_ALL);

            /* init the curl session */
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
            printf("writing to %s\n", snapshot_file_str.c_str());
            /* write the page body to this file handle */
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, snapshot_file);

            /* get it! */
            curl_easy_perform(curl_handle);

            /* close the header file */
            fclose(snapshot_file);
        }

    }

    int Camera::startRecording(long _clip_runtime, bool &did_finish) {
        if (did_finish)
            return EXIT_SUCCESS;


        if (this->clip_runtime != _clip_runtime) this->clip_runtime = _clip_runtime;

        if (!this->connected) {
            bool did_connect = connect();

            if (!did_connect)
                return handleError("Could not connect to camera", false);
        }

        return this->record(did_finish);
    }


    int Camera::setupMuxer() {
        input_stream->start_time = getTime();
        string output_file_str = generateOutputFilename(this->camera_name, this->output_path, true);

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

    int Camera::record(bool &did_finish) {
        double duration_counter = 0;
        AVPacket *packet;


        // Initialize the AVPacket
        packet = av_packet_alloc();

        // Build the muxer
        this->setupMuxer();

        // Take first snapshot
        this->takeSnapshot();

        int64_t  last_pts = 0;
        // Read the packets incoming
        while (av_read_frame(input_format_context, packet) >= 0 && !did_finish) {
            if (packet->pts < 0) {
                av_packet_unref(packet);
                continue;
            }

            if (packet->stream_index != input_index) {
                printf("Not right index");
                continue;
            }

        //    av_pkt_dump_log2(nullptr, AV_LOG_ERROR, packet, 0, input_stream);
            printf("Packet duration: %ld, Duration: %f, clip runtime: %ld\n", packet->duration, duration_counter, clip_runtime);
            // Keeps track of clip duration

            duration_counter +=  (double) (packet->pts - last_pts) * av_q2d(input_stream->time_base);
            last_pts = packet->pts;

            packet->stream_index = output_stream->id;
            packet->pos = -1;


            av_interleaved_write_frame(output_format_context, packet);


            if (duration_counter >= (double) this->clip_runtime) {
                this->takeSnapshot();
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

        return EXIT_SUCCESS;
    }

    int Camera::clipCount() {
        return countClips(output_path, camera_name);
    }
} // never
#pragma clang diagnostic pop