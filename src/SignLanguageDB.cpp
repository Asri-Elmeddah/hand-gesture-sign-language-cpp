#include "SignLanguageDB.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace {

std::vector<GestureSpec> defaultGestures() {
    return {
        {"C Shape (Curved Hand)", "Food / Hungry", 0, 0.0, 180.0, 0.45, 0.78, 0.7, 1.6, false},
        {"Compact Fist", "Stop / No", 0, 0.0, 180.0, 0.80, 1.01, 0.6, 1.6, false},
        {"Index Up", "I need help", 1, 65.0, 115.0, 0.0, 1.01, 1.4, 10.0, false},
        {"Thumb Side", "Yes / I agree", 1, 160.0, 20.0, 0.0, 1.01, 0.0, 1.35, false},
        {"Wide L Shape", "Water, please", 2, 20.0, 160.0, 0.0, 0.80, 0.7, 1.45, false},
        {"Narrow V", "Peace / Hello", 2, 65.0, 115.0, 0.80, 1.01, 1.2, 10.0, false},
        {"V Horizontal", "Victory / Goodbye", 2, 160.0, 20.0, 0.80, 1.01, 0.0, 1.2, false},
        {"Three Fingers Up", "Thank you", 3, 65.0, 115.0, 0.0, 1.01, 0.0, 10.0, false},
        {"Three Fingers Side", "Where / Question", 3, 160.0, 20.0, 0.0, 1.01, 0.0, 10.0, false},
        {"Four Fingers Up", "Excuse me", 4, 65.0, 115.0, 0.0, 1.01, 0.0, 10.0, false},
        {"Four Fingers Side", "Please / Sorry", 4, 160.0, 20.0, 0.0, 1.01, 0.0, 10.0, false},
        {"Open Vertical Palm", "Wait please", 5, 65.0, 115.0, 0.0, 1.01, 0.0, 10.0, false},
        {"Tilted Open Palm", "Good / Fine", 5, 160.0, 20.0, 0.0, 1.01, 0.0, 10.0, false},
    };
}

}  // namespace

SignLanguageDB::SignLanguageDB(std::string path) : path_(std::move(path)) {}

bool SignLanguageDB::orientInRange(double orient, const GestureSpec& spec) const {
    if (spec.orientMin <= spec.orientMax) {
        return orient >= spec.orientMin && orient <= spec.orientMax;
    }
    return orient >= spec.orientMin || orient <= spec.orientMax;
}

void SignLanguageDB::ensureDefaults() {
    if (gestures_.empty()) {
        gestures_ = defaultGestures();
    }
}

bool SignLanguageDB::load() {
    std::ifstream file(path_);
    if (!file.is_open()) {
        ensureDefaults();
        save();
        return true;
    }
    try {
        json data;
        file >> data;
        gestures_.clear();
        for (const auto& entry : data.at("gestures")) {
            GestureSpec spec;
            spec.name = entry.at("name").get<std::string>();
            spec.phrase = entry.at("phrase").get<std::string>();
            spec.fingers = entry.value("fingers", 0);
            spec.orientMin = entry.value("orient_min", 0.0);
            spec.orientMax = entry.value("orient_max", 180.0);
            spec.solidityMin = entry.value("solidity_min", 0.0);
            spec.solidityMax = entry.value("solidity_max", 1.01);
            spec.aspectMin = entry.value("aspect_min", 0.0);
            spec.aspectMax = entry.value("aspect_max", 10.0);
            spec.custom = entry.value("custom", false);
            if (spec.custom) {
                customCounter_++;
            }
            gestures_.push_back(spec);
        }
        return true;
    } catch (const std::exception&) {
        ensureDefaults();
        save();
        return false;
    }
}

bool SignLanguageDB::save() const {
    json array = json::array();
    for (const GestureSpec& spec : gestures_) {
        array.push_back({{"name", spec.name},
                         {"phrase", spec.phrase},
                         {"fingers", spec.fingers},
                         {"orient_min", spec.orientMin},
                         {"orient_max", spec.orientMax},
                         {"solidity_min", spec.solidityMin},
                         {"solidity_max", spec.solidityMax},
                         {"aspect_min", spec.aspectMin},
                         {"aspect_max", spec.aspectMax},
                         {"custom", spec.custom}});
    }
    json data;
    data["gestures"] = array;
    std::ofstream file(path_);
    if (!file.is_open()) {
        return false;
    }
    file << data.dump(2) << "\n";
    return true;
}

void SignLanguageDB::addCustom(const GestureSpec& spec) {
    gestures_.push_back(spec);
    customCounter_++;
    save();
}

GestureSpec SignLanguageDB::match(const HandAnalysisResult& f) const {
    for (const GestureSpec& spec : gestures_) {
        if (spec.fingers != f.fingerCount) {
            continue;
        }
        if (!orientInRange(f.orientationDeg, spec)) {
            continue;
        }
        if (f.solidity < spec.solidityMin || f.solidity > spec.solidityMax) {
            continue;
        }
        if (f.aspectRatio < spec.aspectMin || f.aspectRatio > spec.aspectMax) {
            continue;
        }
        return spec;
    }

    for (const GestureSpec& spec : gestures_) {
        if (spec.fingers == f.fingerCount) {
            return spec;
        }
    }
    return GestureSpec{"Unknown", "", f.fingerCount, 0, 180, 0, 1.01, 0, 10, false};
}

const std::vector<GestureSpec>& SignLanguageDB::gestures() const {
    return gestures_;
}

std::size_t SignLanguageDB::size() const {
    return gestures_.size();
}
