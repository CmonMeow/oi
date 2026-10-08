#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <array>
#include <deque>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <cstdlib>

// Both WASAPI streams use the same QPC timeline and Windows' sample converter.
// The speaker mix is a local AEC reference only; it is never a voice packet.
class VoiceCapture
{
public:
    enum { SampleRate = 16000, FrameSamples = 320, ReferenceLead = 320 };
    using Frame = std::array<int16_t, FrameSamples>;
private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Stream {
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        std::wstring id;
        void close() { if(client)client->Stop();capture.Reset();client.Reset();id.clear(); }
    };
    struct SpeakerBlock { int64_t start; std::vector<int16_t> samples; };
    struct MicrophoneFrame { int64_t start; Frame samples; };
    HRESULT _com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ComPtr<IMMDeviceEnumerator> _devices;
    Stream _microphone, _speakers;
    std::wstring _microphoneId, _speakerId;
    std::deque<SpeakerBlock> _reference;
    std::deque<MicrophoneFrame> _pending;
    Frame _partial = {};
    size_t _partialCount = 0;
    int64_t _partialStart = 0;
    ULONGLONG _nextDeviceCheck = 0;
    bool _running = false, _resetEcho = false, _outputChanged = false;
    bool _haveMicrophone = false, _referenceFailed = false;

    static std::wstring deviceId(IMMDevice* device) {
        LPWSTR text = nullptr;
        if(!device || FAILED(device->GetId(&text)))return {};
        std::wstring id(text);CoTaskMemFree(text);return id;
    }
    HRESULT open(Stream& stream, IMMDevice* device, bool loopback) {
        stream.close();
        if(!device)return E_POINTER;
        HRESULT result=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,
            reinterpret_cast<void**>(stream.client.GetAddressOf()));
        WAVEFORMATEX format={WAVE_FORMAT_PCM,1,SampleRate,SampleRate*2,2,16,0};
        if(SUCCEEDED(result))result=stream.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY|
            (loopback?AUDCLNT_STREAMFLAGS_LOOPBACK:0),2000000,0,&format,nullptr);
        if(SUCCEEDED(result))result=stream.client->GetService(IID_PPV_ARGS(&stream.capture));
        if(SUCCEEDED(result))result=stream.client->Start();
        if(FAILED(result)){stream.close();return result;}
        stream.id=deviceId(device);return S_OK;
    }
    void discardMicrophone() { _partialCount=0;_partial.fill(0);_pending.clear(); }
    bool refreshDevices() {
        _nextDeviceCheck=GetTickCount64()+1000;
        ComPtr<IMMDevice> microphone,speakers;
        HRESULT result=_devices->GetDefaultAudioEndpoint(eCapture,eMultimedia,&microphone);
        if(FAILED(result))return false;
        if(!_microphone.client || deviceId(microphone.Get())!=_microphone.id) {
            discardMicrophone();_haveMicrophone=false;
            if(deviceId(microphone.Get())!=_microphoneId)_resetEcho=true;
            result=open(_microphone,microphone.Get(),false);
            if(FAILED(result)){Error("WASAPI microphone initialization failed (0x%08lX)",result);return false;}
            _microphoneId=_microphone.id;
        }
        result=_devices->GetDefaultAudioEndpoint(eRender,eMultimedia,&speakers);
        if(FAILED(result) || !_speakers.client || deviceId(speakers.Get())!=_speakers.id) {
            _reference.clear();
            // Reopen voice playback as well when Windows changes the default output.
            if(deviceId(speakers.Get())!=_speakerId){_outputChanged=true;_resetEcho=true;}
            if(SUCCEEDED(result))result=open(_speakers,speakers.Get(),true);
            else _speakers.close();
            if(FAILED(result)) {
                if(!_referenceFailed)Error("WASAPI speaker reference unavailable (0x%08lX)",result);
                _referenceFailed=true;
            } else {_referenceFailed=false;_speakerId=_speakers.id;}
        }
        return true;
    }
    void appendMicrophone(int64_t start,const int16_t* samples,UINT32 count,bool silent) {
        for(UINT32 offset=0;offset<count;) {
            if(!_partialCount)_partialStart=start+offset;
            else if(std::abs(start+offset-(_partialStart+static_cast<int64_t>(_partialCount)))>FrameSamples/2) {
                discardMicrophone();_partialStart=start+offset;_resetEcho=true;
            }
            const size_t take=(std::min)(static_cast<size_t>(count-offset),FrameSamples-_partialCount);
            if(silent)std::fill_n(_partial.begin()+_partialCount,take,0);
            else std::copy_n(samples+offset,take,_partial.begin()+_partialCount);
            _partialCount+=take;offset+=static_cast<UINT32>(take);
            if(_partialCount==FrameSamples) {
                _pending.push_back({_partialStart,_partial});_partialCount=0;
                if(_pending.size()>8){_pending.pop_front();_resetEcho=true;}
            }
        }
    }
    bool drain(Stream& stream,bool loopback) {
        if(!stream.capture)return loopback;
        // The endpoint holds at most 200 ms; keep work bounded after a UI stall.
        for(unsigned packet=0;packet<32;++packet) {
            BYTE* data=nullptr;UINT32 count=0;DWORD flags=0;UINT64 device=0,qpc=0;
            HRESULT result=stream.capture->GetBuffer(&data,&count,&flags,&device,&qpc);
            if(result==AUDCLNT_S_BUFFER_EMPTY)return true;
            if(FAILED(result))return false;
            if(!count)return true;
            bool valid=count<=SampleRate && !(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) && qpc;
            bool silent=(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0;
            valid=valid&&(silent||data);
            if(valid) {
                const int64_t start=static_cast<int64_t>((qpc+312)/625); // 100 ns -> 16 kHz
                if(loopback) {
                    SpeakerBlock block{start,std::vector<int16_t>(count,0)};
                    if(!silent)std::copy_n(reinterpret_cast<int16_t*>(data),count,block.samples.begin());
                    _reference.push_back(std::move(block));
                    while(_reference.size()>200 || (!_reference.empty()&&start-_reference.front().start>SampleRate*2))_reference.pop_front();
                } else {
                    if(_haveMicrophone&&(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)){discardMicrophone();_resetEcho=true;}
                    _haveMicrophone=true;
                    appendMicrophone(start,reinterpret_cast<int16_t*>(data),count,silent);
                }
            } else if(!loopback){discardMicrophone();_resetEcho=true;}
            result=stream.capture->ReleaseBuffer(count);
            if(FAILED(result))return false;
        }
        return true;
    }
public:
    VoiceCapture() = default;
    VoiceCapture(const VoiceCapture&)=delete;
    VoiceCapture& operator=(const VoiceCapture&)=delete;
    ~VoiceCapture(){stop();_devices.Reset();if(SUCCEEDED(_com))CoUninitialize();}
    static int64_t clockSamples() {
        LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
        return (now.QuadPart/frequency.QuadPart)*SampleRate+
            (now.QuadPart%frequency.QuadPart)*SampleRate/frequency.QuadPart;
    }
    bool start() {
        if(_running)return true;
        if(!_devices && FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&_devices))))return false;
        if(!refreshDevices()){stop();return false;}
        _running=true;return true;
    }
    void stop() {
        _microphone.close();_speakers.close();discardMicrophone();_reference.clear();
        _running=false;_haveMicrophone=false;_outputChanged=false;
    }
    bool update() {
        if(!_running)return false;
        if(GetTickCount64()>=_nextDeviceCheck && !refreshDevices())return false;
        if(!drain(_speakers,true)) {
            _speakers.close();_reference.clear();_resetEcho=true;_nextDeviceCheck=0;
            if(!_referenceFailed)Error("WASAPI speaker reference interrupted");
            _referenceFailed=true;
        }
        return drain(_microphone,false);
    }
    bool takeEchoReset(){bool value=_resetEcho;_resetEcho=false;return value;}
    bool takeOutputChange(){bool value=_outputChanged;_outputChanged=false;return value;}
    bool pop(Frame& microphone,Frame& reference) {
        if(_pending.empty())return false;
        const auto& frame=_pending.front();
        // Delay processing, not timestamps, until the speaker reference has arrived.
        // One frame of lookahead lets the adaptive filter handle small device offsets.
        if(clockSamples()<frame.start+FrameSamples+ReferenceLead+FrameSamples/2)return false;
        microphone=frame.samples;reference.fill(0);
        const int64_t start=frame.start+ReferenceLead;
        for(const auto& block:_reference) {
            int64_t begin=(std::max)(start,block.start);
            int64_t end=(std::min)(start+FrameSamples,block.start+static_cast<int64_t>(block.samples.size()));
            if(begin<end)std::copy_n(block.samples.begin()+(begin-block.start),static_cast<size_t>(end-begin),reference.begin()+(begin-start));
        }
        _pending.pop_front();return true;
    }
};
