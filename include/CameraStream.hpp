#pragma once

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

class CameraStream {
public:
    CameraStream() = default;
    ~CameraStream();

    CameraStream(const CameraStream&) = delete;
    CameraStream& operator=(const CameraStream&) = delete;

    bool open(int deviceIndex, int frameWidth = 1280, int frameHeight = 720);
    bool read(cv::Mat& frame);
    void release();
    bool isOpened() const;

private:
    cv::VideoCapture capture_;
    bool mirrored_{true};
};
