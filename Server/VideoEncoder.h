#pragma once
#include <stdio.h>

// ffmpeg는 순수 C언어 프로젝트이기 때문에 아래와 같이 C언어로 작성된 것임을 명시해줘야한다.
// 원래 다른 프로젝트들은 헤더파일에 extern "C" 키워드를 명시해줘서 이러한 작업이 필요없지만
// ffmpeg는 이렇게 해주어야한다.
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
}


class VideoEncoder {
    AVFormatContext *origin_ctx = NULL;
    AVCodecContext *decoder_ctx = NULL;
    AVCodecContext *encoder_ctx = NULL;
    AVStream *origin_st = NULL;
    const AVCodec *encoder = NULL;

    AVPacket *origin_pkt;
    AVPacket *out_pkt;
    AVFrame *frame;
public:
    ~VideoEncoder();
    VideoEncoder(const char* path);
};