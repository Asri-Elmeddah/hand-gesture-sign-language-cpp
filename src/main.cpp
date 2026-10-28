#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include "CameraStream.hpp"
#include "ControlLogic.hpp"
#include "EnvLoader.hpp"
#include "HandProcessor.hpp"
#include "NvidiaAIClient.hpp"
#include "SignLanguageDB.hpp"
#include "SpeechEngine.hpp"
#include "TelemetryLogger.hpp"
#include "TranscriptWriter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

const char* const kMainWindow = "Sign Language Translator  [A]=AI Tune [C]=Calibrate [G]=Refine [U]=Update [Q]=Quit";
const char* const kMaskWindow = "HSV Mask";
const char* const kRoiWindow = "Hand Processing";
constexpr int kDwellFrames = 15;
constexpr int kAutoAssistFrames = 90;

struct MouseContext {
    cv::Rect aiButton;
    cv::Rect updateButton;
    std::atomic<bool> aiClicked{false};
    std::atomic<bool> updateClicked{false};
};

void onMouse(int event, int x, int y, int, void* userdata) {
    MouseContext* ctx = static_cast<MouseContext*>(userdata);
    if (!ctx || event != cv::EVENT_LBUTTONDOWN) {
        return;
    }
    if (ctx->aiButton.contains(cv::Point(x, y))) {
        ctx->aiClicked = true;
    }
    if (ctx->updateButton.contains(cv::Point(x, y))) {
        ctx->updateClicked = true;
    }
}

void drawLabel(cv::Mat& image, const std::string& text, const cv::Point& origin,
               const cv::Scalar& color, double scale, int thickness) {
    int baseline = 0;
    const cv::Size box =
        cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, thickness, &baseline);
    cv::rectangle(image, origin + cv::Point(0, baseline),
                  origin + cv::Point(box.width, -box.height), cv::Scalar(30, 30, 30),
                  cv::FILLED);
    cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness,
                cv::LINE_AA);
}

void drawButton(cv::Mat& image, const cv::Rect& rect, const std::string& label,
                const cv::Scalar& color) {
    cv::rectangle(image, rect, color, cv::FILLED);
    cv::rectangle(image, rect, cv::Scalar(255, 255, 255), 1);
    int baseline = 0;
    const cv::Size textSize =
        cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.45, 1, &baseline);
    cv::putText(image, label, cv::Point(rect.x + (rect.width - textSize.width) / 2,
                                        rect.y + (rect.height + textSize.height) / 2),
                cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(20, 20, 20), 1, cv::LINE_AA);
}

void drawProgressBar(cv::Mat& image, const cv::Rect& roi, double pct) {
    const cv::Rect bar(roi.x, roi.y + roi.height + 8, roi.width, 14);
    cv::rectangle(image, bar, cv::Scalar(60, 60, 60), cv::FILLED);
    cv::rectangle(image, bar, cv::Scalar(255, 255, 255), 1);
    const int fillW = static_cast<int>(bar.width * std::clamp(pct, 0.0, 100.0) / 100.0);
    const double t = std::clamp(pct, 0.0, 100.0) / 100.0;
    cv::Scalar color(40 + static_cast<int>(t * 0.0), 60 + static_cast<int>(t * 195.0),
                     40 + static_cast<int>((1.0 - t) * 175.0));
    if (fillW > 0) {
        cv::rectangle(image, cv::Rect(bar.x, bar.y, fillW, bar.height), color, cv::FILLED);
    }
    drawLabel(image, cv::format("Stability %d%%", static_cast<int>(pct)),
              cv::Point(bar.x + 6, bar.y + bar.height - 3), cv::Scalar(255, 255, 255), 0.42, 1);
}

std::string wordsLine(const std::vector<std::string>& words, std::size_t maxWords) {
    std::string line;
    const std::size_t begin = words.size() > maxWords ? words.size() - maxWords : 0;
    for (std::size_t i = begin; i < words.size(); ++i) {
        line += words[i];
        if (i + 1 < words.size()) {
            line += " + ";
        }
    }
    return line;
}

#ifdef _WIN32
std::filesystem::path moduleDirectory() {
    char buffer[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}
#endif

std::string base64Encode(const std::vector<uchar>& bytes) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = bytes[i];
        const unsigned b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const unsigned b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        out += table[(b0 >> 2) & 0x3F];
        out += table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
        out += (i + 1 < bytes.size()) ? table[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=';
        out += (i + 2 < bytes.size()) ? table[b2 & 0x3F] : '=';
    }
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    int cameraIndex = 0;
    if (argc > 1) {
        cameraIndex = std::atoi(argv[1]);
    }

#ifdef _WIN32
    const std::filesystem::path rootDir = moduleDirectory().parent_path().parent_path();
#else
    const std::filesystem::path rootDir = std::filesystem::current_path();
#endif

    EnvLoader env((rootDir / ".env").string());
    env.load();

    std::cout << "=== AI Sign Language Translator & Automatic Control Telemetry ===\n"
              << "Keys: A=AI Tune | C=Local Calibrate | G=Refine Sentence | L=CSV Log | "
                 "U=Update | T=Teach Sign | S=Mute TTS | N=New Sentence | Q=Quit\n"
              << std::endl;

    CameraStream camera;
    if (!camera.open(cameraIndex)) {
        std::cerr << "ERROR: could not open camera " << cameraIndex
                  << ". Try another index:  hand_gesture_control <index>" << std::endl;
        return 1;
    }
    std::cout << "Camera " << cameraIndex << " opened." << std::endl;

    cv::namedWindow(kMainWindow, cv::WINDOW_AUTOSIZE);

    HandProcessor processor;
    processor.createCalibrationWindow();

    SignLanguageDB db((rootDir / "data" / "sign_language_db.json").string());
    if (!db.load()) {
        std::cout << "WARNING: sign DB was corrupted; defaults restored." << std::endl;
    }

    SpeechEngine speech;
    const bool ttsAvailable = speech.init();
    if (!ttsAvailable) {
        std::cout << "WARNING: Windows TTS unavailable; speech disabled." << std::endl;
    }

    TranscriptWriter transcript((rootDir / "session_transcript.txt").string());
    transcript.beginSession();

    NvidiaAIClient ai;
    ai.configure(env.get("NVIDIA_API_KEY"), env.get("NVIDIA_MODEL"));
    if (!ai.isConfigured()) {
        std::cout << "NOTE: NVIDIA_API_KEY not set in .env - AI features use offline fallback."
                  << std::endl;
    }

    TelemetryLogger telemetry;

    ControlLogic control;
    MouseContext mouseCtx;
    cv::setMouseCallback(kMainWindow, onMouse, &mouseCtx);

    std::string lastSentCommand;
    double smoothedFps = 0.0;
    auto previousTick = std::chrono::steady_clock::now();
    const auto sessionStart = std::chrono::steady_clock::now();
    int consecutiveFailures = 0;

    std::string currentGestureName;
    int dwellFrames = 0;
    std::string lockedGestureName;
    std::vector<std::string> sentenceWords;
    std::string confirmedPhrase;
    std::string aiDiagnosis;
    std::string refinedSentence;
    std::string updateStatus;
    int autoAssistFrames = 0;
    bool autoAssistVisible = false;

    int pendingUpdate = 0;
    std::mutex updateMtx;
    std::thread updateThread;

#ifdef _WIN32
    auto resolveGit = []() -> std::string {
        const char* candidates[] = {"C:\\Program Files\\Git\\cmd\\git.exe",
                                    "F:\\tools\\MinGit\\cmd\\git.exe"};
        for (const char* candidate : candidates) {
            if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
                return "\"" + std::string(candidate) + "\"";
            }
        }
        return "git";
    };
    auto launchUpdater = [&]() {
        {
            std::lock_guard<std::mutex> lock(updateMtx);
            if (updateThread.joinable()) {
                updateThread.join();
            }
            updateStatus = "Update: running git fetch/pull...";
        }
        const std::filesystem::path root = rootDir;
        const std::string token = env.get("GITHUB_PAT_TOKEN");
        const bool hasToken = !token.empty() && token.rfind("PASTE_YOUR", 0) != 0;
        const std::string authHeader = hasToken
            ? "AUTHORIZATION: basic " + base64Encode(std::vector<uchar>(
                  ("x-access-token:" + token).begin(), ("x-access-token:" + token).end()))
            : "";
        updateThread = std::thread([root, authHeader, &updateMtx, &updateStatus]() {
            std::string gitExe = "git";
            const char* candidates[] = {"C:\\Program Files\\Git\\cmd\\git.exe",
                                        "F:\\tools\\MinGit\\cmd\\git.exe"};
            for (const char* candidate : candidates) {
                if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
                    gitExe = "\"" + std::string(candidate) + "\"";
                    break;
                }
            }
            std::string gitPrefix = gitExe;
            if (!authHeader.empty()) {
                gitPrefix = gitExe + " -c http.https://github.com/.extraheader=\"" +
                            authHeader + "\"";
            }
            const std::string cmd =
                gitPrefix + " -C \"" + root.string() + "\" fetch origin main && " + gitPrefix +
                " -C \"" + root.string() + "\" pull origin main";
            const int rc = std::system(cmd.c_str());
            std::lock_guard<std::mutex> lock(updateMtx);
            updateStatus = rc == 0
                ? "Update: OK (git fetch/pull done)"
                : "Update: FAILED rc=" + std::to_string(rc) + " (check token/remote)";
        });
    };
#endif

    cv::Mat frame;
    cv::Mat mask;
    cv::Mat roiDebug;
    HandAnalysisResult analysis;

    while (true) {
        if (!camera.read(frame) || frame.empty()) {
            ++consecutiveFailures;
            if (consecutiveFailures > 50) {
                std::cerr << "ERROR: too many consecutive frame failures." << std::endl;
                break;
            }
            continue;
        }
        consecutiveFailures = 0;

        analysis = processor.process(frame, mask, roiDebug);
        control.update(analysis.fingerCount, analysis.handDetected);

        const double maskCoverage =
            mask.empty() ? 0.0
                         : static_cast<double>(cv::countNonZero(mask)) /
                               static_cast<double>(mask.total());
        if (maskCoverage > 0.85 || maskCoverage < 0.02) {
            ++autoAssistFrames;
            if (autoAssistFrames > kAutoAssistFrames) {
                autoAssistVisible = true;
            }
        } else {
            autoAssistFrames = 0;
            autoAssistVisible = false;
        }

        GestureSpec gesture;
        bool gestureValid = false;
        if (analysis.handDetected) {
            gesture = db.match(analysis);
            gestureValid = true;
        }

        if (gestureValid && gesture.name == currentGestureName) {
            ++dwellFrames;
        } else {
            currentGestureName = gestureValid ? gesture.name : "";
            dwellFrames = 0;
            if (!gestureValid || gesture.name != lockedGestureName) {
                lockedGestureName = "";
            }
        }
        const double stabilityPct =
            std::min(100.0, static_cast<double>(dwellFrames) * 100.0 / kDwellFrames);

        if (gestureValid && dwellFrames >= kDwellFrames && gesture.name != lockedGestureName) {
            lockedGestureName = gesture.name;
            confirmedPhrase = gesture.phrase.empty() ? gesture.name : gesture.phrase;
            sentenceWords.push_back(confirmedPhrase);
            transcript.logPhrase(gesture.name, confirmedPhrase);
            speech.speak(std::wstring(confirmedPhrase.begin(), confirmedPhrase.end()));
            std::cout << "[CONFIRMED] " << gesture.name << " -> " << confirmedPhrase
                      << std::endl;
        }

        const std::string& stable = control.stableCommand();
        if (stable != lastSentCommand) {
            lastSentCommand = stable;
            control.sendToHardware(stable);
        }

        if (mouseCtx.aiClicked.exchange(false)) {
            try {
                SceneMetrics metrics = processor.computeSceneMetrics(frame, mask);
                ai.requestTuningAsync(metrics, processor.hsvBounds());
            } catch (const std::exception& ex) {
                aiDiagnosis = std::string("AI metrics failed: ") + ex.what();
            }
        }
        if (mouseCtx.updateClicked.exchange(false)) {
#ifdef _WIN32
            pendingUpdate = 1;
#endif
        }

        AiTuningResult tuning;
        if (ai.pollTuning(tuning) && tuning.valid) {
            try {
                HsvBounds b;
                b.hMin = tuning.hMin;
                b.hMax = tuning.hMax;
                b.sMin = tuning.sMin;
                b.sMax = tuning.sMax;
                b.vMin = tuning.vMin;
                b.vMax = tuning.vMax;
                processor.setHsvBounds(b);
                processor.setMorphKernelSize(tuning.morphKernelSize);
                processor.setBlurSigma(tuning.blurSigma);
                processor.setDefectAngleLimitDeg(tuning.defectAngleLimitDeg);
                processor.setDefectDepthThreshold(tuning.defectDepthThresh);
                aiDiagnosis = (tuning.fromNetwork ? "AI Tune: " : "") + tuning.lightingStatus;
            } catch (const std::exception& ex) {
                std::string message = std::string("AI apply failed: ") + ex.what();
                aiDiagnosis = message.substr(0, std::min<std::size_t>(message.size(), 80));
            }
        }
        const std::string tuningState = ai.tuningStatus();
        if (tuningState == "working") {
            aiDiagnosis = "AI Tune: analyzing scene...";
        } else if (tuningState.rfind("failed", 0) == 0) {
            std::string message = "AI Tune " + tuningState;
            aiDiagnosis = message.substr(0, std::min<std::size_t>(message.size(), 90));
        }
        std::string refined;
        if (ai.pollRefined(refined)) {
            refinedSentence = refined;
            speech.speak(std::wstring(refined.begin(), refined.end()));
            transcript.logRefinedSentence(refined);
            std::cout << "[AI SENTENCE] " << refined << std::endl;
        }

        const cv::Rect roi = processor.roiRect(frame);
        cv::rectangle(frame, roi, cv::Scalar(0, 255, 0), 2);
        const cv::Rect palm = cv::Rect(roi.x + (roi.width - 40) / 2,
                                       roi.y + (roi.height - 40) / 2, 40, 40);
        cv::rectangle(frame, palm, cv::Scalar(0, 165, 255), 1);

        const int w = frame.cols;
        mouseCtx.aiButton = cv::Rect(w - 190, 8, 182, 30);
        mouseCtx.updateButton = cv::Rect(w - 190, 46, 182, 30);
        drawButton(frame, mouseCtx.aiButton, "[ AI TUNE (A) ]", cv::Scalar(120, 200, 90));
        drawButton(frame, mouseCtx.updateButton, "[ UPDATE GITHUB (U) ]",
                   cv::Scalar(200, 170, 90));

        drawLabel(frame, "CMD: " + stable, cv::Point(10, 30), cv::Scalar(80, 255, 80), 0.7, 2);
        std::string signLine = "Sign: ";
        if (gestureValid) {
            signLine += gesture.name + "  ->  " + gesture.phrase;
        } else {
            signLine += "(no hand)";
        }
        drawLabel(frame, signLine, cv::Point(10, 58), cv::Scalar(0, 255, 255), 0.6, 1);
        drawProgressBar(frame, roi, stabilityPct);
        drawLabel(frame, "Sentence: " + wordsLine(sentenceWords, 5),
                  cv::Point(10, frame.rows - 62), cv::Scalar(255, 255, 255), 0.55, 1);
        if (!refinedSentence.empty()) {
            drawLabel(frame, "AI: " + refinedSentence, cv::Point(10, frame.rows - 38),
                      cv::Scalar(180, 220, 255), 0.55, 1);
        }
        drawLabel(frame, cv::format("FPS: %.1f", smoothedFps),
                  cv::Point(10, frame.rows - 14), cv::Scalar(255, 255, 255), 0.55, 1);

        if (telemetry.isRecording()) {
            drawLabel(frame, "[ REC CSV ]", cv::Point(10, 86), cv::Scalar(60, 60, 255), 0.6, 1);
        }
        if (!aiDiagnosis.empty()) {
            drawLabel(frame, aiDiagnosis, cv::Point(10, 110), cv::Scalar(90, 220, 255), 0.55, 1);
        }
        if (!updateStatus.empty()) {
            std::string statusLine;
            {
                std::lock_guard<std::mutex> lock(updateMtx);
                statusLine = updateStatus;
            }
            drawLabel(frame, statusLine, cv::Point(10, 132), cv::Scalar(200, 200, 120), 0.5, 1);
        }
        if (autoAssistVisible) {
            drawLabel(frame, "Mask problem - [Press A for AI Lighting Tune]",
                      cv::Point(10, 154), cv::Scalar(60, 100, 255), 0.55, 1);
        }
        drawLabel(frame, speech.isMuted() ? "TTS: OFF" : "TTS: ON",
                  cv::Point(w - 110, frame.rows - 14), cv::Scalar(220, 220, 220), 0.5, 1);

        if (telemetry.isRecording()) {
            TelemetryLogger::Row row;
            row.timeMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                          sessionStart)
                    .count();
            row.rawFingers = analysis.fingerCount;
            row.filteredFingers = control.stableFingers();
            row.defectCount = analysis.defectCount;
            row.orientationDeg = analysis.orientationDeg;
            row.solidity = analysis.solidity;
            const cv::Scalar frameMean = cv::mean(frame);
            row.ambientBrightness = frameMean[2];
            row.stabilityPct = stabilityPct;
            row.confirmedPhrase = confirmedPhrase;
            telemetry.logRow(row);
        }

        imshow(kMainWindow, frame);
        if (!mask.empty()) {
            imshow(kMaskWindow, mask);
        }
        if (!roiDebug.empty()) {
            imshow(kRoiWindow, roiDebug);
        }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - previousTick).count();
        previousTick = now;
        if (elapsed > 1e-6) {
            smoothedFps = 0.9 * smoothedFps + 0.1 * (1.0 / elapsed);
        }

#ifdef _WIN32
        if (pendingUpdate == 1) {
            pendingUpdate = 0;
            launchUpdater();
        }
#endif

        int key = cv::waitKey(1) & 0xFF;
        if (key == 'q' || key == 'Q' || key == 27) {
            break;
        }
        switch (key) {
            case 'c':
            case 'C':
                if (processor.calibrateLocal(frame)) {
                    std::cout << "[CALIBRATE] Local HSV calibration applied from palm sample."
                              << std::endl;
                }
                break;
            case 'a':
            case 'A': {
                try {
                    SceneMetrics metrics = processor.computeSceneMetrics(frame, mask);
                    ai.requestTuningAsync(metrics, processor.hsvBounds());
                } catch (const std::exception& ex) {
                    aiDiagnosis = std::string("AI metrics failed: ") + ex.what();
                }
                break;
            }
            case 'g':
            case 'G':
                ai.requestRefineAsync(sentenceWords);
                break;
            case 'l':
            case 'L':
                if (telemetry.isRecording()) {
                    telemetry.stop();
                    transcript.noteEvent("Telemetry CSV logging stopped.");
                    std::cout << "[CSV] Logging stopped." << std::endl;
                } else if (telemetry.start((rootDir / "telemetry_log.csv").string())) {
                    transcript.noteEvent("Telemetry CSV logging started.");
                    std::cout << "[CSV] Logging to telemetry_log.csv" << std::endl;
                } else {
                    std::cerr << "[CSV] Failed to open telemetry_log.csv" << std::endl;
                }
                break;
            case 'u':
            case 'U':
#ifdef _WIN32
                pendingUpdate = 1;
#endif
                break;
            case 's':
            case 'S':
                speech.setMuted(!speech.isMuted());
                std::cout << "[TTS] " << (speech.isMuted() ? "muted" : "unmuted") << std::endl;
                break;
            case 'n':
            case 'N':
                sentenceWords.clear();
                refinedSentence.clear();
                std::cout << "[SENTENCE] Buffer cleared - new sentence started." << std::endl;
                break;
            case 't':
            case 'T': {
                if (!analysis.handDetected) {
                    std::cout << "[TEACH] Show a hand in the ROI first." << std::endl;
                    break;
                }
                std::cout << "\n[TEACH] Captured features: fingers="
                          << analysis.fingerCount
                          << " orient=" << static_cast<int>(analysis.orientationDeg)
                          << "deg solidity=" << std::fixed << std::setprecision(2)
                          << analysis.solidity << " aspect=" << analysis.aspectRatio << "\n";
                std::cout << "[TEACH] Phrase for this sign (Enter = auto name): ";
                std::string phrase;
                std::getline(std::cin, phrase);
                if (phrase.empty()) {
                    phrase = "Custom Sign " + std::to_string(db.size() + 1);
                }
                std::cout << "[TEACH] Optional display name (Enter = use phrase): ";
                std::string name;
                std::getline(std::cin, name);
                if (name.empty()) {
                    name = phrase;
                }
                GestureSpec spec;
                spec.name = name;
                spec.phrase = phrase;
                spec.fingers = analysis.fingerCount;
                spec.orientMin = std::fmod(analysis.orientationDeg + 335.0, 180.0);
                spec.orientMax = std::fmod(analysis.orientationDeg + 25.0, 180.0);
                spec.solidityMin = std::max(0.0, analysis.solidity - 0.12);
                spec.solidityMax = std::min(1.01, analysis.solidity + 0.12);
                spec.aspectMin = std::max(0.0, analysis.aspectRatio - 0.35);
                spec.aspectMax = analysis.aspectRatio + 0.35;
                spec.custom = true;
                db.addCustom(spec);
                transcript.noteEvent("Taught new custom sign: " + name + " -> " + phrase);
                std::cout << "[TEACH] Saved '" << name << "' (" << db.size()
                          << " gestures in DB)." << std::endl;
                break;
            }
            default:
                break;
        }
    }

    if (updateThread.joinable()) {
        updateThread.join();
    }

    transcript.endSession();
    telemetry.stop();
    processor.destroyCalibrationWindow();
    camera.release();
    cv::destroyAllWindows();
    std::cout << "Shutdown complete. Transcript: session_transcript.txt" << std::endl;
    return 0;
}
