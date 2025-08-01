/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#define _WIN32_WINNT 0x0501

#include "server.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/masterserver.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/fifo.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>

#include <mastersrv/mastersrv.h>

#include <game/version.h>

// DDRace
#include <engine/shared/linereader.h>
#include <vector>
#include <zlib.h>

#include "databases/connection.h"
#include "databases/connection_pool.h"
#include "register.h"

#if defined(CONF_FAMILY_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <csignal>

volatile sig_atomic_t InterruptSignaled = 0;

CSnapIDPool::CSnapIDPool()
{
	Reset();
}

void CSnapIDPool::Reset()
{
	for(int lobby = 0; lobby < MAX_LOBBIES; lobby++)
	{
		for(int i = 0; i < MAX_IDS; i++)
		{
			m_aIDs[lobby][i].m_Next = i + 1;
			m_aIDs[lobby][i].m_State = 0;
		}

		m_aIDs[lobby][MAX_IDS - 1].m_Next = -1;
		m_FirstFree[lobby] = 0;
		m_FirstTimed[lobby] = -1;
		m_LastTimed[lobby] = -1;
		m_Usage[lobby] = 0;
		m_InUsage[lobby] = 0;
	}
}

void CSnapIDPool::RemoveFirstTimeout(int Lobby)
{
	int NextTimed = m_aIDs[Lobby][m_FirstTimed[Lobby]].m_Next;

	// add it to the free list
	m_aIDs[Lobby][m_FirstTimed[Lobby]].m_Next = m_FirstFree[Lobby];
	m_aIDs[Lobby][m_FirstTimed[Lobby]].m_State = 0;
	m_FirstFree[Lobby] = m_FirstTimed[Lobby];

	// remove it from the timed list
	m_FirstTimed[Lobby] = NextTimed;
	if(m_FirstTimed[Lobby] == -1)
		m_LastTimed[Lobby] = -1;

	m_Usage[Lobby]--;
}

int CSnapIDPool::NewID(int Lobby)
{
	int64_t Now = time_get();

	// process timed ids
	while(m_FirstTimed[Lobby] != -1 && m_aIDs[Lobby][m_FirstTimed[Lobby]].m_Timeout < Now)
		RemoveFirstTimeout(Lobby);

	int ID = m_FirstFree[Lobby];
	if(ID == -1)
	{
		dbg_msg("server", "invalid id");
		return ID;
	}
	m_FirstFree[Lobby] = m_aIDs[Lobby][m_FirstFree[Lobby]].m_Next;
	m_aIDs[Lobby][ID].m_State = 1;
	m_Usage[Lobby]++;
	m_InUsage[Lobby]++;
	return ID;
}

void CSnapIDPool::TimeoutIDs(int Lobby)
{
	// process timed ids
	while(m_FirstTimed[Lobby] != -1)
		RemoveFirstTimeout(Lobby);
}

void CSnapIDPool::FreeID(int Lobby, int ID)
{
	if(ID < 0)
		return;
	dbg_assert(m_aIDs[Lobby][ID].m_State == 1, "id is not allocated");

	m_InUsage[Lobby]--;
	m_aIDs[Lobby][ID].m_State = 2;
	m_aIDs[Lobby][ID].m_Timeout = time_get() + time_freq() * 5;
	m_aIDs[Lobby][ID].m_Next = -1;

	if(m_LastTimed[Lobby] != -1)
	{
		m_aIDs[Lobby][m_LastTimed[Lobby]].m_Next = ID;
		m_LastTimed[Lobby] = ID;
	}
	else
	{
		m_FirstTimed[Lobby] = ID;
		m_LastTimed[Lobby] = ID;
	}
}

void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer *pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s[ip|id] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanExt, this, "Ban player with ip/client id for x minutes for any reason");
	Console()->Register("ban_region", "s[region] s[ip|id] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanRegion, this, "Ban player in a region");
	Console()->Register("ban_region_range", "s[region] s[first ip] s[last ip] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanRegionRange, this, "Ban range in a region");
}

template<class T>
int CServerBan::BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason)
{
	// validate address
	if(Server()->m_RconClientID >= 0 && Server()->m_RconClientID < MAX_CLIENTS &&
		Server()->m_aClients[Server()->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientID)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(i == Server()->m_RconClientID || Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if(Server()->m_RconClientID == IServer::RCON_CID_VOTE)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed != AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}

	int Result = Ban(pBanPool, pData, Seconds, pReason);
	if(Result != 0)
		return Result;

	// drop banned clients
	typename T::CDataType Data = *pData;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			continue;

		if(NetMatch(&Data, Server()->m_NetServer.ClientAddr(i)))
		{
			CNetHash NetHash(&Data);
			char aBuf[256];
			MakeBanInfo(pBanPool->Find(&Data, &NetHash), aBuf, sizeof(aBuf), MSGTYPE_PLAYER);
			Server()->m_NetServer.Drop(i, aBuf);
		}
	}

	return Result;
}

int CServerBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason)
{
	return BanExt(&m_BanAddrPool, pAddr, Seconds, pReason);
}

int CServerBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if(pRange->IsValid())
		return BanExt(&m_BanRangePool, pRange, Seconds, pReason);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

void CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments() > 1 ? clamp(pResult->GetInteger(1), 0, 525600) : 10;
	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "Follow the server rules. Type /rules into the chat.";

	if(str_isallnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->Server()->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
			pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes * 60, pReason);
	}
	else
		ConBan(pResult, pUser);
}

void CServerBan::ConBanRegion(IConsole::IResult *pResult, void *pUser)
{
	const char *pRegion = pResult->GetString(0);
	if(str_comp_nocase(pRegion, g_Config.m_SvRegionName))
		return;

	pResult->RemoveArgument(0);
	ConBanExt(pResult, pUser);
}

void CServerBan::ConBanRegionRange(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pServerBan = static_cast<CServerBan *>(pUser);

	const char *pRegion = pResult->GetString(0);
	if(str_comp_nocase(pRegion, g_Config.m_SvRegionName))
		return;

	pResult->RemoveArgument(0);
	ConBanRange(pResult, static_cast<CNetBan *>(pServerBan));
}

void CServer::CClient::Reset()
{
	// reset input
	for(auto &Input : m_aInputs)
		Input.m_GameTick = -1;
	m_CurrentInput = 0;
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));

	m_Snapshots.PurgeAll();
	m_LastAckedSnapshot = -1;
	m_LastInputTick = -1;
	m_SnapRate = CClient::SNAPRATE_INIT;
	// m_Score = 0;
	m_NextMapChunk = 0;
	m_Flags = 0;
	m_Lobby = 0;
}

CServer::CServer() :
	m_Register(false), m_RegSixup(true)
{
	m_pConfig = &g_Config;
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aDemoRecorder[i] = CDemoRecorder(&m_SnapshotDelta, true);
	m_aDemoRecorder[MAX_CLIENTS] = CDemoRecorder(&m_SnapshotDelta, false);

	m_TickSpeed = SERVER_TICK_SPEED;

	m_pGameServer = 0;

	m_CurrentGameTick = 0;
	m_RunServer = UNINITIALIZED;

	m_aShutdownReason[0] = 0;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	m_RconRestrict = -1;

	m_ServerInfoFirstRequest = 0;
	m_ServerInfoNumRequests = 0;
	m_ServerInfoNeedsUpdate = false;

#ifdef CONF_FAMILY_UNIX
	m_ConnLoggingSocketCreated = false;
#endif

	m_pConnectionPool = new CDbConnectionPool();

	m_aErrorShutdownReason[0] = 0;

	Init();
}

CServer::~CServer()
{
	delete m_pConnectionPool;
}

bool CServer::IsClientNameAvailable(int ClientID, const char *pNameRequest)
{
	// check for empty names
	if(!pNameRequest[0])
		return false;

	// check for names starting with /, as they can be abused to make people
	// write chat commands
	if(pNameRequest[0] == '/')
		return false;

	// make sure that two clients don't have the same name
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i != ClientID && m_aClients[i].m_State >= CClient::STATE_READY)
		{
			if(str_utf8_comp_confusable(pNameRequest, m_aClients[i].m_aName) == 0)
				return false;
		}
	}

	return true;
}

bool CServer::SetClientNameImpl(int ClientID, const char *pNameRequest, bool Set)
{
	dbg_assert(0 <= ClientID && ClientID < MAX_CLIENTS, "invalid client id");
	if(m_aClients[ClientID].m_State < CClient::STATE_READY)
		return false;

	CNameBan *pBanned = IsNameBanned(pNameRequest, m_aNameBans.base_ptr(), m_aNameBans.size());
	if(pBanned)
	{
		if(m_aClients[ClientID].m_State == CClient::STATE_READY && Set)
		{
			char aBuf[256];
			if(pBanned->m_aReason[0])
			{
				str_format(aBuf, sizeof(aBuf), "Kicked (your name is banned: %s)", pBanned->m_aReason);
			}
			else
			{
				str_copy(aBuf, "Kicked (your name is banned)", sizeof(aBuf));
			}
			Kick(ClientID, aBuf);
		}
		return false;
	}

	// trim the name
	char aTrimmedName[MAX_NAME_LENGTH];
	str_copy(aTrimmedName, str_utf8_skip_whitespaces(pNameRequest), sizeof(aTrimmedName));
	str_utf8_trim_right(aTrimmedName);

	char aNameTry[MAX_NAME_LENGTH];
	str_copy(aNameTry, aTrimmedName, sizeof(aNameTry));

	if(!IsClientNameAvailable(ClientID, aNameTry))
	{
		// auto rename
		for(int i = 1;; i++)
		{
			str_format(aNameTry, sizeof(aNameTry), "(%d)%s", i, aTrimmedName);
			if(IsClientNameAvailable(ClientID, aNameTry))
				break;
		}
	}

	bool Changed = str_comp(m_aClients[ClientID].m_aName, aNameTry) != 0;

	if(Set)
	{
		// set the client name
		str_copy(m_aClients[ClientID].m_aName, aNameTry, MAX_NAME_LENGTH);
	}

	return Changed;
}

bool CServer::WouldClientNameChange(int ClientID, const char *pNameRequest)
{
	return SetClientNameImpl(ClientID, pNameRequest, false);
}

void CServer::SetClientName(int ClientID, const char *pName)
{
	SetClientNameImpl(ClientID, pName, true);
}

void CServer::SetClientClan(int ClientID, const char *pClan)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY || !pClan)
		return;

	str_copy(m_aClients[ClientID].m_aClan, pClan, MAX_CLAN_LENGTH);
}

void CServer::SetClientCountry(int ClientID, int Country)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_Country = Country;
}

void CServer::SetClientScore(int ClientID, int Score)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY
			|| m_aClients[ClientID].m_State == CClient::STATE_BOT)
		return;

	if(m_aClients[ClientID].m_Score != Score)
		ExpireServerInfo();

	m_aClients[ClientID].m_Score = Score;
}

void CServer::SetClientFlags(int ClientID, int Flags)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY
		|| m_aClients[ClientID].m_State == CClient::STATE_BOT)
		return;

	if(Flags > m_aClients[ClientID].m_Flags)
		m_aClients[ClientID].m_Flags = Flags;
}

void CServer::Kick(int ClientID, const char *pReason)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if(m_RconClientID == ClientID)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
		return;
	}
	else if(m_aClients[ClientID].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
		return;
	}

	m_NetServer.Drop(ClientID, pReason);
}

void CServer::Ban(int ClientID, int Seconds, const char *pReason)
{
	NETADDR Addr;
	GetClientAddr(ClientID, &Addr);
	m_NetServer.NetBan()->BanAddr(&Addr, Seconds, pReason);
}

/*int CServer::Tick()
{
	return m_CurrentGameTick;
}*/

int64_t CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq() * Tick) / SERVER_TICK_SPEED;
}

/*int CServer::TickSpeed()
{
	return SERVER_TICK_SPEED;
}*/

int CServer::Init()
{
	for(auto &Client : m_aClients)
	{
		Client.m_State = CClient::STATE_EMPTY;
		Client.m_aName[0] = 0;
		Client.m_aClan[0] = 0;
		Client.m_Country = -1;
		Client.m_Snapshots.Init();
		Client.m_Traffic = 0;
		Client.m_TrafficSince = 0;
		Client.m_ShowIps = false;
		Client.m_AuthKey = -1;
		Client.m_Latency = 0;
		Client.m_Sixup = false;
		Client.m_Lobby = 0;
	}

	m_CurrentGameTick = 0;

	m_AnnouncementLastLine = 0;
	memset(m_aPrevStates, CClient::STATE_EMPTY, MAX_CLIENTS * sizeof(int));

	return 0;
}

void CServer::SetRconCID(int ClientID)
{
	m_RconClientID = ClientID;
}

int CServer::GetAuthedState(int ClientID) const
{
	return m_aClients[ClientID].m_Authed;
}

const char *CServer::GetAuthName(int ClientID) const
{
	int Key = m_aClients[ClientID].m_AuthKey;
	if(Key == -1)
	{
		return 0;
	}
	return m_AuthManager.KeyIdent(Key);
}

int CServer::GetClientInfo(int ClientID, CClientInfo *pInfo) const
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");
	dbg_assert(pInfo != 0, "info can not be null");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_pName = m_aClients[ClientID].m_aName;
		pInfo->m_Latency = m_aClients[ClientID].m_Latency;
		pInfo->m_GotDDNetVersion = m_aClients[ClientID].m_DDNetVersionSettled;
		pInfo->m_DDNetVersion = m_aClients[ClientID].m_DDNetVersion >= 0 ? m_aClients[ClientID].m_DDNetVersion : VERSION_VANILLA;
		if(m_aClients[ClientID].m_GotDDNetVersionPacket)
		{
			pInfo->m_pConnectionID = &m_aClients[ClientID].m_ConnectionID;
			pInfo->m_pDDNetVersionStr = m_aClients[ClientID].m_aDDNetVersionStr;
		}
		else
		{
			pInfo->m_pConnectionID = 0;
			pInfo->m_pDDNetVersionStr = 0;
		}
		return 1;
	}
	return 0;
}

void CServer::SetClientDDNetVersion(int ClientID, int DDNetVersion)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		m_aClients[ClientID].m_DDNetVersion = DDNetVersion;
		m_aClients[ClientID].m_DDNetVersionSettled = true;
	}
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}

const char *CServer::ClientName(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "(invalid)";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aName;
	else
		return "(connecting)";
}

const char *CServer::ClientClan(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aClan;
	else
		return "";
}

int CServer::ClientCountry(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return -1;
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientID) const
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME;
}

bool CServer::ClientAuthed(int ClientID) const
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_Authed;
}

int CServer::Port() const
{
	return m_NetServer.Address().port;
}

int CServer::MaxClients() const
{
	return m_NetServer.MaxClients();
}

int CServer::ClientCount() const
{
	int ClientCount = 0;
	for(const auto &Client : m_aClients)
	{
		if(Client.m_State != CClient::STATE_EMPTY)
		{
			ClientCount++;
		}
	}

	return ClientCount;
}

int CServer::DistinctClientCount() const
{
	NETADDR aAddresses[MAX_CLIENTS];
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_State != CClient::STATE_BOT)
		{
			GetClientAddr(i, &aAddresses[i]);
		}
	}

	int ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_State != CClient::STATE_BOT)
		{
			ClientCount++;
			for(int j = 0; j < i; j++)
			{
				if(!net_addr_comp_noport(&aAddresses[i], &aAddresses[j]))
				{
					ClientCount--;
					break;
				}
			}
		}
	}

	return ClientCount;
}

static inline bool RepackMsg(const CMsgPacker *pMsg, CPacker &Packer, bool Sixup)
{
	int MsgId = pMsg->m_MsgID;
	Packer.Reset();

	if(Sixup && !pMsg->m_NoTranslate)
	{
		if(pMsg->m_System)
		{
			if(MsgId >= OFFSET_UUID)
				;
			else if(MsgId >= NETMSG_MAP_CHANGE && MsgId <= NETMSG_MAP_DATA)
				;
			else if(MsgId >= NETMSG_CON_READY && MsgId <= NETMSG_INPUTTIMING)
				MsgId += 1;
			else if(MsgId == NETMSG_RCON_LINE)
				MsgId = 13;
			else if(MsgId >= NETMSG_AUTH_CHALLANGE && MsgId <= NETMSG_AUTH_RESULT)
				MsgId += 4;
			else if(MsgId >= NETMSG_PING && MsgId <= NETMSG_ERROR)
				MsgId += 4;
			else if(MsgId >= NETMSG_RCON_CMD_ADD && MsgId <= NETMSG_RCON_CMD_REM)
				MsgId -= 11;
			else
			{
				dbg_msg("net", "DROP send sys %d", MsgId);
				return true;
			}
		}
		else
		{
			if(MsgId >= 0 && MsgId < OFFSET_UUID)
				MsgId = Msg_SixToSeven(MsgId);

			if(MsgId < 0)
				return true;
		}
	}

	if(MsgId < OFFSET_UUID)
	{
		Packer.AddInt((MsgId << 1) | (pMsg->m_System ? 1 : 0));
	}
	else
	{
		Packer.AddInt((0 << 1) | (pMsg->m_System ? 1 : 0)); // NETMSG_EX, NETMSGTYPE_EX
		g_UuidManager.PackUuid(MsgId, &Packer);
	}
	Packer.AddRaw(pMsg->Data(), pMsg->Size());

	return false;
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID)
{
	CNetChunk Packet;
	if(!pMsg)
		return -1;

	mem_zero(&Packet, sizeof(CNetChunk));
	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(ClientID < 0)
	{
		CPacker Pack6, Pack7;
		if(RepackMsg(pMsg, Pack6, false))
			return -1;
		if(RepackMsg(pMsg, Pack7, true))
			return -1;

		// write message to demo recorder
		if(!(Flags & MSGFLAG_NORECORD))
			m_aDemoRecorder[MAX_CLIENTS].RecordMessage(Pack6.Data(), Pack6.Size());

		if(!(Flags & MSGFLAG_NOSEND))
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(m_aClients[i].m_State == CClient::STATE_INGAME)
				{
					CPacker *pPack = m_aClients[i].m_Sixup ? &Pack7 : &Pack6;
					Packet.m_pData = pPack->Data();
					Packet.m_DataSize = pPack->Size();
					Packet.m_ClientID = i;
					if(Antibot()->OnEngineServerMessage(i, Packet.m_pData, Packet.m_DataSize, Flags))
					{
						continue;
					}
					m_NetServer.Send(&Packet);
				}
			}
		}
	}
	else
	{
		CPacker Pack;
		if(RepackMsg(pMsg, Pack, m_aClients[ClientID].m_Sixup))
			return -1;

		Packet.m_ClientID = ClientID;
		Packet.m_pData = Pack.Data();
		Packet.m_DataSize = Pack.Size();

		if(Antibot()->OnEngineServerMessage(ClientID, Packet.m_pData, Packet.m_DataSize, Flags))
		{
			return 0;
		}

		if(!(Flags & MSGFLAG_NORECORD))
		{
			m_aDemoRecorder[ClientID].RecordMessage(Pack.Data(), Pack.Size());
			m_aDemoRecorder[MAX_CLIENTS].RecordMessage(Pack.Data(), Pack.Size());
		}

		if(!(Flags & MSGFLAG_NOSEND))
			m_NetServer.Send(&Packet);
	}

	return 0;
}

void CServer::SendMsgRaw(int ClientID, const void *pData, int Size, int Flags)
{
	CNetChunk Packet;
	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientID = ClientID;
	Packet.m_pData = pData;
	Packet.m_DataSize = Size;
	Packet.m_Flags = 0;
	if(Flags & MSGFLAG_VITAL)
	{
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	}
	if(Flags & MSGFLAG_FLUSH)
	{
		Packet.m_Flags |= NETSENDFLAG_FLUSH;
	}
	m_NetServer.Send(&Packet);
}

void CServer::DoSnapshot()
{
	GameServer()->OnPreSnap();

	// create snapshot for demo recording
	if(m_aDemoRecorder[MAX_CLIENTS].IsRecording())
	{
		char aData[CSnapshot::MAX_SIZE];

		// build snap and possibly add some messages
		m_SnapshotBuilder.Init();
		GameServer()->OnSnap(-1);
		int SnapshotSize = m_SnapshotBuilder.Finish(aData);

		// write snapshot
		m_aDemoRecorder[MAX_CLIENTS].RecordSnapshot(Tick(), aData, SnapshotSize);
	}

	// create snapshots for all clients
	for(int i = 0; i < MaxClients(); i++)
	{
		// client must be ingame to receive snapshots
		if(m_aClients[i].m_State != CClient::STATE_INGAME)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick() % 50) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_INIT && (Tick() % 10) != 0)
			continue;

		{
			m_SnapshotBuilder.Init(m_aClients[i].m_Sixup);

			GameServer()->OnSnap(i);

			// finish snapshot
			char aData[CSnapshot::MAX_SIZE];
			CSnapshot *pData = (CSnapshot *)aData; // Fix compiler warning for strict-aliasing
			int SnapshotSize = m_SnapshotBuilder.Finish(pData);

			if(m_aDemoRecorder[i].IsRecording())
			{
				// write snapshot
				m_aDemoRecorder[i].RecordSnapshot(Tick(), aData, SnapshotSize);
			}

			int Crc = pData->Crc();

			// remove old snapshos
			// keep 3 seconds worth of snapshots
			m_aClients[i].m_Snapshots.PurgeUntil(m_CurrentGameTick - SERVER_TICK_SPEED * 3);

			// save it the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0);

			// find snapshot that we can perform delta against
			static CSnapshot s_EmptySnap;
			s_EmptySnap.Clear();

			int DeltaTick = -1;
			CSnapshot *pDeltashot = &s_EmptySnap;
			{
				int DeltashotSize = m_aClients[i].m_Snapshots.Get(m_aClients[i].m_LastAckedSnapshot, 0, &pDeltashot, 0);
				if(DeltashotSize >= 0)
					DeltaTick = m_aClients[i].m_LastAckedSnapshot;
				else
				{
					// no acked package found, force client to recover rate
					if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_FULL)
						m_aClients[i].m_SnapRate = CClient::SNAPRATE_RECOVER;
				}
			}

			// create delta
			m_SnapshotDelta.SetStaticsize(protocol7::NETEVENTTYPE_SOUNDWORLD, m_aClients[i].m_Sixup);
			m_SnapshotDelta.SetStaticsize(protocol7::NETEVENTTYPE_DAMAGE, m_aClients[i].m_Sixup);
			char aDeltaData[CSnapshot::MAX_SIZE];
			int DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData);

			if(DeltaSize)
			{
				// compress it
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;

				char aCompData[CSnapshot::MAX_SIZE];
				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData, sizeof(aCompData));
				int NumPackets = (SnapshotSize + MaxSize - 1) / MaxSize;

				for(int n = 0, Left = SnapshotSize; Left > 0; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if(NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY, true);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick - DeltaTick);
				SendMsg(&Msg, MSGFLAG_FLUSH, i);
			}
		}
	}

	GameServer()->OnPostSnap();
}

int CServer::ClientRejoinCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthKey = -1;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;

	pThis->m_aClients[ClientID].Reset();

	pThis->SendMap(ClientID);

	return 0;
}

int CServer::NewClientNoAuthCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	pThis->m_aClients[ClientID].m_DnsblState = CClient::DNSBL_STATE_NONE;

	pThis->m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthKey = -1;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_ShowIps = false;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;
	pThis->m_aClients[ClientID].Reset();

	pThis->SendCapabilities(ClientID);
	pThis->SendMap(ClientID);
#if defined(CONF_FAMILY_UNIX)
	pThis->SendConnLoggingCommand(OPEN_SESSION, pThis->m_NetServer.ClientAddr(ClientID));
#endif
	return 0;
}

int CServer::NewClientCallback(int ClientID, void *pUser, bool Sixup)
{
	CServer *pThis = (CServer *)pUser;
	pThis->m_aClients[ClientID].m_State = CClient::STATE_PREAUTH;
	pThis->m_aClients[ClientID].m_DnsblState = CClient::DNSBL_STATE_NONE;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthKey = -1;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_Traffic = 0;
	pThis->m_aClients[ClientID].m_TrafficSince = 0;
	pThis->m_aClients[ClientID].m_ShowIps = false;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;
	memset(&pThis->m_aClients[ClientID].m_Addr, 0, sizeof(NETADDR));
	pThis->m_aClients[ClientID].Reset();

	pThis->GameServer()->OnClientEngineJoin(ClientID, Sixup);
	pThis->Antibot()->OnEngineClientJoin(ClientID, Sixup);

	pThis->m_aClients[ClientID].m_Sixup = Sixup;

#if defined(CONF_FAMILY_UNIX)
	pThis->SendConnLoggingCommand(OPEN_SESSION, pThis->m_NetServer.ClientAddr(ClientID));
#endif
	return 0;
}

void CServer::InitDnsbl(int ClientID)
{
	NETADDR Addr = *m_NetServer.ClientAddr(ClientID);

	//TODO: support ipv6
	if(Addr.type != NETTYPE_IPV4)
		return;

	// build dnsbl host lookup
	char aBuf[256];
	if(Config()->m_SvDnsblKey[0] == '\0')
	{
		// without key
		str_format(aBuf, sizeof(aBuf), "%d.%d.%d.%d.%s", Addr.ip[3], Addr.ip[2], Addr.ip[1], Addr.ip[0], Config()->m_SvDnsblHost);
	}
	else
	{
		// with key
		str_format(aBuf, sizeof(aBuf), "%s.%d.%d.%d.%d.%s", Config()->m_SvDnsblKey, Addr.ip[3], Addr.ip[2], Addr.ip[1], Addr.ip[0], Config()->m_SvDnsblHost);
	}

	IEngine *pEngine = Kernel()->RequestInterface<IEngine>();
	pEngine->AddJob(m_aClients[ClientID].m_pDnsblLookup = std::make_shared<CHostLookup>(aBuf, NETTYPE_IPV4));
	m_aClients[ClientID].m_DnsblState = CClient::DNSBL_STATE_PENDING;
}

#ifdef CONF_FAMILY_UNIX
void CServer::SendConnLoggingCommand(CONN_LOGGING_CMD Cmd, const NETADDR *pAddr)
{
	if(!Config()->m_SvConnLoggingServer[0] || !m_ConnLoggingSocketCreated)
		return;

	// pack the data and send it
	unsigned char aData[23] = {0};
	aData[0] = Cmd;
	mem_copy(&aData[1], &pAddr->type, 4);
	mem_copy(&aData[5], pAddr->ip, 16);
	mem_copy(&aData[21], &pAddr->port, 2);

	net_unix_send(m_ConnLoggingSocket, &m_ConnLoggingDestAddr, aData, sizeof(aData));
}
#endif

int CServer::DelClientCallback(int ClientID, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=<{%s}> reason='%s'", ClientID, aAddrStr, pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

	// notify the mod about the drop
	if(pThis->m_aClients[ClientID].m_State >= CClient::STATE_READY)
		pThis->GameServer()->OnClientDrop(ClientID, pReason);

	pThis->m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthKey = -1;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_Traffic = 0;
	pThis->m_aClients[ClientID].m_TrafficSince = 0;
	pThis->m_aClients[ClientID].m_ShowIps = false;
	pThis->m_aPrevStates[ClientID] = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_Snapshots.PurgeAll();
	pThis->m_aClients[ClientID].m_Sixup = false;

	pThis->GameServer()->OnClientEngineDrop(ClientID, pReason);
	pThis->Antibot()->OnEngineClientDrop(ClientID, pReason);
#if defined(CONF_FAMILY_UNIX)
	pThis->SendConnLoggingCommand(CLOSE_SESSION, pThis->m_NetServer.ClientAddr(ClientID));
#endif
	return 0;
}

void CServer::SendRconType(int ClientID, bool UsernameReq)
{
	CMsgPacker Msg(NETMSG_RCONTYPE, true);
	Msg.AddInt(UsernameReq);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendCapabilities(int ClientID)
{
	CMsgPacker Msg(NETMSG_CAPABILITIES, true);
	Msg.AddInt(SERVERCAP_CURVERSION); // version
	int flags = SERVERCAPFLAG_DDNET | !SERVERCAPFLAG_CHATTIMEOUTCODE | SERVERCAPFLAG_ANYPLAYERFLAG | SERVERCAPFLAG_PINGEX | SERVERCAPFLAG_SYNCWEAPONINPUT;

	if(g_Config.m_SvAllowDummy)
		flags = flags | SERVERCAPFLAG_ALLOWDUMMY;

	Msg.AddInt(flags); // flags
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendMap(int ClientID)
{
	int Lobby = m_aClients[ClientID].m_Lobby;

	if(Lobby < 0)
		return;

	IMap * pMap = Kernel()->GetIMap(GameServer()->GetLobbiesMap(Lobby));
	
	if(!pMap)
		return;

	int MapType = IsSixup(ClientID) ? MAP_TYPE_SIXUP : MAP_TYPE_SIX;
	{
		CMsgPacker Msg(NETMSG_MAP_DETAILS, true);
		Msg.AddString(pMap->m_aMapName, 0);
		Msg.AddRaw(&pMap->m_aCurrentMapSha256[MapType].data, sizeof(pMap->m_aCurrentMapSha256[MapType].data));
		Msg.AddInt(pMap->m_aCurrentMapCrc[MapType]);
		Msg.AddInt(pMap->m_aCurrentMapSize[MapType]);
		SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
	{
		CMsgPacker Msg(NETMSG_MAP_CHANGE, true);
		Msg.AddString(pMap->m_aMapName, 0);
		Msg.AddInt(pMap->m_aCurrentMapCrc[MapType]);
		Msg.AddInt(pMap->m_aCurrentMapSize[MapType]);
		if(MapType == MAP_TYPE_SIXUP)
		{
			Msg.AddInt(Config()->m_SvMapWindow);
			Msg.AddInt(1024 - 128);
			Msg.AddRaw(pMap->m_aCurrentMapSha256[MapType].data, sizeof(pMap->m_aCurrentMapSha256[MapType].data));
		}
		SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
	}

	m_aClients[ClientID].m_NextMapChunk = 0;
}

void CServer::SendMapData(int ClientID, int Chunk)
{
	int Lobby = m_aClients[ClientID].m_Lobby;

	if(Lobby < 0)
		return;

	int MapType = IsSixup(ClientID) ? MAP_TYPE_SIXUP : MAP_TYPE_SIX;
	unsigned int ChunkSize = 1024 - 128;
	unsigned int Offset = Chunk * ChunkSize;
	int Last = 0;

	IMap * pMap = Kernel()->GetIMap(GameServer()->GetLobbiesMap(Lobby));

	if(!pMap)
		return;

	// drop faulty map data requests
	if(Chunk < 0 || Offset > pMap->m_aCurrentMapSize[MapType])
		return;

	if(Offset + ChunkSize >= pMap->m_aCurrentMapSize[MapType])
	{
		ChunkSize = pMap->m_aCurrentMapSize[MapType] - Offset;
		Last = 1;
	}

	CMsgPacker Msg(NETMSG_MAP_DATA, true);
	if(MapType == MAP_TYPE_SIX)
	{
		Msg.AddInt(Last);
		Msg.AddInt(pMap->m_aCurrentMapCrc[MAP_TYPE_SIX]);
		Msg.AddInt(Chunk);
		Msg.AddInt(ChunkSize);
	}
	Msg.AddRaw(&pMap->m_apCurrentMapData[MapType][Offset], ChunkSize);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);

	if(Config()->m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
	}
}

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY, true);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	Msg.AddString(pLine, 512);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconLineAuthed(const char *pLine, void *pUser, ColorRGBA PrintColor)
{
	CServer *pThis = (CServer *)pUser;
	static int s_ReentryGuard = 0;

	if(s_ReentryGuard)
		return;
	s_ReentryGuard++;

	const char *pStart = str_find(pLine, "<{");
	const char *pEnd = pStart == NULL ? NULL : str_find(pStart + 2, "}>");
	const char *pLineWithoutIps;
	char aLine[512];
	char aLineWithoutIps[512];
	aLine[0] = '\0';
	aLineWithoutIps[0] = '\0';

	if(pStart == NULL || pEnd == NULL)
	{
		pLineWithoutIps = pLine;
	}
	else
	{
		str_append(aLine, pLine, pStart - pLine + 1);
		str_append(aLine, pStart + 2, pStart - pLine + pEnd - pStart - 1);
		str_append(aLine, pEnd + 2, sizeof(aLine));

		str_append(aLineWithoutIps, pLine, pStart - pLine + 1);
		str_append(aLineWithoutIps, "XXX", sizeof(aLineWithoutIps));
		str_append(aLineWithoutIps, pEnd + 2, sizeof(aLineWithoutIps));

		pLine = aLine;
		pLineWithoutIps = aLineWithoutIps;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY && pThis->m_aClients[i].m_State != CClient::STATE_BOT &&
				pThis->m_aClients[i].m_Authed >= pThis->m_RconAuthLevel && (pThis->m_RconRestrict == -1 || pThis->m_RconRestrict == i))
			pThis->SendRconLine(i, pThis->m_aClients[i].m_ShowIps ? pLine : pLineWithoutIps);
	}

	s_ReentryGuard--;
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD, true);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM, true);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::UpdateClientRconCommands()
{
	int ClientID = Tick() % MAX_CLIENTS;

	if(m_aClients[ClientID].m_State != CClient::STATE_EMPTY && m_aClients[ClientID].m_State != CClient::STATE_BOT && m_aClients[ClientID].m_Authed)
	{
		int ConsoleAccessLevel = m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : m_aClients[ClientID].m_Authed == AUTHED_MOD ? IConsole::ACCESS_LEVEL_MOD : IConsole::ACCESS_LEVEL_HELPER;
		for(int i = 0; i < MAX_RCONCMD_SEND && m_aClients[ClientID].m_pRconCmdToSend; ++i)
		{
			SendRconCmdAdd(m_aClients[ClientID].m_pRconCmdToSend, ClientID);
			m_aClients[ClientID].m_pRconCmdToSend = m_aClients[ClientID].m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
		}
	}
}

static inline int MsgFromSixup(int Msg, bool System)
{
	if(System)
	{
		if(Msg == NETMSG_INFO)
			;
		else if(Msg >= 14 && Msg <= 15)
			Msg += 11;
		else if(Msg >= 18 && Msg <= 28)
			Msg = NETMSG_READY + Msg - 18;
		else if(Msg < OFFSET_UUID)
			return -1;
	}

	return Msg;
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	int ClientID = pPacket->m_ClientID;
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageID(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}

	if(m_aClients[ClientID].m_Sixup && (Msg = MsgFromSixup(Msg, Sys)) < 0)
	{
		return;
	}

	if(Config()->m_SvNetlimit && Msg != NETMSG_REQUEST_MAP_DATA)
	{
		int64_t Now = time_get();
		int64_t Diff = Now - m_aClients[ClientID].m_TrafficSince;
		float Alpha = Config()->m_SvNetlimitAlpha / 100.0f;
		float Limit = (float)Config()->m_SvNetlimit * 1024 / time_freq();

		if(m_aClients[ClientID].m_Traffic > Limit)
		{
			m_NetServer.NetBan()->BanAddr(&pPacket->m_Address, 600, "Stressing network");
			return;
		}
		if(Diff > 100)
		{
			m_aClients[ClientID].m_Traffic = (Alpha * ((float)pPacket->m_DataSize / Diff)) + (1.0f - Alpha) * m_aClients[ClientID].m_Traffic;
			m_aClients[ClientID].m_TrafficSince = Now;
		}
	}

	if(Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsg(&Packer, MSGFLAG_VITAL, ClientID);
	}

	if(Sys)
	{
		// system message
		if(Msg == NETMSG_CLIENTVER)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_PREAUTH)
			{
				CUuid *pConnectionID = (CUuid *)Unpacker.GetRaw(sizeof(*pConnectionID));
				int DDNetVersion = Unpacker.GetInt();
				const char *pDDNetVersionStr = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() || !str_utf8_check(pDDNetVersionStr) || DDNetVersion < 0)
				{
					return;
				}
				m_aClients[ClientID].m_ConnectionID = *pConnectionID;
				m_aClients[ClientID].m_DDNetVersion = DDNetVersion;
				str_copy(m_aClients[ClientID].m_aDDNetVersionStr, pDDNetVersionStr, sizeof(m_aClients[ClientID].m_aDDNetVersionStr));
				m_aClients[ClientID].m_DDNetVersionSettled = true;
				m_aClients[ClientID].m_GotDDNetVersionPacket = true;
				m_aClients[ClientID].m_State = CClient::STATE_AUTH;
			}
		}
		else if(Msg == NETMSG_INFO)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_PREAUTH || m_aClients[ClientID].m_State == CClient::STATE_AUTH))
			{
				const char *pVersion = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!str_utf8_check(pVersion))
				{
					return;
				}
				if(str_comp(pVersion, GameServer()->NetVersion()) != 0 && str_comp(pVersion, "0.7 802f1be60a05665f") != 0)
				{
					// wrong version
					char aReason[256];
					str_format(aReason, sizeof(aReason), "Wrong version. Server is running '%s' and client '%s'", GameServer()->NetVersion(), pVersion);
					m_NetServer.Drop(ClientID, aReason);
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!str_utf8_check(pPassword))
				{
					return;
				}
				if(Config()->m_Password[0] != 0 && str_comp(Config()->m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientID, "Wrong password");
					return;
				}

				// reserved slot
				if(ClientID >= (Config()->m_SvMaxClients - Config()->m_SvReservedSlots) && Config()->m_SvReservedSlotsPass[0] != 0 && str_comp(Config()->m_SvReservedSlotsPass, pPassword) != 0)
				{
					m_NetServer.Drop(ClientID, "This server is full");
					return;
				}

				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				SendRconType(ClientID, m_AuthManager.NumNonDefaultKeys() > 0);
				SendCapabilities(ClientID);
				SendMap(ClientID);
			}
		}
		else if(Msg == NETMSG_REQUEST_MAP_DATA)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) == 0 || m_aClients[ClientID].m_State < CClient::STATE_CONNECTING)
				return;

			if(m_aClients[ClientID].m_Sixup)
			{
				for(int i = 0; i < Config()->m_SvMapWindow; i++)
				{
					SendMapData(ClientID, m_aClients[ClientID].m_NextMapChunk++);
				}
				return;
			}

			int Chunk = Unpacker.GetInt();
			if(Chunk != m_aClients[ClientID].m_NextMapChunk || !Config()->m_SvFastDownload)
			{
				SendMapData(ClientID, Chunk);
				return;
			}

			if(Chunk == 0)
			{
				for(int i = 0; i < Config()->m_SvMapWindow; i++)
				{
					SendMapData(ClientID, i);
				}
			}
			SendMapData(ClientID, Config()->m_SvMapWindow + m_aClients[ClientID].m_NextMapChunk);
			m_aClients[ClientID].m_NextMapChunk++;
		}
		else if(Msg == NETMSG_READY)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_CONNECTING))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%d addr=<{%s}> secure=%s", ClientID, aAddrStr, m_NetServer.HasSecurityToken(ClientID) ? "yes" : "no");
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

				void *pPersistentData = 0;
				if(m_aClients[ClientID].m_HasPersistentData)
				{
					pPersistentData = m_aClients[ClientID].m_pPersistentData;
					m_aClients[ClientID].m_HasPersistentData = false;
				}
				m_aClients[ClientID].m_State = CClient::STATE_READY;
				GameServer()->OnClientConnected(ClientID, pPersistentData);
			}

			SendConnectionReady(ClientID);
		}
		else if(Msg == NETMSG_ENTERGAME)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_READY && GameServer()->IsClientReady(ClientID))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player has entered the game. ClientID=%d addr=<{%s}> sixup=%d", ClientID, aAddrStr, IsSixup(ClientID));
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_INGAME;
				if(IsSixup(ClientID))
				{
					CMsgPacker Msgp(4, true, true); //NETMSG_SERVERINFO //TODO: Import the shared protocol from 7 aswell
					GetServerInfoSixup(&Msgp, -1, false);
					SendMsg(&Msgp, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
				}
				GameServer()->OnClientEnter(ClientID);
			}
		}
		else if(Msg == NETMSG_INPUT)
		{
			CClient::CInput *pInput;
			int64_t TagTime;

			int Acked = Unpacker.GetInt();
			m_aClients[ClientID].m_LastAckedSnapshot = Acked;
			// GameServer()->
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if(Unpacker.Error() || Size / 4 > MAX_INPUT_SIZE)
				return;

			if(m_aClients[ClientID].m_LastAckedSnapshot > 0)
				m_aClients[ClientID].m_SnapRate = CClient::SNAPRATE_FULL;
			
			// GameServer()->SetPlayer_LastAckedSnapshot(ClientID, m_aClients[ClientID].m_LastAckedSnapshot);
			

			if(m_aClients[ClientID].m_Snapshots.Get(m_aClients[ClientID].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
				m_aClients[ClientID].m_Latency = (int)(((time_get() - TagTime) * 1000) / time_freq());

			// add message to report the input timing
			// skip packets that are old
			if(IntendedTick > m_aClients[ClientID].m_LastInputTick)
			{
				int TimeLeft = ((TickStartTime(IntendedTick) - time_get()) * 1000) / time_freq();

				CMsgPacker Msgp(NETMSG_INPUTTIMING, true);
				Msgp.AddInt(IntendedTick);
				Msgp.AddInt(TimeLeft);
				SendMsg(&Msgp, 0, ClientID);
			}

			m_aClients[ClientID].m_LastInputTick = IntendedTick;

			pInput = &m_aClients[ClientID].m_aInputs[IntendedTick % 200];

			if(IntendedTick <= Tick())
				IntendedTick = Tick() + 1;

			pInput->m_GameTick = IntendedTick;
			pInput->m_AckedTick = Acked;

			for(int i = 0; i < Size / 4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			mem_copy(m_aClients[ClientID].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE * sizeof(int));

			// call the mod with the fresh input data
			if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
				GameServer()->OnClientDirectInput(ClientID, m_aClients[ClientID].m_LatestInput.m_aData);
		}
		else if(Msg == NETMSG_RCON_CMD)
		{
			const char *pCmd = Unpacker.GetString();
			if(!str_utf8_check(pCmd))
			{
				return;
			}
			if(Unpacker.Error() == 0 && !str_comp(pCmd, "crashmeplx"))
			{
				int Version = m_aClients[ClientID].m_DDNetVersion;
				if(GameServer()->PlayerExists(ClientID) && Version < VERSION_DDNET_OLD)
				{
					m_aClients[ClientID].m_DDNetVersion = VERSION_DDNET_OLD;
				}
			}
			else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
			{
				if(GameServer()->PlayerExists(ClientID))
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d rcon='%s'", ClientID, pCmd);
					Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
					m_RconClientID = ClientID;
					m_RconAuthLevel = m_aClients[ClientID].m_Authed;
					Console()->SetAccessLevel(m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : m_aClients[ClientID].m_Authed == AUTHED_MOD ? IConsole::ACCESS_LEVEL_MOD : m_aClients[ClientID].m_Authed == AUTHED_HELPER ? IConsole::ACCESS_LEVEL_HELPER : IConsole::ACCESS_LEVEL_USER);
					Console()->ExecuteLineFlag(pCmd, CFGFLAG_SERVER, m_aClients[ClientID].m_Lobby, ClientID);
					Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
					m_RconClientID = IServer::RCON_CID_SERV;
					m_RconAuthLevel = AUTHED_ADMIN;
				}
			}
		}
		else if(Msg == NETMSG_RCON_AUTH)
		{
			const char *pName = "";
			if(!IsSixup(ClientID))
				pName = Unpacker.GetString(CUnpacker::SANITIZE_CC); // login name, now used
			const char *pPw = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			if(!str_utf8_check(pPw) || !str_utf8_check(pName))
			{
				return;
			}

			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0)
			{
				int AuthLevel = -1;
				int KeySlot = -1;

				if(!pName[0])
				{
					if(m_AuthManager.CheckKey((KeySlot = m_AuthManager.DefaultKey(AUTHED_ADMIN)), pPw))
						AuthLevel = AUTHED_ADMIN;
					else if(m_AuthManager.CheckKey((KeySlot = m_AuthManager.DefaultKey(AUTHED_MOD)), pPw))
						AuthLevel = AUTHED_MOD;
					else if(m_AuthManager.CheckKey((KeySlot = m_AuthManager.DefaultKey(AUTHED_HELPER)), pPw))
						AuthLevel = AUTHED_HELPER;
				}
				else
				{
					KeySlot = m_AuthManager.FindKey(pName);
					if(m_AuthManager.CheckKey(KeySlot, pPw))
						AuthLevel = m_AuthManager.KeyLevel(KeySlot);
				}

				if(AuthLevel != -1)
				{
					if(m_aClients[ClientID].m_Authed != AuthLevel)
					{
						if(!IsSixup(ClientID))
						{
							CMsgPacker Msgp(NETMSG_RCON_AUTH_STATUS, true);
							Msgp.AddInt(1); //authed
							Msgp.AddInt(1); //cmdlist
							SendMsg(&Msgp, MSGFLAG_VITAL, ClientID);
						}
						else
						{
							CMsgPacker Msgp(11, true, true); //NETMSG_RCON_AUTH_ON
							SendMsg(&Msgp, MSGFLAG_VITAL, ClientID);
						}

						m_aClients[ClientID].m_Authed = AuthLevel; // Keeping m_Authed around is unwise...
						m_aClients[ClientID].m_AuthKey = KeySlot;
						int SendRconCmds = IsSixup(ClientID) ? true : Unpacker.GetInt();
						if(Unpacker.Error() == 0 && SendRconCmds)
							// AUTHED_ADMIN - AuthLevel gets the proper IConsole::ACCESS_LEVEL_<x>
							m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(AUTHED_ADMIN - AuthLevel, CFGFLAG_SERVER);

						char aBuf[256];
						const char *pIdent = m_AuthManager.KeyIdent(KeySlot);
						switch(AuthLevel)
						{
						case AUTHED_ADMIN:
						{
							SendRconLine(ClientID, "Admin authentication successful. Full remote console access granted.");
							str_format(aBuf, sizeof(aBuf), "ClientID=%d authed with key=%s (admin)", ClientID, pIdent);
							break;
						}
						case AUTHED_MOD:
						{
							SendRconLine(ClientID, "Moderator authentication successful. Limited remote console access granted.");
							str_format(aBuf, sizeof(aBuf), "ClientID=%d authed with key=%s (moderator)", ClientID, pIdent);
							break;
						}
						case AUTHED_HELPER:
						{
							SendRconLine(ClientID, "Helper authentication successful. Limited remote console access granted.");
							str_format(aBuf, sizeof(aBuf), "ClientID=%d authed with key=%s (helper)", ClientID, pIdent);
							break;
						}
						}
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

						// DDRace
						GameServer()->OnSetAuthed(ClientID, AuthLevel);
					}
				}
				else if(Config()->m_SvRconMaxTries)
				{
					m_aClients[ClientID].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientID].m_AuthTries, Config()->m_SvRconMaxTries);
					SendRconLine(ClientID, aBuf);
					if(m_aClients[ClientID].m_AuthTries >= Config()->m_SvRconMaxTries)
					{
						if(!Config()->m_SvRconBantime)
							m_NetServer.Drop(ClientID, "Too many remote console authentication tries");
						else
							m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), Config()->m_SvRconBantime * 60, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientID, "Wrong password.");
				}
			}
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msgp(NETMSG_PING_REPLY, true);
			SendMsg(&Msgp, 0, ClientID);
		}
		else if(Msg == NETMSG_PINGEX)
		{
			CUuid *pID = (CUuid *)Unpacker.GetRaw(sizeof(*pID));
			if(Unpacker.Error())
			{
				return;
			}
			CMsgPacker Msgp(NETMSG_PONGEX, true);
			Msgp.AddRaw(pID, sizeof(*pID));
			SendMsg(&Msgp, MSGFLAG_FLUSH, ClientID);
		}
		else
		{
			if(Config()->m_Debug)
			{
				constexpr int MaxDumpedDataSize = 32;
				char aBuf[MaxDumpedDataSize * 3 + 1];
				str_hex(aBuf, sizeof(aBuf), pPacket->m_pData, minimum(pPacket->m_DataSize, MaxDumpedDataSize));

				char aBufMsg[256];
				str_format(aBufMsg, sizeof(aBufMsg), "strange message ClientID=%d msg=%d data_size=%d", ClientID, Msg, pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBufMsg);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State >= CClient::STATE_READY)
			GameServer()->OnMessage(Msg, &Unpacker, ClientID);
	}
}

bool CServer::RateLimitServerInfoConnless()
{
	bool SendClients = true;
	if(Config()->m_SvServerInfoPerSecond)
	{
		SendClients = m_ServerInfoNumRequests <= Config()->m_SvServerInfoPerSecond;
		const int64_t Now = Tick();

		if(Now <= m_ServerInfoFirstRequest + TickSpeed())
		{
			m_ServerInfoNumRequests++;
		}
		else
		{
			m_ServerInfoNumRequests = 1;
			m_ServerInfoFirstRequest = Now;
		}
	}

	return SendClients;
}

void CServer::SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type)
{
	SendServerInfo(pAddr, Token, Type, RateLimitServerInfoConnless());
}

static inline int GetCacheIndex(int Type, bool SendClient)
{
	if(Type == SERVERINFO_INGAME)
		Type = SERVERINFO_VANILLA;
	else if(Type == SERVERINFO_EXTENDED_MORE)
		Type = SERVERINFO_EXTENDED;

	return Type * 2 + SendClient;
}

CServer::CCache::CCache()
{
	m_Cache.clear();
}

CServer::CCache::~CCache()
{
	Clear();
}

CServer::CCache::CCacheChunk::CCacheChunk(const void *pData, int Size)
{
	mem_copy(m_aData, pData, Size);
	m_DataSize = Size;
}

void CServer::CCache::AddChunk(const void *pData, int Size)
{
	m_Cache.emplace_back(pData, Size);
}

void CServer::CCache::Clear()
{
	m_Cache.clear();
}

void CServer::CacheServerInfo(CCache *pCache, int Type, bool SendClients)
{
	pCache->Clear();

	// One chance to improve the protocol!
	CPacker p;
	char aBuf[128];

	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_State != CClient::STATE_BOT)
		{
			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	p.Reset();

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x) \
	do \
	{ \
		str_format(aBuf, sizeof(aBuf), "%d", x); \
		(p).AddString(aBuf, 0); \
	} while(0)

	p.AddString(GameServer()->Version(), 32);
	if(Type != SERVERINFO_VANILLA)
	{
		p.AddString(Config()->m_SvName, 256);
	}
	else
	{
		if(m_NetServer.MaxClients() <= VANILLA_MAX_CLIENTS)
		{
			p.AddString(Config()->m_SvName, 64);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s [%d/%d]", Config()->m_SvName, ClientCount, m_NetServer.MaxClients());
			p.AddString(aBuf, 64);
		}
	}
	p.AddString(GetMapName(), 32);

	IMap * pMap = Kernel()->GetIMap(0);

	if(!pMap)
		return;

	if(Type == SERVERINFO_EXTENDED)
	{
		ADD_INT(p, pMap->m_aCurrentMapCrc[MAP_TYPE_SIX]);
		ADD_INT(p, pMap->m_aCurrentMapSize[MAP_TYPE_SIX]);
	}

	// gametype
	p.AddString(GameServer()->GameType(), 16);

	// flags
	ADD_INT(p, Config()->m_Password[0] ? SERVER_FLAG_PASSWORD : 0);

	int MaxClients = m_NetServer.MaxClients();
	// How many clients the used serverinfo protocol supports, has to be tracked
	// separately to make sure we don't subtract the reserved slots from it
	int MaxClientsProtocol = MAX_CLIENTS;
	if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
	{
		if(ClientCount >= VANILLA_MAX_CLIENTS)
		{
			if(ClientCount < MaxClients)
				ClientCount = VANILLA_MAX_CLIENTS - 1;
			else
				ClientCount = VANILLA_MAX_CLIENTS;
		}
		MaxClientsProtocol = VANILLA_MAX_CLIENTS;
		if(PlayerCount > ClientCount)
			PlayerCount = ClientCount;
	}

	ADD_INT(p, PlayerCount); // num players
	ADD_INT(p, minimum(MaxClientsProtocol, maximum(MaxClients - maximum(Config()->m_SvSpectatorSlots, Config()->m_SvReservedSlots), PlayerCount))); // max players
	ADD_INT(p, ClientCount); // num clients
	ADD_INT(p, minimum(MaxClientsProtocol, maximum(MaxClients - Config()->m_SvReservedSlots, ClientCount))); // max clients

	if(Type == SERVERINFO_EXTENDED)
		p.AddString("", 0); // extra info, reserved

	const void *pPrefix = p.Data();
	int PrefixSize = p.Size();

	CPacker q;
	int ChunksStored = 0;
	int PlayersStored = 0;

#define SAVE(size) \
	do \
	{ \
		pCache->AddChunk(q.Data(), size); \
		ChunksStored++; \
	} while(0)

#define RESET() \
	do \
	{ \
		q.Reset(); \
		q.AddRaw(pPrefix, PrefixSize); \
	} while(0)

	RESET();

	if(Type == SERVERINFO_64_LEGACY)
		q.AddInt(PlayersStored); // offset

	if(!SendClients)
	{
		SAVE(q.Size());
		return;
	}

	if(Type == SERVERINFO_EXTENDED)
	{
		pPrefix = "";
		PrefixSize = 0;
	}

	int Remaining;
	switch(Type)
	{
	case SERVERINFO_EXTENDED: Remaining = -1; break;
	case SERVERINFO_64_LEGACY: Remaining = 24; break;
	case SERVERINFO_VANILLA: Remaining = VANILLA_MAX_CLIENTS; break;
	case SERVERINFO_INGAME: Remaining = VANILLA_MAX_CLIENTS; break;
	default: dbg_assert(0, "caught earlier, unreachable"); return;
	}

	// Use the following strategy for sending:
	// For vanilla, send the first 16 players.
	// For legacy 64p, send 24 players per packet.
	// For extended, send as much players as possible.

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(Remaining == 0)
			{
				if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
					break;

				// Otherwise we're SERVERINFO_64_LEGACY.
				SAVE(q.Size());
				RESET();
				q.AddInt(PlayersStored); // offset
				Remaining = 24;
			}
			if(Remaining > 0)
			{
				Remaining--;
			}

			int PreviousSize = q.Size();

			q.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
			q.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan

			ADD_INT(q, m_aClients[i].m_Country); // client country
			ADD_INT(q, m_aClients[i].m_Score); // client score
			ADD_INT(q, GameServer()->IsClientPlayer(i) ? 1 : 0); // is player?
			if(Type == SERVERINFO_EXTENDED)
				q.AddString("", 0); // extra info, reserved

			if(Type == SERVERINFO_EXTENDED)
			{
				if(q.Size() >= NET_MAX_PAYLOAD - 18) // 8 bytes for type, 10 bytes for the largest token
				{
					// Retry current player.
					i--;
					SAVE(PreviousSize);
					RESET();
					ADD_INT(q, ChunksStored);
					q.AddString("", 0); // extra info, reserved
					continue;
				}
			}
			PlayersStored++;
		}
	}

	SAVE(q.Size());
#undef SAVE
#undef RESET
#undef ADD_RAW
#undef ADD_INT
}

void CServer::CacheServerInfoSixup(CCache *pCache, bool SendClients)
{
	pCache->Clear();

	CPacker Packer;
	Packer.Reset();

	// Could be moved to a separate function and cached
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_State != CClient::STATE_BOT)
		{
			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	char aVersion[32];
	str_format(aVersion, sizeof(aVersion), "0.7↔%s", GameServer()->Version());
	Packer.AddString(aVersion, 32);
	Packer.AddString(Config()->m_SvName, 64);
	Packer.AddString(Config()->m_SvHostname, 128);
	Packer.AddString(GetMapName(), 32);

	// gametype
	Packer.AddString(GameServer()->GameType(), 16);

	// flags
	int Flags = SERVER_FLAG_TIMESCORE;
	if(Config()->m_Password[0]) // password set
		Flags |= SERVER_FLAG_PASSWORD;
	Packer.AddInt(Flags);

	int MaxClients = m_NetServer.MaxClients();
	Packer.AddInt(Config()->m_SvSkillLevel); // server skill level
	Packer.AddInt(PlayerCount); // num players
	Packer.AddInt(maximum(MaxClients - maximum(Config()->m_SvSpectatorSlots, Config()->m_SvReservedSlots), PlayerCount)); // max players
	Packer.AddInt(ClientCount); // num clients
	Packer.AddInt(maximum(MaxClients - Config()->m_SvReservedSlots, ClientCount)); // max clients

	if(SendClients)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			{
				Packer.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
				Packer.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan
				Packer.AddInt(m_aClients[i].m_Country); // client country
				Packer.AddInt(m_aClients[i].m_Score == -9999 ? -1 : -m_aClients[i].m_Score); // client score
				if(m_aClients[i].m_State != CClient::STATE_BOT)
					Packer.AddInt(GameServer()->IsClientPlayer(i) ? 0 : 1); // flag spectator=1, bot=2 (player=0)
				else
					Packer.AddInt(2); // flag spectator=1, bot=2 (player=0)
			}
		}
	}

	pCache->AddChunk(Packer.Data(), Packer.Size());
}

void CServer::SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients)
{
	CPacker p;
	char aBuf[128];
	p.Reset();

	CCache *pCache = &m_aServerInfoCache[GetCacheIndex(Type, SendClients)];

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x) \
	do \
	{ \
		str_format(aBuf, sizeof(aBuf), "%d", x); \
		(p).AddString(aBuf, 0); \
	} while(0)

	CNetChunk Packet;
	Packet.m_ClientID = -1;
	Packet.m_Address = *pAddr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;

	for(const auto &Chunk : pCache->m_Cache)
	{
		p.Reset();
		if(Type == SERVERINFO_EXTENDED)
		{
			if(&Chunk == &pCache->m_Cache.front())
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED, sizeof(SERVERBROWSE_INFO_EXTENDED));
			else
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED_MORE, sizeof(SERVERBROWSE_INFO_EXTENDED_MORE));
			ADD_INT(p, Token);
		}
		else if(Type == SERVERINFO_64_LEGACY)
		{
			ADD_RAW(p, SERVERBROWSE_INFO_64_LEGACY);
			ADD_INT(p, Token);
		}
		else if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
		{
			ADD_RAW(p, SERVERBROWSE_INFO);
			ADD_INT(p, Token);
		}
		else
		{
			dbg_assert(false, "unknown serverinfo type");
		}

		p.AddRaw(Chunk.m_aData, Chunk.m_DataSize);
		Packet.m_pData = p.Data();
		Packet.m_DataSize = p.Size();
		m_NetServer.Send(&Packet);
	}
}

void CServer::GetServerInfoSixup(CPacker *pPacker, int Token, bool SendClients)
{
	if(Token != -1)
	{
		pPacker->Reset();
		pPacker->AddRaw(SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO));
		pPacker->AddInt(Token);
	}

	SendClients = SendClients && Token != -1;

	CCache::CCacheChunk &FirstChunk = m_aSixupServerInfoCache[SendClients].m_Cache.front();
	pPacker->AddRaw(FirstChunk.m_aData, FirstChunk.m_DataSize);
}

void CServer::ExpireServerInfo()
{
	m_ServerInfoNeedsUpdate = true;
}

void CServer::UpdateServerInfo(bool Resend)
{
	if(m_RunServer == UNINITIALIZED)
		return;

	for(int i = 0; i < 3; i++)
		for(int j = 0; j < 2; j++)
			CacheServerInfo(&m_aServerInfoCache[i * 2 + j], i, j);

	for(int i = 0; i < 2; i++)
		CacheServerInfoSixup(&m_aSixupServerInfoCache[i], i);

	if(Resend)
	{
		for(int i = 0; i < MaxClients(); ++i)
		{
			if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_State != CClient::STATE_BOT)
			{
				if(!IsSixup(i))
					SendServerInfo(m_NetServer.ClientAddr(i), -1, SERVERINFO_INGAME, false);
				else
				{
					CMsgPacker Msg(4, true, true); //NETMSG_SERVERINFO //TODO: Import the shared protocol from 7 aswell
					GetServerInfoSixup(&Msg, -1, false);
					SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, i);
				}
			}
		}
	}

	m_ServerInfoNeedsUpdate = false;
}

int CServer::GetBotID()
{
	int id = m_NetServer.GetBotID();

	if(id != -1)
	{
		m_aClients[id].m_State = CClient::STATE_BOT;
		m_aClients[id].m_Country = 0;
		m_aClients[id].m_aClan[0] = 0;
	}

	return id;
}

bool CServer::IsBotID(int ID)
{
	return m_NetServer.IsBotID(ID);
}

void CServer::FreeBotID(int ID)
{
	if(m_aClients[ID].m_State == CClient::STATE_BOT)
	{
		m_aClients[ID].m_State = CClient::STATE_EMPTY;
		m_aClients[ID].m_Country = 0;
		m_aClients[ID].m_aClan[0] = 0;
	}

	m_NetServer.FreeBotID(ID);
}

void CServer::PumpNetwork(bool PacketWaiting)
{
	CNetChunk Packet;
	SECURITY_TOKEN ResponseToken;

	m_NetServer.Update();

	if(PacketWaiting)
	{
		// process packets
		while(m_NetServer.Recv(&Packet, &ResponseToken))
		{
			if(Packet.m_ClientID == -1)
			{
				// stateless
				if(!(Packet.m_Flags & NETSENDFLAG_CONNLESS))
				{
					m_RegSixup.FeedToken(Packet.m_Address, ResponseToken);
					continue;
				}

				if(ResponseToken != NET_SECURITY_TOKEN_UNKNOWN && Config()->m_SvSixup &&
					m_RegSixup.RegisterProcessPacket(&Packet, ResponseToken))
					continue;
				if(ResponseToken == NET_SECURITY_TOKEN_UNKNOWN && m_Register.RegisterProcessPacket(&Packet))
					continue;

				{
					int ExtraToken = 0;
					int Type = -1;
					if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO) + 1 &&
						mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
					{
						if(Packet.m_Flags & NETSENDFLAG_EXTENDED)
						{
							Type = SERVERINFO_EXTENDED;
							ExtraToken = (Packet.m_aExtraData[0] << 8) | Packet.m_aExtraData[1];
						}
						else
							Type = SERVERINFO_VANILLA;
					}
					else if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO_64_LEGACY) + 1 &&
						mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO_64_LEGACY, sizeof(SERVERBROWSE_GETINFO_64_LEGACY)) == 0)
					{
						Type = SERVERINFO_64_LEGACY;
					}
					if(Type == SERVERINFO_VANILLA && ResponseToken != NET_SECURITY_TOKEN_UNKNOWN && Config()->m_SvSixup)
					{
						CUnpacker Unpacker;
						Unpacker.Reset((unsigned char *)Packet.m_pData + sizeof(SERVERBROWSE_GETINFO), Packet.m_DataSize - sizeof(SERVERBROWSE_GETINFO));
						int SrvBrwsToken = Unpacker.GetInt();
						if(Unpacker.Error())
							continue;

						CPacker Packer;
						CNetChunk Response;

						GetServerInfoSixup(&Packer, SrvBrwsToken, RateLimitServerInfoConnless());

						Response.m_ClientID = -1;
						Response.m_Address = Packet.m_Address;
						Response.m_Flags = NETSENDFLAG_CONNLESS;
						Response.m_pData = Packer.Data();
						Response.m_DataSize = Packer.Size();
						m_NetServer.SendConnlessSixup(&Response, ResponseToken);
					}
					else if(Type != -1)
					{
						int Token = ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO)];
						Token |= ExtraToken << 8;
						SendServerInfoConnless(&Packet.m_Address, Token, Type);
					}
				}
			}
			else
			{
				int GameFlags = 0;
				if(Packet.m_Flags & NET_CHUNKFLAG_VITAL)
				{
					GameFlags |= MSGFLAG_VITAL;
				}
				if(Antibot()->OnEngineClientMessage(Packet.m_ClientID, Packet.m_pData, Packet.m_DataSize, GameFlags))
				{
					continue;
				}

				ProcessClientPacket(&Packet);
			}
		}
	}
	{
		unsigned char aBuffer[NET_MAX_PAYLOAD];
		int Flags;
		mem_zero(&Packet, sizeof(Packet));
		Packet.m_pData = aBuffer;
		while(Antibot()->OnEngineSimulateClientMessage(&Packet.m_ClientID, aBuffer, sizeof(aBuffer), &Packet.m_DataSize, &Flags))
		{
			Packet.m_Flags = 0;
			if(Flags & MSGFLAG_VITAL)
			{
				Packet.m_Flags |= NET_CHUNKFLAG_VITAL;
			}
			ProcessClientPacket(&Packet);
		}
	}

	m_ServerBan.Update();
	m_Econ.Update();
}

const char *CServer::GetMapName() const
{
	// get the name of the map without his path
	const char *pMapShortName = &Config()->m_SvMap[0];
	for(int i = 0; i < str_length(Config()->m_SvMap) - 1; i++)
	{
		if(Config()->m_SvMap[i] == '/' || Config()->m_SvMap[i] == '\\')
			pMapShortName = &Config()->m_SvMap[i + 1];
	}
	return pMapShortName;
}

int CServer::LoadMap(const char *pMapName, int Map)
{
	IEngineMap * pMap = Kernel()->GetIEngineMap(Map);
	if(!pMap)
		return 0;
	
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);
	GameServer()->OnMapChange(aBuf, sizeof(aBuf));

	if(!pMap->Load(aBuf))
	{
		for(int i = 0; i < NUM_MAP_TYPES; i++)
		{
			pMap->m_apCurrentMapData[i] = 0;
			pMap->m_aCurrentMapSize[i] = 0;
		}

		pMap->m_aCurrentMap[0] = '\0';
		return 0;
	}

	// reinit snapshot ids
	m_IDPool.TimeoutIDs(0);

	// get the crc of the map
	str_copy(pMap->m_aMapName, pMapName, IO_MAX_PATH_LENGTH);
	pMap->m_aCurrentMapSha256[MAP_TYPE_SIX] = pMap->Sha256();
	pMap->m_aCurrentMapCrc[MAP_TYPE_SIX] = pMap->Crc();
	char aBufMsg[256];
	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(pMap->m_aCurrentMapSha256[MAP_TYPE_SIX], aSha256, sizeof(aSha256));
	str_format(aBufMsg, sizeof(aBufMsg), "%s sha256 is %s", aBuf, aSha256);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);

	str_copy(pMap->m_aCurrentMap, pMapName, sizeof(pMap->m_aCurrentMap));

	// load complete map into memory for download
	{
		IOHANDLE File = Storage()->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);
		pMap->m_aCurrentMapSize[MAP_TYPE_SIX] = (unsigned int)io_length(File);
		free(pMap->m_apCurrentMapData[MAP_TYPE_SIX]);
		pMap->m_apCurrentMapData[MAP_TYPE_SIX] = (unsigned char *)malloc(pMap->m_aCurrentMapSize[MAP_TYPE_SIX]);
		io_read(File, pMap->m_apCurrentMapData[MAP_TYPE_SIX], pMap->m_aCurrentMapSize[MAP_TYPE_SIX]);
		io_close(File);
	}

	// load sixup version of the map
	if(Config()->m_SvSixup)
	{
		str_format(aBuf, sizeof(aBuf), "maps7/%s.map", pMapName);
		IOHANDLE File = Storage()->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);
		if(!File)
		{
			Config()->m_SvSixup = 0;
			str_format(aBufMsg, sizeof(aBufMsg), "couldn't load map %s", aBuf);
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "sixup", aBufMsg);
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "sixup", "disabling 0.7 compatibility");
		}
		else
		{
			pMap->m_aCurrentMapSize[MAP_TYPE_SIXUP] = (unsigned int)io_length(File);
			free(pMap->m_apCurrentMapData[MAP_TYPE_SIXUP]);
			pMap->m_apCurrentMapData[MAP_TYPE_SIXUP] = (unsigned char *)malloc(pMap->m_aCurrentMapSize[MAP_TYPE_SIXUP]);
			io_read(File, pMap->m_apCurrentMapData[MAP_TYPE_SIXUP], pMap->m_aCurrentMapSize[MAP_TYPE_SIXUP]);
			io_close(File);

			pMap->m_aCurrentMapSha256[MAP_TYPE_SIXUP] = sha256(pMap->m_apCurrentMapData[MAP_TYPE_SIXUP], pMap->m_aCurrentMapSize[MAP_TYPE_SIXUP]);
			pMap->m_aCurrentMapCrc[MAP_TYPE_SIXUP] = crc32(0, pMap->m_apCurrentMapData[MAP_TYPE_SIXUP], pMap->m_aCurrentMapSize[MAP_TYPE_SIXUP]);
			sha256_str(pMap->m_aCurrentMapSha256[MAP_TYPE_SIXUP], aSha256, sizeof(aSha256));
			str_format(aBufMsg, sizeof(aBufMsg), "%s sha256 is %s", aBuf, aSha256);
			Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "sixup", aBufMsg);
		}
	}
	if(!Config()->m_SvSixup)
	{
		free(pMap->m_apCurrentMapData[MAP_TYPE_SIXUP]);
		pMap->m_apCurrentMapData[MAP_TYPE_SIXUP] = 0;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aPrevStates[i] = m_aClients[i].m_State;

	return 1;
}

void CServer::InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, CConfig *pConfig, IConsole *pConsole)
{
	m_Register.Init(pNetServer, pMasterServer, pConfig, pConsole);
	m_RegSixup.Init(pNetServer, pMasterServer, pConfig, pConsole);
}

struct CMapNameItem
{
	char m_aName[IO_MAX_PATH_LENGTH - 4];

	bool operator<(const CMapNameItem &Other) const { return str_comp_nocase(m_aName, Other.m_aName) < 0; }
};

int MapScan(const char *pName, int IsDir, int DirType, void *pUserData)
{
	sorted_array<CMapNameItem> *pMapList = (sorted_array<CMapNameItem> *)pUserData;

	if(IsDir || !str_endswith(pName, ".map"))
		return 0;

	CMapNameItem Item;
	int Length = str_length(pName);
	str_truncate(Item.m_aName, sizeof(Item.m_aName), pName, Length - 4);
	pMapList->add(Item);

	return 0;
}

int CServer::Run()
{
	if(m_RunServer == UNINITIALIZED)
		m_RunServer = RUNNING;

	m_AuthManager.Init();

	if(Config()->m_Debug)
	{
		g_UuidManager.DebugDump();
	}

	m_PrintCBIndex = Console()->RegisterPrintCallback(Config()->m_ConsoleOutputLevel, SendRconLineAuthed, this);

	{
		int Size = GameServer()->PersistentClientDataSize();
		for(auto &Client : m_aClients)
		{
			Client.m_HasPersistentData = false;
			Client.m_pPersistentData = malloc(Size);
			Client.m_Map = -1;
		}
	}

	// load map

	// start server
	NETADDR BindAddr;
	int NetType = Config()->m_SvIpv4Only ? NETTYPE_IPV4 : NETTYPE_ALL;

	if(!Config()->m_Bindaddr[0] || net_host_lookup(Config()->m_Bindaddr, &BindAddr, NetType) != 0)
		mem_zero(&BindAddr, sizeof(BindAddr));

	BindAddr.type = NetType;

	int Port = Config()->m_SvPort;
	for(BindAddr.port = Port != 0 ? Port : 8303; !m_NetServer.Open(BindAddr, &m_ServerBan, Config()->m_SvMaxClients, Config()->m_SvMaxClientsPerIP); BindAddr.port++)
	{
		if(Port != 0 || BindAddr.port >= 8310)
		{
			dbg_msg("server", "couldn't open socket. port %d might already be in use", BindAddr.port);
			return -1;
		}
	}

	if(Port == 0)
		dbg_msg("server", "using port %d", BindAddr.port);

#if defined(CONF_UPNP)
	m_UPnP.Open(BindAddr);
#endif

	m_NetServer.SetCallbacks(NewClientCallback, NewClientNoAuthCallback, ClientRejoinCallback, DelClientCallback, this);

	m_Econ.Init(Config(), Console(), &m_ServerBan);

#if defined(CONF_FAMILY_UNIX)
	m_Fifo.Init(Console(), Config()->m_SvInputFifo, CFGFLAG_SERVER);
#endif

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", Config()->m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	//load all maps
	sorted_array<CMapNameItem> MapList;
	Storage()->ListDirectory(IStorage::TYPE_ALL, "maps", MapScan, &MapList);

	Kernel()->SetMapAmount(MapList.size());

	int Counter = 0;
	for(int i = 0; i < MapList.size(); i++)
	{
		char aCommand[IO_MAX_PATH_LENGTH * 2 + 10];
		char aMapEscaped[IO_MAX_PATH_LENGTH * 2];
		char *pDst = aMapEscaped;
		str_escape(&pDst, MapList[i].m_aName, aMapEscaped + sizeof(aMapEscaped));

		// maps

		if(LoadMap(aMapEscaped, Counter))
			Counter++;
	}

	Kernel()->SetMapAmount(Counter);

	Antibot()->Init();
	GameServer()->OnInit();
	if(ErrorShutdown())
	{
		m_RunServer = STOPPING;
	}
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "version " GAME_RELEASE_VERSION " on " CONF_PLATFORM_STRING " " CONF_ARCH_STRING);
	if(GIT_SHORTREV_HASH)
	{
		str_format(aBuf, sizeof(aBuf), "git revision hash: %s", GIT_SHORTREV_HASH);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	// process pending commands
	m_pConsole->StoreCommands(false);

	if(m_AuthManager.IsGenerated())
	{
		dbg_msg("server", "+-------------------------+");
		dbg_msg("server", "| rcon password: '%s' |", Config()->m_SvRconPassword);
		dbg_msg("server", "+-------------------------+");
	}

	// start game
	{
		bool NonActive = false;
		bool PacketWaiting = false;

		m_GameStartTime = time_get();

		UpdateServerInfo();
		while(m_RunServer < STOPPING)
		{
			if(NonActive)
				PumpNetwork(PacketWaiting);

			set_new_tick();

			int64_t t = time_get();
			int NewTicks = 0;

			// handle dnsbl
			if(Config()->m_SvDnsbl)
			{
				for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
				{
					if(m_aClients[ClientID].m_State == CClient::STATE_EMPTY)
						continue;

					if(m_aClients[ClientID].m_DnsblState == CClient::DNSBL_STATE_NONE)
					{
						// initiate dnsbl lookup
						InitDnsbl(ClientID);
					}
					else if(m_aClients[ClientID].m_DnsblState == CClient::DNSBL_STATE_PENDING &&
						m_aClients[ClientID].m_pDnsblLookup->Status() == IJob::STATE_DONE)
					{
						if(m_aClients[ClientID].m_pDnsblLookup->m_Result != 0)
						{
							// entry not found -> whitelisted
							m_aClients[ClientID].m_DnsblState = CClient::DNSBL_STATE_WHITELISTED;
						}
						else
						{
							// entry found -> blacklisted
							m_aClients[ClientID].m_DnsblState = CClient::DNSBL_STATE_BLACKLISTED;

							// console output
							char aAddrStr[NETADDR_MAXSTRSIZE];
							net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

							str_format(aBuf, sizeof(aBuf), "ClientID=%d addr=<{%s}> secure=%s blacklisted", ClientID, aAddrStr, m_NetServer.HasSecurityToken(ClientID) ? "yes" : "no");

							Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "dnsbl", aBuf);
						}
					}

					if(m_aClients[ClientID].m_DnsblState == CClient::DNSBL_STATE_BLACKLISTED &&
						Config()->m_SvDnsblBan)
						m_NetServer.NetBan()->BanAddr(m_NetServer.ClientAddr(ClientID), 60 * 10, "VPN detected, try connecting without. Contact admin if mistaken");
				}
			}

			while(t > TickStartTime(m_CurrentGameTick + 1))
			{
				GameServer()->OnPreTickTeehistorian();

				for(int c = 0; c < MAX_CLIENTS; c++)
					if(m_aClients[c].m_State == CClient::STATE_INGAME)
						if(m_aClients[c].m_aInputs[(Tick() + 1) % 200].m_GameTick == Tick() + 1)
							GameServer()->OnClientPredictedEarlyInput(c, m_aClients[c].m_aInputs[(Tick() + 1) % 200].m_aData);

				m_CurrentGameTick++;
				NewTicks++;

				// apply new input
				for(int c = 0; c < MAX_CLIENTS; c++)
				{
					if(m_aClients[c].m_State != CClient::STATE_INGAME)
						continue;
					for(auto &Input : m_aClients[c].m_aInputs)
					{
						if(Input.m_GameTick == Tick())
						{
							GameServer()->OnClientPredictedInput(c, Input.m_aData, Input.m_AckedTick);
							break;
						}
					}
				}

				GameServer()->OnTick();
				if(ErrorShutdown())
				{
					break;
				}
			}

			// snap game
			if(NewTicks)
			{
				if(Config()->m_SvHighBandwidth || (m_CurrentGameTick % 2) == 0)
					DoSnapshot();

				UpdateClientRconCommands();

#if defined(CONF_FAMILY_UNIX)
				m_Fifo.Update();
#endif
			}

			// master server stuff
			m_Register.RegisterUpdate(m_NetServer.NetType());
			if(Config()->m_SvSixup)
				m_RegSixup.RegisterUpdate(m_NetServer.NetType());

			if(m_ServerInfoNeedsUpdate)
				UpdateServerInfo();

			Antibot()->OnEngineTick();

			if(!NonActive)
				PumpNetwork(PacketWaiting);

			NonActive = true;

			for(auto &Client : m_aClients)
				if(Client.m_State != CClient::STATE_EMPTY && Client.m_State != CClient::STATE_BOT)
					NonActive = false;

			// wait for incoming data
			if(NonActive)
			{
				m_GameStartTime = time_get();
				m_CurrentGameTick = 0;
				m_ServerInfoFirstRequest = 0;
				if(Config()->m_SvShutdownWhenEmpty)
					m_RunServer = STOPPING;
				else
					PacketWaiting = net_socket_read_wait(m_NetServer.Socket(), 1000000);
			}
			else
			{
				set_new_tick();
				t = time_get();
				int x = (TickStartTime(m_CurrentGameTick + 1) - t) * 1000000 / time_freq() + 1;

				PacketWaiting = x > 0 ? net_socket_read_wait(m_NetServer.Socket(), x) : true;
			}
			if(InterruptSignaled)
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "interrupted");
				break;
			}
		}
	}
	const char *pDisconnectReason = "Server shutdown";
	if(m_aShutdownReason[0])
		pDisconnectReason = m_aShutdownReason;

	if(ErrorShutdown())
	{
		dbg_msg("server", "shutdown from game server (%s)", m_aErrorShutdownReason);
		pDisconnectReason = m_aErrorShutdownReason;
	}
	// disconnect all clients on shutdown
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_State != CClient::STATE_BOT)
			m_NetServer.Drop(i, pDisconnectReason);
	}

	m_Econ.Shutdown();

#if defined(CONF_FAMILY_UNIX)
	m_Fifo.Shutdown();
#endif

	GameServer()->OnShutdown();

	for(int i = 0; i < Kernel()->m_AmountMaps; i++)
	{
		IEngineMap * pMap = Kernel()->GetIEngineMap(i);

		if(!pMap)
			continue;
		
		pMap->Unload();
	}

	DbPool()->OnShutdown();

#if defined(CONF_UPNP)
	m_UPnP.Shutdown();
#endif

	for(auto &Client : m_aClients)
	{
		free(Client.m_pPersistentData);
	}

	m_NetServer.Close();

	return ErrorShutdown();
}

bool CServer::GetClientInput(int ClientID, int Tick, CNetObj_PlayerInput * pInput)
{
	if(ClientID < 0 || ClientID > MAX_CLIENTS)
		return false;
	
	if(Tick < 0)
		return false;
	
	if(m_aClients[ClientID].m_aInputs[Tick % 200].m_GameTick == Tick)
	{
		mem_copy(pInput, m_aClients[ClientID].m_aInputs[Tick % 200].m_aData, sizeof(CNetObj_PlayerInput));
		return true;
	}

	return false;
}

bool CServer::ClientReloadMap(int ClientID)
{
	bool isDummy = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == ClientID)
			continue;
		
		if(!ClientIngame(i))
			continue;

		if(m_aClients[i].m_ConnectionID != m_aClients[ClientID].m_ConnectionID)
			continue;
		
		isDummy = true;

		GameServer()->KillPlayer(ClientID);

		m_aClients[ClientID].m_Map = m_aClients[i].m_Map;
		m_aClients[ClientID].m_Lobby = m_aClients[i].m_Lobby;

		return false;
	}

	//right here
	m_aPrevStates[ClientID] = m_aClients[ClientID].m_State;
	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		m_aClients[ClientID].m_HasPersistentData = GameServer()->OnClientDataPersist(ClientID, m_aClients[ClientID].m_pPersistentData);
	}

	if(m_aClients[ClientID].m_State <= CClient::STATE_AUTH)
		return true;

	SendMap(ClientID);
	bool HasPersistentData = m_aClients[ClientID].m_HasPersistentData;
	int Lobby = m_aClients[ClientID].m_Lobby;
	m_aClients[ClientID].Reset();
	m_aClients[ClientID].m_Lobby = Lobby;
	m_aClients[ClientID].m_HasPersistentData = HasPersistentData;
	m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;

	if(ErrorShutdown())
	{
		return true;
	}
	

	return true;
}

void CServer::ConTestingCommands(CConsole::IResult *pResult, void *pUser)
{
	CConsole *pThis = static_cast<CConsole *>(pUser);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Value: %d", pThis->Config()->m_SvTestingCommands);
	pThis->Print(CConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
}

void CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	if(pResult->NumArguments() > 1)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pResult->GetString(1));
		((CServer *)pUser)->Kick(pResult->GetInteger(0), aBuf);
	}
	else
		((CServer *)pUser)->Kick(pResult->GetInteger(0), "Kicked by console");
}

void CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer *pThis = static_cast<CServer *>(pUser);
	const char *pName = pResult->NumArguments() == 1 ? pResult->GetString(0) : "";

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State == CClient::STATE_EMPTY)
			continue;

		if(!str_utf8_find_nocase(pThis->m_aClients[i].m_aName, pName))
			continue;

		net_addr_str(pThis->m_NetServer.ClientAddr(i), aAddrStr, sizeof(aAddrStr), true);
		if(pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			char aDnsblStr[64];
			aDnsblStr[0] = '\0';
			if(pThis->Config()->m_SvDnsbl)
			{
				const char *pDnsblStr = pThis->m_aClients[i].m_DnsblState == CClient::DNSBL_STATE_WHITELISTED ? "white" :
																pThis->m_aClients[i].m_DnsblState == CClient::DNSBL_STATE_BLACKLISTED ? "black" :
																									pThis->m_aClients[i].m_DnsblState == CClient::DNSBL_STATE_PENDING ? "pending" : "n/a";

				str_format(aDnsblStr, sizeof(aDnsblStr), " dnsbl=%s", pDnsblStr);
			}

			char aAuthStr[128];
			aAuthStr[0] = '\0';
			if(pThis->m_aClients[i].m_AuthKey >= 0)
			{
				const char *pAuthStr = pThis->m_aClients[i].m_Authed == AUTHED_ADMIN ? "(Admin)" :
												       pThis->m_aClients[i].m_Authed == AUTHED_MOD ? "(Mod)" :
																		     pThis->m_aClients[i].m_Authed == AUTHED_HELPER ? "(Helper)" : "";

				str_format(aAuthStr, sizeof(aAuthStr), " key=%s %s", pThis->m_AuthManager.KeyIdent(pThis->m_aClients[i].m_AuthKey), pAuthStr);
			}

			const char *pClientPrefix = "";
			if(pThis->m_aClients[i].m_Sixup)
			{
				pClientPrefix = "0.7:";
			}
			str_format(aBuf, sizeof(aBuf), "id=%d addr=<{%s}> name='%s' client=%s%d secure=%s flags=%d%s%s",
				i, aAddrStr, pThis->m_aClients[i].m_aName, pClientPrefix, pThis->m_aClients[i].m_DDNetVersion,
				pThis->m_NetServer.HasSecurityToken(i) ? "yes" : "no", pThis->m_aClients[i].m_Flags, aDnsblStr, aAuthStr);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "id=%d addr=<{%s}> connecting", i, aAddrStr);
		}
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

static int GetAuthLevel(const char *pLevel)
{
	int Level = -1;
	if(!str_comp_nocase(pLevel, "admin"))
		Level = AUTHED_ADMIN;
	else if(str_startswith(pLevel, "mod"))
		Level = AUTHED_MOD;
	else if(!str_comp_nocase(pLevel, "helper"))
		Level = AUTHED_HELPER;

	return Level;
}

void CServer::AuthRemoveKey(int KeySlot)
{
	int NewKeySlot = KeySlot;
	int OldKeySlot = m_AuthManager.RemoveKey(KeySlot);
	LogoutKey(KeySlot, "key removal");

	// Update indices.
	if(OldKeySlot != NewKeySlot)
	{
		for(auto &Client : m_aClients)
			if(Client.m_AuthKey == OldKeySlot)
				Client.m_AuthKey = NewKeySlot;
	}
}

void CServer::ConAuthAdd(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	CAuthManager *pManager = &pThis->m_AuthManager;

	const char *pIdent = pResult->GetString(0);
	const char *pLevel = pResult->GetString(1);
	const char *pPw = pResult->GetString(2);

	int Level = GetAuthLevel(pLevel);
	if(Level == -1)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "level can be one of {\"admin\", \"mod(erator)\", \"helper\"}");
		return;
	}

	bool NeedUpdate = !pManager->NumNonDefaultKeys();
	if(pManager->AddKey(pIdent, pPw, Level) < 0)
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "ident already exists");
	else
	{
		if(NeedUpdate)
			pThis->SendRconType(-1, true);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "key added");
	}
}

void CServer::ConAuthAddHashed(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	CAuthManager *pManager = &pThis->m_AuthManager;

	const char *pIdent = pResult->GetString(0);
	const char *pLevel = pResult->GetString(1);
	const char *pPw = pResult->GetString(2);
	const char *pSalt = pResult->GetString(3);

	int Level = GetAuthLevel(pLevel);
	if(Level == -1)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "level can be one of {\"admin\", \"mod(erator)\", \"helper\"}");
		return;
	}

	MD5_DIGEST Hash;
	unsigned char aSalt[SALT_BYTES];

	if(md5_from_str(&Hash, pPw))
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "Malformed password hash");
		return;
	}
	if(str_hex_decode(aSalt, sizeof(aSalt), pSalt))
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "Malformed salt hash");
		return;
	}

	bool NeedUpdate = !pManager->NumNonDefaultKeys();

	if(pManager->AddKeyHash(pIdent, Hash, aSalt, Level) < 0)
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "ident already exists");
	else
	{
		if(NeedUpdate)
			pThis->SendRconType(-1, true);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "key added");
	}
}

void CServer::ConAuthUpdate(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	CAuthManager *pManager = &pThis->m_AuthManager;

	const char *pIdent = pResult->GetString(0);
	const char *pLevel = pResult->GetString(1);
	const char *pPw = pResult->GetString(2);

	int KeySlot = pManager->FindKey(pIdent);
	if(KeySlot == -1)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "ident couldn't be found");
		return;
	}

	int Level = GetAuthLevel(pLevel);
	if(Level == -1)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "level can be one of {\"admin\", \"mod(erator)\", \"helper\"}");
		return;
	}

	pManager->UpdateKey(KeySlot, pPw, Level);
	pThis->LogoutKey(KeySlot, "key update");

	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "key updated");
}

void CServer::ConAuthUpdateHashed(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	CAuthManager *pManager = &pThis->m_AuthManager;

	const char *pIdent = pResult->GetString(0);
	const char *pLevel = pResult->GetString(1);
	const char *pPw = pResult->GetString(2);
	const char *pSalt = pResult->GetString(3);

	int KeySlot = pManager->FindKey(pIdent);
	if(KeySlot == -1)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "ident couldn't be found");
		return;
	}

	int Level = GetAuthLevel(pLevel);
	if(Level == -1)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "level can be one of {\"admin\", \"mod(erator)\", \"helper\"}");
		return;
	}

	MD5_DIGEST Hash;
	unsigned char aSalt[SALT_BYTES];

	if(md5_from_str(&Hash, pPw))
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "Malformed password hash");
		return;
	}
	if(str_hex_decode(aSalt, sizeof(aSalt), pSalt))
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "Malformed salt hash");
		return;
	}

	pManager->UpdateKeyHash(KeySlot, Hash, aSalt, Level);
	pThis->LogoutKey(KeySlot, "key update");

	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "key updated");
}

void CServer::ConAuthRemove(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	CAuthManager *pManager = &pThis->m_AuthManager;

	const char *pIdent = pResult->GetString(0);

	int KeySlot = pManager->FindKey(pIdent);
	if(KeySlot == -1)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "ident couldn't be found");
		return;
	}

	pThis->AuthRemoveKey(KeySlot);

	if(!pManager->NumNonDefaultKeys())
		pThis->SendRconType(-1, false);

	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", "key removed, all users logged out");
}

static void ListKeysCallback(const char *pIdent, int Level, void *pUser)
{
	static const char LSTRING[][10] = {"helper", "moderator", "admin"};

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s %s", pIdent, LSTRING[Level - 1]);
	((CServer *)pUser)->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "auth", aBuf);
}

void CServer::ConAuthList(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	CAuthManager *pManager = &pThis->m_AuthManager;

	pManager->ListKeys(ListKeysCallback, pThis);
}

void CServer::ConNameBan(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	char aBuf[256];
	const char *pName = pResult->GetString(0);
	const char *pReason = pResult->NumArguments() > 3 ? pResult->GetString(3) : "";
	int Distance = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : str_length(pName) / 3;
	int IsSubstring = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : 0;

	for(int i = 0; i < pThis->m_aNameBans.size(); i++)
	{
		CNameBan *pBan = &pThis->m_aNameBans[i];
		if(str_comp(pBan->m_aName, pName) == 0)
		{
			str_format(aBuf, sizeof(aBuf), "changed name='%s' distance=%d old_distance=%d is_substring=%d old_is_substring=%d reason='%s' old_reason='%s'", pName, Distance, pBan->m_Distance, IsSubstring, pBan->m_IsSubstring, pReason, pBan->m_aReason);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
			pBan->m_Distance = Distance;
			pBan->m_IsSubstring = IsSubstring;
			str_copy(pBan->m_aReason, pReason, sizeof(pBan->m_aReason));
			return;
		}
	}

	pThis->m_aNameBans.add(CNameBan(pName, Distance, IsSubstring, pReason));
	str_format(aBuf, sizeof(aBuf), "added name='%s' distance=%d is_substring=%d reason='%s'", pName, Distance, IsSubstring, pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
}

void CServer::ConNameUnban(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	const char *pName = pResult->GetString(0);

	for(int i = 0; i < pThis->m_aNameBans.size(); i++)
	{
		CNameBan *pBan = &pThis->m_aNameBans[i];
		if(str_comp(pBan->m_aName, pName) == 0)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "removed name='%s' distance=%d is_substring=%d reason='%s'", pBan->m_aName, pBan->m_Distance, pBan->m_IsSubstring, pBan->m_aReason);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
			pThis->m_aNameBans.remove_index(i);
		}
	}
}

void CServer::ConNameBans(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	for(int i = 0; i < pThis->m_aNameBans.size(); i++)
	{
		CNameBan *pBan = &pThis->m_aNameBans[i];
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "name='%s' distance=%d is_substring=%d reason='%s'", pBan->m_aName, pBan->m_Distance, pBan->m_IsSubstring, pBan->m_aReason);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
	}
}

void CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = static_cast<CServer *>(pUser);
	pThis->m_RunServer = STOPPING;
	const char *pReason = pResult->GetString(0);
	if(pReason[0])
	{
		str_copy(pThis->m_aShutdownReason, pReason, sizeof(pThis->m_aShutdownReason));
	}
}

void CServer::DemoRecorder_HandleAutoStart()
{
	/*
	if(Config()->m_SvAutoDemoRecord)
	{
		m_aDemoRecorder[MAX_CLIENTS].Stop();
		char aFilename[IO_MAX_PATH_LENGTH];
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", "auto/autorecord", aDate);
		m_aDemoRecorder[MAX_CLIENTS].Start(Storage(), m_pConsole, aFilename, GameServer()->NetVersion(), m_aCurrentMap, &m_aCurrentMapSha256[MAP_TYPE_SIX], m_aCurrentMapCrc[MAP_TYPE_SIX], "server", m_aCurrentMapSize[MAP_TYPE_SIX], m_apCurrentMapData[MAP_TYPE_SIX]);
		if(Config()->m_SvAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/server", "autorecord", ".demo", Config()->m_SvAutoDemoMax);
		}
	}
	*/
}

bool CServer::DemoRecorder_IsRecording()
{
	return m_aDemoRecorder[MAX_CLIENTS].IsRecording();
}

void CServer::SaveDemo(int ClientID, float Time)
{
	// if(IsRecording(ClientID))
	// {
	// 	m_aDemoRecorder[ClientID].Stop();

	// 	// rename the demo
	// 	char aOldFilename[IO_MAX_PATH_LENGTH];
	// 	char aNewFilename[IO_MAX_PATH_LENGTH];
	// 	str_format(aOldFilename, sizeof(aOldFilename), "demos/%s_%d_%d_tmp.demo", m_aCurrentMap, m_NetServer.Address().port, ClientID);
	// 	str_format(aNewFilename, sizeof(aNewFilename), "demos/%s_%s_%5.2f.demo", m_aCurrentMap, m_aClients[ClientID].m_aName, Time);
	// 	Storage()->RenameFile(aOldFilename, aNewFilename, IStorage::TYPE_SAVE);
	// }
}

void CServer::StartRecord(int ClientID)
{
	/*
	if(Config()->m_SvPlayerDemoRecord)
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "demos/%s_%d_%d_tmp.demo", m_aCurrentMap, m_NetServer.Address().port, ClientID);
		m_aDemoRecorder[ClientID].Start(Storage(), Console(), aFilename, GameServer()->NetVersion(), m_aCurrentMap, &m_aCurrentMapSha256[MAP_TYPE_SIX], m_aCurrentMapCrc[MAP_TYPE_SIX], "server", m_aCurrentMapSize[MAP_TYPE_SIX], m_apCurrentMapData[MAP_TYPE_SIX]);
	}
	*/
}

void CServer::StopRecord(int ClientID)
{
	// if(IsRecording(ClientID))
	// {
	// 	m_aDemoRecorder[ClientID].Stop();

	// 	char aFilename[IO_MAX_PATH_LENGTH];
	// 	str_format(aFilename, sizeof(aFilename), "demos/%s_%d_%d_tmp.demo", m_aCurrentMap, m_NetServer.Address().port, ClientID);
	// 	Storage()->RemoveFile(aFilename, IStorage::TYPE_SAVE);
	// }
}

bool CServer::IsRecording(int ClientID)
{
	return m_aDemoRecorder[ClientID].IsRecording();
}

void CServer::ConRecord(IConsole::IResult *pResult, void *pUser)
{
	/*
	CServer *pServer = (CServer *)pUser;
	char aFilename[IO_MAX_PATH_LENGTH];

	if(pResult->NumArguments())
		str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pResult->GetString(0));
	else
	{
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/demo_%s.demo", aDate);
	}
	pServer->m_aDemoRecorder[MAX_CLIENTS].Start(pServer->Storage(), pServer->Console(), aFilename, pServer->GameServer()->NetVersion(), pServer->m_aCurrentMap, &pServer->m_aCurrentMapSha256[MAP_TYPE_SIX], pServer->m_aCurrentMapCrc[MAP_TYPE_SIX], "server", pServer->m_aCurrentMapSize[MAP_TYPE_SIX], pServer->m_apCurrentMapData[MAP_TYPE_SIX]);
	*/
}

void CServer::ConStopRecord(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_aDemoRecorder[MAX_CLIENTS].Stop();
}

void CServer::ConMapReload(IConsole::IResult *pResult, void *pUser)
{
	g_Config.m_sv_grenade = 0;
	g_Config.m_sv_laser = 1;
	g_Config.m_sv_hammer = 0;

	g_Config.m_SvRollback = 1;
	g_Config.m_SvRollbackShadow = 0;
	g_Config.m_SvRunAheadDefault = 0;
	g_Config.m_SvLineOfSight = 0;
}

void CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		pServer->LogoutClient(pServer->m_RconClientID, "");
	}
}

void CServer::ConShowIps(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(pResult->NumArguments())
		{
			pServer->m_aClients[pServer->m_RconClientID].m_ShowIps = pResult->GetInteger(0);
		}
		else
		{
			char aStr[9];
			str_format(aStr, sizeof(aStr), "Value: %d", pServer->m_aClients[pServer->m_RconClientID].m_ShowIps);
			char aBuf[32];
			pServer->SendRconLine(pServer->m_RconClientID, pServer->Console()->Format(aBuf, sizeof(aBuf), "server", aStr));
		}
	}
}

void CServer::ConAddSqlServer(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	if(!pSelf->Config()->m_SvUseSQL)
		return;

	if(pResult->NumArguments() != 7 && pResult->NumArguments() != 8)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "7 or 8 arguments are required");
		return;
	}

	bool ReadOnly;
	if(str_comp_nocase(pResult->GetString(0), "w") == 0)
		ReadOnly = false;
	else if(str_comp_nocase(pResult->GetString(0), "r") == 0)
		ReadOnly = true;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return;
	}

	bool SetUpDb = pResult->NumArguments() == 8 ? pResult->GetInteger(7) : true;

	auto pMysqlConn = CreateMysqlConnection(
		pResult->GetString(1), pResult->GetString(2), pResult->GetString(3),
		pResult->GetString(4), pResult->GetString(5), pResult->GetInteger(6),
		SetUpDb);

	if(!pMysqlConn)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "can't add MySQL server: compiled without MySQL support");
		return;
	}

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"Added new Sql%sServer: DB: '%s' Prefix: '%s' User: '%s' IP: <{%s}> Port: %d",
		ReadOnly ? "Read" : "Write",
		pResult->GetString(1), pResult->GetString(2), pResult->GetString(3),
		pResult->GetString(5), pResult->GetInteger(6));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->DbPool()->RegisterDatabase(std::move(pMysqlConn), ReadOnly ? CDbConnectionPool::READ : CDbConnectionPool::WRITE);
}

void CServer::ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	if(str_comp_nocase(pResult->GetString(0), "w") == 0)
	{
		pSelf->DbPool()->Print(pSelf->Console(), CDbConnectionPool::WRITE);
		pSelf->DbPool()->Print(pSelf->Console(), CDbConnectionPool::WRITE_BACKUP);
	}
	else if(str_comp_nocase(pResult->GetString(0), "r") == 0)
	{
		pSelf->DbPool()->Print(pSelf->Console(), CDbConnectionPool::READ);
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return;
	}
}

void CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		str_clean_whitespaces(pThis->Config()->m_SvName);
		pThis->UpdateServerInfo(true);
	}
}

void CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
}

void CServer::ConchainCommandAccessUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 2)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		const IConsole::CCommandInfo *pInfo = pThis->Console()->GetCommandInfo(pResult->GetString(0), CFGFLAG_SERVER, false);
		int OldAccessLevel = 0;
		if(pInfo)
			OldAccessLevel = pInfo->GetAccessLevel();
		pfnCallback(pResult, pCallbackUserData);
		if(pInfo && OldAccessLevel != pInfo->GetAccessLevel())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(pThis->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY ||
					(pInfo->GetAccessLevel() > AUTHED_ADMIN - pThis->m_aClients[i].m_Authed && AUTHED_ADMIN - pThis->m_aClients[i].m_Authed < OldAccessLevel) ||
					(pInfo->GetAccessLevel() < AUTHED_ADMIN - pThis->m_aClients[i].m_Authed && AUTHED_ADMIN - pThis->m_aClients[i].m_Authed > OldAccessLevel) ||
					(pThis->m_aClients[i].m_pRconCmdToSend && str_comp(pResult->GetString(0), pThis->m_aClients[i].m_pRconCmdToSend->m_pName) >= 0))
					continue;

				if(OldAccessLevel < pInfo->GetAccessLevel())
					pThis->SendRconCmdAdd(pInfo, i);
				else
					pThis->SendRconCmdRem(pInfo, i);
			}
		}
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->Console()->SetPrintOutputLevel(pThis->m_PrintCBIndex, pResult->GetInteger(0));
	}
}

void CServer::LogoutClient(int ClientID, const char *pReason)
{
	if(!IsSixup(ClientID))
	{
		CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
		Msg.AddInt(0); //authed
		Msg.AddInt(0); //cmdlist
		SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
	else
	{
		CMsgPacker Msg(12, true, true); //NETMSG_RCON_AUTH_OFF
		SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}

	m_aClients[ClientID].m_AuthTries = 0;
	m_aClients[ClientID].m_pRconCmdToSend = 0;

	char aBuf[64];
	if(*pReason)
	{
		str_format(aBuf, sizeof(aBuf), "Logged out by %s.", pReason);
		SendRconLine(ClientID, aBuf);
		str_format(aBuf, sizeof(aBuf), "ClientID=%d with key=%s logged out by %s", ClientID, m_AuthManager.KeyIdent(m_aClients[ClientID].m_AuthKey), pReason);
	}
	else
	{
		SendRconLine(ClientID, "Logout successful.");
		str_format(aBuf, sizeof(aBuf), "ClientID=%d with key=%s logged out", ClientID, m_AuthManager.KeyIdent(m_aClients[ClientID].m_AuthKey));
	}

	m_aClients[ClientID].m_Authed = AUTHED_NO;
	m_aClients[ClientID].m_AuthKey = -1;

	GameServer()->OnSetAuthed(ClientID, AUTHED_NO);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CServer::LogoutKey(int Key, const char *pReason)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(m_aClients[i].m_AuthKey == Key)
			LogoutClient(i, pReason);
}

void CServer::ConchainRconPasswordChangeGeneric(int Level, const char *pCurrent, IConsole::IResult *pResult)
{
	if(pResult->NumArguments() == 1)
	{
		int KeySlot = m_AuthManager.DefaultKey(Level);
		const char *pNew = pResult->GetString(0);
		if(str_comp(pCurrent, pNew) == 0)
		{
			return;
		}
		if(KeySlot == -1 && pNew[0])
		{
			m_AuthManager.AddDefaultKey(Level, pNew);
		}
		else if(KeySlot >= 0)
		{
			if(!pNew[0])
			{
				AuthRemoveKey(KeySlot);
				// Already logs users out.
			}
			else
			{
				m_AuthManager.UpdateKey(KeySlot, pNew, Level);
				LogoutKey(KeySlot, "key update");
			}
		}
	}
}

void CServer::ConchainRconPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pThis = static_cast<CServer *>(pUserData);
	pThis->ConchainRconPasswordChangeGeneric(AUTHED_ADMIN, pThis->Config()->m_SvRconPassword, pResult);
	pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainRconModPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pThis = static_cast<CServer *>(pUserData);
	pThis->ConchainRconPasswordChangeGeneric(AUTHED_MOD, pThis->Config()->m_SvRconModPassword, pResult);
	pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainRconHelperPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pThis = static_cast<CServer *>(pUserData);
	pThis->ConchainRconPasswordChangeGeneric(AUTHED_HELPER, pThis->Config()->m_SvRconHelperPassword, pResult);
	pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainMapUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() >= 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		str_copy(pThis->Config()->m_SvMap, pResult->GetString(0), 128);
	}
}

void CServer::ConchainSixupUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	return;
	// pfnCallback(pResult, pCallbackUserData);
	// CServer *pThis = static_cast<CServer *>(pUserData);
	// if(pResult->NumArguments() >= 1 && pThis->m_aCurrentMap[0] != '\0')
	// 	pThis->m_MapReload |= (pThis->m_apCurrentMapData[MAP_TYPE_SIXUP] != 0) != (pResult->GetInteger(0) != 0);
}

#if defined(CONF_FAMILY_UNIX)
void CServer::ConchainConnLoggingServerChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pServer = (CServer *)pUserData;

		// open socket to send new connections
		if(!pServer->m_ConnLoggingSocketCreated)
		{
			pServer->m_ConnLoggingSocket = net_unix_create_unnamed();
			if(pServer->m_ConnLoggingSocket == -1)
			{
				pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Failed to created socket for communication with the connection logging server.");
			}
			else
			{
				pServer->m_ConnLoggingSocketCreated = true;
			}
		}

		// set the destination address for the connection logging
		net_unix_set_addr(&pServer->m_ConnLoggingDestAddr, pResult->GetString(0));
	}
}
#endif

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pAntibot = Kernel()->RequestInterface<IEngineAntibot>();

	// register console commands
	Console()->Register("kick", "i[id] ?r[reason]", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "?r[name]", CFGFLAG_SERVER, ConStatus, this, "List players containing name or all players");
	Console()->Register("shutdown", "?r[reason]", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");
	Console()->Register("show_ips", "?i[show]", CFGFLAG_SERVER, ConShowIps, this, "Show IP addresses in rcon commands (1 = on, 0 = off)");

	Console()->Register("record", "?s[file]", CFGFLAG_SERVER | CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Register("add_sqlserver", "s['r'|'w'] s[Database] s[Prefix] s[User] s[Password] s[IP] i[Port] ?i[SetUpDatabase ?]", CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConAddSqlServer, this, "add a sqlserver");
	Console()->Register("dump_sqlservers", "s['r'|'w']", CFGFLAG_SERVER, ConDumpSqlServers, this, "dumps all sqlservers readservers = r, writeservers = w");

	Console()->Register("auth_add", "s[ident] s[level] r[pw]", CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConAuthAdd, this, "Add a rcon key");
	Console()->Register("auth_add_p", "s[ident] s[level] s[hash] s[salt]", CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConAuthAddHashed, this, "Add a prehashed rcon key");
	Console()->Register("auth_change", "s[ident] s[level] r[pw]", CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConAuthUpdate, this, "Update a rcon key");
	Console()->Register("auth_change_p", "s[ident] s[level] s[hash] s[salt]", CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConAuthUpdateHashed, this, "Update a rcon key with prehashed data");
	Console()->Register("auth_remove", "s[ident]", CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConAuthRemove, this, "Remove a rcon key");
	Console()->Register("auth_list", "", CFGFLAG_SERVER, ConAuthList, this, "List all rcon keys");

	Console()->Register("name_ban", "s[name] ?i[distance] ?i[is_substring] ?r[reason]", CFGFLAG_SERVER, ConNameBan, this, "Ban a certain nickname");
	Console()->Register("name_unban", "s[name]", CFGFLAG_SERVER, ConNameUnban, this, "Unban a certain nickname");
	Console()->Register("name_bans", "", CFGFLAG_SERVER, ConNameBans, this, "List all name bans");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("access_level", ConchainCommandAccessUpdate, this);
	Console()->Chain("console_output_level", ConchainConsoleOutputLevelUpdate, this);

	Console()->Chain("sv_rcon_password", ConchainRconPasswordChange, this);
	Console()->Chain("sv_rcon_mod_password", ConchainRconModPasswordChange, this);
	Console()->Chain("sv_rcon_helper_password", ConchainRconHelperPasswordChange, this);
	Console()->Chain("sv_map", ConchainMapUpdate, this);
	Console()->Chain("sv_sixup", ConchainSixupUpdate, this);

#if defined(CONF_FAMILY_UNIX)
	Console()->Chain("sv_conn_logging_server", ConchainConnLoggingServerChange, this);
#endif

	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
	m_pGameServer->OnConsoleInit();
}

int CServer::SnapNewID(int Lobby)
{
	if(Lobby < 0 || Lobby >= MAX_LOBBIES)
		Lobby = 0;
	return m_IDPool.NewID(Lobby);
}

void CServer::SnapFreeID(int Lobby, int ID)
{
	if(Lobby < 0 || Lobby >= MAX_LOBBIES)
		Lobby = 0;
	m_IDPool.FreeID(Lobby, ID);
}

void *CServer::SnapNewItem(int Type, int ID, int Size)
{
	if(Type > 0xffff)
	{
		g_UuidManager.GetUuid(Type);
	}
	dbg_assert(ID >= 0 && ID <= 0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

static CServer *CreateServer() { return new CServer(); }

void HandleSigIntTerm(int Param)
{
	InterruptSignaled = 1;

	// Exit the next time a signal is received
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}

int main(int argc, const char **argv)
{
	cmdline_fix(&argc, &argv);
	bool Silent = false;

	for(int i = 1; i < argc; i++)
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0)
		{
			Silent = true;
#if defined(CONF_FAMILY_WINDOWS)
			ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
			break;
		}
	}

	if(secure_random_init() != 0)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return -1;
	}
	if(MysqlInit() != 0)
	{
		dbg_msg("mysql", "failed to initialize MySQL library");
		return -1;
	}

	signal(SIGINT, HandleSigIntTerm);
	signal(SIGTERM, HandleSigIntTerm);

#if defined(CONF_EXCEPTION_HANDLING)
	init_exception_handler();
#endif

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	IEngine *pEngine = CreateEngine("DDNet", Silent, 2);
	IGameServer *pGameServer = CreateGameServer();
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER | CFGFLAG_ECON);
	IEngineMasterServer *pEngineMasterServer = CreateEngineMasterServer();
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_SERVER, argc, argv);
	IConfigManager *pConfigManager = CreateConfigManager();
	IEngineAntibot *pEngineAntibot = CreateEngineAntibot();

#if defined(CONF_EXCEPTION_HANDLING)
	char aBuf[IO_MAX_PATH_LENGTH];
	char aBufName[IO_MAX_PATH_LENGTH];
	char aDate[64];
	str_timestamp(aDate, sizeof(aDate));
	str_format(aBufName, sizeof(aBufName), "dumps/" GAME_NAME "-Server_crash_log_%d_%s.RTP", pid(), aDate);
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, aBufName, aBuf, sizeof(aBuf));
	set_exception_handler_log_file(aBuf);
#endif

	pServer->InitRegister(&pServer->m_NetServer, pEngineMasterServer, pConfigManager->Values(), pConsole);

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pGameServer);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfigManager);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineMasterServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMasterServer *>(pEngineMasterServer), false);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineAntibot);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IAntibot *>(pEngineAntibot), false);

		if(RegisterFail)
		{
			delete pKernel;
			return -1;
		}
	}

	pEngine->Init();
	pConfigManager->Init();
	pConsole->Init();
	pEngineMasterServer->Init();
	pEngineMasterServer->Load();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	IOHANDLE File = pStorage->OpenFile(AUTOEXEC_SERVER_FILE, IOFLAG_READ, IStorage::TYPE_ALL);
	if(File)
	{
		io_close(File);
		pConsole->ExecuteFile(AUTOEXEC_SERVER_FILE);
	}
	else // fallback
	{
		pConsole->ExecuteFile(AUTOEXEC_FILE);
	}

	// parse the command line arguments
	if(argc > 1)
		pConsole->ParseArguments(argc - 1, &argv[1]);

	pConsole->Register("sv_test_cmds", "", CFGFLAG_SERVER, CServer::ConTestingCommands, pConsole, "Turns testing commands aka cheats on/off (setting only works in initial config)");

	pEngine->InitLogfile();

	// run the server
	dbg_msg("server", "starting...");
	int Ret = pServer->Run();

	MysqlUninit();
	secure_random_uninit();

	// free
	delete pKernel;

	cmdline_free(argc, argv);
	return Ret;
}

// DDRace

void CServer::GetClientAddr(int ClientID, NETADDR *pAddr) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		*pAddr = *m_NetServer.ClientAddr(ClientID);
	}
}

const char *CServer::GetAnnouncementLine(char const *pFileName)
{
	IOHANDLE File = m_pStorage->OpenFile(pFileName, IOFLAG_READ | IOFLAG_SKIP_BOM, IStorage::TYPE_ALL);
	if(!File)
		return 0;

	std::vector<char *> Lines;
	char *pLine;
	CLineReader Reader;
	Reader.Init(File);
	while((pLine = Reader.Get()))
		if(str_length(pLine))
			if(pLine[0] != '#')
				Lines.push_back(pLine);

	if(Lines.empty())
	{
		return 0;
	}
	else if(Lines.size() == 1)
	{
		m_AnnouncementLastLine = 0;
	}
	else if(!Config()->m_SvAnnouncementRandom)
	{
		if(++m_AnnouncementLastLine >= Lines.size())
			m_AnnouncementLastLine %= Lines.size();
	}
	else
	{
		unsigned Rand;
		do
		{
			Rand = rand() % Lines.size();
		} while(Rand == m_AnnouncementLastLine);

		m_AnnouncementLastLine = Rand;
	}

	io_close(File);

	return Lines[m_AnnouncementLastLine];
}

int *CServer::GetIdMap(int ClientID)
{
	return m_aIdMap + VANILLA_MAX_CLIENTS * ClientID;
}

bool CServer::SetTimedOut(int ClientID, int OrigID)
{
	if(!m_NetServer.SetTimedOut(ClientID, OrigID))
	{
		return false;
	}
	m_aClients[ClientID].m_Sixup = m_aClients[OrigID].m_Sixup;

	if(m_aClients[OrigID].m_Authed != AUTHED_NO)
	{
		LogoutClient(ClientID, "Timeout Protection");
	}
	DelClientCallback(OrigID, "Timeout Protection used", this);
	m_aClients[ClientID].m_Authed = AUTHED_NO;
	m_aClients[ClientID].m_Flags = m_aClients[OrigID].m_Flags;
	m_aClients[ClientID].m_DDNetVersion = m_aClients[OrigID].m_DDNetVersion;
	m_aClients[ClientID].m_GotDDNetVersionPacket = m_aClients[OrigID].m_GotDDNetVersionPacket;
	m_aClients[ClientID].m_DDNetVersionSettled = m_aClients[OrigID].m_DDNetVersionSettled;
	return true;
}

void CServer::SetErrorShutdown(const char *pReason)
{
	str_copy(m_aErrorShutdownReason, pReason, sizeof(m_aErrorShutdownReason));
}
