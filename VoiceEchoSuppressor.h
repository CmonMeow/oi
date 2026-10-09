#pragma once
#include "VoiceAudioFormat.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Suppress during speaker playback unless the cancellation residual contains
// independent speech. Compare 80 ms windows with up to 200 ms acoustic delay.
class VoiceEchoSuppressor {
    enum {
        Decimation = 4,
        Block = VoiceAudioFormat::FrameSamples / Decimation,
        MicWindow = Block * 4,
        MaxDelay = VoiceAudioFormat::SampleRate / Decimation / 5,
        SpeakerWindow = MicWindow + MaxDelay
    };
    std::array<float, SpeakerWindow> _speaker{};
    std::array<float, MicWindow> _microphone{}, _raw{};
    unsigned _frames = 0;
    unsigned _playbackHold = 0, _speechFrames = 0, _voiceHold = 0;
    bool _echo = false;
    float _gain = 1;
    double _residualRatio = 1;
public:
    void reset() {
        _speaker.fill(0);
        _microphone.fill(0);
        _raw.fill(0);
        _frames = 0;
        _playbackHold = _speechFrames = _voiceHold = 0;
        _echo = false;
        _gain = 1;
        _residualRatio = 1;
    }
    bool analyze(const int16_t* mic, const int16_t* speaker, const int16_t* raw, bool voiced) {
        std::move(_speaker.begin() + Block, _speaker.end(), _speaker.begin());
        std::move(_microphone.begin() + Block, _microphone.end(), _microphone.begin());
        std::move(_raw.begin() + Block, _raw.end(), _raw.begin());
        for (int i = 0; i < Block; ++i) {
            float m = 0, r = 0, original = 0;
            for (int j = 0; j < Decimation; ++j) {
                m += mic[Decimation * i + j];
                original += raw[Decimation * i + j];
                r += speaker[Decimation * i + j];
            }
            _microphone[MicWindow - Block + i] = m / Decimation;
            _raw[MicWindow - Block + i] = original / Decimation;
            _speaker[SpeakerWindow - Block + i] = r / Decimation;
        }
        double blockSum = 0, blockEnergy = 0;
        for (int i = SpeakerWindow - Block; i < SpeakerWindow; ++i) {
            blockSum += _speaker[i];
            blockEnergy += _speaker[i] * _speaker[i];
        }
        blockEnergy -= blockSum * blockSum / Block;
        if (blockEnergy > Block * 25. * 25.) _playbackHold = 12;
        else if (_playbackHold) --_playbackHold;
        if (_frames < MicWindow / Block) ++_frames;
        double micEnergy = 0, micSum = 0;
        for (float x : _microphone) { micEnergy += x * x; micSum += x; }
        micEnergy -= micSum * micSum / MicWindow;
        double rawSum = 0, rawEnergy = 0;
        for (float x : _raw) { rawSum += x; rawEnergy += x * x; }
        rawEnergy -= rawSum * rawSum / MicWindow;
        double rawBest = 0;
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
                double dot = 0, rawDot = 0;
                for (int i = 0; i < MicWindow; ++i) {
                    dot += double(_speaker[start + i]) * _microphone[i];
                    rawDot += double(_speaker[start + i]) * _raw[i];
                }
                double sum = sums[start + MicWindow] - sums[start];
                double energy = squares[start + MicWindow] - squares[start] - sum * sum / MicWindow;
                dot -= micSum * sum / MicWindow;
                rawDot -= rawSum * sum / MicWindow;
                if (energy > MicWindow * 25. * 25. && rawEnergy > MicWindow * 25. * 25.)
                    rawBest = (std::max)(rawBest, rawDot * rawDot / (energy * rawEnergy));
                if (energy > MicWindow * 25. * 25.)
                    best = (std::max)(best, dot * dot / (energy * micEnergy));
            }
        }
        // Estimate the remaining echo relative to the signal the filter removed.
        // Learn only when correlation provides evidence of echo; low correlation
        // alone is not evidence that the residual is a local speaker's voice.
        double removedSum = 0, removedEnergy = 0, removedDot = 0;
        for (int i = 0; i < MicWindow; ++i) {
            double x = _raw[i] - _microphone[i];
            removedSum += x;
            removedEnergy += x * x;
            removedDot += x * _microphone[i];
        }
        removedEnergy -= removedSum * removedSum / MicWindow;
        removedDot -= removedSum * micSum / MicWindow;
        double removedMatch = removedEnergy > MicWindow * 25. * 25. &&
            micEnergy > MicWindow * 25. * 25. ?
            removedDot * removedDot / (removedEnergy * micEnergy) : 0;
        if (_playbackHold && removedEnergy > MicWindow * 100. * 100. &&
            (rawBest > .6 || removedMatch > .15)) {
            double ratio = (std::max)(.02, (std::min)(4., micEnergy / removedEnergy));
            _residualRatio += .1 * (ratio - _residualRatio);
        }
        bool excess = micEnergy > removedEnergy * _residualRatio * 1.2;
        // Require voiced speech above the learned echo estimate, bridging 120 ms
        // of unvoiced consonants. Once speech opens, tolerate more correlation
        // from the other speaker so simultaneous speech is not cut as readily.
        if (voiced) _voiceHold = 6;
        else if (_voiceHold) --_voiceHold;
        bool independent = _voiceHold && _frames >= MicWindow / Block &&
            micEnergy > MicWindow * 100. * 100. && best < (_echo ? .12 : .3) && excess;
        if (independent) _speechFrames = (std::min)(_speechFrames + 1, 2u);
        else _speechFrames = 0;
        _echo = _playbackHold && _speechFrames < 2;
        return _echo;
    }
    void apply(int16_t* samples, bool echo) {
        const float target = echo ? .01585f : 1.f; // -36 dB speaker fallback.
        const float next = echo ? target : (std::min)(1.f, _gain + .5f);
        for (int i = 0; i < VoiceAudioFormat::FrameSamples; ++i)
            samples[i] = static_cast<int16_t>(samples[i] *
                (_gain + (next - _gain) * (i + 1) / VoiceAudioFormat::FrameSamples));
        _gain = next;
    }
};
