#pragma once
#include "NetworkProtocol.h"
#include "opus.h"

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
    if (packet.frameSamples != VOICE_SAMPLES_PER_PACKET ||
        packet.data.size() != 4 + VOICE_SAMPLES_PER_PACKET / 2 || packet.data[2] > 88)
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

public:
    cOpusCodec()
        : _encoder(NULL)
    {
        __int32 error = 0;
        _encoder = opus_encoder_create(VOICE_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
        if (error != 0 || !_encoder)
        {
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

    }

    cOpusCodec(const cOpusCodec&) = delete;
    cOpusCodec& operator=(const cOpusCodec&) = delete;

    bool available() const
    {
        return _encoder != NULL;
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

};
