#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETAPI_H
#define _NETAPI_H

#include <unordered_map>
#include <mutex>
#include <vector>
#include "pointers.h"

class NetPeer;
class NetChannel;
class NetPool;
class NetMessage;

#define RCVBUFSize 65535;

struct NetworkParams {
	unsigned ackTimeoutB;					
	unsigned ackRedundancy;					
	unsigned initBandwidth;					
	unsigned minBandwidth;					
	unsigned maxBandwidth;					
	unsigned outWindow;						
	unsigned ackWindow;						
	unsigned maxChannelBitMask;				
	unsigned maxOutputAckMask;				
};
extern NetworkParams networkParams;

struct RoutingItem {

	unsigned short andFlag;

	unsigned short eqFlag;

	NetCallBack* processRoutine;

	void* data;
};

class NetChannel : public RefCountSafe
{

protected:
	
	mutable std::recursive_mutex Critical_Section;

	NetCallBack* processRoutine;

	void* data;

	std::vector<RoutingItem> subsets;

	NetChannel()
	{
		processRoutine = NULL;
		data = NULL;
	}

public:
	virtual NetStatus open(NetPeer* _peer, struct sockaddr_in& distant) = 0;

	virtual NetStatus reconnect(struct sockaddr_in& distant) = 0;

	virtual void getDistantAddress(struct sockaddr_in& distant) const = 0;

	virtual void getLocalAddress(struct sockaddr_in& local) const = 0;

	virtual bool dropPacketToSend() { return false; }

	virtual bool isControl() const { return false; }

	virtual unsigned getLatency() = 0;

	virtual unsigned getOutputBandWidth() = 0;

	virtual unsigned __int64 getLastMessageArrival() const = 0;

	virtual void getOutputQueueStatistics(__int32& msgs, __int32& bytes, __int32& vimMsgs, __int32& vimBytes) = 0;

	virtual void processData(MsgHeader* hdr, const struct sockaddr_in& distant) = 0;

	virtual void setProcessRoutine(NetCallBack* processR, void* _data = NULL)
	{
		processRoutine = processR;
		data = _data;
	}

	virtual void dataSentAck(size_t size) {}

	virtual void setSubsetRoutine(const RoutingItem& item)
	{
		Critical_Section.lock();
		removeSubsetRoutine(item);
		subsets.push_back(item);
		Critical_Section.unlock();
	}

	virtual bool removeSubsetRoutine(const RoutingItem& item)
	{
		Critical_Section.lock();
		__int32 i;
		for (i = 0; i < subsets.size(); i++)
			if (subsets[i].andFlag == item.andFlag && subsets[i].eqFlag == item.eqFlag)
			{
				subsets.erase(subsets.begin() + i);
				Critical_Section.unlock();
				return true;
			}
		Critical_Section.unlock();
		return false;
	}

	virtual void dispatchMessage(NetMessage* msg, bool urgent = false) = 0;

	Ref<NetMessage> prepared;

	virtual NetMessage* getLastVIM(bool urgent) = 0;

	virtual bool getPreparedMessage() = 0;

	virtual void sendRaw(const sockaddr_in& ia, const void* data, __int32 size, __int32 sizeEncrypted) = 0;

	virtual unsigned __int64 preSend(unsigned __int64 bunchStart) = 0;

	virtual void postSend() = 0;

	virtual unsigned __int64 getMessageTime(unsigned __int32 ser) = 0;

	virtual void cancelAllMessages() = 0;

	virtual void checkConnectivity(unsigned __int64 now) = 0;

	virtual bool dropped() = 0;

	virtual void tick() = 0;

	virtual void close() = 0;

	virtual ~NetChannel() {}
};

class NetPeer : public RefCountSafe
{
protected:
	
	mutable std::recursive_mutex Critical_Section;

	NetPool* pool;
	unsigned short port;
	
	Ref<NetChannel> broadcastCh;

	NetPeer(NetPool* _pool)
	{
		pool = _pool;
		port = 0;
		broadcastCh = NULL;
	}

public:
	virtual NetPool* getPool() const { return pool; }
    // UDP payload bytes, including transport headers but excluding IP/UDP headers.
    virtual void getTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const
    { incoming = outgoing = 0; }

	virtual unsigned short getPort() const { return port; }

	virtual void getLocalAddress(struct sockaddr_in& local) const = 0;

	virtual SOCKET GetSocket() const = 0;

	virtual NetChannel* getBroadcastChannel() const { return broadcastCh.GetRef(); }

	virtual bool registerChannel(struct sockaddr_in& distant, NetChannel* ch) = 0;

	virtual void unregisterChannel(NetChannel* ch) = 0;

	virtual NetChannel* findChannel(const struct sockaddr_in& distant) = 0;

	virtual unsigned short getLocalPort() const { return port; }

	virtual void close() = 0;

	virtual void stopThreads() {}

	virtual void suspendSocket(bool susp = true) {}

	virtual void replaceSocket(SOCKET _sock) {}

	virtual void processData(MsgHeader* hdr, const struct sockaddr_in& distant) = 0;

	virtual NetStatus sendData(MsgHeader* hdr, struct sockaddr_in distant) = 0;

	virtual void sendRaw(const sockaddr_in& ia, const void* data, __int32 size, __int32 sizeEncrypted) = 0;

	virtual void cancelAllMessages() = 0;

	virtual ~NetPeer() { port = 0; }

	void enterPeer() { Critical_Section.lock(); }
	void leavePeer() { Critical_Section.unlock(); }
};

inline unsigned __int64 sockaddrKey(const struct sockaddr_in& addr)
{
	return ((unsigned __int64)ADDR(addr) | ((unsigned __int64)PORT(addr) << 32));
}

extern unsigned __int64 channelKey(const Ref<NetChannel>& ch);

#include <winsock.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "wsock32")

extern unsigned long bindIPAddress;

class NetPool : public RefCountSafe
{
protected:

	std::unordered_map<unsigned short, Ref<NetPeer>> peers;
	std::unordered_map<unsigned __int64, Ref<NetChannel>> channels;

public:
	NetPool()
	{
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);
	}

	virtual NetPeer* createPeer(unsigned short port);

	virtual NetChannel* createChannel(bool control);

	virtual void deletePeer(NetPeer* peer)
	{
		if (!peer)
			return;
		peer->close();
		for (auto it = peers.begin(); it != peers.end();)
		{
			if (it->second.GetRef() == peer)
				it = peers.erase(it);
			else
				++it;
		}
	}

	virtual NetChannel* createChannel(struct sockaddr_in& distant, NetPeer* peer = NULL)
	{
		Ref<NetPeer> pee = peer;
		if (!pee && !peers.empty())
			pee = peers.begin()->second;
		if (!pee)
			return NULL; 
		NetChannel* ch = createChannel(false);
		if (!ch)
			return NULL;
		if (ch->open(pee.GetRef(), distant) != nsOK)
		{
			delete ch;
			return NULL;
		}
		channels[channelKey(ch)] = ch;
		return ch;
	}

	virtual NetChannel* findChannel(struct sockaddr_in& distant)
	{
		auto it = channels.find(sockaddrKey(distant));
		if (it == channels.end())
			return NULL;
		return it->second.GetRef();
	}

	virtual NetChannel* getBroadcastChannel()
	{
		if (peers.empty())
			return NULL;
		return peers.begin()->second->getBroadcastChannel();
	}

	virtual void deleteChannel(NetChannel* channel)
	{
		if (!channel)
			return;
		channel->close();
		for (auto it = channels.begin(); it != channels.end();)
		{
			if (it->second.GetRef() == channel)
				it = channels.erase(it);
			else
				++it;
		}
	}

	virtual ~NetPool() { WSACleanup(); }
};

#endif
