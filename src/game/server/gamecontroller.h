/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTROLLER_H
#define GAME_SERVER_GAMECONTROLLER_H

#include <base/vmath.h>
#include <engine/map.h>
#include <game/server/entities/flag.h>
#include <game/server/sql_handler.h>

/*
	Class: Game Controller
		Controls the main game logic. Keeping track of team and player score,
		winning conditions and specific game logic.
*/
class IGameController
{

	vec2 m_aaSpawnPoints[3][64];
	int m_aNumSpawnPoints[3];

	class CGameContext *m_pGameServer;
	class CConfig *m_pConfig;
	class IServer *m_pServer;

protected:
	CGameContext *GameServer() const { return m_pGameServer; }
	CConfig *Config() { return m_pConfig; }
	IServer *Server() const { return m_pServer; }

	void DoActivityCheck();

	struct CSpawnEval
	{
		CSpawnEval()
		{
			m_Got = false;
			m_FriendlyTeam = -1;
			m_Pos = vec2(100, 100);
		}

		vec2 m_Pos;
		bool m_Got;
		int m_FriendlyTeam;
		float m_Score;
	};

	float EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos, int DDTeam);
	void EvaluateSpawnType(CSpawnEval *pEval, int Type, int DDTeam);

	void ResetGame();

	char m_aMapWish[MAX_MAP_LENGTH];

	int m_RoundStartTick;
	int m_GameOverTick;
	int m_SuddenDeath;

	int m_Warmup;
	int m_RoundCount;

	int m_GameFlags;
	int m_UnbalancedTick;
	bool m_ForceBalanced;

public:
	#define MAX_BOTS 32
	const char *m_pGameType;
	int m_aTeamscore[2];
	class CFlag *m_apFlags[2];
	int m_BotCount = 0;
	class CBot *m_apBots[MAX_BOTS] = {0};
	bool idm; //todo, not this shitty solution
	bool m_tourneyMode = false;
	int m_Lobby;

	int m_ScoreLimit;
	int m_TimeLimit;
	int m_SpectatorSlots;
	
	std::unique_ptr<SqlHandler> sql_handler;

	IGameController(class CGameContext *pGameServer);
	virtual ~IGameController();

	// event
	/*
		Function: OnCharacterDeath
			Called when a CCharacter in the world dies.

		Arguments:
			victim - The CCharacter that died.
			killer - The player that killed it.
			weapon - What weapon that killed it. Can be -1 for undefined
				weapon when switching team or player suicides.
	*/
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	/*
		Function: OnCharacterSpawn
			Called when a CCharacter spawns into the game world.

		Arguments:
			chr - The CCharacter that was spawned.
	*/
	virtual void OnCharacterSpawn(class CCharacter *pChr);

	virtual void HandleCharacterTiles(class CCharacter *pChr, int MapIndex);

	/*
		Function: OnEntity
			Called when the map is loaded to process an entity
			in the map.

		Arguments:
			index - Entity index.
			pos - Where the entity is located in the world.

		Returns:
			bool?
	*/
	virtual bool OnEntity(int Index, vec2 Pos, int Layer, int Flags, int Number = 0);

	virtual void OnPlayerConnect(class CPlayer *pPlayer);
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer, const char *pReason);
	virtual void OnPlayerNameChange(class CPlayer *pPlayer);

	void OnReset();

	// game
	void DoWarmup(int Seconds);

	void StartRound();
	void EndRound();
	virtual void ChangeMap(const char *pToMap);

	bool IsFriendlyFire(int ClientID1, int ClientID2);

	bool IsForceBalanced();
	bool IsTeamplay() const;
	bool CheckTeamBalance();

	/*

	*/
	virtual bool CanBeMovedOnBalance(int ClientID);

	virtual void Tick();

	virtual void Snap(int SnappingClient);

	//spawn
	virtual bool CanSpawn(int Team, vec2 *pOutPos, int DDTeam);

	virtual void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true);
	/*

	*/
	virtual const char *GetTeamName(int Team);
	virtual int GetAutoTeam(int NotThisID);
	virtual bool CanJoinTeam(int Team, int NotThisID);
	int ClampTeam(int Team);

	virtual int64_t GetMaskForPlayerWorldEvent(int Asker, int ExceptID = -1);

	// DDRace

	float m_CurrentRecord;
	int m_FakeWarmup;
};

#endif
