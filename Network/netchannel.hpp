#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETCHANNEL_H
#define _NETCHANNEL_H

#include "netpeer.hpp"
#include <Network/bitmask.hpp>

extern unsigned __int32 netMessageToSerial(const Ref<NetMessage>& msg);
extern unsigned __int32 netMessageDepend(const Ref<NetMessage>& msg);

#define MAX_ACK_ARRAY 1024 

#define MAX_OLD_ACKS 8

#define LATENCY_F 0.30f 

#define BANDWIDTH_F 0.15f		
#define LOCAL_BANDWIDTH_F 0.25f 

#define RBE_WEIGHT 0.00f 

#define MAX_PING_GAP 6000
#define PING_TRY_INTERVAL 3000
#define MAX_HEART_BEAT_GAP 2000

#define MAX_ACK_TIMEOUT 7000

#define MAX_TIMEOUT 4000

#define NOT_RECEIVED 0xff 

#define MAX_PAIR_SEND_GAP 50

#define PACKET_PAIR_COEF 0.025f 

#define MIN_PACKET_PAIR 50 

#define MAX_PACKET_PAIR 800 

#define MAX_PACKET_PAIR_DELAY 200

#define GOOD_CHANNEL_BIT_MASK 4096 

#define ACK_QUEUE_GRANUL 100

#define URGENT_MSG_THRESHOLD 65 

#define VIM_MSG_THRESHOLD 25 

class NetChannelBasic : public NetChannel
{
protected:
	
	NetPeer* peer;
	unsigned timeout;
	bool control;

public:
	NetChannelBasic(bool _control)
	{
		starvation = false;
		opened = false;
		timeout = 1000;
		control = _control;
		memset((void*)&(dist), NULL, sizeof(dist));
		dist.sin_addr.s_addr = INADDR_BROADCAST;
		peer = NULL;
		
		inputMax = inputMin = receivedSerialWindowStart = ackPtr = serial = 1;
		lastSerialSent = -1;
		oldAckFirst = oldAckLast = 0;
		aveLatency = actLatency = 0;
		heartBeatGap = timeout;
		
		urgentToSend = NULL;
		vimToSend = NULL;
		commonToSend = NULL;
		
		lastPingArrival = lastPingDeparture =
			lastMsgArrival = lastMsgDeparture =
				lastPairDeparture = runTime = GetTickCount64();
		recentVIMs = 0;
		ackMin = ackMax = 0;
		
		memset((void*)&(ackStatQueue), NULL, sizeof(ackStatQueue));
		for (__int32 i = 2; i < SLIDING_WINDOW; i += 2)
			ackStatQueue[i] = runTime - networkParams.ackWindow;
		ackStatQueue[0] = runTime;
		ackStatQueue[1] = (networkParams.initBandwidth * (unsigned __int64)networkParams.ackWindow) / 1000;
		ackStatIndex = 0;
		
		InitSendQueue(runTime);

		memset((void*)&(ackTime), NULL, sizeof(ackTime));
	}

	virtual NetStatus open(NetPeer* _peer, struct sockaddr_in& distant)
	{
		if (opened || !_peer)
			return nsError;
		peer = _peer;
		dist = distant;
		if (!control && !peer->registerChannel(distant, this))
			return nsInvalidSharing;

		Critical_Section.lock();

		opened = true;
		inputMax = inputMin = receivedSerialWindowStart = ackPtr = serial = 1;
		lastSerialSent = -1;
		oldAckFirst = oldAckLast = 0;
		for (__int32 i = 0; i < MAX_ACK_ARRAY;)
			ack[i++] = NOT_RECEIVED;
		aveLatency = actLatency = 0;
		lastMsgArrival = runTime = GetTickCount64();
		lastPingArrival = runTime - MAX_PING_GAP;
		lastPingDeparture = runTime - PING_TRY_INTERVAL;
		lastPairDeparture = runTime - MAX_HEART_BEAT_GAP;
		lastMsgDeparture = runTime - heartBeatGap;
		recentVIMs = 0;
		
		ackMin = ackMax = 0;
		
		memset((void*)&(ackStatQueue), NULL, sizeof(ackStatQueue));
		for (__int32 i = 2; i < SLIDING_WINDOW; i += 2)
			ackStatQueue[i] = runTime - networkParams.ackWindow;
		ackStatQueue[0] = runTime;
		ackStatQueue[1] = (networkParams.initBandwidth * (unsigned __int64)networkParams.ackWindow) / 1000;
		ackStatIndex = 0;
		
		InitSendQueue(runTime);

		Critical_Section.unlock();
		return nsOK;
	}

	virtual bool isControl() const { return control; }

	virtual unsigned getLatency()
	{
		return aveLatency;
	}

	virtual unsigned getOutputBandWidth()
	{
		Critical_Section.lock();
		float ackBandwidth = getAckBandwidth(networkParams.ackWindow);
		Critical_Section.unlock();

		return ackBandwidth > 0.0f ? static_cast<unsigned>(ackBandwidth) : 0;
	}

	virtual unsigned __int64 getLastMessageArrival() const
	{
		Critical_Section.lock();
		unsigned __int64 result = lastMsgArrival;
		Critical_Section.unlock();
		return result;
	}

	virtual void getOutputQueueStatistics(__int32& msgs, __int32& bytes, __int32& vimMsgs, __int32& vimBytes)
	{
		msgs = bytes = vimMsgs = vimBytes = 0;
		Critical_Section.lock();
		NetMessage* tmp = vimToSend.GetRef();
		while (tmp)
		{
			vimMsgs++; 
			vimBytes += tmp->header->length + IP_UDP_HEADER;
			tmp = tmp->next.GetRef();
		}
		tmp = urgentToSend.GetRef();
		while (tmp)
		{
			vimMsgs++;
			vimBytes += tmp->header->length + IP_UDP_HEADER;
			tmp = tmp->next.GetRef();
		}
		tmp = commonToSend.GetRef();
		while (tmp)
		{
			msgs++;
			bytes += tmp->header->length + IP_UDP_HEADER;
			tmp = tmp->next.GetRef();
		}
		Critical_Section.unlock();
	}

	virtual void getDistantAddress(struct sockaddr_in& distant) const { distant = dist; }
	virtual void getLocalAddress(struct sockaddr_in& local) const { peer ? peer->getLocalAddress(local) : memset((void*)&(local), 0, sizeof(local)); }

	virtual void sendRaw(const sockaddr_in& ia, const void* data, __int32 size, __int32 sizeEncrypted) { peer->sendRaw(ia, data, size, sizeEncrypted); }

	virtual void processData(MsgHeader* hdr, const struct sockaddr_in& distant)
	{
		Critical_Section.lock();
		Ref<NetMessage> msg = NetMessagePool::pool()->newMessage(hdr->length - sizeof(MsgHeader), this);
		if (!msg)
		{
			Critical_Section.unlock();
			return;
		}
		if (!msg->setMessage(hdr))
		{
			msg->recycle();
			Critical_Section.unlock();
			return;
		}

		if (!control && received(hdr->serial))
		{
			inputResent(msg.GetRef());
			msg->recycle();
			Critical_Section.unlock();
			return;
		}
		const unsigned short flags = hdr->flags;
		
		msg->msgProcessRoutine = processRoutine; 
		msg->dta = data;
		for (__int32 i = 0; i < subsets.size(); i++)
			if ((flags & subsets[i].andFlag) == subsets[i].eqFlag)
			{
				msg->msgProcessRoutine = subsets[i].processRoutine;
				msg->dta = subsets[i].data;
				break;
			}
		msg->distant = distant;
		msg->nextEvent = msg->status = nsInputReceived;

		if (!control && !(flags & MSG_FROM_BCAST_FLAG))
		{ 
			
			inputStatistics(msg.GetRef());
			
			if (flags & MSG_INSTANT_FLAG)
				setDelayMessage(msg.GetRef());

			if (flags & MSG_DUMMY_FLAG)
			{
				Critical_Section.unlock();
				return;
			}
			
			if (flags & MSG_VIM_FLAG)
			{
				if (flags & MSG_ORDERED_FLAG)
				{ 
					unsigned __int32 pred = (unsigned __int32)hdr->c.control2;
					if (pred >= receivedSerialWindowStart && !processedSerials.get(pred))
					{

						Ref<NetMessage> old;
						auto it = deferred.find(pred);
						if (it != deferred.end())
							old = it->second;
						deferred[pred] = msg;

						msg->next = old; 
						Critical_Section.unlock();
						return;
					}
				}
				processVIM(msg.GetRef());
				return;
			}
		}
		
		Critical_Section.unlock();
		if (msg->msgProcessRoutine)
			msg->nextEvent = (*msg->msgProcessRoutine)(msg.GetRef(), nsInputReceived, msg->dta);
	}

	virtual void dataSent(size_t size, unsigned __int64 refTime)
	{
		if (refTime - sendQueue[sendIndex] < networkParams.outWindow * (1.0f / (SLIDING_WINDOW_SEND - 8)))
		{
			sendQueue[sendIndex + 1] += size;
		}
		else
		{
			unsigned __int64 total = sendQueue[sendIndex + 1];
			if ((sendIndex += 2) >= SLIDING_WINDOW_SEND)
				sendIndex = 0;
			sendQueue[sendIndex] = refTime;
			sendQueue[sendIndex + 1] = total + size;
		}
	}

	virtual void dataSentAck(size_t size)
	{
		Critical_Section.lock();
		unsigned __int64 now = GetTickCount64();
		dataSent(size, now);

		if ((now - ackStatQueue[ackStatIndex]) < ACK_QUEUE_GRANUL)
		{
			ackStatQueue[ackStatIndex + 1] += newBytes;
		}
		else
		{
			unsigned __int64 total = ackStatQueue[ackStatIndex + 1];
			if ((ackStatIndex += 2) >= SLIDING_WINDOW)
				ackStatIndex = 0;
			ackStatQueue[ackStatIndex] = now;
			ackStatQueue[ackStatIndex + 1] = total + newBytes;
		}
		Critical_Section.unlock();
	}

	virtual void dispatchMessage(NetMessage* msg, bool urgent = false);

	virtual NetMessage* getLastVIM(bool urgent);

	virtual bool getPreparedMessage();

	virtual unsigned __int64 preSend(unsigned __int64 bunchStart);

	virtual void postSend();

	virtual unsigned __int64 getMessageTime(unsigned __int32 ser);

	static const unsigned __int64 RUN_INTERVAL;

	virtual void cancelAllMessages()
	{
		Critical_Section.lock();
		Ref<NetMessage> ptr;
		Ref<NetMessage> tmp;
		lastUrgent = NULL;
		ptr = urgentToSend;
		urgentToSend = NULL;
		while (ptr)
		{
			tmp = ptr->next;
			ptr->cancel();
			ptr = tmp;
		}
		lastVIM = NULL;
		ptr = vimToSend;
		vimToSend = NULL;
		while (ptr)
		{
			tmp = ptr->next;
			ptr->cancel();
			ptr = tmp;
		}
		ptr = commonToSend;
		commonToSend = NULL;
		while (ptr)
		{
			tmp = ptr->next;
			ptr->cancel();
			ptr = tmp;
		}
		Critical_Section.unlock();
	}

	virtual void checkConnectivity(unsigned __int64 now)
	{
		if (!opened)
			return;
		Critical_Section.lock();
		checkConnectivityInternal(now);
		Critical_Section.unlock();
	}

	virtual bool dropped()
	{
		Critical_Section.lock();
		unsigned __int64 now = GetTickCount64();
		bool drop = now > lastPingArrival + (MAX_PING_GAP << 1) && now > lastMsgArrival + 15000;
		Critical_Section.unlock();

		return drop;
	}

	virtual void tick();

	virtual void close()
	{
		if (!opened)
			return;
		opened = false;

		cancelAllMessages();
		if (peer)
		{
			if (!control)
				peer->unregisterChannel(this);
			peer = NULL;
		}
		Critical_Section.lock();
		subsets.clear();
		processRoutine = NULL;
		deferred.clear();
		revisited.clear();

		Critical_Section.unlock();
		if (NetMessagePool::pool())
			NetMessagePool::pool()->garbageCollect();
	}

	static void* operator new(size_t size) { return malloc(size); }

	static void* operator new(size_t size, const char* file, __int32 line) { return malloc(size); }

	static void operator delete(void* mem) { free(mem); }

	virtual ~NetChannelBasic() { close(); }

protected:
	bool opened; 

	struct sockaddr_in dist; 

	unsigned __int32 serial;		 
	unsigned __int32 lastSerialSent; 

	Ref<NetMessage> vimToSend; 
	NetMessage* vimToSendEnd;  
	Ref<NetMessage> lastVIM;   

	Ref<NetMessage> urgentToSend; 
	NetMessage* urgentToSendEnd;  
	Ref<NetMessage> lastUrgent;	  

	Ref<NetMessage> commonToSend; 
	NetMessage* commonToSendEnd;  

	BitMask pendingAckSerials; 

	unsigned __int32 ackMax; 

	unsigned __int32 ackMin; 

	BitMask recentPendingAckSerials; 

	unsigned __int64 sendQueue[SLIDING_WINDOW_SEND];

	__int32 sendIndex;

	void InitSendQueue(unsigned __int64 time);
	
	typedef std::unordered_map<unsigned __int32, Ref<NetMessage>> RevisitedMap;
	RevisitedMap revisited;
	
	unsigned __int64 runTime;
	
	void setOutputData(NetMessage* msg);
	
	void insertResend(NetMessage* msg);
	
	void insertUrgent(NetMessage* msg);
	
	void insertUrgentAfter(NetMessage* msg, NetMessage* after);

	void insertVIM(NetMessage* msg);
	
	void insertCommon(NetMessage* msg);
	
	bool getUrgentMessage();
	
	bool getVIMMessage();
	
	bool getCommonMessage();

	std::unordered_map<unsigned __int32, Ref<NetMessage>> deferred;

	void inputStatistics(NetMessage* msg);

	void inputResent(NetMessage* msg)
	{
		unsigned __int32 ser = msg->getSerial();
		if (ser < receivedSerialWindowStart)
			return;
		if (ser >= inputMin)
		{
			
			__int32 serI = ackPtr - (inputMax - ser);
			if (serI < 0)
				serI += MAX_ACK_ARRAY;
			ackTime[serI] = (unsigned)msg->refTime;
			if (msg->header->flags & MSG_VIM_FLAG)
				ack[serI] = networkParams.ackRedundancy; 
		}
		else
		{
			
			__int32 newIndex = oldAckLast + 1;
			if (newIndex >= MAX_OLD_ACKS)
				newIndex = 0;
			if (newIndex != oldAckFirst)
			{ 
				oldAckQueue[oldAckLast] = ser;
				oldAckLast = newIndex;
			}
		}
	}

	void processVIM(NetMessage* msg)
	{
		unsigned __int32 ser = msg->getSerial(); 

		if (ser >= receivedSerialWindowStart)
		{ 
			if (msg->msgProcessRoutine)
			{
				Critical_Section.unlock();
				msg->nextEvent = (*msg->msgProcessRoutine)(msg, nsInputReceived, msg->dta);
				Critical_Section.lock();
			}
			processedSerials.on(ser);
		}
		
		Ref<NetMessage> def;
		auto it = deferred.find(ser);
		if (it != deferred.end())
		{
			def = it->second; 
			deferred.erase(it);
		}

		while (def)
		{ 

			Ref<NetMessage> next = def->next;
			def->next = NULL;
			processVIM(def.GetRef());
			def = next;
			Critical_Section.lock();
		}
		Critical_Section.unlock();
	}

	void setDelayMessage(NetMessage* request);

	unsigned char ack[MAX_ACK_ARRAY];

	unsigned __int32 oldAckQueue[MAX_OLD_ACKS];

	__int32 oldAckFirst;
	
	__int32 oldAckLast;

	unsigned ackTime[MAX_ACK_ARRAY];

	__int32 ackPtr; 

	unsigned __int32 inputMax; 

	unsigned __int32 inputMin; 

	BitMask receivedSerials; 

	unsigned __int32 receivedSerialWindowStart; 

	bool starvation; 

	unsigned recentVIMs; 

	inline bool received(unsigned __int32 ser)
	{
		if (ser < receivedSerialWindowStart)
			return true;
		return receivedSerials.get(ser);
	}

	BitMask processedSerials; 

	unsigned newAcks; 

	unsigned newBytes; 
	
	void newAcknowledgement(unsigned __int32 s, NetMessage* msg)
	{
		pendingAckSerials.off(s);
		recentPendingAckSerials.off(s);
		if (!msg)
			return;

		msg->status = nsOutputAck;
		
		newAcks++;
		newBytes += msg->header->length + IP_UDP_HEADER;
		
		if (msg->msgProcessRoutine && (msg->nextEvent == nsOutputTimeout || msg->nextEvent == nsOutputAck))
		{
			Critical_Section.unlock();
			msg->nextEvent = (*msg->msgProcessRoutine)(msg, nsOutputAck, msg->dta);
			Critical_Section.lock();
		}
	}

	unsigned __int64 lastMsgArrival; 

	unsigned __int64 lastMsgDeparture; 

	unsigned __int64 lastPingArrival; 

	unsigned __int64 lastPingDeparture; 

	unsigned __int64 lastPairDeparture; 

	void checkConnectivityInternal(unsigned __int64 now)
	{
		if (control)
			return; 

		if (!now)
			now = GetTickCount64();
		if (!starvation && 
			(!recentVIMs ||
				now < lastMsgDeparture + (heartBeatGap >> 1)) && 
			(now < lastPingArrival + MAX_PING_GAP ||			 
				now < lastPingDeparture + PING_TRY_INTERVAL) &&	 
			now < lastMsgDeparture + heartBeatGap)				 
			return;

		Ref<NetMessage> msg = urgentToSend; 
		const unsigned msgLen = sizeof(MsgHeader);

		if (!msg)
		{
			msg = NetMessagePool::pool()->newMessage(msgLen - sizeof(MsgHeader), this);
			msg->setLength(msgLen - sizeof(MsgHeader));
			msg->header->flags |= MSG_DUMMY_FLAG;
			insertUrgent(msg.GetRef());
		}

		if (now > lastPingDeparture + PING_TRY_INTERVAL)
		{

			msg->header->flags |= MSG_INSTANT_FLAG;
			lastPingDeparture = now;
		}

		starvation = false;
		lastMsgDeparture = now;
	}

	unsigned heartBeatGap;
	unsigned aveLatency;
	unsigned actLatency;
	unsigned __int64 ackStatQueue[SLIDING_WINDOW];
	__int32 ackStatIndex;

	unsigned getLatency(bool average) { return average ? aveLatency : actLatency; }
	float getAckBandwidth(unsigned __int64 windowSize);
};

#endif
