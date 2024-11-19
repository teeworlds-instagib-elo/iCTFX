/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef GAME_SERVER_ENTITIES_FLAG_H
#define GAME_SERVER_ENTITIES_FLAG_H

#include <game/server/entity.h>
#include <game/server/entities/character.h>

class CFlag : public CEntity
{
public:
	static const int ms_PhysSize = 14;
	CCharacter *m_pCarryingCharacter;
	vec2 m_Vel;
	vec2 m_StandPos;

	vec2 m_Positions[POSITION_HISTORY];

	int m_Team;
	int m_AtStand;
	int m_DropTick;
	int m_GrabTick;

	CFlag(CGameWorld *pGameWorld, int Team);

	virtual void Reset();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);
};

#endif

