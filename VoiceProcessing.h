#pragma once
#include "VoiceAudioFormat.h"

#include <array>
#include <deque>
#include <cstdint>
#include "VoiceEchoCancellation.h"

// All processing and queues belong to the voice/UI thread. No microphone data
// is queued for transmission across mute/PTT sessions.
class cVoiceProcessing
{
public:
    enum { SampleRate = VoiceAudioFormat::SampleRate, FrameSamples = VoiceAudioFormat::FrameSamples };
    typedef std::array<int16_t, FrameSamples> Frame;

private:
    VoiceEchoCancellation _echo;
    std::deque<Frame> _leadIn;
    std::deque<Frame> _ready;
    int _hold = 0;
    int _speechFrames = 0;

public:
    cVoiceProcessing() = default;

    cVoiceProcessing(const cVoiceProcessing&) = delete;
    cVoiceProcessing& operator=(const cVoiceProcessing&) = delete;

    void resetEcho() { reset(); }

    void reset(bool resetFilter = true)
    {
        _leadIn.clear();
        _ready.clear();
        _hold = 0;
        _speechFrames = 0;
        _echo.reset(resetFilter);
    }

    void process(const int16_t* microphone, const Frame& playback, bool pushToTalk)
    {
        Frame clean;
        const bool speech = _echo.process(microphone, playback.data(), clean.data());
        int64_t energy = 0;
        for (int16_t sample : clean) energy += static_cast<int64_t>(sample) * sample;
        // Ignore near-silent classifier triggers (-50 dBFS RMS). Preroll and
        // hangover retain quieter consonants around an actual utterance.
        const bool audible = energy >= FrameSamples * 100LL * 100;
        if (speech && audible) { if (_speechFrames < 2) ++_speechFrames; }
        else _speechFrames = 0;
        // Classify the echo-cancelled signal, retaining leading consonants and
        // short pauses without adjusting microphone gain or speaker volume.
        if (pushToTalk || _speechFrames == 2) _hold = 12;
        if (_hold)
        {
            while (!_leadIn.empty())
            {
                _ready.push_back(_leadIn.front());
                _leadIn.pop_front();
            }
            _ready.push_back(clean);
            --_hold;
        }
        else
        {
            _leadIn.push_back(clean);
            if (_leadIn.size() > 3) _leadIn.pop_front();
        }
        while (_ready.size() > 16) _ready.pop_front();
    }

    bool pop(Frame& frame)
    {
        if (_ready.empty()) return false;
        frame = _ready.front();
        _ready.pop_front();
        return true;
    }
};
