extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavutil/timestamp.h>
}

#include <iostream>
#include <sys/time.h>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <csignal>
#include <thread>

using json = nlohmann::json;
int did_finish = 0;
int network_init = 0;
int error_count = 0;


time_t get_time() {
    struct timeval tv = {};

    gettimeofday(&tv, nullptr);

    return tv.tv_sec;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    json packet;
    packet["index"] = pkt->stream_index;
    packet["tag"] = tag;
    packet["pts"] = pkt->pts;
    packet["pts_time"] = av_ts2timestr(pkt->pts, time_base);
    packet["dts"] = pkt->dts;
    packet["dts_time"] = av_ts2timestr(pkt->dts, time_base);
    packet["duration"] = pkt->duration;
    packet["duration_time"] = av_ts2timestr(pkt->duration, time_base);

    std::cout << packet.dump(4) << std::endl;
}

std::string generate_output_filename(const char *output_path, const char *stream_name) {
    std::string file_name;

    file_name.append(stream_name);
    file_name.append("-");
    file_name.append(std::to_string(get_time()));
    file_name.append(".mp4");

    std::filesystem::path path = output_path;
    path /= file_name;

    return path.string();
}


void force_finish([[maybe_unused]] int code) {
    did_finish = 1;
}

int stream_to_file(const char *stream_url, const char *output_path, const char *stream_name, const long clip_runtime) {
    if (did_finish > 0) {
        return EXIT_SUCCESS;
    }
    AVFormatContext *output_format_context;
    AVFormatContext *input_format_context = avformat_alloc_context();
    AVCodecContext *input_codec_context;
    AVStream *input_stream = nullptr;
    AVStream *output_stream;
    AVPacket *packet;

    AVOutputFormat const *output_format = nullptr;
    time_t time_now, time_start;
    int input_index = -1;

    if (network_init == 0) {
        // Initialize ffmpeg library
        av_log_set_level(AV_LOG_ERROR);
        avformat_network_init();
        network_init = 1;
    }



    // Open RTSP input
    if (avformat_open_input(&input_format_context, stream_url, nullptr, nullptr) != 0) {
        printf("ERROR: Cannot open input file\n");
        avformat_close_input(&input_format_context);
        error_count += 1;

        if (error_count > 10) {
            return EXIT_FAILURE;
        } else {
            return stream_to_file(stream_url, output_path, stream_name, clip_runtime);
        }
    }

    // Get RTSP stream info
    if (avformat_find_stream_info(input_format_context, nullptr) < 0) {
        printf("ERROR: Cannot find stream info\n");
        avformat_close_input(&input_format_context);
        error_count += 1;

        if (error_count > 10) {
            return EXIT_FAILURE;
        } else {
            return stream_to_file(stream_url, output_path, stream_name, clip_runtime);
        }
    }




    // Find the video stream
    for (int i = 0; i < input_format_context->nb_streams; i++) {
        // Check if this is a video
        if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            // Set the input stream
            input_stream = input_format_context->streams[i];
            input_index = i;

            // Initialize the codec context
            input_codec_context = avcodec_alloc_context3(nullptr);
            break;
        }
    }

    if (input_index < 0) {
        printf("ERROR: Cannot find input video stream\n");
        avformat_close_input(&input_format_context);
        return EXIT_FAILURE;
    }

    // Get output file handle
    std::string output_file_str = generate_output_filename(output_path, stream_name);
    const char *output_file = output_file_str.c_str();

    json config;
    config["outputFile"] = output_file;
    config["rtspStream"] = stream_url;
    config["clipRuntime"] = clip_runtime;

    std::cout << config.dump(4) << std::endl;


    // Determine output format
    output_format = av_guess_format(nullptr, output_file, nullptr);

    // Initialize output context
    avformat_alloc_output_context2(&output_format_context, output_format, nullptr, output_file);

    // Get file handle for writing
    if (avio_open2(&output_format_context->pb, output_file, AVIO_FLAG_WRITE, nullptr, nullptr) != 0) {
        printf("ERROR: Cannot open output %s\n", output_file);
        return EXIT_FAILURE;
    }

    // Create output stream
    output_stream = avformat_new_stream(output_format_context, nullptr);

    // Copy the input stream codec parameters to the output stream
    if (avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar) < 0) {
        printf("ERROR: Cannot copy parameters to stream\n");
        avformat_close_input(&input_format_context);
        return EXIT_FAILURE;
    }

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
    if (avformat_write_header(output_format_context, nullptr) < 0) {
        printf("ERROR: Cannot write header\n");
        avformat_close_input(&input_format_context);
        return EXIT_FAILURE;
    }


    while (av_read_frame(input_format_context, packet) >= 0 && time_now - time_start <= clip_runtime &&
           did_finish < 1) {
        if (packet->stream_index == input_index) {
            std::cout.clear();
            // log_packet(output_format_context, packet, "out");
            std::signal(SIGINT, force_finish);
            // Discard invalid packets?
            if (time_start == time_now && packet->stream_index >= input_format_context->nb_streams) {
                time_start = time_now = get_time();
                av_packet_unref(packet);
                continue;
            }


            // Set stream index
            packet->stream_index = output_stream->id;

            // This fixes the timing issue
            av_packet_rescale_ts(packet, input_stream->time_base, output_stream->time_base);

            // I think this is for packet order
            packet->pos = -1;

            // Write the frame
            av_interleaved_write_frame(output_format_context, packet);
        }

        time_now = get_time();
    }

    // Close the context and write file handle but async so we can start next process immediately
    std::thread([&output_format_context, input_format_context]() {
        av_write_trailer(output_format_context);
        avio_close(output_format_context->pb);
        avformat_free_context(output_format_context);


    }).detach();
    avformat_close_input(&input_format_context);
    error_count = 0;


    if (did_finish == 0) {
        return stream_to_file(stream_url, output_path, stream_name, clip_runtime);
    }

    return EXIT_SUCCESS;
}


int main(int argc, char **argv) {
    if (argc < 4 || argc > 5) {
        printf("usage: %s RTSP_STREAM_URL OUTPUT_PATH STREAM_NAME SPLIT_EVERY\n"
               "i.e. %s rtsp://0.0.0.0 ./ 60\n"
               "Write an RTSP stream to file.\n"
               "\n", argv[0], argv[0]);
        return 1;
    }


    const char *stream_url = argv[1],
            *output_path = argv[2],
            *stream_name = argv[3];

    const long clip_runtime = strtol(argv[4], nullptr, 10);

    int return_state = EXIT_SUCCESS;
    std::signal(SIGINT, force_finish);
    return_state = stream_to_file(stream_url, output_path, stream_name, clip_runtime);

    avformat_network_deinit();
    return return_state;
}

