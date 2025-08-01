/* copyright (c) 2007 magnus auvinen, see licence.txt for more info */
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/version.h>

#include "character.h"
#include "gun.h"
#include "plasma.h"

//////////////////////////////////////////////////
// CGun
//////////////////////////////////////////////////
CGun::CGun(CGameWorld *pGameWorld, vec2 Pos, bool Freeze, bool Explosive, int Layer, int Number) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Layer = Layer;
	m_Number = Number;
	m_LastFire = Server()->Tick();
	m_Pos = Pos;
	m_EvalTick = Server()->Tick();
	m_Freeze = Freeze;
	m_Explosive = Explosive;

	GameWorld()->InsertEntity(this);
}

void CGun::Fire()
{
	CCharacter *Ents[MAX_CLIENTS];
	int IdInTeam[MAX_CLIENTS];
	int LenInTeam[MAX_CLIENTS];
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		IdInTeam[i] = -1;
		LenInTeam[i] = 0;
	}

	int Num = -1;
	Num = GameServer()->m_World[m_Lobby].FindEntities(m_Pos, g_Config.m_SvPlasmaRange, (CEntity **)Ents, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

	for(int i = 0; i < Num; i++)
	{
		CCharacter *Target = Ents[i];
		if(m_Layer == LAYER_SWITCH && m_Number > 0 && !GameServer()->Collision(m_Lobby)->m_pSwitchers[m_Number].m_Status[0])
			continue;
		int res = GameServer()->Collision(m_Lobby)->IntersectLine(m_Pos, Target->m_Pos, 0, 0);
		if(!res)
		{
			int Len = length(Target->m_Pos - m_Pos);
			if(LenInTeam[0] == 0 || LenInTeam[0] > Len)
			{
				LenInTeam[0] = Len;
				IdInTeam[0] = i;
			}
		}
	}
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(IdInTeam[i] != -1)
		{
			CCharacter *Target = Ents[IdInTeam[i]];
			new CPlasma(&GameServer()->m_World[m_Lobby], m_Pos, normalize(Target->m_Pos - m_Pos), m_Freeze, m_Explosive, i);
			m_LastFire = Server()->Tick();
		}
	}
	for(int i = 0; i < Num; i++)
	{
		CCharacter *Target = Ents[i];
		if(Target->IsAlive() && Target->m_Solo)
		{
			if(IdInTeam[0] != i)
			{
				int res = GameServer()->Collision(m_Lobby)->IntersectLine(m_Pos, Target->m_Pos, 0, 0);
				if(!res)
				{
					new CPlasma(&GameServer()->m_World[m_Lobby], m_Pos, normalize(Target->m_Pos - m_Pos), m_Freeze, m_Explosive, 0);
					m_LastFire = Server()->Tick();
				}
			}
		}
	}
}

void CGun::Reset()
{
	m_MarkedForDestroy = true;
}

void CGun::Tick()
{
	if(Server()->Tick() % int(Server()->TickSpeed() * 0.15f) == 0)
	{
		int Flags;
		m_EvalTick = Server()->Tick();
		int index = GameServer()->Collision(m_Lobby)->IsMover(m_Pos.x, m_Pos.y, &Flags);
		if(index)
		{
			m_Core = GameServer()->Collision(m_Lobby)->CpSpeed(index, Flags);
		}
		m_Pos += m_Core;
	}
	if(g_Config.m_SvPlasmaPerSec > 0 && m_LastFire + Server()->TickSpeed() / g_Config.m_SvPlasmaPerSec <= Server()->Tick())
		Fire();
}

void CGun::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CCharacter *Char = GameServer()->GetPlayerChar(SnappingClient);

	if(SnappingClient != SERVER_DEMO_CLIENT && (GameServer()->m_apPlayers[SnappingClient]->GetTeam() == TEAM_SPECTATORS || GameServer()->m_apPlayers[SnappingClient]->IsPaused()) &&
		GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID != SPEC_FREEVIEW)
		Char = GameServer()->GetPlayerChar(GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID);

	int SnappingClientVersion = SnappingClient != SERVER_DEMO_CLIENT ? GameServer()->GetClientVersion(SnappingClient) : CLIENT_VERSIONNR;

	CNetObj_EntityEx *pEntData = 0;
	if(SnappingClientVersion >= VERSION_DDNET_SWITCH)
		pEntData = static_cast<CNetObj_EntityEx *>(Server()->SnapNewItem(NETOBJTYPE_ENTITYEX, GetID(), sizeof(CNetObj_EntityEx)));

	if(pEntData)
	{
		pEntData->m_SwitchNumber = m_Number;
		pEntData->m_Layer = m_Layer;

		if(m_Explosive && !m_Freeze)
			pEntData->m_EntityClass = ENTITYCLASS_GUN_NORMAL;
		else if(m_Explosive && m_Freeze)
			pEntData->m_EntityClass = ENTITYCLASS_GUN_EXPLOSIVE;
		else if(!m_Explosive && m_Freeze)
			pEntData->m_EntityClass = ENTITYCLASS_GUN_FREEZE;
		else
			pEntData->m_EntityClass = ENTITYCLASS_GUN_UNFREEZE;
	}
	else
	{
		int Tick = (Server()->Tick() % Server()->TickSpeed()) % 11;
		if(Char && Char->IsAlive() && (m_Layer == LAYER_SWITCH && m_Number > 0 && !GameServer()->Collision(m_Lobby)->m_pSwitchers[m_Number].m_Status[Char->Team()]) && (!Tick))
			return;
	}

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));

	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;

	if(pEntData)
		pObj->m_StartTick = 0;
	else
		pObj->m_StartTick = m_EvalTick;
}
