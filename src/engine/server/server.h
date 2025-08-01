/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_SERVER_H
#define ENGINE_SERVER_SERVER_H

#include <base/hash.h>
#include <base/math.h>

#include <engine/engine.h>
#include <engine/server.h>

#include <engine/map.h>
#include <engine/server/register.h>
#include <engine/shared/console.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/fifo.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/protocol.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/uuid_manager.h>

#include <base/tl/array.h>

#include <list>

#include "antibot.h"
#include "authmanager.h"
#include "name_ban.h"

#if defined(CONF_UPNP)
#include "upnp.h"
#endif

class CSnapIDPool
{
	enum
	{
		MAX_IDS = 16 * 1024,
	};

	class CID
	{
	public:
		short m_Next;
		short m_State; // 0 = free, 1 = allocated, 2 = timed
		int m_Timeout;
	};

	CID m_aIDs[MAX_LOBBIES][MAX_IDS];

	int m_FirstFree[MAX_LOBBIES];
	int m_FirstTimed[MAX_LOBBIES];
	int m_LastTimed[MAX_LOBBIES];
	int m_Usage[MAX_LOBBIES];
	int m_InUsage[MAX_LOBBIES];

public:
	CSnapIDPool();

	void Reset();
	void RemoveFirstTimeout(int Lobby);
	int NewID(int Lobby);
	void TimeoutIDs(int Lobby);
	void FreeID(int Lobby, int ID);
};

class CServerBan : public CNetBan
{
	class CServer *m_pServer;

	template<class T>
	int BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason);

public:
	class CServer *Server() const { return m_pServer; }

	void InitServerBan(class IConsole *pConsole, class IStorage *pStorage, class CServer *pServer);

	virtual int BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason);
	virtual int BanRange(const CNetRange *pRange, int Seconds, const char *pReason);

	static void ConBanExt(class IConsole::IResult *pResult, void *pUser);
	static void ConBanRegion(class IConsole::IResult *pResult, void *pUser);
	static void ConBanRegionRange(class IConsole::IResult *pResult, void *pUser);
};

class CServer : public IServer
{
	class IGameServer *m_pGameServer;
	class CConfig *m_pConfig;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;
	class IEngineAntibot *m_pAntibot;

#if defined(CONF_UPNP)
	CUPnP m_UPnP;
#endif

#if defined(CONF_FAMILY_UNIX)
	UNIXSOCKETADDR m_ConnLoggingDestAddr;
	bool m_ConnLoggingSocketCreated;
	UNIXSOCKET m_ConnLoggingSocket;
#endif

	class CDbConnectionPool *m_pConnectionPool;

public:
	class IGameServer *GameServer() { return m_pGameServer; }
	class CConfig *Config() { return m_pConfig; }
	const CConfig *Config() const { return m_pConfig; }
	class IConsole *Console() { return m_pConsole; }
	class IStorage *Storage() { return m_pStorage; }
	class IEngineAntibot *Antibot() { return m_pAntibot; }
	class CDbConnectionPool *DbPool() { return m_pConnectionPool; }

	enum
	{
		MAX_RCONCMD_SEND = 16,
	};

	class CClient
	{
	public:
		enum
		{
			STATE_EMPTY = 0,
			STATE_PREAUTH,
			STATE_AUTH,
			STATE_CONNECTING,
			STATE_READY,
			STATE_INGAME,
			STATE_BOT,

			SNAPRATE_INIT = 0,
			SNAPRATE_FULL,
			SNAPRATE_RECOVER,

			DNSBL_STATE_NONE = 0,
			DNSBL_STATE_PENDING,
			DNSBL_STATE_BLACKLISTED,
			DNSBL_STATE_WHITELISTED,
		};

		class CInput
		{
		public:
			int m_aData[MAX_INPUT_SIZE];
			int m_GameTick; // the tick that was chosen for the input
			int m_AckedTick; // the tick that client last received
		};

		// connection state info
		int m_State;
		int m_Latency;
		int m_SnapRate;

		float m_Traffic;
		int64_t m_TrafficSince;

		int m_LastAckedSnapshot;
		int m_LastInputTick;
		CSnapshotStorage m_Snapshots;

		CInput m_LatestInput;
		CInput m_aInputs[200]; // TODO: handle input better
		int m_CurrentInput;

		int m_Lobby;
		int m_Map;

		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		int m_Country;
		int m_Score;
		int m_Authed;
		int m_AuthKey;
		int m_AuthTries;
		int m_NextMapChunk;
		int m_Flags;
		bool m_ShowIps;

		const IConsole::CCommandInfo *m_pRconCmdToSend;

		bool m_HasPersistentData;
		void *m_pPersistentData;

		void Reset();

		// DDRace

		NETADDR m_Addr;
		bool m_GotDDNetVersionPacket;
		bool m_DDNetVersionSettled;
		int m_DDNetVersion;
		char m_aDDNetVersionStr[64];
		CUuid m_ConnectionID;

		// DNSBL
		int m_DnsblState;
		std::shared_ptr<CHostLookup> m_pDnsblLookup;

		bool m_Sixup;
	};

	CClient m_aClients[MAX_CLIENTS];
	int m_aIdMap[MAX_CLIENTS * VANILLA_MAX_CLIENTS];

	CSnapshotDelta m_SnapshotDelta;
	CSnapshotBuilder m_SnapshotBuilder;
	CSnapIDPool m_IDPool;
	CNetServer m_NetServer;
	CEcon m_Econ;
#if defined(CONF_FAMILY_UNIX)
	CFifo m_Fifo;
#endif
	CServerBan m_ServerBan;

	int64_t m_GameStartTime;
	//int m_CurrentGameTick;

	enum
	{
		UNINITIALIZED = 0,
		RUNNING = 1,
		STOPPING = 2
	};

	int m_RunServer;

	int m_RconClientID;
	int m_RconAuthLevel;
	int m_PrintCBIndex;
	char m_aShutdownReason[128];

	enum
	{
		MAP_TYPE_SIX = 0,
		MAP_TYPE_SIXUP,
		NUM_MAP_TYPES
	};


	CDemoRecorder m_aDemoRecorder[MAX_CLIENTS + 1];
	CRegister m_Register;
	CRegister m_RegSixup;
	CAuthManager m_AuthManager;

	int m_RconRestrict;

	int64_t m_ServerInfoFirstRequest;
	int m_ServerInfoNumRequests;

	char m_aErrorShutdownReason[128];

	array<CNameBan> m_aNameBans;

	CServer();
	~CServer();

	bool IsClientNameAvailable(int ClientID, const char *pNameRequest);
	bool SetClientNameImpl(int ClientID, const char *pNameRequest, bool Set);

	virtual bool WouldClientNameChange(int ClientID, const char *pNameRequest);
	virtual void SetClientName(int ClientID, const char *pName);
	virtual void SetClientClan(int ClientID, char const *pClan);
	virtual void SetClientCountry(int ClientID, int Country);
	virtual void SetClientScore(int ClientID, int Score);
	virtual void SetClientFlags(int ClientID, int Flags);

	virtual bool GetClientInput(int ClientID, int Tick, CNetObj_PlayerInput * pInput);
	virtual bool ClientReloadMap(int ClientID);

	virtual int GetBotID();
	virtual void FreeBotID(int ID);
	virtual bool IsBotID(int ID);

	void Kick(int ClientID, const char *pReason);
	void Ban(int ClientID, int Seconds, const char *pReason);

	void DemoRecorder_HandleAutoStart();
	bool DemoRecorder_IsRecording();

	//int Tick()
	int64_t TickStartTime(int Tick);
	//int TickSpeed()

	int Init();

	void SetRconCID(int ClientID);
	int GetAuthedState(int ClientID) const;
	const char *GetAuthName(int ClientID) const;
	int GetClientInfo(int ClientID, CClientInfo *pInfo) const;
	void SetClientDDNetVersion(int ClientID, int DDNetVersion);
	void GetClientAddr(int ClientID, char *pAddrStr, int Size) const;
	const char *ClientName(int ClientID) const;
	const char *ClientClan(int ClientID) const;
	int ClientCountry(int ClientID) const;
	bool ClientIngame(int ClientID) const;
	bool ClientAuthed(int ClientID) const;
	int Port() const;
	int MaxClients() const;
	int ClientCount() const;
	int DistinctClientCount() const;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID);

	void DoSnapshot();

	static int NewClientCallback(int ClientID, void *pUser, bool Sixup);
	static int NewClientNoAuthCallback(int ClientID, void *pUser);
	static int DelClientCallback(int ClientID, const char *pReason, void *pUser);

	static int ClientRejoinCallback(int ClientID, void *pUser);

	void SendRconType(int ClientID, bool UsernameReq);
	void SendCapabilities(int ClientID);
	void SendMap(int ClientID);
	void SendMapData(int ClientID, int Chunk);
	void SendConnectionReady(int ClientID);
	void SendRconLine(int ClientID, const char *pLine);
	static void SendRconLineAuthed(const char *pLine, void *pUser, ColorRGBA PrintColor = {1, 1, 1, 1});

	void SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void UpdateClientRconCommands();

	void ProcessClientPacket(CNetChunk *pPacket);

	class CCache
	{
	public:
		class CCacheChunk
		{
		public:
			CCacheChunk(const void *pData, int Size);
			CCacheChunk(const CCacheChunk &) = delete;

			int m_DataSize;
			unsigned char m_aData[NET_MAX_PAYLOAD];
		};

		std::list<CCacheChunk> m_Cache;

		CCache();
		~CCache();

		void AddChunk(const void *pData, int Size);
		void Clear();
	};
	CCache m_aServerInfoCache[3 * 2];
	CCache m_aSixupServerInfoCache[2];
	bool m_ServerInfoNeedsUpdate;

	void ExpireServerInfo();
	void CacheServerInfo(CCache *pCache, int Type, bool SendClients);
	void CacheServerInfoSixup(CCache *pCache, bool SendClients);
	void SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients);
	void GetServerInfoSixup(CPacker *pPacker, int Token, bool SendClients);
	bool RateLimitServerInfoConnless();
	void SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type);
	void UpdateServerInfo(bool Resend = false);

	void PumpNetwork(bool PacketWaiting);

	const char *GetMapName() const;
	virtual int LoadMap(const char *pMapName, int Map);

	void SaveDemo(int ClientID, float Time);
	void StartRecord(int ClientID);
	void StopRecord(int ClientID);
	bool IsRecording(int ClientID);

	void InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, CConfig *pConfig, IConsole *pConsole);
	int Run();

	static void ConTestingCommands(IConsole::IResult *pResult, void *pUser);
	static void ConKick(IConsole::IResult *pResult, void *pUser);
	static void ConStatus(IConsole::IResult *pResult, void *pUser);
	static void ConShutdown(IConsole::IResult *pResult, void *pUser);
	static void ConRecord(IConsole::IResult *pResult, void *pUser);
	static void ConStopRecord(IConsole::IResult *pResult, void *pUser);
	static void ConMapReload(IConsole::IResult *pResult, void *pUser);
	static void ConLogout(IConsole::IResult *pResult, void *pUser);
	static void ConShowIps(IConsole::IResult *pResult, void *pUser);

	static void ConAuthAdd(IConsole::IResult *pResult, void *pUser);
	static void ConAuthAddHashed(IConsole::IResult *pResult, void *pUser);
	static void ConAuthUpdate(IConsole::IResult *pResult, void *pUser);
	static void ConAuthUpdateHashed(IConsole::IResult *pResult, void *pUser);
	static void ConAuthRemove(IConsole::IResult *pResult, void *pUser);
	static void ConAuthList(IConsole::IResult *pResult, void *pUser);

	static void ConNameBan(IConsole::IResult *pResult, void *pUser);
	static void ConNameUnban(IConsole::IResult *pResult, void *pUser);
	static void ConNameBans(IConsole::IResult *pResult, void *pUser);

	// console commands for sqlmasters
	static void ConAddSqlServer(IConsole::IResult *pResult, void *pUserData);
	static void ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData);

	static void ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainCommandAccessUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	void LogoutClient(int ClientID, const char *pReason);
	void LogoutKey(int Key, const char *pReason);

	void ConchainRconPasswordChangeGeneric(int Level, const char *pCurrent, IConsole::IResult *pResult);
	static void ConchainRconPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRconModPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRconHelperPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainMapUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSixupUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

#if defined(CONF_FAMILY_UNIX)
	static void ConchainConnLoggingServerChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
#endif

	void RegisterCommands();

	virtual int SnapNewID(int Lobby);
	virtual void SnapFreeID(int Lobby, int ID);
	virtual void *SnapNewItem(int Type, int ID, int Size);
	void SnapSetStaticsize(int ItemType, int Size);

	// DDRace

	void GetClientAddr(int ClientID, NETADDR *pAddr) const;
	int m_aPrevStates[MAX_CLIENTS];
	const char *GetAnnouncementLine(char const *pFileName);
	unsigned m_AnnouncementLastLine;
	void RestrictRconOutput(int ClientID) { m_RconRestrict = ClientID; }

	virtual int *GetIdMap(int ClientID);

	void InitDnsbl(int ClientID);
	bool DnsblWhite(int ClientID)
	{
		return m_aClients[ClientID].m_DnsblState == CClient::DNSBL_STATE_NONE ||
		       m_aClients[ClientID].m_DnsblState == CClient::DNSBL_STATE_WHITELISTED;
	}
	bool DnsblPending(int ClientID)
	{
		return m_aClients[ClientID].m_DnsblState == CClient::DNSBL_STATE_PENDING;
	}
	bool DnsblBlack(int ClientID)
	{
		return m_aClients[ClientID].m_DnsblState == CClient::DNSBL_STATE_BLACKLISTED;
	}

	void AuthRemoveKey(int KeySlot);
	bool ClientPrevIngame(int ClientID) { return m_aPrevStates[ClientID] == CClient::STATE_INGAME; }
	const char *GetNetErrorString(int ClientID) { return m_NetServer.ErrorString(ClientID); }
	void ResetNetErrorString(int ClientID) { m_NetServer.ResetErrorString(ClientID); }
	bool SetTimedOut(int ClientID, int OrigID);
	void SetTimeoutProtected(int ClientID) { m_NetServer.SetTimeoutProtected(ClientID); }

	void SendMsgRaw(int ClientID, const void *pData, int Size, int Flags);

	bool ErrorShutdown() const { return m_aErrorShutdownReason[0] != 0; }
	void SetErrorShutdown(const char *pReason);

	bool IsSixup(int ClientID) const { return ClientID != SERVER_DEMO_CLIENT && m_aClients[ClientID].m_Sixup; }

#ifdef CONF_FAMILY_UNIX
	enum CONN_LOGGING_CMD
	{
		OPEN_SESSION = 1,
		CLOSE_SESSION = 2,
	};

	void SendConnLoggingCommand(CONN_LOGGING_CMD Cmd, const NETADDR *pAddr);
#endif
};

#endif
