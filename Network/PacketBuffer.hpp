#ifdef _MSC_VER
#pragma once
#endif

#ifndef NETMESSAGE_H
#define NETMESSAGE_H

#include <deque>

#include <unordered_map>
#include <sysdef.h>

class PacketCache;

class PacketBuffer : public AtomicRefCount
{
protected:
	ChannelInterface* channel;
	struct sockaddr_in distant;
	// Local sequence including wrap count; never serialized.
	unsigned __int64 localSequence;
	PacketStatus status;

	union
	{
		DatagramHeader* header;
		unsigned char* data;
	};

	unsigned totalLen;
	unsigned msgLen;
	unsigned __int64 packetActivityMs;
	unsigned __int64 packetStartedMs;
	unsigned __int64 retransmitDelayMs;
	unsigned __int64 deliveryLifetimeMs;
	bool awaitingRttSample;
	bool allowBatching;
	PacketCallback* packetEventHandler;
	void* packetEventContext;

	PacketStatus subscribedPacketEvent;

	unsigned __int64 heartbeatRequestSequence;
	unsigned __int64 heartbeatReceivedMs;

	IntrusivePtr<PacketBuffer> orderingPredecessor;

	void resetPacket()
	{
		status = PacketInvalidMessage;
		packetEventHandler = NULL;
		next = NULL;
		orderingPredecessor = NULL;
		channel = NULL;
		memset(data, 0, totalLen);
		header->serial = NULL;
		localSequence = 0;
		header->flags = 0;
		msgLen = header->length = sizeof(DatagramHeader);
		header->ackBaseSequence = 1;
		header->ackBits = 0;
		subscribedPacketEvent = PacketInvalidMessage;
		memset((void*)&(distant), NULL, sizeof(distant));
		awaitingRttSample = false;
		allowBatching = true;
		packetActivityMs = packetStartedMs = deliveryLifetimeMs = retransmitDelayMs = 0L;
	}
	virtual void assignChannel(ChannelInterface* ch) { channel = ch; }

	friend class PacketCache;
	friend unsigned packetIdKey(const IntrusivePtr<PacketBuffer>& msg);
	friend class ReliableChannel;
	friend DWORD WINAPI runUdpWorker(void* param);

	static unsigned __int32 nextId;

public:
	unsigned __int32 id;
	IntrusivePtr<PacketBuffer> next;

	PacketBuffer(unsigned len)
	{
		if (len < sizeof(DatagramHeader))
			len = sizeof(DatagramHeader);
		data = (unsigned char*)malloc(totalLen = len);
		packetEventHandler = NULL;
		packetEventContext = NULL;
		resetPacket();
	}
	virtual ~PacketBuffer()
	{
		resetPacket();
		if (data)
		{
			free(data);
			data = NULL;
		}
	}
	virtual bool copyDatagram(const DatagramHeader* hdr)
	{
		if (hdr->length > totalLen)
		{
			Error("Message overflow %u>%u", (unsigned)hdr->length, (unsigned)totalLen);
			status = PacketInvalidMessage;
			subscribedPacketEvent = PacketInvalidMessage;
			return false;
		}

		memcpy(data, (const char*)hdr, hdr->length);
		msgLen = hdr->length;
		localSequence = hdr->serial;
		packetActivityMs = packetStartedMs = GetTickCount64();
		return true;
	}
	virtual void assignSourceEndpoint(const PacketBuffer* msg)
	{
		channel = msg->channel;
		distant = msg->distant;
		status = msg->status;
		header->flags = msg->header->flags & ~(PACKET_FRAGMENT | PACKET_FINAL_FRAGMENT);
		header->serial = msg->header->serial;
		localSequence = msg->localSequence;
		header->ackBaseSequence = msg->header->ackBaseSequence;
		header->ackBits = msg->header->ackBits;

		packetActivityMs = msg->packetActivityMs;
		packetStartedMs = msg->packetStartedMs;
		retransmitDelayMs = msg->retransmitDelayMs;
		deliveryLifetimeMs = msg->deliveryLifetimeMs;
		awaitingRttSample = msg->awaitingRttSample;
		allowBatching = msg->allowBatching;
		packetEventHandler = msg->packetEventHandler;
		packetEventContext = msg->packetEventContext;
		subscribedPacketEvent = msg->subscribedPacketEvent;
		heartbeatRequestSequence = msg->heartbeatRequestSequence;
		heartbeatReceivedMs = msg->heartbeatReceivedMs;
		orderingPredecessor = msg->orderingPredecessor;
		next = NULL;
	}
	virtual void assignPayload(const unsigned char* _data, unsigned __int32 length)
	{
		if (length + sizeof(DatagramHeader) > totalLen)
		{
			Error("Packet payload exceeds capacity: %u > %zu bytes", length, static_cast<size_t>(totalLen) - sizeof(DatagramHeader));
			length = totalLen - sizeof(DatagramHeader);
		}
		if (length)
			memcpy(data + sizeof(DatagramHeader), _data, length);
		msgLen = length + sizeof(DatagramHeader);
		header->length = (msgLen > USHRT_MAX) ? USHRT_MAX : msgLen;
	}
	virtual void resizePayload(unsigned length)
	{
		if (length + sizeof(DatagramHeader) > totalLen)
			length = totalLen - sizeof(DatagramHeader);
		msgLen = length + sizeof(DatagramHeader);
		header->length = (msgLen > USHRT_MAX) ? USHRT_MAX : msgLen;
	}
	virtual void updatePacketFlags(unsigned short andMask, unsigned short orMask)
	{
		andMask |= (PACKET_PING_REPLY | PACKET_ORDERED |
					PACKET_BATCH_MEMBER | PACKET_HEADER_ONLY);
		orMask &= ~(PACKET_PING_REPLY | PACKET_ORDERED |
					PACKET_BATCH_MEMBER | PACKET_HEADER_ONLY);
		header->flags &= andMask;
		header->flags |= orMask;
	}
	virtual bool markBatchMember(bool bunch)
	{
		if (bunch && allowBatching)
		{
			header->flags |= PACKET_BATCH_MEMBER;
			return true;
		}
		header->flags &= ~PACKET_BATCH_MEMBER;
		return false;
	}
	virtual void requireOrderedDelivery(PacketBuffer* _pred)
	{
		if (_pred)
		{ 
			if (!(_pred->header->flags & PACKET_RELIABLE))
				return;
			header->flags |= (PACKET_ORDERED | PACKET_RELIABLE);
			if (_pred->sequenceNumber() == NULL)
				orderingPredecessor = _pred; 
			else
			{
				orderingPredecessor = NULL; 
				header->ordered.predecessorSequence = static_cast<unsigned __int32>(_pred->sequenceNumber());
			}
		}
		else
		{ 
			header->flags &= ~PACKET_ORDERED;
			orderingPredecessor = NULL;
		}
	}
	virtual void setOrderingDependency()
	{
		channel->AddRef();
		orderingPredecessor = channel->lastReliablePacket((header->flags & PACKET_PRIORITY) != 0);
		channel->Release();
		if (orderingPredecessor)
		{
			header->flags |= (PACKET_ORDERED | PACKET_RELIABLE);
			if (orderingPredecessor->header->flags & PACKET_PRIORITY)
				header->flags |= PACKET_PRIORITY;
			if (orderingPredecessor->sequenceNumber() != NULL)
			{ 
				header->ordered.predecessorSequence = static_cast<unsigned __int32>(orderingPredecessor->sequenceNumber());
				orderingPredecessor = NULL; 
			}
		}
		else
			header->flags &= ~PACKET_ORDERED;
	}
	virtual void setDeliveryDeadline(unsigned __int64 timeout) { deliveryLifetimeMs = timeout; }
	virtual void setPacketCallback(PacketCallback* routine, PacketStatus event, void* _dta)
	{
		packetEventHandler = routine;
		packetEventContext = _dta;
		subscribedPacketEvent = routine ? event : PacketInvalidMessage;
	}
	virtual void queueForSend(bool urgent = false)
	{
		if (channel)
			channel->queueOutboundPacket(this, urgent);
	}
	virtual DatagramHeader* datagramHeader() const { return header; }
	virtual unsigned short packetFlags() const { return header->flags; }
	virtual unsigned payloadLength() const { return (msgLen - sizeof(DatagramHeader)); }
	virtual void* payloadBytes() const { return (data ? data + sizeof(DatagramHeader) : NULL); }
	virtual void packetDestination(struct sockaddr_in& _distant) const { _distant = distant; }
	virtual void assignDestination(struct sockaddr_in& _distant) { distant = _distant; }
	virtual unsigned __int64 sequenceNumber() const { return localSequence; }
	virtual ChannelInterface* ownerChannel() const { return channel; }
	virtual unsigned __int64 packetTimestamp() const { return packetActivityMs; }
	virtual PacketStatus packetState() const { return status; }
	virtual bool hasBeenSent() const { return (status == PacketOutputSent || status == PacketOutputTimeout || status == PacketOutputAck); }
	virtual void cancelPacket()
	{
		status = PacketCancel;
		if (packetEventHandler && subscribedPacketEvent != PacketNoMoreCallbacks)
			subscribedPacketEvent = (*packetEventHandler)(this, PacketCancel, packetEventContext);
	}
	virtual void releaseToCache();
	bool WasReceived() const { return status == PacketOutputAck; }

	static void* operator new(size_t size) { return malloc(size); }
	static void* operator new(size_t size, const char* file, __int32 line) { return malloc(size); }
	static void operator delete(void* mem) { free(mem); }
};

extern unsigned packetIdKey(const IntrusivePtr<PacketBuffer>& msg);

extern size_t packetPointerKey(const IntrusivePtr<PacketBuffer>& msg);

class PacketCache : public AtomicRefCount
{
protected:
	mutable std::recursive_mutex stateMutex;

	PacketCache();

	static IntrusivePtr<PacketCache> sharedCacheInstance;

	virtual ~PacketCache();

	std::unordered_map<unsigned, IntrusivePtr<PacketBuffer>> recycled;
	size_t recycledBytes = 0;
	std::unordered_map<size_t, IntrusivePtr<PacketBuffer>> used;
	std::deque<std::pair<size_t, unsigned>> garbageQueue;
	void garbageCollectStep();

	__int32 allocationsUntilSweep;

public:
	static PacketCache* sharedPacketCache() { return sharedCacheInstance.GetRef(); }
	virtual IntrusivePtr<PacketBuffer> acquirePacket(unsigned minLen, ChannelInterface* ch);
	virtual void reclaimPacket(PacketBuffer* msg);
	virtual void reclaimIdlePackets();
	virtual unsigned unusedMemory();
	virtual unsigned freeOneItem();
	virtual unsigned trimCachedPackets();
};

#endif
