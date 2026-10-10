#pragma once
#include <string>

struct TransferProgress {
    unsigned long long sampledAt=0, sampledBytes=0, lastProgress=0;
    double bytesPerSecond=0;
    void update(unsigned long long bytes,unsigned long long now) {
        lastProgress=now;
        if(!sampledAt){sampledAt=now;sampledBytes=bytes;return;}
        if(now-sampledAt<1000)return;
        const double rate=(bytes-sampledBytes)*1000.0/(now-sampledAt);
        bytesPerSecond=bytesPerSecond?bytesPerSecond*.75+rate*.25:rate;
        sampledAt=now;sampledBytes=bytes;
    }
    long long remaining(unsigned long long size,unsigned long long bytes,unsigned long long now) const {
        if(bytes>=size)return 0;
        if(!bytesPerSecond||now-lastProgress>3000)return -1;
        return (long long)((size-bytes)/bytesPerSecond+.999);
    }
    static std::string eta(long long seconds) {
        if(seconds<0)return "ETA: ...";
        if(seconds>=3600)return "ETA: "+std::to_string((seconds+3599)/3600)+"h";
        if(seconds>=60)return "ETA: "+std::to_string((seconds+59)/60)+"m";
        return "ETA: "+std::to_string(seconds)+"s";
    }
};
