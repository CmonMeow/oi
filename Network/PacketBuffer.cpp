
#include "TransportIncludes.hpp"

unsigned __int32 PacketBuffer::nextId = 1;

void PacketBuffer::releaseToCache()
{
	PacketCache::sharedPacketCache()->reclaimPacket(this);
}

const __int32 AllocationsBetweenCacheSweeps = 64;

const unsigned CachedPayloadLimitBytes = 512;

unsigned packetIdKey(const IntrusivePtr<PacketBuffer>& msg)
{
	if (!msg)
		return 0;
	return (msg->totalLen - sizeof(DatagramHeader));
}

size_t packetPointerKey(const IntrusivePtr<PacketBuffer>& msg)
{
	return reinterpret_cast<size_t>(msg.GetRef());
}

PacketCache::PacketCache()
{
	recycled.reserve(400);
	used.reserve(12);
	allocationsUntilSweep = AllocationsBetweenCacheSweeps;
}

IntrusivePtr<PacketCache> PacketCache::sharedCacheInstance(new PacketCache);

PacketCache::~PacketCache()
{
	trimCachedPackets();
}

IntrusivePtr<PacketBuffer> PacketCache::acquirePacket(unsigned minLen, ChannelInterface* ch)
{
	stateMutex.lock();
	if (--allocationsUntilSweep <= 0)
	{

		allocationsUntilSweep = AllocationsBetweenCacheSweeps;
		garbageCollectStep();
	}
	IntrusivePtr<PacketBuffer> result;
	unsigned taken = minLen;
	if (minLen < CachedPayloadLimitBytes)
	{
		unsigned CacheSizeSearchSlackBytes = 32;	 
		unsigned CacheSizeSearchFraction32 = 8; 

		__int32 tryOverSize = max(minLen * CacheSizeSearchFraction32 / 32, CacheSizeSearchSlackBytes);
		auto first = recycled.find(taken);
		if (first != recycled.end() && first->second)
			result = first->second;
		while (!result && ++taken < minLen + tryOverSize)
		{
			auto it = recycled.find(taken);
			if (it != recycled.end() && it->second)
			{
				result = it->second;
				break;
			}
		}
	}

	if (!result)
		result = new PacketBuffer(minLen + sizeof(DatagramHeader));
	else
	{
		recycledBytes -= result->totalLen + sizeof(PacketBuffer);
		PacketBuffer* next = result->next.GetRef();
		unsigned key = packetIdKey(result);
		if (!next)
			recycled.erase(key);
		else
			recycled[key] = next;
		result->resetPacket();
	}
	used[packetPointerKey(result)] = result;

	result->id = PacketBuffer::nextId++;
	garbageQueue.emplace_back(packetPointerKey(result), result->id);
	stateMutex.unlock();
	result->assignChannel(ch);
	return result.GetRef();
}

void PacketCache::reclaimPacket(PacketBuffer* msg)
{
	if (!msg)
		return;
	stateMutex.lock();
	if (used.find(packetPointerKey(msg)) != used.end())
	{

		msg->resetPacket();
		const size_t allocation = msg->totalLen + sizeof(PacketBuffer);
		if (msg->totalLen - sizeof(DatagramHeader) < CachedPayloadLimitBytes &&
			recycledBytes + allocation <= 8 * 1024 * 1024)
		{
			recycledBytes += allocation;
			IntrusivePtr<PacketBuffer> old;
			unsigned key = packetIdKey(msg);
			auto it = recycled.find(key);
			if (it != recycled.end())
				old = it->second;
			recycled[key] = msg;
			msg->next = old;
		}
		used.erase(packetPointerKey(msg));
	}
	else
	{

		msg->Release();
	}
	stateMutex.unlock();
}

void PacketCache::garbageCollectStep()
{
	// Full scans on allocation become quadratic under broadcast traffic.
	// Rotate a bounded number of candidates; generation IDs reject stale
	// entries when an explicitly recycled message has already been reused.
	const size_t count = (std::min)(size_t(256), garbageQueue.size());
	for (size_t i = 0; i < count; ++i)
	{
		const auto entry = garbageQueue.front(); garbageQueue.pop_front();
		auto it = used.find(entry.first);
		if (it == used.end() || it->second->id != entry.second) continue;
		if (it->second->referenceCount() == 1)
		{
			IntrusivePtr<PacketBuffer> msg = it->second;
			reclaimPacket(msg.GetRef());
		}
		else garbageQueue.push_back(entry);
	}
}

void PacketCache::reclaimIdlePackets()
{
	std::vector<IntrusivePtr<PacketBuffer> > collect;
	{
		stateMutex.lock();
		for (const auto& it : used)
		{
			IntrusivePtr<PacketBuffer> msg = it.second;
			if (msg && msg->referenceCount() <= 2)
				collect.push_back(msg);
		}
		stateMutex.unlock();
	}
	for (size_t i = 0; i < collect.size(); ++i)
	{
		PacketBuffer* msg = collect[i].GetRef();
		if (msg && msg->referenceCount() <= 2)
			reclaimPacket(msg);
	}
}

unsigned PacketCache::unusedMemory()
{
	unsigned size = 0;
	stateMutex.lock();
	for (const auto& it : recycled)
	{
		IntrusivePtr<PacketBuffer> msg = it.second;
		while (msg)
		{ 
			size += msg->totalLen + sizeof(PacketBuffer);
			msg = msg->next.GetRef();
		}
	}
	stateMutex.unlock();
	return size;
}

unsigned PacketCache::freeOneItem()
{
	unsigned size = 0;
	stateMutex.lock();
	auto oldestIt = recycled.end();
	for (auto it = recycled.begin(); it != recycled.end(); ++it)
	{
		if (!it->second)
			continue;
		if (oldestIt == recycled.end() || it->second->id < oldestIt->second->id)
			oldestIt = it;
	}
	if (oldestIt != recycled.end())
	{
		// Detach each link before releasing it; a large pool must not recurse
		// through Ref destructors and exhaust the stack.
		while (oldestIt->second)
		{
			IntrusivePtr<PacketBuffer> msg = oldestIt->second;
			size += msg->totalLen + sizeof(PacketBuffer);
			recycledBytes -= msg->totalLen + sizeof(PacketBuffer);
			oldestIt->second = msg->next;
			msg->next = NULL;
		}
		recycled.erase(oldestIt);
	}
	stateMutex.unlock();
	return size;
}

unsigned PacketCache::trimCachedPackets()
{
	stateMutex.lock();
	allocationsUntilSweep = AllocationsBetweenCacheSweeps;
	reclaimIdlePackets();
	unsigned size = 0;
	for (auto it = recycled.begin(); it != recycled.end();)
	{
		while (it->second)
		{ 
			IntrusivePtr<PacketBuffer> msg = it->second;
			size += msg->totalLen + sizeof(PacketBuffer);
			it->second = msg->next;
			msg->next = NULL;
		}
		it = recycled.erase(it);
	}
	recycledBytes = 0;
	garbageQueue.clear();
	for (const auto& entry : used)
		garbageQueue.emplace_back(entry.first, entry.second->id);
	stateMutex.unlock();
	return size;
}
