#pragma once
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include "WebCam.h"

/// @brief cv::Mat 형식을 h.264로 인코딩
class Encoder {
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    AVCodecContext* enc = nullptr;
    AVFrame* frame = av_frame_alloc();
    int64_t pts = 0;
    
    static void copyI420ToAVFrame(const cv::Mat& i420, AVFrame* f, int W, int H);
    Encoder();
public:
    ~Encoder();
    static Encoder& instance();
    AVFrame* cvMat2AvFrame(cv::Mat rgb);
};