#include "TranscriptWriter.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream oss;
    oss << std::put_time(&local, "%H:%M:%S");
    return oss.str();
}

std::string currentDateTime() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream oss;
    oss << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}

TranscriptWriter::TranscriptWriter(std::string path) : path_(std::move(path)) {}

void TranscriptWriter::beginSession() {
    sessionActive_ = true;
    appendLine("=========================================================");
    appendLine("SIGN LANGUAGE TRANSLATOR SESSION START: " + currentDateTime());
    appendLine("=========================================================");
}

std::string TranscriptWriter::timestamp() const {
    return currentTimestamp();
}

void TranscriptWriter::logPhrase(const std::string& gesture, const std::string& phrase) {
    phrases_.push_back(phrase);
    appendLine("[" + timestamp() + "] " + gesture + " -> " + phrase);
}

void TranscriptWriter::logRefinedSentence(const std::string& sentence) {
    appendLine("[" + timestamp() + "] AI-REFINED SENTENCE: " + sentence);
}

void TranscriptWriter::noteEvent(const std::string& text) {
    appendLine("[" + timestamp() + "] " + text);
}

void TranscriptWriter::endSession() {
    if (!sessionActive_) {
        return;
    }
    appendLine("---------------------------------------------------------");
    appendLine("SESSION END: " + currentDateTime());

    std::ostringstream summary;
    summary << "Full communication summary: ";
    for (std::size_t i = 0; i < phrases_.size(); ++i) {
        summary << phrases_[i];
        if (i + 1 < phrases_.size()) {
            summary << ". ";
        }
    }
    if (phrases_.empty()) {
        summary << "(no signs were confirmed this session)";
    }
    appendLine(summary.str());
    appendLine("=========================================================");
    sessionActive_ = false;
}

const std::vector<std::string>& TranscriptWriter::communicatedPhrases() const {
    return phrases_;
}

void TranscriptWriter::appendLine(const std::string& line) {
    std::ofstream file(path_, std::ios::app);
    if (file.is_open()) {
        file << line << "\n";
    }
}
