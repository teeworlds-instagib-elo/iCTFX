/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef GAME_SERVER_ENTITIES_BOT_H
#define GAME_SERVER_ENTITIES_BOT_H

#include <game/server/entity.h>
#include <game/server/entities/character.h>
#include <game/server/gamemodes/DDRace.h>

class CBot : public CEntity
{
public:
	static const int ms_PhysSize = 28;

	CGameControllerDDRace * m_pController;

	float m_Difficulty = 0.2f;

	bool m_Alive;
	int m_RespawnTimer;
	int m_Team;

	int m_ClientID;

	int m_HookedID;

	int m_AvoidCharge;
	int m_Score;

	vec2 m_Vel;
	int m_Chase;
	int m_Chase_Direction;
	vec2 m_TargetPos;
	vec2 m_MoveTo;
	vec2 m_LookAt;
	int m_Waypoint;
	int m_PreviousWaypoint;
	bool m_HasFlag;
	int m_ReloadTimer;
	int m_ShootTimer;
	int m_ShootChargeUp;
	vec2 m_ShootTarget;

	vec2 m_Positions[POSITION_HISTORY];

	CBot(CGameWorld *pGameWorld, CGameControllerDDRace * pController, int Team);
	~CBot();

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

	void Die(int Killer = -1);
};

#endif

