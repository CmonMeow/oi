#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETMESSAGE_H
#define _NETMESSAGE_H

#include <unordered_map>
#include <sysdef.h>

class NetMessagePool;

class NetMessage : public RefCountSafe
{
protected:
	NetChannel* channel;
	struct sockaddr_in distant;
	NetStatus status;

	union
	{
		MsgHeader* header;
		unsigned char* data;
	};

	unsigned totalLen;
	unsigned msgLen;
	unsigned __int64 refTime;
	unsigned __int64 firstTime;
	unsigned __int64 ackTimeout;
	unsigned __int64 sendTimeout;
	bool waitForLatency;
	bool canBeSecondary;
	NetCallBack* msgProcessRoutine;
	void* dta;

	NetStatus nextEvent;

	unsigned __int32 heartBeatRequest;
	unsigned __int64 heartBeatTime;

	Ref<NetMessage> pred;

	void init()
	{
		status = nsInvalidMessage;
		msgProcessRoutine = NULL;
		next = NULL;
		pred = NULL;
		channel = NULL;
		memset(data, 0, totalLen);
		header->serial = NULL;
		header->flags = 0;
		msgLen = header->length = sizeof(MsgHeader);
		header->ackOrigin = 1;
		header->ackBitmask = 0;
		nextEvent = nsInvalidMessage;
		memset((void*)&(distant), NULL, sizeof(distant));
		waitForLatency = false;
		canBeSecondary = true;
		refTime = firstTime = sendTimeout = ackTimeout = 0L;
	}
	virtual void setChannel(NetChannel* ch) { channel = ch; }

	friend class NetMessagePool;
	friend unsigned netMessageToUnsigned(const Ref<NetMessage>& msg);
	friend class NetChannelBasic;
	friend DWORD WINAPI udpListenSend(void* param);

	static unsigned __int32 nextId;

public:
	unsigned __int32 id;
	Ref<NetMessage> next;

	NetMessage(unsigned len)
	{
		if (len < sizeof(MsgHeader))
			len = sizeof(MsgHeader);
		data = (unsigned char*)malloc(totalLen = len);
		msgProcessRoutine = NULL;
		dta = NULL;
		init();
	}
	virtual ~NetMessage()
	{
		init();
		if (data)
		{
			free(data);
			data = NULL;
		}
	}
	virtual bool setMessage(const MsgHeader* hdr)
	{
		if (hdr->length > totalLen)
		{
			Error("Message overflow %u>%u", (unsigned)hdr->length, (unsigned)totalLen);
			status = nsInvalidMessage;
			nextEvent = nsInvalidMessage;
			return false;
		}

		memcpy(data, (const char*)hdr, hdr->length);
		msgLen = hdr->length;
		refTime = firstTime = GetTickCount64();
		return true;
	}
	virtual void setFrom(const NetMessage* msg)
	{
		channel = msg->channel;
		distant = msg->distant;
		status = msg->status;
		header->flags = msg->header->flags & ~(MSG_PART_FLAG | MSG_CLOSING_FLAG);
		header->serial = msg->header->serial;
		header->ackOrigin = msg->header->ackOrigin;
		header->ackBitmask = msg->header->ackBitmask;

		refTime = msg->refTime;
		firstTime = msg->firstTime;
		ackTimeout = msg->ackTimeout;
		sendTimeout = msg->sendTimeout;
		waitForLatency = msg->waitForLatency;
		canBeSecondary = msg->canBeSecondary;
		msgProcessRoutine = msg->msgProcessRoutine;
		dta = msg->dta;
		nextEvent = msg->nextEvent;
		heartBeatRequest = msg->heartBeatRequest;
		heartBeatTime = msg->heartBeatTime;
		pred = msg->pred;
		next = NULL;
	}
	virtual void setData(const unsigned char* _data, unsigned __int32 length)
	{
		if (length + sizeof(MsgHeader) > totalLen)
		{
			Error("setData length %d>%d", length, totalLen - sizeof(MsgHeader));
			length = totalLen - sizeof(MsgHeader);
		}
		if (length)
			memcpy(data + sizeof(MsgHeader), _data, length);
		msgLen = length + sizeof(MsgHeader);
		header->length = (msgLen > USHRT_MAX) ? USHRT_MAX : msgLen;
	}
	virtual void setLength(unsigned length)
	{
		if (length + sizeof(MsgHeader) > totalLen)
			length = totalLen - sizeof(MsgHeader);
		msgLen = length + sizeof(MsgHeader);
		header->length = (msgLen > USHRT_MAX) ? USHRT_MAX : msgLen;
	}
	virtual void setFlags(unsigned short andMask, unsigned short orMask)
	{
		andMask |= (MSG_DELAY_FLAG | MSG_ORDERED_FLAG |
					MSG_BUNCH_FLAG | MSG_DUMMY_FLAG);
		orMask &= ~(MSG_DELAY_FLAG | MSG_ORDERED_FLAG |
					MSG_BUNCH_FLAG | MSG_DUMMY_FLAG);
		header->flags &= andMask;
		header->flags |= orMask;
	}
	virtual bool setBunch(bool bunch)
	{
		if (bunch && canBeSecondary)
		{
			header->flags |= MSG_BUNCH_FLAG;
			return true;
		}
		header->flags &= ~MSG_BUNCH_FLAG;
		return false;
	}
	virtual void setOrdered(NetMessage* _pred)
	{
		if (_pred)
		{ 
			if (!(_pred->header->flags & MSG_VIM_FLAG))
				return;
			header->flags |= (MSG_ORDERED_FLAG | MSG_VIM_FLAG);
			if (_pred->getSerial() == NULL)
				pred = _pred; 
			else
			{
				pred = NULL; 
				header->c.control2 = _pred->getSerial();
			}
		}
		else
		{ 
			header->flags &= ~MSG_ORDERED_FLAG;
			pred = NULL;
		}
	}
	virtual void setOrderedPrevious()
	{
		channel->AddRef();
		pred = channel->getLastVIM((header->flags & MSG_URGENT_FLAG) != 0);
		channel->Release();
		if (pred)
		{
			header->flags |= (MSG_ORDERED_FLAG | MSG_VIM_FLAG);
			if (pred->header->flags & MSG_URGENT_FLAG)
				header->flags |= MSG_URGENT_FLAG;
			if (pred->getSerial() != NULL)
			{ 
				header->c.control2 = pred->getSerial();
				pred = NULL; 
			}
		}
		else
			header->flags &= ~MSG_ORDERED_FLAG;
	}
	virtual void setSendTimeout(unsigned __int64 timeout) { sendTimeout = timeout; }
	virtual void setCallback(NetCallBack* routine, NetStatus event, void* _dta)
	{
		msgProcessRoutine = routine;
		dta = _dta;
		nextEvent = routine ? event : nsInvalidMessage;
	}
	virtual void send(bool urgent = false)
	{
		if (channel)
			channel->dispatchMessage(this, urgent);
	}
	virtual MsgHeader* getHeader() const { return header; }
	virtual unsigned short getFlags() const { return header->flags; }
	virtual unsigned getLength() const { return (msgLen - sizeof(MsgHeader)); }
	virtual void* getData() const { return (data ? data + sizeof(MsgHeader) : NULL); }
	virtual void getDistant(struct sockaddr_in& _distant) const { _distant = distant; }
	virtual void setDistant(struct sockaddr_in& _distant) { distant = _distant; }
	virtual unsigned __int32 getSerial() const { return header->serial; }
	virtual NetChannel* getChannel() const { return channel; }
	virtual unsigned __int64 getTime() const { return refTime; }
	virtual NetStatus getStatus() const { return status; }
	virtual bool wasSent() const { return (status == nsOutputSent || status == nsOutputTimeout || status == nsOutputAck); }
	virtual void cancel()
	{
		status = nsCancel;
		if (msgProcessRoutine && nextEvent != nsNoMoreCallbacks)
			nextEvent = (*msgProcessRoutine)(this, nsCancel, dta);
	}
	virtual void recycle();
	bool WasReceived() const { return status == nsOutputAck; }

	static void* operator new(size_t size) { return malloc(size); }
	static void* operator new(size_t size, const char* file, __int32 line) { return malloc(size); }
	static void operator delete(void* mem) { free(mem); }
};

extern unsigned netMessageToUnsigned(const Ref<NetMessage>& msg);

extern size_t netMessageAddressToUnsigned(const Ref<NetMessage>& msg);

class NetMessagePool : public RefCountSafe
{
protected:
	mutable std::recursive_mutex Critical_Section;

	NetMessagePool();

	static Ref<NetMessagePool> msgPool;

	virtual ~NetMessagePool();

	std::unordered_map<unsigned, Ref<NetMessage>> recycled;
	std::unordered_map<size_t, Ref<NetMessage>> used;

	__int32 garbageCounter;

public:
	static NetMessagePool* pool() { return msgPool.GetRef(); }
	virtual Ref<NetMessage> newMessage(unsigned minLen, NetChannel* ch);
	virtual void recycleMessage(NetMessage* msg);
	virtual void garbageCollect();
	virtual unsigned unusedMemory();
	virtual unsigned freeOneItem();
	virtual unsigned freeMemory();
};

#endif
