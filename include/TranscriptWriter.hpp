#pragma once

#include <string>
#include <vector>

class TranscriptWriter {
public:
    explicit TranscriptWriter(std::string path);

    void beginSession();
    void logPhrase(const std::string& gesture, const std::string& phrase);
    void logRefinedSentence(const std::string& sentence);
    void noteEvent(const std::string& text);
    void endSession();

    const std::vector<std::string>& communicatedPhrases() const;

private:
    std::string timestamp() const;
    void appendLine(const std::string& line);

    std::string path_;
    std::vector<std::string> phrases_;
    bool sessionActive_{false};
};
