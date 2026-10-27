#include "HandProcessor.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

const char* const kCalibrationWindow = "HSV Calibration";
constexpr int kPalmSampleSize = 40;
constexpr int kBackgroundBandPx = 25;

cv::Mat ellipseKernel(int size) {
    const int s = std::max(1, size | 1);
    return cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(s, s));
}

}

void HandProcessor::createCalibrationWindow() {
    if (calibrationWindowCreated_) {
        return;
    }
    cv::namedWindow(kCalibrationWindow, cv::WINDOW_AUTOSIZE);
    cv::createTrackbar("H_min", kCalibrationWindow, &hMin_, 179);
    cv::createTrackbar("H_max", kCalibrationWindow, &hMax_, 179);
    cv::createTrackbar("S_min", kCalibrationWindow, &sMin_, 255);
    cv::createTrackbar("S_max", kCalibrationWindow, &sMax_, 255);
    cv::createTrackbar("V_min", kCalibrationWindow, &vMin_, 255);
    cv::createTrackbar("V_max", kCalibrationWindow, &vMax_, 255);
    calibrationWindowCreated_ = true;
}

void HandProcessor::destroyCalibrationWindow() {
    if (calibrationWindowCreated_) {
        cv::destroyWindow(kCalibrationWindow);
        calibrationWindowCreated_ = false;
    }
}

bool HandProcessor::calibrationWindowActive() const {
    return calibrationWindowCreated_;
}

cv::Rect HandProcessor::roiRect(const cv::Mat& frame) const {
    const int w = std::min(static_cast<int>(frame.cols * roiWidthRatio_), frame.cols);
    const int h = std::min(static_cast<int>(frame.rows * roiHeightRatio_), frame.rows);
    const int x = std::clamp((frame.cols - w) / 2, 0, frame.cols - w);
    const int y = std::clamp(frame.rows - h - static_cast<int>(frame.rows * roiBottomMarginRatio_),
                             0, frame.rows - h);
    return cv::Rect(x, y, w, h);
}

cv::Rect HandProcessor::palmSampleRect(const cv::Mat& frame) const {
    const cv::Rect roi = roiRect(frame);
    const int size = std::min(kPalmSampleSize, std::min(roi.width, roi.height));
    const int x = roi.x + (roi.width - size) / 2;
    const int y = roi.y + (roi.height - size) / 2;
    return cv::Rect(x, y, size, size);
}

void HandProcessor::buildMask(const cv::Mat& bgrRoi, cv::Mat& mask) {
    cv::Mat blurred;
    cv::GaussianBlur(bgrRoi, blurred, cv::Size(7, 7), blurSigma_, blurSigma_);

    cv::Mat hsv;
    cv::cvtColor(blurred, hsv, cv::COLOR_BGR2HSV);

    cv::inRange(hsv, cv::Scalar(hMin_, sMin_, vMin_), cv::Scalar(hMax_, sMax_, vMax_), mask);

    const cv::Mat kernel = ellipseKernel(morphKernelSize_);
    cv::erode(mask, mask, kernel, cv::Point(-1, -1), 2);
    cv::dilate(mask, mask, kernel, cv::Point(-1, -1), 2);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, ellipseKernel(morphKernelSize_ + 2));
    cv::medianBlur(mask, mask, 5);
}

HsvBounds HandProcessor::hsvBounds() const {
    return {hMin_, hMax_, sMin_, sMax_, vMin_, vMax_};
}

void HandProcessor::setHsvBounds(const HsvBounds& bounds) {
    hMin_ = std::clamp(bounds.hMin, 0, 179);
    hMax_ = std::clamp(bounds.hMax, 0, 179);
    sMin_ = std::clamp(bounds.sMin, 0, 255);
    sMax_ = std::clamp(bounds.sMax, 0, 255);
    vMin_ = std::clamp(bounds.vMin, 0, 255);
    vMax_ = std::clamp(bounds.vMax, 0, 255);
    syncTrackbars();
}

void HandProcessor::syncTrackbars() {
    if (!calibrationWindowCreated_) {
        return;
    }
    cv::setTrackbarPos("H_min", kCalibrationWindow, hMin_);
    cv::setTrackbarPos("H_max", kCalibrationWindow, hMax_);
    cv::setTrackbarPos("S_min", kCalibrationWindow, sMin_);
    cv::setTrackbarPos("S_max", kCalibrationWindow, sMax_);
    cv::setTrackbarPos("V_min", kCalibrationWindow, vMin_);
    cv::setTrackbarPos("V_max", kCalibrationWindow, vMax_);
}

void HandProcessor::setMorphKernelSize(int size) {
    morphKernelSize_ = std::clamp(size, 1, 15);
}

void HandProcessor::setBlurSigma(double sigma) {
    blurSigma_ = std::clamp(sigma, 0.0, 6.0);
}

void HandProcessor::setDefectAngleLimitDeg(double deg) {
    maxFingerAngleDeg_ = std::clamp(deg, 45.0, 140.0);
}

void HandProcessor::setDefectDepthThreshold(double ratio) {
    minDefectDepthRatio_ = std::clamp(ratio, 0.02, 0.5);
}

int HandProcessor::morphKernelSize() const {
    return morphKernelSize_;
}

double HandProcessor::blurSigma() const {
    return blurSigma_;
}

double HandProcessor::defectAngleLimitDeg() const {
    return maxFingerAngleDeg_;
}

double HandProcessor::defectDepthThreshold() const {
    return minDefectDepthRatio_;
}

bool HandProcessor::calibrateLocal(const cv::Mat& bgrFrame) {
    if (bgrFrame.empty()) {
        return false;
    }

    const cv::Rect roi = roiRect(bgrFrame);
    const cv::Rect palm = palmSampleRect(bgrFrame);

    cv::Mat roiHsv;
    cv::cvtColor(bgrFrame(roi), roiHsv, cv::COLOR_BGR2HSV);
    const cv::Rect palmInRoi(palm.x - roi.x, palm.y - roi.y, palm.width, palm.height);

    cv::Scalar palmMean, palmStd;
    cv::meanStdDev(roiHsv(palmInRoi), palmMean, palmStd);

    cv::Scalar bgMean, bgStd;
    cv::Mat bgMask = cv::Mat::ones(roiHsv.size(), CV_8UC1) * 255;
    bgMask(palmInRoi).setTo(0);
    cv::meanStdDev(roiHsv, bgMean, bgStd, bgMask);

    const double hTol = std::max(12.0, 2.2 * palmStd[0]);
    const double sTol = std::max(35.0, 2.2 * palmStd[1]);
    const double vTol = std::max(45.0, 2.2 * palmStd[2]);

    HsvBounds bounds;
    bounds.hMin = std::clamp(static_cast<int>(palmMean[0] - hTol), 0, 179);
    bounds.hMax = std::clamp(static_cast<int>(palmMean[0] + hTol), 0, 179);
    bounds.sMin = std::clamp(static_cast<int>(palmMean[1] - sTol), 0, 255);
    bounds.sMax = std::clamp(static_cast<int>(palmMean[1] + sTol + 0.35 * bgMean[1]), 0, 255);
    bounds.vMin = std::clamp(static_cast<int>(palmMean[2] - vTol), 0, 255);
    bounds.vMax = std::clamp(static_cast<int>(palmMean[2] + vTol + 0.35 * bgMean[2]), 0, 255);

    setHsvBounds(bounds);
    return true;
}

SceneMetrics HandProcessor::computeSceneMetrics(const cv::Mat& bgrFrame,
                                                const cv::Mat& mask) const {
    SceneMetrics metrics;
    if (bgrFrame.empty()) {
        return metrics;
    }

    const cv::Rect roi = roiRect(bgrFrame);
    const cv::Rect palm = palmSampleRect(bgrFrame);

    cv::Mat hsv;
    cv::cvtColor(bgrFrame, hsv, cv::COLOR_BGR2HSV);

    cv::meanStdDev(hsv(palm), metrics.palmHsvMean, metrics.palmHsvStd);

    const int band = kBackgroundBandPx;
    const int bx = std::max(0, roi.x - band);
    const int by = std::max(0, roi.y - band);
    const int bw = std::min(bgrFrame.cols - bx, roi.width + 2 * band);
    const int bh = std::min(bgrFrame.rows - by, roi.height + 2 * band);
    const cv::Rect expanded(bx, by, bw, bh);

    cv::Mat bgMask = cv::Mat::ones(expanded.size(), CV_8UC1) * 255;
    const cv::Rect roiInExpanded(roi.x - expanded.x, roi.y - expanded.y, roi.width, roi.height);
    bgMask(roiInExpanded).setTo(0);
    bgMask(palm - expanded.tl()).setTo(0);

    cv::meanStdDev(hsv(expanded), metrics.bgHsvMean, metrics.bgHsvStd, bgMask);

    const cv::Scalar frameMean = cv::mean(hsv);
    metrics.ambientBrightness = frameMean[2];

    const double palmV = metrics.palmHsvMean[2];
    const double bgV = metrics.bgHsvMean[2];
    metrics.contrastRatio = std::abs(palmV - bgV) / 255.0;

    if (!mask.empty()) {
        metrics.maskCoverage =
            static_cast<double>(cv::countNonZero(mask)) / static_cast<double>(mask.total());
        cv::Mat labels;
        const int labelCount = cv::connectedComponents(mask, labels, 8, CV_32S);
        metrics.maskComponents = std::max(0, labelCount - 1);
    }

    metrics.valid = true;
    return metrics;
}

HandAnalysisResult HandProcessor::process(const cv::Mat& bgrFrame, cv::Mat& mask,
                                          cv::Mat& roiDebugImage) {
    HandAnalysisResult result;
    mask.release();
    roiDebugImage.release();
    if (bgrFrame.empty()) {
        return result;
    }

    const cv::Rect roi = roiRect(bgrFrame);
    cv::Mat bgrRoi = bgrFrame(roi).clone();
    buildMask(bgrRoi, mask);
    roiDebugImage = bgrRoi;

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int largestIndex = -1;
    double largestArea = 0.0;
    for (std::size_t i = 0; i < contours.size(); ++i) {
        const double area = cv::contourArea(contours[i]);
        if (area > largestArea) {
            largestArea = area;
            largestIndex = static_cast<int>(i);
        }
    }

    const double minArea = minContourAreaRatio_ * static_cast<double>(roi.area());
    if (largestIndex < 0 || largestArea < minArea) {
        return result;
    }

    const std::vector<cv::Point>& hand = contours[static_cast<std::size_t>(largestIndex)];
    result.handDetected = true;
    result.contourArea = largestArea;

    const cv::Rect handBox = cv::boundingRect(hand);
    result.boundingBox = handBox + roi.tl();
    result.aspectRatio = static_cast<double>(handBox.height) /
                         std::max(1.0, static_cast<double>(handBox.width));

    std::vector<cv::Point> hullPoints;
    std::vector<int> hullIndices;
    if (hand.size() >= 5) {
        cv::convexHull(hand, hullIndices, false, false);
        hullPoints.reserve(hullIndices.size());
        for (int idx : hullIndices) {
            hullPoints.push_back(hand[static_cast<std::size_t>(idx)]);
        }
        if (hullPoints.size() >= 3) {
            const double hullArea = std::max(1.0, cv::contourArea(hullPoints));
            result.solidity = std::clamp(largestArea / hullArea, 0.0, 1.0);
        }
        if (hullPoints.size() >= 5) {
            const cv::RotatedRect ellipse = cv::fitEllipse(hullPoints);
            double angle = ellipse.angle;
            if (ellipse.size.width < ellipse.size.height) {
                angle += 90.0;
            }
            angle = std::fmod(angle + 360.0, 180.0);
            result.orientationDeg = angle;
        }
    }

    cv::drawContours(roiDebugImage, contours, largestIndex, cv::Scalar(0, 255, 0), 2);
    cv::rectangle(roiDebugImage, handBox, cv::Scalar(200, 200, 0), 1);

    int fingers = 0;
    int valleys = 0;
    if (hullIndices.size() >= 3) {
        cv::polylines(roiDebugImage, std::vector<std::vector<cv::Point>>{hullPoints}, true,
                      cv::Scalar(255, 200, 0), 2);

        std::vector<cv::Vec4i> defects;
        cv::convexityDefects(hand, hullIndices, defects);

        const double depthThreshold =
            minDefectDepthRatio_ * static_cast<double>(handBox.height);
        for (const cv::Vec4i& defect : defects) {
            const double depth = static_cast<double>(defect[3]) / 256.0;
            if (depth < depthThreshold) {
                continue;
            }
            const cv::Point& start = hand[static_cast<std::size_t>(defect[0])];
            const cv::Point& end = hand[static_cast<std::size_t>(defect[1])];
            const cv::Point& far = hand[static_cast<std::size_t>(defect[2])];

            const double a = cv::norm(end - far);
            const double b = cv::norm(start - far);
            const double c = cv::norm(end - start);
            if (a < 1e-6 || b < 1e-6) {
                continue;
            }
            double cosVal = (a * a + b * b - c * c) / (2.0 * a * b);
            cosVal = std::clamp(cosVal, -1.0, 1.0);
            const double angleDeg = std::acos(cosVal) * 180.0 / CV_PI;
            if (angleDeg >= maxFingerAngleDeg_) {
                continue;
            }

            ++valleys;
            cv::circle(roiDebugImage, far, 5, cv::Scalar(0, 0, 255), -1);
            cv::line(roiDebugImage, start, far, cv::Scalar(0, 140, 255), 1);
            cv::line(roiDebugImage, end, far, cv::Scalar(0, 140, 255), 1);
            cv::circle(roiDebugImage, start, 4, cv::Scalar(255, 0, 255), 1);
            cv::circle(roiDebugImage, end, 4, cv::Scalar(255, 0, 255), 1);
        }

        if (valleys == 0) {
            const double aspect = result.aspectRatio;
            fingers = (aspect > singleFingerAspectRatio_) ? 1 : 0;
        } else {
            fingers = std::min(valleys + 1, 5);
        }
    }

    result.fingerCount = fingers;
    result.defectCount = valleys;

    const cv::Rect palm = palmSampleRect(bgrFrame);
    cv::rectangle(roiDebugImage, palm - roi.tl(), cv::Scalar(0, 165, 255), 1);

    cv::putText(roiDebugImage, "Fingers: " + std::to_string(fingers), cv::Point(8, 28),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    return result;
}
