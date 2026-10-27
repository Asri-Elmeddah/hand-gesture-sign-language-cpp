#pragma once

#include "HandProcessor.hpp"

#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AiTuningResult {
    bool valid{false};
    int hMin{0};
    int hMax{179};
    int sMin{30};
    int sMax{150};
    int vMin{60};
    int vMax{255};
    int morphKernelSize{3};
    int blurSigma{1};
    double defectAngleLimitDeg{90.0};
    double defectDepthThresh{0.10};
    std::string lightingStatus;
    bool fromNetwork{false};
};

class NvidiaAIClient {
public:
    NvidiaAIClient();
    ~NvidiaAIClient();

    NvidiaAIClient(const NvidiaAIClient&) = delete;
    NvidiaAIClient& operator=(const NvidiaAIClient&) = delete;

    bool configure(const std::string& apiKey, const std::string& model);
    bool isConfigured() const;

    void requestTuningAsync(const SceneMetrics& metrics, const HsvBounds& current);
    void requestRefineAsync(const std::vector<std::string>& words);

    bool pollTuning(AiTuningResult& out);
    bool pollRefined(std::string& out);

    std::string tuningStatus() const;
    std::string refineStatus() const;
    bool busy() const;

private:
    void joinWorker();

    std::string apiKey_;
    std::string model_;
    bool configured_{false};

    std::thread worker_;
    std::string tuningStatus_{"idle"};
    std::string refineStatus_{"idle"};
    AiTuningResult pendingTuning_;
    bool aiUpdatePending_{false};
    std::string pendingSentence_;
    bool refinePending_{false};
    mutable std::mutex mtx_;
};
