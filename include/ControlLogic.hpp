#pragma once

#include <cstddef>
#include <deque>
#include <string>

class ControlLogic {
public:
    static constexpr std::size_t kDefaultVoteWindow = 10;

    explicit ControlLogic(std::size_t voteWindow = kDefaultVoteWindow);

    void update(int fingerCount, bool handDetected);

    const std::string& stableCommand() const;
    const std::string& rawCommand() const;
    int stableFingers() const;

    void sendToHardware(const std::string& command);

    static std::string mapFingersToCommand(int fingerCount);

private:
    std::deque<std::string> votes_;
    std::deque<int> fingerVotes_;
    std::size_t voteWindow_;
    std::string rawCommand_;
    std::string stableCommand_{"NO_HAND"};
    int stableFingers_{0};
};
