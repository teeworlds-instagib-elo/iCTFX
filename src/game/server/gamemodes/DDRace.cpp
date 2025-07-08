/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
/* Based on Race mod stuff and tweaked by GreYFoX@GTi and others to fit our DDRace needs. */
#include "DDRace.h"

#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <engine/server.h>
#include <game/version.h>
#include <game/server/entities/flag.h>

#include <algorithm>

#include <limits>
#include <game/server/entities/bot.h>

#define GAME_TYPE_NAME "iCTFX"
#define TEST_TYPE_NAME "TestiCTFX"

CGameControllerDDRace::CGameControllerDDRace(class CGameContext *pGameServer, int Lobby) :
	IGameController(pGameServer)
{
	m_pGameType = g_Config.m_SvTestingCommands ? TEST_TYPE_NAME : GAME_TYPE_NAME;
	m_Lobby = Lobby;
	m_GameFlags = GAMEFLAG_TEAMS|GAMEFLAG_FLAGS;

	InitTeleporter();
	m_apFlags[0] = 0;
	m_apFlags[1] = 0;
	m_aTeamscore[TEAM_RED] = 0;
	m_aTeamscore[TEAM_BLUE] = 0;

	idm = false;
	m_BotCount = 0;
	
	if(g_Config.m_SvSaveServer) {
		auto database = CreateMysqlConnection(g_Config.m_SqlDatabase, g_Config.m_SqlPrefix, g_Config.m_SqlUser, g_Config.m_SqlPass, g_Config.m_SqlHost, g_Config.m_SqlPort, g_Config.m_SqlSetup);
		if(database != nullptr)
		{
			char aError[256] = "error message not initialized";
			if(database->Connect(aError, sizeof(aError)))
			{
				dbg_msg("sql", "failed connecting to db: %s", aError);
				return;
			}
			//save score
			ServerStats server_stats{};
			char error[4096] = {};
			if (!database->GetServerStats("save_server", server_stats, error, sizeof(error))) {
				m_aTeamscore[TEAM_RED] = server_stats.score_red;
				m_aTeamscore[TEAM_BLUE] = server_stats.score_blue;
			} else {
				dbg_msg("sql", "failed to read stats: %s", error);
			}
			database->Disconnect();
		}
		sql_handler->start();
	}

	int waypointAmount = 16;

	m_aWaypoints[0].x = 7*32;
	m_aWaypoints[0].y = 24.5*32;
	m_aWaypoints[0].connections[0] = 4;
	m_aWaypoints[0].connections[1] = 5;
	m_aWaypoints[0].connectionAmount = 2;

	m_aWaypoints[1].x = 40*32;
	m_aWaypoints[1].y = 35*32;
	m_aWaypoints[1].connections[0] = 2;
	m_aWaypoints[1].connections[1] = 10;
	m_aWaypoints[1].connections[2] = 15;
	m_aWaypoints[1].connectionAmount = 3;

	m_aWaypoints[2].x = 36*32;
	m_aWaypoints[2].y = 22.5*32;
	m_aWaypoints[2].connections[0] = 5;
	m_aWaypoints[2].connections[1] = 3;
	m_aWaypoints[2].connections[2] = 1;
	m_aWaypoints[2].connectionAmount = 3;

	m_aWaypoints[3].x = 34*32;
	m_aWaypoints[3].y = 13*32;
	m_aWaypoints[3].connections[0] = 4;
	m_aWaypoints[3].connections[1] = 8;
	m_aWaypoints[3].connections[2] = 2;
	m_aWaypoints[3].connectionAmount = 3;

	m_aWaypoints[4].x = 9*32;
	m_aWaypoints[4].y = 13*32;
	m_aWaypoints[4].connections[0] = 0;
	m_aWaypoints[4].connections[1] = 3;
	m_aWaypoints[4].connectionAmount = 2;

	m_aWaypoints[5].x = 21*32;
	m_aWaypoints[5].y = 21*32;
	m_aWaypoints[5].connections[0] = 0;
	m_aWaypoints[5].connections[1] = 2;
	m_aWaypoints[5].connections[2] = 14;
	m_aWaypoints[5].connectionAmount = 3;

	m_aWaypoints[6].x = 6*32;
	m_aWaypoints[6].y = 36*32;
	m_aWaypoints[6].connections[0] = 14;
	m_aWaypoints[6].connections[1] = 15;
	m_aWaypoints[6].connectionAmount = 2;

	m_aWaypoints[7].x = 51*32;
	m_aWaypoints[7].y = 24*32;
	m_aWaypoints[7].connections[0] = 8;
	m_aWaypoints[7].connections[1] = 9;
	m_aWaypoints[7].connections[2] = 1;
	m_aWaypoints[7].connectionAmount = 3;

	m_aWaypoints[8].x = 42*32;
	m_aWaypoints[8].y = 16*32;
	m_aWaypoints[8].connections[0] = 3;
	m_aWaypoints[8].connections[1] = 7;
	m_aWaypoints[8].connectionAmount = 2;

	m_aWaypoints[9].x = 75*32;
	m_aWaypoints[9].y = 22*32;
	m_aWaypoints[9].connections[0] = 10;
	m_aWaypoints[9].connections[1] = 7;
	m_aWaypoints[9].connections[2] = 11;
	m_aWaypoints[9].connectionAmount = 3;

	m_aWaypoints[10].x = 75*32;
	m_aWaypoints[10].y = 34*32;
	m_aWaypoints[10].connections[0] = 12;
	m_aWaypoints[10].connections[1] = 9;
	m_aWaypoints[10].connections[2] = 1;
	m_aWaypoints[10].connectionAmount = 3;

	m_aWaypoints[11].x = 90*32;
	m_aWaypoints[11].y = 15*32;
	m_aWaypoints[11].connections[0] = 13;
	m_aWaypoints[11].connections[1] = 9;
	m_aWaypoints[11].connections[2] = 11+waypointAmount;
	m_aWaypoints[11].connectionAmount = 3;

	m_aWaypoints[12].x = 86*32;
	m_aWaypoints[12].y = 33*32;
	m_aWaypoints[12].connections[0] = 10;
	m_aWaypoints[12].connections[1] = 13;
	m_aWaypoints[12].connectionAmount = 2;

	m_aWaypoints[13].x = 90*32;
	m_aWaypoints[13].y = 25*32;
	m_aWaypoints[13].connections[0] = 13+waypointAmount;
	m_aWaypoints[13].connections[1] = 12;
	m_aWaypoints[13].connections[2] = 11;
	m_aWaypoints[13].connectionAmount = 3;

	m_aWaypoints[14].x = 14*32;
	m_aWaypoints[14].y = 29*32;
	m_aWaypoints[14].connections[0] = 5;
	m_aWaypoints[14].connections[1] = 6;
	m_aWaypoints[14].connectionAmount = 2;

	m_aWaypoints[15].x = 25*32;
	m_aWaypoints[15].y = 38*32;
	m_aWaypoints[15].connections[0] = 1;
	m_aWaypoints[15].connections[1] = 6;
	m_aWaypoints[15].connectionAmount = 2;



	//copy and mirror
	for(int x = 0; x < waypointAmount; x++)
	{
		m_aWaypoints[x+waypointAmount].x = (m_aWaypoints[x].x-99*32)*-1 +99*32;
		m_aWaypoints[x+waypointAmount].y = m_aWaypoints[x].y;

		m_aWaypoints[x+waypointAmount].connectionAmount = m_aWaypoints[x].connectionAmount;

		for(int i = 0; i < m_aWaypoints[x].connectionAmount; i++)
		{
			if(m_aWaypoints[x].connections[i] < waypointAmount)
			{
				m_aWaypoints[x+waypointAmount].connections[i] = m_aWaypoints[x].connections[i] + waypointAmount;
			}
			else
			{
				m_aWaypoints[x+waypointAmount].connections[i] = m_aWaypoints[x].connections[i] - waypointAmount;
			}
		}
	}
}

CGameControllerDDRace::~CGameControllerDDRace() {
	if (g_Config.m_SvSaveServer) {
		sql_handler->stop();
	}
};

void CGameControllerDDRace::UpdateServerStats() {
	if(g_Config.m_SvSaveServer)
	{
		//save score
		ServerStats server_stats{
			m_aTeamscore[TEAM_RED],
			m_aTeamscore[TEAM_BLUE]
		};

		sql_handler->set_server_stats(server_stats);
	}
}

void CGameControllerDDRace::OnCharacterSpawn(CCharacter *pChr)
{
	IGameController::OnCharacterSpawn(pChr);
	pChr->SetTeleports(&m_TeleOuts, &m_TeleCheckOuts);
	GameServer()->Collision(m_Lobby)->SetTeleport(&m_TeleOuts);
}

void CGameControllerDDRace::HandleCharacterTiles(CCharacter *pChr, int MapIndex)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	const int ClientID = pPlayer->GetCID();

	int m_TileIndex = GameServer()->Collision(m_Lobby)->GetTileIndex(MapIndex);
	int m_TileFIndex = GameServer()->Collision(m_Lobby)->GetFTileIndex(MapIndex);

	//Sensitivity
	// int S1 = GameServer()->Collision(m_Lobby)->GetPureMapIndex(vec2(pChr->GetPos().x + pChr->GetProximityRadius() / 3.f, pChr->GetPos().y - pChr->GetProximityRadius() / 3.f));
	// int S2 = GameServer()->Collision(m_Lobby)->GetPureMapIndex(vec2(pChr->GetPos().x + pChr->GetProximityRadius() / 3.f, pChr->GetPos().y + pChr->GetProximityRadius() / 3.f));
	// int S3 = GameServer()->Collision(m_Lobby)->GetPureMapIndex(vec2(pChr->GetPos().x - pChr->GetProximityRadius() / 3.f, pChr->GetPos().y - pChr->GetProximityRadius() / 3.f));
	// int S4 = GameServer()->Collision(m_Lobby)->GetPureMapIndex(vec2(pChr->GetPos().x - pChr->GetProximityRadius() / 3.f, pChr->GetPos().y + pChr->GetProximityRadius() / 3.f));
	// int Tile1 = GameServer()->Collision(m_Lobby)->GetTileIndex(S1);
	// int Tile2 = GameServer()->Collision(m_Lobby)->GetTileIndex(S2);
	// int Tile3 = GameServer()->Collision(m_Lobby)->GetTileIndex(S3);
	// int Tile4 = GameServer()->Collision(m_Lobby)->GetTileIndex(S4);
	// int FTile1 = GameServer()->Collision(m_Lobby)->GetFTileIndex(S1);
	// int FTile2 = GameServer()->Collision(m_Lobby)->GetFTileIndex(S2);
	// int FTile3 = GameServer()->Collision(m_Lobby)->GetFTileIndex(S3);
	// int FTile4 = GameServer()->Collision(m_Lobby)->GetFTileIndex(S4);

	// const int PlayerDDRaceState = pChr->m_DDRaceState;
	// bool IsOnStartTile = (m_TileIndex == TILE_START) || (m_TileFIndex == TILE_START) || FTile1 == TILE_START || FTile2 == TILE_START || FTile3 == TILE_START || FTile4 == TILE_START || Tile1 == TILE_START || Tile2 == TILE_START || Tile3 == TILE_START || Tile4 == TILE_START;
	// start
	// if(IsOnStartTile && PlayerDDRaceState != DDRACE_CHEAT)
	// {
	// 	const int Team = GetPlayerTeam(ClientID);
	// 	if(m_Teams.GetSaving(Team))
	// 	{
	// 		GameServer()->SendStartWarning(ClientID, "You can't start while loading/saving of team is in progress");
	// 		pChr->Die(ClientID, WEAPON_WORLD);
	// 		return;
	// 	}
	// 	if(g_Config.m_SvTeam == SV_TEAM_MANDATORY && (Team == TEAM_FLOCK || m_Teams.Count(Team) <= 1))
	// 	{
	// 		GameServer()->SendStartWarning(ClientID, "You have to be in a team with other tees to start");
	// 		pChr->Die(ClientID, WEAPON_WORLD);
	// 		return;
	// 	}
	// 	if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO && Team > TEAM_FLOCK && Team < TEAM_SUPER && m_Teams.Count(Team) < g_Config.m_SvMinTeamSize)
	// 	{
	// 		char aBuf[128];
	// 		str_format(aBuf, sizeof(aBuf), "Your team has fewer than %d players, so your team rank won't count", g_Config.m_SvMinTeamSize);
	// 		GameServer()->SendStartWarning(ClientID, aBuf);
	// 	}
	// 	if(g_Config.m_SvResetPickups)
	// 	{
	// 		pChr->ResetPickups();
	// 	}

	// 	m_Teams.OnCharacterStart(ClientID);
	// 	pChr->m_LastTimeCp = -1;
	// 	pChr->m_LastTimeCpBroadcasted = -1;
	// 	for(float &CurrentTimeCp : pChr->m_aCurrentTimeCp)
	// 	{
	// 		CurrentTimeCp = 0.0f;
	// 	}
	// }

	// finish
	// if(((m_TileIndex == TILE_FINISH) || (m_TileFIndex == TILE_FINISH) || FTile1 == TILE_FINISH || FTile2 == TILE_FINISH || FTile3 == TILE_FINISH || FTile4 == TILE_FINISH || Tile1 == TILE_FINISH || Tile2 == TILE_FINISH || Tile3 == TILE_FINISH || Tile4 == TILE_FINISH) && PlayerDDRaceState == DDRACE_STARTED)
	// 	m_Teams.OnCharacterFinish(ClientID);

	// solo part
	if(((m_TileIndex == TILE_SOLO_ENABLE) || (m_TileFIndex == TILE_SOLO_ENABLE)))
	{
		GameServer()->SendChatTarget(ClientID, "You are now in a solo part");
		pChr->SetSolo(true);
	}
	else if(((m_TileIndex == TILE_SOLO_DISABLE) || (m_TileFIndex == TILE_SOLO_DISABLE)))
	{
		GameServer()->SendChatTarget(ClientID, "You are now out of the solo part");
		pChr->SetSolo(false);
	}
}

int CGameControllerDDRace::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int WeaponID)
{
	IGameController::OnCharacterDeath(pVictim, pKiller, WeaponID);
	int HadFlag = 0;

	// drop flags
	for(int i = 0; i < 2; i++)
	{
		CFlag *F = m_apFlags[i];
		if(F && pKiller && pKiller->GetCharacter() && F->m_pCarryingCharacter == pKiller->GetCharacter())
			HadFlag |= 2;
		if(F && F->m_pCarryingCharacter == pVictim)
		{
			GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_DROP);
			F->m_DropTick = Server()->Tick();
			F->m_pCarryingCharacter = 0;
			F->m_Vel = vec2(0,0);
			F->m_Pos = pVictim->m_Pos;
			for(int i = 0; i < POSITION_HISTORY; i++)
			{
				F->m_Positions[i] = F->m_Pos;
			}
			// pVictim->GetPlayer()->m_Stats.m_LostFlags++;

			if(pKiller && pKiller->GetTeam() != pVictim->GetPlayer()->GetTeam())
			{
				// if(g_Config.m_SvLoltextShow)
				// 	GameServer()->CreateLolText(pKiller->GetCharacter(), "+1");
				pKiller->m_Score++;
			}

			HadFlag |= 1;
		}
	}

	return HadFlag;
}

#include <iostream>
#include <fstream>
#include "../../../engine/server.h"
#include <string>
using namespace std;

void CGameControllerDDRace::OnPlayerConnect(CPlayer *pPlayer)
{
	IGameController::OnPlayerConnect(pPlayer);
	int ClientID = pPlayer->GetCID();

	if(!Server()->ClientPrevIngame(ClientID))
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), GetTeamName(pPlayer->GetTeam()));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf, -1, CGameContext::CHAT_SIX);

		GameServer()->SendChatTarget(ClientID, "welcome to iCTFX!");
		GameServer()->SendChatTarget(ClientID, "Version: 1.3");
		GameServer()->SendChatTarget(ClientID, "use /lobby to go to a different lobby");
		GameServer()->SendChatTarget(ClientID, "lobby 0 cannot be changed");
		GameServer()->SendChatTarget(ClientID, "use /list to list all players");
	}

	pPlayer->m_Score = 0;
	pPlayer->m_Kills = 0;
	pPlayer->m_Deaths = 0;
	pPlayer->m_Touches = 0;
	pPlayer->m_Captures = 0;
	pPlayer->m_FastestCapture = -1;
	pPlayer->m_Shots = 0;
	pPlayer->m_Wallshots = 0;
	pPlayer->m_WallshotKills = 0;
	pPlayer->m_Suicides = 0;

	if(g_Config.m_SvSaveServer)
	{
		sql_handler->get_player_stats(pPlayer, Server()->ClientName(pPlayer->GetCID()));
	}
}

void CGameControllerDDRace::OnPlayerNameChange(class CPlayer *pPlayer)
{
	pPlayer->m_Score = 0;
	pPlayer->m_Kills = 0;
	pPlayer->m_Deaths = 0;
	pPlayer->m_Touches = 0;
	pPlayer->m_Captures = 0;
	pPlayer->m_FastestCapture = -1;
	pPlayer->m_Shots = 0;
	pPlayer->m_Wallshots = 0;
	pPlayer->m_WallshotKills = 0;
	pPlayer->m_Suicides = 0;

	if(g_Config.m_SvSaveServer)
	{
		sql_handler->get_player_stats(pPlayer, Server()->ClientName(pPlayer->GetCID()));
	}
}

void CGameControllerDDRace::OnPlayerDisconnect(CPlayer *pPlayer, const char *pReason)
{
	int ClientID = pPlayer->GetCID();
	bool WasModerator = pPlayer->m_Moderating && Server()->ClientIngame(ClientID);

	IGameController::OnPlayerDisconnect(pPlayer, pReason);

	if(!GameServer()->PlayerModerating() && WasModerator)
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, "Server kick/spec votes are no longer actively moderated.");
	
	if(g_Config.m_SvSaveServer)
	{
		//save score
		Stats stats;
		stats.kills = pPlayer->m_Kills;
		stats.deaths = pPlayer->m_Deaths;
		stats.touches = pPlayer->m_Touches;
		stats.captures = pPlayer->m_Captures;
		stats.fastest_capture = pPlayer->m_FastestCapture;
		stats.shots = pPlayer->m_Shots;
		stats.wallshots = pPlayer->m_Wallshots;
		stats.wallshot_kills = pPlayer->m_WallshotKills;
		stats.suicides = pPlayer->m_Suicides;

		sql_handler->set_stats(Server()->ClientName(pPlayer->GetCID()), stats);
	}
}

void CGameControllerDDRace::Snap(int SnappingClient)
{
	IGameController::Snap(SnappingClient);

	if(Server()->IsSixup(SnappingClient))
		return;
	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;

	pGameDataObj->m_TeamscoreRed = m_aTeamscore[TEAM_RED];
	pGameDataObj->m_TeamscoreBlue = m_aTeamscore[TEAM_BLUE];

	if(m_apFlags[TEAM_RED])
	{
		if(m_apFlags[TEAM_RED]->m_AtStand)
			pGameDataObj->m_FlagCarrierRed = FLAG_ATSTAND;
		else if(m_apFlags[TEAM_RED]->m_pCarryingCharacter && m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierRed = m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer()->GetCID();
		else if(m_apFlags[TEAM_RED]->m_BotGrabbed && m_apFlags[TEAM_RED]->m_Bot >= 0)
			pGameDataObj->m_FlagCarrierRed = m_apFlags[TEAM_RED]->m_Bot;
		else
			pGameDataObj->m_FlagCarrierRed = FLAG_TAKEN;
	}
	else
		pGameDataObj->m_FlagCarrierRed = FLAG_MISSING;
	if(m_apFlags[TEAM_BLUE])
	{
		if(m_apFlags[TEAM_BLUE]->m_AtStand)
			pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
		else if(m_apFlags[TEAM_BLUE]->m_pCarryingCharacter && m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierBlue = m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer()->GetCID();
		else if(m_apFlags[TEAM_BLUE]->m_BotGrabbed && m_apFlags[TEAM_BLUE]->m_Bot >= 0)
			pGameDataObj->m_FlagCarrierBlue = m_apFlags[TEAM_BLUE]->m_Bot;
		else
			pGameDataObj->m_FlagCarrierBlue = FLAG_TAKEN;
	}
	else
		pGameDataObj->m_FlagCarrierBlue = FLAG_MISSING;
}

void CGameControllerDDRace::Tick()
{
	if(idm || !(m_apFlags[0] && m_apFlags[1]))
	{
		if(m_GameFlags != 0)
		{
			m_ScoreLimit = 20;
		}

		m_pGameType = "iDMX";
		m_GameFlags = 0;
	}else
	{
		if(m_GameFlags != (GAMEFLAG_TEAMS|GAMEFLAG_FLAGS))
		{
			m_ScoreLimit = 1000;
		}

		m_pGameType = g_Config.m_SvTestingCommands ? TEST_TYPE_NAME : GAME_TYPE_NAME;
		m_GameFlags = GAMEFLAG_TEAMS|GAMEFLAG_FLAGS;
	}

	IGameController::Tick();

	if(m_Lobby == 0)
	{
		int aNumplayers[2] = {0, 0};
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->GetLobby(i) == m_Lobby)
			{
				if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
					aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
			}
		}

		int numPlayers = aNumplayers[0] + aNumplayers[1];

		int wantedAmount = g_Config.m_SvBotAmount - numPlayers;

		if(!numPlayers)
			wantedAmount = 0;

		for(int i = 0; i < MAX_BOTS; i++)
		{
			if(m_BotCount <= i && i < wantedAmount)
			{
				CBot * pBot = new CBot(&GameServer()->m_World[m_Lobby], this, !(i % 2));
				GameServer()->m_World[m_Lobby].InsertEntity(pBot);
				m_apBots[i] = pBot;
			}else if(m_BotCount > i && i >= wantedAmount)
			{
				delete m_apBots[i];
			}
		}
		m_BotCount = wantedAmount;
	}


	if(!idm)
		for(int fi = 0; fi < 2; fi++)
		{
			CFlag *F = m_apFlags[fi];

			if(!F)
				continue;

			// flag hits death-tile or left the game layer, reset it
			if((GameServer()->Collision(m_Lobby)->GetCollisionAt(F->m_Pos.x, F->m_Pos.y) == TILE_DEATH || GameServer()->Collision(m_Lobby)->GetCollisionAt(F->m_Pos.x, F->m_Pos.y) == TILE_SOLID || F->GameLayerClipped(F->m_Pos)) && !F->m_BotGrabbed)
			{
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "flag_return");
				GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_RETURN);
				F->Reset();
				continue;
			}

			F->m_Positions[Server()->Tick() % POSITION_HISTORY] = F->m_Pos;

			//
			if(F->m_pCarryingCharacter)
			{
				// update flag position
				F->m_Pos = F->m_pCarryingCharacter->m_Pos;

				if(m_apFlags[fi^1] && m_apFlags[fi^1]->m_AtStand && F->m_pCarryingCharacter->m_DeathTick == -1)
				{
					if(distance(F->m_Pos, m_apFlags[fi^1]->m_Pos) < CFlag::ms_PhysSize + CCharacter::ms_PhysSize)
					{
						// CAPTURE! \o/
						m_aTeamscore[fi^1] += 100;
						UpdateServerStats();
						F->m_pCarryingCharacter->GetPlayer()->m_Score += 5;
						int playerID = F->m_pCarryingCharacter->GetPlayer()->GetCID();

						// if(fi^1 == 0)
						// {
						// 	g_Config.m_SvBotAmount++;
						// }
						
						// F->m_pCarryingCharacter->GetPlayer()->m_Stats.m_Captures++;

						char aBuf[512];
						str_format(aBuf, sizeof(aBuf), "flag_capture player='%d:%s'",
							F->m_pCarryingCharacter->GetPlayer()->GetCID(),
							Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()));
						GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

						float CaptureTime = (Server()->Tick() - F->m_GrabTick)/(float)Server()->TickSpeed();
						if(CaptureTime <= 60)
						{
							str_format(aBuf, sizeof(aBuf), "The %s flag was captured by '%s' (%d.%s%d seconds)", fi ? "blue" : "red", Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()), (int)CaptureTime%60, ((int)(CaptureTime*100)%100)<10?"0":"", (int)(CaptureTime*100)%100);
						}
						else
						{
							str_format(aBuf, sizeof(aBuf), "The %s flag was captured by '%s'", fi ? "blue" : "red", Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()));
						}

						auto capture_time_millis = CaptureTime*1000;

						if (F->m_pCarryingCharacter->GetPlayer()->m_FastestCapture < 0) {
							F->m_pCarryingCharacter->GetPlayer()->m_FastestCapture = capture_time_millis;
						} else if(F->m_pCarryingCharacter->GetPlayer()->m_FastestCapture > capture_time_millis) {
							F->m_pCarryingCharacter->GetPlayer()->m_FastestCapture = capture_time_millis;
						}
						F->m_pCarryingCharacter->GetPlayer()->m_Captures++;

						for(int i = 0; i < 2; i++)
							m_apFlags[i]->Reset();
						
						for(int i = 0; i < MAX_CLIENTS; i++)
						{
							if(GameServer()->PlayerExists(i) && GameServer()->GetLobby(i) == m_Lobby)							
								GameServer()->SendChatTarget(i, aBuf);
						}

						GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_CAPTURE);
						for(int i = 0; i < MAX_CLIENTS; i++)
						{
							if(Server()->IsSixup(i) && m_Lobby == GameServer()->GetLobby(i))
							{
								GameServer()->SendGameMsg(protocol7::GAMEMSG_CTF_CAPTURE, fi, playerID, Server()->Tick() - F->m_GrabTick, i);
							}

						}
					}
				}
			}
			else if(!F->m_BotGrabbed)
			{
				CCharacter *apCloseCCharacters[MAX_CLIENTS];
				int Num = 0;
				for(CEntity *pEnt = GameServer()->m_World[m_Lobby].m_apFirstEntityTypes[CGameWorld::ENTTYPE_CHARACTER]; pEnt; pEnt = pEnt->m_pNextTypeEntity)
				{
					vec2 Pos = F->m_Pos;
					int tick = ((CCharacter*)pEnt)->m_pPlayer->m_LastAckedSnapshot % POSITION_HISTORY;
					if(tick > 0 && ((CCharacter*)pEnt)->m_pPlayer->m_Rollback && g_Config.m_SvRollback)
						Pos = F->m_Positions[tick];
					
					if(distance(pEnt->m_Pos, Pos) < CFlag::ms_PhysSize + pEnt->m_ProximityRadius && ((CCharacter*)pEnt)->m_DeathTick == -1)
					{
						if(apCloseCCharacters)
							apCloseCCharacters[Num] = (CCharacter * )pEnt;
						Num++;
						if(Num == MAX_CLIENTS)
							break;
					}
				}

				//int Num = GameServer()->m_World[m_Lobby].FindEntities(F->m_Pos, CFlag::ms_PhysSize, (CEntity**)apCloseCCharacters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
				
				for(int i = 0; i < Num; i++)
				{
					if(!apCloseCCharacters[i]->IsAlive() || apCloseCCharacters[i]->GetPlayer()->GetTeam() == TEAM_SPECTATORS || GameServer()->Collision(m_Lobby)->IntersectLine(F->m_Pos, apCloseCCharacters[i]->m_Pos, NULL, NULL))
						continue;

					if(apCloseCCharacters[i]->GetPlayer()->GetTeam() == F->m_Team && apCloseCCharacters[i]->m_DeathTick == -1)
					{
						// return the flag
						if(!F->m_AtStand && !F->m_BotGrabbed)
						{
							CCharacter *pChr = apCloseCCharacters[i];
							pChr->GetPlayer()->m_Score += 1;
							

							char aBuf[256];
							str_format(aBuf, sizeof(aBuf), "flag_return player='%d:%s'",
								pChr->GetPlayer()->GetCID(),
								Server()->ClientName(pChr->GetPlayer()->GetCID()));
							GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

							GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_RETURN);
							F->Reset();
							for(int i = 0; i < MAX_CLIENTS; i++)
							{
								if(Server()->IsSixup(i))
								{
									GameServer()->SendGameMsg(protocol7::GAMEMSG_CTF_RETURN, fi, i);
								}

							}
						}
					}
					else
					{
						// take the flag
						if(F->m_AtStand)
						{
							m_aTeamscore[fi^1]++;
							UpdateServerStats();
							F->m_GrabTick = Server()->Tick();
							for(int i = 0; i < MAX_CLIENTS; i++)
							{
								if(Server()->IsSixup(i))
								{
									GameServer()->SendGameMsg(protocol7::GAMEMSG_CTF_GRAB, fi, i);
								}
							}
						}

						F->m_AtStand = 0;
						F->m_pCarryingCharacter = apCloseCCharacters[i];
						F->m_pCarryingCharacter->GetPlayer()->m_Score += 1;
						F->m_pCarryingCharacter->GetPlayer()->m_Touches++;
						

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "flag_grab player='%d:%s'",
							F->m_pCarryingCharacter->GetPlayer()->GetCID(),
							Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()));
						GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

						for(int c = 0; c < MAX_CLIENTS; c++)
						{
							CPlayer *pPlayer = GameServer()->m_apPlayers[c];
							if(!pPlayer)
								continue;

							if(pPlayer->GetTeam() == TEAM_SPECTATORS && pPlayer->m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[pPlayer->m_SpectatorID] && GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetTeam() == fi)
								GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_GRAB_EN, c);
							else if(pPlayer->GetTeam() == fi)
								GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_GRAB_EN, c);
							else
								GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_GRAB_PL, c);
						}
						// demo record entry
						GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_GRAB_EN, -2);
						break;
					}
				}

				if(!F->m_pCarryingCharacter && !F->m_AtStand && !F->m_BotGrabbed)
				{
					if(Server()->Tick() > F->m_DropTick + Server()->TickSpeed()*30)
					{
						GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_RETURN);
						F->Reset();
						for(int i = 0; i < MAX_CLIENTS; i++)
						{
							if(Server()->IsSixup(i))
							{
								GameServer()->SendGameMsg(protocol7::GAMEMSG_CTF_DROP, fi, -1);
							}

						}
					}
					else
					{
						F->m_Vel.y += 0.5f; //GameServer()->m_World[m_Lobby].m_Core.m_Tuning.m_Gravity;
						GameServer()->Collision(m_Lobby)->MoveBox(&F->m_Pos, &F->m_Vel, vec2(F->ms_PhysSize, F->ms_PhysSize), 0.5f);
						
						int index = GameServer()->Collision(m_Lobby)->GetMapIndex(F->m_Pos);
						CCollision * col = GameServer()->Collision(m_Lobby);
						int tele = col->IsTeleport(index);
						if(!tele)
							tele = col->IsEvilTeleport(index);
						
						if(!tele)
							tele = col->IsCheckTeleport(index);

						if(!tele)
							tele = col->IsCheckEvilTeleport(index);

						if(tele)
						{
							int size = (*col->m_pTeleOuts)[tele - 1].size();
							if(size)
							{
								int RandomOut = rand() % size;
								
								F->m_Pos = (*col->m_pTeleOuts)[tele - 1][RandomOut];
							}
						}
					}
				}
			}
		}
	
	if(m_GameOverTick == -1 && !m_Warmup && !g_Config.m_SvSaveServer)
	{
		// check score win condition
		if(!idm && m_apFlags[0] && m_apFlags[0])
		{
			if((m_ScoreLimit > 0 && (m_aTeamscore[TEAM_RED] >= m_ScoreLimit || m_aTeamscore[TEAM_BLUE] >= m_ScoreLimit)) ||
				(m_TimeLimit > 0 && (Server()->Tick()-m_RoundStartTick) >= m_TimeLimit*Server()->TickSpeed()*60))
			{
				if(m_SuddenDeath)
				{
					if(m_aTeamscore[TEAM_RED]/100 != m_aTeamscore[TEAM_BLUE]/100)
						EndRound();
				}
				else
				{
					if(m_aTeamscore[TEAM_RED] != m_aTeamscore[TEAM_BLUE])
						EndRound();
					else
						m_SuddenDeath = 1;
				}
			}
		} else
		{
			//idm
			int Topscore = 0;
			int TopscoreCount = 0;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i])
				{
					if(GameServer()->m_apPlayers[i]->m_Score > Topscore)
					{
						Topscore = GameServer()->m_apPlayers[i]->m_Score;
						TopscoreCount = 1;
					}
					else if(GameServer()->m_apPlayers[i]->m_Score == Topscore)
						TopscoreCount++;
				}
			}

			// check score win condition
			if((m_ScoreLimit > 0 && Topscore >= m_ScoreLimit) ||
				(m_TimeLimit > 0 && (Server()->Tick()-m_RoundStartTick) >= m_TimeLimit*Server()->TickSpeed()*60))
			{
				if(TopscoreCount == 1)
					EndRound();
				else
					m_SuddenDeath = 1;
			}
		}
	}
}

void CGameControllerDDRace::DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	// Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	CCharacter *pCharacter = pPlayer->GetCharacter();

	IGameController::DoTeamChange(pPlayer, Team, DoChatMsg);
}

void CGameControllerDDRace::ChangeMap(const char *pToMap)
{
	int Map = GameServer()->Layers(m_Lobby)->m_Map;

	if(Map != GameServer()->Layers(m_Lobby)->m_Map || true) //map has reloaded
	{
		m_apFlags[0] = 0;
		m_apFlags[1] = 0;

		m_aTeamscore[0] = 0;
		m_aTeamscore[1] = 0;
	}

	IGameController::ChangeMap(pToMap);

	InitTeleporter();
}

bool CGameControllerDDRace::OnEntity(int Index, vec2 Pos, int Layer, int Flags, int Number)
{
	if(IGameController::OnEntity(Index, Pos, Layer, Flags, Number) || idm)
		return true;

	int Team = -1;
	if(Index == ENTITY_FLAGSTAND_RED) Team = TEAM_RED;
	if(Index == ENTITY_FLAGSTAND_BLUE) Team = TEAM_BLUE;
	if(Team == -1)
		return false;

	CFlag *F = 0;
	if(!m_apFlags[Team])
	{
		F = new CFlag(&GameServer()->m_World[m_Lobby], Team);
		m_apFlags[Team] = F;
	}

	m_apFlags[Team]->m_StandPos = Pos;
	m_apFlags[Team]->m_Pos = Pos;
	
	if(F)
		GameServer()->m_World[m_Lobby].InsertEntity(F);
	return true;
}



int64_t CGameControllerDDRace::GetMaskForPlayerWorldEvent(int Asker, int ExceptID)
{
	if(Asker == -1)
		return CmaskAllExceptOne(ExceptID);

	//todo, maybe mask for lobbies
	return 0;
}

void CGameControllerDDRace::InitTeleporter()
{
	if(!GameServer()->Collision(m_Lobby)->Layers()->TeleLayer())
		return;
	
	m_TeleOuts.clear();
	m_TeleCheckOuts.clear();

	int Width = GameServer()->Collision(m_Lobby)->Layers()->TeleLayer()->m_Width;
	int Height = GameServer()->Collision(m_Lobby)->Layers()->TeleLayer()->m_Height;

	for(int i = 0; i < Width * Height; i++)
	{
		int Number = GameServer()->Collision(m_Lobby)->TeleLayer()[i].m_Number;
		int Type = GameServer()->Collision(m_Lobby)->TeleLayer()[i].m_Type;
		if(Number > 0)
		{
			if(Type == TILE_TELEOUT)
			{
				m_TeleOuts[Number - 1].push_back(
					vec2(i % Width * 32 + 16, i / Width * 32 + 16));
			}
			else if(Type == TILE_TELECHECKOUT)
			{
				m_TeleCheckOuts[Number - 1].push_back(
					vec2(i % Width * 32 + 16, i / Width * 32 + 16));
			}
		}
	}
}
