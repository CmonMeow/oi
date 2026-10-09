#pragma once
#include "VoiceAudioFormat.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Detect speaker audio remaining after cancellation over 80 ms, allowing up to 200 ms acoustic
// delay. Decimation keeps this bounded; independent speech releases the duck.
class VoiceEchoSuppressor {
    enum {
        Decimation = 4,
        Block = VoiceAudioFormat::FrameSamples / Decimation,
        MicWindow = Block * 4,
        MaxDelay = VoiceAudioFormat::SampleRate / Decimation / 5,
        SpeakerWindow = MicWindow + MaxDelay
    };
    std::array<float, SpeakerWindow> _speaker{};
    std::array<float, MicWindow> _microphone{};
    unsigned _frames = 0;
    bool _echo = false;
    float _gain = 1;
public:
    void reset() {
        _speaker.fill(0);
        _microphone.fill(0);
        _frames = 0;
        _echo = false;
        _gain = 1;
    }
    bool analyze(const int16_t* mic, const int16_t* speaker) {
        std::move(_speaker.begin() + Block, _speaker.end(), _speaker.begin());
        std::move(_microphone.begin() + Block, _microphone.end(), _microphone.begin());
        for (int i = 0; i < Block; ++i) {
            float m = 0, r = 0;
            for (int j = 0; j < Decimation; ++j) {
                m += mic[Decimation * i + j];
                r += speaker[Decimation * i + j];
            }
            _microphone[MicWindow - Block + i] = m / Decimation;
            _speaker[SpeakerWindow - Block + i] = r / Decimation;
        }
        if (_frames < MicWindow / Block) {
            ++_frames;
            _echo = false;
            return false;
        }
        double micEnergy = 0, micSum = 0;
        for (float x : _microphone) { micEnergy += x * x; micSum += x; }
        micEnergy -= micSum * micSum / MicWindow;
        double best = 0;
        if (micEnergy > MicWindow * 25. * 25.) {
            // Reuse overlapping window sums; only correlation needs a dot product.
            double sums[SpeakerWindow + 1] = {}, squares[SpeakerWindow + 1] = {};
            for (int i = 0; i < SpeakerWindow; ++i) {
                double r = _speaker[i];
                sums[i + 1] = sums[i] + r;
                squares[i + 1] = squares[i] + r * r;
            }
            for (int start = 0; start <= MaxDelay; start += Decimation) {
                double dot = 0;
                for (int i = 0; i < MicWindow; ++i)
                    dot += double(_speaker[start + i]) * _microphone[i];
                double sum = sums[start + MicWindow] - sums[start];
                double energy = squares[start + MicWindow] - squares[start] - sum * sum / MicWindow;
                dot -= micSum * sum / MicWindow;
                if (energy > MicWindow * 25. * 25.)
                    best = (std::max)(best, dot * dot / (energy * micEnergy));
            }
        }
        // Squared correlation. Hysteresis avoids chopping between syllables.
        _echo = best > (_echo ? .16 : .36);
        return _echo;
    }
    void apply(int16_t* samples, bool echo) {
        const float target = echo ? .1259f : 1.f; // -18 dB while echo dominates.
        const float next = echo ? target : (std::min)(1.f, _gain + .25f);
        for (int i = 0; i < VoiceAudioFormat::FrameSamples; ++i)
            samples[i] = static_cast<int16_t>(samples[i] *
                (_gain + (next - _gain) * (i + 1) / VoiceAudioFormat::FrameSamples));
        _gain = next;
    }
};
