#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETGLOBAL_H
#define _NETGLOBAL_H

#pragma pack(push, netGlobal, 1)

#define ADDR(addr) ntohl((addr).sin_addr.S_un.S_addr)

#define IP4(addr) ((addr).sin_addr.S_un.S_un_b.s_b1)

#define IP3(addr) ((addr).sin_addr.S_un.S_un_b.s_b2)

#define IP2(addr) ((addr).sin_addr.S_un.S_un_b.s_b3)

#define IP1(addr) ((addr).sin_addr.S_un.S_un_b.s_b4)

#define PORT(addr) ntohs((addr).sin_port)

const __int32 MAX_IN_DATA = 2048;
const unsigned MAX_REASSEMBLED_USER_MESSAGE = 65536;

struct MsgHeader {
	
	unsigned short length;
	
	unsigned short flags;

	unsigned __int32 crc;
	
	unsigned __int32 serial;
	
	unsigned __int32 ackOrigin;
	union
	{
		
		unsigned __int64 ackBitmask;
		struct
		{
			
			unsigned __int32 control1;
			
			unsigned __int32 control2;
		} c;
	};
};

#define MSG_VIM_FLAG 0x8000

#define MSG_URGENT_FLAG 0x4000

#define MSG_ORDERED_FLAG 0x2000

#define MSG_FROM_BCAST_FLAG 0x1000

#define MSG_TO_BCAST_FLAG 0x0800

#define MSG_DELAY_FLAG 0x0400

#define MSG_INSTANT_FLAG 0x0200

#define MSG_BUNCH_FLAG 0x0080

#define MSG_DUMMY_FLAG 0x0040

#define MSG_PART_FLAG 0x0020

#define MSG_CLOSING_FLAG 0x0010

#define MSG_VOICE_FLAG 0x0008

#define MSG_USER_FLAGS 0x0007

#define MSG_ALL_FLAGS 0xffff

#define SHORT_ACK(flags) (((flags) & (MSG_ORDERED_FLAG | MSG_DELAY_FLAG)) != 0)

#define IP_HEADER 20
#define UDP_HEADER 8
#define TCP_HEADER 20
#define IP_UDP_HEADER 28
#define MAX_PACKET_SIZE (1400 - IP_UDP_HEADER - sizeof(MsgHeader))

enum NetStatus
{
	nsError, 
	nsOK,	 

	nsInvalidSharing, 
	nsInvalidMessage, 

	nsInputPending,	   
	nsInputReceived,   
	nsInputPartialAck, 
	nsInputAck,		   

	nsOutputPending,  
	nsOutputSent,	  
	nsOutputObsolete, 
	nsOutputTimeout,  
	nsOutputAck,	  

	nsCancel,		   
	nsNoMoreCallbacks, 
};

class NetMessage;

typedef NetStatus NetCallBack(NetMessage* msg, NetStatus event, void* data);

#pragma pack(pop, netGlobal)

#endif
