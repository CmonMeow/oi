

#include "netpch.hpp"
#include "netpeer.hpp"
#include "netchannel.hpp"

unsigned __int64 channelKey(const Ref<NetChannel>& ch)
{
	struct sockaddr_in addr;
	ch->getDistantAddress(addr);
	return sockaddrKey(addr);
}

const NetworkParams defaultNetworkParams = {
	400,
	2,		  
	32000,	  
	8192,	  
	2000000,  
	3000,
	3000,
	65536,	  
	400,	  
};
NetworkParams networkParams = defaultNetworkParams;

NetPeer* NetPool::createPeer(unsigned short port)
{
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
	{
		return NULL;
	}

	DWORD enableReporting = FALSE;
	DWORD returned = 0;
	const DWORD SIO_UDP_CONNRESET = 0x9800000C;
	if (WSAIoctl(s, SIO_UDP_CONNRESET, &enableReporting, sizeof(enableReporting), NULL, 0, &returned, NULL, NULL) == SOCKET_ERROR)
	{
		DWORD err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK)
		{
			closesocket(s);
			return NULL;
		}
	}

	__int32 tmp = 1;
	if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&tmp, sizeof(tmp)) == SOCKET_ERROR)
	{
		closesocket(s);
		return NULL;
	}
	tmp = RCVBUFSize;
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&tmp, sizeof(tmp));

	struct sockaddr_in local;
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = bindIPAddress;
	for (; port <= 49151; port++)
	{
		local.sin_port = htons(port);
		if (bind(s, (struct sockaddr*)&local, sizeof(local)) != SOCKET_ERROR)
		{
			NetPeer* newPeer = new NetPeerUDP(s, port, this);
			if (!newPeer)
				return NULL;
			peers[newPeer->getPort()] = newPeer;
			return newPeer;
		}
	}
	closesocket(s);
	return NULL;
}

NetChannel* NetPool::createChannel(bool control = false) { return new NetChannelBasic(control); }
