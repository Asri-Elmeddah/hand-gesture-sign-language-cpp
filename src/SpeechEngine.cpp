#include "SpeechEngine.hpp"

#ifdef _WIN32

#include <sapi.h>

namespace {

const CLSID kSpVoiceClsid = {0x96749377, 0x3391, 0x11D2,
                             {0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96}};
const IID kISpVoiceIid = {0x6C44DF74, 0x72B9, 0x4992,
                          {0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4}};

}

SpeechEngine::SpeechEngine() {
    comInitialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
}

SpeechEngine::~SpeechEngine() {
    if (voice_) {
        voice_->Release();
        voice_ = nullptr;
    }
    if (comInitialized_) {
        CoUninitialize();
    }
}

bool SpeechEngine::init() {
    if (!comInitialized_) {
        return false;
    }
    const HRESULT hr = CoCreateInstance(kSpVoiceClsid, nullptr, CLSCTX_INPROC_SERVER,
                                        kISpVoiceIid, reinterpret_cast<void**>(&voice_));
    return SUCCEEDED(hr) && voice_ != nullptr;
}

void SpeechEngine::speak(const std::wstring& text) {
    if (muted_ || !voice_ || text.empty()) {
        return;
    }
    voice_->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
}

void SpeechEngine::setMuted(bool muted) {
    muted_ = muted;
}

bool SpeechEngine::isMuted() const {
    return muted_;
}

#endif
