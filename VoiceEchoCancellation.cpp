#include "VoiceEchoCancellation.h"
#include "VoiceAudioFormat.h"
#include "api/environment/environment_factory.h"
#include "common_audio/vad/include/webrtc_vad.h"
#include "modules/audio_processing/aec3/echo_canceller3.h"
#include "modules/audio_processing/audio_buffer.h"
#include "modules/audio_processing/high_pass_filter.h"
#include "modules/audio_processing/ns/noise_suppressor.h"
#include <array>
#include <xmmintrin.h>

namespace {
// x64 MSVC does not enable WebRTC's clang-only denormal guard. Filters can
// decay into subnormal floats during silence, making DSP much more expensive.
// Scope FTZ/DAZ to audio work and preserve the calling thread's settings.
class ScopedAudioFloatMode {
    const unsigned _saved = _mm_getcsr();
public:
    ScopedAudioFloatMode() { _mm_setcsr(_saved | 0x8040); }
    ~ScopedAudioFloatMode() { _mm_setcsr(_saved); }
};

webrtc::EchoCanceller3Config echoConfig() {
    webrtc::EchoCanceller3Config config;
    // WASAPI's digital reference can be quiet even when laptop speakers
    // produce a loud acoustic echo. These thresholds were checked against
    // saved laptop captures; they do not amplify either audio stream.
    config.render_levels.active_render_limit = 25.f;
    config.render_levels.poor_excitation_render_limit = 37.5f;
    config.filter.refined.noise_gate /= 16;
    config.filter.refined_initial.noise_gate /= 16;
    config.filter.coarse.noise_gate /= 16;
    config.filter.coarse_initial.noise_gate /= 16;
    config.echo_model.noise_gate_power /= 16;
    config.echo_model.min_noise_floor_power /= 16;
    config.ep_strength.default_gain = 16.f;
    return config;
}
}

struct VoiceEchoCancellation::State {
    webrtc::Environment environment = webrtc::CreateEnvironment();
    webrtc::EchoCanceller3 echo{environment, echoConfig(), std::nullopt, nullptr, 16000, 1, 1};
    webrtc::AudioBuffer render{16000,1,16000,1,16000,1};
    webrtc::AudioBuffer capture{16000,1,16000,1,16000,1};
    webrtc::HighPassFilter highPass{16000,1};
    std::unique_ptr<webrtc::NoiseSuppressor> noise;
    VadInst* vad = WebRtcVad_Create();
    State() { resetPostprocessing(); }
    ~State() { if(vad)WebRtcVad_Free(vad); }
    void resetPostprocessing() {
        highPass.Reset();
        noise = std::make_unique<webrtc::NoiseSuppressor>(webrtc::NsConfig{},16000,1);
        if(vad){WebRtcVad_Init(vad);WebRtcVad_set_mode(vad,1);}
    }
    void block(const int16_t* mic, const int16_t* reference, int16_t* output) {
        const webrtc::StreamConfig format(16000,1);
        render.CopyFrom(reference,format);
        echo.AnalyzeRender(&render);
        capture.CopyFrom(mic,format);
        highPass.Process(&capture,false);
        echo.AnalyzeCapture(&capture);
        noise->Analyze(capture);
        // Capture and render were already aligned by WASAPI QPC timestamps.
        echo.SetAudioBufferDelay(0);
        echo.ProcessCapture(&capture,false);
        noise->Process(&capture);
        capture.CopyTo(format,output);
    }
};

VoiceEchoCancellation::VoiceEchoCancellation():_state(std::make_unique<State>()) {}
VoiceEchoCancellation::~VoiceEchoCancellation() = default;
void VoiceEchoCancellation::reset(bool resetFilter) {
    ScopedAudioFloatMode floatMode;
    if(resetFilter){_state=std::make_unique<State>();return;}
    // Flush internal delayed microphone audio at mute/PTT boundaries without
    // throwing away the learned acoustic filter. No flushed samples are sent.
    std::array<int16_t,160> silence{},discarded{};
    for(int i=0;i<20;++i)_state->block(silence.data(),silence.data(),discarded.data());
    _state->resetPostprocessing();
}
bool VoiceEchoCancellation::process(const int16_t* mic,const int16_t* reference,int16_t* output) {
    ScopedAudioFloatMode floatMode;
    static_assert(VoiceAudioFormat::SampleRate==16000 && VoiceAudioFormat::FrameSamples==320,"AEC format mismatch");
    for(int offset=0;offset<320;offset+=160)_state->block(mic+offset,reference+offset,output+offset);
    return _state->vad && WebRtcVad_Process(_state->vad,16000,output,320)==1;
}
