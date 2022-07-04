/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_GAMEMODES_DDRACE_H
#define GAME_SERVER_GAMEMODES_DDRACE_H
#include <game/server/entities/door.h>
#include <game/server/gamecontroller.h>
#include <game/server/teams.h>
#include <engine/server/databases/connection.h>

#include <asio.hpp>

#include <map>
#include <vector>

class CGameControllerDDRace : public IGameController
{
public:
	CGameControllerDDRace(class CGameContext *pGameServer);
	~CGameControllerDDRace();

	void UpdateServerStats();

	void OnCharacterSpawn(class CCharacter *pChr) override;
	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int WeaponID) override;
	void Snap(int SnappingClient);
	
	void HandleCharacterTiles(class CCharacter *pChr, int MapIndex) override;
	bool OnEntity(int Index, vec2 Pos, int Layer, int Flags, int Number) override;

	void OnPlayerConnect(class CPlayer *pPlayer) override;
	void OnPlayerDisconnect(class CPlayer *pPlayer, const char *pReason) override;

	void Tick() override;

	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg) override;

	int64_t GetMaskForPlayerWorldEvent(int Asker, int ExceptID = -1) override;

	void InitTeleporter();

	int GetPlayerTeam(int ClientID) const;
	
	IDbConnection GetDatabaseConnection();

	CGameTeams m_Teams;

	std::map<int, std::vector<vec2>> m_TeleOuts;
	std::map<int, std::vector<vec2>> m_TeleCheckOuts;

	asio::io_context io;
};
#endif // GAME_SERVER_GAMEMODES_DDRACE_H
