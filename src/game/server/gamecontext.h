/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <engine/antibot.h>
#include <engine/console.h>
#include <engine/server.h>

#include <game/layers.h>
#include <game/voting.h>

#include <base/tl/array.h>
#include <base/tl/string.h>

#include "eventhandler.h"
//#include "gamecontroller.h"
#include "gameworld.h"
#include "teehistorian.h"

#include <memory>

/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/

enum
{
	NUM_TUNEZONES = 256
};

class CConfig;
class CHeap;
class CPlayer;
class CScore;
class IConsole;
class IGameController;
class IEngine;
class IStorage;
struct CAntibotData;
struct CScoreRandomMapResult;

class CGameContext : public IGameServer
{
	IServer *m_pServer;
	CConfig *m_pConfig;
	IConsole *m_pConsole;
	IEngine *m_pEngine;
	IStorage *m_pStorage;
	IAntibot *m_pAntibot;
	CLayers m_Layers[MAX_LOBBIES];
	CCollision m_Collision[MAX_LOBBIES];
	protocol7::CNetObjHandler m_NetObjHandler7;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;
	CTuningParams m_aTuningList[NUM_TUNEZONES];
	array<string> m_aCensorlist;

	bool m_TeeHistorianActive;
	CTeeHistorian m_TeeHistorian;
	ASYNCIO *m_pTeeHistorianFile;
	CUuid m_GameUuid;
	CPrng m_Prng;

	bool m_Resetting;

	void SwapTeams(int Lobby);

	static void CommandCallback(int ClientID, int FlagMask, const char *pCmd, IConsole::IResult *pResult, void *pUser);
	static void TeeHistorianWrite(const void *pData, int DataSize, void *pUser);

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDump(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneZone(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDumpZone(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneResetZone(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneSetZoneMsgEnter(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneSetZoneMsgLeave(IConsole::IResult *pResult, void *pUserData);
	static void ConSwitchOpen(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConTimeLimit(IConsole::IResult *pResult, void *pUserData);
	static void ConScoreLimit(IConsole::IResult *pResult, void *pUserData);
	static void ConSpectatorSlots(IConsole::IResult *pResult, void *pUserData);
	static void ConLobby(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteSpec(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteLobbies(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConForceVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConAddMapVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteNo(IConsole::IResult *pResult, void *pUserData);
	static void ConDumpAntibot(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static void ConSwapTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConSetHitPoints(IConsole::IResult *pResult, void *pUserData);
	static void ConShuffleTeams(IConsole::IResult *pResult, void *pUserData);
	void Construct(int Resetting);
	void Destruct(int Resetting);
	void AddVote(const char *pDescription, const char *pCommand);

	struct CPersistentClientData
	{
		bool m_IsSpectator;
	};

public:
	IServer *Server() const { return m_pServer; }
	CConfig *Config() { return m_pConfig; }
	IConsole *Console() { return m_pConsole; }
	IEngine *Engine() { return m_pEngine; }
	IStorage *Storage() { return m_pStorage; }
	CCollision *Collision(int Lobby) { return &m_Collision[Lobby]; }
	CTuningParams *Tuning() { return &m_Tuning; }
	CTuningParams *TuningList() { return &m_aTuningList[0]; }
	IAntibot *Antibot() { return m_pAntibot; }
	CTeeHistorian *TeeHistorian() { return &m_TeeHistorian; }
	bool TeeHistorianActive() const { return m_TeeHistorianActive; }

	CGameContext();
	CGameContext(int Reset);
	~CGameContext();

	void Clear();

	void CreateMapEntities(int Lobby);

	CEventHandler m_Events;
	CPlayer *m_apPlayers[MAX_CLIENTS];

	IGameController *m_apController[MAX_LOBBIES];
	CGameWorld m_World[MAX_LOBBIES];

	bool m_AddMapVotes = false;

	// helper functions
	class CCharacter *GetPlayerChar(int ClientID);
	bool EmulateBug(int Bug);

	virtual int GetLobbiesMap(int Lobby);

	// voting
	void StartVote(int Lobby, const char *pDesc, const char *pCommand, const char *pReason, const char *pSixupDesc);
	void EndVote(int Lobby);
	void SendVoteSet(int ClientID, int Lobby);
	void SendVoteStatus(int ClientID, int Total, int Yes, int No, int Lobby);
	void AbortVoteKickOnDisconnect(int ClientID);

	struct CVote
	{
		int m_VoteCreator;
		int m_VoteType;
		int64_t m_VoteCloseTime;
		bool m_VoteUpdate;
		int m_VotePos;
		char m_aVoteDescription[VOTE_DESC_LENGTH];
		char m_aSixupVoteDescription[VOTE_DESC_LENGTH];
		char m_aVoteCommand[VOTE_CMD_LENGTH];
		char m_aVoteReason[VOTE_REASON_LENGTH];
		int m_VoteEnforce;
		int64_t m_LastMapVote;
		bool m_VoteWillPass;

		int m_VoteVictim;
		int m_VoteEnforcer;
	};

	int m_NumVoteOptions;

	CVote m_aVotes[MAX_LOBBIES];

	char m_aaZoneEnterMsg[NUM_TUNEZONES][256]; // 0 is used for switching from or to area without tunings
	char m_aaZoneLeaveMsg[NUM_TUNEZONES][256];

	char m_aDeleteTempfile[128];
	void DeleteTempfile();

	enum
	{
		VOTE_ENFORCE_UNKNOWN = 0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,
		VOTE_ENFORCE_ABORT,
	};
	CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	// helper functions
	void CreateDamageInd(int Lobby, vec2 Pos, float AngleMod, int Amount, int64_t Mask = -1);
	void CreateExplosion(int Lobby, vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, int64_t Mask);
	void CreateHammerHit(int Lobby, vec2 Pos, int64_t Mask = -1);
	void CreatePlayerSpawn(int Lobby, vec2 Pos, int64_t Mask = -1);
	void CreateDeath(int Lobby, vec2 Pos, int ClientID, int64_t Mask = -1);
	void CreateSound(int Lobby, vec2 Pos, int Sound, int64_t Mask = -1);
	void CreateSoundGlobal(int Lobby, int Sound, int Target = -1);

	bool CheckSightVisibility(int Lobby, CCharacter * pChar, vec2 Pos, float radius, CCharacter * pCharTarget);

	enum
	{
		CHAT_ALL = -2,
		CHAT_SPEC = -1,
		CHAT_RED = 0,
		CHAT_BLUE = 1,
		CHAT_WHISPER_SEND = 2,
		CHAT_WHISPER_RECV = 3,

		CHAT_SIX = 1 << 0,
		CHAT_SIXUP = 1 << 1,
	};

	// network
	void CallVote(int ClientID, const char *pDesc, const char *pCmd, const char *pReason, const char *pChatmsg, const char *pSixupDesc = 0);
	void SendChatTarget(int To, const char *pText, int Flags = CHAT_SIX | CHAT_SIXUP);
	void SendChatTeam(int Team, const char *pText);
	void SendChat(int ClientID, int Team, const char *pText, int SpamProtectionClientID = -1, int Flags = CHAT_SIX | CHAT_SIXUP);
	void SendGameMsg(int GameMsgID, int ParaI1, int ClientID);
	void SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID);
	void SendStartWarning(int ClientID, const char *pMessage);
	void SendEmoticon(int ClientID, int Emoticon);
	void SendWeaponPickup(int ClientID, int Weapon);
	void SendMotd(int ClientID);
	void SendSettings(int ClientID);
	void SendBroadcast(const char *pText, int ClientID, bool IsImportant = true);

	void List(int ClientID, const char *filter);

	//
	void CheckPureTuning();
	void SendTuningParams(int ClientID, int Zone = 0);

	struct CVoteOptionServer *GetVoteOption(int Index);
	void ProgressVoteOptions(int ClientID);

	//
	void LoadMapSettings();

	virtual void SetPlayer_LastAckedSnapshot(int ClientID, int tick);

	// engine events
	virtual void OnInit();
	virtual void OnConsoleInit();
	virtual void OnMapChange(char *pNewMapName, int MapNameSize);
	virtual void OnShutdown();

	virtual void ResetAllGames();
	virtual void OnTick();
	virtual void OnPreSnap();
	virtual void OnSnap(int ClientID);
	virtual void OnPostSnap();

	void *PreProcessMsg(int *MsgID, CUnpacker *pUnpacker, int ClientID);
	void CensorMessage(char *pCensoredMessage, const char *pMessage, int Size);
	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID);

	virtual bool OnClientDataPersist(int ClientID, void *pData);
	virtual void OnClientConnected(int ClientID, void *pData);
	virtual void OnClientEnter(int ClientID);
	virtual void OnClientDrop(int ClientID, const char *pReason);
	virtual void OnClientDirectInput(int ClientID, void *pInput);
	virtual void OnClientPredictedInput(int ClientID, void *pInput, int tick);
	virtual void OnClientPredictedEarlyInput(int ClientID, void *pInput);

	virtual void OnClientEngineJoin(int ClientID, bool Sixup);
	virtual void OnClientEngineDrop(int ClientID, const char *pReason);

	virtual bool IsClientReady(int ClientID) const;
	virtual bool IsClientPlayer(int ClientID) const;
	virtual int PersistentClientDataSize() const { return sizeof(CPersistentClientData); }

	virtual CUuid GameUuid() const;
	virtual const char *GameType() const;
	virtual const char *Version() const;
	virtual const char *NetVersion() const;

	// DDRace
	void OnPreTickTeehistorian();
	bool OnClientDDNetVersionKnown(int ClientID);
	int ProcessSpamProtection(int ClientID, bool RespectChatInitialDelay = true);
	int GetLobby(int ClientID);
	// Describes the time when the first player joined the server.
	int64_t m_NonEmptySince;
	int GetClientVersion(int ClientID) const;
	bool PlayerExists(int ClientID) const { return m_apPlayers[ClientID]; }
	void KillPlayer(int ClientID) override;
	// Returns true if someone is actively moderating.
	bool PlayerModerating() const;
	void ForceVote(int EnforcerID, bool Success);

	// Checks if player can vote and notify them about the reason
	bool RateLimitPlayerVote(int ClientID);
	bool RateLimitPlayerMapVote(int ClientID);

	std::shared_ptr<CScoreRandomMapResult> m_SqlRandomMapResult;

private:
	//DDRace Console Commands

	static void ConKillPlayer(IConsole::IResult *pResult, void *pUserData);

	static void ConNinja(IConsole::IResult *pResult, void *pUserData);
	static void ConEndlessHook(IConsole::IResult *pResult, void *pUserData);
	static void ConUnEndlessHook(IConsole::IResult *pResult, void *pUserData);
	static void ConUnSolo(IConsole::IResult *pResult, void *pUserData);
	static void ConUnDeep(IConsole::IResult *pResult, void *pUserData);
	static void ConLiveFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConUnLiveFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConUnSuper(IConsole::IResult *pResult, void *pUserData);
	static void ConShotgun(IConsole::IResult *pResult, void *pUserData);
	static void ConIDM(IConsole::IResult *pResult, void *pUserData);
	static void ConLOS(IConsole::IResult *pResult, void *pUserData);
	static void ConGrenade(IConsole::IResult *pResult, void *pUserData);
	static void ConHammer(IConsole::IResult *pResult, void *pUserData);
	static void ConTournamentMode(IConsole::IResult *pResult, void *pUserData);
	static void ConLaser(IConsole::IResult *pResult, void *pUserData);
	static void ConJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConWeapons(IConsole::IResult *pResult, void *pUserData);
	static void ConUnShotgun(IConsole::IResult *pResult, void *pUserData);
	static void ConUnGrenade(IConsole::IResult *pResult, void *pUserData);
	static void ConUnLaser(IConsole::IResult *pResult, void *pUserData);
	static void ConUnJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConUnWeapons(IConsole::IResult *pResult, void *pUserData);
	static void ConAddWeapon(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveWeapon(IConsole::IResult *pResult, void *pUserData);

	void ModifyWeapons(IConsole::IResult *pResult, void *pUserData, int Weapon, bool Remove);
	void MoveCharacter(int ClientID, int X, int Y, bool Raw = false);
	static void ConGoLeft(IConsole::IResult *pResult, void *pUserData);
	static void ConGoRight(IConsole::IResult *pResult, void *pUserData);
	static void ConGoUp(IConsole::IResult *pResult, void *pUserData);
	static void ConGoDown(IConsole::IResult *pResult, void *pUserData);
	static void ConMove(IConsole::IResult *pResult, void *pUserData);
	static void ConMoveRaw(IConsole::IResult *pResult, void *pUserData);

	static void ConToTeleporter(IConsole::IResult *pResult, void *pUserData);
	static void ConToCheckTeleporter(IConsole::IResult *pResult, void *pUserData);
	void Teleport(CCharacter *pChr, vec2 Pos);
	static void ConTeleport(IConsole::IResult *pResult, void *pUserData);

	static void ConCredits(IConsole::IResult *pResult, void *pUserData);
	static void ConInfo(IConsole::IResult *pResult, void *pUserData);
	static void ConHelp(IConsole::IResult *pResult, void *pUserData);
	static void ConSettings(IConsole::IResult *pResult, void *pUserData);
	static void ConRules(IConsole::IResult *pResult, void *pUserData);
	static void ConKill(IConsole::IResult *pResult, void *pUserData);
	static void ConTogglePause(IConsole::IResult *pResult, void *pUserData);
	static void ConTogglePauseVoted(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSpec(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleSpecVoted(IConsole::IResult *pResult, void *pUserData);
	static void ConForcePause(IConsole::IResult *pResult, void *pUserData);
	static void ConTeamTop5(IConsole::IResult *pResult, void *pUserData);
	static void ConTop(IConsole::IResult *pResult, void *pUserData);
	static void ConTimes(IConsole::IResult *pResult, void *pUserData);
	static void ConPoints(IConsole::IResult *pResult, void *pUserData);
	static void ConTopPoints(IConsole::IResult *pResult, void *pUserData);

	//ictf features
	static void ConStop(IConsole::IResult *pResult, void *pUserData);
	static void ConGo(IConsole::IResult *pResult, void *pUserData);
	static void ConXonX(IConsole::IResult *pResult, void *pUserData);
	static void ConReset(IConsole::IResult *pResult, void *pUserData);

	static void ConUTF8(IConsole::IResult *pResult, void *pUserData);
	static void ConDND(IConsole::IResult *pResult, void *pUserData);
	static void ConTimeout(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadTime(IConsole::IResult *pResult, void *pUserData);
	static void ConMe(IConsole::IResult *pResult, void *pUserData);
	static void ConWhisper(IConsole::IResult *pResult, void *pUserData);
	static void ConConverse(IConsole::IResult *pResult, void *pUserData);
	static void ConSetEyeEmote(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConEyeEmote(IConsole::IResult *pResult, void *pUserData);
	static void ConShowOthers(IConsole::IResult *pResult, void *pUserData);
	static void ConShowAll(IConsole::IResult *pResult, void *pUserData);
	static void ConSpecTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConNinjaJetpack(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTime(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTimeAll(IConsole::IResult *pResult, void *pUserData);
	static void ConTime(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTimerType(IConsole::IResult *pResult, void *pUserData);
	static void ConTele(IConsole::IResult *pResult, void *pUserData);
	static void ConStats(IConsole::IResult *pResult, void *pUserData);
	static void ConRank(IConsole::IResult *pResult, void *pUserData);
	static void ConTop5(IConsole::IResult *pResult, void *pUserData);
	static void ConProtectedKill(IConsole::IResult *pResult, void *pUserData);

	static void ConVoteMute(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteUnmute(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteMutes(IConsole::IResult *pResult, void *pUserData);
	static void ConMute(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteID(IConsole::IResult *pResult, void *pUserData);
	static void ConMuteIP(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmute(IConsole::IResult *pResult, void *pUserData);
	static void ConUnmuteID(IConsole::IResult *pResult, void *pUserData);
	static void ConMutes(IConsole::IResult *pResult, void *pUserData);
	static void ConModerate(IConsole::IResult *pResult, void *pUserData);

	static void ConList(IConsole::IResult *pResult, void *pUserData);
	static void ConFreezeHammer(IConsole::IResult *pResult, void *pUserData);
	static void ConUnFreezeHammer(IConsole::IResult *pResult, void *pUserData);

	enum
	{
		MAX_MUTES = 32,
		MAX_VOTE_MUTES = 32,
	};
	struct CMute
	{
		NETADDR m_Addr;
		int m_Expire;
		char m_aReason[128];
		bool m_InitialChatDelay;
	};

	CMute m_aMutes[MAX_MUTES];
	int m_NumMutes;
	CMute m_aVoteMutes[MAX_VOTE_MUTES];
	int m_NumVoteMutes;
	bool TryMute(const NETADDR *pAddr, int Secs, const char *pReason, bool InitialChatDelay);
	void Mute(const NETADDR *pAddr, int Secs, const char *pDisplayName, const char *pReason = "", bool InitialChatDelay = false);
	bool TryVoteMute(const NETADDR *pAddr, int Secs);
	bool VoteMute(const NETADDR *pAddr, int Secs, const char *pDisplayName, int AuthedID);
	bool VoteUnmute(const NETADDR *pAddr, const char *pDisplayName, int AuthedID);
	void Whisper(int ClientID, char *pStr);
	void WhisperID(int ClientID, int VictimID, const char *pMessage);
	void Converse(int ClientID, char *pStr);
	bool IsVersionBanned(int Version);
	void UnlockTeam(int ClientID, int Team);

public:
	CLayers *Layers(int Lobby) { return &m_Layers[Lobby]; }

	enum
	{
		VOTE_ENFORCE_NO_ADMIN = VOTE_ENFORCE_YES + 1,
		VOTE_ENFORCE_YES_ADMIN,

		VOTE_TYPE_UNKNOWN = 0,
		VOTE_TYPE_OPTION,
		VOTE_TYPE_KICK,
		VOTE_TYPE_SPECTATE,
	};

	inline bool IsOptionVote(int Lobby) const { return m_aVotes[Lobby].m_VoteType == VOTE_TYPE_OPTION; }
	inline bool IsKickVote(int Lobby) const { return m_aVotes[Lobby].m_VoteType == VOTE_TYPE_KICK; }
	inline bool IsSpecVote(int Lobby) const { return m_aVotes[Lobby].m_VoteType == VOTE_TYPE_SPECTATE; }

	static void SendChatResponse(const char *pLine, void *pUser, ColorRGBA PrintColor = {1, 1, 1, 1});
	static void SendChatResponseAll(const char *pLine, void *pUser);
	virtual void OnSetAuthed(int ClientID, int Level);
	virtual bool PlayerCollision();
	virtual bool PlayerHooking();
	virtual float PlayerJetpack();

	void ResetTuning();

	int m_ChatResponseTargetID;
	int m_ChatPrintCBIndex;
};

inline int64_t CmaskAll() { return -1LL; }
inline int64_t CmaskOne(int ClientID) { return 1LL << ClientID; }
inline int64_t CmaskUnset(int64_t Mask, int ClientID) { return Mask ^ CmaskOne(ClientID); }
inline int64_t CmaskAllExceptOne(int ClientID) { return CmaskUnset(CmaskAll(), ClientID); }
inline bool CmaskIsSet(int64_t Mask, int ClientID) { return (Mask & CmaskOne(ClientID)) != 0; }
#endif
