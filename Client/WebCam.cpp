#include "WebCam.h"

WebCam::WebCam()
{
    cap = cv::VideoCapture(0);
    if(!cap.isOpened()) {
        printf("Can't open Web Camera.\n");
        return;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, FRAME_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT);
    cap.set(cv::CAP_PROP_FPS, FRAME_PER_SEC);

    printf("WebCam is On - %d * %d (%d FPS)\n", FRAME_WIDTH, FRAME_HEIGHT, FRAME_PER_SEC);
}

WebCam::~WebCam()
{
    cap.release();
    cv::destroyAllWindows();
    printf("WebCam is Off\n");
}

WebCam& WebCam::instance() {
    static WebCam webcam;
    return webcam;
}

cv::Mat WebCam::getFrame()
{
    cv::Mat frame;
    if(!cap.isOpened() || !cap.read(frame)) {
        printf("Camera isn't available.\n");
        return cv::Mat();
    }

    return frame;
}
