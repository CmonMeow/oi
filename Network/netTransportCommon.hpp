

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

void setEnum(NetSessionEnum* _en)
{
	poolCriticalSection().lock();
	_enum = _en;
	if (_en)
	{
		getPool();
		getClientPeer();
	}
	poolCriticalSection().unlock();
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

NetTranspSessionEnum* CreateNetSessionEnum()
{
	return new NetSessionEnum;
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

	bool isMagic = (flags & MSG_MAGIC_FLAG) != 0;
	if (isMagic) 
	{
		if (len < 4)
			return nsNoMoreCallbacks; 
		unsigned __int32 magic = *(unsigned __int32*)msg->getData();

		switch (magic)
		{

		case MAGIC_DESTROY_PLAYER: 
		{
			_server->User_Critical_Section.lock(); 
			__int32 player = _server->channelToPlayer(msg->getChannel());
			if (player == -1)
				Error("No player found for channel %p - MAGIC_DESTROY_PLAYER message ignored", (void*)msg->getChannel());
			else
				_server->finishDestroyPlayer(player);
			_server->User_Critical_Section.unlock();
			break; 
		}
		}

		return nsNoMoreCallbacks; 
	}

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
		if (msg)
			Error("serverReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
				msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(), msg->id);
	}

	_server->insertReceived(msg.GetRef());

	_server->Receive_Critical_Section.unlock();

	return nsNoMoreCallbacks;
}

__int32 NetSessionDescriptions::Add()
{
	if (_size >= MAX_SESSIONS)
		return -1;
	return _size++;
}

void NetSessionDescriptions::Delete(__int32 i)
{
	_size--;
	for (__int32 j = i; j < _size; j++)
		_data[j] = _data[j + 1];
}

void NetSessionDescriptions::Clear()
{
	_size = 0;
}

NetSessionEnum::NetSessionEnum()
	: Critical_Section()
{
	_running = false;
	setEnum(this);
}

NetSessionEnum::~NetSessionEnum()
{
	setEnum(NULL);
	Done();
	
}

std::string NetSessionEnum::IPToGUID(std::string ip, __int32 port)
{
	char buffer[256];
	sprintf(buffer, "%s:%d", ip.c_str(), port);
	return buffer;
}

bool NetSessionEnum::Init()
{
	setEnum(this);
	return true;
}

void NetSessionEnum::Done()
{
	Critical_Section.lock();
	StopEnumHosts();
	_sessions.Clear();
	Critical_Section.unlock();
}

bool NetSessionEnum::StartEnumHosts(std::string ip, unsigned short port)
{ 
	_running = true;
	bool anything = false;
	setEnum(this);
	NetPeer* peer = getClientPeer();

	if (!peer)
	{
		Error("Error: createPeer failed");
		return false;
	}

	NetChannel* br = peer->getBroadcastChannel();
	
	std::string ipaddr;
	struct sockaddr_in addr;

	if (!ip.length())
	{ 
		getHostAddress(addr, NULL, port);
		if (needsRequest(addr, port))
		{
			sendRequest(br, addr, port); 
			anything = true;
		}
		getLocalAddress(addr, port);
		if (needsRequest(addr, port))
		{
			sendRequest(br, addr, port); 
			anything = true;
		}
	}
	else
	{
		decodeURLAddress(ip, ipaddr, port);
		if (getHostAddress(addr, ipaddr.c_str(), port) && 
			needsRequest(addr, port))
		{
			sendRequest(br, addr, port);
			anything = true;
		}
	}

	return anything;
}

void NetSessionEnum::StopEnumHosts()
{
	_running = false;
}

__int32 NetSessionEnum::NSessions()
{
	Critical_Section.lock();
	__int32 n = _sessions.Size();
	Critical_Section.unlock();
	return n;
}

void NetSessionEnum::GetSessions(std::vector<SessionInfo>& sessions)
{
	Critical_Section.lock();

	__int32 n = _sessions.Size();
	for (__int32 i = 0; i < n;)
	{
		if (GetTickCount64() - _sessions[i].lastTime > MAX_ENUM_AGE)
		{
			if(!StartEnumHosts(_sessions[i].address, _sessions[i].port))
			_sessions.Delete(i);
			n--;
		}
		else
			i++;
	}

	sessions.resize(n);
	for (__int32 i = 0; i < n; i++)
	{
		sessions[i].address = _sessions[i].address;
		sessions[i].name = _sessions[i].name;
		sessions[i].lastTime = _sessions[i].lastTime;
		sessions[i].password = (_sessions[i].password & 2) != 0;
		sessions[i].lock = (_sessions[i].password & 1) != 0;
		sessions[i].badActualVersion = false;
		sessions[i].badRequiredVersion = false;
		sessions[i].serverState = _sessions[i].serverState;
		sessions[i].playerCount = _sessions[i].playerCount;
		sessions[i].maxPlayers = _sessions[i].maxPlayers;
		sessions[i].ping = _sessions[i].pingTime;
	}

	Critical_Section.unlock();
}

NetClient::NetClient()
	: Send_Critical_Section(),
	  Receive_Critical_Section()
{
	
	lastMsgReported = GetTickCount64();
	
	channel = NULL;
	
	received = NULL;
	split = lastSplit = NULL;
	splitUrgent = lastSplitUrgent = NULL;
	sent = NULL;
	sessionTerminated = false;
	whySessionTerminated = NTROther;
	playerNo = -1; 
	setClient(this);
}

void NetClient::sendDisconnectMsg()
{
	Send_Critical_Section.lock();
	if (!sessionTerminated) 
	{
		sessionTerminated = true;
		whySessionTerminated = NTRDisconnected;
		
		if (channel)
		{
			Ref<NetMessage> out = NetMessagePool::pool()->newMessage(4, channel.GetRef());
			if (out)
			{
				unsigned __int32 magic = MAGIC_DESTROY_PLAYER;
				out->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG);
				out->setData((unsigned char*)&magic, sizeof(magic));
				out->send(true);
				Sleep(DESTRUCT_WAIT);
			}
		}
	}
	Send_Critical_Section.unlock();
}

NetClient::~NetClient()
{
	Send_Critical_Section.lock();
	if (!sessionTerminated) 
	{
		sessionTerminated = true;
		whySessionTerminated = NTRDisconnected;
		
		if (channel)
		{
			Ref<NetMessage> out = NetMessagePool::pool()->newMessage(4, channel.GetRef());
			if (out)
			{
				unsigned __int32 magic = MAGIC_DESTROY_PLAYER;
				out->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG);
				out->setData((unsigned char*)&magic, 4);
				out->send(true);
				Sleep(DESTRUCT_WAIT);
			}
		}
	}

	setClient(NULL);
	
	RemoveUserMessages();
	RemoveSendComplete();

	if (channel)
	{
		NetChannel* old = channel.GetRef();
		channel = NULL;
		Send_Critical_Section.unlock();
		getPool()->deleteChannel(old);
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

		case MAGIC_TERMINATE_SESSION: 
									  
			NetTerminationReason reason = NTROther;
			_client->whySessionTerminatedStr[0] = 0; 
			if (len >= 2 * sizeof(unsigned __int32))
			{
				reason = (NetTerminationReason)((unsigned __int32*)msg->getData())[1];
				if (len >= 2 * sizeof(unsigned __int32))
				{
					__int32 len = ((__int32*)msg->getData())[2];
					if (len > 0)
					{
						if (len > 511)
							len = 511;
						strncpy(_client->whySessionTerminatedStr, (char*)msg->getData() + 3 * sizeof(unsigned __int32), len);
						_client->whySessionTerminatedStr[len] = 0; 
					}
				}
			}
			_client->Send_Critical_Section.lock();
			_client->sessionTerminated = true;
			_client->whySessionTerminated = reason;
			_client->Send_Critical_Section.unlock();
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
		if (msg)
			Error("clientReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
				msg->getLength(), msg->getSerial(), (unsigned)msg->getFlags(), msg->id);
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
	Send_Critical_Section.unlock();
	return sessionTerminated;
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
	Ref<NetMessage> msg;
	Receive_Critical_Section.lock();

	while (received)
	{
		msg = received;
		received = msg->next;
		msg->next = NULL;
		Receive_Critical_Section.unlock();

		(*callback)((char*)msg->getData(), msg->getLength(), context);
		Receive_Critical_Section.lock();
	}
	Receive_Critical_Section.unlock();
}

void NetClient::insertReceived(NetMessage* msg)

{
	if (!msg)
		return;
	if (received)
	{
		unsigned __int32 s = msg->getSerial();
		if (s < received->getSerial())
		{
			msg->next = received;
			received = msg;
		}
		else
		{
			NetMessage* ptr = received.GetRef();
			while (ptr->next && ptr->next->getSerial() < s)
				ptr = ptr->next.GetRef();
			msg->next = ptr->next;
			ptr->next = msg;
		}
	}
	else
	{
		msg->next = NULL;
		received = msg;
	}
}

void NetClient::RemoveUserMessages()
{
	Receive_Critical_Section.lock();
	Ref<NetMessage> tmp;
	while (received)
	{
		tmp = received->next;
		received->next = NULL; 
		received = tmp;
	}
	Receive_Critical_Section.unlock();
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
	received = NULL;
	sent = NULL;
	m_enumResponse = true;
	session.serverState = 0;
	session.maxPlayers = 0xff;
	session.playerCount = 0;
	session.password = false;
	session.port = 0;
	session.name[0] = (char)0;
	botId = 0;

	setServer(this);
	poolCriticalSection().lock();
	sessionPort = getServerPeer() ? getServerPeer()->getPort() : 0;
	poolCriticalSection().unlock();
	User_Critical_Section.unlock();
}

void NetServer::disconnectAllPlayers()
{
	
	m_enumResponse = false;
	
	unsigned it;
	User_Critical_Section.lock();
	Ref<NetChannel> ch;
	if (users.getFirst(it, ch))
		do
			destroyPlayer(ch.GetRef(), NTRDisconnected);
		while (users.getNext(it, ch));
	User_Critical_Section.unlock();
	Sleep(DESTRUCT_WAIT); 
}

NetServer::~NetServer()
{
	CancelAllMessages();
	setServer(NULL);
	
	RemoveUserMessages();
	RemoveSendComplete();
	
	RemovePlayers();
	
	FreeMemory();
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

	bool dropped = (to != botId) && channel->dropped();
	if (dropped)
		finishDestroyPlayer(to);
	User_Critical_Section.unlock();
	return true;
}

void NetServer::GetConnectionLimits(__int32& maxBandwidthPerClient)
{
	maxBandwidthPerClient = networkParams.maxBandwidth;
}

NetStatus destroyPlayerCallback(NetMessage* msg, NetStatus event, void* data)

{
	poolCriticalSection().lock();
	__int32 player = static_cast<__int32>(reinterpret_cast<INT_PTR>(data));
	if (_server)
		_server->finishDestroyPlayer(player);
	poolCriticalSection().unlock();
	return nsNoMoreCallbacks;
}

void NetServer::destroyPlayer(NetChannel* ch, NetTerminationReason reason, const char* reasonStr)
{
	
	ch->cancelAllMessages();
	
	char buf[1024];
	unsigned __int32* magic = (unsigned __int32*)buf;
	size_t reasonLenSize = (reasonStr ? strlen(reasonStr) : 0);
	const size_t maxReasonLen = sizeof(buf) - 3 * sizeof(unsigned __int32) - 1;
	if (reasonLenSize > maxReasonLen)
		reasonLenSize = maxReasonLen;
	__int32 reasonLen = static_cast<__int32>(reasonLenSize);
	__int32 msgLen = 3 * sizeof(unsigned __int32) + reasonLen;
	Ref<NetMessage> out = NetMessagePool::pool()->newMessage(msgLen, ch);
	if (!out)
		return;
	magic[0] = MAGIC_TERMINATE_SESSION;
	magic[1] = reason;
	magic[2] = reasonLen;
	if (reasonStr)
	{
		memcpy(buf + 3 * sizeof(unsigned __int32), reasonStr, reasonLen);
		buf[msgLen] = 0;
	}
	out->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG);
	out->setData((unsigned char*)buf, msgLen);
	
	out->setCallback(destroyPlayerCallback, nsOutputSent, reinterpret_cast<void*>(static_cast<INT_PTR>(channelToPlayer(ch))));
	out->send(true);
}

void NetServer::logUsers()
{
	char usersText[1024];

	unsigned it;
	__int32 key;
	Ref<NetChannel> channel;
	sprintf(usersText, "%d: ", users.card());
	if (users.getFirst(it, channel, &key))
		do
			sprintf(usersText + strlen(usersText), " %d", key);
		while (users.getNext(it, channel, &key));
		Error("NetServer::logUsers %s", usersText);
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
	m_support.erase(sockaddrKey(daddr));
	getPool()->deleteChannel(ch.GetRef());
	for (std::vector<CreatePlayerInfo>::iterator it = _createPlayers.begin(); it != _createPlayers.end(); ++it)
		if (it->player == player)
		{
			Error("NetServer::finishDestroyPlayer(%d): DESTROY immediately after CREATE, both cancelled", player);
			_createPlayers.erase(it);
			User_Critical_Section.unlock();
			return;
		}
	_deletePlayers.push_back(DeletePlayerInfo());
	DeletePlayerInfo& info = _deletePlayers.back();
	info.player = player;
	Error("NetServer:finishDestroyPlayer (waiting for ProcessPlayers) - session.playerCount=%d, playerId=%d, |users|=%u",
		session.playerCount, info.player, users.card());
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
					Send_Critical_Section.unlock();
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
	Ref<NetMessage> msg;
	Receive_Critical_Section.lock();

	while (received)
	{
		msg = received;
		received = msg->next;
		msg->next = NULL;
		
		User_Critical_Section.lock();
		__int32 player = channelToPlayer(msg->getChannel());
		User_Critical_Section.unlock();
		Receive_Critical_Section.unlock();
		if (player == -1)
			Error("No player found for channel %p - message ignored", (void*)msg->getChannel());
		else
			(*callback)(player, (char*)msg->getData(), msg->getLength(), context);
		Receive_Critical_Section.lock();
	}
	Receive_Critical_Section.unlock();
}

void NetServer::insertReceived(NetMessage* msg)

{
	if (!msg)
	{
		received = NULL;
		return;
	}

	if (received)
	{
		unsigned __int32 s = msg->getSerial();
		if (s < received->getSerial())
		{
			msg->next = received;
			received = msg;
		}
		else
		{
			NetMessage* ptr = received.GetRef();
			while (ptr->next && ptr->next->getSerial() < s)
				ptr = ptr->next.GetRef();
			msg->next = ptr->next;
			ptr->next = msg;
		}
	}
	else
	{
		msg->next = NULL;
		received = msg;
	}
}

void NetServer::RemoveUserMessages()
{
	Receive_Critical_Section.lock();
	Ref<NetMessage> tmp;
	while (received)
	{
		tmp = received->next;
		received->next = NULL; 
		received = tmp;
	}
	Receive_Critical_Section.unlock();
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
	if (!_deletePlayers.empty() || !_createPlayers.empty())
		Error("NetServer::ProcessPlayers(): users.card=%u, session.playerCount=%d, created=%d, deleted=%d",
			users.card(), session.playerCount, (int)_createPlayers.size(), (int)_deletePlayers.size());
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
