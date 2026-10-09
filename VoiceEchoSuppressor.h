#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Detect speaker audio remaining after cancellation over 80 ms, allowing up to 200 ms acoustic
// delay. Decimation keeps this bounded; independent speech releases the duck.
class VoiceEchoSuppressor {
    std::array<float,1120> _speaker{};
    std::array<float,320> _microphone{};
    unsigned _frames=0;
    bool _echo=false;
    float _gain=1;
public:
    void reset(){_speaker.fill(0);_microphone.fill(0);_frames=0;_echo=false;_gain=1;}
    bool analyze(const int16_t* mic,const int16_t* speaker){
        std::move(_speaker.begin()+80,_speaker.end(),_speaker.begin());
        std::move(_microphone.begin()+80,_microphone.end(),_microphone.begin());
        for(int i=0;i<80;++i){float m=0,r=0;for(int j=0;j<4;++j){m+=mic[4*i+j];r+=speaker[4*i+j];}_microphone[240+i]=m*.25f;_speaker[1040+i]=r*.25f;}
        if(_frames<4){++_frames;_echo=false;return false;}
        double micEnergy=0,micSum=0;for(float x:_microphone){micEnergy+=x*x;micSum+=x;}
        micEnergy-=micSum*micSum/320;
        double best=0;
        if(micEnergy>320*25.*25.)for(int start=0;start<=800;start+=4){
            double dot=0,energy=0,sum=0;for(int i=0;i<320;++i){double r=_speaker[start+i];energy+=r*r;sum+=r;dot+=r*_microphone[i];}
            energy-=sum*sum/320;dot-=micSum*sum/320;
            if(energy>320*25.*25.)best=(std::max)(best,dot*dot/(energy*micEnergy));
        }
        // Squared correlation. Hysteresis avoids chopping between syllables.
        _echo=best>(_echo?.16:.36);
        return _echo;
    }
    void apply(int16_t* samples,bool echo){
        const float target=echo?.1259f:1.f; // -18 dB while echo dominates.
        const float next=echo?target:(std::min)(1.f,_gain+.25f);
        for(int i=0;i<320;++i)samples[i]=static_cast<int16_t>(samples[i]*(_gain+(next-_gain)*(i+1)/320));
        _gain=next;
    }
};

