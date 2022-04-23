/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "laser.h"
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>

#include <engine/shared/config.h>
#include <game/server/teams.h>

#include "character.h"

CLaser::CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Type) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	m_TelePos = vec2(0, 0);
	m_WasTele = false;
	m_Type = Type;
	m_TeleportCancelled = false;
	m_IsBlueTeleport = false;
	m_TuneZone = GameServer()->Collision()->IsTune(GameServer()->Collision()->GetMapIndex(m_Pos));
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	m_TeamMask = pOwnerChar ? pOwnerChar->Teams()->TeamMask(pOwnerChar->Team(), -1, m_Owner) : 0;
	m_BelongsToPracticeTeam = pOwnerChar && pOwnerChar->Teams()->IsPractice(pOwnerChar->Team());

	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit;
	bool pDontHitSelf = g_Config.m_SvOldLaser || (m_Bounces == 0 && !m_WasTele);

	if(pOwnerChar ? (!(pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_LASER) && m_Type == WEAPON_LASER) || (!(pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_SHOTGUN) && m_Type == WEAPON_SHOTGUN) : g_Config.m_SvHit)
		pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar, m_Owner);
	else
		pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar, m_Owner, pOwnerChar);

	if(!pHit || (pHit == pOwnerChar && g_Config.m_SvOldLaser) || (pHit != pOwnerChar && pOwnerChar ? (pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_LASER && m_Type == WEAPON_LASER) || (pOwnerChar->m_Hit & CCharacter::DISABLE_HIT_SHOTGUN && m_Type == WEAPON_SHOTGUN) : !g_Config.m_SvHit))
		return false;
	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	pHit->TakeDamage(vec2(0.f, 0.f), GameServer()->Tuning()->m_LaserDamage, m_Owner, WEAPON_LASER);
	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		m_MarkedForDestroy = true;
		return;
	}
	m_PrevPos = m_Pos;
	vec2 Coltile;

	int Res;
	int z;

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
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
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = -1;

			GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE, m_TeamMask);

		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
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
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CLaser::TickPaused()
{
	++m_EvalTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, m_From))
		return;
	CCharacter *OwnerChar = 0;
	if(m_Owner >= 0)
		OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(!OwnerChar)
		return;

	CCharacter *pOwnerChar = 0;
	int64_t TeamMask = -1LL;

	if(m_Owner >= 0)
		pOwnerChar = GameServer()->GetPlayerChar(m_Owner);

	if(pOwnerChar && pOwnerChar->IsAlive())
		TeamMask = pOwnerChar->Teams()->TeamMask(pOwnerChar->Team(), -1, m_Owner);

	if(SnappingClient != SERVER_DEMO_CLIENT && !CmaskIsSet(TeamMask, SnappingClient))
		return;
	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_From.x;
	pObj->m_FromY = (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}

void CLaser::SwapClients(int Client1, int Client2)
{
	m_Owner = m_Owner == Client1 ? Client2 : m_Owner == Client2 ? Client1 : m_Owner;
}
