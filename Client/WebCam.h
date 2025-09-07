#pragma once
#include <stdio.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080
#define FRAME_PER_SEC 30

class WebCam {
private:
    cv::VideoCapture cap;

    WebCam();
public:
    ~WebCam();
    static WebCam& instance();

    cv::Mat getFrame();
};