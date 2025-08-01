/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "entity.h"
#include "gamecontext.h"
#include "player.h"

//////////////////////////////////////////////////
// Entity
//////////////////////////////////////////////////
CEntity::CEntity(CGameWorld *pGameWorld, int ObjType, vec2 Pos, int ProximityRadius)
{
	m_pGameWorld = pGameWorld;

	m_ObjType = ObjType;
	m_Pos = Pos;
	m_ProximityRadius = ProximityRadius;

	m_Lobby = 0;
	if(pGameWorld)
		m_Lobby = pGameWorld->m_Core.m_Lobby;

	m_MarkedForDestroy = false;
	m_ID = Server()->SnapNewID(m_Lobby);

	m_pPrevTypeEntity = 0;
	m_pNextTypeEntity = 0;
}

CEntity::~CEntity()
{
	GameWorld()->RemoveEntity(this);
	Server()->SnapFreeID(m_Lobby, m_ID);
}

bool CEntity::NetworkClipped(int SnappingClient) const
{
	return ::NetworkClipped(m_pGameWorld->GameServer(), SnappingClient, m_Pos);
}

bool CEntity::NetworkClipped(int SnappingClient, vec2 CheckPos) const
{
	return ::NetworkClipped(m_pGameWorld->GameServer(), SnappingClient, CheckPos);
}

bool CEntity::GameLayerClipped(vec2 CheckPos)
{
	return round_to_int(CheckPos.x) / 32 < -200 || round_to_int(CheckPos.x) / 32 > GameServer()->Collision(m_Lobby)->GetWidth() + 200 ||
	       round_to_int(CheckPos.y) / 32 < -200 || round_to_int(CheckPos.y) / 32 > GameServer()->Collision(m_Lobby)->GetHeight() + 200;
}

bool CEntity::GetNearestAirPos(vec2 Pos, vec2 PrevPos, vec2 *pOutPos)
{
	for(int k = 0; k < 16 && GameServer()->Collision(m_Lobby)->CheckPoint(Pos); k++)
	{
		Pos -= normalize(PrevPos - Pos);
	}

	vec2 PosInBlock = vec2(round_to_int(Pos.x) % 32, round_to_int(Pos.y) % 32);
	vec2 BlockCenter = vec2(round_to_int(Pos.x), round_to_int(Pos.y)) - PosInBlock + vec2(16.0f, 16.0f);

	*pOutPos = vec2(BlockCenter.x + (PosInBlock.x < 16 ? -2.0f : 1.0f), Pos.y);
	if(!GameServer()->Collision(m_Lobby)->TestBox(*pOutPos, vec2(28.0f, 28.0f)))
		return true;

	*pOutPos = vec2(Pos.x, BlockCenter.y + (PosInBlock.y < 16 ? -2.0f : 1.0f));
	if(!GameServer()->Collision(m_Lobby)->TestBox(*pOutPos, vec2(28.0f, 28.0f)))
		return true;

	*pOutPos = vec2(BlockCenter.x + (PosInBlock.x < 16 ? -2.0f : 1.0f),
		BlockCenter.y + (PosInBlock.y < 16 ? -2.0f : 1.0f));
	return !GameServer()->Collision(m_Lobby)->TestBox(*pOutPos, vec2(28.0f, 28.0f));
}

bool CEntity::GetNearestAirPosPlayer(vec2 PlayerPos, vec2 *OutPos)
{
	for(int dist = 5; dist >= -1; dist--)
	{
		*OutPos = vec2(PlayerPos.x, PlayerPos.y - dist);
		if(!GameServer()->Collision(m_Lobby)->TestBox(*OutPos, vec2(28.0f, 28.0f)))
		{
			return true;
		}
	}
	return false;
}

bool NetworkClipped(const CGameContext *pGameServer, int SnappingClient, vec2 CheckPos)
{
	if(SnappingClient == SERVER_DEMO_CLIENT || pGameServer->m_apPlayers[SnappingClient]->m_ShowAll)
		return false;

	float dx = pGameServer->m_apPlayers[SnappingClient]->m_ViewPos.x - CheckPos.x;
	if(absolute(dx) > pGameServer->m_apPlayers[SnappingClient]->m_ShowDistance.x)
		return true;

	float dy = pGameServer->m_apPlayers[SnappingClient]->m_ViewPos.y - CheckPos.y;
	return absolute(dy) > pGameServer->m_apPlayers[SnappingClient]->m_ShowDistance.y;
}
