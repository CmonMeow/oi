#pragma once
#include <winsock2.h>
#include <map>
#include <memory>
#include "ScreenSignaling.h"

// Browser-facing sockets never leave loopback. Opaque ICE/DTLS/SRTP packets
// travel to the other browser only through the authenticated room connection.
class ScreenRelay
{
    struct Link {
        SOCKET socket=INVALID_SOCKET;
        int peer=-1,port=0;
        string share;
        sockaddr_in browser={};
        std::deque<string> pending;
        ~Link(){if(socket!=INVALID_SOCKET)closesocket(socket);}
    };
    std::map<string,std::unique_ptr<Link>> links;
    std::function<void(const ScreenSignaling::Event&)> send;
    bool initialized=false;

    static void write(Link& link,const string& bytes) {
        if(link.browser.sin_port)
            sendto(link.socket,bytes.data(),(int)bytes.size(),0,(sockaddr*)&link.browser,sizeof(link.browser));
        else if(link.pending.size()<8)link.pending.push_back(bytes);
    }
public:
    explicit ScreenRelay(std::function<void(const ScreenSignaling::Event&)> output):send(output) {
        WSADATA data;initialized=WSAStartup(MAKEWORD(2,2),&data)==0;
    }
    ~ScreenRelay(){clear();if(initialized)WSACleanup();}
    int open(int peer,const string& share,const string& connection) {
        auto old=links.find(connection);
        if(old!=links.end())return old->second->peer==peer&&old->second->share==share?old->second->port:0;
        if(!initialized||links.size()>=25||peer<0||!ScreenSignaling::validId(share)||!ScreenSignaling::validId(connection))return 0;
        auto link=std::make_unique<Link>();link->peer=peer;link->share=share;
        link->socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(link->socket==INVALID_SOCKET)return 0;
        sockaddr_in address={};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(link->socket,(sockaddr*)&address,sizeof(address))==SOCKET_ERROR)return 0;
        int size=sizeof(address);if(getsockname(link->socket,(sockaddr*)&address,&size)==SOCKET_ERROR)return 0;
        u_long nonblocking=1;if(ioctlsocket(link->socket,FIONBIO,&nonblocking)==SOCKET_ERROR)return 0;
        int buffer=131072;setsockopt(link->socket,SOL_SOCKET,SO_RCVBUF,(const char*)&buffer,sizeof(buffer));
        link->port=ntohs(address.sin_port);int port=link->port;links.emplace(connection,std::move(link));return port;
    }
    void close(const string& connection){links.erase(connection);}
    void stop(const string& share){for(auto it=links.begin();it!=links.end();)if(it->second->share==share)it=links.erase(it);else ++it;}
    void clear(){links.clear();}
    void receive(const ScreenSignaling::Event& event) {
        auto found=links.find(event.connection);
        if(found==links.end()||found->second->peer!=event.peer||found->second->share!=event.share||event.payload.empty()||event.payload.size()>2048)return;
        write(*found->second,event.payload);
    }
    void update() {
        for(auto& item:links){
            auto& link=*item.second;
            for(unsigned i=0;i<32;++i){
                char bytes[2048];sockaddr_in from={};int size=sizeof(from);
                int count=recvfrom(link.socket,bytes,sizeof(bytes),0,(sockaddr*)&from,&size);
                if(count<0){if(WSAGetLastError()==WSAEMSGSIZE)continue;break;}
                if(!count||from.sin_addr.s_addr!=htonl(INADDR_LOOPBACK))continue;
                if(link.browser.sin_port&&link.browser.sin_port!=from.sin_port)continue;
                link.browser=from;
                send({ScreenSignaling::Media,link.peer,link.share,item.first,string(bytes,count)});
            }
            if(link.browser.sin_port){for(const auto& bytes:link.pending)write(link,bytes);link.pending.clear();}
        }
    }
};
