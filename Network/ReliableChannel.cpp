
#include "TransportIncludes.hpp"
#include "UdpEndpoint.hpp"
#include "ReliableChannel.hpp"

unsigned __int64 packetSequenceKey(const IntrusivePtr<PacketBuffer>& msg)
{
	return msg ? msg->sequenceNumber() : NULL;
}

unsigned __int64 packetDependencyKey(const IntrusivePtr<PacketBuffer>& msg)
{
	unsigned __int64 result = 0;
    if (msg && msg->datagramHeader()->ordered.predecessorSequence)
        expandWireSequence(msg->datagramHeader()->ordered.predecessorSequence, msg->sequenceNumber(), result);
    return result;
}

void ReliableChannel::resetSendRateSamples(unsigned __int64 time)
{
	memset((void*)&(sendRateSamples), 0, sizeof(sendRateSamples));
	for (__int32 i = 0; i < SendRateSampleSlots; i += 2)
		sendRateSamples[i] = time;
	sendRateWriteIndex = 0;
}

void ReliableChannel::updateReceiveMetrics(PacketBuffer* msg)
{
	
	const unsigned __int64 ser = msg->sequenceNumber();
	receivedSerials.on(ser);
	if (msg->header->flags & PACKET_RELIABLE)
		reliablePacketsSinceAck++;
	if (ser > highestReceivedSequence)
	{
		const unsigned gap = static_cast<unsigned>(ser - highestReceivedSequence);
		if (gap >= ACK_RING_CAPACITY)
		{
			memset(ack, ACK_SLOT_EMPTY, sizeof(ack));
			ackRingCursor = (ackRingCursor + gap % ACK_RING_CAPACITY) % ACK_RING_CAPACITY;
		}
		else
			for (unsigned i = 0; i < gap; ++i)
			{
				ackRingCursor = (ackRingCursor + 1) % ACK_RING_CAPACITY;
				ack[ackRingCursor] = ACK_SLOT_EMPTY;
			}
		highestReceivedSequence = ser;
		if (highestReceivedSequence - ackRingFirstSequence >= ACK_RING_CAPACITY)
			ackRingFirstSequence = highestReceivedSequence - ACK_RING_CAPACITY + 1;
	}
	
	if (highestReceivedSequence - receivedSerialWindowStart > transportTuning.receiveSequenceWindowSize)
	{
		unsigned __int64 newReceivedSerialWindowStart = highestReceivedSequence - transportTuning.receiveSequenceWindowSize + RECEIVE_WINDOW_TRIM_SLACK;
		receivedSerials.range(receivedSerialWindowStart, newReceivedSerialWindowStart - receivedSerialWindowStart, false);
		receivedSerials.growOptimize(true, newReceivedSerialWindowStart);
		processedSerials.range(receivedSerialWindowStart, newReceivedSerialWindowStart - receivedSerialWindowStart, false);
		processedSerials.growOptimize(true, newReceivedSerialWindowStart);
		receivedSerialWindowStart = newReceivedSerialWindowStart;
	}

	__int32 serI;
	if (ser >= ackRingFirstSequence)
	{
		serI = ackRingCursor - static_cast<int>(highestReceivedSequence - ser);
		if (serI < 0)
			serI += ACK_RING_CAPACITY;
		ack[serI] = transportTuning.ackRepeatCount; 
	}
	else
		serI = -1;
	
	__int32 i = ackRingCursor;
	unsigned __int64 notAck = highestReceivedSequence - ACK_RING_CAPACITY;
	do
	{
		notAck++;
		if (++i >= ACK_RING_CAPACITY)
			i = 0;
		if (ack[i] > 0 && ack[i] != ACK_SLOT_EMPTY)
			break;
	} while (i != ackRingCursor);
	if (highestReceivedSequence - notAck > 31 && 
		!priorityQueueHead &&
		!reliableQueueHead &&
		!bestEffortQueueHead)	   
		starvation = true; 

	lastPacketReceivedMs = msg->packetActivityMs;
	if (serI >= 0)
		ackTime[serI] = (unsigned)lastPacketReceivedMs;

	unsigned __int64 ackOrigin = 0;
    const bool validAck = expandWireSequence(msg->header->ackBaseSequence, serial - 1, ackOrigin) && ackOrigin < serial;
    if (validAck && (msg->header->flags & PACKET_PING_REPLY))
	{ 
		lastPingReceivedMs = msg->packetActivityMs;
		IntrusivePtr<PacketBuffer> origMsg;

		{
			auto it = sentPacketsBySequence.find(ackOrigin);
			if (it != sentPacketsBySequence.end())
				origMsg = it->second;
		}
		unsigned measuredRttMs;

		if (origMsg && (measuredRttMs = (unsigned)(msg->packetActivityMs - origMsg->packetActivityMs)) >= msg->header->pingReply.replyDelayMs)
		{

			latestRttMs = measuredRttMs - msg->header->pingReply.replyDelayMs;

			if (smoothedRttMs)
				smoothedRttMs = (unsigned)((1.0f - RTT_SMOOTHING_WEIGHT) * smoothedRttMs + RTT_SMOOTHING_WEIGHT * latestRttMs);
			else
				smoothedRttMs = latestRttMs;

			timeout = smoothedRttMs * 3 + transportTuning.ackTimeoutPaddingMs;
			
			if ((heartbeatIntervalMs = timeout - smoothedRttMs - (transportTuning.ackTimeoutPaddingMs >> 1)) > HEARTBEAT_INTERVAL_LIMIT_MS)
				heartbeatIntervalMs = HEARTBEAT_INTERVAL_LIMIT_MS;
			if (timeout > BASE_RETRANSMIT_DELAY_LIMIT_MS)
				timeout = BASE_RETRANSMIT_DELAY_LIMIT_MS;

			origMsg->awaitingRttSample = false;
		}
	}

	unsigned __int64 ack;
	unsigned ackLen;
	unsigned __int64 s = ackOrigin;
	if (PACKET_HAS_SHORT_ACK(msg->header->flags))
	{
		ack = (msg->header->flags & PACKET_ORDERED)
			? msg->header->ordered.ackBits : msg->header->pingReply.ackBits;
		ackLen = 32;
	}
	else
	{
		ack = msg->header->ackBits;
		ackLen = 64;
	}
	if (!validAck) ack = 0;
	unsigned __int64 oldest;
	if (s >= ackLen - 1)
		oldest = s - ackLen + 1;
	else
		oldest = 0;

	newAcks = 0;  
	newBytes = 0; 

	bool wasNegative = false;
	unsigned __int64 highest = 0;
	IntrusivePtr<PacketBuffer> ackMsg;
	while (ack && s >= oldest)
	{ 
		if (ack & 1L)
		{ 
			{
				auto it = sentPacketsBySequence.find(s);
				if (it != sentPacketsBySequence.end())
					ackMsg = it->second;
				else
					ackMsg = NULL;
			}
			if (ackMsg && ackMsg->status == PacketOutputAck)

				ackMsg = NULL;
			recordAcknowledgement(s, ackMsg.GetRef());
		}
		else if (!wasNegative)
		{
			wasNegative = true;
			highest = s;
		}
		ack >>= 1;
		if (s == 0) break;
		s--;
	}

	if (wasNegative)
	{
		s = recentPendingAckSerials.getFirst();
		while (s != SequenceBitmap::END && s <= highest)
		{
			recentPendingAckSerials.off(s);
			if (s >= ackMin)
				pendingAckSerials.on(s);
			s = recentPendingAckSerials.getNext(s);
		}
	}

	if (newAcks)
	{
		unsigned dt = (unsigned)(msg->packetActivityMs - ackRateSamples[ackRateWriteIndex]);
		if (dt < ACK_RATE_SAMPLE_INTERVAL_MS)
			ackRateSamples[ackRateWriteIndex + 1] += newBytes;
		else
		{
			unsigned __int64 total = ackRateSamples[ackRateWriteIndex + 1];
			if ((ackRateWriteIndex += 2) >= AckRateSampleSlots)
				ackRateWriteIndex = 0;
			ackRateSamples[ackRateWriteIndex] = msg->packetActivityMs;
			ackRateSamples[ackRateWriteIndex + 1] = total + newBytes;
		}
	}
	
	updateLivenessLocked(msg->packetActivityMs);
}

void ReliableChannel::queueDelayedAcknowledgement(PacketBuffer* request)
{
	IntrusivePtr<PacketBuffer> msg = priorityQueueHead; 
	while (msg && PACKET_HAS_SHORT_ACK(msg->header->flags))
		msg = msg->next;
	if (!msg)
	{ 
		if (!bestEffortQueueHead)
		{ 
			msg = PacketCache::sharedPacketCache()->acquirePacket(0, this);
		}
		else
		{
			msg = bestEffortQueueHead; 
			bestEffortQueueHead = msg->next;
			msg->next = NULL;
		}
		enqueuePriority(msg.GetRef());
	}
	msg->header->flags |= PACKET_PING_REPLY;
	msg->heartbeatRequestSequence = request->sequenceNumber();
	msg->heartbeatReceivedMs = request->packetActivityMs;
}

void ReliableChannel::queueOutboundPacket(PacketBuffer* msg, bool urgent)
{
	if (!opened)
		return;
	if (msg->payloadLength() > DATAGRAM_PAYLOAD_LIMIT_BYTES)
	{
		return;
	}
	if (msg->header->flags & PACKET_RELIABLE)
	{
		msg->deliveryLifetimeMs = 0;
		msg->packetStartedMs = GetTickCount64();
	}
	else if (msg->deliveryLifetimeMs) 
		msg->packetActivityMs = msg->packetStartedMs = GetTickCount64();
	if (msg->header->flags & PACKET_PRIORITY)
		urgent = true;

	stateMutex.lock();
	if (msg->header->flags & PACKET_RELIABLE)
	{ 
		if (msg->header->flags & PACKET_PRIORITY)
			lastPriorityPacket = msg;
		else
			lastReliableQueued = msg;
	}

	if (urgent)
		enqueuePriority(msg);
	else if (msg->header->flags & PACKET_RELIABLE)
		enqueueReliable(msg);
	else
		enqueueBestEffort(msg);
	stateMutex.unlock();
}

PacketBuffer* ReliableChannel::lastReliablePacket(bool urgent)
{
	return (urgent ? lastPriorityPacket.GetRef() : lastReliableQueued.GetRef());
}

float ReliableChannel::estimateAcknowledgedRate(unsigned __int64 windowSize)
{
	__int32 ackRateReadIndex = ackRateWriteIndex + 2;
	if (ackRateReadIndex >= AckRateSampleSlots)
		ackRateReadIndex = 0;
	unsigned __int64 windowEdge = ackRateSamples[ackRateWriteIndex] - windowSize;
	
	if (ackRateSamples[ackRateReadIndex] > windowEdge)
		return ((1000.f * (ackRateSamples[ackRateWriteIndex + 1] - ackRateSamples[ackRateReadIndex + 1])) /
				(ackRateSamples[ackRateWriteIndex] - ackRateSamples[ackRateReadIndex]));
	
	__int32 probe = ackRateReadIndex + 2;
	if (probe >= AckRateSampleSlots)
		probe = 0;
	while (ackRateSamples[probe] <= windowEdge)
	{
		ackRateReadIndex = probe;
		if ((probe += 2) >= AckRateSampleSlots)
			probe = 0;
	}
	
	unsigned __int64 deltaProbe = ackRateSamples[ackRateWriteIndex + 1] - ackRateSamples[probe + 1];
	unsigned __int64 deltaOld = ackRateSamples[ackRateWriteIndex + 1] - ackRateSamples[ackRateReadIndex + 1];
	
	float delta = deltaProbe + (deltaOld - deltaProbe) *
								   (float)(ackRateSamples[probe] - windowEdge) / (float)(ackRateSamples[probe] - ackRateSamples[ackRateReadIndex]);
	return (1000.f * delta / windowSize);
}

bool ReliableChannel::prepareNextPacket()
{
	if (!opened)
		return false;
	stateMutex.lock();
	if (serial >= MAX_LOCAL_SEQUENCE)
	{
		stateMutex.unlock();
		return false;
	}
	bool result = false;
	
		result = preparePriorityPacket();
		if (!result)
		{
			result = prepareReliablePacket();
			if (!result)
				result = prepareBestEffortPacket();
		}
		stateMutex.unlock();
	return result;
}

bool ReliableChannel::preparePriorityPacket()
{
	if (!priorityQueueHead)
		return false;
	prepared = priorityQueueHead;
	priorityQueueHead = prepared->next;
	prepared->next = NULL;
	writeAcknowledgements(prepared.GetRef());
	return true;
}

bool ReliableChannel::prepareReliablePacket()
{
	if (!reliableQueueHead)
		return false;
	prepared = reliableQueueHead;
	reliableQueueHead = prepared->next;
	prepared->next = NULL;
	writeAcknowledgements(prepared.GetRef());
	return true;
}

bool ReliableChannel::prepareBestEffortPacket()
{
	while (bestEffortQueueHead && bestEffortQueueHead->deliveryLifetimeMs && GetTickCount64() > bestEffortQueueHead->packetActivityMs + bestEffortQueueHead->deliveryLifetimeMs)
	{
		
		IntrusivePtr<PacketBuffer> msg = bestEffortQueueHead;
		bestEffortQueueHead = msg->next;
		msg->next = NULL;
		msg->status = PacketOutputObsolete;
		
		if (msg->packetEventHandler &&
			(msg->subscribedPacketEvent == PacketOutputSent ||		 
				msg->subscribedPacketEvent == PacketOutputTimeout || 
				msg->subscribedPacketEvent == PacketOutputObsolete))
		{ 
			stateMutex.unlock();
			msg->subscribedPacketEvent = (*msg->packetEventHandler)(msg.GetRef(), PacketOutputObsolete, msg->packetEventContext);
			stateMutex.lock();
		}
		
	}
	if (!bestEffortQueueHead)
		return false;
	prepared = bestEffortQueueHead;
	bestEffortQueueHead = prepared->next;
	prepared->next = NULL;
	writeAcknowledgements(prepared.GetRef());
	return true;
}

void ReliableChannel::enqueueRetransmission(PacketBuffer* msg)

{
	if (!priorityQueueHead)
	{ 
		enqueuePriority(msg);
		return;
	}
	if (msg->id < priorityQueueHead->id)
	{ 
		msg->next = priorityQueueHead;
		priorityQueueHead = msg;
		return;
	}
	IntrusivePtr<PacketBuffer> ptr = priorityQueueHead;
	while (ptr->next && ptr->next->id < msg->id)
		ptr = ptr->next;
	enqueuePriorityAfter(msg, ptr.GetRef());
}

void ReliableChannel::enqueuePriority(PacketBuffer* msg)

{
	if (!priorityQueueHead)
		priorityQueueHead = priorityQueueTail = msg;
	else
	{
		priorityQueueTail->next = msg;
		priorityQueueTail = msg;
	}
	msg->next = NULL; 
}

void ReliableChannel::enqueuePriorityAfter(PacketBuffer* msg, PacketBuffer* after)

{
	if (!after || !priorityQueueHead)
	{
		enqueuePriority(msg);
		return;
	}
	PacketBuffer* ptr = priorityQueueHead.GetRef();
	while (ptr && ptr != after)
		ptr = ptr->next.GetRef();
	if (!ptr)
	{
		enqueuePriority(msg);
		return;
	}
	msg->next = after->next;
	after->next = msg;
	if (after == priorityQueueTail)
		priorityQueueTail = msg;
}

void ReliableChannel::enqueueReliable(PacketBuffer* msg)

{
	if (!reliableQueueHead)
		reliableQueueHead = reliableQueueTail = msg;
	else
	{
		reliableQueueTail->next = msg;
		reliableQueueTail = msg;
	}
	msg->next = NULL; 
}

void ReliableChannel::enqueueBestEffort(PacketBuffer* msg)

{
	if (!bestEffortQueueHead)
		bestEffortQueueHead = bestEffortQueueTail = msg;
	else
	{
		bestEffortQueueTail->next = msg;
		bestEffortQueueTail = msg;
	}
	msg->next = NULL; 
}

unsigned __int64 ReliableChannel::beginSendBatch(unsigned __int64 bunchStart)
{
	
	unsigned __int64 previousMsgDeparture = lastPacketSentMs;
	lastPacketSentMs = GetTickCount64();
	if (!opened || !prepared)
		return lastPacketSentMs;
	prepared->packetActivityMs = lastPacketSentMs;
	
	if (prepared->header->flags & PACKET_PING_REPLY)
		prepared->header->pingReply.replyDelayMs = (unsigned __int32)(lastPacketSentMs - prepared->heartbeatReceivedMs);
	
	if (prepared->header->flags & PACKET_PING_REQUEST)
		lastPingSentMs = lastPacketSentMs;
	
	if (prepared->markBatchMember(lastSentSequence + 1 == prepared->sequenceNumber() &&
						   bunchStart && previousMsgDeparture >= bunchStart &&
						   previousMsgDeparture >= lastPacketSentMs - PACKET_PAIR_SEND_GAP_MS))
		lastProbePairSentMs = lastPacketSentMs;
	lastSentSequence = prepared->sequenceNumber();
	if (prepared->status == PacketOutputPending)
	{ 
		if (lastSentSequence >= ackMin)
			recentPendingAckSerials.on(lastSentSequence);
		if (lastSentSequence >= ackMax)
			ackMax = lastSentSequence + 1;
	}

	return lastPacketSentMs;
}

void ReliableChannel::finishSendBatch()
{
	if (!opened || !prepared)
	{
		prepared = NULL;
		return;
	}
	
	sentPacketsBySequence[prepared->sequenceNumber()] = prepared.GetRef();
	
	unsigned sendBytes = prepared->header->length + IPV4_UDP_HEADER_BYTES;

	dataSent(sendBytes, prepared->packetActivityMs);

	if (prepared->packetEventHandler &&
		(prepared->subscribedPacketEvent == PacketOutputSent ||		  
			prepared->subscribedPacketEvent == PacketOutputTimeout || 
			prepared->subscribedPacketEvent == PacketOutputObsolete)) 
	{
		prepared->subscribedPacketEvent = (*prepared->packetEventHandler)(prepared.GetRef(), prepared->status, prepared->packetEventContext);
	}
	prepared = NULL; 
}

unsigned __int64 ReliableChannel::packetSendTime(unsigned __int64 ser)
{
	IntrusivePtr<PacketBuffer> msg;
	auto it = sentPacketsBySequence.find(ser);
	if (it == sentPacketsBySequence.end())
		return 0;
	msg = it->second;

	return msg->packetTimestamp();
}

void ReliableChannel::writeAcknowledgements(PacketBuffer* msg)

{
	msg->next = NULL;
	if (msg->status == PacketOutputSent ||
		msg->status == PacketOutputTimeout ||
		msg->status == PacketError)
	{								
		msg->retransmitDelayMs += timeout; 
		if (msg->retransmitDelayMs > RETRANSMIT_DELAY_LIMIT_MS)
			msg->retransmitDelayMs = RETRANSMIT_DELAY_LIMIT_MS;
		msg->allowBatching = false;
	}
	else
	{ 
		msg->localSequence = serial++;
        msg->header->serial = static_cast<unsigned __int32>(msg->localSequence);
        // Zero still means "not assigned" / "no ordered predecessor" on wire.
        if (static_cast<unsigned __int32>(serial) == 0) ++serial;
		msg->status = PacketOutputPending;
		msg->retransmitDelayMs = timeout; 
		if (msg->header->flags & PACKET_PING_REQUEST)
			msg->awaitingRttSample = true; 
	}

	unsigned __int64 newest;
	unsigned __int64 oldest;
	__int32 i;
	unsigned size = PACKET_HAS_SHORT_ACK(msg->header->flags) ? 31 : 63;

	if ((msg->header->flags & PACKET_PING_REPLY))
		newest = msg->heartbeatRequestSequence;	 
	else								 
		if (ack[ackRingCursor] == ACK_SLOT_EMPTY) 
		newest = highestReceivedSequence;
	else 
		if (recentAckHead != recentAckTail)
	{ 

		newest = recentAckQueue[recentAckHead++];
		if (recentAckHead >= RECENT_ACK_CAPACITY)
			recentAckHead = 0;
	}
	else
	{ 
		oldest = highestReceivedSequence - ACK_RING_CAPACITY;
		i = ackRingCursor;
		do
		{
			oldest++;
			if (++i >= ACK_RING_CAPACITY)
				i = 0;
			if (ack[i] > 0 && ack[i] != ACK_SLOT_EMPTY)
				break;
		} while (i != ackRingCursor);
		
		newest = oldest + size;
		if (newest > highestReceivedSequence)
			newest = highestReceivedSequence;
	}
	oldest = (newest >= size) ? newest - size : 0;

	msg->header->ackBaseSequence = static_cast<unsigned __int32>(newest);
	
	if (newest >= ackRingFirstSequence)
	{
		i = ackRingCursor - static_cast<int>(highestReceivedSequence - newest);
		if (i < 0)
			i += ACK_RING_CAPACITY; 
		unsigned __int64 j = newest;
		while (j >= ackRingFirstSequence && j >= oldest)
		{
			if (ack[i] > 0 && ack[i] != ACK_SLOT_EMPTY)
				ack[i]--;
			if (--i < 0)
				i = ACK_RING_CAPACITY - 1;
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
	reliablePacketsSinceAck = 0;

	if (PACKET_HAS_SHORT_ACK(msg->header->flags))
	{
		
		if (msg->header->flags & PACKET_ORDERED)
		{ 
			if (msg->orderingPredecessor)
			{
				msg->header->ordered.predecessorSequence = static_cast<unsigned __int32>(msg->orderingPredecessor->sequenceNumber());
				msg->orderingPredecessor = NULL;
			}
		}
		
		if (msg->header->flags & PACKET_ORDERED)
			msg->header->ordered.ackBits = (unsigned __int32)am;
		else
			msg->header->pingReply.ackBits = (unsigned __int32)am;
	}
	else
	{
		
		msg->header->ackBits = am;
	}
}

const unsigned __int64 ReliableChannel::RetransmitScanIntervalMs = 50;

void ReliableChannel::serviceChannel()
{
	unsigned __int64 now = GetTickCount64();

	if (lastRetransmitScanMs + RetransmitScanIntervalMs < now)
	{
		if (!opened)
			return;
		stateMutex.lock();
		lastRetransmitScanMs = now;
		IntrusivePtr<PacketBuffer> msg;

		unsigned __int64 lostTime = now - (unsigned __int64)(2.5f * smoothedRttMs + 150.f);
		
		for (const auto& kv : sentPacketsBySequence)
		{
			msg = kv.second;
			if (!msg)
				continue;

			if (msg->packetActivityMs < lostTime)
			{
				unsigned __int64 s = msg->sequenceNumber();
				if (recentPendingAckSerials.get(s))
				{
					recentPendingAckSerials.off(s);
					if (s >= ackMin)
						pendingAckSerials.on(s);
				}
			}
		}

		unsigned __int64 actual = pendingAckSerials.getFirst();
		while (actual != SequenceBitmap::END)
		{
			auto it = sentPacketsBySequence.find(actual);
			msg = (it != sentPacketsBySequence.end()) ? it->second : IntrusivePtr<PacketBuffer>();
			if (!msg || msg->packetActivityMs + transportTuning.retransmitHistoryMs < now)
				pendingAckSerials.off(actual);
			actual = pendingAckSerials.getNext(actual);
		}
		actual = recentPendingAckSerials.getFirst();
		while (actual != SequenceBitmap::END)
		{
			auto it = sentPacketsBySequence.find(actual);
			msg = (it != sentPacketsBySequence.end()) ? it->second : IntrusivePtr<PacketBuffer>();
			if (!msg || msg->packetActivityMs + transportTuning.retransmitHistoryMs < now)
				recentPendingAckSerials.off(actual);
			actual = recentPendingAckSerials.getNext(actual);
		}
		
		actual = ackMin;
		bool shiftMin = false;
		while (actual < ackMax)
		{
			auto it = sentPacketsBySequence.find(actual);
			msg = (it != sentPacketsBySequence.end()) ? it->second : IntrusivePtr<PacketBuffer>();
			if (!msg || msg->packetActivityMs + transportTuning.retransmitHistoryMs < now)
			{
				ackMin = actual + 1;
				shiftMin = true;
			}
			actual++;
		}
		if (shiftMin || ackMin + transportTuning.pendingAckWindowSize < ackMax)
		{
			if (ackMin + transportTuning.pendingAckWindowSize < ackMax)
				ackMin = ackMax - transportTuning.pendingAckWindowSize;

			actual = pendingAckSerials.getFirst();
			if (actual < ackMin)
				pendingAckSerials.range(actual, ackMin - actual, false);
			pendingAckSerials.growOptimize(true, ackMin);

			actual = recentPendingAckSerials.getFirst();
			if (actual < ackMin)
				recentPendingAckSerials.range(actual, ackMin - actual, false);
			recentPendingAckSerials.growOptimize(true, ackMin);
		}

		for (auto it = sentPacketsBySequence.begin(); it != sentPacketsBySequence.end();)
		{
			msg = it->second;
			if (!msg)
			{
				it = sentPacketsBySequence.erase(it);
				continue;
			}

			if ((msg->header->flags & PACKET_RELIABLE) && msg->status != PacketOutputAck)
			{
				if (msg->packetActivityMs + msg->retransmitDelayMs < now)
				{
					enqueueRetransmission(msg.GetRef());
					it = sentPacketsBySequence.erase(it);
					continue;
				}
			}
			else
			{
				unsigned __int64 useful = msg->retransmitDelayMs << (msg->awaitingRttSample ? 2 : 1);
				if (useful < transportTuning.retransmitHistoryMs)
					useful = transportTuning.retransmitHistoryMs;
				if (msg->packetActivityMs + useful < now)
				{
					it = sentPacketsBySequence.erase(it);
					continue;
				}
			}
			++it;
		}

		stateMutex.unlock();
	}
}
