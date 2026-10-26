#include "CameraStream.hpp"

CameraStream::~CameraStream() {
    release();
}

bool CameraStream::open(int deviceIndex, int frameWidth, int frameHeight) {
    release();

#if defined(_WIN32)
    capture_.open(deviceIndex, cv::CAP_ANY);
    if (!capture_.isOpened()) {
        capture_.open(deviceIndex, cv::CAP_DSHOW);
    }
#else
    capture_.open(deviceIndex, cv::CAP_ANY);
#endif
    if (!capture_.isOpened()) {
        return false;
    }

    capture_.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(frameWidth));
    capture_.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(frameHeight));
    capture_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    capture_.set(cv::CAP_PROP_BUFFERSIZE, 1);

    return capture_.isOpened();
}

bool CameraStream::read(cv::Mat& frame) {
    if (!capture_.isOpened()) {
        return false;
    }
    if (!capture_.read(frame) || frame.empty()) {
        return false;
    }
    if (mirrored_) {
        cv::flip(frame, frame, 1);
    }
    return true;
}

void CameraStream::release() {
    if (capture_.isOpened()) {
        capture_.release();
    }
}

bool CameraStream::isOpened() const {
    return capture_.isOpened();
}
