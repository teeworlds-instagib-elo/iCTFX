/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_NETWORK_H
#define ENGINE_SHARED_NETWORK_H

#include "huffman.h"
#include "ringbuffer.h"

#include <base/math.h>

#include <engine/message.h>
#include <engine/shared/protocol.h>

/*

CURRENT:
	packet header: 3 bytes
		unsigned char flags_ack; // 6bit flags, 2bit ack
			0.6:   ORNCaaAA
			0.6.5: ORNCTUAA
			0.7:   --NORCAA

		unsigned char ack; // 8 bit ack
		unsigned char num_chunks; // 8 bit chunks

		(unsigned char padding[3])	// 24 bit extra in case it's a connection less packet
									// this is to make sure that it's compatible with the
									// old protocol

	chunk header: 2-3 bytes
		unsigned char flags_size; // 2bit flags, 6 bit size
		unsigned char size_seq; // 4bit size, 4bit seq
		(unsigned char seq;) // 8bit seq, if vital flag is set
*/

enum
{
	NETFLAG_ALLOWSTATELESS = 1,
	NETSENDFLAG_VITAL = 1,
	NETSENDFLAG_CONNLESS = 2,
	NETSENDFLAG_FLUSH = 4,
	NETSENDFLAG_EXTENDED = 8,

	NETSTATE_OFFLINE = 0,
	NETSTATE_CONNECTING,
	NETSTATE_ONLINE,

	NETBANTYPE_SOFT = 1,
	NETBANTYPE_DROP = 2
};

enum
{
	NET_VERSION = 2,

	NET_MAX_PACKETSIZE = 1400,
	NET_MAX_PAYLOAD = NET_MAX_PACKETSIZE - 6,
	NET_MAX_CHUNKHEADERSIZE = 5,
	NET_PACKETHEADERSIZE = 3,
	NET_MAX_CLIENTS = 64,
	NET_MAX_CONSOLE_CLIENTS = 4,
	NET_MAX_SEQUENCE = 1 << 10,
	NET_SEQUENCE_MASK = NET_MAX_SEQUENCE - 1,

	NET_CONNSTATE_OFFLINE = 0,
	NET_CONNSTATE_CONNECT = 1,
	NET_CONNSTATE_PENDING = 2,
	NET_CONNSTATE_ONLINE = 3,
	NET_CONNSTATE_ERROR = 4,
	NET_CONNSTATE_BOT = 5,

	NET_PACKETFLAG_UNUSED = 1 << 0,
	NET_PACKETFLAG_TOKEN = 1 << 1,
	NET_PACKETFLAG_CONTROL = 1 << 2,
	NET_PACKETFLAG_CONNLESS = 1 << 3,
	NET_PACKETFLAG_RESEND = 1 << 4,
	NET_PACKETFLAG_COMPRESSION = 1 << 5,
	// NOT SENT VIA THE NETWORK DIRECTLY:
	NET_PACKETFLAG_EXTENDED = 1 << 6,

	NET_CHUNKFLAG_VITAL = 1,
	NET_CHUNKFLAG_RESEND = 2,

	NET_CTRLMSG_KEEPALIVE = 0,
	NET_CTRLMSG_CONNECT = 1,
	NET_CTRLMSG_CONNECTACCEPT = 2,
	NET_CTRLMSG_ACCEPT = 3,
	NET_CTRLMSG_CLOSE = 4,

	NET_CONN_BUFFERSIZE = 1024 * 32,

	NET_CONNLIMIT_IPS = 16,

	NET_ENUM_TERMINATOR
};

typedef int SECURITY_TOKEN;

SECURITY_TOKEN ToSecurityToken(unsigned char *pData);

extern const unsigned char SECURITY_TOKEN_MAGIC[4];

enum
{
	NET_SECURITY_TOKEN_UNKNOWN = -1,
	NET_SECURITY_TOKEN_UNSUPPORTED = 0,
};

typedef int (*NETFUNC_DELCLIENT)(int ClientID, const char *pReason, void *pUser);
typedef int (*NETFUNC_NEWCLIENT_CON)(int ClientID, void *pUser);
typedef int (*NETFUNC_NEWCLIENT)(int ClientID, void *pUser, bool Sixup);
typedef int (*NETFUNC_NEWCLIENT_NOAUTH)(int ClientID, void *pUser);
typedef int (*NETFUNC_CLIENTREJOIN)(int ClientID, void *pUser);

struct CNetChunk
{
	// -1 means that it's a stateless packet
	// 0 on the client means the server
	int m_ClientID;
	NETADDR m_Address; // only used when client_id == -1
	int m_Flags;
	int m_DataSize;
	const void *m_pData;
	// only used if the flags contain NETSENDFLAG_EXTENDED and NETSENDFLAG_CONNLESS
	unsigned char m_aExtraData[4];
};

class CNetChunkHeader
{
public:
	int m_Flags;
	int m_Size;
	int m_Sequence;

	unsigned char *Pack(unsigned char *pData, int Split = 4);
	unsigned char *Unpack(unsigned char *pData, int Split = 4);
};

class CNetChunkResend
{
public:
	int m_Flags;
	int m_DataSize;
	unsigned char *m_pData;

	int m_Sequence;
	int64_t m_LastSendTime;
	int64_t m_FirstSendTime;
};

class CNetPacketConstruct
{
public:
	int m_Flags;
	int m_Ack;
	int m_NumChunks;
	int m_DataSize;
	unsigned char m_aChunkData[NET_MAX_PAYLOAD];
	unsigned char m_aExtraData[4];
};

class CNetConnection
{
	// TODO: is this needed because this needs to be aware of
	// the ack sequencing number and is also responible for updating
	// that. this should be fixed.
	friend class CNetRecvUnpacker;

private:
	unsigned short m_Sequence;
	unsigned short m_Ack;
	unsigned short m_PeerAck;
	unsigned m_State;

	SECURITY_TOKEN m_SecurityToken;
	int m_RemoteClosed;
	bool m_BlockCloseMsg;
	bool m_UnknownSeq;

	CStaticRingBuffer<CNetChunkResend, NET_CONN_BUFFERSIZE> m_Buffer;

	int64_t m_LastUpdateTime;
	int64_t m_LastRecvTime;
	int64_t m_LastSendTime;

	char m_aErrorString[256];

	CNetPacketConstruct m_Construct;

	NETADDR m_PeerAddr;
	NETSOCKET m_Socket;
	NETSTATS m_Stats;

	//
	void ResetStats();
	void SetError(const char *pString);
	void AckChunks(int Ack);

	int QueueChunkEx(int Flags, int DataSize, const void *pData, int Sequence);
	void SendControl(int ControlMsg, const void *pExtra, int ExtraSize);
	void ResendChunk(CNetChunkResend *pResend);
	void Resend();

public:
	bool m_TimeoutProtected;
	bool m_TimeoutSituation;

	void Reset(bool Rejoin = false);
	void Init(NETSOCKET Socket, bool BlockCloseMsg);
	int Connect(NETADDR *pAddr);
	void Disconnect(const char *pReason);

	int Update();
	int Flush();

	int Feed(CNetPacketConstruct *pPacket, NETADDR *pAddr, SECURITY_TOKEN SecurityToken = NET_SECURITY_TOKEN_UNSUPPORTED);
	int QueueChunk(int Flags, int DataSize, const void *pData);

	const char *ErrorString();
	void SignalResend();
	int State() const { return m_State; }
	void SetState(int state) { m_State = state; }
	const NETADDR *PeerAddress() const { return &m_PeerAddr; }

	void ResetErrorString() { m_aErrorString[0] = 0; }
	const char *ErrorString() const { return m_aErrorString; }

	// Needed for GotProblems in NetClient
	int64_t LastRecvTime() const { return m_LastRecvTime; }
	int64_t ConnectTime() const { return m_LastUpdateTime; }

	int AckSequence() const { return m_Ack; }
	int SeqSequence() const { return m_Sequence; }
	int SecurityToken() const { return m_SecurityToken; }
	CStaticRingBuffer<CNetChunkResend, NET_CONN_BUFFERSIZE> *ResendBuffer() { return &m_Buffer; }

	void SetTimedOut(const NETADDR *pAddr, int Sequence, int Ack, SECURITY_TOKEN SecurityToken, CStaticRingBuffer<CNetChunkResend, NET_CONN_BUFFERSIZE> *pResendBuffer, bool Sixup);

	// anti spoof
	void DirectInit(NETADDR &Addr, SECURITY_TOKEN SecurityToken, SECURITY_TOKEN Token, bool Sixup);
	void SetUnknownSeq() { m_UnknownSeq = true; }
	void SetSequence(int Sequence) { m_Sequence = Sequence; }

	bool m_Sixup;
	SECURITY_TOKEN m_Token;
};

class CConsoleNetConnection
{
private:
	int m_State;

	NETADDR m_PeerAddr;
	NETSOCKET m_Socket;

	char m_aBuffer[NET_MAX_PACKETSIZE];
	int m_BufferOffset;

	char m_aErrorString[256];

	bool m_LineEndingDetected;
	char m_aLineEnding[3];

public:
	void Init(NETSOCKET Socket, const NETADDR *pAddr);
	void Disconnect(const char *pReason);

	int State() const { return m_State; }
	const NETADDR *PeerAddress() const { return &m_PeerAddr; }
	const char *ErrorString() const { return m_aErrorString; }

	void Reset();
	int Update();
	int Send(const char *pLine);
	int Recv(char *pLine, int MaxLength);
};

class CNetRecvUnpacker
{
public:
	bool m_Valid;

	NETADDR m_Addr;
	CNetConnection *m_pConnection;
	int m_CurrentChunk;
	int m_ClientID;
	CNetPacketConstruct m_Data;
	unsigned char m_aBuffer[NET_MAX_PACKETSIZE];

	CNetRecvUnpacker() { Clear(); }
	void Clear();
	void Start(const NETADDR *pAddr, CNetConnection *pConnection, int ClientID);
	int FetchChunk(CNetChunk *pChunk);
};

// server side
class CNetServer
{
	struct CSlot
	{
	public:
		CNetConnection m_Connection;
	};

	struct CSpamConn
	{
		NETADDR m_Addr;
		int64_t m_Time;
		int m_Conns;
	};

	NETADDR m_Address;
	NETSOCKET m_Socket;
	class CNetBan *m_pNetBan;
	CSlot m_aSlots[NET_MAX_CLIENTS];
	int m_MaxClients;
	int m_MaxClientsPerIP;

	NETFUNC_NEWCLIENT m_pfnNewClient;
	NETFUNC_NEWCLIENT_NOAUTH m_pfnNewClientNoAuth;
	NETFUNC_DELCLIENT m_pfnDelClient;
	NETFUNC_CLIENTREJOIN m_pfnClientRejoin;
	void *m_pUser;

	int m_NumConAttempts; // log flooding attacks
	int64_t m_TimeNumConAttempts;
	unsigned char m_aSecurityTokenSeed[16];

	// vanilla connect flood detection
	int64_t m_VConnFirst;
	int m_VConnNum;

	CSpamConn m_aSpamConns[NET_CONNLIMIT_IPS];

	CNetRecvUnpacker m_RecvUnpacker;

	void OnTokenCtrlMsg(NETADDR &Addr, int ControlMsg, const CNetPacketConstruct &Packet);
	int OnSixupCtrlMsg(NETADDR &Addr, CNetChunk *pChunk, int ControlMsg, const CNetPacketConstruct &Packet, SECURITY_TOKEN &ResponseToken, SECURITY_TOKEN Token);
	void OnPreConnMsg(NETADDR &Addr, CNetPacketConstruct &Packet);
	void OnConnCtrlMsg(NETADDR &Addr, int ClientID, int ControlMsg, const CNetPacketConstruct &Packet);
	bool ClientExists(const NETADDR &Addr) { return GetClientSlot(Addr) != -1; }
	int GetClientSlot(const NETADDR &Addr);
	void SendControl(NETADDR &Addr, int ControlMsg, const void *pExtra, int ExtraSize, SECURITY_TOKEN SecurityToken);

	int TryAcceptClient(NETADDR &Addr, SECURITY_TOKEN SecurityToken, bool VanillaAuth = false, bool Sixup = false, SECURITY_TOKEN Token = 0);
	int NumClientsWithAddr(NETADDR Addr);
	bool Connlimit(NETADDR Addr);
	void SendMsgs(NETADDR &Addr, const CMsgPacker *apMsgs[], int Num);

public:
	int SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser);
	int SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_NEWCLIENT_NOAUTH pfnNewClientNoAuth, NETFUNC_CLIENTREJOIN pfnClientRejoin, NETFUNC_DELCLIENT pfnDelClient, void *pUser);

	//
	bool Open(NETADDR BindAddr, class CNetBan *pNetBan, int MaxClients, int MaxClientsPerIP);
	int Close();

	int GetBotID();
	void FreeBotID(int ID);
	
	bool IsBotID(int ID);

	//
	int Recv(CNetChunk *pChunk, SECURITY_TOKEN *pResponseToken);
	int Send(CNetChunk *pChunk);
	int Update();

	//
	int Drop(int ClientID, const char *pReason);

	// status requests
	const NETADDR *ClientAddr(int ClientID) const { return m_aSlots[ClientID].m_Connection.PeerAddress(); }
	bool HasSecurityToken(int ClientID) const { return m_aSlots[ClientID].m_Connection.SecurityToken() != NET_SECURITY_TOKEN_UNSUPPORTED; }
	NETADDR Address() const { return m_Address; }
	NETSOCKET Socket() const { return m_Socket; }
	class CNetBan *NetBan() const { return m_pNetBan; }
	int NetType() const { return net_socket_type(m_Socket); }
	int MaxClients() const { return m_MaxClients; }

	void SendTokenSixup(NETADDR &Addr, SECURITY_TOKEN Token);
	int SendConnlessSixup(CNetChunk *pChunk, SECURITY_TOKEN ResponseToken);

	int ConnectionState(int ClientID) const { 
		if(ClientID < 0 || ClientID >= MAX_CLIENTS)
			return NET_CONNSTATE_OFFLINE;
		
		return m_aSlots[ClientID].m_Connection.State();
	}

	//
	void SetMaxClientsPerIP(int Max);
	bool SetTimedOut(int ClientID, int OrigID);
	void SetTimeoutProtected(int ClientID);

	int ResetErrorString(int ClientID);
	const char *ErrorString(int ClientID);

	// anti spoof
	SECURITY_TOKEN GetToken(const NETADDR &Addr);
	// vanilla token/gametick shouldn't be negative
	SECURITY_TOKEN GetVanillaToken(const NETADDR &Addr) { return absolute(GetToken(Addr)); }
};

class CNetConsole
{
	struct CSlot
	{
		CConsoleNetConnection m_Connection;
	};

	NETSOCKET m_Socket;
	class CNetBan *m_pNetBan;
	CSlot m_aSlots[NET_MAX_CONSOLE_CLIENTS];

	NETFUNC_NEWCLIENT_CON m_pfnNewClient;
	NETFUNC_DELCLIENT m_pfnDelClient;
	void *m_pUser;

	CNetRecvUnpacker m_RecvUnpacker;

public:
	void SetCallbacks(NETFUNC_NEWCLIENT_CON pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser);

	//
	bool Open(NETADDR BindAddr, class CNetBan *pNetBan);
	int Close();

	//
	int Recv(char *pLine, int MaxLength, int *pClientID = 0);
	int Send(int ClientID, const char *pLine);
	int Update();

	//
	int AcceptClient(NETSOCKET Socket, const NETADDR *pAddr);
	int Drop(int ClientID, const char *pReason);

	// status requests
	const NETADDR *ClientAddr(int ClientID) const { return m_aSlots[ClientID].m_Connection.PeerAddress(); }
	class CNetBan *NetBan() const { return m_pNetBan; }
};

// client side
class CNetClient
{
	CNetConnection m_Connection;
	CNetRecvUnpacker m_RecvUnpacker;

public:
	NETSOCKET m_Socket;
	// openness
	bool Open(NETADDR BindAddr);
	int Close();

	// connection state
	int Disconnect(const char *pReason);
	int Connect(NETADDR *pAddr);

	// communication
	int Recv(CNetChunk *pChunk);
	int Send(CNetChunk *pChunk);

	// pumping
	int Update();
	int Flush();

	int ResetErrorString();

	// error and state
	int NetType() const { return net_socket_type(m_Socket); }
	int State();
	int GotProblems(int64_t MaxLatency) const;
	const char *ErrorString() const;

	bool SecurityTokenUnknown() { return m_Connection.SecurityToken() == NET_SECURITY_TOKEN_UNKNOWN; }
};

// TODO: both, fix these. This feels like a junk class for stuff that doesn't fit anywere
class CNetBase
{
	static IOHANDLE ms_DataLogSent;
	static IOHANDLE ms_DataLogRecv;
	static CHuffman ms_Huffman;

public:
	static void OpenLog(IOHANDLE DataLogSent, IOHANDLE DataLogRecv);
	static void CloseLog();
	static void Init();
	static int Compress(const void *pData, int DataSize, void *pOutput, int OutputSize);
	static int Decompress(const void *pData, int DataSize, void *pOutput, int OutputSize);

	static void SendControlMsg(NETSOCKET Socket, NETADDR *pAddr, int Ack, int ControlMsg, const void *pExtra, int ExtraSize, SECURITY_TOKEN SecurityToken, bool Sixup = false);
	static void SendPacketConnless(NETSOCKET Socket, NETADDR *pAddr, const void *pData, int DataSize, bool Extended, unsigned char aExtra[4]);
	static void SendPacket(NETSOCKET Socket, NETADDR *pAddr, CNetPacketConstruct *pPacket, SECURITY_TOKEN SecurityToken, bool Sixup = false, bool NoCompress = false);

	static int UnpackPacket(unsigned char *pBuffer, int Size, CNetPacketConstruct *pPacket, bool &Sixup, SECURITY_TOKEN *pSecurityToken = 0, SECURITY_TOKEN *pResponseToken = 0);

	// The backroom is ack-NET_MAX_SEQUENCE/2. Used for knowing if we acked a packet or not
	static int IsSeqInBackroom(int Seq, int Ack);
};

#endif
