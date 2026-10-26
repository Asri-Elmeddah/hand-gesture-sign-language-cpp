#pragma once

#include <opencv2/core.hpp>

#include <string>

struct HsvBounds {
    int hMin{0};
    int hMax{20};
    int sMin{30};
    int sMax{150};
    int vMin{60};
    int vMax{255};
};

struct HandAnalysisResult {
    bool handDetected{false};
    int fingerCount{0};
    int defectCount{0};
    double orientationDeg{0.0};
    double solidity{0.0};
    double aspectRatio{0.0};
    double contourArea{0.0};
    cv::Rect boundingBox;
};

struct SceneMetrics {
    bool valid{false};
    cv::Scalar palmHsvMean;
    cv::Scalar palmHsvStd;
    cv::Scalar bgHsvMean;
    cv::Scalar bgHsvStd;
    double ambientBrightness{0.0};
    double contrastRatio{0.0};
    double maskCoverage{0.0};
    int maskComponents{0};
    std::string roiSnapshotBase64;
};

class HandProcessor {
public:
    void createCalibrationWindow();
    void destroyCalibrationWindow();
    bool calibrationWindowActive() const;

    HandAnalysisResult process(const cv::Mat& bgrFrame, cv::Mat& mask, cv::Mat& roiDebugImage);

    cv::Rect roiRect(const cv::Mat& frame) const;

    HsvBounds hsvBounds() const;
    void setHsvBounds(const HsvBounds& bounds);
    void syncTrackbars();

    bool calibrateLocal(const cv::Mat& bgrFrame);

    SceneMetrics computeSceneMetrics(const cv::Mat& bgrFrame, const cv::Mat& mask) const;

    void setMorphKernelSize(int size);
    void setBlurSigma(double sigma);
    void setDefectAngleLimitDeg(double deg);
    void setDefectDepthThreshold(double ratio);

    int morphKernelSize() const;
    double blurSigma() const;
    double defectAngleLimitDeg() const;
    double defectDepthThreshold() const;

private:
    void buildMask(const cv::Mat& bgrRoi, cv::Mat& mask);
    cv::Rect palmSampleRect(const cv::Mat& frame) const;

    int hMin_{0};
    int hMax_{20};
    int sMin_{30};
    int sMax_{150};
    int vMin_{60};
    int vMax_{255};

    double roiWidthRatio_{0.45};
    double roiHeightRatio_{0.60};
    double roiBottomMarginRatio_{0.05};

    double minContourAreaRatio_{0.02};
    double minDefectDepthRatio_{0.10};
    double maxFingerAngleDeg_{90.0};
    double singleFingerAspectRatio_{1.6};

    int morphKernelSize_{3};
    double blurSigma_{1.0};

    bool calibrationWindowCreated_{false};
};
