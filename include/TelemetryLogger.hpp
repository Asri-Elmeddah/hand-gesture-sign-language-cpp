#pragma once

#include <fstream>
#include <string>

class TelemetryLogger {
public:
    struct Row {
        double timeMs{0.0};
        int rawFingers{0};
        int filteredFingers{0};
        int defectCount{0};
        double orientationDeg{0.0};
        double solidity{0.0};
        double ambientBrightness{0.0};
        double stabilityPct{0.0};
        std::string confirmedPhrase;
    };

    bool start(const std::string& path);
    void stop();
    bool isRecording() const;
    void logRow(const Row& row);

private:
    std::ofstream file_;
    bool recording_{false};
};
