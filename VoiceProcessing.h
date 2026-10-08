#pragma once

#include <array>
#include <deque>
#include <cstdint>
#include "speex/speex_echo.h"
#include "speex/speex_preprocess.h"

// All processing and queues belong to the voice/UI thread. No microphone data
// is retained across mute/PTT sessions.
class cVoiceProcessing
{
public:
    enum { SampleRate = 16000, FrameSamples = 320 };
    typedef std::array<int16_t, FrameSamples> Frame;

private:
    SpeexEchoState* _echo;
    SpeexPreprocessState* _preprocess;
    std::deque<Frame> _leadIn;
    std::deque<Frame> _ready;
    int _hold = 0;

    void configure()
    {
        int enabled = 1, noiseDb = -20, echoDb = -40, talkingEchoDb = -15;
        int target = 8000, maxGainDb = 12;
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_DENOISE, &enabled);
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseDb);
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_ECHO_STATE, _echo);
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &echoDb);
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &talkingEchoDb);
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_AGC, &enabled);
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_AGC_TARGET, &target);
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &maxGainDb);
    }

public:
    cVoiceProcessing()
        : _echo(speex_echo_state_init(FrameSamples, SampleRate / 5)),
          _preprocess(speex_preprocess_state_init(FrameSamples, SampleRate))
    {
        int rate = SampleRate;
        speex_echo_ctl(_echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
        configure();
    }

    ~cVoiceProcessing()
    {
        speex_preprocess_state_destroy(_preprocess);
        speex_echo_state_destroy(_echo);
    }

    cVoiceProcessing(const cVoiceProcessing&) = delete;
    cVoiceProcessing& operator=(const cVoiceProcessing&) = delete;

    void resetEcho() { speex_echo_state_reset(_echo); }

    void reset()
    {
        _leadIn.clear();
        _ready.clear();
        _hold = 0;
        resetEcho();
        speex_preprocess_state_destroy(_preprocess);
        _preprocess = speex_preprocess_state_init(FrameSamples, SampleRate);
        configure();
    }

    void process(const int16_t* microphone, const Frame& playback, bool pushToTalk)
    {
        Frame clean;
        speex_echo_cancellation(_echo, microphone, playback.data(), clean.data());
        speex_preprocess_run(_preprocess, clean.data());
        int probability = 0;
        speex_preprocess_ctl(_preprocess, SPEEX_PREPROCESS_GET_PROB, &probability);
        // Speech probability follows the estimated background noise, rather
        // than a fixed microphone volume threshold. Hysteresis avoids chatter.
        if (pushToTalk || probability >= (_hold ? 35 : 65)) _hold = 12;
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
