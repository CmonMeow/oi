// Offline regression tests. Build as a separate console test in build/x64,
// linking third_party/opus/lib/x64/Release/opus.lib. No audio devices are opened.
#include "VoiceMixer.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static NetworkVoicePacket constant(int player, uint32_t sequence, int16_t value)
{
    cVoiceMixer::Frame frame; frame.fill(value);
    NetworkVoicePacket packet;
    packet.playerId = player; packet.sequence = sequence;
    EncodeAdpcmVoicePacket(frame.data(), packet);
    return packet;
}

int main()
{
    // Ten simulated minutes, eight simultaneous speakers, one output frame
    // per 20 ms. Every speaker must be present for the entire run.
    cVoiceMixer mixer;
    for (uint32_t f = 0; f < 30000; ++f)
    {
        for (int p = 0; p < 8; ++p) mixer.push(constant(p, f, 1000), f * 20ull);
        auto output = mixer.render(f * 20ull);
        for (auto sample : output) assert(sample == (f < 2 ? 0 : 8000));
    }
    mixer.clear();
    for (auto sample : mixer.render(600000)) assert(sample == 0);
    puts("PASS: 10 minutes, eight simultaneous speakers, bounded continuous playback");

    // Independent Opus states: the mix must equal two independent decoders,
    // even when network arrivals are reversed and duplicated.
    cOpusCodec encoders[2];
    OpusDecoder* reference[2];
    for (int p = 0; p < 2; ++p) { int error; reference[p] = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error); assert(error == OPUS_OK); }
    std::deque<cVoiceMixer::Frame> expected;
    for (uint32_t batch = 0; batch < 300; ++batch)
    {
        NetworkVoicePacket packets[2][3];
        for (int f = 0; f < 3; ++f)
        {
            cVoiceMixer::Frame sum = {};
            for (int p = 0; p < 2; ++p)
            {
                cVoiceMixer::Frame pcm, decoded;
                for (int i = 0; i < VOICE_SAMPLES_PER_PACKET; ++i)
                    pcm[i] = (int16_t)(1000 * sin((batch * 3 * 320 + f * 320 + i) * (p ? .12 : .07)));
                auto& packet = packets[p][f]; packet.playerId = p; packet.sequence = batch * 3 + f;
                assert(encoders[p].encode(pcm.data(), packet));
                assert(opus_decode(reference[p], packet.data.data(), (int)packet.data.size(), decoded.data(), 320, 0) == 320);
                for (int i = 0; i < 320; ++i) sum[i] += decoded[i];
            }
            expected.push_back(sum);
        }
        for (int p = 0; p < 2; ++p)
            for (int f : {1, 0, 2, 1}) mixer.push(packets[p][f], batch * 60ull);
        for (int f = 0; f < 3; ++f)
        {
            assert(mixer.render(batch * 60ull + f * 20) == expected.front());
            expected.pop_front();
        }
    }
    for (auto decoder : reference) opus_decoder_destroy(decoder);
    puts("PASS: separate Opus states, reordered packets, duplicates, burst arrivals");

    mixer.clear();
    for (uint32_t i = 0; i < 4; ++i) mixer.push(constant(1, 0xfffffffeu + i, 1000 + (int16_t)i), 0);
    for (int i = 0; i < 4; ++i) assert(mixer.render(i * 20)[0] == 1000 + i);
    // Late duplicates must not replay. A single short utterance must not wait forever.
    mixer.push(constant(1, 0xffffffffu, 9999), 100);
    assert(mixer.render(100)[0] == 0);
    mixer.push(constant(1, 2, 2000), 120);
    assert(mixer.render(120)[0] == 0);
    assert(mixer.render(160)[0] == 2000);
    puts("PASS: sequence wrap, late packets, short utterances");

    mixer.clear();
    for (int f = 0; f < 100; ++f) mixer.push(constant(1, f, 1000), 0);
    for (int f = 0; f < 3; ++f) mixer.push(constant(2, f, 2000), 0);
    for (int f = 0; f < 3; ++f) assert(mixer.render(f * 20)[0] == 3000);
    for (int f = 0; f < 20; ++f) mixer.render(60 + f * 20);
    assert(mixer.render(500)[0] == 0);
    mixer.clear();
    for (int p = 0; p < 8; ++p)
        for (int f = 0; f < 3; ++f) mixer.push(constant(p, f, 20000), 0);
    for (auto sample : mixer.render(0)) assert(sample >= 29999 && sample <= 30000);
    assert(mixer.render(31000)[0] == 0);
    puts("PASS: bounded per-speaker backlog, clipping protection, stale stream cleanup");

    mixer.clear();
    cOpusCodec encoder;
    int error;
    OpusDecoder* lossReference = opus_decoder_create(VOICE_SAMPLE_RATE, 1, &error);
    assert(error == OPUS_OK);
    for (uint32_t f = 0; f < 7; ++f)
    {
        cVoiceMixer::Frame pcm;
        for (int i = 0; i < 320; ++i) pcm[i] = (int16_t)(1200 * sin((f * 320 + i) * .1));
        NetworkVoicePacket packet; packet.playerId = 1; packet.sequence = f;
        assert(encoder.encode(pcm.data(), packet));
        if (f != 3) mixer.push(packet, 0);
        cVoiceMixer::Frame decoded = {};
        assert(opus_decode(lossReference, f == 3 ? NULL : packet.data.data(),
            f == 3 ? 0 : (int)packet.data.size(), decoded.data(), 320, 0) == 320);
        expected.push_back(decoded);
    }
    for (int f = 0; f < 7; ++f)
    {
        assert(mixer.render(f * 20) == expected.front()); expected.pop_front();
    }
    opus_decoder_destroy(lossReference);
    auto malformed = constant(1, 0, 1000);
    malformed.data[2] = 255;
    vector<__int16> decoded;
    DecodeAdpcmVoicePacket(malformed, decoded); assert(decoded.empty());
    puts("PASS: Opus packet loss concealment and invalid ADPCM rejection");
}
