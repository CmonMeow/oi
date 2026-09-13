#pragma once

#include "NetworkProtocol.h"
#include "opus.h"
#include <deque>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

static const __int32 IMA_INDEX_TABLE[16] =
{
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const __int32 IMA_STEP_TABLE[89] =
{
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static __int32 ClampInt(__int32 value, __int32 minValue, __int32 maxValue)
{
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

static unsigned char EncodeImaSample(__int16 sample, __int32& predictor, __int32& index)
{
    __int32 step = IMA_STEP_TABLE[index];
    __int32 diff = sample - predictor;
    unsigned char code = 0;
    if (diff < 0)
    {
        code = 8;
        diff = -diff;
    }

    __int32 delta = step >> 3;
    if (diff >= step)
    {
        code |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= (step >> 1))
    {
        code |= 2;
        diff -= step >> 1;
        delta += step >> 1;
    }
    if (diff >= (step >> 2))
    {
        code |= 1;
        delta += step >> 2;
    }

    predictor += (code & 8) ? -delta : delta;
    predictor = ClampInt(predictor, -32768, 32767);
    index = ClampInt(index + IMA_INDEX_TABLE[code & 15], 0, 88);
    return code & 15;
}

static __int16 DecodeImaSample(unsigned char code, __int32& predictor, __int32& index)
{
    __int32 step = IMA_STEP_TABLE[index];
    __int32 delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;

    predictor += (code & 8) ? -delta : delta;
    predictor = ClampInt(predictor, -32768, 32767);
    index = ClampInt(index + IMA_INDEX_TABLE[code & 15], 0, 88);
    return (__int16)predictor;
}

static void EncodeAdpcmVoicePacket(const __int16* samples, NetworkVoicePacket& packet)
{
    packet.codec = VOICE_CODEC_ADPCM;
    packet.sampleRate = VOICE_SAMPLE_RATE;
    packet.frameSamples = VOICE_SAMPLES_PER_PACKET;
    packet.data.assign(4 + VOICE_SAMPLES_PER_PACKET / 2, 0);
    __int32 predictor = samples[0];
    __int32 index = 0;
    packet.data[0] = (unsigned char)(predictor & 0xff);
    packet.data[1] = (unsigned char)((predictor >> 8) & 0xff);
    packet.data[2] = (unsigned char)index;
    packet.data[3] = 0;
    for (__int32 i = 0; i < VOICE_SAMPLES_PER_PACKET; i += 2)
    {
        unsigned char low = EncodeImaSample(samples[i], predictor, index);
        unsigned char high = 0;
        if (i + 1 < VOICE_SAMPLES_PER_PACKET)
        {
            high = EncodeImaSample(samples[i + 1], predictor, index);
        }
        packet.data[4 + i / 2] = low | (high << 4);
    }
}

static void DecodeAdpcmVoicePacket(const NetworkVoicePacket& packet, vector<__int16>& samples)
{
    if (packet.data.size() < 4)
    {
        samples.clear();
        return;
    }
    samples.resize(packet.frameSamples);
    __int32 predictor = (__int16)(packet.data[0] | (packet.data[1] << 8));
    __int32 index = packet.data[2];
    for (__int32 i = 0; i < (__int32)packet.frameSamples; i += 2)
    {
        if ((size_t)(4 + i / 2) >= packet.data.size())
        {
            break;
        }
        unsigned char packed = packet.data[4 + i / 2];
        samples[i] = DecodeImaSample(packed & 15, predictor, index);
        if (i + 1 < (__int32)packet.frameSamples)
        {
            samples[i + 1] = DecodeImaSample((packed >> 4) & 15, predictor, index);
        }
    }
}

class cOpusCodec
{
    OpusEncoder* _encoder;
    OpusDecoder* _decoder;

public:
    cOpusCodec()
        : _encoder(NULL),
          _decoder(NULL)
    {
        __int32 error = 0;
        _encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
        if (error != 0 || !_encoder)
        {
            return;
        }
        _decoder = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
        if (error != 0 || !_decoder)
        {
            opus_encoder_destroy(_encoder);
            _encoder = NULL;
            return;
        }
        opus_encoder_ctl(_encoder, OPUS_SET_BITRATE(20000));
        opus_encoder_ctl(_encoder, OPUS_SET_COMPLEXITY(4));
    }

    ~cOpusCodec()
    {
        if (_encoder)
        {
            opus_encoder_destroy(_encoder);
        }
        if (_decoder)
        {
            opus_decoder_destroy(_decoder);
        }
    }

    bool available() const
    {
        return _encoder != NULL && _decoder != NULL;
    }

    bool encode(const __int16* samples, NetworkVoicePacket& packet)
    {
        if (!available())
        {
            return false;
        }
        packet.codec = VOICE_CODEC_OPUS;
        packet.sampleRate = VOICE_SAMPLE_RATE;
        packet.frameSamples = VOICE_SAMPLES_PER_PACKET;
        packet.data.resize(VOICE_MAX_OPUS_BYTES);
        __int32 bytes = opus_encode(_encoder, samples, VOICE_SAMPLES_PER_PACKET, &packet.data[0], VOICE_MAX_OPUS_BYTES);
        if (bytes <= 0)
        {
            packet.data.clear();
            return false;
        }
        packet.data.resize(bytes);
        return true;
    }

    bool decode(const NetworkVoicePacket& packet, vector<__int16>& samples)
    {
        if (!available() || packet.codec != VOICE_CODEC_OPUS || packet.data.empty())
        {
            return false;
        }
        samples.resize(VOICE_SAMPLES_PER_PACKET);
        __int32 decoded = opus_decode(_decoder, &packet.data[0], (__int32)packet.data.size(), &samples[0], VOICE_SAMPLES_PER_PACKET, 0);
        if (decoded <= 0)
        {
            samples.clear();
            return false;
        }
        samples.resize(decoded);
        return true;
    }
};

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
    void collectCapture()
    {
        if (!_captureReady || !_recording) return;
        for (size_t count = 0; count < 4; ++count) {
            CaptureBuffer& buffer = _captureBuffers[_captureReadIndex];
            WAVEHDR& header = buffer.header;
            if (!(header.dwFlags & WHDR_DONE)) break;
            _captureReadIndex = (_captureReadIndex + 1) % 4;
            if (header.dwBytesRecorded >= sizeof(buffer.samples)) {
                NetworkVoicePacket packet;
                packet.playerId = -1;
                packet.sequence = _sequence++;
                if (!_opus.encode(buffer.samples, packet)) EncodeAdpcmVoicePacket(buffer.samples, packet);
                _captureQueue.push_back(packet);
                while (_captureQueue.size() > 16) _captureQueue.pop_front();
            }
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
        if (_playbackReady)
        {
            waveOutReset(_waveOut);
            cleanupPlaybackBuffers(true);
        }
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

        if (_playbackBuffers.size() >= 48) {
            cleanupPlaybackBuffers(true);
            if (_playbackBuffers.size() >= 48) return;
        }
        PlaybackBuffer* buffer = new PlaybackBuffer;
        ZeroMemory(&buffer->header, sizeof(buffer->header));
        if (packet.codec == VOICE_CODEC_OPUS)
        {
            _opus.decode(packet, buffer->samples);
        }
        else if (packet.codec == VOICE_CODEC_ADPCM)
        {
            DecodeAdpcmVoicePacket(packet, buffer->samples);
        }
        if (buffer->samples.empty())
        {
            delete buffer;
            return;
        }
        buffer->header.lpData = reinterpret_cast<LPSTR>(&buffer->samples[0]);
        buffer->header.dwBufferLength = (__int32)(buffer->samples.size() * sizeof(__int16));
        buffer->header.dwUser = reinterpret_cast<DWORD_PTR>(buffer);

        if (waveOutPrepareHeader(_waveOut, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            delete buffer;
            return;
        }
        if (waveOutWrite(_waveOut, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
        {
            waveOutUnprepareHeader(_waveOut, &buffer->header, sizeof(WAVEHDR));
            delete buffer;
            return;
        }
        _playbackBuffers.push_back(buffer);
    }

    void cleanupPlaybackBuffers(bool force = false)
    {
        if (force && _playbackReady && waveOutReset(_waveOut) != MMSYSERR_NOERROR) return;
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

    bool micEnabled() const { return _transmitEnabled && _recording && _captureReady; }

    void update(cNetworkRuntime& network, bool talkKeyDown, const PackedClientSettings& settings, bool micClicked = false)
    {
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

        collectCapture();
        NetworkVoicePacket packet;
        while (popCapturedPacket(packet))
        {
            if (_transmitEnabled && _recording && (network.clientReady() || network.isHost()))
            {
                network.sendVoice(packet);
                _lastVoiceSent = GetTickCount64();
            }
        }
        while (network.consumeVoicePacket(packet))
        {
            playPacket(packet);
        }
        cleanupPlaybackBuffers();
    }
};
