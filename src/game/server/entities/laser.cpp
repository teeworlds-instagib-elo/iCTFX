/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "laser.h"
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>

#include <engine/shared/config.h>
#include <game/server/teams.h>

#include "character.h"
#include "../player.h"

CLaser::CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, CPlayer *pPlayer, int Type) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Owner = pPlayer->GetCID();
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	m_TelePos = vec2(0, 0);
	m_WasTele = false;
	m_Type = Type;
	m_TeleportCancelled = false;
	m_IsBlueTeleport = false;
	m_StartTick = Server()->Tick();

	m_NextPos = m_Pos;
	shot_index = 0;

	m_DidHit = false;

	m_TuneZone = GameServer()->Collision()->IsTune(GameServer()->Collision()->GetMapIndex(m_Pos));
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	m_TeamMask = pOwnerChar ? pOwnerChar->Teams()->TeamMask(pOwnerChar->Team(), -1, m_Owner) : 0;
	m_BelongsToPracticeTeam = pOwnerChar && pOwnerChar->Teams()->IsPractice(pOwnerChar->Team());
	
	if (pPlayer) {
		pPlayer->m_Shots++;
	}

	m_DeathTick = 0;

	GameWorld()->InsertEntity(this);
	DoBounce();
}

CLaser::~CLaser() {
	if (m_Bounces > 0) {
		CPlayer *pOwner = GameServer()->m_apPlayers[m_Owner];
		if (!pOwner) {
			return;
		}

		pOwner->m_Wallshots++;

		if (m_DidHit) {
			pOwner->m_WallshotKills++;
		}
	}
}

bool CLaser::HitCharacter(vec2 From, vec2 To)
{
	

	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit;
	bool pDontHitSelf = g_Config.m_SvOldLaser || (m_Bounces == 0 && !m_WasTele);

	int tick = -1;

	if(GameServer()->m_apPlayers[m_Owner]->m_Rollback && g_Config.m_SvRollback)
		tick = GameServer()->m_apPlayers[m_Owner]->m_LastAckedSnapshot;
	
	shots[shot_index].tick = Server()->Tick();
	shots[shot_index].rollbackTick = tick;
	shots[shot_index].from = From;
	shots[shot_index].to = To;
	shot_index++;

	if(pOwnerChar ? (!(pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_LASER) && m_Type == WEAPON_LASER) || (!(pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_SHOTGUN) && m_Type == WEAPON_SHOTGUN) : g_Config.m_SvHit)
		pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar, m_Owner, nullptr, tick);
	else
		pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar, m_Owner, pOwnerChar, tick);

	if(!pHit || (pHit == pOwnerChar && g_Config.m_SvOldLaser) || (pHit != pOwnerChar && pOwnerChar ? (pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_LASER && m_Type == WEAPON_LASER) || (pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_SHOTGUN && m_Type == WEAPON_SHOTGUN) : !g_Config.m_SvHit))
		return false;
	
	if(pHit->m_pPlayer->m_Rollback)
		return false; //check when rollback is in proper position
	
	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	m_DidHit = true;
	pHit->TakeDamage(vec2(0.f, 0.f), GameServer()->Tuning()->m_LaserDamage, m_Owner, WEAPON_LASER, m_StartTick);
	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0 && m_DeathTick == 0)
	{
		m_DeathTick = Server()->Tick();
		return;
	}

	m_PrevPos = m_Pos;
	m_Pos = m_NextPos;
	vec2 Coltile;

	int Res;
	int z;

	vec2 To = m_Pos + m_Dir * m_Energy;
	vec2 Tele;

	int teleptr = 0;
	if(m_Energy > 0 && GameServer()->Collision()->IntersectLineTeleWeapon(m_Pos, To, &Tele, &To, &teleptr))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			
			if(!teleptr)
				m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			
			if(!teleptr)
				m_Bounces++;
			

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum && !teleptr)
				m_Energy = -1;

			GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE, m_TeamMask);
			m_NextPos = m_Pos;

			if(teleptr)
			{
				m_NextPos = Tele;
				GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE, m_TeamMask);
			}

		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_NextPos = To;
			m_Energy = -1;
		}
	}
}

void CLaser::Reset()
{
	m_MarkedForDestroy = true;
}

void CLaser::Tick()
{
	if(m_DeathTick == 0 && Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
	
	if(m_DeathTick)
		m_MarkedForDestroy = true;
	
	if(!GameServer()->m_apPlayers[m_Owner])
		return;
	
	for(int shot = 0; shot < shot_index; shot++)
	{
		for(int player = 0; player < MAX_CLIENTS; player++)\
		{
			if(!GameServer()->m_apPlayers[player])
				continue;
			
			if(!GameServer()->m_apPlayers[player]->m_Rollback)
				continue;
			
			if(!GameServer()->m_apPlayers[player]->GetCharacter())
				continue;
			
			if(GameServer()->m_apPlayers[player]->m_LastAckedSnapshot < m_DeathTick)
				m_MarkedForDestroy = false;

			if(!GameServer()->m_apPlayers[player]->m_LastAckedSnapshot == shots[shot].tick)
				continue;
			
			//check hit
			vec2 At;

			int tick = -1;

			if(GameServer()->m_apPlayers[m_Owner]->m_Rollback)
			{
				tick = GameServer()->m_apPlayers[m_Owner]->m_LastAckedSnapshot;
			}

			CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);

			CCharacter *pHit = GameServer()->m_World.IntersectCharacter(shots[shot].from, shots[shot].to, 0.f, At, pOwnerChar, -1, GameServer()->m_apPlayers[player]->GetCharacter(), tick);
			if(pHit)
			{
				pHit->TakeDamage(vec2(0.f, 0.f), GameServer()->Tuning()->m_LaserDamage, m_Owner, WEAPON_LASER, m_StartTick);
				m_Energy = -1;
				m_DidHit = true;
				m_MarkedForDestroy = true;
			}
		}
	}
}

void CLaser::TickPaused()
{
	++m_EvalTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, m_From) && g_Config.m_SvAntiZoom)
		return;

	if(Server()->GetClientVersion(SnappingClient) >= VERSION_DDNET_MULTI_LASER)
	{
		CNetObj_DDNetLaser *pObj = static_cast<CNetObj_DDNetLaser *>(Server()->SnapNewItem(NETOBJTYPE_DDNETLASER, GetID(), sizeof(CNetObj_DDNetLaser)));
		if(!pObj)
			return;

		pObj->m_ToX = (int)m_Pos.x;
		pObj->m_ToY = (int)m_Pos.y;
		pObj->m_FromX = (int)m_From.x;
		pObj->m_FromY = (int)m_From.y;
		pObj->m_StartTick = m_EvalTick;
		pObj->m_Owner = m_Owner;
		pObj->m_Type = 0;
		pObj->m_Subtype = 0;
		pObj->m_SwitchNumber = m_Number;
		pObj->m_Flags = 0;
	}else
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)m_Pos.x;
		pObj->m_Y = (int)m_Pos.y;
		pObj->m_FromX = (int)m_From.x;
		pObj->m_FromY = (int)m_From.y;
		pObj->m_StartTick = m_EvalTick;
	}
}

void CLaser::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : m_Owner == Client2 ? Client1 : m_Owner;
}
