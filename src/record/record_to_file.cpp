//
// Created by Keaton Burleson on 11/1/23.
//

#include "record_to_file.h"

using json = nlohmann::json;


RecordToFile::RecordToFile(const char *_stream_url, const char *_output_path, const char *_stream_name,
                           long _clip_runtime) {
    error_count = 0;
    network_init = 0;

    stream_url = _stream_url;
    output_path = _output_path;
    stream_name = _stream_name;
    clip_runtime = _clip_runtime;
}


int RecordToFile::start(int &did_finish) { // NOLINT(*-no-recursion)
    if (did_finish > 0) {
        return EXIT_SUCCESS;
    }


    AVFormatContext *output_format_context;
    AVFormatContext *input_format_context = avformat_alloc_context();
    AVCodecContext *input_codec_context;
    AVStream *input_stream;
    AVStream *output_stream;
    AVPacket *packet;
    AVOutputFormat const *output_format;

    time_t time_now, time_start;
    int input_index = -1;
    int snapshot_taken = 0;

    // Only initialize this once
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
            return start(did_finish);
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
            return start(did_finish);
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
    std::string output_file_str = generate_output_filename(1);
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


    while (av_read_frame(input_format_context, packet) >= 0 && time_now - time_start <= clip_runtime && did_finish < 1) {
        if (packet->stream_index == input_index) {
            std::cout.clear();
            // log_packet(output_format_context, packet, "out");



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
            time_now = get_time();

            if (snapshot_taken == 0 && take_snapshot == 1) {
                snapshot_taken = 1;
                std::string snapshot_file_str = generate_output_filename(0);
                const char *snapshot_file = snapshot_file_str.c_str();

                AVCodec const *snapshot_codec = avcodec_find_decoder(output_stream->codecpar->codec_id);
                AVCodecContext *snapshot_codec_context = avcodec_alloc_context3(snapshot_codec);
                if (!snapshot_codec_context) {
                    printf("failed to allocated memory for AVCodecContext\n");
                    return -1;
                }

                if (avcodec_parameters_to_context(snapshot_codec_context, output_stream->codecpar) < 0)
                {
                    printf("failed to copy codec params to codec context\n");
                    return -1;
                }

                if (avcodec_open2(snapshot_codec_context, snapshot_codec, nullptr) < 0)
                {
                    printf("failed to open codec through avcodec_open2\n");
                    return -1;
                }



                AVFrame *picture_frame = av_frame_alloc();
                if (!picture_frame) {
                    printf("ERROR: Cannot get picture frame\n");
                    avformat_close_input(&input_format_context);
                    return EXIT_FAILURE;
                }

                int response = avcodec_send_packet(snapshot_codec_context, packet);
                while (response >= 0) {
                    response = avcodec_receive_frame(snapshot_codec_context, picture_frame);

                    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                        break;
                    } else if (response < 0) {
                        printf("Error while receiving a frame from the decoder: %s\n", av_err2str(response));
                        return response;
                    }

                    FILE *snapshot;
                    int i;
                    snapshot = fopen(snapshot_file, "w");

                    fprintf(snapshot, "\211   P   N   G  \r  \n \032 \n");

                    for (i = 0; i < picture_frame->height; i++)
                        fwrite(picture_frame->data[0] + i * picture_frame->linesize[0], 1, picture_frame->width,
                               snapshot);
                    fclose(snapshot);
                }

                av_frame_free(&picture_frame);
                avcodec_free_context(&snapshot_codec_context);
            }
        }
    }

    // Close the context and write file handle but async, so we can start next process immediately
    std::thread([&output_format_context]() {
        av_write_trailer(output_format_context);
        avio_close(output_format_context->pb);
        avformat_free_context(output_format_context);
    }).detach();

    avformat_close_input(&input_format_context);
    error_count = 0;


    if (did_finish == 0) {
        return start(did_finish);
    }

    return EXIT_SUCCESS;
}

time_t RecordToFile::get_time() {
    struct timeval tv = {};

    gettimeofday(&tv, nullptr);

    return tv.tv_sec;
}

std::string RecordToFile::generate_output_filename(int is_video) {
    std::string file_name;

    file_name.append(stream_name);
    file_name.append("-");
    file_name.append(std::to_string(get_time()));
    file_name.append(is_video == 1 ? ".mp4" : ".png");

    std::filesystem::path path = output_path;
    path /= file_name;

    return path.string();
}



