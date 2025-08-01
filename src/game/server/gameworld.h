/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEWORLD_H
#define GAME_SERVER_GAMEWORLD_H

#include <game/gamecore.h>

#include <list>

class CEntity;
class CCharacter;
class CBot;

/*
	Class: Game World
		Tracks all entities in the game. Propagates tick and
		snap calls to all entities.
*/
class CGameWorld
{
public:
	enum
	{
		ENTTYPE_PROJECTILE = 0,
		ENTTYPE_LASER,
		ENTTYPE_PICKUP,
		ENTTYPE_FLAG,
		ENTTYPE_CHARACTER,
		ENTTYPE_BOT,
		NUM_ENTTYPES
	};

private:
	void Reset();
	void RemoveEntities();

	CEntity *m_pNextTraverseEntity = nullptr;

	class CGameContext *m_pGameServer;
	class CConfig *m_pConfig;
	class IServer *m_pServer;

	void UpdatePlayerMaps();

public:
	class CGameContext *GameServer() { return m_pGameServer; }
	class CConfig *Config() { return m_pConfig; }
	class IServer *Server() { return m_pServer; }

	CEntity *m_apFirstEntityTypes[NUM_ENTTYPES];

	//fun options
	bool m_grenade = false;
	bool m_laser = true;
	bool m_hammer = false;
	bool m_lineOfSight = false;

	bool m_ResetRequested;
	bool m_Paused;
	CWorldCore m_Core;

	CGameWorld();
	~CGameWorld();

	void SetGameServer(CGameContext *pGameServer);
	void DeleteAllEntities();

	CEntity *FindFirst(int Type);

	/*
		Function: FindEntities
			Finds entities close to a position and returns them in a list.

		Arguments:
			Pos - Position.
			Radius - How close the entities have to be.
			ppEnts - Pointer to a list that should be filled with the pointers
				to the entities.
			Max - Number of entities that fits into the ents array.
			Type - Type of the entities to find.

		Returns:
			Number of entities found and added to the ents array.
	*/
	int FindEntities(vec2 Pos, float Radius, CEntity **ppEnts, int Max, int Type);

	/*
		Function: InterserctCharacters
			Finds the CCharacters that intersects the line. // made for types lasers=1 and doors=0

		Arguments:
			Pos0 - Start position
			Pos1 - End position
			Radius - How for from the line the CCharacter is allowed to be.
			NewPos - Intersection position
			pNotThis - Entity to ignore intersecting with

		Returns:
			Returns a pointer to the closest hit or NULL of there is no intersection.
	*/
	//class CCharacter *IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, class CEntity *pNotThis = 0);
	class CCharacter *IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, class CCharacter *pNotThis = 0, int CollideWith = -1, class CCharacter *pThisOnly = 0, int tick = -1);
	
	class CBot *IntersectBot(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, class CBot *pNotThis = 0, int CollideWith = -1, class CBot *pThisOnly = 0, int tick = -1);
	/*
		Function: ClosestCharacter
			Finds the closest CCharacter to a specific point.

		Arguments:
			Pos - The center position.
			Radius - How far off the CCharacter is allowed to be
			ppNotThis - Entity to ignore

		Returns:
			Returns a pointer to the closest CCharacter or NULL if no CCharacter is close enough.
	*/
	class CCharacter *ClosestCharacter(vec2 Pos, float Radius, CEntity *ppNotThis);

	/*
		Function: InsertEntity
			Adds an entity to the world.

		Arguments:
			pEntity - Entity to add
	*/
	void InsertEntity(CEntity *pEntity);

	/*
		Function: RemoveEntity
			Removes an entity from the world.

		Arguments:
			pEntity - Entity to remove
	*/
	void RemoveEntity(CEntity *pEntity);

	/*
		Function: Snap
			Calls Snap on all the entities in the world to create
			the snapshot.

		Arguments:
			SnappingClient - ID of the client which snapshot
			is being created.
	*/
	void Snap(int SnappingClient);

	/*
		Function: Tick
			Calls Tick on all the entities in the world to progress
			the world to the next tick.
	*/
	void Tick();

	/*
		Function: SwapClients
			Calls SwapClients on all the entities in the world to ensure that /swap
			command is handled safely.
	*/
	void SwapClients(int Client1, int Client2);

	// DDRace
	void ReleaseHooked(int ClientID);

	/*
		Function: IntersectedCharacters
			Finds all CCharacters that intersect the line.

		Arguments:
			Pos0 - Start position
			Pos1 - End position
			Radius - How for from the line the CCharacter is allowed to be.
			pNotThis - Entity to ignore intersecting with

		Returns:
			Returns list with all Characters on line.
	*/
	std::list<class CCharacter *> IntersectedCharacters(vec2 Pos0, vec2 Pos1, float Radius, class CEntity *pNotThis = 0);
};

#endif
