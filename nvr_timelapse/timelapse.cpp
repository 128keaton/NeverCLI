
/* 
 By Teddy Silvance on 11/19/23.
 Encode camera snapshots into mp4 video using H.264 codec
*/
extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

#include <filesystem>
#include <iostream>
#include <string>
#include <iterator>
#include <ctime>
#include "../common.h"

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile) {
    int ret;
    /* send the frame to the encoder */
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int main(int argc, char **argv) {
    int days;
    const AVCodec *codec;
    AVCodecContext *c = NULL;
    int i, ret;
    FILE *f;
    AVFrame *frame;
    uint8_t endcode[] = {0, 0, 1, 0xb7};

    /*validate args*/
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <NUMBER_OF_DAYS> <PATH_TO_CAMERA_JSON>\n", argv[0]);
        exit(0);
    }
    days = atoi(argv[1]);
    if (days == 0) {
        fprintf(stderr, "%s is not an integer\n", argv[1]);
        exit(1);
    }
    std::filesystem::path json_file_path = argv[2];
    if (!exists(json_file_path)) {
        fprintf(stderr, "Could not find %s. File does not exist.\n", argv[2]);
        exit(1);
    }

    std::string camera_name = (std::string(json_file_path).substr(
            std::string(json_file_path).find("camera-"),
            std::string(json_file_path).find(".json") -
            std::string(json_file_path).find("camera-")));

    /*check if snapshots exist*/
    std::filesystem::path snapshot_directory = std::filesystem::path("/nvr/snapshots") /
                                          camera_name;
    if (!exists(snapshot_directory)) {
        fprintf(stderr, "Could not find %s. Path does not exist.", std::string(snapshot_directory).c_str());
        exit(1);
    }

    /*create timelapse file*/
    string timelapse_file_str = generateOutputFilename(camera_name, string("/nvr"), nvr::timelapse);
    //std::cout<<timelapse_file_str<<std::endl;
    f = fopen(timelapse_file_str.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", timelapse_file_str.c_str());
        //exit(1);
    }
    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        fprintf(stderr, "Codec libx264 not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    /* put sample parameters */
    c->bit_rate = 400000;
    c->qmin = 16;
    c->qmax = 32;
    /* resolution must be a multiple of two */
    c->width = 1920;
    c->height = 1080;
    /* frames per second */
    c->time_base = (AVRational) {1, 25};
    c->framerate = (AVRational) {25, 1};

    /* emit one intra frame every 25 frames*/
    c->gop_size = 25;
    c->max_b_frames = 10;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open codec */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->format = c->pix_fmt;
    frame->width = c->width;
    frame->height = c->height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    /*set start of timelapse based on days specified*/
    time_t current_time;
    time(&current_time);
    current_time -= days*24*3600;
    tm *local_time = localtime(&current_time);
    char date_time[20];
    strftime(date_time, 20, "%Y-%m-%d_%H-%M-%S", local_time);
    std::string start_date_time = std::string(snapshot_directory) + "/" + camera_name + "-" + std::string(date_time);

    /*sort files in the directory in ascending order*/
    std::vector<std::filesystem::path> files_in_directory;
    copy(std::filesystem::directory_iterator(snapshot_directory), std::filesystem::directory_iterator(), // directory_iterator::value_type
         std::back_inserter(files_in_directory));
    sort(files_in_directory.begin(), files_in_directory.end());
    /*loop through the snapshots*/
    i = 0;
    for (const auto &filename: files_in_directory) {
        //for (i=0; i< 25; i++) {
        fflush(stdout);

        /*make the frame data is writable by allocating a new buffer for the frame*/
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);
        /*prepare image*/
        AVFormatContext *input_format_context;
        input_format_context = avformat_alloc_context();

        /*continue to next snapshot file if current file is earlier that start_date_time */
        if(strcmp(start_date_time.c_str(), std::string(filename).c_str()) > 0){
            continue;
        }
        if (avformat_open_input(&input_format_context, std::string(filename).c_str(), NULL, NULL) != 0) {
            fprintf(stderr, "Unable to open %s\n", std::string(filename).c_str());
            exit(1);
        }
        if (avformat_find_stream_info(input_format_context, NULL) < 0) {
            fprintf(stderr, "Unable to find steam info");
            avformat_close_input(&input_format_context);
            exit(1);
        }

        int video_stream_index = -1;
        for (uint16_t stream_index = 0; stream_index < input_format_context->nb_streams; stream_index++) {
            if (input_format_context->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_index = stream_index;
                break;
            }
        }
        if (video_stream_index == -1) {
            fprintf(stderr, "Could not find video steam.");
            avformat_close_input(&input_format_context);
            exit(1);
        }
        /*Find decoder for video stream*/
        AVCodecParameters *input_codec_parameters = input_format_context->streams[video_stream_index]->codecpar;
        const AVCodec *input_codec = avcodec_find_decoder(input_codec_parameters->codec_id);
        if (input_codec == NULL) {
            fprintf(stderr, "Could not find decoder\n");
            avformat_close_input(&input_format_context);
            exit(1);
        }

        AVCodecContext *input_codec_context;
        input_codec_context = avcodec_alloc_context3(input_codec);
        if (avcodec_open2(input_codec_context, input_codec, NULL) < 0) {
            fprintf(stderr, "Could not open decoder\n");
            avformat_close_input(&input_format_context);
            exit(1);
        }

        /*read encoded frame into AVPacket*/
        AVPacket *encoded_packet;
        encoded_packet = av_packet_alloc();
        encoded_packet->data = NULL;
        encoded_packet->size = 0;
        if (av_read_frame(input_format_context, encoded_packet) < 0) {
            fprintf(stderr, "Cannot read frame\n");
            av_packet_free(&encoded_packet);
            avcodec_close(input_codec_context);
            avformat_close_input(&input_format_context);
            exit(1);
        }

        /*decode the encoded_frame in 2 steps*/
        /*1. send raw packet to decoder*/
        if (avcodec_send_packet(input_codec_context, encoded_packet) < 0) {
            fprintf(stderr, "Could not decode frame\n");
            av_packet_free(&encoded_packet);
            avcodec_close(input_codec_context);
            avformat_close_input(&input_format_context);
            exit(1);
        }
        /*2. receive decoded frame from decoder*/
        AVFrame *decoded_frame = av_frame_alloc();
        if (avcodec_receive_frame(input_codec_context, decoded_frame) < 0) {
            fprintf(stderr, "Could not decode frame\n");
            av_packet_free(&encoded_packet);
            avcodec_close(input_codec_context);
            avformat_close_input(&input_format_context);
            exit(1);
        }

        /*allocate an SwsContext to perform scaling and conversion operations using sw_scale()*/
        SwsContext *sws_context = sws_getContext(decoded_frame->width, decoded_frame->height,
                                                 AV_PIX_FMT_YUV420P, c->width, c->height, c->pix_fmt,
                                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws_context == NULL) {
            fprintf(stderr, "Error while calling sws_getContext\n");
            av_packet_free(&encoded_packet);
            avcodec_close(input_codec_context);
            avformat_close_input(&input_format_context);
            exit(1);
        }

        avformat_close_input(&input_format_context);

        /*scale image*/
        sws_scale(sws_context, decoded_frame->data, decoded_frame->linesize, 0, decoded_frame->height, frame->data,
                  frame->linesize);
        sws_freeContext(sws_context);
        /*assign presentation time stamp to frame*/
        frame->pts = i;
        /* encode the image */
        encode(c, frame, encoded_packet, f);
        av_packet_free(&encoded_packet);
        avcodec_close(input_codec_context);
        avformat_close_input(&input_format_context);
        avformat_free_context(input_format_context);
        i++;
    }

    /* flush the encoder */
    //encode(c, NULL, NULL, f);

    /* Add sequence end code to have a real MPEG file*/
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
        fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    return 0;
}
