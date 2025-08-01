/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>
#include <game/version.h>

#include "character.h"
#include "door.h"

CDoor::CDoor(CGameWorld *pGameWorld, vec2 Pos, float Rotation, int Length,
	int Number) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Number = Number;
	m_Pos = Pos;
	m_Length = Length;
	m_Direction = vec2(sin(Rotation), cos(Rotation));
	vec2 To = Pos + normalize(m_Direction) * m_Length;

	GameServer()->Collision(m_Lobby)->IntersectNoLaser(Pos, To, &this->m_To, 0);
	ResetCollision();
	GameWorld()->InsertEntity(this);
}

void CDoor::ResetCollision()
{
	for(int i = 0; i < m_Length - 1; i++)
	{
		vec2 CurrentPos(m_Pos.x + (m_Direction.x * i),
			m_Pos.y + (m_Direction.y * i));
		if(GameServer()->Collision(m_Lobby)->CheckPoint(CurrentPos) || GameServer()->Collision(m_Lobby)->GetTile(m_Pos.x, m_Pos.y) || GameServer()->Collision(m_Lobby)->GetFTile(m_Pos.x, m_Pos.y))
			break;
		else
			GameServer()->Collision(m_Lobby)->SetDCollisionAt(
				m_Pos.x + (m_Direction.x * i),
				m_Pos.y + (m_Direction.y * i), TILE_STOPA, 0 /*Flags*/,
				m_Number);
	}
}

void CDoor::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient, m_Pos) && NetworkClipped(SnappingClient, m_To))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(
		NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));

	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;

	int SnappingClientVersion = SnappingClient != SERVER_DEMO_CLIENT ? GameServer()->GetClientVersion(SnappingClient) : CLIENT_VERSIONNR;

	CNetObj_EntityEx *pEntData = 0;
	if(SnappingClientVersion >= VERSION_DDNET_SWITCH)
		pEntData = static_cast<CNetObj_EntityEx *>(Server()->SnapNewItem(NETOBJTYPE_ENTITYEX, GetID(), sizeof(CNetObj_EntityEx)));

	if(pEntData)
	{
		pEntData->m_SwitchNumber = m_Number;
		pEntData->m_Layer = m_Layer;
		pEntData->m_EntityClass = ENTITYCLASS_DOOR;

		pObj->m_FromX = (int)m_To.x;
		pObj->m_FromY = (int)m_To.y;
		pObj->m_StartTick = 0;
	}
	else
	{
		CCharacter *Char = GameServer()->GetPlayerChar(SnappingClient);

		if(SnappingClient != SERVER_DEMO_CLIENT && (GameServer()->m_apPlayers[SnappingClient]->GetTeam() == TEAM_SPECTATORS || GameServer()->m_apPlayers[SnappingClient]->IsPaused()) && GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID != SPEC_FREEVIEW)
			Char = GameServer()->GetPlayerChar(GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID);

		if(Char && Char->Team() != TEAM_SUPER && Char->IsAlive() && GameServer()->Collision(m_Lobby)->m_NumSwitchers > 0 && GameServer()->Collision(m_Lobby)->m_pSwitchers[m_Number].m_Status[Char->Team()])
		{
			pObj->m_FromX = (int)m_To.x;
			pObj->m_FromY = (int)m_To.y;
		}
		else
		{
			pObj->m_FromX = (int)m_Pos.x;
			pObj->m_FromY = (int)m_Pos.y;
		}
		pObj->m_StartTick = Server()->Tick();
	}
}
