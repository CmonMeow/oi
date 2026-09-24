#pragma once
#include <deque>
#include <unordered_map>
#include <vector>

// Called under the transport's receive lock. Ordered messages have already
// been sequenced by the channel; serial numbers from different peers cannot
// be compared. Keep arrival order and bound storage before the UI consumes it.
class IncomingPacketQueue
{
	struct Usage { unsigned messages = 0; size_t bytes = 0; };
	std::deque<IntrusivePtr<PacketBuffer>> messages;
	std::unordered_map<ChannelInterface*, Usage> usage;
	std::unordered_map<ChannelInterface*, IntrusivePtr<ChannelInterface>> overloaded;
	size_t bytes = 0;
public:
	bool empty() const { return messages.empty(); }
	void push(PacketBuffer* msg, bool server)
	{
		if (!msg || overloaded.count(msg->ownerChannel())) return;
		const auto peer = usage.find(msg->ownerChannel());
		const size_t size = msg->payloadLength();
		if (messages.size() >= 16384 || bytes + size > 32 * 1024 * 1024 ||
			(server && peer != usage.end() &&
			 (peer->second.messages >= 512 || peer->second.bytes + size > 2 * 1024 * 1024)))
		{
			overloaded.emplace(msg->ownerChannel(), msg->ownerChannel());
			return;
		}
		messages.emplace_back(msg);
		auto& admitted = usage[msg->ownerChannel()];
		++admitted.messages; admitted.bytes += size; bytes += size;
	}
	IntrusivePtr<PacketBuffer> pop()
	{
		IntrusivePtr<PacketBuffer> msg = messages.front(); messages.pop_front();
		auto it = usage.find(msg->ownerChannel());
		bytes -= msg->payloadLength();
		it->second.bytes -= msg->payloadLength();
		if (--it->second.messages == 0) usage.erase(it);
		return msg;
	}
	std::vector<IntrusivePtr<ChannelInterface>> takeOverloaded()
	{
		std::vector<IntrusivePtr<ChannelInterface>> result;
		for (const auto& entry : overloaded) result.push_back(entry.second);
		overloaded.clear();
		return result;
	}
	void clear() { messages.clear(); usage.clear(); overloaded.clear(); bytes = 0; }
};
