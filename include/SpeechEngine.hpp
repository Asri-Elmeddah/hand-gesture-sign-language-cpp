#pragma once

#include <string>

#ifdef _WIN32

#include <objbase.h>
#include <sapi.h>

class SpeechEngine {
public:
    SpeechEngine();
    ~SpeechEngine();

    SpeechEngine(const SpeechEngine&) = delete;
    SpeechEngine& operator=(const SpeechEngine&) = delete;

    bool init();
    void speak(const std::wstring& text);
    void setMuted(bool muted);
    bool isMuted() const;

private:
    ISpVoice* voice_{nullptr};
    bool comInitialized_{false};
    bool muted_{false};
};

#else

class SpeechEngine {
public:
    bool init() { return false; }
    void speak(const std::wstring&) {}
    void setMuted(bool muted) { muted_ = muted; }
    bool isMuted() const { return muted_; }

private:
    bool muted_{false};
};

#endif
