#include "ControlLogic.hpp"

#include <algorithm>
#include <iostream>
#include <map>

ControlLogic::ControlLogic(std::size_t voteWindow)
    : voteWindow_(voteWindow == 0 ? 1 : voteWindow) {}

std::string ControlLogic::mapFingersToCommand(int fingerCount) {
    switch (fingerCount) {
        case 0:
            return "STOP";
        case 1:
            return "FORWARD";
        case 2:
            return "BACKWARD";
        case 3:
            return "TURN_LEFT";
        case 4:
            return "TURN_RIGHT";
        case 5:
            return "STANDBY";
        default:
            return "UNKNOWN";
    }
}

void ControlLogic::update(int fingerCount, bool handDetected) {
    rawCommand_ = handDetected ? mapFingersToCommand(fingerCount) : "NO_HAND";

    votes_.push_back(rawCommand_);
    while (votes_.size() > voteWindow_) {
        votes_.pop_front();
    }

    fingerVotes_.push_back(handDetected ? fingerCount : 0);
    while (fingerVotes_.size() > voteWindow_) {
        fingerVotes_.pop_front();
    }
    int counts[6] = {0, 0, 0, 0, 0, 0};
    for (int v : fingerVotes_) {
        if (v >= 0 && v <= 5) {
            ++counts[v];
        }
    }
    int bestFinger = 0;
    int bestFingerCount = -1;
    for (int i = 0; i <= 5; ++i) {
        if (counts[i] > bestFingerCount) {
            bestFingerCount = counts[i];
            bestFinger = i;
        }
    }
    stableFingers_ = bestFinger;

    std::map<std::string, int> tally;
    for (const std::string& vote : votes_) {
        ++tally[vote];
    }

    int bestCount = 0;
    for (const auto& entry : tally) {
        bestCount = std::max(bestCount, entry.second);
    }

    std::string winner;
    bool tie = false;
    for (const auto& entry : tally) {
        if (entry.second == bestCount) {
            if (!winner.empty()) {
                tie = true;
                break;
            }
            winner = entry.first;
        }
    }

    if (!tie && !winner.empty() && winner != stableCommand_) {
        stableCommand_ = winner;
    }
}

const std::string& ControlLogic::stableCommand() const {
    return stableCommand_;
}

const std::string& ControlLogic::rawCommand() const {
    return rawCommand_;
}

int ControlLogic::stableFingers() const {
    return stableFingers_;
}

void ControlLogic::sendToHardware(const std::string& command) {
    // Placeholder: open a Serial/UART port here (Arduino/ESP32) and write the command.
    std::cout << "[HARDWARE TX] " << command << std::endl;
}
