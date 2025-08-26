#include "Encoder.h"

void Encoder::copyI420ToAVFrame(const cv::Mat &i420, AVFrame *f, int W, int H)
{
    const uint8_t* pY = i420.data;
    const uint8_t* pU = pY + W*H;
    const uint8_t* pV = pU + (W/2)*(H/2);

    // Y
    for (int y = 0; y < H; ++y)
        memcpy(f->data[0] + y*f->linesize[0], pY + y*W, W);
    // U
    for (int y = 0; y < H/2; ++y)
        memcpy(f->data[1] + y*f->linesize[1], pU + y*(W/2), W/2);
    // V
    for (int y = 0; y < H/2; ++y)
        memcpy(f->data[2] + y*f->linesize[2], pV + y*(W/2), W/2);
}

Encoder::Encoder()
{
    if(!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!codec) {
        printf("Can't find codec.\n");
        return;
    }
    enc = avcodec_alloc_context3(codec);
    enc->width = FRAME_WIDTH;
    enc->height = FRAME_HEIGHT;
    enc->time_base = AVRational{1, FRAME_PER_SEC};
    enc->framerate = AVRational{FRAME_PER_SEC, 1};
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->gop_size = FRAME_PER_SEC * 2;
    enc->max_b_frames = 0;
    enc->bit_rate = 2000000;

    AVDictionary* enc_opts = nullptr;
    av_dict_set(&enc_opts, "preset", "veryfast", 0);
    av_dict_set(&enc_opts, "tune", "zerolatency", 0);
    av_dict_set(&enc_opts, "profile", "baseline", 0);
    av_dict_set(&enc_opts, "x264-params", "repeat-headers=1:scenecut=0:rc-lookahead=0", 0);

    if(int e = avcodec_open2(enc, codec, &enc_opts); e < 0) {
        printf("avcodec_open2 failed : %d\n", e);
        return;
    }

    av_dict_free(&enc_opts);

    frame->format = enc->pix_fmt;
    frame->width = enc->width;
    frame->height = enc->height;

    if(int e = av_frame_get_buffer(frame, 32); e < 0) {
        printf("av_frame_get_buffer failed : %d\n", e);
        return;
    }
}

Encoder::~Encoder()
{
    avcodec_send_frame(enc, nullptr);
    
    while (true) {
        AVPacket pkt; av_init_packet(&pkt);
        int r = avcodec_receive_packet(enc, &pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) { av_packet_unref(&pkt); break; }
        const bool key = (pkt.flags & AV_PKT_FLAG_KEY) != 0;
        // onEncodedH264(pkt.data, pkt.size, key);
        av_packet_unref(&pkt);
    }

    av_frame_free(&frame);
    avcodec_free_context(&enc);
}

Encoder& Encoder::instance() {
   static Encoder encoder;
   return encoder; 
}

AVFrame *Encoder::cvMat2AvFrame(cv::Mat rgb)
{
    cv::Mat yuv;

    cv::cvtColor(rgb, yuv, cv::COLOR_BGR2YUV_I420);

    if(av_frame_make_writable(frame) < 0) return;
    copyI420ToAVFrame(yuv, frame, FRAME_WIDTH, FRAME_HEIGHT);
    frame->pts = pts++;

    if(int e = avcodec_send_frame(enc, frame); e < 0) {
        printf("send_frame failed : %d\n", e);
        return;
    } 
    return nullptr;
}
