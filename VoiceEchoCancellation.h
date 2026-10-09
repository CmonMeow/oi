#pragma once
#include <cstdint>
#include <memory>

// Fixed 16 kHz mono interface; WebRTC types stay out of the client headers.
class VoiceEchoCancellation {
    struct State;
    std::unique_ptr<State> _state;
public:
    VoiceEchoCancellation();
    ~VoiceEchoCancellation();
    VoiceEchoCancellation(const VoiceEchoCancellation&) = delete;
    VoiceEchoCancellation& operator=(const VoiceEchoCancellation&) = delete;
    void reset(bool resetFilter = true);
    bool process(const int16_t* microphone, const int16_t* playback, int16_t* output);
};
