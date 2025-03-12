/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "flag.h"
#include <game/server/gamecontext.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <engine/shared/config.h>

CFlag::CFlag(CGameWorld *pGameWorld, int Team)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_FLAG)
{
	m_Team = Team;
	m_ProximityRadius = ms_PhysSize;
	m_pCarryingCharacter = NULL;
	m_GrabTick = 0;

	Reset();
}

void CFlag::Reset()
{
	m_pCarryingCharacter = NULL;
	m_AtStand = 1;
	m_Pos = m_StandPos;
	m_Vel = vec2(0,0);
	m_GrabTick = 0;
	for(int i = 0; i < POSITION_HISTORY; i++)
		m_Positions[i] = m_Pos;
}

void CFlag::TickPaused()
{
	++m_DropTick;
	if(m_GrabTick)
		++m_GrabTick;
}

void CFlag::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
	
	if(g_Config.m_SvLineOfSight && m_pCarryingCharacter != NULL && SnappingClient >= 0 && GameServer()->m_apPlayers[SnappingClient]->GetCharacter() &&
		GameServer()->m_apPlayers[SnappingClient]->GetCharacter() != m_pCarryingCharacter && !GameServer()->CheckSightVisibility(GameServer()->m_apPlayers[SnappingClient]->GetCharacter(), m_Pos, CCharacter::ms_PhysSize, 0))
		return;

	CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, m_Team, sizeof(CNetObj_Flag));
	if(!pFlag)
		return;

	pFlag->m_X = (int)m_Pos.x;
	pFlag->m_Y = (int)m_Pos.y;
	pFlag->m_Team = m_Team;
}

