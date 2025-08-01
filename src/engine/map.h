/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_MAP_H
#define ENGINE_MAP_H

#include "kernel.h"
#include <base/hash.h>

enum
{
	MAX_MAP_LENGTH = 128
};

class IMap : public IInterface
{
	MACRO_INTERFACE("map", 0)
public:
	virtual void *GetData(int Index) = 0;
	virtual int GetDataSize(int Index) = 0;
	virtual void *GetDataSwapped(int Index) = 0;
	virtual void UnloadData(int Index) = 0;
	virtual void *GetItem(int Index, int *Type, int *pID) = 0;
	virtual int GetItemSize(int Index) = 0;
	virtual void GetType(int Type, int *pStart, int *pNum) = 0;
	virtual void *FindItem(int Type, int ID) = 0;
	virtual int NumItems() = 0;

	enum
	{
		MAP_TYPE_SIX = 0,
		MAP_TYPE_SIXUP,
		NUM_MAP_TYPES
	};

	~IMap()
	{
		for(auto &CurrentMapData : m_apCurrentMapData)
		{
			if(CurrentMapData)
				free(CurrentMapData);
		}
	}

	char m_aMapName[IO_MAX_PATH_LENGTH] = {0};
	char m_aCurrentMap[IO_MAX_PATH_LENGTH] = {0};
	SHA256_DIGEST m_aCurrentMapSha256[NUM_MAP_TYPES] = {0};
	unsigned m_aCurrentMapCrc[NUM_MAP_TYPES] = {0};
	unsigned char *m_apCurrentMapData[NUM_MAP_TYPES] = {0};
	unsigned int m_aCurrentMapSize[NUM_MAP_TYPES] = {0};
};

class IEngineMap : public IMap
{
	MACRO_INTERFACE("enginemap", 0)
public:
	virtual bool Load(const char *pMapName) = 0;
	virtual bool IsLoaded() = 0;
	virtual void Unload() = 0;
	virtual SHA256_DIGEST Sha256() = 0;
	virtual unsigned Crc() = 0;
	virtual int MapSize() = 0;
	virtual IOHANDLE File() = 0;
};

extern IEngineMap *CreateEngineMap();

#endif
