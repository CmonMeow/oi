
#include "netpch.hpp"

unsigned __int32 NetMessage::nextId = 1;

void NetMessage::recycle()
{
	NetMessagePool::pool()->recycleMessage(this);
}

const __int32 NET_MESSAGE_POOL_GARBAGE = 1000; 

const unsigned MAX_RECYCLED_MESSAGE_SIZE = 512;

unsigned netMessageToUnsigned(const Ref<NetMessage>& msg)
{
	if (!msg)
		return 0;
	return (msg->totalLen - sizeof(MsgHeader));
}

size_t netMessageAddressToUnsigned(const Ref<NetMessage>& msg)
{
	return reinterpret_cast<size_t>(msg.GetRef());
}

NetMessagePool::NetMessagePool()
{
	recycled.reserve(400);
	used.reserve(12);
	garbageCounter = NET_MESSAGE_POOL_GARBAGE;
}

Ref<NetMessagePool> NetMessagePool::msgPool(new NetMessagePool);

NetMessagePool::~NetMessagePool()
{
	freeMemory();
}

Ref<NetMessage> NetMessagePool::newMessage(unsigned minLen, NetChannel* ch)
{
	Critical_Section.lock();
	if (--garbageCounter <= 0)
	{

		garbageCounter = NET_MESSAGE_POOL_GARBAGE;
		garbageCollect();
	}
	Ref<NetMessage> result;
	unsigned taken = minLen;
	if (minLen < MAX_RECYCLED_MESSAGE_SIZE)
	{
		unsigned NET_MESSAGE_POOL_TRY = 32;	 
		unsigned NET_MESSAGE_POOL_TRY_REL = 8; 

		__int32 tryOverSize = max(minLen * NET_MESSAGE_POOL_TRY_REL / 32, NET_MESSAGE_POOL_TRY);
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
		result = new NetMessage(minLen + sizeof(MsgHeader));
	else
	{
		NetMessage* next = result->next.GetRef();
		unsigned key = netMessageToUnsigned(result);
		if (!next)
			recycled.erase(key);
		else
			recycled[key] = next;
		result->init();
	}
	used[netMessageAddressToUnsigned(result)] = result;

	result->id = NetMessage::nextId++;
	Critical_Section.unlock();
	result->setChannel(ch);
	return result.GetRef();
}

void NetMessagePool::recycleMessage(NetMessage* msg)
{
	if (!msg)
		return;
	Critical_Section.lock();
	if (used.find(netMessageAddressToUnsigned(msg)) != used.end())
	{

		msg->init();
		if (msg->totalLen - sizeof(MsgHeader) < MAX_RECYCLED_MESSAGE_SIZE)
		{
			Ref<NetMessage> old;
			unsigned key = netMessageToUnsigned(msg);
			auto it = recycled.find(key);
			if (it != recycled.end())
				old = it->second;
			recycled[key] = msg;
			msg->next = old;
		}
		used.erase(netMessageAddressToUnsigned(msg));
	}
	else
	{

		msg->Release();
	}
	Critical_Section.unlock();
}

void NetMessagePool::garbageCollect()
{
	std::vector<Ref<NetMessage> > collect;
	{
		Critical_Section.lock();
		for (const auto& it : used)
		{
			Ref<NetMessage> msg = it.second;
			if (msg && msg->RefCounter() <= 2)
				collect.push_back(msg);
		}
		Critical_Section.unlock();
	}
	for (size_t i = 0; i < collect.size(); ++i)
	{
		NetMessage* msg = collect[i].GetRef();
		if (msg && msg->RefCounter() <= 2)
			recycleMessage(msg);
	}
}

unsigned NetMessagePool::unusedMemory()
{
	unsigned size = 0;
	Critical_Section.lock();
	for (const auto& it : recycled)
	{
		Ref<NetMessage> msg = it.second;
		while (msg)
		{ 
			size += msg->totalLen + sizeof(NetMessage);
			msg = msg->next.GetRef();
		}
	}
	Critical_Section.unlock();
	return size;
}

unsigned NetMessagePool::freeOneItem()
{
	unsigned size = 0;
	Critical_Section.lock();
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
			Ref<NetMessage> msg = oldestIt->second;
			size += msg->totalLen + sizeof(NetMessage);
			oldestIt->second = msg->next;
			msg->next = NULL;
		}
		recycled.erase(oldestIt);
	}
	Critical_Section.unlock();
	return size;
}

unsigned NetMessagePool::freeMemory()
{
	garbageCounter = NET_MESSAGE_POOL_GARBAGE;
	garbageCollect();
	unsigned size = 0;
	Critical_Section.lock();
	for (auto it = recycled.begin(); it != recycled.end();)
	{
		while (it->second)
		{ 
			Ref<NetMessage> msg = it->second;
			size += msg->totalLen + sizeof(NetMessage);
			it->second = msg->next;
			msg->next = NULL;
		}
		it = recycled.erase(it);
	}
	Critical_Section.unlock();
	return size;
}
