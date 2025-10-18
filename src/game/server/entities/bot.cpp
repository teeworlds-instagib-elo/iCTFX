/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "bot.h"
#include <game/server/gamecontext.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <engine/shared/config.h>
#include "laser.h"
#include <stdio.h>


CBot::CBot(CGameWorld *pGameWorld, CGameControllerDDRace * pController, int Team)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_BOT)
{
	m_pController = pController;
	m_Team = Team;
	m_ProximityRadius = 28;
	m_RespawnTimer = - rand() % 50;
	Reset();

	m_ClientID = Server()->GetBotID();
}

CBot::~CBot()
{
	Die();
	if(m_ClientID != -1)
		Server()->FreeBotID(m_ClientID);
}

void CBot::Reset()
{
	m_Difficulty = 0.2f;
	m_Alive = false;
	m_HookedID = -1;
}

void CBot::Die(int Killer)
{
	if(m_ClientID >= 0)
		GameServer()->CreateDeath(m_Lobby, m_Pos, m_ClientID, 0);

	if(m_HasFlag && m_pController->m_apFlags[!m_Team])
	{
		GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_DROP);
		m_pController->m_apFlags[!m_Team]->m_AtStand = false;
		m_pController->m_apFlags[!m_Team]->m_BotGrabbed = false;
		m_pController->m_apFlags[!m_Team]->m_DropTick = Server()->Tick();
		m_HasFlag = false;
		m_Pos = vec2(0,0);
	}

	if(Killer >= 0 && m_ClientID >= 0)
	{
		// send the kill message
		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = Killer;
		Msg.m_Victim = m_ClientID;
		Msg.m_Weapon = WEAPON_LASER;
		Msg.m_ModeSpecial = 0;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!Server()->ClientIngame(i) || GameServer()->GetLobby(i) != m_Lobby)
				continue;
			
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}

	if(m_HookedID >= 0)
	{
		if(GameWorld()->m_Core.m_apCharacters[m_HookedID])
		{
			GameWorld()->m_Core.m_apCharacters[m_HookedID]->m_HookedPlayer = -1;
			GameWorld()->m_Core.m_apCharacters[m_HookedID]->m_HookState = HOOK_RETRACTED;
			GameWorld()->m_Core.m_apCharacters[m_HookedID]->m_HookPos = GameWorld()->m_Core.m_apCharacters[m_HookedID]->m_Pos;
		}
	}

	m_Alive = false;
	m_RespawnTimer = 0;
}

void CBot::Tick()
{
	if(m_Difficulty > 1)
		m_Difficulty = 1;
	
	if(m_Difficulty < 0)
		m_Difficulty = 0;
	
	if(!m_pController->m_apFlags[0] || !m_pController->m_apFlags[1])
		return;
	
	if(m_ClientID == -1)
		return;
	
	if(!m_Alive) //spawn
	{
		m_RespawnTimer++;
		if(m_RespawnTimer > Server()->TickSpeed()*0.5)
		{
			vec2 pos;
			m_pController->CanSpawn(m_Team, &pos, 0);
			GameServer()->CreatePlayerSpawn(m_Lobby, pos, 0);

			m_Alive = true;

			m_Chase = !m_Team-2;	//goto enemy team flag

			m_Pos = pos;
			m_Waypoint = -1;
			m_PreviousWaypoint = -1;
			m_ReloadTimer = 0;
			m_ShootTimer = 999;
			m_Chase_Direction = 0;

			for(int i = 0; i < POSITION_HISTORY; i++)
			{
				m_Positions[i] = m_Pos;
			}
		}
		return;
	}

	if(m_Waypoint >= 0)
	{
		//move to waypoint
		m_MoveTo = vec2(m_pController->m_aWaypoints[m_Waypoint].x, m_pController->m_aWaypoints[m_Waypoint].y);
		m_MoveTo += vec2(rand() % 100 * (rand() % 2 ? 1 : -1), rand() % 100 * (rand() % 2 ? 1 : -1)) / 4.0;


		vec2 To = m_TargetPos;
		GameServer()->Collision(m_Lobby)->IntersectLine(m_Pos, m_Pos+normalize(To-m_Pos)*950, 0, &To);

		float distanceTarget = distance(m_TargetPos, m_Pos);
		float distanceSight = distance(To, m_Pos);

		//move straight to target if its straight in sight
		if(distanceTarget < distanceSight)
		{
			m_MoveTo = m_TargetPos;
		}
	}

	m_ReloadTimer++;
	m_ShootTimer++;

	int SvBotReactionTime = mix(g_Config.m_SvBotReactionTime_easy, g_Config.m_SvBotReactionTime_hard, m_Difficulty);
	int SvBotChargeTime = mix(g_Config.m_SvBotChargeTime_easy, g_Config.m_SvBotChargeTime_hard, m_Difficulty);
	int SvBotReactionTimeRandom = mix(g_Config.m_SvBotReactionTimeRandom_easy, g_Config.m_SvBotReactionTimeRandom_hard, m_Difficulty);
	int SvBotAvoidAimCharge = mix(g_Config.m_SvBotAvoidAimCharge_easy, g_Config.m_SvBotAvoidAimCharge_hard, m_Difficulty);

	float speedDiff = sqrtf(clamp(m_Difficulty, 0.1f, 1.0f));
	int SvBotSpeed = mix(g_Config.m_SvBotSpeed_easy, g_Config.m_SvBotSpeed_hard, m_Difficulty);
	int SvBotAimRandom = mix(g_Config.m_SvBotAimRandom_easy, g_Config.m_SvBotAimRandom_hard, m_Difficulty);
	int SvBotAimDistanceFalloff = mix(g_Config.m_SvBotAimDistanceFalloff_easy, g_Config.m_SvBotAimDistanceFalloff_hard, m_Difficulty);

	int shootDelay = SvBotReactionTime;
	
	int avoidAimTime = 10;

	m_LookAt = m_TargetPos;

	bool seeNobody = true;

	if(true)
	{
		CEntity *apEnts[MAX_CLIENTS];
		int Num = GameWorld()->FindEntities(m_Pos, 900, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			CCharacter * pChar = (CCharacter*)apEnts[i];

			float distance1 = distance(m_Pos, apEnts[i]->m_Pos);

			if(distance1 < pChar->ms_PhysSize + ms_PhysSize + 1.5*32)	//push bot away
			{
				m_Vel = (m_Vel + normalize(m_Pos-pChar->m_Pos)*ms_PhysSize)/2.0;
			}

			if(!pChar->m_pPlayer)
				continue;
			
			if(pChar->m_pPlayer->GetTeam() == m_Team)
				continue;
						
			//enemy

			//chase enemy
			if(rand() % 60 == 0)
			{
				m_Chase = pChar->m_Core.m_Id;
			}

			//shoot enemy
			if(m_ShootTimer > shootDelay && m_ReloadTimer > g_Config.m_SvBotReloadTime)
			{
				vec2 To = pChar->m_Pos;

				int delay = shootDelay + (Server()->Tick() - pChar->m_pPlayer->m_LastAckedSnapshot)/3.0;

				if(pChar->m_pPlayer && SvBotReactionTime)
				{
					To.x = pChar->m_pPlayer->m_CoreAheads[(Server()->Tick()-delay) % POSITION_HISTORY].m_X;
					To.y = pChar->m_pPlayer->m_CoreAheads[(Server()->Tick()-delay) % POSITION_HISTORY].m_Y;
				}
				
				GameServer()->Collision(m_Lobby)->IntersectLine(m_Pos, m_Pos+normalize(To-m_Pos)*950, 0, &To);

				float sightDistance = distance(m_Pos, To);

				//we have sight
				if(sightDistance + 5 > distance1)
				{
					seeNobody = false;

					m_LookAt = To;

					if(m_ShootChargeUp + rand() % 3 > SvBotChargeTime)
					{
						m_ShootChargeUp = 0;
						m_ReloadTimer = 0;
						m_ShootTimer = - (rand() % SvBotReactionTimeRandom);
						m_ShootTimer -= shootDelay - delay;	//delay shooting for higher ping players
						m_ShootTarget = To;
					}
				}
			}

			m_AvoidCharge = clamp(m_AvoidCharge, 0, (int)(SvBotAvoidAimCharge*1.2));

			//avoid players aim
			if(pChar->m_ReloadTimer - avoidAimTime <= 0)
			{
				
				vec2 PredPos = m_Pos + normalize(m_MoveTo-m_Pos)*SvBotSpeed*avoidAimTime;
				vec2 Direction = normalize(vec2(pChar->m_Input.m_TargetX, pChar->m_Input.m_TargetY));

				vec2 To = PredPos;
				GameServer()->Collision(m_Lobby)->IntersectLine(pChar->m_Pos, pChar->m_Pos+normalize(To-pChar->m_Pos)*950, 0, &To);
				
				float distancePlayer = distance(pChar->m_Pos, PredPos);
				float distanceSight = distance(pChar->m_Pos, To);

				//player can straight aim to us
				if(distancePlayer < distanceSight && rand() % 12 < 4)
				{

					
					//does player aim straight at us

					vec2 sight = pChar->m_Pos + Direction * distancePlayer;

					//if aim is less than 6 tiles away
					if(distance(sight, PredPos) < 4*32)
					{
						//avoid aim

						m_AvoidCharge += 2;

						if(m_AvoidCharge >= SvBotAvoidAimCharge)
							m_Vel = (m_Vel - normalize(sight-m_Pos)*SvBotSpeed*2) / 2;
					}else
					{
						m_AvoidCharge--;
					}
				}
				else
				{
					m_AvoidCharge--;
				}
			}else
			{
				m_AvoidCharge--;
			}
		}

		//bots
		Num = GameWorld()->FindEntities(m_Pos, 900, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_BOT);
		for(int i = 0; i < Num; i++)
		{
			CBot * pChar = (CBot*)apEnts[i];

			if(pChar == this || !pChar->m_Alive)
				continue;

			float distance1 = distance(m_Pos, apEnts[i]->m_Pos);

			if(distance1 < pChar->ms_PhysSize + ms_PhysSize)	//push bot away
			{
				m_Vel = (m_Vel + normalize(m_Pos-pChar->m_Pos)*ms_PhysSize)/2;
			}

			if(pChar->m_Team == m_Team)
				continue;
						
			//enemy

			//shoot enemy
			if(m_ReloadTimer > g_Config.m_SvBotReloadTime && rand() % 7 == 0)
			{
				vec2 To = pChar->m_Pos;
				
				GameServer()->Collision(m_Lobby)->IntersectLine(m_Pos, m_Pos+normalize(To-m_Pos)*950, 0, &To);

				float sightDistance = distance(m_Pos, To);

				//we have sight
				if(sightDistance > distance1)
				{
					m_ShootTimer += 100;
					m_ShootChargeUp = 0;
					m_ReloadTimer = 0;
					float LaserReach;
					LaserReach = GameServer()->Tuning()->m_LaserReach;

					vec2 dir = normalize(To-m_Pos);
					if(SvBotAimRandom > 1)
						dir += vec2(rand() % SvBotAimRandom * (rand() % 2 == 1 ? 1 : -1), rand() % SvBotAimRandom * (rand() % 2 == 1 ? 1 : -1)) / 100.0;

					CLaser * pLaser = new CLaser(&GameServer()->m_World[m_Lobby], m_Pos, dir, LaserReach, 0, WEAPON_LASER, this, m_Team);
					GameServer()->CreateSound(m_Lobby, m_Pos, SOUND_LASER_FIRE, 0);
				}
			}
		}
	}

	if(!seeNobody)
	{
		m_ShootChargeUp++;
	}
	else
	{
		m_ShootChargeUp--;
		if(m_ShootChargeUp < 0)
			m_ShootChargeUp = 0;
	}
	
	if(m_ShootTimer == shootDelay)
	{
		m_ShootTimer += 100; //make it impossible to accidentlly shoot twice
		float LaserReach;
		LaserReach = GameServer()->Tuning()->m_LaserReach;

		int randomness = SvBotAimRandom + distance(m_ShootTarget, m_Pos)/SvBotAimDistanceFalloff;
		vec2 dir = normalize(m_ShootTarget-m_Pos);
		if(randomness > 1)
			dir += vec2(rand() % randomness * (rand() % 2 == 1 ? 1 : -1), rand() % randomness * (rand() % 2 == 1 ? 1 : -1)) / 100.0;

		CLaser * pLaser = new CLaser(&GameServer()->m_World[m_Lobby], m_Pos, dir, LaserReach, 0, WEAPON_LASER, this, m_Team);
		GameServer()->CreateSound(m_Lobby, m_Pos, SOUND_LASER_FIRE, 0);
	}

	if(m_Waypoint == -1)
	{
		//choose first waypoint

		int lowestScore = 999999;
		int bestWaypoint = -1;

		for(int w = 0; w < MAX_WAYPOINTS; w++)
		{
			vec2 wp = vec2(m_pController->m_aWaypoints[w].x, m_pController->m_aWaypoints[w].y);
			vec2 bp = m_Pos;
			float distance1 = distance(wp, bp);
			int distance2 = distance(wp, m_pController->m_apFlags[!m_Team]->m_Pos);

			int score = distance1*9+distance2;

			vec2 To;
			GameServer()->Collision(m_Lobby)->IntersectLine(m_Pos, wp, &To, nullptr);
			if(distance(To, m_Pos) < distance1) //cant reach waypoint
			{
				score += 15*32;	//make point a less favorable
			}

			if(score < lowestScore)
			{
				lowestScore = score;
				bestWaypoint = w;
			}
		}

		m_Waypoint = bestWaypoint;
		return;
	}

	if(m_HasFlag)
	{
		m_pController->m_apFlags[!m_Team]->m_Pos = m_Pos;
		m_pController->m_apFlags[!m_Team]->m_AtStand = false;
		m_pController->m_apFlags[!m_Team]->m_BotGrabbed = true;
	}

	int distanceWaypoint = distance(vec2(m_pController->m_aWaypoints[m_Waypoint].x, m_pController->m_aWaypoints[m_Waypoint].y), m_Pos);

	for(int f = 0; f < 2; f++)
	{
		if(m_HasFlag && m_Team != f)
			continue;
		
		if(distance(m_Pos, m_pController->m_apFlags[f]->m_Pos) < 32*2+SvBotSpeed)
		{
			//pick up flag
			if(f != m_Team && !m_pController->m_apFlags[f]->m_BotGrabbed && !m_pController->m_apFlags[f]->m_pCarryingCharacter)
			{
				m_HasFlag = true;
				m_pController->m_apFlags[f]->m_AtStand = false;
				m_pController->m_apFlags[f]->m_BotGrabbed = true;
				m_pController->m_apFlags[f]->m_Bot = m_ClientID;

				for(int c = 0; c < MAX_CLIENTS; c++)
				{
					CPlayer *pPlayer = GameServer()->m_apPlayers[c];
					if(!pPlayer)
						continue;

					if(pPlayer->GetTeam() == TEAM_SPECTATORS && pPlayer->m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[pPlayer->m_SpectatorID] && GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetTeam() == f)
						GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_GRAB_EN, c);
					else if(pPlayer->GetTeam() == f)
						GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_GRAB_EN, c);
					else
						GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_GRAB_PL, c);
				}
			}
			else
			{
				if(m_pController->m_apFlags[f]->m_pCarryingCharacter || m_pController->m_apFlags[f]->m_BotGrabbed)
					continue;
				
				if(m_HasFlag && m_pController->m_apFlags[f]->m_AtStand)
				{
					m_pController->m_apFlags[!f]->Reset();
					m_pController->m_aTeamscore[f] += 100;

					GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_CAPTURE);

					m_HasFlag = false;
				}else if(!m_pController->m_apFlags[f]->m_AtStand)
					GameServer()->CreateSoundGlobal(m_Lobby, SOUND_CTF_RETURN);
				m_pController->m_apFlags[f]->Reset();
			}
			
			continue;
		}
	}

	//chase logic
	if(true)
	{
		float dist[2];
		dist[0] = distance(m_Pos, m_pController->m_apFlags[0]->m_Pos);
		dist[1] = distance(m_Pos, m_pController->m_apFlags[1]->m_Pos);

		float closestDistFlag = 9999;
		int closestPlayerFlag = -1;

		CEntity * apEnts[MAX_CLIENTS];
		int num = GameWorld()->FindEntities(m_pController->m_apFlags[m_Team]->m_Pos, 80*32, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

		for(int i = 0; i < num; i++)
		{
			CCharacter * pChar = (CCharacter*)apEnts[i];

			if(!pChar->m_pPlayer)
				continue;
			
			if(pChar->m_pPlayer->GetTeam() == m_Team)
				continue;
			
			float playerDistToFlag = distance(pChar->m_Pos, m_pController->m_apFlags[m_Team]->m_Pos);
			float playerDistToBot = distance(pChar->m_Pos, m_Pos);

			if(playerDistToFlag < closestDistFlag)
			{
				closestDistFlag = playerDistToFlag;
				closestPlayerFlag = i;
			}
		}

		num = GameWorld()->FindEntities(m_pController->m_apFlags[m_Team]->m_Pos, 80*32, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_BOT);
		num = 0;
		for(int i = 0; i < num; i++)
		{
			CBot * pChar = (CBot*)apEnts[i];

			if(!pChar->m_Alive)
				continue;
			
			if(pChar->m_Team == m_Team)
				continue;
			
			float playerDistToFlag = distance(pChar->m_Pos, m_pController->m_apFlags[m_Team]->m_Pos);
			float playerDistToBot = distance(pChar->m_Pos, m_Pos);

			if(playerDistToFlag < closestDistFlag)
			{
				closestDistFlag = playerDistToFlag;
			}
		}

		if(m_Chase >= 0)
		{
			if(!GameServer()->PlayerExists(m_Chase) || !GameServer()->m_apPlayers[m_Chase]->GetCharacter() || rand() % 120 == 0)
				m_Chase = !m_Team-2;
		}

		float distanceChasePlayer = 99999;
		if(m_Chase >= 0 && GameServer()->PlayerExists(m_Chase) && GameServer()->m_apPlayers[m_Chase]->GetCharacter())
		{
			distanceChasePlayer = distance(GameServer()->m_apPlayers[m_Chase]->GetCharacter()->m_Pos, m_Pos);
			m_TargetPos = GameServer()->m_apPlayers[m_Chase]->GetCharacter()->m_Pos;

			m_Chase_Direction = GameServer()->m_apPlayers[m_Chase]->GetCharacter()->m_Input.m_Direction;
		}


		//defend
		//when closer to own flag and flag not at stand
		if(distanceChasePlayer > dist[m_Team] && dist[m_Team] < dist[!m_Team] && ((!m_pController->m_apFlags[m_Team]->m_AtStand && GetID() % 4 != 0) || dist[m_Team] > closestDistFlag) || m_HasFlag ||
			((m_pController->m_apFlags[!m_Team]->m_BotGrabbed && GetID() % 3 != 0) || m_pController->m_apFlags[!m_Team]->m_pCarryingCharacter))
		{
			if(m_Chase != m_Team-2 && rand() % 3 == 0 && closestPlayerFlag >= 0)
			{
				m_Chase = closestPlayerFlag;
			}

			if(rand() % 40 == 0 && closestDistFlag < 70*32 || m_HasFlag)
				m_Chase = m_Team-2;
			
			//cut off flag
			if(!m_pController->m_apFlags[m_Team]->m_AtStand)
				m_Chase_Direction = (!m_Team)*2-1;	//goto enemy team (team 0 is left: direction 0, etc)
		}
		//attack when closer to enemy flag
		else if(distanceChasePlayer > dist[!m_Team])
		{
			m_Chase_Direction = 0;
			if(rand() % 40 == 0)
				m_Chase = !m_Team-2;
		}

		if(m_Chase < 0)
			m_TargetPos = m_pController->m_apFlags[m_Chase+2]->m_Pos;
	}

	if(rand() % 600 == 0)
		m_Waypoint = m_PreviousWaypoint;
	
	if(rand() % 60 == 0 && m_Waypoint >= 0)
	{
		vec2 To;
		GameServer()->Collision(m_Lobby)->IntersectLine(m_Pos, m_MoveTo, &To, nullptr);
		if(distance(To, m_Pos) < distanceWaypoint) //cant reach waypoint
		{
			m_Waypoint = -1;
		}
	}
	
	
	//choose waypoint closest to m_TargetPos
	if(distanceWaypoint < SvBotSpeed+1*32)
	{
		//choose new waypoint
		int curWaypoint = m_Waypoint;

		int lowestScore = 99999;
		int bestWaypoint = m_Waypoint;

		int currentDistance = distance(m_Pos, m_TargetPos);

		for(int w = 0; w < m_pController->m_aWaypoints[curWaypoint].connectionAmount; w++)
		{
			int waypoint = m_pController->m_aWaypoints[curWaypoint].connections[w];

			if(waypoint < 0 || waypoint >= MAX_WAYPOINTS)
				return;

			vec2 wp = vec2(m_pController->m_aWaypoints[waypoint].x, m_pController->m_aWaypoints[waypoint].y);
			int distance1 = distance(wp, m_TargetPos);


			int score = distance1 + m_pController->m_aWaypoints[waypoint].y*0.25;

			if(m_Chase_Direction != 0)
			{
				if(m_Chase_Direction < 0 && wp.x < m_Pos.x)
				{
					score *= 0.8;
				}

				if(m_Chase_Direction > 0 && wp.x > m_Pos.x)
				{
					score *= 0.8;
				}
			}

			if(score < lowestScore && rand() % 4 != 1 || distance1 < 5*32)
			{
				lowestScore = score;
				bestWaypoint = waypoint;
			}

			//make bots choose a more random route
			if(currentDistance > distance1 && rand() % (1+m_pController->m_aWaypoints[m_Waypoint].connectionAmount) == 0)
			{
				lowestScore = score;
				bestWaypoint = waypoint;
			}
			
			for(int c = 0; c < m_pController->m_aWaypoints[waypoint].connectionAmount && c < MAX_WAYPOINT_CONNECTIONS; c++)
			{
				int connection = m_pController->m_aWaypoints[waypoint].connections[c];
				wp = vec2(m_pController->m_aWaypoints[connection].x, m_pController->m_aWaypoints[connection].y);
				distance1 = distance(wp, m_TargetPos);


				score = distance1 + rand()%(20*32) + m_pController->m_aWaypoints[waypoint].y*0.25;

				if(score < lowestScore && rand() % 3 != 1)
				{
					lowestScore = score;
					bestWaypoint = waypoint;
				}
			}
		}

		m_PreviousWaypoint = m_Waypoint;
		m_Waypoint = bestWaypoint;
	}

	vec2 direction = normalize(m_MoveTo - m_Pos);

	float multiplier = 2;
	multiplier /= 1 + abs(m_Pos.x/32 - 100)/10;
	multiplier += m_HasFlag*0.2;
	multiplier += 1;

	if(m_ShootTimer < shootDelay*2 || m_ShootChargeUp < SvBotChargeTime)
		multiplier *= 0.5;

	m_Vel = (m_Vel * 4 + direction*SvBotSpeed*multiplier) / 5.0;
	vec2 normalVel = normalize(m_Vel);

	normalVel.y += sin(Server()->Tick() / 10.0) * 0.1;

	vec2 newVel = vec2(0,0);

	//collision
	for(int i = 0; i < length(m_Vel); i++)
	{
		if(m_HasFlag || true)
		{
			if(normalVel.x > 0 && GameServer()->Collision(m_Lobby)->GetCollisionAt(m_Pos.x + 1, m_Pos.y) == TILE_SOLID)
			{
				normalVel.x = -0.1;
			}

			if(normalVel.x < 0 && GameServer()->Collision(m_Lobby)->GetCollisionAt(m_Pos.x - 1, m_Pos.y) == TILE_SOLID)
			{
				normalVel.x = 0.1;
			}

			if(normalVel.y > 0 && GameServer()->Collision(m_Lobby)->GetCollisionAt(m_Pos.x, m_Pos.y+1) == TILE_SOLID)
			{
				normalVel.y = -0.1;
			}

			if(normalVel.y < 0 && GameServer()->Collision(m_Lobby)->GetCollisionAt(m_Pos.x, m_Pos.y-1) == TILE_SOLID)
			{
				normalVel.y = 0.1;
			}

			if(length(normalVel) > 0.01)
				normalVel = normalize(normalVel);
		}

		m_Pos += normalVel;
		newVel += normalVel;
	}

	m_Vel = newVel;

	if(GameServer()->Collision(m_Lobby)->GetCollisionAt(m_Pos.x, m_Pos.y) == TILE_SOLID)
		Die();

	
	m_Positions[Server()->Tick() % POSITION_HISTORY] = m_Pos;
}

void CBot::Snap(int SnappingClient)
{
	if(m_ClientID == -1)
		return;
	
	// CNetObj_Pickup *pBot = (CNetObj_Pickup *)Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup));
	// pBot->m_X = m_Pos.x;
	// pBot->m_Y = m_Pos.y;
	// pBot->m_Type = m_Team;
	// pBot->m_Subtype = 0;

	// return;

	int id = m_ClientID;
	if(!Server()->Translate(id, SnappingClient))
		return;
	
	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;
	char name[16];
	str_format(name, 16, "Bot %i", id);
	StrToInts(&pClientInfo->m_Name0, 4, name);
	StrToInts(&pClientInfo->m_Clan0, 3, "Bots");
	StrToInts(&pClientInfo->m_Skin0, 6, "Default");
	pClientInfo->m_UseCustomColor = 0;
	pClientInfo->m_Country = 0;
	pClientInfo->m_ColorBody = 0;
	pClientInfo->m_ColorFeet = 0;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, id, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;
	pPlayerInfo->m_ClientID = id;
	pPlayerInfo->m_Local = false;
	pPlayerInfo->m_Team = m_Team;
	pPlayerInfo->m_Score = 0;
	pPlayerInfo->m_Latency = 0;

	if(NetworkClipped(SnappingClient))
		return;

	if(!m_Alive)
		return;

	CNetObj_Character *pChar = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, id, sizeof(CNetObj_Character)));
	if(!pChar)
		return;
	
	mem_zero(pChar, sizeof(CNetObj_Character));

	pChar->m_X = m_Pos.x;
	pChar->m_Y = m_Pos.y;
	pChar->m_Tick = Server()->Tick();
	pChar->m_Weapon = WEAPON_LASER;
	pChar->m_AttackTick = 0;
	pChar->m_AmmoCount = 0;
	pChar->m_HookDx = 0;
	pChar->m_HookDy = 0;
	pChar->m_HookedPlayer = 0;
	pChar->m_Emote = 0;
	pChar->m_HookTick = 0;
	pChar->m_Jumped = 0;

	vec2 dir = normalize(m_LookAt-m_Pos)*5;
	float tmp_angle = atan2f(dir.y, dir.x);
	if(tmp_angle < -(pi / 2.0f))
	{
		pChar->m_Angle = (int)((tmp_angle + (2.0f * pi)) * 256.0f);
	}
	else
	{
		pChar->m_Angle = (int)(tmp_angle * 256.0f);
	}
	pChar->m_HookState = 0;
	pChar->m_VelX = round_to_int(m_Vel.x * 256.0f);
	pChar->m_VelY = round_to_int(m_Vel.y * 256.0f);
	pChar->m_Direction = 0;

	CNetObj_DDNetCharacter *pDDNetCharacter = static_cast<CNetObj_DDNetCharacter *>(Server()->SnapNewItem(NETOBJTYPE_DDNETCHARACTER, id, sizeof(CNetObj_DDNetCharacter)));
	if(!pDDNetCharacter)
		return;

	pDDNetCharacter->m_Flags = 0;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_SOLO;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_SUPER;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_ENDLESS_HOOK;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_COLLISION;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_HOOK;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_ENDLESS_JUMP;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_JETPACK;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_GRENADE_HIT;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_HAMMER_HIT;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_LASER_HIT;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_SHOTGUN_HIT;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_GUN;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_GRENADE;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_LASER;
	if(false)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_LASER;
	if(false)
	{
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_MOVEMENTS;
	}

	pDDNetCharacter->m_FreezeEnd = 0;
	pDDNetCharacter->m_Jumps = 0;
	pDDNetCharacter->m_TeleCheckpoint = 0;
	pDDNetCharacter->m_StrongWeakID = 0;
}

