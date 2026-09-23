#pragma once
#include <deque>
#include <unordered_map>
#include <vector>

// Called under the transport's receive lock. Ordered messages have already
// been sequenced by the channel; serial numbers from different peers cannot
// be compared. Keep arrival order and bound storage before the UI consumes it.
class NetReceiveQueue
{
	struct Usage { unsigned messages = 0; size_t bytes = 0; };
	std::deque<Ref<NetMessage>> messages;
	std::unordered_map<NetChannel*, Usage> usage;
	std::unordered_map<NetChannel*, Ref<NetChannel>> overloaded;
	size_t bytes = 0;
public:
	bool empty() const { return messages.empty(); }
	void push(NetMessage* msg, bool server)
	{
		if (!msg || overloaded.count(msg->getChannel())) return;
		const auto peer = usage.find(msg->getChannel());
		const size_t size = msg->getLength();
		if (messages.size() >= 16384 || bytes + size > 32 * 1024 * 1024 ||
			(server && peer != usage.end() &&
			 (peer->second.messages >= 512 || peer->second.bytes + size > 2 * 1024 * 1024)))
		{
			overloaded.emplace(msg->getChannel(), msg->getChannel());
			return;
		}
		messages.emplace_back(msg);
		auto& admitted = usage[msg->getChannel()];
		++admitted.messages; admitted.bytes += size; bytes += size;
	}
	Ref<NetMessage> pop()
	{
		Ref<NetMessage> msg = messages.front(); messages.pop_front();
		auto it = usage.find(msg->getChannel());
		bytes -= msg->getLength();
		it->second.bytes -= msg->getLength();
		if (--it->second.messages == 0) usage.erase(it);
		return msg;
	}
	std::vector<Ref<NetChannel>> takeOverloaded()
	{
		std::vector<Ref<NetChannel>> result;
		for (const auto& entry : overloaded) result.push_back(entry.second);
		overloaded.clear();
		return result;
	}
	void clear() { messages.clear(); usage.clear(); overloaded.clear(); bytes = 0; }
};
