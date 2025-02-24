/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

#include "alloc.h"

// this include should perhaps be removed
#include "teeinfo.h"
#include <game/server/gamecontext.h>

#include <atomic>

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

#ifndef POSITION_HISTORY
	#define POSITION_HISTORY 50
#endif

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientID, int Team);
	~CPlayer();

	void Reset();

	void TryRespawn();
	void Respawn(bool WeakHook = false); // with WeakHook == true the character will be spawned after all calls of Tick from other Players
	CCharacter *ForceSpawn(vec2 Pos); // required for loading savegames
	void SetTeam(int Team, bool DoChatMsg = true);
	int GetTeam() const { return m_Team; }
	int GetCID() const { return m_ClientID; }
	int GetClientVersion() const;
	bool SetTimerType(int TimerType);

	void Tick();
	void PostTick();

	// will be called after all Tick and PostTick calls from other players
	void PostPostTick();
	void Snap(int SnappingClient);
	void FakeSnap();

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedEarlyInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect();

	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();

	void SpectatePlayerName(const char *pName);

	void SendChat(const char* message);

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;
	int m_TuneZone;
	int m_TuneZoneOld;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aCurLatency[MAX_CLIENTS];
	int m_LastAckedSnapshot;
	int m_LAS_leftover;

	// used for spectator mode
	int m_SpectatorID;

	bool m_IsReady;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;
	int m_LastCommands[4];
	int m_LastCommandPos;
	int m_LastWhisperTo;
	int m_LastInvited;
	int m_RespawnTick;
	int m_Spree;

	bool m_Rollback;
	bool m_RollbackPrediction;
	bool m_ShowRollbackShadow;
	float m_Rollback_partial;

	float m_RunAhead;

	CNetObj_Character m_CoreAheads[POSITION_HISTORY];

	int m_LatestTargetX;
	int m_LatestTargetY;

	int m_SendVoteIndex;

	CTeeInfo m_TeeInfos;

	int m_DieTick;
	int m_PreviousDieTick;
	std::atomic<int> m_Score;
	std::atomic<int> m_Kills;
	std::atomic<int> m_Deaths;
	std::atomic<int> m_Touches;
	std::atomic<int> m_Captures;
	std::atomic<int> m_Suicides;
	std::atomic<int> m_FastestCapture; //in ms
	std::atomic<int> m_Shots;
	std::atomic<int> m_Wallshots;
	std::atomic<int> m_WallshotKills;
	
	int m_JoinTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	int m_TeamChangeTick;
	bool m_SentSemicolonTip;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

private:
	CCharacter *m_pCharacter;
	int m_NumInputs;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	bool m_WeakHookSpawn;
	int m_ClientID;
	int m_Team;

	int m_Paused;
	int64_t m_ForcePauseTime;
	int64_t m_LastPause;

	int m_DefEmote;
	int m_OverrideEmote;
	int m_OverrideEmoteReset;
	bool m_Halloween;

public:
	enum
	{
		PAUSE_NONE = 0,
		PAUSE_PAUSED,
		PAUSE_SPEC
	};

	enum
	{
		TIMERTYPE_DEFAULT = -1,
		TIMERTYPE_GAMETIMER = 0,
		TIMERTYPE_BROADCAST,
		TIMERTYPE_GAMETIMER_AND_BROADCAST,
		TIMERTYPE_SIXUP,
		TIMERTYPE_NONE,
	};

	bool m_DND;
	int64_t m_FirstVoteTick;
	char m_aTimeoutCode[64];

	void ProcessPause();
	int Pause(int State, bool Force);
	int ForcePause(int Time);
	int IsPaused();

	bool IsPlaying();
	int64_t m_Last_KickVote;
	int64_t m_Last_Team;
	int m_ShowOthers;
	bool m_ShowAll;
	vec2 m_ShowDistance;
	bool m_SpecTeam;
	bool m_NinjaJetpack;
	bool m_Afk;
	bool m_HasFinishScore;

	int m_ChatScore;

	bool m_Moderating;

	bool AfkTimer(CNetObj_PlayerInput *pNewTarget); // returns true if kicked
	void UpdatePlaytime();
	void AfkVoteTimer(CNetObj_PlayerInput *pNewTarget);
	int64_t m_LastPlaytime;
	int64_t m_LastEyeEmote;
	int64_t m_LastBroadcast;
	bool m_LastBroadcastImportance;

	CNetObj_PlayerInput *m_pLastTarget;
	bool m_LastTargetInit;
	/* 
		afk timer's 1st warning after 50% of sv_max_afk_time
		2nd warning after 90%
		kick after reaching 100% of sv_max_afk_time
	*/
	bool m_SentAfkWarning[2];

	bool m_EyeEmoteEnabled;
	int m_TimerType;

	int GetDefaultEmote() const;
	void OverrideDefaultEmote(int Emote, int Tick);
	bool CanOverrideDefaultEmote() const;

	bool m_FirstPacket;
	int64_t m_LastSQLQuery;
	bool m_NotEligibleForFinish;
	int64_t m_EligibleForFinishCheck;
	bool m_VotedForPractice;
	int m_SwapTargetsClientID; //Client ID of the swap target for the given player
};

#endif
