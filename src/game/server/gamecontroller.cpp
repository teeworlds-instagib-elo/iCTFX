/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <game/mapitems.h>

#include <game/generated/protocol.h>

#include "entities/character.h"
#include "entities/pickup.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"

#include "entities/door.h"
#include "entities/dragger.h"
#include "entities/gun.h"
#include "entities/light.h"
#include "entities/plasma.h"
#include "entities/projectile.h"
#include "entities/bot.h"
#include <game/layers.h>

IGameController::IGameController(class CGameContext *pGameServer): sql_handler(std::make_unique<SqlHandler>())
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();
	// m_pGameType = "iCTF++";

	//
	DoWarmup(g_Config.m_SvWarmup);
	m_GameOverTick = -1;
	m_SuddenDeath = 0;
	m_RoundStartTick = Server()->Tick();
	m_RoundCount = 0;
	m_GameFlags = 0;
	m_aMapWish[0] = 0;

	m_UnbalancedTick = -1;
	m_ForceBalanced = false;

	m_aNumSpawnPoints[0] = 0;
	m_aNumSpawnPoints[1] = 0;
	m_aNumSpawnPoints[2] = 0;
	m_FakeWarmup = 0;

	m_CurrentRecord = 0;

	m_ScoreLimit = g_Config.m_SvScorelimit;
	m_TimeLimit = g_Config.m_SvTimelimit;
	m_SpectatorSlots = g_Config.m_SvSpectatorSlots;
	m_tourneyMode = g_Config.m_SvTournamentMode;
}

IGameController::~IGameController() = default;

void IGameController::DoActivityCheck()
{
	if(g_Config.m_SvInactiveKickTime == 0)
		return;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
#ifdef CONF_DEBUG
		if(g_Config.m_DbgDummies)
		{
			if(i >= MAX_CLIENTS - g_Config.m_DbgDummies)
				break;
		}
#endif
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && Server()->GetAuthedState(i) == AUTHED_NO)
		{
			if(Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick + g_Config.m_SvInactiveKickTime * Server()->TickSpeed() * 60)
			{
				switch(g_Config.m_SvInactiveKick)
				{
				case 0:
				{
					// move player to spectator
					DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 1:
				{
					// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
					int Spectators = 0;
					for(auto &pPlayer : GameServer()->m_apPlayers)
						if(pPlayer && pPlayer->GetTeam() == TEAM_SPECTATORS)
							++Spectators;
					if(Spectators >= m_SpectatorSlots)
						Server()->Kick(i, "Kicked for inactivity");
					else
						DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 2:
				{
					// kick the player
					Server()->Kick(i, "Kicked for inactivity");
				}
				}
			}
		}
	}
}

float IGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos, int DDTeam)
{
	float Score = 0.0f;
	CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World[m_Lobby].FindFirst(CGameWorld::ENTTYPE_CHARACTER));
	for(; pC; pC = (CCharacter *)pC->TypeNext())
	{
		float d = distance(Pos, pC->m_Pos);
		Score += d == 0 ? 1000000000.0f : 1.0f / d;
	}

	CBot *pB = static_cast<CBot *>(GameServer()->m_World[m_Lobby].FindFirst(CGameWorld::ENTTYPE_BOT));
	for(; pB; pB = (CBot *)pB->TypeNext())
	{
		float d = distance(Pos, pB->m_Pos);
		Score += d == 0 ? 1000000000.0f : 1.0f / d;
	}

	return Score;
}

void IGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type, int DDTeam)
{
	// j == 0: Find an empty slot, j == 1: Take any slot if no empty one found
	for(int j = 0; j < 2 && !pEval->m_Got; j++)
	{
		// get spawn point
		for(int i = 0; i < m_aNumSpawnPoints[Type]; i++)
		{
			vec2 P = m_aaSpawnPoints[Type][i];

			if(j == 0)
			{
				// check if the position is occupado
				CCharacter *aEnts[MAX_CLIENTS];
				int Num = GameServer()->m_World[m_Lobby].FindEntities(m_aaSpawnPoints[Type][i], 64, (CEntity **)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
				vec2 Positions[5] = {vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f)}; // start, left, up, right, down
				int Result = -1;
				for(int Index = 0; Index < 5 && Result == -1; ++Index)
				{
					Result = Index;
					if(!GameServer()->m_World[m_Lobby].m_Core.m_Tuning[0].m_PlayerCollision)
						break;
					for(int c = 0; c < Num; ++c)
						if(GameServer()->Collision(m_Lobby)->CheckPoint(m_aaSpawnPoints[Type][i] + Positions[Index]) ||
							distance(aEnts[c]->m_Pos, m_aaSpawnPoints[Type][i] + Positions[Index]) <= aEnts[c]->GetProximityRadius())
						{
							Result = -1;
							break;
						}
				}
				if(Result == -1)
					continue; // try next spawn point

				P += Positions[Result];
			}

			float S = EvaluateSpawnPos(pEval, P, DDTeam);
			if(!pEval->m_Got || (j == 0 && pEval->m_Score > S))
			{
				pEval->m_Got = true;
				pEval->m_Score = S;
				pEval->m_Pos = P;
			}
		}
	}
}

bool IGameController::CanSpawn(int Team, vec2 *pOutPos, int DDTeam)
{
	CSpawnEval Eval;

	// spectators can't spawn
	if(Team == TEAM_SPECTATORS)
		return false;

	if(IsTeamplay())
	{
		Eval.m_FriendlyTeam = Team;

		// first try own team spawn, then normal spawn and then enemy
		EvaluateSpawnType(&Eval, 1+(Team&1), DDTeam);
		if(!Eval.m_Got)
		{
			EvaluateSpawnType(&Eval, 0, DDTeam);
			if(!Eval.m_Got)
				EvaluateSpawnType(&Eval, 1+((Team+1)&1), DDTeam);
		}
	}
	else
	{
		EvaluateSpawnType(&Eval, 0, DDTeam);
		EvaluateSpawnType(&Eval, 1, DDTeam);
		EvaluateSpawnType(&Eval, 2, DDTeam);
	}

	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

bool IGameController::OnEntity(int Index, vec2 Pos, int Layer, int Flags, int Number)
{
	if(Index < 0)
		return false;

	int x, y;
	x = (Pos.x - 16.0f) / 32.0f;
	y = (Pos.y - 16.0f) / 32.0f;
	int sides[8];
	sides[0] = GameServer()->Collision(m_Lobby)->Entity(x, y + 1, Layer);
	sides[1] = GameServer()->Collision(m_Lobby)->Entity(x + 1, y + 1, Layer);
	sides[2] = GameServer()->Collision(m_Lobby)->Entity(x + 1, y, Layer);
	sides[3] = GameServer()->Collision(m_Lobby)->Entity(x + 1, y - 1, Layer);
	sides[4] = GameServer()->Collision(m_Lobby)->Entity(x, y - 1, Layer);
	sides[5] = GameServer()->Collision(m_Lobby)->Entity(x - 1, y - 1, Layer);
	sides[6] = GameServer()->Collision(m_Lobby)->Entity(x - 1, y, Layer);
	sides[7] = GameServer()->Collision(m_Lobby)->Entity(x - 1, y + 1, Layer);

	if(Index >= ENTITY_SPAWN && Index <= ENTITY_SPAWN_BLUE)
	{
		int Type = Index - ENTITY_SPAWN;
		m_aaSpawnPoints[Type][m_aNumSpawnPoints[Type]] = Pos;
		m_aNumSpawnPoints[Type] = minimum(m_aNumSpawnPoints[Type] + 1, (int)std::size(m_aaSpawnPoints[0]));
	}

	else if(Index == ENTITY_DOOR)
	{
		for(int i = 0; i < 8; i++)
		{
			if(sides[i] >= ENTITY_LASER_SHORT && sides[i] <= ENTITY_LASER_LONG)
			{
				new CDoor(
					&GameServer()->m_World[m_Lobby], //GameWorld
					Pos, //Pos
					pi / 4 * i, //Rotation
					32 * 3 + 32 * (sides[i] - ENTITY_LASER_SHORT) * 3, //Length
					Number //Number
				);
			}
		}
	}
	else if(Index == ENTITY_CRAZY_SHOTGUN_EX)
	{
		int Dir;
		if(!Flags)
			Dir = 0;
		else if(Flags == ROTATION_90)
			Dir = 1;
		else if(Flags == ROTATION_180)
			Dir = 2;
		else
			Dir = 3;
		float Deg = Dir * (pi / 2);
		CProjectile *bullet = new CProjectile(
			&GameServer()->m_World[m_Lobby],
			WEAPON_SHOTGUN, //Type
			-1, //Owner
			Pos, //Pos
			vec2(sin(Deg), cos(Deg)), //Dir
			-2, //Span
			true, //Freeze
			true, //Explosive
			0, //Force
			(g_Config.m_SvShotgunBulletSound) ? SOUND_GRENADE_EXPLODE : -1, //SoundImpact
			Layer,
			Number);
		bullet->SetBouncing(2 - (Dir % 2));
	}
	else if(Index == ENTITY_CRAZY_SHOTGUN)
	{
		int Dir;
		if(!Flags)
			Dir = 0;
		else if(Flags == (TILEFLAG_ROTATE))
			Dir = 1;
		else if(Flags == (TILEFLAG_VFLIP | TILEFLAG_HFLIP))
			Dir = 2;
		else
			Dir = 3;
		float Deg = Dir * (pi / 2);
		CProjectile *bullet = new CProjectile(
			&GameServer()->m_World[m_Lobby],
			WEAPON_SHOTGUN, //Type
			-1, //Owner
			Pos, //Pos
			vec2(sin(Deg), cos(Deg)), //Dir
			-2, //Span
			true, //Freeze
			false, //Explosive
			0,
			SOUND_GRENADE_EXPLODE,
			Layer,
			Number);
		bullet->SetBouncing(2 - (Dir % 2));
	}

	int Type = -1;
	int SubType = 0;

	// if(Index == ENTITY_ARMOR_1)
	// 	Type = POWERUP_ARMOR;
	// else if(Index == ENTITY_HEALTH_1)
	// 	Type = POWERUP_HEALTH;
	// else if(Index == ENTITY_WEAPON_SHOTGUN)
	// {
	// 	Type = POWERUP_WEAPON;
	// 	SubType = WEAPON_SHOTGUN;
	// }
	// else if(Index == ENTITY_WEAPON_GRENADE)
	// {
	// 	Type = POWERUP_WEAPON;
	// 	SubType = WEAPON_GRENADE;
	// }
	// else if(Index == ENTITY_WEAPON_LASER)
	// {
	// 	Type = POWERUP_WEAPON;
	// 	SubType = WEAPON_LASER;
	// }
	// else if(Index == ENTITY_POWERUP_NINJA)
	// {
	// 	Type = POWERUP_NINJA;
	// 	SubType = WEAPON_NINJA;
	// }
	// else 
	if(Index >= ENTITY_LASER_FAST_CCW && Index <= ENTITY_LASER_FAST_CW)
	{
		int sides2[8];
		sides2[0] = GameServer()->Collision(m_Lobby)->Entity(x, y + 2, Layer);
		sides2[1] = GameServer()->Collision(m_Lobby)->Entity(x + 2, y + 2, Layer);
		sides2[2] = GameServer()->Collision(m_Lobby)->Entity(x + 2, y, Layer);
		sides2[3] = GameServer()->Collision(m_Lobby)->Entity(x + 2, y - 2, Layer);
		sides2[4] = GameServer()->Collision(m_Lobby)->Entity(x, y - 2, Layer);
		sides2[5] = GameServer()->Collision(m_Lobby)->Entity(x - 2, y - 2, Layer);
		sides2[6] = GameServer()->Collision(m_Lobby)->Entity(x - 2, y, Layer);
		sides2[7] = GameServer()->Collision(m_Lobby)->Entity(x - 2, y + 2, Layer);

		float AngularSpeed = 0.0f;
		int Ind = Index - ENTITY_LASER_STOP;
		int M;
		if(Ind < 0)
		{
			Ind = -Ind;
			M = 1;
		}
		else if(Ind == 0)
			M = 0;
		else
			M = -1;

		if(Ind == 0)
			AngularSpeed = 0.0f;
		else if(Ind == 1)
			AngularSpeed = pi / 360;
		else if(Ind == 2)
			AngularSpeed = pi / 180;
		else if(Ind == 3)
			AngularSpeed = pi / 90;
		AngularSpeed *= M;

		for(int i = 0; i < 8; i++)
		{
			if(sides[i] >= ENTITY_LASER_SHORT && sides[i] <= ENTITY_LASER_LONG)
			{
				CLight *Lgt = new CLight(&GameServer()->m_World[m_Lobby], Pos, pi / 4 * i, 32 * 3 + 32 * (sides[i] - ENTITY_LASER_SHORT) * 3, Layer, Number);
				Lgt->m_AngularSpeed = AngularSpeed;
				if(sides2[i] >= ENTITY_LASER_C_SLOW && sides2[i] <= ENTITY_LASER_C_FAST)
				{
					Lgt->m_Speed = 1 + (sides2[i] - ENTITY_LASER_C_SLOW) * 2;
					Lgt->m_CurveLength = Lgt->m_Length;
				}
				else if(sides2[i] >= ENTITY_LASER_O_SLOW && sides2[i] <= ENTITY_LASER_O_FAST)
				{
					Lgt->m_Speed = 1 + (sides2[i] - ENTITY_LASER_O_SLOW) * 2;
					Lgt->m_CurveLength = 0;
				}
				else
					Lgt->m_CurveLength = Lgt->m_Length;
			}
		}
	}
	else if(Index >= ENTITY_DRAGGER_WEAK && Index <= ENTITY_DRAGGER_STRONG)
	{
		CDraggerTeam(&GameServer()->m_World[m_Lobby], Pos, Index - ENTITY_DRAGGER_WEAK + 1, false, Layer, Number);
	}
	else if(Index >= ENTITY_DRAGGER_WEAK_NW && Index <= ENTITY_DRAGGER_STRONG_NW)
	{
		CDraggerTeam(&GameServer()->m_World[m_Lobby], Pos, Index - ENTITY_DRAGGER_WEAK_NW + 1, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAE)
	{
		new CGun(&GameServer()->m_World[m_Lobby], Pos, false, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAF)
	{
		new CGun(&GameServer()->m_World[m_Lobby], Pos, true, false, Layer, Number);
	}
	else if(Index == ENTITY_PLASMA)
	{
		new CGun(&GameServer()->m_World[m_Lobby], Pos, true, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAU)
	{
		new CGun(&GameServer()->m_World[m_Lobby], Pos, false, false, Layer, Number);
	}

	return false;
}

void IGameController::OnPlayerConnect(CPlayer *pPlayer)
{
	int ClientID = pPlayer->GetCID();
	pPlayer->Respawn();

	if(!Server()->ClientPrevIngame(ClientID))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), pPlayer->GetTeam());
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}
}

void IGameController::OnPlayerNameChange(class CPlayer *pPlayer) {}

void IGameController::OnPlayerDisconnect(class CPlayer *pPlayer, const char *pReason)
{
	pPlayer->OnDisconnect();
	int ClientID = pPlayer->GetCID();
	if(Server()->ClientIngame(ClientID))
	{
		char aBuf[512];
		if(pReason && *pReason)
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game (%s)", Server()->ClientName(ClientID), pReason);
		else
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game", Server()->ClientName(ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf, -1, CGameContext::CHAT_SIX);

		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void IGameController::EndRound()
{
	if(m_Warmup) // game can't end when we are running warmup
		return;
	
	char aBuf[256];
	str_format(aBuf, 256, "Game finished in lobby %i", m_Lobby);

	GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	GameServer()->m_World[m_Lobby].m_Paused = true;
	m_GameOverTick = Server()->Tick();
	m_SuddenDeath = 0;
}

void IGameController::ResetGame()
{
	GameServer()->m_World[m_Lobby].m_ResetRequested = true;
}

const char *IGameController::GetTeamName(int Team)
{
	if(Team == 0)
		return "red team";
	if(Team == 1)
		return "blue team";
	return "spectators";
}

//static bool IsSeparator(char c) { return c == ';' || c == ' ' || c == ',' || c == '\t'; }

void IGameController::StartRound()
{
	ResetGame();

	m_RoundStartTick = Server()->Tick();
	m_SuddenDeath = 0;
	m_GameOverTick = -1;
	GameServer()->m_World[m_Lobby].m_Paused = false;
	m_ForceBalanced = false;
	Server()->DemoRecorder_HandleAutoStart();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d'", m_pGameType, m_GameFlags & GAMEFLAG_TEAMS);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	for(int i = 0; i < MAX_CLIENTS; i++) {
		CPlayer* pPlayer = GameServer()->m_apPlayers[i];
		if(pPlayer)
		{
			const int team = pPlayer->GetTeam();
			if(!g_Config.m_SvSaveServer)
			{
				pPlayer->m_Score = 0;
				pPlayer->m_Deaths = 0;
				pPlayer->m_Kills = 0;
				pPlayer->m_Captures = 0;
				pPlayer->m_FastestCapture = 0;
				pPlayer->m_Suicides = 0;
				pPlayer->m_Touches = 0;
			}
		}
	}

	if(!g_Config.m_SvSaveServer) {
		m_aTeamscore[TEAM_RED] =  0;
		m_aTeamscore[TEAM_BLUE] = 0;
	}
}

void IGameController::ChangeMap(const char *pToMap)
{
	if(!pToMap)
		return;

	for(int i = 0; i < GameServer()->Kernel()->m_AmountMaps; i++)
	{
		if(GameServer()->Kernel()->GetIMap(i) && GameServer()->Kernel()->GetIMap(i)->m_aCurrentMap[0] != 0 &&
			str_comp(GameServer()->Kernel()->GetIMap(i)->m_aMapName, pToMap) == 0)
		{
			GameServer()->m_World[m_Lobby].DeleteAllEntities();
			GameServer()->Layers(m_Lobby)->Init(GameServer()->Kernel(), i);
			GameServer()->Collision(m_Lobby)->Init(GameServer()->Layers(m_Lobby));
			
			for(int i = 0; i < 3; i++)
				m_aNumSpawnPoints[i] = 0;
			
			GameServer()->CreateMapEntities(m_Lobby);
			break;
		}
	}
}

void IGameController::OnReset()
{
	for(auto &pPlayer : GameServer()->m_apPlayers)
		if(pPlayer)
			pPlayer->Respawn();
}

bool IGameController::IsTeamplay() const
{
	return m_GameFlags&GAMEFLAG_TEAMS;
}

int IGameController::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
	pVictim->GetPlayer()->m_Deaths++;
	if(pKiller == pVictim->GetPlayer())
	{
		pVictim->GetPlayer()->m_Score--; // suicide
		pVictim->GetPlayer()->m_Suicides++;
	}
	else
	{
		if(IsTeamplay() && pVictim->GetPlayer()->GetTeam() == pKiller->GetTeam())
		{
			pKiller->m_Score--; // teamkill
		}
		else
		{
			pKiller->m_Score++; // normal kill
			pKiller->m_Kills++;
		}
	}
	pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*0.5f;

	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*3.0f;
	return 0;
}


void IGameController::OnCharacterSpawn(class CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(10);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER);
	pChr->GiveWeapon(WEAPON_GUN);
}

void IGameController::HandleCharacterTiles(CCharacter *pChr, int MapIndex)
{
	// Do nothing by default
}

void IGameController::DoWarmup(int Seconds)
{
	if(Seconds < 0)
		m_Warmup = 0;
	else
		m_Warmup = Seconds * Server()->TickSpeed();
}

bool IGameController::IsForceBalanced()
{
	return false;
}

bool IGameController::CanBeMovedOnBalance(int ClientID)
{
	return true;
}

void IGameController::Tick()
{
	// do warmup
	if(m_Warmup)
	{
		m_Warmup--;
		if(!m_Warmup)
			StartRound();
	}

	if(m_GameOverTick != -1)
	{
		// game over.. wait for restart
		if(Server()->Tick() > m_GameOverTick + Server()->TickSpeed() * 10)
		{
			StartRound();
			m_RoundCount++;
		}
	}

	if(m_FakeWarmup)
	{
		m_FakeWarmup--;
		if(!m_FakeWarmup && GameServer()->m_World[m_Lobby].m_Paused)
		{
			GameServer()->m_World[m_Lobby].m_Paused = false;
			GameServer()->SendChat(-1, CGameContext::CHAT_ALL, "Game started");
		}
	}

	// game is Paused
	if(GameServer()->m_World[m_Lobby].m_Paused)
		++m_RoundStartTick;

	// do team-balancing
	if(IsTeamplay() && m_UnbalancedTick != -1 && Server()->Tick() > m_UnbalancedTick+g_Config.m_SvTeambalanceTime*Server()->TickSpeed()*60)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "Balancing teams");

		int aT[2] = {0,0};
		float aTScore[2] = {0,0};
		float aPScore[MAX_CLIENTS] = {0.0f};
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			{
				aT[GameServer()->m_apPlayers[i]->GetTeam()]++;
				aPScore[i] = GameServer()->m_apPlayers[i]->m_Score*Server()->TickSpeed()*60.0f;
				aTScore[GameServer()->m_apPlayers[i]->GetTeam()] += aPScore[i];
			}
		}

		// are teams unbalanced?
		if(absolute(aT[0]-aT[1]) >= 2)
		{
			int M = (aT[0] > aT[1]) ? 0 : 1;
			int NumBalance = absolute(aT[0]-aT[1]) / 2;

			do
			{
				CPlayer *pP = 0;
				float PD = aTScore[M];
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!GameServer()->m_apPlayers[i] || !CanBeMovedOnBalance(i))
						continue;
					// remember the player who would cause lowest score-difference
					if(GameServer()->m_apPlayers[i]->GetTeam() == M && (!pP || absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i])) < PD))
					{
						pP = GameServer()->m_apPlayers[i];
						PD = absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i]));
					}
				}

				// move the player to the other team
				int Temp = pP->m_LastActionTick;
				pP->SetTeam(M^1);
				pP->m_LastActionTick = Temp;

				pP->Respawn();
				pP->m_ForceBalanced = true;
			} while (--NumBalance);

			m_ForceBalanced = true;
		}
		m_UnbalancedTick = -1;
	}

	// check for inactive players
	if(g_Config.m_SvInactiveKickTime > 0)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
		#ifdef CONF_DEBUG
			if(g_Config.m_DbgDummies)
			{
				if(i >= MAX_CLIENTS-g_Config.m_DbgDummies)
					break;
			}
		#endif
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			{
				if(Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick+g_Config.m_SvInactiveKickTime*Server()->TickSpeed()*60)
				{
					switch(g_Config.m_SvInactiveKick)
					{
					case 0:
						{
							// move player to spectator
							GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 1:
						{
							// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
							int Spectators = 0;
							for(int j = 0; j < MAX_CLIENTS; ++j)
								if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
									++Spectators;
							if(Spectators >= m_SpectatorSlots)
								Server()->Kick(i, "Kicked for inactivity");
							else
								GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 2:
						{
							// kick the player
							Server()->Kick(i, "Kicked for inactivity");
						}
					}
				}
			}
		}
	}

	DoActivityCheck();
}

void IGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
	if(GameServer()->m_World[m_Lobby].m_Paused)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
	pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
	if(m_FakeWarmup)
		pGameInfoObj->m_WarmupTimer = m_FakeWarmup;
	else
		pGameInfoObj->m_WarmupTimer = m_Warmup;

	pGameInfoObj->m_RoundNum = 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount + 1;

	pGameInfoObj->m_ScoreLimit = m_ScoreLimit;
	pGameInfoObj->m_TimeLimit = m_TimeLimit;

	CCharacter *pChr;
	CPlayer *pPlayer = SnappingClient != SERVER_DEMO_CLIENT ? GameServer()->m_apPlayers[SnappingClient] : 0;
	CPlayer *pPlayer2;

	// if(pPlayer && (pPlayer->m_TimerType == CPlayer::TIMERTYPE_GAMETIMER || pPlayer->m_TimerType == CPlayer::TIMERTYPE_GAMETIMER_AND_BROADCAST) && pPlayer->GetClientVersion() >= VERSION_DDNET_GAMETICK)
	// {
	// 	if((pPlayer->GetTeam() == TEAM_SPECTATORS || pPlayer->IsPaused()) && pPlayer->m_SpectatorID != SPEC_FREEVIEW && (pPlayer2 = GameServer()->m_apPlayers[pPlayer->m_SpectatorID]))
	// 	{
	// 		if((pChr = pPlayer2->GetCharacter()) && pChr->m_DDRaceState == DDRACE_STARTED)
	// 		{
	// 			pGameInfoObj->m_WarmupTimer = -pChr->m_StartTime;
	// 			pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_RACETIME;
	// 		}
	// 	}
	// 	else if((pChr = pPlayer->GetCharacter()) && pChr->m_DDRaceState == DDRACE_STARTED)
	// 	{
	// 		pGameInfoObj->m_WarmupTimer = -pChr->m_StartTime;
	// 		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_RACETIME;
	// 	}
	// }

	CNetObj_GameInfoEx *pGameInfoEx = (CNetObj_GameInfoEx *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFOEX, 0, sizeof(CNetObj_GameInfoEx));
	if(!pGameInfoEx)
		return;
	
	pGameInfoEx->m_Flags =
		!GAMEINFOFLAG_TIMESCORE |
		!GAMEINFOFLAG_GAMETYPE_RACE |
		!GAMEINFOFLAG_GAMETYPE_DDRACE |
		!GAMEINFOFLAG_GAMETYPE_DDNET |
		GAMEINFOFLAG_UNLIMITED_AMMO |
		!GAMEINFOFLAG_RACE_RECORD_MESSAGE |
		GAMEINFOFLAG_ALLOW_EYE_WHEEL |
		GAMEINFOFLAG_ALLOW_HOOK_COLL |
		(g_Config.m_SvAllowZoom * GAMEINFOFLAG_ALLOW_ZOOM) |
		!GAMEINFOFLAG_BUG_DDRACE_GHOST |
		!GAMEINFOFLAG_BUG_DDRACE_INPUT |
		!GAMEINFOFLAG_PREDICT_DDRACE |
		GAMEINFOFLAG_PREDICT_DDRACE_TILES |
		GAMEINFOFLAG_ENTITIES_DDNET |
		GAMEINFOFLAG_ENTITIES_DDRACE |
		!GAMEINFOFLAG_ENTITIES_RACE |
		!GAMEINFOFLAG_RACE|
		GAMEINFOFLAG_GAMETYPE_VANILLA |
		GAMEINFOFLAG_PREDICT_VANILLA |
		GAMEINFOFLAG_BUG_VANILLA_BOUNCE;
	// pGameInfoEx->m_Flags =
	// 	GAMEINFOFLAG_TIMESCORE |
	// 	GAMEINFOFLAG_GAMETYPE_RACE |
	// 	GAMEINFOFLAG_GAMETYPE_DDRACE |
	// 	GAMEINFOFLAG_GAMETYPE_DDNET |
	// 	GAMEINFOFLAG_UNLIMITED_AMMO |
	// 	GAMEINFOFLAG_RACE_RECORD_MESSAGE |
	// 	GAMEINFOFLAG_ALLOW_EYE_WHEEL |
	// 	GAMEINFOFLAG_ALLOW_HOOK_COLL |
	// 	GAMEINFOFLAG_ALLOW_ZOOM |
	// 	GAMEINFOFLAG_BUG_DDRACE_GHOST |
	// 	GAMEINFOFLAG_BUG_DDRACE_INPUT |
	// 	GAMEINFOFLAG_PREDICT_DDRACE |
	// 	GAMEINFOFLAG_PREDICT_DDRACE_TILES |
	// 	GAMEINFOFLAG_ENTITIES_DDNET |
	// 	GAMEINFOFLAG_ENTITIES_DDRACE |
	// 	GAMEINFOFLAG_ENTITIES_RACE |
	// 	GAMEINFOFLAG_RACE;
	pGameInfoEx->m_Flags2 = 0;
	pGameInfoEx->m_Version = GAMEINFO_CURVERSION;

	if(Server()->IsSixup(SnappingClient))
	{
		protocol7::CNetObj_GameData *pGameData = static_cast<protocol7::CNetObj_GameData *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATA, 0, sizeof(protocol7::CNetObj_GameData)));
		if(!pGameData)
			return;

		pGameData->m_GameStartTick = m_RoundStartTick;
		pGameData->m_GameStateFlags = 0;
		if(m_GameOverTick != -1)
			pGameData->m_GameStateFlags |= protocol7::GAMESTATEFLAG_GAMEOVER;
		if(m_SuddenDeath)
			pGameData->m_GameStateFlags |= protocol7::GAMESTATEFLAG_SUDDENDEATH;
		if(GameServer()->m_World[m_Lobby].m_Paused)
			pGameData->m_GameStateFlags |= protocol7::GAMESTATEFLAG_PAUSED;

		

		pGameData->m_GameStateEndTick = 0;

		protocol7::CNetObj_GameDataTeam *pTeamData = static_cast<protocol7::CNetObj_GameDataTeam *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATATEAM, 0, sizeof(protocol7::CNetObj_GameDataTeam)));
		if(!pTeamData)
			return;
		pTeamData->m_TeamscoreBlue = m_aTeamscore[TEAM_BLUE];
		pTeamData->m_TeamscoreRed = m_aTeamscore[TEAM_RED];


		protocol7::CNetObj_GameDataFlag *pFlagData = static_cast<protocol7::CNetObj_GameDataFlag *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATAFLAG, 0, sizeof(protocol7::CNetObj_GameDataFlag)));
		if(!pFlagData)
			return;
			if(m_apFlags[TEAM_RED])
		{
			if(m_apFlags[TEAM_RED]->m_AtStand)
				pFlagData->m_FlagCarrierRed = protocol7::FLAG_ATSTAND;
			else if(m_apFlags[TEAM_RED]->m_pCarryingCharacter && m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer())
				pFlagData->m_FlagCarrierRed = m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer()->GetCID();
			else
				pFlagData->m_FlagCarrierRed = protocol7::FLAG_TAKEN;
		}
		else
			pFlagData->m_FlagCarrierRed = protocol7::FLAG_MISSING;
		if(m_apFlags[TEAM_BLUE])
		{
			if(m_apFlags[TEAM_BLUE]->m_AtStand)
				pFlagData->m_FlagCarrierBlue = protocol7::FLAG_ATSTAND;
			else if(m_apFlags[TEAM_BLUE]->m_pCarryingCharacter && m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer())
				pFlagData->m_FlagCarrierBlue = m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer()->GetCID();
			else
				pFlagData->m_FlagCarrierBlue = protocol7::FLAG_TAKEN;
		}
		else
			pFlagData->m_FlagCarrierBlue = protocol7::FLAG_MISSING;
		// protocol7::CNetObj_GameDataRace *pRaceData = static_cast<protocol7::CNetObj_GameDataRace *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATARACE, 0, sizeof(protocol7::CNetObj_GameDataRace)));
		// if(!pRaceData)
		// 	return;

		// pRaceData->m_BestTime = round_to_int(m_CurrentRecord * 1000);
		// pRaceData->m_Precision = 0;
		// pRaceData->m_RaceFlags = protocol7::RACEFLAG_HIDE_KILLMSG | protocol7::RACEFLAG_KEEP_WANTED_WEAPON;
	}

	if(GameServer()->Collision(m_Lobby)->m_pSwitchers)
	{
		int Team = pPlayer && pPlayer->GetCharacter() ? pPlayer->GetCharacter()->Team() : 0;

		if(pPlayer && (pPlayer->GetTeam() == TEAM_SPECTATORS || pPlayer->IsPaused()) && pPlayer->m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[pPlayer->m_SpectatorID] && GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetCharacter())
			Team = GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetCharacter()->Team();

		if(Team == TEAM_SUPER)
			return;

		CNetObj_SwitchState *pSwitchState = static_cast<CNetObj_SwitchState *>(Server()->SnapNewItem(NETOBJTYPE_SWITCHSTATE, Team, sizeof(CNetObj_SwitchState)));
		if(!pSwitchState)
			return;

		pSwitchState->m_NumSwitchers = clamp(GameServer()->Collision(m_Lobby)->m_NumSwitchers, 0, 255);
		pSwitchState->m_Status1 = 0;
		pSwitchState->m_Status2 = 0;
		pSwitchState->m_Status3 = 0;
		pSwitchState->m_Status4 = 0;
		pSwitchState->m_Status5 = 0;
		pSwitchState->m_Status6 = 0;
		pSwitchState->m_Status7 = 0;
		pSwitchState->m_Status8 = 0;

		for(int i = 0; i < pSwitchState->m_NumSwitchers + 1; i++)
		{
			int Status = (int)GameServer()->Collision(m_Lobby)->m_pSwitchers[i].m_Status[Team];

			if(i < 32)
				pSwitchState->m_Status1 |= Status << i;
			else if(i < 64)
				pSwitchState->m_Status2 |= Status << (i - 32);
			else if(i < 96)
				pSwitchState->m_Status3 |= Status << (i - 64);
			else if(i < 128)
				pSwitchState->m_Status4 |= Status << (i - 96);
			else if(i < 160)
				pSwitchState->m_Status5 |= Status << (i - 128);
			else if(i < 192)
				pSwitchState->m_Status6 |= Status << (i - 160);
			else if(i < 224)
				pSwitchState->m_Status7 |= Status << (i - 192);
			else if(i < 256)
				pSwitchState->m_Status8 |= Status << (i - 224);
		}
	}
}

int IGameController::GetAutoTeam(int NotThisID)
{
	// this will force the auto balancer to work overtime as well
#ifdef CONF_DEBUG
	if(g_Config.m_DbgStress)
		return 0;
#endif

	int aNumplayers[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID && GameServer()->GetLobby(i) == m_Lobby)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	int Team = 0;
	if(IsTeamplay())
		Team = aNumplayers[TEAM_RED] > aNumplayers[TEAM_BLUE] ? TEAM_BLUE : TEAM_RED;


	if(CanJoinTeam(Team, NotThisID))
		return Team;
	return -1;
}

bool IGameController::CanJoinTeam(int Team, int NotThisID)
{
	if(Team == TEAM_SPECTATORS || (GameServer()->m_apPlayers[NotThisID]
		&& GameServer()->m_apPlayers[NotThisID]->GetLobby() == m_Lobby && GameServer()->m_apPlayers[NotThisID]->GetTeam() != TEAM_SPECTATORS))
		return true;

	int aNumplayers[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID && GameServer()->GetLobby(i) == m_Lobby)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	return (aNumplayers[0] + aNumplayers[1]) < Server()->MaxClients() - m_SpectatorSlots;
}

bool IGameController::CheckTeamBalance()
{
	if(!IsTeamplay() || !g_Config.m_SvTeambalanceTime)
		return true;

	int aT[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	char aBuf[256];
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		str_format(aBuf, sizeof(aBuf), "Teams are NOT balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		if(m_UnbalancedTick == -1)
			m_UnbalancedTick = Server()->Tick();
		return false;
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Teams are balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		m_UnbalancedTick = -1;
		return true;
	}
}

int IGameController::ClampTeam(int Team)
{
	if(Team < 0)
		return TEAM_SPECTATORS;
	return 0;
}

int64_t IGameController::GetMaskForPlayerWorldEvent(int Asker, int ExceptID)
{
	// Send all world events to everyone by default
	return CmaskAllExceptOne(ExceptID);
}



void IGameController::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	// if(Team == 0) Team = !pPlayer->GetTeam();
	if(Team == pPlayer->GetTeam())
		return;
	
	int aT[2] = {0, 0};
	// if(Team != TEAM_SPECTATORS)
	// {
	// 	for(int i = 0; i < MAX_CLIENTS; i++)
	// 	{
	// 		CPlayer *pP = GameServer()->m_apPlayers[i];
	// 		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
	// 			aT[pP->GetTeam()]++;
	// 	}

	// 	// simulate what would happen if changed team
	// 	aT[Team]++;
	// 	if (pPlayer->GetTeam() != TEAM_SPECTATORS)
	// 		aT[Team^1]--;

	// 	// there is a player-difference of at least 2
	// 	if(absolute(aT[0]-aT[1]) >= 2)
	// 	{
	// 		// player wants to join team with less players
	// 		if (!((aT[0] < aT[1] && Team == TEAM_RED) || (aT[0] > aT[1] && Team == TEAM_BLUE)))
	// 			return;
	// 	}
	// }
	pPlayer->SetTeam(Team);
	int ClientID = pPlayer->GetCID();

	char aBuf[128];
	// DoChatMsg = false;
	if(DoChatMsg)
	{
		str_format(aBuf, sizeof(aBuf), "'%s' joined the %s", Server()->ClientName(ClientID), GetTeamName(Team));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", ClientID, Server()->ClientName(ClientID), Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// OnPlayerInfoChange(pPlayer);
}

bool IGameController::IsFriendlyFire(int ClientID1, int ClientID2)
{
	if(ClientID1 == ClientID2)
		return false;
	
	if(ClientID2 < 0 || ClientID1 < 0)
		return false;

	if(IsTeamplay())
	{
		if(!GameServer()->m_apPlayers[ClientID1] || !GameServer()->m_apPlayers[ClientID2])
			return false;

		if(GameServer()->m_apPlayers[ClientID1]->GetTeam() == GameServer()->m_apPlayers[ClientID2]->GetTeam())
			return true;
	}

	return false;
}