#include "NvidiaAIClient.hpp"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <winhttp.h>
#endif

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

using json = nlohmann::json;

namespace {

std::string extractJsonObject(const std::string& text) {
    const std::size_t begin = text.find('{');
    if (begin == std::string::npos) {
        return "";
    }
    int depth = 0;
    bool inString = false;
    for (std::size_t i = begin; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\' && i + 1 < text.size()) {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(begin, i - begin + 1);
            }
        }
    }
    return "";
}

std::string wordsToSentence(const std::vector<std::string>& words) {
    if (words.empty()) {
        return "";
    }
    std::string sentence;
    for (std::size_t i = 0; i < words.size(); ++i) {
        sentence += words[i];
        if (i + 1 < words.size()) {
            sentence += ". ";
        }
    }
    if (!sentence.empty() && sentence.back() != '.') {
        sentence += ".";
    }
    return sentence;
}

void sanitizeTuning(AiTuningResult& r) {
    if (r.hMin > r.hMax) {
        std::swap(r.hMin, r.hMax);
    }
    if (r.sMin > r.sMax) {
        std::swap(r.sMin, r.sMax);
    }
    if (r.vMin > r.vMax) {
        std::swap(r.vMin, r.vMax);
    }
    r.hMin = std::clamp(r.hMin, 0, 179);
    r.hMax = std::clamp(r.hMax, 0, 179);
    r.sMin = std::clamp(r.sMin, 0, 255);
    r.sMax = std::clamp(r.sMax, 0, 255);
    r.vMin = std::clamp(r.vMin, 0, 255);
    r.vMax = std::clamp(r.vMax, 0, 255);
    if (r.hMin == r.hMax) {
        r.hMax = std::min(179, r.hMin + 1);
    }
    if (r.sMin == r.sMax) {
        r.sMax = std::min(255, r.sMin + 1);
    }
    if (r.vMin == r.vMax) {
        r.vMax = std::min(255, r.vMin + 1);
    }
    r.morphKernelSize = std::clamp(r.morphKernelSize, 1, 15);
    if ((r.morphKernelSize % 2) == 0) {
        r.morphKernelSize = std::min(15, r.morphKernelSize + 1);
    }
    r.blurSigma = std::clamp(r.blurSigma, 0, 5);
    r.defectAngleLimitDeg = std::clamp(r.defectAngleLimitDeg, 45.0, 140.0);
    r.defectDepthThresh = std::clamp(r.defectDepthThresh, 0.02, 0.5);
    if (r.lightingStatus.size() > 60) {
        r.lightingStatus = r.lightingStatus.substr(0, 60);
    }
}

}  // namespace

NvidiaAIClient::NvidiaAIClient() = default;

NvidiaAIClient::~NvidiaAIClient() {
    joinWorker();
}

void NvidiaAIClient::joinWorker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool NvidiaAIClient::configure(const std::string& apiKey, const std::string& model) {
    apiKey_ = apiKey;
    model_ = model.empty() ? "z-ai/glm-5.3" : model;
    configured_ = !apiKey_.empty() && apiKey_.rfind("PASTE_YOUR", 0) != 0;
    return configured_;
}

bool NvidiaAIClient::isConfigured() const {
    return configured_;
}

bool NvidiaAIClient::busy() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return tuningStatus_ == "working" || refineStatus_ == "working";
}

std::string NvidiaAIClient::tuningStatus() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return tuningStatus_;
}

std::string NvidiaAIClient::refineStatus() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return refineStatus_;
}

bool NvidiaAIClient::pollTuning(AiTuningResult& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!aiUpdatePending_) {
        return false;
    }
    out = pendingTuning_;
    aiUpdatePending_ = false;
    return true;
}

bool NvidiaAIClient::pollRefined(std::string& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!refinePending_) {
        return false;
    }
    out = pendingSentence_;
    refinePending_ = false;
    return true;
}

void NvidiaAIClient::requestTuningAsync(const SceneMetrics& metrics, const HsvBounds& current) {
    joinWorker();
    if (!metrics.valid) {
        tuningStatus_ = "failed: no scene metrics";
        return;
    }

    tuningStatus_ = "working";
    aiUpdatePending_ = false;

    const SceneMetrics m = metrics;
    const HsvBounds cur = current;
    const std::string key = apiKey_;
    const std::string model = model_;
    const bool configured = configured_;

    worker_ = std::thread([this, m, cur, key, model, configured]() {
        try {
        AiTuningResult result;
        result.valid = true;

        if (!configured) {
            result.fromNetwork = false;
            result.hMin = std::clamp(static_cast<int>(m.palmHsvMean[0] - 18), 0, 179);
            result.hMax = std::clamp(static_cast<int>(m.palmHsvMean[0] + 18), 0, 179);
            result.sMin = std::clamp(static_cast<int>(m.palmHsvMean[1] - 45), 0, 255);
            result.sMax = std::clamp(static_cast<int>(m.palmHsvMean[1] + 70), 0, 255);
            result.vMin = std::clamp(static_cast<int>(m.palmHsvMean[2] - 70), 0, 255);
            result.vMax = std::clamp(static_cast<int>(m.palmHsvMean[2] + 70), 0, 255);
            result.morphKernelSize = 3;
            result.blurSigma = 1;
            result.defectAngleLimitDeg = 90.0;
            result.defectDepthThresh = 0.10;
            if (m.ambientBrightness < 60.0) {
                result.lightingStatus = "Offline tune: low-light room compensated";
            } else if (m.ambientBrightness > 190.0) {
                result.lightingStatus = "Offline tune: bright glare compensated";
            } else if (m.contrastRatio < 0.15) {
                result.lightingStatus = "Offline tune: low hand/background contrast";
            } else {
                result.lightingStatus = "Offline tune: local HSV auto-range applied";
            }
            sanitizeTuning(result);
            {
                std::lock_guard<std::mutex> lock(mtx_);
                pendingTuning_ = result;
                aiUpdatePending_ = true;
                tuningStatus_ = "offline";
            }
            return;
        }

#ifdef _WIN32
        json promptMetrics = {
            {"palm_hsv_mean", {m.palmHsvMean[0], m.palmHsvMean[1], m.palmHsvMean[2]}},
            {"palm_hsv_std", {m.palmHsvStd[0], m.palmHsvStd[1], m.palmHsvStd[2]}},
            {"background_hsv_mean", {m.bgHsvMean[0], m.bgHsvMean[1], m.bgHsvMean[2]}},
            {"background_hsv_std", {m.bgHsvStd[0], m.bgHsvStd[1], m.bgHsvStd[2]}},
            {"ambient_brightness_v", m.ambientBrightness},
            {"contrast_ratio", m.contrastRatio},
            {"mask_coverage", m.maskCoverage},
            {"mask_components", m.maskComponents},
            {"current_hsv_bounds",
             {{"h_min", cur.hMin}, {"h_max", cur.hMax}, {"s_min", cur.sMin},
              {"s_max", cur.sMax}, {"v_min", cur.vMin}, {"v_max", cur.vMax}}}};

        json systemMsg = {
            "role", "system",
            "content",
            "You are a Computer Vision Calibration Expert for skin-color HSV segmentation of a "
            "hand inside an ROI. You receive scene telemetry (palm vs background HSV mean/std, "
            "ambient brightness 0-255, contrast ratio 0-1, binary mask coverage 0-1, connected "
            "components count). OpenCV hue range is 0-179. Respond with ONLY a strict JSON "
            "object, no markdown, with exactly these keys: {\"h_min\": int, \"h_max\": int, "
            "\"s_min\": int, \"s_max\": int, \"v_min\": int, \"v_max\": int, "
            "\"morph_kernel_size\": int, \"blur_sigma\": int, \"defect_angle_limit_deg\": float, "
            "\"defect_depth_thresh\": float, \"lighting_status\": \"string\"}. The bounds must "
            "isolate the user's hand from the room background. morph_kernel_size is 1-15 odd, "
            "blur_sigma 0-5, defect_angle_limit_deg 45-140, defect_depth_thresh 0.02-0.5. "
            "lighting_status is a short diagnosis (max 8 words), e.g. 'Low-light room "
            "compensated'."};

        json userMsg = {{"role", "user"},
                        {"content", "Scene telemetry JSON:\n" + promptMetrics.dump()}};

        json body = {{"model", model},
                     {"messages", json::array({systemMsg, userMsg})},
                     {"temperature", 0.2},
                     {"max_tokens", 400}};

        std::string responseText;
        std::string error;

        HINTERNET session = WinHttpOpen(L"NvidiaSignTranslator/1.0",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session) {
            WinHttpSetTimeouts(session, 5000, 5000, 10000, 20000);
            HINTERNET connect = WinHttpConnect(session, L"integrate.api.nvidia.com",
                                               INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (connect) {
                HINTERNET request = WinHttpOpenRequest(
                    connect, L"POST", L"/v1/chat/completions", nullptr, WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (request) {
                    std::wstring headers =
                        L"Content-Type: application/json\r\nAuthorization: Bearer " +
                        std::wstring(key.begin(), key.end()) + L"\r\n";
                    const std::string payload = body.dump();
                    if (WinHttpSendRequest(request, headers.c_str(),
                                           static_cast<DWORD>(headers.size()),
                                           const_cast<char*>(payload.data()),
                                           static_cast<DWORD>(payload.size()),
                                           static_cast<DWORD>(payload.size()), 0)) {
                        if (WinHttpReceiveResponse(request, nullptr)) {
                            DWORD status = 0;
                            DWORD statusSize = sizeof(status);
                            WinHttpQueryHeaders(request,
                                                WINHTTP_QUERY_STATUS_CODE |
                                                    WINHTTP_QUERY_FLAG_NUMBER,
                                                WINHTTP_HEADER_NAME_BY_INDEX, &status,
                                                &statusSize, WINHTTP_NO_HEADER_INDEX);
                            std::string data;
                            DWORD bytesRead = 0;
                            char buffer[4096];
                            while (WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) &&
                                   bytesRead > 0) {
                                data.append(buffer, bytesRead);
                            }
                            if (status == 200) {
                                responseText = data;
                            } else {
                                error = "HTTP " + std::to_string(status) + ": " +
                                        data.substr(0, 300);
                            }
                        } else {
                            error = "receive response failed";
                        }
                    } else {
                        error = "send request failed (offline?)";
                    }
                    WinHttpCloseHandle(request);
                } else {
                    error = "open request failed";
                }
                WinHttpCloseHandle(connect);
            } else {
                error = "connect failed (offline?)";
            }
            WinHttpCloseHandle(session);
        } else {
            error = "winhttp init failed";
        }

        if (!responseText.empty()) {
            try {
                json response = json::parse(responseText);
                const std::string content =
                    response.at("choices").at(0).at("message").at("content").get<std::string>();
                const std::string objText = extractJsonObject(content);
                if (!objText.empty()) {
                    json obj = json::parse(objText);
                    result.hMin = static_cast<int>(obj.value("h_min", 0.0));
                    result.hMax = static_cast<int>(obj.value("h_max", 179.0));
                    result.sMin = static_cast<int>(obj.value("s_min", 30.0));
                    result.sMax = static_cast<int>(obj.value("s_max", 150.0));
                    result.vMin = static_cast<int>(obj.value("v_min", 60.0));
                    result.vMax = static_cast<int>(obj.value("v_max", 255.0));
                    result.morphKernelSize =
                        static_cast<int>(obj.value("morph_kernel_size", 3.0));
                    result.blurSigma = static_cast<int>(obj.value("blur_sigma", 1.0));
                    result.defectAngleLimitDeg = obj.value("defect_angle_limit_deg", 90.0);
                    result.defectDepthThresh = obj.value("defect_depth_thresh", 0.10);
                    result.lightingStatus =
                        obj.value("lighting_status", std::string("AI tune applied"));
                    result.fromNetwork = true;
                    sanitizeTuning(result);
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        pendingTuning_ = result;
                        aiUpdatePending_ = true;
                        tuningStatus_ = "ok";
                    }
                    return;
                }
                error = "no JSON object in model reply";
            } catch (const std::exception& ex) {
                error = std::string("parse: ") + ex.what();
            }
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            tuningStatus_ = "failed: " + error;
        }
#else
        {
            std::lock_guard<std::mutex> lock(mtx_);
            tuningStatus_ = "failed: unsupported platform";
        }
#endif
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(mtx_);
            tuningStatus_ = std::string("failed: ") + ex.what();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mtx_);
            tuningStatus_ = "failed: unknown exception";
        }
    });
}

void NvidiaAIClient::requestRefineAsync(const std::vector<std::string>& words) {
    joinWorker();
    if (words.empty()) {
        refineStatus_ = "failed: sentence buffer empty";
        return;
    }

    refineStatus_ = "working";
    refinePending_ = false;

    const std::string key = apiKey_;
    const std::string model = model_;
    const bool configured = configured_;
    const std::string raw = wordsToSentence(words);

    worker_ = std::thread([this, key, model, configured, raw]() {
        try {
        if (!configured) {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                pendingSentence_ = raw;
                refinePending_ = true;
                refineStatus_ = "offline";
            }
            return;
        }

#ifdef _WIN32
        json systemMsg = {
            "role", "system",
            "content",
            "You are an assistive communication assistant. The user is a deaf-mute person who "
            "signed a sequence of short phrases. Merge them into ONE natural, grammatical, "
            "polite sentence in English. Reply with ONLY the sentence, nothing else."};

        json userMsg = {{"role", "user"}, {"content", "Signed phrases: " + raw}};

        json body = {{"model", model},
                     {"messages", json::array({systemMsg, userMsg})},
                     {"temperature", 0.3},
                     {"max_tokens", 150}};

        std::string responseText;
        std::string error;

        HINTERNET session = WinHttpOpen(L"NvidiaSignTranslator/1.0",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session) {
            WinHttpSetTimeouts(session, 5000, 5000, 10000, 20000);
            HINTERNET connect = WinHttpConnect(session, L"integrate.api.nvidia.com",
                                               INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (connect) {
                HINTERNET request = WinHttpOpenRequest(
                    connect, L"POST", L"/v1/chat/completions", nullptr, WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (request) {
                    std::wstring headers =
                        L"Content-Type: application/json\r\nAuthorization: Bearer " +
                        std::wstring(key.begin(), key.end()) + L"\r\n";
                    const std::string payload = body.dump();
                    if (WinHttpSendRequest(request, headers.c_str(),
                                           static_cast<DWORD>(headers.size()),
                                           const_cast<char*>(payload.data()),
                                           static_cast<DWORD>(payload.size()),
                                           static_cast<DWORD>(payload.size()), 0)) {
                        if (WinHttpReceiveResponse(request, nullptr)) {
                            DWORD status = 0;
                            DWORD statusSize = sizeof(status);
                            WinHttpQueryHeaders(request,
                                                WINHTTP_QUERY_STATUS_CODE |
                                                    WINHTTP_QUERY_FLAG_NUMBER,
                                                WINHTTP_HEADER_NAME_BY_INDEX, &status,
                                                &statusSize, WINHTTP_NO_HEADER_INDEX);
                            std::string data;
                            DWORD bytesRead = 0;
                            char buffer[4096];
                            while (WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) &&
                                   bytesRead > 0) {
                                data.append(buffer, bytesRead);
                            }
                            if (status == 200) {
                                responseText = data;
                            } else {
                                error = "HTTP " + std::to_string(status);
                            }
                        } else {
                            error = "receive failed";
                        }
                    } else {
                        error = "send failed (offline?)";
                    }
                    WinHttpCloseHandle(request);
                } else {
                    error = "open request failed";
                }
                WinHttpCloseHandle(connect);
            } else {
                error = "connect failed (offline?)";
            }
            WinHttpCloseHandle(session);
        } else {
            error = "winhttp init failed";
        }

        if (!responseText.empty()) {
            try {
                json response = json::parse(responseText);
                std::string content =
                    response.at("choices").at(0).at("message").at("content").get<std::string>();
                content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());
                if (!content.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(mtx_);
                        pendingSentence_ = content;
                        refinePending_ = true;
                        refineStatus_ = "ok";
                    }
                    return;
                }
                error = "empty reply";
            } catch (const std::exception& ex) {
                error = std::string("parse: ") + ex.what();
            }
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            pendingSentence_ = raw;
            refinePending_ = true;
            refineStatus_ = "offline: " + error;
        }
#else
        {
            std::lock_guard<std::mutex> lock(mtx_);
            pendingSentence_ = raw;
            refinePending_ = true;
            refineStatus_ = "offline";
        }
#endif
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(mtx_);
            refineStatus_ = std::string("failed: ") + ex.what();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mtx_);
            refineStatus_ = "failed: unknown exception";
        }
    });
}
