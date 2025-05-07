#include "VideoEncoder.h"

VideoEncoder::~VideoEncoder()
{
    avformat_free_context(origin_ctx);
    
    printf("VideoEncoder Free\n");
}

VideoEncoder::VideoEncoder(const char *path)
{
    printf("FFmpeg avutil version: %s\n", av_version_info());
    printf("Target Video : %s\n", path);

    if (avcodec_find_encoder(AV_CODEC_ID_H264) == NULL) {
        fprintf(stderr, "H.264 encoder not found in this build.\n");
        return;
    }
    
    // 파일 읽기
    if(avformat_open_input(&origin_ctx, path, NULL, NULL) < 0) {
        printf("Can't open file\n");
        return;
    }

    if(avformat_find_stream_info(origin_ctx, NULL) < 0) {
        printf("Can't find Stream\n");
        return;
    }


    int vid_idx = av_find_best_stream(origin_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vid_idx < 0) {
        fprintf(stderr, "No video stream found\n");
        return;
    }
    
    origin_st = origin_ctx->streams[vid_idx];

    const AVCodec *decoder = avcodec_find_decoder(origin_st->codecpar->codec_id);
    if(decoder == NULL) {
        printf("Can't find decoder\n");
        return;
    }
    decoder_ctx = avcodec_alloc_context3(decoder);
    if(decoder_ctx == NULL) {
        printf("Can't allocate decoder context\n");
        return;
    }
    if(avcodec_parameters_to_context(decoder_ctx, origin_st->codecpar) < 0) {
        printf("Failed to set parameter to context\n");
        return;
    }

    if(avcodec_open2(decoder_ctx, decoder, NULL) < 0) {
        printf("Failed to open decoder\n");
        return;
    }

    encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(encoder == NULL) {
        printf("Can't find encoder\n");
        return;
    }

    encoder_ctx = avcodec_alloc_context3(encoder);
    if(encoder_ctx == NULL) {
        printf("Can't allocate encoder context\n");
        return;
    }

    encoder_ctx->width = decoder_ctx->width;
    encoder_ctx->height = decoder_ctx->height;
    encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_ctx->time_base = origin_st->time_base;
    encoder_ctx->bit_rate = 400000;
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "preset", "medium", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);

    if(avcodec_open2(encoder_ctx, encoder, &opts) < 0) {
        printf("Failed to Open Encoder\n");
        return;
    }
    
    origin_pkt = av_packet_alloc();
    frame = av_frame_alloc();
    out_pkt = av_packet_alloc();
}