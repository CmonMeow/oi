

void decodeURLAddress(std::string address, std::string& ip, unsigned short& port)
{
	const char* ptr = strrchr(address.c_str(), ':');
	if (!ptr)
	{
		ip = address;
		return;
	}
	ip = address.substr(0, ptr - address.c_str());
	port = atoi(ptr + 1);
}

NetPool* getPool()

{
	createPool();
	return pool.GetRef();
}

void setClient(NetClient* _cl)
{
	poolCriticalSection().lock();
	_client = _cl;
	if (_cl)
	{
		getPool();
		getClientPeer();
	}
	poolCriticalSection().unlock();
}

void setServer(NetServer* _srv)
{
	poolCriticalSection().lock();
	_server = _srv;
	if (_srv)
	{
		getPool();
		getClientPeer();
		getServerPeer();
	}
	poolCriticalSection().unlock();
}

SOCKET GetServerSocket()
{
	NetPeer* peer = getServerPeer();
	return peer ? peer->GetSocket() : INVALID_SOCKET;
}

NetTranspClient* CreateNetClient()
{
	return new NetClient;
}

NetTranspServer* CreateNetServer()
{
	return new NetServer;
}

Ref<NetMessage> mergeMessageList(NetMessage* msg)
{
	if (!msg)
		return NULL;
	unsigned size = 0;
	NetMessage* ptr = msg;
	NetMessage* last;
	do
	{
		unsigned partLength = ptr->getLength();
		if (partLength > MAX_REASSEMBLED_USER_MESSAGE ||
			size > MAX_REASSEMBLED_USER_MESSAGE - partLength)
		{
			Error("mergeMessageList: dropping oversized user message");
			return NULL;
		}
		size += partLength;
		last = ptr;
	} while ((ptr = ptr->next.GetRef()));
	Ref<NetMessage> composite = NetMessagePool::pool()->newMessage(size, msg->getChannel());
	if (!composite)
		return NULL;
	composite->setFrom(last);
	unsigned char* data = (unsigned char*)composite->getData();
	for (ptr = msg; ptr; ptr = ptr->next.GetRef())
	{
		memcpy(data, ptr->getData(), ptr->getLength());
		data += ptr->getLength();
	}
	composite->setLength(size);
	return composite;
}

bool messageListWouldExceedLimit(NetMessage* msg, unsigned additionalLength)
{
	if (additionalLength > MAX_REASSEMBLED_USER_MESSAGE)
		return true;
	unsigned size = additionalLength;
	for (NetMessage* ptr = msg; ptr; ptr = ptr->next.GetRef())
	{
		unsigned partLength = ptr->getLength();
		if (partLength > MAX_REASSEMBLED_USER_MESSAGE ||
			size > MAX_REASSEMBLED_USER_MESSAGE - partLength)
		{
			return true;
		}
		size += partLength;
	}
	return false;
}

NetStatus serverReceive(NetMessage* msgPtr, NetStatus event, void* data)
{
	Ref<NetMessage> msg = msgPtr;
	
	if (!_server || !msg)
		return nsNoMoreCallbacks; 
	unsigned len = msg->getLength();
	if (!len)
		return nsNoMoreCallbacks; 
	struct sockaddr_in dist;
	msg->getDistant(dist);
	unsigned flags = msg->getFlags();

    // Control packets on established server channels cannot remove a player.
    if (flags & MSG_MAGIC_FLAG) return nsNoMoreCallbacks;

	_server->Receive_Critical_Section.lock();
	
	if (flags & MSG_PART_FLAG)
	{
		unsigned __int64 addrKey = sockaddrKey(dist); 
		std::unordered_map<unsigned __int64, ChannelSupport>::iterator supportIt = _server->m_support.find(addrKey);
		ChannelSupport* sup = supportIt != _server->m_support.end() ? &supportIt->second : NULL;
		if (!sup)
		{
			_server->Receive_Critical_Section.unlock();
			return nsNoMoreCallbacks;
		}
		bool closing = (flags & MSG_CLOSING_FLAG) > 0;
		if (flags & MSG_URGENT_FLAG)
			if (closing) 
			{
				if (!sup->m_splitUrgent || !sup->m_lastSplitUrgent)
				{
					_server->Receive_Critical_Section.unlock();
					return nsNoMoreCallbacks;
				}
				sup->m_lastSplitUrgent->next = msg;
				msg->next = NULL;
				msg = mergeMessageList(sup->m_splitUrgent.GetRef());
				sup->m_splitUrgent = NULL;
			}
			else 
			{
				if (messageListWouldExceedLimit(sup->m_splitUrgent.GetRef(), msg->getLength()))
				{
					sup->m_splitUrgent = NULL;
					sup->m_lastSplitUrgent = NULL;
					_server->Receive_Critical_Section.unlock();
					return nsNoMoreCallbacks;
				}
				if (!sup->m_splitUrgent)
					sup->m_splitUrgent = msg;
				else
					sup->m_lastSplitUrgent->next = msg;
				(sup->m_lastSplitUrgent = msg.GetRef())->next = NULL;
				_server->Receive_Critical_Section.unlock();
				return nsNoMoreCallbacks;
			}
		else if (closing) 
		{
			if (!sup->m_split || !sup->m_lastSplit)
			{
				_server->Receive_Critical_Section.unlock();
				return nsNoMoreCallbacks;
			}
			sup->m_lastSplit->next = msg;
			msg->next = NULL;
			msg = mergeMessageList(sup->m_split.GetRef());
			sup->m_split = NULL;
		}
		else 
		{
			if (messageListWouldExceedLimit(sup->m_split.GetRef(), msg->getLength()))
			{
				sup->m_split = NULL;
				sup->m_lastSplit = NULL;
				_server->Receive_Critical_Section.unlock();
				return nsNoMoreCallbacks;
			}
			if (!sup->m_split)
				sup->m_split = msg;
			else
				sup->m_lastSplit->next = msg;
			(sup->m_lastSplit = msg.GetRef())->next = NULL;
			_server->Receive_Critical_Section.unlock();
			return nsNoMoreCallbacks;
		}
	}

	_server->insertReceived(msg.GetRef());

	_server->Receive_Critical_Section.unlock();

	return nsNoMoreCallbacks;
}

NetClient::NetClient()
	: Send_Critical_Section(),
	  Receive_Critical_Section()
{
	
	lastMsgReported = GetTickCount64();
	
	channel = NULL;
	
	received.clear();
	split = lastSplit = NULL;
	splitUrgent = lastSplitUrgent = NULL;
	sent = NULL;
	sessionTerminated = false;
	whySessionTerminated = NTROther;
	playerNo = -1; 
	setClient(this);
}

NetClient::~NetClient()
{
	Send_Critical_Section.lock();
	sessionTerminated = true;
    whySessionTerminated = NTRDisconnected;

	setClient(NULL);
	
	RemoveUserMessages();
	RemoveSendComplete();

	if (channel)
	{
		Ref<NetChannel> old = channel;
		channel = NULL;
		Send_Critical_Section.unlock();
		getPool()->deleteChannel(old.GetRef());
	}
	else
		Send_Critical_Section.unlock();
	
	FreeMemory();
}

NetStatus clientReceive(NetMessage* msgPtr, NetStatus event, void* data)
{
	Ref<NetMessage> msg = msgPtr;
	
	if (!_client || !msg)
		return nsNoMoreCallbacks; 
	_client->lastMsgReported = msg->getTime();
	unsigned len = msg->getLength();
	if (!len)
		return nsNoMoreCallbacks; 

	unsigned flags = msg->getFlags();

	bool isMagic = (flags & MSG_MAGIC_FLAG) != 0;
	if (isMagic) 
	{
		if (len < 4)
			return nsNoMoreCallbacks; 
		unsigned __int32 magic = *(unsigned __int32*)msg->getData();

		switch (magic)
		{
		case MAGIC_ACK_PLAYER: 
			if (len == sizeof(AckPlayerPacket) && _client)
			{
				AckPlayerPacket* app = (AckPlayerPacket*)msg->getData();
				_client->Send_Critical_Section.lock();
				if (_client->ackPlayer == CRNone) 
				{
					_client->playerNo = app->playerNo;
					_client->ackPlayer = app->result;
				}
				_client->Send_Critical_Section.unlock();
			}
			break; 


		}

		return nsNoMoreCallbacks;
	}

	_client->Receive_Critical_Section.lock();
	
	if (flags & MSG_PART_FLAG)
	{
		bool closing = (flags & MSG_CLOSING_FLAG) > 0;
		if (flags & MSG_URGENT_FLAG)
			if (closing) 
			{
				if (!_client->splitUrgent || !_client->lastSplitUrgent)
				{
					_client->Receive_Critical_Section.unlock();
					return nsNoMoreCallbacks;
				}
				_client->lastSplitUrgent->next = msg;
				msg->next = NULL;
				msg = mergeMessageList(_client->splitUrgent.GetRef());
				_client->splitUrgent = NULL;
			}
			else 
			{
				if (messageListWouldExceedLimit(_client->splitUrgent.GetRef(), msg->getLength()))
				{
					_client->splitUrgent = NULL;
					_client->lastSplitUrgent = NULL;
					_client->Receive_Critical_Section.unlock();
					return nsNoMoreCallbacks;
				}
				if (!_client->splitUrgent)
					_client->splitUrgent = msg;
				else
					_client->lastSplitUrgent->next = msg;
				(_client->lastSplitUrgent = msg.GetRef())->next = NULL;
				_client->Receive_Critical_Section.unlock();
				return nsNoMoreCallbacks;
			}
		else if (closing) 
		{
			if (!_client->split || !_client->lastSplit)
			{
				_client->Receive_Critical_Section.unlock();
				return nsNoMoreCallbacks;
			}
			_client->lastSplit->next = msg;
			msg->next = NULL;
			msg = mergeMessageList(_client->split.GetRef());
			_client->split = NULL;
		}
		else 
		{
			if (messageListWouldExceedLimit(_client->split.GetRef(), msg->getLength()))
			{
				_client->split = NULL;
				_client->lastSplit = NULL;
				_client->Receive_Critical_Section.unlock();
				return nsNoMoreCallbacks;
			}
			if (!_client->split)
				_client->split = msg;
			else
				_client->lastSplit->next = msg;
			(_client->lastSplit = msg.GetRef())->next = NULL;
			_client->Receive_Critical_Section.unlock();
			return nsNoMoreCallbacks;
		}
	}

	if (msg)
		_client->insertReceived(msg.GetRef());

	_client->Receive_Critical_Section.unlock();

	return nsNoMoreCallbacks;
}

NetStatus clientSendComplete(NetMessage* msg, NetStatus event, void* data)
{
	NetClient* client = (NetClient*)data;
	if (!client || !msg || client->sessionTerminated)
		return nsNoMoreCallbacks; 

	client->Send_Critical_Section.lock();
	msg->next = client->sent;
	client->sent = msg;
	client->Send_Critical_Section.unlock();
	return nsNoMoreCallbacks;
}

Ref<NetMessage> NetClient::SendMsg(BYTE* buffer, __int32 bufferSize, DWORD& msgID, NetMsgFlags flags, const Ref<NetMessage>& dependOn)
{
	bool vim = (flags & NMFGuaranteed) > 0;
	bool urgent = (flags & NMFHighPriority) > 0;
	Send_Critical_Section.lock();
	if (!channel || !buffer || bufferSize <= 0)
	{
		Send_Critical_Section.unlock();
		return NULL;
	}
	__int32 maxMessage = MAX_PACKET_SIZE;
	if (!vim && bufferSize > maxMessage)
	{
		Send_Critical_Section.unlock();

		Error("NetClient: trying to send too large non-guaranteed message (%d bytes long)", bufferSize);
		return NULL;
	}
	Ref<NetMessage> msg;
	if (vim)
	{ 
		unsigned fl = MSG_VIM_FLAG | (urgent ? MSG_URGENT_FLAG : 0);
		if (bufferSize > maxMessage)
		{ 

			__int32 toSent = bufferSize;
			__int32 packet;
			fl |= MSG_PART_FLAG;
			do
			{
				packet = (toSent > maxMessage) ? maxMessage : toSent;
				toSent -= packet;
				msg = NetMessagePool::pool()->newMessage(packet, channel.GetRef());
				if (!msg)
				{
					Send_Critical_Section.unlock();
					return NULL;
				}
				msg->setFlags(MSG_ALL_FLAGS, fl | (toSent ? 0 : MSG_CLOSING_FLAG));
				msg->setOrderedPrevious();
				msg->setData((unsigned char*)buffer, packet);
				buffer += packet;
				msg->send(urgent);
			} while (toSent);
		}
		else
		{ 
			msg = NetMessagePool::pool()->newMessage(bufferSize, channel.GetRef());
			msg->setFlags(MSG_ALL_FLAGS, fl);
			msg->setOrderedPrevious();
			msg->setData((unsigned char*)buffer, bufferSize);
			msg->send(urgent);
		}
	}
	else
	{ 
		msg = NetMessagePool::pool()->newMessage(bufferSize, channel.GetRef());
		if (!msg)
		{
			Send_Critical_Section.unlock();
			return NULL;
		}
		if (dependOn)
			msg->setOrdered(dependOn.GetRef());
		if (flags & NMFSetCallback)
			msg->setCallback(clientSendComplete, nsOutputSent, this);
		msg->setSendTimeout(SEND_TIMEOUT);
		msg->setData((unsigned char*)buffer, bufferSize);
		msg->send();
	}
	msgID = (DWORD)msg->id;

	Send_Critical_Section.unlock();
	return msg;
}

void NetClient::GetSendQueueInfo(__int32& nMsg, __int32& nBytes, __int32& nMsgG, __int32& nBytesG)
{
	Send_Critical_Section.lock();
	if (channel)
		channel->getOutputQueueStatistics(nMsg, nBytes, nMsgG, nBytesG);
	else
	{
		nMsg = nBytes = nMsgG = nBytesG = 0;
	}
	Send_Critical_Section.unlock();
}

void NetClient::GetConnectionLimits(__int32& maxBandwidthPerClient)
{
	maxBandwidthPerClient = INT_MAX;
}

bool NetClient::GetConnectionInfo(__int32& latencyMS, __int32& throughputBPS)
{
	Send_Critical_Section.lock();
	if (!channel)
	{
		Send_Critical_Section.unlock();
		return false;
	}
	latencyMS = (__int32)channel->getLatency();
	throughputBPS = (__int32)channel->getOutputBandWidth();

	Send_Critical_Section.unlock();
	return true;
}

bool NetClient::GetLocalAddress(in_addr& addr) const
{
	struct sockaddr_in local;
	if (!getLocalAddress(local, 0))
		return false;
	addr = local.sin_addr;
	return true;
}

bool NetClient::GetLocalAddress(in_addr& addr, __int32& port) const
{
	Send_Critical_Section.lock();
	if (!channel)
	{
		Send_Critical_Section.unlock();
		return false;
	}
	struct sockaddr_in saddr;
	channel->getLocalAddress(saddr);
	addr = saddr.sin_addr;
	port = ntohs(saddr.sin_port);
	Send_Critical_Section.unlock();
	return true;
}

bool NetClient::GetDistantAddress(in_addr& addr, __int32& port) const
{
	Send_Critical_Section.lock();
	if (!channel)
	{
		Send_Critical_Section.unlock();
		return false;
	}
	struct sockaddr_in saddr;
	channel->getDistantAddress(saddr);
	addr = saddr.sin_addr;
	port = saddr.sin_port;
	Send_Critical_Section.unlock();
	return true;
}

bool NetClient::GetServerAddress(sockaddr_in& address) const
{
	Send_Critical_Section.lock();
	if (!channel)
	{
		Send_Critical_Section.unlock();
		return false;
	}
	channel->getDistantAddress(address);
	Send_Critical_Section.unlock();
	return true;
}

bool NetClient::IsSessionTerminated()
{
	Send_Critical_Section.lock();
	if (!channel)
	{
		Send_Critical_Section.unlock();
		return true;
	}
	
	if (!amIBot && channel->dropped())
	{
		sessionTerminated = true;
		whySessionTerminated = NTRTimeout;
	}
    const bool terminated = sessionTerminated;
    Send_Critical_Section.unlock();
    return terminated;
}

NetTerminationReason NetClient::GetWhySessionTerminated()
{
	NetTerminationReason reason;
	Send_Critical_Section.lock();
	reason = whySessionTerminated;

	Send_Critical_Section.unlock();
	return reason;
}

std::string NetClient::GetWhySessionTerminatedStr()
{
	std::string reason;
	Send_Critical_Section.lock();
	reason = whySessionTerminatedStr;

	Send_Critical_Section.unlock();
	return reason;
}

void NetClient::ProcessUserMessages(UserMessageClientCallback* callback, void* context)
{
	if (!callback)
		return;
	LARGE_INTEGER started, frequency;
	QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&started);
	Ref<NetMessage> msg;
	Receive_Critical_Section.lock();
	auto overloaded = received.takeOverloaded();
	Receive_Critical_Section.unlock();
	if (!overloaded.empty())
	{
		std::lock_guard<std::recursive_mutex> lock(Send_Critical_Section);
		sessionTerminated = true;
		whySessionTerminated = NTROther;
		strcpy_s(whySessionTerminatedStr, "receive queue exceeded");
		RemoveUserMessages();
		return;
	}
	Receive_Critical_Section.lock();

	for (unsigned count = 0; count < 256 && !received.empty(); ++count)
	{
		msg = received.pop();
		if (!(msg->getFlags() & MSG_VIM_FLAG) && GetTickCount64() - msg->getTime() > SEND_TIMEOUT) continue;
		Receive_Critical_Section.unlock();

		(*callback)((char*)msg->getData(), msg->getLength(), context);
		Receive_Critical_Section.lock();
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		if ((now.QuadPart - started.QuadPart) * 1000 >= frequency.QuadPart * 8) break;
	}
	Receive_Critical_Section.unlock();
}

void NetClient::insertReceived(NetMessage* msg)
{
	received.push(msg, false);
}

void NetClient::RemoveUserMessages()
{
	std::lock_guard<std::recursive_mutex> lock(Receive_Critical_Section);
	received.clear();
}

void NetClient::RemoveSendComplete()
{
	Send_Critical_Section.lock();
	Ref<NetMessage> tmp;
	while (sent)
	{
		tmp = sent->next;
		sent->next = NULL; 
		sent = tmp;
	}
	Send_Critical_Section.unlock();
}

unsigned NetClient::FreeMemory()
{
	if (!NetMessagePool::pool())
		return 0;
	unsigned ret = NetMessagePool::pool()->freeMemory();
	return ret;
}

NetServer::NetServer()
	: User_Critical_Section(),
	  Receive_Critical_Section(),
	  Send_Critical_Section()
{
	
	User_Critical_Section.lock();
	received.clear();
	sent = NULL;
	acceptConnections = true;
	session.serverState = 0;
	session.maxPlayers = 0xff;
	session.playerCount = 0;
	session.password = false;
	session.port = 0;
	session.name[0] = (char)0;

	setServer(this);
	poolCriticalSection().lock();
	sessionPort = getServerPeer() ? getServerPeer()->getPort() : 0;
	poolCriticalSection().unlock();
	User_Critical_Section.unlock();
}

NetServer::~NetServer()
{
	CancelAllMessages();
	setServer(NULL);
    // Release registered channels as well as the user map. Otherwise a later
    // host in this process finds orphaned endpoints and rejects reconnects.
    {
        std::lock_guard<std::recursive_mutex> poolGuard(poolCriticalSection());
        std::lock_guard<std::recursive_mutex> userGuard(User_Critical_Section);
        unsigned iterator;
        __int32 player;
        Ref<NetChannel> old;
        while (users.getFirst(iterator, old, &player))
        {
            users.removeKey(player);
            getPool()->deleteChannel(old.GetRef());
        }
    }
	
	RemoveUserMessages();
	RemoveSendComplete();
	
	RemovePlayers();
	
	FreeMemory();
}

void NetServer::GetTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const
{
    std::lock_guard<std::recursive_mutex> guard(poolCriticalSection());
    NetPeer* peer = getServerPeer(false);
    incoming = outgoing = 0;
    if (peer) peer->getTrafficTotals(incoming, outgoing);
}

void NetServer::GetSendQueueInfo(__int32 to, __int32& nMsg, __int32& nBytes, __int32& nMsgG, __int32& nBytesG)
{
	User_Critical_Section.lock();
	Ref<NetChannel> channel;
	if (users.get(to, channel))
		channel->getOutputQueueStatistics(nMsg, nBytes, nMsgG, nBytesG);
	else
	{
		nMsg = nBytes = nMsgG = nBytesG = 0;
	}
	User_Critical_Section.unlock();
}

bool NetServer::GetConnectionInfo(__int32 to, __int32& latencyMS, __int32& throughputBPS)
{
	User_Critical_Section.lock();
	Ref<NetChannel> channel;
	if (!users.get(to, channel))
	{
		User_Critical_Section.unlock();
		return false;
	}
	latencyMS = (__int32)channel->getLatency();
	throughputBPS = (__int32)channel->getOutputBandWidth();

	bool dropped = channel->dropped();
	if (dropped)
		finishDestroyPlayer(to);
	User_Critical_Section.unlock();
	return true;
}

void NetServer::GetConnectionLimits(__int32& maxBandwidthPerClient)
{
	maxBandwidthPerClient = networkParams.maxBandwidth;
}

void NetServer::destroyPlayer(NetChannel* ch, NetTerminationReason, const char*)
{
    // Application code sends the encrypted notice before requesting local cleanup.
    if (ch) finishDestroyPlayer(channelToPlayer(ch));
}

void NetServer::finishDestroyPlayer(__int32 player)
{
	if (player == -1)
	{
		Error("NetServer::finishDestroyPlayer(%d): invalid player", player);
		return;
	}

	User_Critical_Section.lock();
	Ref<NetChannel> ch;
	if (!users.get(player, ch))
	{
		Error("NetServer::finishDestroyPlayer(%d): users.get failed", player);
		User_Critical_Section.unlock();
		return;
	}
	
	users.removeKey(player);
	struct sockaddr_in daddr;
	ch->getDistantAddress(daddr);
	{ std::lock_guard<std::recursive_mutex> lock(Receive_Critical_Section); m_support.erase(sockaddrKey(daddr)); }
	getPool()->deleteChannel(ch.GetRef());
	for (std::vector<CreatePlayerInfo>::iterator it = _createPlayers.begin(); it != _createPlayers.end(); ++it)
		if (it->player == player)
		{
			_createPlayers.erase(it);
			User_Critical_Section.unlock();
			return;
		}
	_deletePlayers.push_back(DeletePlayerInfo());
	DeletePlayerInfo& info = _deletePlayers.back();
	info.player = player;
	User_Critical_Section.unlock();
}

void NetServer::KickOff(__int32 player, NetTerminationReason reason, const char* reasonStr)
{
	User_Critical_Section.lock();
	Ref<NetChannel> channel;
	if (!users.get(player, channel))
	{
		Error("NetServer::KickOff: player=%d - !users.get", player);
		User_Critical_Section.unlock();
		return;
	}

	destroyPlayer(channel.GetRef(), reason, reasonStr);
	User_Critical_Section.unlock();
}

bool NetServer::Init(std::string name, std::string password, __int32 port)
{
	if (!sessionPort)
	{
		Error("Cannot start host on port %d.", port);
		return false;
	}
	
	User_Critical_Section.lock();
	_password = password;
	session.maxPlayers = 0xff;
	session.playerCount = 0;
	session.password = (password.length() > 0) * 2;
	session.port = sessionPort;
	strncpy(session.name, name.c_str(), sizeof(session.name));
	session.name[sizeof(session.name) - 1] = (char)0;
	sessionName = name;
	session.serverState = 0;

	User_Critical_Section.unlock();
	return true;
}

void NetServer::UpdateLockedOrPassworded(bool lock, bool passworded)
{
	session.password = (lock ? 1 : 0) + (passworded ? 2 : 0);
}

NetStatus serverSendComplete(NetMessage* msg, NetStatus event, void* data)

{
	NetServer* server = (NetServer*)data;
	if (!server || !msg)
		return nsNoMoreCallbacks; 

	server->Send_Critical_Section.lock();
	msg->next = server->sent;
	server->sent = msg;
	server->Send_Critical_Section.unlock();
	return nsNoMoreCallbacks;
}

Ref<NetMessage> NetServer::SendMsg(__int32 to, BYTE* buffer, __int32 bufferSize, DWORD& msgID, NetMsgFlags flags, const Ref<NetMessage>& dependOn)
{
	if (!buffer || bufferSize <= 0)
	{
		Error("NetServer: trying to send empty message to %d", to);
		return NULL;
	}
	Ref<NetChannel> channel;
	Ref<NetMessage> msg;
	bool vim = (flags & NMFGuaranteed) > 0;
	bool urgent = (flags & NMFHighPriority) > 0;
	bool setCallback = (flags & NMFSetCallback) > 0;
	__int32 maxMessage = MAX_PACKET_SIZE;
	if (!getServerPeer())
	{
		Error("NetServer: unable to get peer.");
		return NULL;
	}
	if ((!vim || to == DPNID_ALL_PLAYERS_GROUP) && bufferSize > maxMessage)
	{

		Error("NetServer: trying to send too large non-guaranteed message (%d bytes long, max %d allowed)", bufferSize, maxMessage);
		return NULL;
	}
	unsigned short fl = 0;
	if (vim)
		fl |= MSG_VIM_FLAG;
	if (urgent)
		fl |= MSG_URGENT_FLAG;

	User_Critical_Section.lock();
	if (to == DPNID_ALL_PLAYERS_GROUP)
	{ 

		unsigned it;
		if (users.getFirst(it, channel))
			do
			{
				msg = NetMessagePool::pool()->newMessage(bufferSize, channel.GetRef());
				if (!msg)
				{
					User_Critical_Section.unlock();
					Error("NetServer: pool()->newMessage failed when sending to %d", to);
					return NULL;
				}
				msg->setFlags(MSG_ALL_FLAGS, fl);
				if (fl)
				{
					if (setCallback)
						msg->setCallback(serverSendComplete, nsOutputSent, this);
					msg->setOrderedPrevious(); 
				}
				else 
				{
					if (setCallback)
						msg->setCallback(serverSendComplete, nsOutputSent, this);
					if (dependOn)
						msg->setOrdered(dependOn.GetRef());
					msg->setSendTimeout(SEND_TIMEOUT);
				}
				msg->setData((unsigned char*)buffer, bufferSize);
				msg->send(urgent);

			} while (users.getNext(it, channel));
	}

	else 
	{
		if (!users.get(to, channel))
		{

			Error("NetServer::SendMsg: cannot find channel #%d, users.card=%u", to, users.card());
			Error("NetServer: users.get failed when sending to %d", to);
			User_Critical_Section.unlock();
			return NULL;
		}
		if (bufferSize > maxMessage) 
		{

			__int32 toSent = bufferSize;
			__int32 packet;
			fl |= MSG_PART_FLAG;
			do
			{
				packet = (toSent > maxMessage) ? maxMessage : toSent;
				toSent -= packet;
				msg = NetMessagePool::pool()->newMessage(packet, channel.GetRef());
				if (!msg)
				{
					User_Critical_Section.unlock();
					Error("NetServer: pool()->newMessage failed when sending to %d", to);
					return NULL;
				}
				msg->setFlags(MSG_ALL_FLAGS, fl | (toSent ? 0 : MSG_CLOSING_FLAG));
				if (setCallback && !toSent) 
					msg->setCallback(serverSendComplete, nsOutputSent, this);
				msg->setOrderedPrevious();
				msg->setData((unsigned char*)buffer, packet);
				buffer += packet;
				msg->send(urgent);
			} while (toSent);
		}
		else 
		{
			msg = NetMessagePool::pool()->newMessage(bufferSize, channel.GetRef());
			if (!msg)
			{
				User_Critical_Section.unlock();
				Error("NetServer: pool()->newMessage failed when sending to %d", to);
				return NULL;
			}
			msg->setFlags(MSG_ALL_FLAGS, fl);
			if (fl)
			{
				if (setCallback)
					msg->setCallback(serverSendComplete, nsOutputSent, this);
				msg->setOrderedPrevious(); 
			}
			else 
			{
				if (setCallback)
					msg->setCallback(serverSendComplete, nsOutputSent, this);
				if (dependOn)
					msg->setOrdered(dependOn.GetRef());
				msg->setSendTimeout(SEND_TIMEOUT);
			}
			msg->setData((unsigned char*)buffer, bufferSize);
			msg->send(urgent);
		}
	}

	msgID = (DWORD)msg->id;
	User_Critical_Section.unlock();
	return msg;
}

void NetServer::CancelAllMessages()
{
	poolCriticalSection().lock();
	if (getServerPeer(false))
		getServerPeer(false)->cancelAllMessages();
	poolCriticalSection().unlock();
}

void NetServer::UpdateSessionDescription(__int32 state)
{
	User_Critical_Section.lock();
	session.serverState = state;
	User_Critical_Section.unlock();
}

bool NetServer::GetURL(char* address, DWORD addressLen)
{
	poolCriticalSection().lock();
	bool result = (getServerPeer() != NULL);
	if (result)
	{
		struct sockaddr_in local;
		getLocalAddress(local, sessionPort);
		sprintf(address, "%u.%u.%u.%u:%u",
			(unsigned)IP4(local), (unsigned)IP3(local), (unsigned)IP2(local), (unsigned)IP1(local), (unsigned)sessionPort);
	}
	poolCriticalSection().unlock();
	return result;
}

bool NetServer::GetServerAddress(sockaddr_in& address)
{
	getLocalAddress(address, sessionPort);
	return true;
}

bool NetServer::GetClientAddress(__int32 client, sockaddr_in& address)
{
	Ref<NetChannel> channel;
	if (!users.get(client, channel))
		return false;
	channel->getDistantAddress(address);
	return true;
}

__int32 NetServer::channelToPlayer(NetChannel* ch)
{
	if (!ch)
		return -1;
	unsigned it;
	__int32 i;
	User_Critical_Section.lock();
	Ref<NetChannel> itch;
	if (users.getFirst(it, itch, &i))
		do
			if (itch.GetRef() == ch)
				break;
		while (users.getNext(it, itch, &i));
	if (!itch)
		i = -1;
	User_Critical_Section.unlock();
	return i;
}

void NetServer::ProcessUserMessages(UserMessageServerCallback* callback, void* context)
{
	if (!callback)
		return;
	LARGE_INTEGER started, frequency;
	QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&started);
	Ref<NetMessage> msg;
	Receive_Critical_Section.lock();
	auto overloaded = received.takeOverloaded();
	Receive_Critical_Section.unlock();
	for (const auto& channel : overloaded)
	{
		const int player = channelToPlayer(channel.GetRef());
		if (player >= RESERVED_IDS)
		{
			Error("Player %d exceeded the receive queue limit", player);
			finishDestroyPlayer(player);
		}
	}
	Receive_Critical_Section.lock();

	for (unsigned count = 0; count < 256 && !received.empty(); ++count)
	{
		msg = received.pop();
		if (!(msg->getFlags() & MSG_VIM_FLAG) && GetTickCount64() - msg->getTime() > SEND_TIMEOUT) continue;
		
		Receive_Critical_Section.unlock();
		__int32 player = channelToPlayer(msg->getChannel());
		if (player >= RESERVED_IDS)
			(*callback)(player, (char*)msg->getData(), msg->getLength(), context);
		Receive_Critical_Section.lock();
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		if ((now.QuadPart - started.QuadPart) * 1000 >= frequency.QuadPart * 8) break;
	}
	Receive_Critical_Section.unlock();
}

void NetServer::insertReceived(NetMessage* msg)
{
	received.push(msg, true);
}

void NetServer::RemoveUserMessages()
{
	std::lock_guard<std::recursive_mutex> lock(Receive_Critical_Section);
	received.clear();
}

void NetServer::RemoveSendComplete()
{
	Send_Critical_Section.lock();
	Ref<NetMessage> tmp;
	while (sent)
	{
		tmp = sent->next;
		sent->next = NULL; 
		sent = tmp;
	}
	Send_Critical_Section.unlock();
}

void NetServer::ProcessPlayers(CreatePlayerCallback* callbackCreate, DeletePlayerCallback* callbackDelete, void* context)
{
	User_Critical_Section.lock();
	for (size_t i = 0; i < _deletePlayers.size(); i++)
	{
		DeletePlayerInfo& info = _deletePlayers[i];
		
		callbackDelete(info.player, context);

		session.playerCount--;
	}
	if (session.playerCount < 0)
		session.playerCount = 0;
	_deletePlayers.clear();
	for (size_t i = 0; i < _createPlayers.size(); i++)
	{
		CreatePlayerInfo& info = _createPlayers[i];
		
		callbackCreate(info.player, info.botClient, info.name, info.inaddr, context);
		session.playerCount++;
	}
	_createPlayers.clear();
	User_Critical_Section.unlock();
}

void NetServer::RemovePlayers()
{
	User_Critical_Section.lock();
	_createPlayers.clear();
	_deletePlayers.clear();
	User_Critical_Section.unlock();
}

unsigned NetServer::FreeMemory()
{
	if (!NetMessagePool::pool())
		return 0;
	unsigned ret = NetMessagePool::pool()->freeMemory();
	return ret;
}

Ref<NetChannel> NetServer::playerToChannel(__int32 player)
{
	Ref<NetChannel> channel;
	User_Critical_Section.lock();
	users.get(player, channel);
	User_Critical_Section.unlock();
	return channel;
}
