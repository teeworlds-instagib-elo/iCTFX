/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "layers.h"

CLayers::CLayers()
{
	m_GroupsNum = 0;
	m_GroupsStart = 0;
	m_LayersNum = 0;
	m_LayersStart = 0;
	m_pGameGroup = 0;
	m_pGameLayer = 0;
	m_pMap = 0;

	m_pTeleLayer = 0;
	m_pSpeedupLayer = 0;
	m_pFrontLayer = 0;
	m_pSwitchLayer = 0;
	m_pTuneLayer = 0;
	m_Map = -1;
}

void CLayers::Init(class IKernel *pKernel, int Map)
{
	m_Map = Map;
	m_pMap = pKernel->GetIMap(Map);

	if(!m_pMap)
	{
		m_Map = -1;
		return;
	}

	m_pMap->GetType(MAPITEMTYPE_GROUP, &m_GroupsStart, &m_GroupsNum);
	m_pMap->GetType(MAPITEMTYPE_LAYER, &m_LayersStart, &m_LayersNum);

	m_pTeleLayer = 0;
	m_pSpeedupLayer = 0;
	m_pFrontLayer = 0;
	m_pSwitchLayer = 0;
	m_pTuneLayer = 0;

	for(int g = 0; g < NumGroups(); g++)
	{
		CMapItemGroup *pGroup = GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = GetLayer(pGroup->m_StartLayer + l);

			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTilemap = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);

				if(pTilemap->m_Flags & TILESLAYERFLAG_GAME)
				{
					m_pGameLayer = pTilemap;
					m_pGameGroup = pGroup;

					// make sure the game group has standard settings
					m_pGameGroup->m_OffsetX = 0;
					m_pGameGroup->m_OffsetY = 0;
					m_pGameGroup->m_ParallaxX = 100;
					m_pGameGroup->m_ParallaxY = 100;

					if(m_pGameGroup->m_Version >= 2)
					{
						m_pGameGroup->m_UseClipping = 0;
						m_pGameGroup->m_ClipX = 0;
						m_pGameGroup->m_ClipY = 0;
						m_pGameGroup->m_ClipW = 0;
						m_pGameGroup->m_ClipH = 0;
					}

					//break;
				}
				if(pTilemap->m_Flags & TILESLAYERFLAG_TELE)
				{
					if(pTilemap->m_Version <= 2)
					{
						pTilemap->m_Tele = *((int *)(pTilemap) + 15);
					}
					m_pTeleLayer = pTilemap;
				}
				if(pTilemap->m_Flags & TILESLAYERFLAG_SPEEDUP)
				{
					if(pTilemap->m_Version <= 2)
					{
						pTilemap->m_Speedup = *((int *)(pTilemap) + 16);
					}
					m_pSpeedupLayer = pTilemap;
				}
				if(pTilemap->m_Flags & TILESLAYERFLAG_FRONT)
				{
					if(pTilemap->m_Version <= 2)
					{
						pTilemap->m_Front = *((int *)(pTilemap) + 17);
					}
					m_pFrontLayer = pTilemap;
				}
				if(pTilemap->m_Flags & TILESLAYERFLAG_SWITCH)
				{
					if(pTilemap->m_Version <= 2)
					{
						pTilemap->m_Switch = *((int *)(pTilemap) + 18);
					}
					m_pSwitchLayer = pTilemap;
				}
				if(pTilemap->m_Flags & TILESLAYERFLAG_TUNE)
				{
					if(pTilemap->m_Version <= 2)
					{
						pTilemap->m_Tune = *((int *)(pTilemap) + 19);
					}
					m_pTuneLayer = pTilemap;
				}
			}
		}
	}

	InitTilemapSkip();
}

void CLayers::InitBackground(class IMap *pMap)
{
	if(!pMap)
		return;
	
	m_pMap = pMap;
	m_pMap->GetType(MAPITEMTYPE_GROUP, &m_GroupsStart, &m_GroupsNum);
	m_pMap->GetType(MAPITEMTYPE_LAYER, &m_LayersStart, &m_LayersNum);

	//following is here to prevent crash using standard map as background
	m_pTeleLayer = 0;
	m_pSpeedupLayer = 0;
	m_pFrontLayer = 0;
	m_pSwitchLayer = 0;
	m_pTuneLayer = 0;

	for(int g = 0; g < NumGroups(); g++)
	{
		CMapItemGroup *pGroup = GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = GetLayer(pGroup->m_StartLayer + l);

			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTilemap = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);

				if(pTilemap->m_Flags & TILESLAYERFLAG_GAME)
				{
					m_pGameLayer = pTilemap;
					m_pGameGroup = pGroup;

					// make sure the game group has standard settings
					m_pGameGroup->m_OffsetX = 0;
					m_pGameGroup->m_OffsetY = 0;
					m_pGameGroup->m_ParallaxX = 100;
					m_pGameGroup->m_ParallaxY = 100;

					if(m_pGameGroup->m_Version >= 2)
					{
						m_pGameGroup->m_UseClipping = 0;
						m_pGameGroup->m_ClipX = 0;
						m_pGameGroup->m_ClipY = 0;
						m_pGameGroup->m_ClipW = 0;
						m_pGameGroup->m_ClipH = 0;
					}
					//We don't care about tile layers.
				}
			}
		}
	}

	InitTilemapSkip();
}

void CLayers::InitTilemapSkip()
{
	if(!m_pMap)
		return;
	
	for(int g = 0; g < NumGroups(); g++)
	{
		const CMapItemGroup *pGroup = GetGroup(g);

		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			const CMapItemLayer *pLayer = GetLayer(pGroup->m_StartLayer + l);

			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const CMapItemLayerTilemap *pTilemap = (CMapItemLayerTilemap *)pLayer;
				CTile *pTiles = (CTile *)m_pMap->GetData(pTilemap->m_Data);
				for(int y = 0; y < pTilemap->m_Height; y++)
				{
					for(int x = 1; x < pTilemap->m_Width;)
					{
						int SkippedX;
						for(SkippedX = 1; x + SkippedX < pTilemap->m_Width && SkippedX < 255; SkippedX++)
						{
							if(pTiles[y * pTilemap->m_Width + x + SkippedX].m_Index)
								break;
						}

						pTiles[y * pTilemap->m_Width + x].m_Skip = SkippedX - 1;
						x += SkippedX;
					}
				}
			}
		}
	}
}

CMapItemGroup *CLayers::GetGroup(int Index) const
{
	if(!m_pMap)
		return 0;
	return static_cast<CMapItemGroup *>(m_pMap->GetItem(m_GroupsStart + Index, 0, 0));
}

CMapItemLayer *CLayers::GetLayer(int Index) const
{
	if(!m_pMap)
		return 0;
	return static_cast<CMapItemLayer *>(m_pMap->GetItem(m_LayersStart + Index, 0, 0));
}
