#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NET_TRANSPORT_HPP
#define _NET_TRANSPORT_HPP

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <string>
#include <vector>

#include "pointers.h"
#include "bitmask.hpp"

#include "netpch.hpp"

#ifndef DECL_ENUM_CONNECT_RESULT
#define DECL_ENUM_CONNECT_RESULT
enum ConnectResult : __int32;
#endif
enum ConnectResult : __int32
{
	CRNone = -1,
	CROK,
	CRPassword,
	CRVersion,
	CRError,
	CRName,
	CRSessionFull,
	CRTimeout,
};

enum NetMsgFlags {
	NMFNone = 0,
	NMFGuaranteed = 1,
	NMFHighPriority = 2,
	NMFStatsAlreadyDone = 4,
	NMFSetCallback = 8
};

enum NetAppMessageType : unsigned char {
    NAMTConnect = 1, NAMTDisconnect = 2, NAMTChat = 3, NAMTHeartbeat = 4,
    NAMTPlayerAssign = 5, NAMTVoice = 12, NAMTKeyHello = 13, NAMTKeyAccept = 14,
    NAMTChatKey = 17, NAMTPrivateChat = 18, NAMTEncrypted = 26, NAMTSessionEnd = 27, NAMTPresence = 28, NAMTCommandError = 29, NAMTSystemNotice = 30, NAMTFile = 31, NAMTScreen = 32
};

#pragma pack(push, netAppMessage, 1)
struct NetAppMessageHeader {
	NetAppMessageType type;
};
#pragma pack(pop, netAppMessage)

enum NetTerminationReason
{
	NTRTimeout,
	NTRDisconnected,
	NTRKicked,
	NTRBanned,
	NTRSessionLocked,
	NTROther, 
};

__forceinline NetMsgFlags operator|(NetMsgFlags a, NetMsgFlags b)
{
	return NetMsgFlags((__int32)a | (__int32)b);
}

struct SendCompleteInfo {
	SendCompleteInfo() : msgID(0), ok(false) {}

	DWORD msgID;
	bool ok;
};
struct CreatePlayerInfo {
	CreatePlayerInfo() : player(0), botClient(false), inaddr(0)
	{
		name[0] = 0;
	}

	__int32 player;
	bool botClient;
	unsigned long inaddr;
	char name[40];
};
struct DeletePlayerInfo {
	DeletePlayerInfo() : player(0) {}

	__int32 player;
};
typedef void UserMessageClientCallback(char* buffer, __int32 bufferSize, void* context);
typedef void UserMessageServerCallback(__int32 from, char* buffer, __int32 bufferSize, void* context);
typedef void SendCompleteCallback(DWORD msgID, bool ok, void* context);
typedef void CreatePlayerCallback(__int32 player, bool botClient, const char* name, unsigned long inaddr, void* context);
typedef void DeletePlayerCallback(__int32 player, void* context);

class ParamEntry;
typedef bool CancelNNCallback();

class NetTranspClient
{
public:
	
	NetTranspClient() {}
	
	virtual ~NetTranspClient() {}

	virtual ConnectResult Init(
		std::string address, std::string password, bool botClient, unsigned short& port,
		std::string player, CancelNNCallback* cancelNNCallback = NULL) = 0;

	// Init starts an attempt; poll without blocking until the result is not CRNone.
	virtual ConnectResult PollInit() = 0;

	virtual Ref<NetMessage> SendMsg(BYTE* buffer, __int32 bufferSize, DWORD& msgID, NetMsgFlags flags, const Ref<NetMessage>& dependOn) = 0;
	virtual void GetSendQueueInfo(__int32& nMsg, __int32& nBytes, __int32& nMsgG, __int32& nBytesG) = 0;
	virtual bool GetConnectionInfo(__int32& latencyMS, __int32& throughputBPS) = 0;
    virtual void GetTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const = 0;
	virtual bool GetConnectionInfoRaw(__int32& latencyMS, __int32& throughputBPS)
	{
		return GetConnectionInfo(latencyMS, throughputBPS);
	}

	virtual bool GetLocalAddress(in_addr& addr) const { return false; }
	virtual bool GetLocalAddress(in_addr& addr, __int32& port) const { return false; }
	virtual bool GetDistantAddress(in_addr& addr, __int32& port) const { return false; }

	virtual bool GetServerAddress(sockaddr_in& addr) const { return false; }

	virtual bool IsSessionTerminated() = 0;
	virtual NetTerminationReason GetWhySessionTerminated() = 0;
	virtual std::string GetWhySessionTerminatedStr() = 0;

	virtual void ProcessUserMessages(UserMessageClientCallback* callback, void* context) = 0;
	virtual void RemoveUserMessages() = 0;
	virtual void RemoveSendComplete() = 0;

	virtual unsigned FreeMemory()
	{
		return 0;
	}
};

class NetTranspServer
{
public:
	
	NetTranspServer() {}
	
	virtual ~NetTranspServer() {}

	virtual bool Init(std::string name, std::string password, __int32 port) = 0;
	
	virtual void UpdateLockedOrPassworded(bool lock, bool passworded) = 0;
	
	virtual std::string GetSessionName() = 0;
	
	virtual __int32 GetSessionPort() = 0;

	virtual Ref<NetMessage> SendMsg(__int32 to, BYTE* buffer, __int32 bufferSize, DWORD& msgID, NetMsgFlags flags, const Ref<NetMessage>& dependOn) = 0;
	virtual void CancelAllMessages() = 0;
	virtual void GetSendQueueInfo(__int32 to, __int32& nMsg, __int32& nBytes, __int32& nMsgG, __int32& nBytesG) = 0;
	virtual bool GetConnectionInfo(__int32 to, __int32& latencyMS, __int32& throughputBPS) = 0;
    virtual void GetTrafficTotals(unsigned __int64& incoming, unsigned __int64& outgoing) const = 0;
	virtual bool GetConnectionInfoRaw(__int32 to, __int32& latencyMS, __int32& throughputBPS)
	{
		return GetConnectionInfo(to, latencyMS, throughputBPS);
	}
	virtual void GetConnectionLimits(__int32& maxBandwidthPerClient)
	{
		maxBandwidthPerClient = INT_MAX;
	}
	
	virtual void UpdateSessionDescription(__int32 state) = 0;
	virtual void KickOff(__int32 player, NetTerminationReason reason, const char* reasonStr = NULL) = 0;
	
	virtual bool GetURL(char* address, DWORD addressLen) = 0;

	virtual bool GetServerAddress(sockaddr_in& address) = 0;
	virtual bool GetClientAddress(__int32 client, sockaddr_in& address) = 0;

	virtual void ProcessUserMessages(UserMessageServerCallback* callback, void* context) = 0;
	virtual void RemoveUserMessages() = 0;
	virtual void RemoveSendComplete() = 0;
	virtual void ProcessPlayers(CreatePlayerCallback* callbackCreate, DeletePlayerCallback* callbackDelete, void* context) = 0;
	virtual void RemovePlayers() = 0;

	virtual unsigned FreeMemory()
	{
		return 0;
	}
};



NetTranspClient* CreateNetClient();

NetTranspServer* CreateNetServer();

void enterNN();
void leaveNN();

#endif
