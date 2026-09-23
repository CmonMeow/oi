
#include "netpch.hpp"
#include "netpeer.hpp"
#include "netchannel.hpp"

unsigned __int32 netMessageToSerial(const Ref<NetMessage>& msg)
{
	return msg ? msg->getSerial() : NULL;
}

unsigned __int32 netMessageDepend(const Ref<NetMessage>& msg)
{
	return msg ? ((unsigned __int32)msg->getHeader()->c.control2) : NULL;
}

void NetChannelBasic::InitSendQueue(unsigned __int64 time)
{
	memset((void*)&(sendQueue), 0, sizeof(sendQueue));
	for (__int32 i = 0; i < SLIDING_WINDOW_SEND; i += 2)
		sendQueue[i] = time;
	sendIndex = 0;
}

void NetChannelBasic::inputStatistics(NetMessage* msg)
{
	
	const unsigned __int32 ser = msg->getSerial();
	receivedSerials.on(ser);
	if (msg->header->flags & MSG_VIM_FLAG)
		recentVIMs++;
	if (ser > inputMax)
	{
		const unsigned gap = ser - inputMax;
		if (gap >= MAX_ACK_ARRAY)
		{
			memset(ack, NOT_RECEIVED, sizeof(ack));
			ackPtr = (ackPtr + gap % MAX_ACK_ARRAY) % MAX_ACK_ARRAY;
		}
		else
			for (unsigned i = 0; i < gap; ++i)
			{
				ackPtr = (ackPtr + 1) % MAX_ACK_ARRAY;
				ack[ackPtr] = NOT_RECEIVED;
			}
		inputMax = ser;
		if (inputMax - inputMin >= MAX_ACK_ARRAY)
			inputMin = inputMax - MAX_ACK_ARRAY + 1;
	}
	
	if (inputMax - receivedSerialWindowStart > networkParams.maxChannelBitMask)
	{
		__int32 newReceivedSerialWindowStart = inputMax - networkParams.maxChannelBitMask + GOOD_CHANNEL_BIT_MASK;
		receivedSerials.range(receivedSerialWindowStart, newReceivedSerialWindowStart - receivedSerialWindowStart, false);
		receivedSerials.growOptimize(true, newReceivedSerialWindowStart);
		processedSerials.range(receivedSerialWindowStart, newReceivedSerialWindowStart - receivedSerialWindowStart, false);
		processedSerials.growOptimize(true, newReceivedSerialWindowStart);
		receivedSerialWindowStart = newReceivedSerialWindowStart;
	}

	__int32 serI;
	if (ser >= inputMin)
	{
		serI = ackPtr - (inputMax - ser);
		if (serI < 0)
			serI += MAX_ACK_ARRAY;
		ack[serI] = networkParams.ackRedundancy; 
	}
	else
		serI = -1;
	
	__int32 i = ackPtr;
	unsigned __int32 notAck = inputMax - MAX_ACK_ARRAY;
	do
	{
		notAck++;
		if (++i >= MAX_ACK_ARRAY)
			i = 0;
		if (ack[i] > 0 && ack[i] != NOT_RECEIVED)
			break;
	} while (i != ackPtr);
	if (inputMax - notAck > 31 && 
		!urgentToSend &&
		!vimToSend &&
		!commonToSend)	   
		starvation = true; 

	lastMsgArrival = msg->refTime;
	if (serI >= 0)
		ackTime[serI] = (unsigned)lastMsgArrival;

	if (msg->header->flags & MSG_DELAY_FLAG)
	{ 
		lastPingArrival = msg->refTime;
		Ref<NetMessage> origMsg;

		{
			auto it = revisited.find(msg->header->ackOrigin);
			if (it != revisited.end())
				origMsg = it->second;
		}
		unsigned newActLatency;

		if (origMsg && (newActLatency = (unsigned)(msg->refTime - origMsg->refTime)) >= msg->header->c.control2)
		{

			actLatency = newActLatency - msg->header->c.control2;

			if (aveLatency)
				aveLatency = (unsigned)((1.0f - LATENCY_F) * aveLatency + LATENCY_F * actLatency);
			else
				aveLatency = actLatency;

			timeout = aveLatency * 3 + networkParams.ackTimeoutB;
			
			if ((heartBeatGap = timeout - aveLatency - (networkParams.ackTimeoutB >> 1)) > MAX_HEART_BEAT_GAP)
				heartBeatGap = MAX_HEART_BEAT_GAP;
			if (timeout > MAX_TIMEOUT)
				timeout = MAX_TIMEOUT;

			origMsg->waitForLatency = false;
		}
	}

	unsigned __int64 ack; 
	unsigned ackLen;
	unsigned __int32 s = msg->header->ackOrigin;
	if (SHORT_ACK(msg->header->flags))
	{
		ack = msg->header->c.control1;
		ackLen = 32;
	}
	else
	{
		ack = msg->header->ackBitmask;
		ackLen = 64;
	}
	unsigned __int32 oldest; 
	if (s >= ackLen - 1)
		oldest = s - ackLen + 1;
	else
		oldest = 0;

	newAcks = 0;  
	newBytes = 0; 

	bool wasNegative = false;
	unsigned __int32 highest = 0; 
	Ref<NetMessage> ackMsg;
	while (ack && s >= oldest)
	{ 
		if (ack & 1L)
		{ 
			{
				auto it = revisited.find(s);
				if (it != revisited.end())
					ackMsg = it->second;
				else
					ackMsg = NULL;
			}
			if (ackMsg && ackMsg->status == nsOutputAck)

				ackMsg = NULL;
			newAcknowledgement(s, ackMsg.GetRef());
		}
		else if (!wasNegative)
		{
			wasNegative = true;
			highest = s;
		}
		ack >>= 1;
		s--;
	}

	if (wasNegative)
	{
		s = recentPendingAckSerials.getFirst();
		while (s != BitMask::END && s <= highest)
		{
			recentPendingAckSerials.off(s);
			if (s >= ackMin)
				pendingAckSerials.on(s);
			s = recentPendingAckSerials.getNext(s);
		}
	}

	if (newAcks)
	{
		unsigned dt = (unsigned)(msg->refTime - ackStatQueue[ackStatIndex]);
		if (dt < ACK_QUEUE_GRANUL)
			ackStatQueue[ackStatIndex + 1] += newBytes;
		else
		{
			unsigned __int64 total = ackStatQueue[ackStatIndex + 1];
			if ((ackStatIndex += 2) >= SLIDING_WINDOW)
				ackStatIndex = 0;
			ackStatQueue[ackStatIndex] = msg->refTime;
			ackStatQueue[ackStatIndex + 1] = total + newBytes;
		}
	}
	
	checkConnectivityInternal(msg->refTime);
}

void NetChannelBasic::setDelayMessage(NetMessage* request)
{
	Ref<NetMessage> msg = urgentToSend; 
	while (msg && SHORT_ACK(msg->header->flags))
		msg = msg->next;
	if (!msg)
	{ 
		if (!commonToSend)
		{ 
			msg = NetMessagePool::pool()->newMessage(0, this);
		}
		else
		{
			msg = commonToSend; 
			commonToSend = msg->next;
			msg->next = NULL;
		}
		insertUrgent(msg.GetRef());
	}
	msg->header->flags |= MSG_DELAY_FLAG;
	msg->heartBeatRequest = request->getSerial();
	msg->heartBeatTime = request->refTime;
}

void NetChannelBasic::dispatchMessage(NetMessage* msg, bool urgent)
{
	if (!opened)
		return;
	if (msg->getLength() > MAX_PACKET_SIZE)
	{
		return;
	}
	if (msg->header->flags & MSG_VIM_FLAG)
	{
		msg->sendTimeout = 0;
		msg->firstTime = GetTickCount64();
	}
	else if (msg->sendTimeout) 
		msg->refTime = msg->firstTime = GetTickCount64();
	if (msg->header->flags & MSG_URGENT_FLAG)
		urgent = true;

	Critical_Section.lock();
	if (msg->header->flags & MSG_VIM_FLAG)
	{ 
		if (msg->header->flags & MSG_URGENT_FLAG)
			lastUrgent = msg;
		else
			lastVIM = msg;
	}

	if (urgent)
		insertUrgent(msg);
	else if (msg->header->flags & MSG_VIM_FLAG)
		insertVIM(msg);
	else
		insertCommon(msg);
	Critical_Section.unlock();
}

NetMessage* NetChannelBasic::getLastVIM(bool urgent)
{
	return (urgent ? lastUrgent.GetRef() : lastVIM.GetRef());
}

float NetChannelBasic::getAckBandwidth(unsigned __int64 windowSize)
{
	__int32 oldAckStat = ackStatIndex + 2;
	if (oldAckStat >= SLIDING_WINDOW)
		oldAckStat = 0;
	unsigned __int64 windowEdge = ackStatQueue[ackStatIndex] - windowSize;
	
	if (ackStatQueue[oldAckStat] > windowEdge)
		return ((1000.f * (ackStatQueue[ackStatIndex + 1] - ackStatQueue[oldAckStat + 1])) /
				(ackStatQueue[ackStatIndex] - ackStatQueue[oldAckStat]));
	
	__int32 probe = oldAckStat + 2;
	if (probe >= SLIDING_WINDOW)
		probe = 0;
	while (ackStatQueue[probe] <= windowEdge)
	{
		oldAckStat = probe;
		if ((probe += 2) >= SLIDING_WINDOW)
			probe = 0;
	}
	
	unsigned __int64 deltaProbe = ackStatQueue[ackStatIndex + 1] - ackStatQueue[probe + 1];
	unsigned __int64 deltaOld = ackStatQueue[ackStatIndex + 1] - ackStatQueue[oldAckStat + 1];
	
	float delta = deltaProbe + (deltaOld - deltaProbe) *
								   (float)(ackStatQueue[probe] - windowEdge) / (float)(ackStatQueue[probe] - ackStatQueue[oldAckStat]);
	return (1000.f * delta / windowSize);
}

bool NetChannelBasic::getPreparedMessage()
{
	if (!opened)
		return false;
	Critical_Section.lock();
	if (serial >= MAX_CHANNEL_SERIAL)
	{
		Critical_Section.unlock();
		return false;
	}
	bool result = false;
	
		result = getUrgentMessage();
		if (!result)
		{
			result = getVIMMessage();
			if (!result)
				result = getCommonMessage();
		}
		Critical_Section.unlock();
	return result;
}

bool NetChannelBasic::getUrgentMessage()
{
	if (!urgentToSend)
		return false;
	prepared = urgentToSend;
	urgentToSend = prepared->next;
	prepared->next = NULL;
	setOutputData(prepared.GetRef());
	return true;
}

bool NetChannelBasic::getVIMMessage()
{
	if (!vimToSend)
		return false;
	prepared = vimToSend;
	vimToSend = prepared->next;
	prepared->next = NULL;
	setOutputData(prepared.GetRef());
	return true;
}

bool NetChannelBasic::getCommonMessage()
{
	while (commonToSend && commonToSend->sendTimeout && GetTickCount64() > commonToSend->refTime + commonToSend->sendTimeout)
	{
		
		Ref<NetMessage> msg = commonToSend;
		commonToSend = msg->next;
		msg->next = NULL;
		msg->status = nsOutputObsolete;
		
		if (msg->msgProcessRoutine &&
			(msg->nextEvent == nsOutputSent ||		 
				msg->nextEvent == nsOutputTimeout || 
				msg->nextEvent == nsOutputObsolete))
		{ 
			Critical_Section.unlock();
			msg->nextEvent = (*msg->msgProcessRoutine)(msg.GetRef(), nsOutputObsolete, msg->dta);
			Critical_Section.lock();
		}
		
	}
	if (!commonToSend)
		return false;
	prepared = commonToSend;
	commonToSend = prepared->next;
	prepared->next = NULL;
	setOutputData(prepared.GetRef());
	return true;
}

void NetChannelBasic::insertResend(NetMessage* msg)

{
	if (!urgentToSend)
	{ 
		insertUrgent(msg);
		return;
	}
	if (msg->id < urgentToSend->id)
	{ 
		msg->next = urgentToSend;
		urgentToSend = msg;
		return;
	}
	Ref<NetMessage> ptr = urgentToSend;
	while (ptr->next && ptr->next->id < msg->id)
		ptr = ptr->next;
	insertUrgentAfter(msg, ptr.GetRef());
}

void NetChannelBasic::insertUrgent(NetMessage* msg)

{
	if (!urgentToSend)
		urgentToSend = urgentToSendEnd = msg;
	else
	{
		urgentToSendEnd->next = msg;
		urgentToSendEnd = msg;
	}
	msg->next = NULL; 
}

void NetChannelBasic::insertUrgentAfter(NetMessage* msg, NetMessage* after)

{
	if (!after || !urgentToSend)
	{
		insertUrgent(msg);
		return;
	}
	NetMessage* ptr = urgentToSend.GetRef();
	while (ptr && ptr != after)
		ptr = ptr->next.GetRef();
	if (!ptr)
	{
		insertUrgent(msg);
		return;
	}
	msg->next = after->next;
	after->next = msg;
	if (after == urgentToSendEnd)
		urgentToSendEnd = msg;
}

void NetChannelBasic::insertVIM(NetMessage* msg)

{
	if (!vimToSend)
		vimToSend = vimToSendEnd = msg;
	else
	{
		vimToSendEnd->next = msg;
		vimToSendEnd = msg;
	}
	msg->next = NULL; 
}

void NetChannelBasic::insertCommon(NetMessage* msg)

{
	if (!commonToSend)
		commonToSend = commonToSendEnd = msg;
	else
	{
		commonToSendEnd->next = msg;
		commonToSendEnd = msg;
	}
	msg->next = NULL; 
}

unsigned __int64 NetChannelBasic::preSend(unsigned __int64 bunchStart)
{
	
	unsigned __int64 previousMsgDeparture = lastMsgDeparture;
	lastMsgDeparture = GetTickCount64();
	if (!opened || !prepared)
		return lastMsgDeparture;
	prepared->refTime = lastMsgDeparture;
	
	if (prepared->header->flags & MSG_DELAY_FLAG)
		prepared->header->c.control2 = (unsigned __int32)(lastMsgDeparture - prepared->heartBeatTime);
	
	if (prepared->header->flags & MSG_INSTANT_FLAG)
		lastPingDeparture = lastMsgDeparture;
	
	if (prepared->setBunch(lastSerialSent + 1 == prepared->header->serial &&
						   bunchStart && previousMsgDeparture >= bunchStart &&
						   previousMsgDeparture >= lastMsgDeparture - MAX_PAIR_SEND_GAP))
		lastPairDeparture = lastMsgDeparture;
	lastSerialSent = prepared->header->serial;
	if (prepared->status == nsOutputPending)
	{ 
		if (lastSerialSent >= ackMin)
			recentPendingAckSerials.on(lastSerialSent);
		if (lastSerialSent >= ackMax)
			ackMax = lastSerialSent + 1;
	}

	return lastMsgDeparture;
}

void NetChannelBasic::postSend()
{
	if (!opened || !prepared)
	{
		prepared = NULL;
		return;
	}
	
	revisited[prepared->header->serial] = prepared.GetRef();
	
	unsigned sendBytes = prepared->header->length + IP_UDP_HEADER;

	dataSent(sendBytes, prepared->refTime);

	if (prepared->msgProcessRoutine &&
		(prepared->nextEvent == nsOutputSent ||		  
			prepared->nextEvent == nsOutputTimeout || 
			prepared->nextEvent == nsOutputObsolete)) 
	{
		prepared->nextEvent = (*prepared->msgProcessRoutine)(prepared.GetRef(), prepared->status, prepared->dta);
	}
	prepared = NULL; 
}

unsigned __int64 NetChannelBasic::getMessageTime(unsigned __int32 ser)
{
	Ref<NetMessage> msg;
	auto it = revisited.find(ser);
	if (it == revisited.end())
		return 0;
	msg = it->second;

	return msg->getTime();
}

void NetChannelBasic::setOutputData(NetMessage* msg)

{
	msg->next = NULL;
	if (msg->status == nsOutputSent ||
		msg->status == nsOutputTimeout ||
		msg->status == nsError)
	{								
		msg->ackTimeout += timeout; 
		if (msg->ackTimeout > MAX_ACK_TIMEOUT)
			msg->ackTimeout = MAX_ACK_TIMEOUT;
		msg->canBeSecondary = false;
	}
	else
	{ 
		msg->header->serial = serial++;
		if (serial == NULL)
			serial++;
		msg->status = nsOutputPending;
		msg->ackTimeout = timeout; 
		if (msg->header->flags & MSG_INSTANT_FLAG)
			msg->waitForLatency = true; 
	}

	unsigned __int32 newest; 
	unsigned __int32 oldest; 
	__int32 i;
	unsigned size = SHORT_ACK(msg->header->flags) ? 31 : 63;

	if ((msg->header->flags & MSG_DELAY_FLAG))
		newest = msg->heartBeatRequest;	 
	else								 
		if (ack[ackPtr] == NOT_RECEIVED) 
		newest = inputMax;
	else 
		if (oldAckFirst != oldAckLast)
	{ 

		newest = oldAckQueue[oldAckFirst++];
		if (oldAckFirst >= MAX_OLD_ACKS)
			oldAckFirst = 0;
	}
	else
	{ 
		oldest = inputMax - MAX_ACK_ARRAY;
		i = ackPtr;
		do
		{
			oldest++;
			if (++i >= MAX_ACK_ARRAY)
				i = 0;
			if (ack[i] > 0 && ack[i] != NOT_RECEIVED)
				break;
		} while (i != ackPtr);
		
		newest = oldest + size;
		if (newest > inputMax)
			newest = inputMax;
	}
	oldest = (newest >= size) ? newest - size : 0;

	msg->header->ackOrigin = newest;
	
	if (newest >= inputMin)
	{
		i = ackPtr - (inputMax - newest);
		if (i < 0)
			i += MAX_ACK_ARRAY; 
		unsigned j = newest;
		while (j >= inputMin && j >= oldest)
		{
			if (ack[i] > 0 && ack[i] != NOT_RECEIVED)
				ack[i]--;
			if (--i < 0)
				i = MAX_ACK_ARRAY - 1;
			j--;
		}
	}
	
	unsigned __int64 am = 0;
	while (oldest <= newest)
	{
		am <<= 1;
		if (receivedSerials.get(oldest++))
			am++;
	}
	recentVIMs = 0;

	if (SHORT_ACK(msg->header->flags))
	{
		
		if (msg->header->flags & MSG_ORDERED_FLAG)
		{ 
			if (msg->pred)
			{
				msg->header->c.control2 = msg->pred->getSerial();
				msg->pred = NULL;
			}
		}
		
		msg->header->c.control1 = (unsigned __int32)am;
	}
	else
	{
		
		msg->header->ackBitmask = am;
	}
}

const unsigned __int64 NetChannelBasic::RUN_INTERVAL = 50;

void NetChannelBasic::tick()
{
	unsigned __int64 now = GetTickCount64();

	if (runTime + RUN_INTERVAL < now)
	{
		if (!opened)
			return;
		Critical_Section.lock();
		runTime = now;
		Ref<NetMessage> msg;

		unsigned __int64 lostTime = now - (unsigned __int64)(2.5f * aveLatency + 150.f);
		
		for (const auto& kv : revisited)
		{
			msg = kv.second;
			if (!msg)
				continue;

			if (msg->refTime < lostTime)
			{
				unsigned s = msg->header->serial;
				if (recentPendingAckSerials.get(s))
				{
					recentPendingAckSerials.off(s);
					if (s >= ackMin)
						pendingAckSerials.on(s);
				}
			}
		}

		__int32 actual = pendingAckSerials.getFirst();
		while (actual != BitMask::END)
		{
			auto it = revisited.find(actual);
			msg = (it != revisited.end()) ? it->second : Ref<NetMessage>();
			if (!msg || msg->refTime + networkParams.outWindow < now)
				pendingAckSerials.off(actual);
			actual = pendingAckSerials.getNext(actual);
		}
		actual = recentPendingAckSerials.getFirst();
		while (actual != BitMask::END)
		{
			auto it = revisited.find(actual);
			msg = (it != revisited.end()) ? it->second : Ref<NetMessage>();
			if (!msg || msg->refTime + networkParams.outWindow < now)
				recentPendingAckSerials.off(actual);
			actual = recentPendingAckSerials.getNext(actual);
		}
		
		actual = ackMin;
		bool shiftMin = false;
		while ((unsigned)actual < ackMax)
		{
			auto it = revisited.find(actual);
			msg = (it != revisited.end()) ? it->second : Ref<NetMessage>();
			if (!msg || msg->refTime + networkParams.outWindow < now)
			{
				ackMin = actual + 1;
				shiftMin = true;
			}
			actual++;
		}
		if (shiftMin || ackMin + networkParams.maxOutputAckMask < ackMax)
		{
			if (ackMin + networkParams.maxOutputAckMask < ackMax)
				ackMin = ackMax - networkParams.maxOutputAckMask;

			actual = pendingAckSerials.getFirst();
			if ((unsigned)actual < ackMin)
				pendingAckSerials.range(actual, ackMin - actual, false);
			pendingAckSerials.growOptimize(true, ackMin);

			actual = recentPendingAckSerials.getFirst();
			if ((unsigned)actual < ackMin)
				recentPendingAckSerials.range(actual, ackMin - actual, false);
			recentPendingAckSerials.growOptimize(true, ackMin);
		}

		for (auto it = revisited.begin(); it != revisited.end();)
		{
			msg = it->second;
			if (!msg)
			{
				it = revisited.erase(it);
				continue;
			}

			if ((msg->header->flags & MSG_VIM_FLAG) && msg->status != nsOutputAck)
			{
				if (msg->refTime + msg->ackTimeout < now)
				{
					insertResend(msg.GetRef());
					it = revisited.erase(it);
					continue;
				}
			}
			else
			{
				unsigned __int64 useful = msg->ackTimeout << (msg->waitForLatency ? 2 : 1);
				if (useful < networkParams.outWindow)
					useful = networkParams.outWindow;
				if (msg->refTime + useful < now)
				{
					it = revisited.erase(it);
					continue;
				}
			}
			++it;
		}

		Critical_Section.unlock();
	}
}
