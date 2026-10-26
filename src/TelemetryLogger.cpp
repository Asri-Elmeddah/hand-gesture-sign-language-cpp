#include "TelemetryLogger.hpp"

#include <iomanip>

bool TelemetryLogger::start(const std::string& path) {
    if (recording_) {
        return true;
    }
    file_.open(path, std::ios::app);
    if (!file_.is_open()) {
        return false;
    }
    std::ifstream check(path);
    std::string firstLine;
    std::getline(check, firstLine);
    check.close();
    if (firstLine.rfind("Time_ms", 0) != 0) {
        file_ << "Time_ms,Raw_Fingers,Filtered_Fingers,Defect_Count,Orientation_Deg,"
                 "Solidity_Ratio,Ambient_Brightness,Stability_Pct,Confirmed_Phrase\n";
    }
    recording_ = true;
    return true;
}

void TelemetryLogger::stop() {
    if (recording_) {
        file_.flush();
        file_.close();
        recording_ = false;
    }
}

bool TelemetryLogger::isRecording() const {
    return recording_;
}

void TelemetryLogger::logRow(const Row& row) {
    if (!recording_) {
        return;
    }
    std::string phrase = row.confirmedPhrase;
    std::string escaped = "\"";
    for (const char c : phrase) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += "\"";

    file_ << static_cast<long long>(row.timeMs) << "," << row.rawFingers << ","
          << row.filteredFingers << "," << row.defectCount << "," << std::fixed
          << std::setprecision(2) << row.orientationDeg << "," << row.solidity << ","
          << row.ambientBrightness << "," << row.stabilityPct << "," << escaped << "\n";
}
