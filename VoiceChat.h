#pragma once

#include "NetworkProtocol.h"
#include "VoiceMixer.h"
#include "VoiceProcessing.h"
#include <algorithm>
#include <deque>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

class cVoiceChat
{
    struct CaptureBuffer
    {
        WAVEHDR header;
        __int16 samples[VOICE_SAMPLES_PER_PACKET];
    };

    struct PlaybackBuffer
    {
        WAVEHDR header;
        vector<__int16> samples;
    };

    HWAVEIN _waveIn;
    HWAVEOUT _waveOut;
    CaptureBuffer _captureBuffers[4];
    std::deque<PlaybackBuffer*> _playbackBuffers;
    CRITICAL_SECTION _queueLock;
    std::deque<NetworkVoicePacket> _captureQueue;
    cOpusCodec _opus;
    cVoiceProcessing _processing;
    typedef cVoiceProcessing::Frame AudioFrame;
    struct ReferenceFrame { unsigned __int64 start; AudioFrame samples; };
    cVoiceMixer _mixer;
    std::deque<ReferenceFrame> _playbackHistory;
    unsigned __int64 _playbackSubmitted = 0;
    unsigned __int64 _captureConsumed = 0;
    unsigned __int64 _inputPosition = 0, _outputPosition = 0;
    bool _positionFailed = false;
    bool _networkWasActive = false;

    // WinMM positions are 32-bit sample counters. Extend wraps before comparing
    // them with the 64-bit capture and playback histories.
    static unsigned __int64 extendPosition(DWORD sample, unsigned __int64& previous)
    {
        previous += static_cast<DWORD>(sample - static_cast<DWORD>(previous));
        return previous;
    }

    static bool positionSamples(const MMTIME& time, unsigned __int64& counter, unsigned __int64& samples)
    {
        switch (time.wType)
        {
        case TIME_SAMPLES: samples = extendPosition(time.u.sample, counter); return true;
        case TIME_BYTES: samples = extendPosition(time.u.cb, counter) / sizeof(__int16); return true;
        case TIME_MS: samples = extendPosition(time.u.ms, counter) * VOICE_SAMPLE_RATE / 1000; return true;
        default: return false;
        }
    }

    bool devicePositions(unsigned __int64& input, unsigned __int64& output)
    {
        MMTIME in = {}, out = {};
        in.wType = out.wType = TIME_SAMPLES;
        if (!_playbackReady || waveInGetPosition(_waveIn, &in, sizeof(in)) != MMSYSERR_NOERROR ||
            waveOutGetPosition(_waveOut, &out, sizeof(out)) != MMSYSERR_NOERROR ||
            !positionSamples(in, _inputPosition, input) || !positionSamples(out, _outputPosition, output))
        {
            if (!_positionFailed) Error("Audio device sample positions unavailable; echo reference disabled");
            _positionFailed = true;
            return false;
        }
        _positionFailed = false;
        return true;
    }

    AudioFrame playbackReference(__int64 start) const
    {
        AudioFrame reference = {};
        for (const ReferenceFrame& frame : _playbackHistory)
        {
            __int64 offset = static_cast<__int64>(frame.start) - start;
            if (offset >= VOICE_SAMPLES_PER_PACKET) break;
            if (offset <= -VOICE_SAMPLES_PER_PACKET) continue;
            for (int i = 0; i < VOICE_SAMPLES_PER_PACKET; ++i)
                if (offset + i >= 0 && offset + i < VOICE_SAMPLES_PER_PACKET)
                    reference[static_cast<size_t>(offset + i)] = frame.samples[i];
        }
        return reference;
    }
    bool _recording;
    bool _transmitEnabled;
    bool _buttonTransmitEnabled = false;
    unsigned __int64 _lastVoiceSent = 0;
    bool _captureFailed = false;
    size_t _captureReadIndex = 0;
    bool _captureReady;
    bool _playbackReady;
    unsigned __int32 _sequence;

    // All application capture state and codec access stays on the UI thread.
    void collectCapture(bool pushToTalk)
    {
        if (!_captureReady || !_recording) return;
        unsigned __int64 inputNow = 0, outputNow = 0;
        const bool havePositions = devicePositions(inputNow, outputNow);
        for (size_t count = 0; count < 4; ++count) {
            CaptureBuffer& buffer = _captureBuffers[_captureReadIndex];
            WAVEHDR& header = buffer.header;
            if (!(header.dwFlags & WHDR_DONE)) break;
            // A buffer may finish after the position snapshot was taken.
            if (havePositions && inputNow < _captureConsumed + header.dwBytesRecorded / sizeof(__int16)) break;
            _captureReadIndex = (_captureReadIndex + 1) % 4;
            if (header.dwBytesRecorded >= sizeof(buffer.samples)) {
                // Subtract the capture backlog from the current output position:
                // use what the device played, not what just arrived over the network.
                AudioFrame reference = {};
                if (havePositions && inputNow >= _captureConsumed)
                    reference = playbackReference(static_cast<__int64>(outputNow) -
                        static_cast<__int64>(inputNow - _captureConsumed));
                _processing.process(buffer.samples, reference, pushToTalk);
                AudioFrame clean;
                while (_processing.pop(clean))
                {
                    NetworkVoicePacket packet;
                    packet.playerId = -1;
                    packet.sequence = _sequence++;
                    if (!_opus.encode(clean.data(), packet)) EncodeAdpcmVoicePacket(clean.data(), packet);
                    _captureQueue.push_back(packet);
                    while (_captureQueue.size() > 16) _captureQueue.pop_front();
                }
            }
            _captureConsumed += header.dwBytesRecorded / sizeof(__int16);
            header.dwBytesRecorded = 0;
            if (waveInAddBuffer(_waveIn, &header, sizeof(header)) != MMSYSERR_NOERROR) {
                captureFailed(); return;
            }
        }
    }

    void captureFailed()
    {
        Error("Microphone start or buffer submission failed");
        _captureFailed = true;
        _transmitEnabled = _buttonTransmitEnabled = false;
        stopRecording();
    }

    void openDevices()
    {
        WAVEFORMATEX format;
        ZeroMemory(&format, sizeof(format));
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = VOICE_SAMPLE_RATE;
        format.wBitsPerSample = 16;
        format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        openCapture(WAVE_MAPPER);

        _playbackReady = waveOutOpen(&_waveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
    }

    bool openCapture(UINT device)
    {
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = VOICE_SAMPLE_RATE;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 2;
        format.nAvgBytesPerSec = format.nSamplesPerSec * 2;
        _captureReady = waveInOpen(&_waveIn, device, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
        if (_captureReady)
        {
            for (size_t i = 0; i < 4; ++i)
            {
                ZeroMemory(&_captureBuffers[i], sizeof(_captureBuffers[i]));
                _captureBuffers[i].header.lpData = reinterpret_cast<LPSTR>(_captureBuffers[i].samples);
                _captureBuffers[i].header.dwBufferLength = sizeof(_captureBuffers[i].samples);
                if (waveInPrepareHeader(_waveIn, &_captureBuffers[i].header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
                {
                    for (size_t j = 0; j < i; ++j)
                        waveInUnprepareHeader(_waveIn, &_captureBuffers[j].header, sizeof(WAVEHDR));
                    waveInClose(_waveIn);
                    _waveIn = NULL;
                    _captureReady = false;
                    return false;
                }
            }
        }

        return _captureReady;
    }

    void closeCapture()
    {
        stopRecording();
        if (_captureReady)
        {
            waveInReset(_waveIn);
            for (size_t i = 0; i < 4; ++i)
                waveInUnprepareHeader(_waveIn, &_captureBuffers[i].header, sizeof(WAVEHDR));
            waveInClose(_waveIn);
        }
        _waveIn = NULL;
        _captureReady = false;
        EnterCriticalSection(&_queueLock);
        _captureQueue.clear();
        LeaveCriticalSection(&_queueLock);
    }

    void startRecording()
    {
        if (!_captureReady || _recording || _captureFailed)
        {
            return;
        }
        _captureConsumed = _inputPosition = 0;
        _processing.reset();
        _captureReadIndex = 0;
        _recording = true;
        for (size_t i = 0; i < 4; ++i)
        {
            _captureBuffers[i].header.dwBytesRecorded = 0;
            if (waveInAddBuffer(_waveIn, &_captureBuffers[i].header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                captureFailed(); return;
            }
        }
        if (waveInStart(_waveIn) != MMSYSERR_NOERROR) captureFailed();
    }

    void stopRecording()
    {
        if (!_captureReady || !_recording)
        {
            return;
        }
        _recording = false;
        waveInStop(_waveIn);
        waveInReset(_waveIn);
        _captureQueue.clear();
        _processing.reset();
    }

    bool popCapturedPacket(NetworkVoicePacket& packet)
    {
        bool result = false;
        EnterCriticalSection(&_queueLock);
        if (!_captureQueue.empty())
        {
            packet = _captureQueue.front();
            _captureQueue.pop_front();
            result = true;
        }
        LeaveCriticalSection(&_queueLock);
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
        bool underrun = !_playbackBuffers.empty();
        for (PlaybackBuffer* buffer : _playbackBuffers)
            if (!(buffer->header.dwFlags & WHDR_DONE)) underrun = false;
        cleanupPlaybackBuffers();
        if (underrun) _processing.resetEcho();
        // Keep a continuous output sample clock, including silence. A short
        // queue lets capture map to the actual speaker timeline for AEC.
        while (_playbackBuffers.size() < 3)
        {
            AudioFrame frame = _mixer.render(GetTickCount64());
            if (!submitPlayback(frame)) break;
        }
    }

    bool submitPlayback(const AudioFrame& frame)
    {
        PlaybackBuffer* buffer = new PlaybackBuffer;
        ZeroMemory(&buffer->header, sizeof(buffer->header));
        buffer->samples.assign(frame.begin(), frame.end());
        buffer->header.lpData = reinterpret_cast<LPSTR>(&buffer->samples[0]);
        buffer->header.dwBufferLength = (__int32)(buffer->samples.size() * sizeof(__int16));
        buffer->header.dwUser = reinterpret_cast<DWORD_PTR>(buffer);

        if (waveOutPrepareHeader(_waveOut, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            delete buffer;
            return false;
        }
        if (waveOutWrite(_waveOut, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            waveOutUnprepareHeader(_waveOut, &buffer->header, sizeof(WAVEHDR));
            delete buffer;
            return false;
        }
        _playbackBuffers.push_back(buffer);
        _playbackHistory.push_back({ _playbackSubmitted, frame });
        _playbackSubmitted += VOICE_SAMPLES_PER_PACKET;
        while (_playbackHistory.size() > 100) _playbackHistory.pop_front();
        return true;
    }

    void cleanupPlaybackBuffers(bool force = false)
    {
        if (force && _playbackReady && waveOutReset(_waveOut) != MMSYSERR_NOERROR) return;
        if (force)
        {
            _mixer.clear();
            _playbackHistory.clear();
            _playbackSubmitted = _outputPosition = 0;
            _processing.resetEcho();
        }
        while (!_playbackBuffers.empty())
        {
            PlaybackBuffer* buffer = _playbackBuffers.front();
            if (!force && !(buffer->header.dwFlags & WHDR_DONE))
            {
                break;
            }
            if (waveOutUnprepareHeader(_waveOut, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return;
            _playbackBuffers.pop_front();
            delete buffer;
        }
    }

public:
    cVoiceChat()
        : _waveIn(NULL),
          _waveOut(NULL),
          _recording(false),
          _transmitEnabled(false),
          _captureReady(false),
          _playbackReady(false),
          _sequence(0)
    {
        static_assert(VOICE_SAMPLE_RATE == cVoiceProcessing::SampleRate &&
            VOICE_SAMPLES_PER_PACKET == cVoiceProcessing::FrameSamples, "Voice DSP format mismatch");
        InitializeCriticalSection(&_queueLock);
        openDevices();
    }

    ~cVoiceChat()
    {
        closeCapture();
        if (_playbackReady)
        {
            waveOutReset(_waveOut);
            cleanupPlaybackBuffers(true);
            waveOutClose(_waveOut);
        }
        DeleteCriticalSection(&_queueLock);
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
