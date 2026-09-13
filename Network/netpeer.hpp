#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETPEER_H
#define _NETPEER_H

#include <winsock2.h>
#include <unordered_map>
#include <atomic>
#include "bitmask.hpp"

extern unsigned long bindIPAddress;

const __int32 SLIDING_WINDOW = 64;
const __int32 SLIDING_WINDOW_SEND = 256;

inline bool getLocalAddress(struct sockaddr_in& me, unsigned short port)
{
	me.sin_family = AF_INET;
	char meName[128];
	if (gethostname(meName, 128) == SOCKET_ERROR)
		return false;

	struct hostent* h = gethostbyname(meName);
	if (h)
	{
		if (h->h_length < 4)
			return false;
		memcpy(&(me.sin_addr.s_addr), h->h_addr, 4);
	}
	else
	{
		me.sin_addr.s_addr = inet_addr(meName);
		if (me.sin_addr.s_addr == INADDR_NONE)
			return false;
	}
	me.sin_port = htons(port);
	return true;
}

inline bool getHostAddress(struct sockaddr_in& host, const char* ip, unsigned short port)
{
	host.sin_family = AF_INET;
	host.sin_port = htons(port);
	if (!ip || !ip[0])
		host.sin_addr.s_addr = INADDR_BROADCAST;
	else
	{
		host.sin_addr.s_addr = inet_addr(ip);
		if (host.sin_addr.s_addr == INADDR_NONE)
		{
			struct hostent* h = gethostbyname(ip);
			if (!h)
				return false;
			host.sin_addr.s_addr = *(unsigned __int32*)h->h_addr_list[0];
		}
	}
	return true;
}

unsigned __int32 crc32(unsigned __int32 crc, const unsigned char* buf, __int64 len);

class NetPeerUDP : public NetPeer
{

protected:
	
	std::unordered_map<unsigned __int64, Ref<NetChannel>> chMap;

    std::atomic<unsigned __int64> incomingBytes{0};
    std::atomic<unsigned __int64> outgoingBytes{0};
	SOCKET sock;

	HANDLE listener = NULL;

	std::atomic<bool> listen{false};

	friend DWORD WINAPI udpListenSend(void* param);

	void reconnect();

	bool reconnecting;

public:
    void getTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const override
    {
        incoming = incomingBytes.load(std::memory_order_relaxed);
        outgoing = outgoingBytes.load(std::memory_order_relaxed);
    }

	NetPeerUDP(NetPool* _pool) : NetPeer(_pool)
	{
		sock = INVALID_SOCKET;
		port = 0;
		listen = reconnecting = false;
	}

	NetPeerUDP(SOCKET _sock, unsigned short _port, NetPool* _pool)
		: NetPeer(_pool)
	{
		Critical_Section.lock();
		sock = _sock;
		port = _port;
		listen = reconnecting = false;
		broadcastCh = NULL;
		if (pool)
		{ 
			broadcastCh = pool->createChannel(true);
			if (broadcastCh)
			{
				struct sockaddr_in distant;
				memset((void*)&(distant), NULL, sizeof(distant));
				distant.sin_addr.s_addr = INADDR_BROADCAST;
				broadcastCh->open(this, distant);
			}
		}
		if (sock != INVALID_SOCKET)
		{ 
			
			listen = true;
			DWORD thid;
			listener = CreateThread(NULL, 32 * 1024, &udpListenSend, this, 0, &thid);
			if (listener)
				SetThreadPriority(listener, THREAD_PRIORITY_HIGHEST); 
			else
				listen = false; 
		}
		Critical_Section.unlock();
	}

	virtual void getLocalAddress(struct sockaddr_in& local) const { ::getLocalAddress(local, port); }

	virtual SOCKET GetSocket() const { return sock; }

	virtual bool registerChannel(struct sockaddr_in& distant, NetChannel* ch)
	{
		if (!ch)
			return false;
		Critical_Section.lock();
		const unsigned __int64 key = sockaddrKey(distant);
		bool result = (chMap.find(key) == chMap.end());
		if (result)
			chMap[key] = ch;
		Critical_Section.unlock();
		return result;
	}

	virtual void unregisterChannel(NetChannel* ch)
	{
        if (!ch) return;
        Critical_Section.lock();
        for (auto it = chMap.begin(); it != chMap.end();)
		{
			if (it->second.GetRef() == ch)
				it = chMap.erase(it);
			else
				++it;
		}
        Critical_Section.unlock();
	}

	virtual NetChannel* findChannel(const struct sockaddr_in& distant);

	virtual void close();

	virtual void stopThreads()
	{
		Critical_Section.lock();
		bool wasListen = listen;
		listen = false;
		Critical_Section.unlock();
		if (wasListen)
		{
			if (listener)
				WaitForSingleObject(listener, INFINITE);
			CloseHandle(listener);
		}
	}

	virtual void processData(MsgHeader* hdr, const struct sockaddr_in& distant) {}

	virtual NetStatus sendData(MsgHeader* hdr, struct sockaddr_in distant)
	{
		Critical_Section.lock();
		if (sock != INVALID_SOCKET)
		{
			hdr->crc = 0;
			hdr->crc = crc32(0, (const unsigned char*)hdr, hdr->length);

			char retryCounter = 12;
		retry:
			if (sendto(sock, reinterpret_cast<const char*>(hdr), hdr->length, 0, (const sockaddr*)&distant, sizeof(distant)) != SOCKET_ERROR)
			{
                outgoingBytes.fetch_add(hdr->length, std::memory_order_relaxed);
				Critical_Section.unlock();
				return nsOutputSent;
			}
			__int32 werror = WSAGetLastError();
			__int32 error = 0;
			__int32 errLen = sizeof(error);
			getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &errLen);

			WSASetLastError(0); 

			if (werror == WSAECONNRESET)
			{ 
				reconnect();
				if ((sock != INVALID_SOCKET) && (retryCounter--))
					goto retry;
			}
		}
		Critical_Section.unlock();
		return nsError;
	}

	virtual void sendRaw(const sockaddr_in& ia, const void* data, __int32 size, __int32 sizeEncrypted)
	{
		Critical_Section.lock(); 
        int sentBytes = sendto(sock, reinterpret_cast<const char*>(data), size, 0, reinterpret_cast<const sockaddr*>(&ia), sizeof(ia));
        if (sentBytes > 0) outgoingBytes.fetch_add(sentBytes, std::memory_order_relaxed);
		Critical_Section.unlock();
	}

	virtual void cancelAllMessages()
	{
		Critical_Section.lock();
        const auto channels = chMap;
        for (const auto& it : channels)
            if (it.second) it.second->cancelAllMessages();

		Critical_Section.unlock();
	}

	virtual ~NetPeerUDP() { close(); }
};

#endif
