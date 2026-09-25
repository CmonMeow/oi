#pragma once
#include "HostDirectoryProtocol.h"
#include <mutex>

// Shared only between the UI's directory client and the game socket's receive worker.
// Never responds unless this instance is currently trying to publish this exact host.
class HostDirectoryProbe {
    std::mutex mutex;
    bool enabled=false;
    sockaddr_in directory={};
    HostDirectory::Packet registration;
public:
    static HostDirectoryProbe& instance() { static HostDirectoryProbe probe;return probe; }
    void set(const sockaddr_in& address,const HostDirectory::Packet& packet) {
        std::lock_guard<std::mutex> lock(mutex);directory=address;registration=packet;enabled=true;
    }
    void clear() { std::lock_guard<std::mutex> lock(mutex);enabled=false; }
    bool receive(SOCKET socket,const void* data,int bytes,const sockaddr_in& from) {
        HostDirectory::Packet p;
        if(!HostDirectory::decode(data,bytes,p))return false;
        std::lock_guard<std::mutex> lock(mutex);
        if(enabled && p.kind==HostDirectory::Probe && HostDirectory::sameEndpoint(from,directory) &&
            sodium_memcmp(p.nonce,registration.nonce,16)==0 && p.gamePort==registration.gamePort &&
            p.version==registration.version && p.users==registration.users &&
            memcmp(p.name,registration.name,sizeof(p.name))==0 && HostDirectory::verified(p)) {
            p.kind=HostDirectory::Proof;memset(p.signature,0,sizeof(p.signature));HostDirectory::send(socket,from,p);
        }
        return true;
    }
};
