#pragma once

#include "NetworkProtocol.h"
#include "VoiceMixer.h"
#include "VoiceProcessing.h"
#include "VoiceCapture.h"
#include <algorithm>
#include <deque>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

class cVoiceChat
{
    struct PlaybackBuffer
    {
        WAVEHDR header = {};
        __int16 samples[VOICE_SAMPLES_PER_PACKET] = {};
        bool queued = false;
    };

    VoiceCapture _capture;
    HWAVEOUT _waveOut;
    PlaybackBuffer _playbackStorage[4];
    std::deque<PlaybackBuffer*> _playbackBuffers;
    std::deque<NetworkVoicePacket> _captureQueue;
    cOpusCodec _opus;
    cVoiceProcessing _processing;
    typedef cVoiceProcessing::Frame AudioFrame;
    cVoiceMixer _mixer;
    bool _networkWasActive = false;
    bool _recording;
    bool _transmitEnabled;
    bool _buttonTransmitEnabled = false;
    unsigned __int64 _lastVoiceSent = 0;
    bool _captureFailed = false;
    bool _captureReady;
    bool _playbackReady;
    unsigned __int32 _sequence;

    void collectCapture(bool pushToTalk)
    {
        if (!_recording) return;
        if (!_capture.update()) { captureFailed(); return; }
        if (_capture.takeOutputChange()) {
            closePlayback();openPlayback();
        }
        if (_capture.takeEchoReset()) _processing.reset();
        AudioFrame microphone, reference;
        while (_capture.pop(microphone, reference)) {
            _processing.process(microphone.data(), reference, pushToTalk);
            AudioFrame clean;
            while (_processing.pop(clean)) {
                NetworkVoicePacket packet;
                packet.playerId = -1;
                packet.sequence = _sequence++;
                if (!_opus.encode(clean.data(), packet)) EncodeAdpcmVoicePacket(clean.data(), packet);
                _captureQueue.push_back(packet);
                while (_captureQueue.size() > 16) _captureQueue.pop_front();
            }
        }
    }

    void captureFailed()
    {
        Error("WASAPI microphone capture failed");
        _captureFailed = true;
        _transmitEnabled = _buttonTransmitEnabled = false;
        stopRecording();
    }

    void openPlayback()
    {
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = VOICE_SAMPLE_RATE;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 2;
        format.nAvgBytesPerSec = VOICE_SAMPLE_RATE * 2;
        _playbackReady = waveOutOpen(&_waveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
    }

    void closePlayback()
    {
        if (!_playbackReady) return;
        cleanupPlaybackBuffers(true);
        for (auto& buffer : _playbackStorage) {
            if (buffer.header.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(_waveOut, &buffer.header, sizeof(WAVEHDR));
            buffer.header = {};
        }
        waveOutClose(_waveOut);_waveOut = NULL;_playbackReady = false;
    }

    void startRecording()
    {
        if (_recording || _captureFailed) return;
        _captureReady = _capture.start();
        if (!_captureReady) { captureFailed(); return; }
        _processing.reset(false);
        _recording = true;
    }

    void stopRecording()
    {
        _recording = false;
        _capture.stop();
        _captureQueue.clear();
        _processing.reset(false);
    }

    bool popCapturedPacket(NetworkVoicePacket& packet)
    {
        bool result = false;
        if (!_captureQueue.empty())
        {
            packet = _captureQueue.front();
            _captureQueue.pop_front();
            result = true;
        }
        return result;
    }

    void playPacket(const NetworkVoicePacket& packet)
    {
        if (!_playbackReady)
        {
            return;
        }

        _mixer.push(packet, GetTickCount64());
    }

    void servicePlayback()
    {
        if (!_playbackReady) return;
        cleanupPlaybackBuffers();
        // Keep output queued across ordinary UI scheduling jitter.
        while (_playbackBuffers.size() < 4)
        {
            AudioFrame frame = _mixer.render(GetTickCount64());
            if (!submitPlayback(frame)) break;
        }
    }

    bool submitPlayback(const AudioFrame& frame)
    {
        PlaybackBuffer* buffer = nullptr;
        for (auto& slot : _playbackStorage) if (!slot.queued) { buffer = &slot; break; }
        if (!buffer) return false;
        std::copy(frame.begin(), frame.end(), buffer->samples);
        buffer->header.lpData = reinterpret_cast<LPSTR>(&buffer->samples[0]);
        buffer->header.dwBufferLength = sizeof(buffer->samples);
        buffer->header.dwUser = reinterpret_cast<DWORD_PTR>(buffer);

        if (!(buffer->header.dwFlags & WHDR_PREPARED) &&
            waveOutPrepareHeader(_waveOut, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            return false;
        }
        if (waveOutWrite(_waveOut, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            return false;
        }
        buffer->queued = true;
        _playbackBuffers.push_back(buffer);
        return true;
    }

    void cleanupPlaybackBuffers(bool force = false)
    {
        if (force && _playbackReady && waveOutReset(_waveOut) != MMSYSERR_NOERROR) return;
        if (force)
        {
            _mixer.clear();
            _processing.resetEcho();
        }
        while (!_playbackBuffers.empty())
        {
            PlaybackBuffer* buffer = _playbackBuffers.front();
            if (!force && !(buffer->header.dwFlags & WHDR_DONE))
            {
                break;
            }
            buffer->queued = false;
            _playbackBuffers.pop_front();
        }
    }

public:
    cVoiceChat()
        : _waveOut(NULL),
          _recording(false),
          _transmitEnabled(false),
          _captureReady(false),
          _playbackReady(false),
          _sequence(0)
    {
        static_assert(VOICE_SAMPLE_RATE == cVoiceProcessing::SampleRate &&
            VOICE_SAMPLES_PER_PACKET == cVoiceProcessing::FrameSamples &&
            VOICE_SAMPLE_RATE == VoiceCapture::SampleRate &&
            VOICE_SAMPLES_PER_PACKET == VoiceCapture::FrameSamples, "Voice DSP format mismatch");
        openPlayback();
    }

    ~cVoiceChat()
    {
        stopRecording();
        closePlayback();
    }

    bool transmitting(const cNetworkRuntime& network) const
    {
        return _transmitEnabled && _captureReady && _recording &&
               (network.clientReady() || network.isHost()) &&
               _lastVoiceSent != 0 && GetTickCount64() - _lastVoiceSent < 250;
    }

    void resetTransmitMode()
    {
        _transmitEnabled = false;
        _buttonTransmitEnabled = false;
        _lastVoiceSent = 0;
        stopRecording();
        NetworkVoicePacket discarded;
        while (popCapturedPacket(discarded)) {}
    }

    bool speaking(__int32 player, const cNetworkRuntime& network) const
    {
        return player == network.localPlayerId() ? transmitting(network) : _mixer.speaking(player, GetTickCount64());
    }

    bool micEnabled() const { return _transmitEnabled && _recording && _captureReady; }

    void update(cNetworkRuntime& network, bool talkKeyDown, const PackedClientSettings& settings, bool micClicked = false)
    {
        const bool networkActive = network.clientReady() || network.isHost();
        if (_networkWasActive && !networkActive) cleanupPlaybackBuffers(true);
        _networkWasActive = networkActive;
        if (!settings.voiceEnabled())
        {
            _transmitEnabled = false;
            _buttonTransmitEnabled = false;
            stopRecording();
            NetworkVoicePacket packet;
            while (network.consumeVoicePacket(packet))
            {
            }
            cleanupPlaybackBuffers(true);
            return;
        }

        if (!talkKeyDown) _captureFailed = false;
        if (micClicked) _buttonTransmitEnabled = !_buttonTransmitEnabled;
        // PTT takes over from a latched mic; releasing the key must mute it.
        if (talkKeyDown) _buttonTransmitEnabled = false;
        _transmitEnabled = _buttonTransmitEnabled || talkKeyDown;

        if (_transmitEnabled)
        {
            startRecording();
        }
        else
        {
            stopRecording();
        }

        NetworkVoicePacket packet;
        while (network.consumeVoicePacket(packet)) playPacket(packet);
        servicePlayback();
        collectCapture(talkKeyDown);
        while (popCapturedPacket(packet))
        {
            if (_transmitEnabled && _recording && (network.clientReady() || network.isHost()))
            {
                network.sendVoice(packet);
                _lastVoiceSent = GetTickCount64();
            }
        }

    }
};
