extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavutil/timestamp.h>
}

#include <sys/time.h>
#include <cstdlib>

time_t get_time() {
    struct timeval tv = {};

    gettimeofday(&tv, nullptr);

    return tv.tv_sec;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

int stream_to_file(const char *stream_url, const char *output_file, const long clip_runtime) {
    AVCodecContext *input_codec_context;
    AVFormatContext *input_format_context = avformat_alloc_context();
    AVFormatContext *output_format_context;
    AVStream *input_stream;
    AVStream *output_stream;
    AVPacket *packet;

    AVOutputFormat const *output_format;


    int input_index = -1;
    time_t time_now, time_start;


    // Initialize ffmpeg library
    av_log_set_level(AV_LOG_ERROR);
    avformat_network_init();


    printf("Opening RTSP stream from URL %s\n", stream_url);

    // Open RTSP input
    if (avformat_open_input(&input_format_context, stream_url, nullptr, nullptr) != 0) {
        printf("ERROR: Cannot open input file\n");
        avformat_close_input(&input_format_context);
        return EXIT_FAILURE;
    }

    // Get RTSP stream info
    if (avformat_find_stream_info(input_format_context, nullptr) < 0) {
        printf("ERROR: Cannot find stream info\n");
        avformat_close_input(&input_format_context);
        return EXIT_FAILURE;
    }



    // Find the video stream
    for (int i = 0; i < input_format_context->nb_streams; i++) {
        // Determine the decoder from the codec ID from the given input stream


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

    while (av_read_frame(input_format_context, packet) >= 0 && time_now - time_start <= clip_runtime) {
        if (packet->stream_index == input_index) {
            log_packet(output_format_context, packet, "out");

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

    // Write the closing file bits, close the context
    av_write_trailer(output_format_context);
    avio_close(output_format_context->pb);
    avformat_free_context(input_format_context);
    avformat_free_context(output_format_context);

    avformat_network_deinit();

    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: %s RTSP_STREAM_URL OUTPUT_PATH SPLIT_EVERY\n"
               "i.e. %s rtsp://0.0.0.0 ./test.mp4 60\n"
               "Write an RTSP stream to file.\n"
               "\n", argv[0], argv[0]);
        return 1;
    }


    long clip_runtime = strtol(argv[3], nullptr, 10);


    printf("File %s\n", argv[1]);

    return stream_to_file(argv[1],
                          argv[2],  clip_runtime);
}

