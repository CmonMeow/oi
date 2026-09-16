
#include "../sysdef.h"
#include "netTransport.hpp"
#include <time.h>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <vector>
#include "netpch.hpp"
#include "netpeer.hpp"
#include "netchannel.hpp"
#include "AsyncResolver.h"

class NetSessionEnum;
class NetClient;
class NetServer;

unsigned short GetNetworkPort() { return 777; }
void decodeURLAddress(std::string address, std::string& ip, unsigned short& port);
Ref<NetMessage> mergeMessageList(NetMessage* msg);
NetPool* getPool();

static __int32 GenerateServerChallenge(const sockaddr_in& distant)
{
	static volatile LONG counter = 0;
	typedef BOOLEAN (APIENTRY *RtlGenRandomFn)(PVOID, ULONG);
	static RtlGenRandomFn rtlGenRandom = NULL;
	static bool randomProviderLoaded = false;
	if (!randomProviderLoaded)
	{
		HMODULE advapi = LoadLibraryA("advapi32.dll");
		if (advapi)
			rtlGenRandom = reinterpret_cast<RtlGenRandomFn>(GetProcAddress(advapi, "SystemFunction036"));
		randomProviderLoaded = true;
	}
	unsigned __int32 randomValue = 0;
	if (rtlGenRandom && rtlGenRandom(&randomValue, sizeof(randomValue)))
	{
		randomValue &= 0x7fffffff;
		return randomValue == 0 ? 1 : static_cast<__int32>(randomValue);
	}

	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);

	unsigned __int64 value = static_cast<unsigned __int64>(qpc.QuadPart);
	value ^= GetTickCount64() + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
	value ^= static_cast<unsigned __int64>(InterlockedIncrement(&counter)) * 0xbf58476d1ce4e5b9ull;
	value ^= static_cast<unsigned __int64>(distant.sin_addr.s_addr) << 17;
	value ^= static_cast<unsigned __int64>(distant.sin_port) << 41;
	value ^= static_cast<unsigned __int64>(GetCurrentProcessId()) << 9;
	value ^= static_cast<unsigned __int64>(GetCurrentThreadId()) << 33;

	value ^= value >> 30;
	value *= 0xbf58476d1ce4e5b9ull;
	value ^= value >> 27;
	value *= 0x94d049bb133111ebull;
	value ^= value >> 31;

	__int32 challenge = static_cast<__int32>(value & 0x7fffffff);
	return challenge == 0 ? 1 : challenge;
}

#ifndef DPNID_ALL_PLAYERS_GROUP
#define DPNID_ALL_PLAYERS_GROUP 0
#endif

#define MAX_SESSIONS 255
#define LEN_SESSION_NAME 256
#define LEN_GAMETYPE_NAME 8
#define LEN_MISSION_NAME 40

#define RESERVED_IDS 1

#define LEN_PLAYER_NAME 40
#define LEN_PASSWORD_NAME 40

#define MAX_ENUM_AGE 10000

#define MIN_ENUM_RETRY 4000

#define ACK_PLAYER_TIMEOUT_MS 8000

#define CREATE_PLAYER_RESEND_MS 2000


#define DESTRUCT_WAIT 500

#define DESTROY_WAIT 100

#define SEND_TIMEOUT 800000

#define MSG_MAGIC_FLAG 0x0001

#define MAGIC_ENUM_REQUEST 0xeee191ae

#define MAGIC_ENUM_RESPONSE 0xfff1e8ac

#define MAGIC_REQUEST_PLAYER 0xbbba1564

#define MAGIC_CHALLENGE_PLAYER 0xa5a48965

#define MAGIC_CREATE_W_CHALLENGE 0xccca5e62

#define MAGIC_ACK_PLAYER 0xaaa51a7e




#pragma pack(push, netPackets, 1)

struct MagicPacket 
{
	unsigned __int32 magic;
};

struct SessionPacket : public MagicPacket 
{
	char name[LEN_SESSION_NAME];
	__int32 serverState;
	__int32 maxPlayers;
	short password;
	short port;
	__int32 playerCount;
	unsigned __int32 request;
};

const size_t SESSION_PACKET_SIZE = sizeof(SessionPacket);

struct NetSessionDescription : public SessionPacket
{
	char address[32];
	unsigned __int32 ip;
	unsigned __int64 lastTime;
	unsigned __int32 pingTime;
};

struct ChallengePlayerPacket : public MagicPacket 
{
	__int32 challenge;
};

struct CreatePlayerPacket : public MagicPacket 
{
	char name[LEN_PLAYER_NAME];
	char password[LEN_PASSWORD_NAME];
	__int32 actualBuild;
	short botClient; 
};

struct CreatePlayerPacketChallenge : public CreatePlayerPacket 
{
	__int32 challenge;
};

struct AckPlayerPacket : public MagicPacket 
{
	__int32 result;
	__int32 playerNo;
};



#pragma pack(pop, netPackets)

class NetSessionDescriptions
{

protected:
	
	__int32 _size;

	NetSessionDescription _data[MAX_SESSIONS];

public:
	
	NetSessionDescriptions() { Clear(); }

	~NetSessionDescriptions() { Clear(); }

	__int32 Size() { return _size; }

	__int32 Add();

	void Delete(__int32 i);

	void Clear();

	NetSessionDescription& operator[](__int32 i)
	{
		return _data[i];
	}

	const NetSessionDescription& operator[](__int32 i) const
	{
		return _data[i];
	}
};

class NetSessionEnum : public NetTranspSessionEnum
{

protected:
	
	NetSessionDescriptions _sessions;

	mutable std::recursive_mutex Critical_Section;

	bool _running;

	void sendRequest(NetChannel* br, struct sockaddr_in& addr, unsigned short port);

	bool needsRequest(struct sockaddr_in& addr, unsigned short port);

	friend NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data);

public:
	
	NetSessionEnum();

	virtual ~NetSessionEnum();

	virtual bool Init() override;

	virtual void Done();

	virtual std::string IPToGUID(std::string ip, __int32 port) override;

	virtual bool RunningEnumHosts() override
	{
		return _running;
	}

	virtual bool StartEnumHosts(std::string ip, unsigned short port) override;

	virtual void StopEnumHosts() override;

	virtual void ClearEnumHosts() override { Done(); }

	virtual __int32 NSessions() override;

	virtual void GetSessions(std::vector<SessionInfo>& sessions) override;
};

static NetPeer* getClientPeer();

std::recursive_mutex& poolCriticalSection()
{
	static std::recursive_mutex Critical_Section;
	return Critical_Section;
}

class NetClient : public NetTranspClient
{
    AsyncResolver resolver;
    sockaddr_in connectingAddress = {};
    CreatePlayerPacketChallenge connectPacket = {};
    unsigned __int64 connectDeadline = 0, nextConnectSend = 0;
    bool resolving = false, challengeSent = false;
    ConnectResult connectResult = CRError;
    CancelNNCallback* cancelConnect = nullptr;


protected:
	
	unsigned __int64 lastMsgReported;

	Ref<NetChannel> channel;

	__int32 challengePlayer;
	
	__int32 ackPlayer;

	__int32 playerNo;

	bool amIBot;

	bool sessionTerminated;

	NetTerminationReason whySessionTerminated;

	char whySessionTerminatedStr[512] = {};

	Ref<NetMessage> received;

	void insertReceived(NetMessage* msg);

	Ref<NetMessage> split;

	NetMessage* lastSplit;

	Ref<NetMessage> splitUrgent;

	NetMessage* lastSplitUrgent;

	mutable std::recursive_mutex Receive_Critical_Section;

	Ref<NetMessage> sent;

	mutable std::recursive_mutex Send_Critical_Section;

	inline void enterAll() const
	{
		poolCriticalSection().lock();
		NetPeer* peer = getClientPeer();
		if (peer)
			peer->enterPeer();
		Send_Critical_Section.lock();
	}
	inline void leaveAll() const
	{
		Send_Critical_Section.unlock();
		NetPeer* peer = getClientPeer();
		if (peer)
			peer->leavePeer();
		poolCriticalSection().unlock();
	}

	friend NetStatus clientReceive(NetMessage* msg, NetStatus event, void* data);
	friend NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data);
	friend NetStatus clientSendComplete(NetMessage* msg, NetStatus event, void* data);

	bool SendMagicPacket(const void* data, size_t size);

public:
	
	NetClient();

	virtual ~NetClient();

	virtual ConnectResult Init(
		std::string address, std::string password, bool botClient, unsigned short& port,
		std::string player, CancelNNCallback* cancelNNCallback = NULL);


	ConnectResult PollInit() override;

	virtual Ref<NetMessage> SendMsg(BYTE* buffer, __int32 bufferSize, DWORD& msgID, NetMsgFlags flags, const Ref<NetMessage>& dependOn);

	virtual void GetSendQueueInfo(__int32& nMsg, __int32& nBytes, __int32& nMsgG, __int32& nBytesG);

	virtual bool GetConnectionInfo(__int32& latencyMS, __int32& throughputBPS);
    void GetTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const override
    {
        std::lock_guard<std::recursive_mutex> guard(poolCriticalSection());
        NetPeer* peer = getClientPeer();
        incoming = outgoing = 0;
        if (peer) peer->getTrafficTotals(incoming, outgoing);
    }
	virtual void GetConnectionLimits(__int32& maxBandwidthPerClient);

	virtual bool GetLocalAddress(in_addr& addr, __int32& port) const;

	virtual bool GetLocalAddress(in_addr& addr) const;

	virtual bool GetDistantAddress(in_addr& addr, __int32& port) const;

	virtual bool GetServerAddress(sockaddr_in& address) const;

	virtual bool IsSessionTerminated();

	virtual NetTerminationReason GetWhySessionTerminated();
	virtual std::string GetWhySessionTerminatedStr();

	virtual void ProcessUserMessages(UserMessageClientCallback* callback, void* context);

	virtual void RemoveUserMessages();

	virtual void RemoveSendComplete();

	virtual unsigned FreeMemory();

};

class ChannelSupport
{
public:
	
	__int32 m_id;

	Ref<NetMessage> m_split;

	NetMessage* m_lastSplit;

	Ref<NetMessage> m_splitUrgent;

	NetMessage* m_lastSplitUrgent;

	ChannelSupport()
	{
		m_id = 0;
		m_lastSplit = m_lastSplitUrgent = NULL;
	}

	ChannelSupport(const ChannelSupport& from)
	{
		m_id = from.m_id;
		m_split = from.m_split;
		m_lastSplit = from.m_lastSplit;
		m_splitUrgent = from.m_splitUrgent;
		m_lastSplitUrgent = from.m_lastSplitUrgent;
	}

	bool operator==(const ChannelSupport& sec) const
	{
		return (m_id == sec.m_id);
	}
};

class cNetUserMap
{
	std::unordered_map<__int32, Ref<NetChannel>> _users;

	bool getByIndex(unsigned index, Ref<NetChannel>& result, __int32* key) const
	{
		if (index >= _users.size())
		{
			result = NULL;
			if (key)
				*key = -1;
			return false;
		}

		std::unordered_map<__int32, Ref<NetChannel>>::const_iterator it = _users.begin();
		std::advance(it, index);
		result = it->second;
		if (key)
			*key = it->first;
		return true;
	}

public:
	unsigned card() const
	{
		return (unsigned)_users.size();
	}

	bool get(__int32 key, Ref<NetChannel>& result) const
	{
		std::unordered_map<__int32, Ref<NetChannel>>::const_iterator it = _users.find(key);
		if (it == _users.end())
		{
			result = NULL;
			return false;
		}
		result = it->second;
		return true;
	}

	bool put(__int32 key, NetChannel* value)
	{
		_users[key] = value;
		return true;
	}

	bool removeKey(__int32 key)
	{
		return _users.erase(key) != 0;
	}

	bool getFirst(unsigned& iterator, Ref<NetChannel>& first, __int32* key = NULL) const
	{
		iterator = 0;
		return getByIndex(iterator++, first, key);
	}

	bool getNext(unsigned& iterator, Ref<NetChannel>& next, __int32* key = NULL) const
	{
		return getByIndex(iterator++, next, key);
	}
};

class NetServer : public NetTranspServer
{

protected:
	
	std::string _password;

	SessionPacket session;

	mutable std::recursive_mutex User_Critical_Section;

	unsigned short sessionPort;

	std::string sessionName;

	Ref<NetMessage> received;

	void insertReceived(NetMessage* msg);

	std::unordered_map<unsigned __int64, ChannelSupport> m_support;

	mutable std::recursive_mutex Receive_Critical_Section;

	Ref<NetMessage> sent;

	mutable std::recursive_mutex Send_Critical_Section;

	cNetUserMap users;

	__int32 botId;

	std::vector<CreatePlayerInfo> _createPlayers;

	std::vector<DeletePlayerInfo> _deletePlayers;

	bool m_enumResponse;

	struct PlayerChallengeSent {
		sockaddr_in addr;
		__int32 challenge;
		unsigned __int64 time;
	};

	typedef std::vector<PlayerChallengeSent> PlayerChallengeSentList;
	PlayerChallengeSentList _challengesSent;

	static void Expire(PlayerChallengeSentList& list, unsigned __int64 timeout);
	static __int32 FindChallenge(const PlayerChallengeSentList& list, const sockaddr_in& addr);
	static void DeleteChallengeAt(PlayerChallengeSentList& list, __int32 index);
	static void DeleteAllChallenges(PlayerChallengeSentList& list, const sockaddr_in& addr);

	__int32 channelToPlayer(NetChannel* ch);

	friend NetStatus ctrlReceive(NetMessage* msg, NetStatus event, void* data);
	friend NetStatus serverReceive(NetMessage* msg, NetStatus event, void* data);
	friend NetStatus serverSendComplete(NetMessage* msg, NetStatus event, void* data);

	void destroyPlayer(NetChannel* ch, NetTerminationReason reason, const char* reasonStr = NULL);

	void finishDestroyPlayer(__int32 player);


public:
	
	NetServer();

	virtual ~NetServer();

	virtual bool Init(std::string name, std::string password, __int32 port);
	void UpdateLockedOrPassworded(bool lock, bool passworded);

	virtual __int32 GetSessionPort()
	{
		return sessionPort;
	}

	virtual std::string GetSessionName()
	{
		return sessionName;
	}

	virtual Ref<NetMessage> SendMsg(__int32 to, BYTE* buffer, __int32 bufferSize, DWORD& msgID, NetMsgFlags flags, const Ref<NetMessage>& dependOn);

	virtual void CancelAllMessages();


	virtual void GetSendQueueInfo(__int32 to, __int32& nMsg, __int32& nBytes, __int32& nMsgG, __int32& nBytesG);

	virtual bool GetConnectionInfo(__int32 to, __int32& latencyMS, __int32& throughputBPS);
    void GetTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const override;
	virtual void GetConnectionLimits(__int32& maxBandwidthPerClient);

	virtual void UpdateSessionDescription(__int32 state);

	virtual bool GetURL(char* address, DWORD addressLen);

	virtual bool GetServerAddress(sockaddr_in& address);
	virtual bool GetClientAddress(__int32 client, sockaddr_in& address);

	virtual void KickOff(__int32 player, NetTerminationReason reason, const char* reasonStr = NULL);

	virtual void ProcessUserMessages(UserMessageServerCallback* callback, void* context);

	virtual void RemoveUserMessages();

	virtual void RemoveSendComplete();

	virtual void ProcessPlayers(CreatePlayerCallback* callbackCreate, DeletePlayerCallback* callbackDelete, void* context);

	virtual void RemovePlayers();

	virtual void NotifyChannelDataSent(__int32 player, size_t size);

	virtual unsigned FreeMemory();

	virtual Ref<NetChannel> playerToChannel(__int32 player);
};

NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data);
NetStatus ctrlReceive(NetMessage* msg, NetStatus event, void* data);
NetStatus clientReceive(NetMessage* msg, NetStatus event, void* data);
NetStatus serverReceive(NetMessage* msg, NetStatus event, void* data);

static Ref<NetPool> pool; 

static Ref<NetPeer> clientPeer; 

static Ref<NetPeer> serverPeer; 

void destroyPool();

void createPool()
{
	if (!pool)
	{
        pool = new NetPool();
        // Run while all global pools and synchronization objects are still alive.
        static const int registered = std::atexit(destroyPool);
        if (registered != 0) Error("Could not register network shutdown");
	}
}

void destroyPool()
{
    // Listener callbacks may need the pool mutex; never hold it while joining them.
    Ref<NetPeer> closingClient, closingServer;
    poolCriticalSection().lock();
    closingClient = clientPeer;
    closingServer = serverPeer;
    poolCriticalSection().unlock();
    if (closingClient) closingClient->stopThreads();
    if (closingServer) closingServer->stopThreads();
    poolCriticalSection().lock();
	if (clientPeer)
	{
		if (pool)
			pool->deletePeer(clientPeer.GetRef());
		else
			clientPeer->close();
		clientPeer = NULL;
	}
	if (serverPeer)
	{
		if (pool)
			pool->deletePeer(serverPeer.GetRef());
		else
			serverPeer->close();
		serverPeer = NULL;
	}

	pool = NULL;
	poolCriticalSection().unlock();
}

static void setupPortBitMask(BitMask& mask, bool server)
{
	mask.empty();
	mask.on(server ? GetNetworkPort() : 0);
}

static NetPeer* getClientPeer()

{
	if (!clientPeer)
	{
		clientPeer = getPool()->createPeer(NULL);
		if (clientPeer)
		{
			NetChannel* ctrl = clientPeer->getBroadcastChannel();
			if (ctrl)
				ctrl->setProcessRoutine(enumReceive);
		}
	}
	return clientPeer.GetRef();
}

static NetPeer* getServerPeer(bool create = true)

{
	if (create && !serverPeer)
	{
		serverPeer = getPool()->createPeer(GetNetworkPort());
		if (serverPeer)
		{
			NetChannel* ctrl = serverPeer->getBroadcastChannel();
			if (ctrl)
				ctrl->setProcessRoutine(ctrlReceive);
		}
	}
	return serverPeer.GetRef();
}

static NetSessionEnum* _enum = NULL;

static NetClient* _client = NULL;

static NetServer* _server = NULL;

std::recursive_mutex& NatCriticalSection()
{
	static std::recursive_mutex Critical_Section;
	return Critical_Section;
}

void enterNN() { NatCriticalSection().lock(); }
void leaveNN() { NatCriticalSection().unlock(); }

void stopUdpListenSend()
{
	poolCriticalSection().lock();
	if (clientPeer)
		clientPeer->stopThreads();
	if (serverPeer)
		serverPeer->stopThreads();
	clientPeer = NULL;
	serverPeer = NULL;
	poolCriticalSection().unlock();
}

void NetServer::Expire(PlayerChallengeSentList& list, unsigned __int64 timeout)
{
	const unsigned __int64 now = GetTickCount64();
	list.erase(std::remove_if(list.begin(), list.end(), [now, timeout](const PlayerChallengeSent& item) {
		return now - item.time > timeout;
	}), list.end());
}

__int32 NetServer::FindChallenge(const PlayerChallengeSentList& list, const sockaddr_in& addr)
{
	for (size_t i = 0; i < list.size(); ++i)
		if (list[i].addr.sin_port == addr.sin_port && ADDR(list[i].addr) == ADDR(addr))
			return (__int32)i;
	return -1;
}

void NetServer::DeleteChallengeAt(PlayerChallengeSentList& list, __int32 index)
{
	if (index >= 0 && (size_t)index < list.size())
		list.erase(list.begin() + index);
}

void NetServer::DeleteAllChallenges(PlayerChallengeSentList& list, const sockaddr_in& addr)
{
	list.erase(std::remove_if(list.begin(), list.end(), [&addr](const PlayerChallengeSent& item) {
		return item.addr.sin_port == addr.sin_port && ADDR(item.addr) == ADDR(addr);
	}), list.end());
}

NetStatus ctrlReceive(NetMessage* msg, NetStatus event, void* data)
{
	if (!_server || !msg || msg->getLength() < 4 ||
		!(msg->getFlags() & MSG_MAGIC_FLAG))
		return nsNoMoreCallbacks; 

	unsigned __int32 magic = *(unsigned __int32*)msg->getData();
	struct sockaddr_in distant;
	msg->getDistant(distant);

	switch (magic)
	{

	case MAGIC_ENUM_REQUEST: 
		if (_server->m_enumResponse &&
			msg->getLength() == sizeof(MagicPacket))
		{
			_server->User_Critical_Section.lock();

			Ref<NetMessage> out = NetMessagePool::pool()->newMessage(SESSION_PACKET_SIZE, msg->getChannel());
			if (out)
			{
				out->setDistant(distant);
				out->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
				_server->session.magic = MAGIC_ENUM_RESPONSE;
				_server->session.request = msg->getSerial();
				_server->session.playerCount = _server->users.card();

				out->setData((unsigned char*)&(_server->session), SESSION_PACKET_SIZE);
				out->send(true); 
			}
			_server->User_Critical_Section.unlock();
		}
		break;
	case MAGIC_REQUEST_PLAYER:
		if (_server->m_enumResponse && msg->getLength() == sizeof(MAGIC_REQUEST_PLAYER))
		{
			_server->User_Critical_Section.lock(); 
			
			__int32 wasSent = NetServer::FindChallenge(_server->_challengesSent, distant);
			if (wasSent < 0)
			{
				NetServer::PlayerChallengeSent sent;
				sent.addr = distant;
				sent.challenge = GenerateServerChallenge(distant);
				_server->_challengesSent.push_back(sent);
				wasSent = (__int32)_server->_challengesSent.size() - 1;
			}

			unsigned __int64 now = GetTickCount64();
			ChallengePlayerPacket chp;
			chp.magic = MAGIC_CHALLENGE_PLAYER;
			chp.challenge = _server->_challengesSent[wasSent].challenge;
			_server->_challengesSent[wasSent].time = now;

			_server->Expire(_server->_challengesSent, ACK_PLAYER_TIMEOUT_MS);

			_server->User_Critical_Section.unlock();


			Ref<NetMessage> out = NetMessagePool::pool()->newMessage(sizeof(chp), msg->getChannel());
			if (out)
			{
				out->setDistant(distant);
				out->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
				out->setData((unsigned char*)&chp, sizeof(chp));
				out->send(true); 
			}

		}
		break;

	case MAGIC_CREATE_W_CHALLENGE:
		if (_server->m_enumResponse &&
			(msg->getLength() == sizeof(CreatePlayerPacket) || msg->getLength() == sizeof(CreatePlayerPacketChallenge)))
		{
			CreatePlayerPacket* cpp = (CreatePlayerPacket*)msg->getData();
			_server->User_Critical_Section.lock(); 

			if (magic == MAGIC_CREATE_W_CHALLENGE && msg->getLength() == sizeof(CreatePlayerPacketChallenge))
			{
				__int32 challenge = ((CreatePlayerPacketChallenge*)cpp)->challenge;
				__int32 wasSent = NetServer::FindChallenge(_server->_challengesSent, distant);
				if (wasSent < 0)
				{
					Error("Server: Challenge %x not found", challenge);
					_server->User_Critical_Section.unlock();
					break;
				}
				else if (challenge != _server->_challengesSent[wasSent].challenge)
				{
					Error("Server: Challenge not matching %x!=%x", challenge, _server->_challengesSent[wasSent].challenge);
					_server->User_Critical_Section.unlock();
					break;
				}
				else
				{
					NetServer::DeleteChallengeAt(_server->_challengesSent, wasSent);
				}
			}
			else
			{
				_server->User_Critical_Section.unlock();
				break;
			}

			NetServer::DeleteAllChallenges(_server->_challengesSent, distant);

			ConnectResult result = CROK;

			__int32 player = 0; 
			NetChannel* ch = NULL;
			if (result == CROK)
			{
				poolCriticalSection().lock();
				NetPeer* peer = getServerPeer();
				Ref<NetChannel> findCh;
				ch = peer->findChannel(distant);
				if (ch)
				{ 
					unsigned it;
					__int32 existingPlayer = -1;
					Ref<NetChannel> existingCh;
					if (_server->users.getFirst(it, existingCh, &existingPlayer))
					{
						do
						{
							if (existingCh.GetRef() == ch)
								break;
						}
						while (_server->users.getNext(it, existingCh, &existingPlayer));
					}
					if (existingPlayer >= RESERVED_IDS && existingCh.GetRef() == ch)
					{ 
						poolCriticalSection().unlock();
						_server->User_Critical_Section.unlock();
						break; 
					}
					
					_server->finishDestroyPlayer(_server->channelToPlayer(ch));
					ch = NULL;
					poolCriticalSection().unlock();
					_server->User_Critical_Section.unlock();
					Sleep(DESTROY_WAIT); 
					_server->User_Critical_Section.lock();
					poolCriticalSection().lock();
				}
				
				player = RESERVED_IDS;
				while (_server->users.get(player, findCh))
					player++;

				__int32 publicPlayers = _server->users.card() + 1; 
				if (_server->session.maxPlayers > 0 &&
						publicPlayers >= _server->session.maxPlayers)
					result = CRSessionFull;
				else
				{ 
					ch = peer->getPool()->createChannel(distant, peer);
					if (!ch)
					{
						result = CRError; 
						Error("NetServer: createChannel() failed => cannot insert a new player");
					}
					else
					{ 

						_server->users.put(player, ch);
						ChannelSupport sup;
						sup.m_id = player;
						_server->m_support[sockaddrKey(distant)] = sup;
						_server->_createPlayers.push_back(CreatePlayerInfo());
						CreatePlayerInfo& info = _server->_createPlayers.back();
						info.player = player;

						struct sockaddr_in inaddr;
						msg->getDistant(inaddr);
						info.inaddr = inaddr.sin_addr.s_addr; 

						if ((info.botClient = ((cpp->botClient & 1) != 0)))
							_server->botId = player;
						strncpy(info.name, cpp->name, sizeof(info.name));
						info.name[sizeof(info.name) - 1] = (char)0;
						if (strcmp(cpp->name, info.name))
							Error("NetServer: name of a new player is too long => truncating to '%s'", info.name);
						ch->setProcessRoutine(serverReceive);
					}
				}
				poolCriticalSection().unlock();
			}
			_server->User_Critical_Section.unlock();
			
			AckPlayerPacket app;
			app.magic = MAGIC_ACK_PLAYER;
			app.result = result;
			app.playerNo = player; 
								   
			Ref<NetMessage> out = NetMessagePool::pool()->newMessage(sizeof(AckPlayerPacket), ch ? ch : msg->getChannel());
			if (out)
			{
				if (!ch)
					out->setDistant(distant);
				out->setFlags(MSG_ALL_FLAGS, MSG_MAGIC_FLAG | (ch ? MSG_VIM_FLAG : MSG_FROM_BCAST_FLAG));
				out->setData((unsigned char*)&app, sizeof(app));
				out->send(true); 
			}
			if (ch) 
				ch->checkConnectivity(0);
		}
		break;


	}

	return nsNoMoreCallbacks;
}

NetStatus enumReceive(NetMessage* msg, NetStatus event, void* data)
{
	
	if ((!_enum || !_enum->_running) && !_client ||
		!msg || msg->getLength() < 4 || !(msg->getFlags() & MSG_MAGIC_FLAG))
		return nsNoMoreCallbacks; 

	unsigned __int32 magic = *(unsigned __int32*)msg->getData();
	struct sockaddr_in distant;
	msg->getDistant(distant);
	unsigned __int32 ip = ntohl(distant.sin_addr.s_addr);
	unsigned short port = ntohs(distant.sin_port);

	switch (magic)
	{
	case MAGIC_ENUM_RESPONSE: 
	{
		if (!_enum || !_enum->_running || msg->getLength() != SESSION_PACKET_SIZE)
			break;

		SessionPacket* s = (SessionPacket*)msg->getData();
		{
			_enum->Critical_Section.lock();

			__int32 iFound = -1;
			for (__int32 i = 0; i < _enum->_sessions.Size(); i++)
				if (_enum->_sessions[i].ip == ip && _enum->_sessions[i].port == port)
				{
					iFound = i;
					break;
				}

			unsigned __int64 reqTime = msg->getChannel()->getMessageTime(s->request);
			
			unsigned pingTime = reqTime ? (unsigned)((msg->getTime() - reqTime) / 1000) : 0;

			if (iFound < 0)
			{ 
				iFound = _enum->_sessions.Add();
				if (iFound < 0)
				{ 
					_enum->Critical_Section.unlock();
					return nsNoMoreCallbacks;
				}
				NetSessionDescription& ndesc = _enum->_sessions[iFound];
				ndesc.ip = ip;
				ndesc.port = port;
				ndesc.pingTime = pingTime;

				sprintf(ndesc.address, "%s:%u", inet_ntoa(distant.sin_addr), (unsigned)port);
			}

			NetSessionDescription& desc = _enum->_sessions[iFound];
			
			strncpy(desc.name, s->name, LEN_SESSION_NAME);
			desc.name[LEN_SESSION_NAME - 1] = 0;

			desc.serverState = s->serverState;
			desc.maxPlayers = s->maxPlayers;
			desc.playerCount = s->playerCount;
			desc.password = s->password;
			desc.lastTime = GetTickCount64();
			desc.pingTime = (3 * desc.pingTime + pingTime + 2) >> 2;
			_enum->Critical_Section.unlock();
		}
	}
	break;
	case MAGIC_CHALLENGE_PLAYER:
		if (msg->getLength() == sizeof(ChallengePlayerPacket) && _client)
		{
			ChallengePlayerPacket* chp = (ChallengePlayerPacket*)msg->getData();
			_client->Send_Critical_Section.lock();
			if (_client->challengePlayer == 0) 
			{
				_client->challengePlayer = chp->challenge;
			}
			_client->Send_Critical_Section.unlock();
		}
		break; 
	}

	return nsNoMoreCallbacks;
}

bool NetSessionEnum::needsRequest(struct sockaddr_in& addr, unsigned short port)
{
	bool needs = true;
	Critical_Section.lock();
	unsigned __int32 ip = ntohl(addr.sin_addr.s_addr);
	for (__int32 i = 0; i < _sessions.Size(); i++)
	{
		const NetSessionDescription& src = _sessions[i];
		if (src.ip == ip && src.port == port)
		{
			if (GetTickCount64() - src.lastTime < MIN_ENUM_RETRY)
				needs = false;
			break;
		}
	}
	Critical_Section.unlock();
	return needs;
}

void NetSessionEnum::sendRequest(NetChannel* br, struct sockaddr_in& addr, unsigned short port)
{
	MagicPacket request;
	request.magic = MAGIC_ENUM_REQUEST;

	addr.sin_port = htons(port);
	Ref<NetMessage> msg = NetMessagePool::pool()->newMessage(sizeof(MagicPacket), br);
	if (msg)
	{
		msg->setDistant(addr);
		msg->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
		msg->setData((unsigned char*)&request, sizeof(MagicPacket));
		msg->send();
	}
}

bool NetClient::SendMagicPacket(const void* data, size_t size)
{
	if (size > 0xffffffffu)
	{
		return false;
	}
	Ref<NetMessage> msgReq = NetMessagePool::pool()->newMessage(static_cast<unsigned>(size), channel.GetRef());
	if (!msgReq)
	{
		return false;
	}
	msgReq->setFlags(MSG_ALL_FLAGS, MSG_TO_BCAST_FLAG | MSG_MAGIC_FLAG);
	msgReq->setData((const unsigned char*)data, static_cast<unsigned __int32>(size));
	msgReq->send(true); 
	return true;
}

ConnectResult NetClient::Init(std::string address, std::string password, bool botClient, unsigned short& port,
    std::string player, CancelNNCallback* cancelNNCallback)
{
    if (channel || connectResult == CRNone) return CRError;
    std::string ip;
    decodeURLAddress(address.empty() ? "127.0.0.1" : address, ip, port);
    connectingAddress.sin_family = AF_INET;
    connectingAddress.sin_port = htons(port);
    resolving = InetPtonA(AF_INET, ip.c_str(), &connectingAddress.sin_addr) != 1;
    connectDeadline = GetTickCount64() + ACK_PLAYER_TIMEOUT_MS;
    nextConnectSend = 0;
    challengeSent = false;
    cancelConnect = cancelNNCallback;
    connectPacket = {};
    connectPacket.magic = MAGIC_CREATE_W_CHALLENGE;
    strncpy(connectPacket.name, player.c_str(), LEN_PLAYER_NAME - 1);
    strncpy(connectPacket.password, password.c_str(), LEN_PASSWORD_NAME - 1);
    connectPacket.botClient = botClient ? 1 : 0;
    amIBot = botClient;
    ackPlayer = CRNone;
    challengePlayer = 0;
    sessionTerminated = false;
    whySessionTerminated = NTROther;
    connectResult = CRNone;
    if (resolving) resolver.start(ip);
    return CRNone;
}

ConnectResult NetClient::PollInit()
{
    if (connectResult != CRNone) return connectResult;
    const unsigned __int64 now = GetTickCount64();
    if (now >= connectDeadline || (cancelConnect && cancelConnect()))
    {
        resolver.cancel();
        return connectResult = CRTimeout;
    }
    if (resolving)
    {
        int result = resolver.poll(connectingAddress);
        if (!result) return CRNone;
        if (result < 0) return connectResult = CRError;
        resolving = false;
    }
    enterAll();
    if (!channel)
    {
        channel = getPool()->createChannel(connectingAddress, getClientPeer());
        if (!channel) { leaveAll(); return connectResult = CRError; }
        channel->setProcessRoutine(clientReceive, channel.GetRef());
    }
    if (ackPlayer != CRNone)
    {
        channel->checkConnectivity(0);
        connectResult = static_cast<ConnectResult>(ackPlayer);
    }
    else if (now >= nextConnectSend || (!challengeSent && challengePlayer != 0))
    {
        bool sent;
        if (!challengePlayer)
        {
            unsigned __int32 request = MAGIC_REQUEST_PLAYER;
            sent = SendMagicPacket(&request, sizeof(request));
        }
        else
        {
            challengeSent = true;
            connectPacket.challenge = challengePlayer;
            sent = SendMagicPacket(&connectPacket, sizeof(connectPacket));
        }
        if (!sent) connectResult = CRError;
        nextConnectSend = now + CREATE_PLAYER_RESEND_MS;
    }
    leaveAll();
    return connectResult;
}

void VoiceDataSent(__int32 pid, __int32 len)
{
	if (_server)
		_server->NotifyChannelDataSent(pid, len);
}

void NetServer::NotifyChannelDataSent(__int32 player, size_t size)
{
	Ref<NetChannel> channel;
	users.get(player, channel);
	if (channel)
	{
		channel->dataSentAck(size);
	}
}

#include "netTransportCommon.hpp"
