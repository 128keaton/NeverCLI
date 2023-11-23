

/**
 * @file libavcodec encoding video API usage example
 * @example encode_video.c
 *
 * Generate synthetic video data and encode it to an output file.
 */
extern "C"{
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>

	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/opt.h>
	#include <libavutil/imgutils.h>
}

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

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

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int main(int argc, char **argv)
{
    const char *filename, *codec_name;
    const AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, ret, x, y;
    FILE *f;
    AVFrame *frame;
    AVPacket *pkt;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    codec_name = argv[2];

    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 352;
    c->height = 288;
    /* frames per second */
    c->time_base = (AVRational){1, 25};
    c->framerate = (AVRational){25, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n");// av_err2str(ret));
        exit(1);
    }

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    /* encode 1 second of video */
    for (i = 0; i < 25; i++) {
        fflush(stdout);

        /* Make sure the frame data is writable.
           On the first round, the frame is fresh from av_frame_get_buffer()
           and therefore we know it is writable.
           But on the next rounds, encode() will have called
           avcodec_send_frame(), and the codec may have kept a reference to
           the frame in its internal structures, that makes the frame
           unwritable.
           av_frame_make_writable() checks that and allocates a new buffer
           for the frame only if necessary.
         */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        /* Prepare a dummy image.
           In real code, this is where you would have your own logic for
           filling the frame. FFmpeg does not care what you put in the
           frame.
         */
        /* 
        // Y
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        // Cb and Cr
        for (y = 0; y < c->height/2; y++) {
            for (x = 0; x < c->width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }
	*/
	AVFormatContext *input_format_context;
	input_format_context = avformat_alloc_context();
	//AVDictionary * input_image_dict;
	if(avformat_open_input(&input_format_context, "image1.jpeg", NULL, NULL) != 0){
	  fprintf(stderr, "Unable to open image1.jpeg\n");
	  exit(1);
	}
	if(avformat_find_stream_info(input_format_context, NULL) < 0){
	  fprintf(stderr, "Unable to find steam info");
	  avformat_close_input(&input_format_context);
	  exit(1);
	}
	//printf("%d\n", input_format_context->nb_streams);
	int video_stream_index = -1;
	for(uint16_t stream_index = 0; stream_index < input_format_context->nb_streams; stream_index++){
	  if(input_format_context->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ){
	    video_stream_index = stream_index;
	    break;
	  }
	}
	if(video_stream_index == -1){
	  fprintf(stderr, "Could not find video steam.");
	  avformat_close_input(&input_format_context);
	  exit(1);
	}
	/*Find decoder for video stream*/
	AVCodecParameters * input_codec_parameters = input_format_context->streams[video_stream_index]->codecpar;
	const AVCodec * input_codec = avcodec_find_decoder(input_codec_parameters->codec_id);
	if (input_codec == NULL){
	  fprintf(stderr, "Could not find decoder\n");
	  avformat_close_input(&input_format_context);
	  exit(1);
	}
	
	AVCodecContext * input_codec_context;
	input_codec_context = avcodec_alloc_context3(input_codec);
	if (avcodec_open2(input_codec_context, input_codec, NULL) < 0){
	  fprintf(stderr, "Could not open decoder\n");
	  avformat_close_input(&input_format_context);
	  exit(1);
	}
	//printf("opened decoder successfully");
	/*read encoded frame into AVPacket*/
	AVPacket * encoded_packet;
	encoded_packet = av_packet_alloc();
	encoded_packet->data = NULL;
	encoded_packet->size = 0;
	if (av_read_frame(input_format_context, encoded_packet) < 0){
	  fprintf(stderr, "Cannot read frame\n");
	  av_packet_free(&encoded_packet);
	  avcodec_close(input_codec_context);
	  avformat_close_input(&input_format_context);
	  exit(1);
	}
	//printf("Read frame successfully");
	/*decode the encoded_frame*/
	/*deprecated avcodec_decode_video2(input_codec_context,decoded_frame,&frame_finished,encoded_packet);*/
	/*frame_finished = 1 means whole frame decoded, 0 means that the frame was not decoded*/
	/*send raw packet to decoder*/
	if(avcodec_send_packet(input_codec_context, encoded_packet) < 0){
	  fprintf(stderr, "Could not decode frame\n");
	  av_packet_free(&encoded_packet);
	  avcodec_close(input_codec_context);
	  avformat_close_input(&input_format_context);
	  exit(1);
	}
	//printf("Decoded frame successfully 1");
	/*recieve decoded frame from decoder*/
	AVFrame *decoded_frame = av_frame_alloc();
	if(avcodec_receive_frame(input_codec_context, decoded_frame) < 0){
	  fprintf(stderr, "Could not decode frame\n");
	  av_packet_free(&encoded_packet);
	  avcodec_close(input_codec_context);
	  avformat_close_input(&input_format_context);
	  exit(1);
	}
	//printf("Decoded frame successfully 2");
	//printf("width = %d ,height= %d, format=%d, %d\n", decoded_frame->width, decoded_frame->height, decoded_frame->format, frame->format);
	
	/*allocate an SwsContext to perform scaling and conversion operations using sw_scale()*/
	SwsContext * sws_context = sws_getContext(decoded_frame->width, decoded_frame->height,
                      AV_PIX_FMT_YUV420P,c->width, c->height, c->pix_fmt,
                      SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws_context == NULL){
    	  fprintf(stderr, "Error while calling sws_getContext\n");
    	  av_packet_free(&encoded_packet);
	  avcodec_close(input_codec_context);
	  avformat_close_input(&input_format_context);
    	  exit(1);
	}
	//printf("Created SwsContext successfully");
	avformat_close_input(&input_format_context);
        frame->pts = i;
        /*scale image*/
	sws_scale(sws_context, decoded_frame->data, decoded_frame->linesize, 0, decoded_frame->height, frame->data, frame->linesize);
	sws_freeContext(sws_context);
        /* encode the image */
        encode(c, frame, encoded_packet, f);
        av_packet_free(&encoded_packet);
	avcodec_close(input_codec_context);
	avformat_close_input(&input_format_context);
        avformat_free_context(input_format_context);
    }

    /* flush the encoder */
    encode(c, NULL, pkt, f);

    /* Add sequence end code to have a real MPEG file.
       It makes only sense because this tiny examples writes packets
       directly. This is called "elementary stream" and only works for some
       codecs. To create a valid file, you usually need to write packets
       into a proper file format or protocol; see mux.c.
     */
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
        fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);


    return 0;
}
