/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "teams.h"
#include "teehistorian.h"
#include <engine/shared/config.h>

#include "entities/character.h"
#include "entities/laser.h"
#include "entities/projectile.h"
#include "player.h"

enum
{
	NUM_CHECKPOINTS = 25,
	TIMESTAMP_STR_LENGTH = 20, // 2019-04-02 19:38:36
};

CGameTeams::CGameTeams(CGameContext *pGameContext) :
	m_pGameContext(pGameContext)
{
	Reset();
}

void CGameTeams::Reset()
{
	m_Core.Reset();
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_TeeStarted[i] = false;
		m_TeeFinished[i] = false;
		m_LastChat[i] = 0;
	}

	for(int i = 0; i < NUM_TEAMS; ++i)
	{
		m_TeamState[i] = TEAMSTATE_EMPTY;
		m_TeamLocked[i] = false;

		m_Invited[i] = 0;
		m_Practice[i] = false;
		m_LastSwap[i] = 0;
		m_TeamSentStartWarning[i] = false;
		m_TeamUnfinishableKillTick[i] = -1;
	}
}

void CGameTeams::ResetRoundState(int Team)
{
	ResetInvited(Team);
	ResetSwitchers(Team);
	m_LastSwap[Team] = 0;

	m_Practice[Team] = false;
	m_TeamUnfinishableKillTick[Team] = -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_Core.Team(i) == Team && GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->m_VotedForPractice = false;
			GameServer()->m_apPlayers[i]->m_SwapTargetsClientID = -1;
		}
	}
}

void CGameTeams::ResetSwitchers(int Team)
{
	if(GameServer()->Collision()->m_NumSwitchers > 0)
	{
		for(int i = 0; i < GameServer()->Collision()->m_NumSwitchers + 1; ++i)
		{
			GameServer()->Collision()->m_pSwitchers[i].m_Status[Team] = GameServer()->Collision()->m_pSwitchers[i].m_Initial;
			GameServer()->Collision()->m_pSwitchers[i].m_EndTick[Team] = 0;
			GameServer()->Collision()->m_pSwitchers[i].m_Type[Team] = TILE_SWITCHOPEN;
		}
	}
}

void CGameTeams::OnCharacterStart(int ClientID)
{
	int Tick = Server()->Tick();
	CCharacter *pStartingChar = Character(ClientID);
	if(!pStartingChar)
		return;
	if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO && pStartingChar->m_DDRaceState == DDRACE_STARTED)
		return;
	if((g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO || m_Core.Team(ClientID) != TEAM_FLOCK) && pStartingChar->m_DDRaceState == DDRACE_FINISHED)
		return;
	if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO &&
		(m_Core.Team(ClientID) == TEAM_FLOCK || m_Core.Team(ClientID) == TEAM_SUPER))
	{
		m_TeeStarted[ClientID] = true;
		pStartingChar->m_DDRaceState = DDRACE_STARTED;
		pStartingChar->m_StartTime = Tick;
		return;
	}
	bool Waiting = false;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_Core.Team(ClientID) != m_Core.Team(i))
			continue;
		CPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer || !pPlayer->IsPlaying())
			continue;
		if(GetDDRaceState(pPlayer) != DDRACE_FINISHED)
			continue;

		Waiting = true;
		pStartingChar->m_DDRaceState = DDRACE_NONE;

		if(m_LastChat[ClientID] + Server()->TickSpeed() + g_Config.m_SvChatDelay < Tick)
		{
			char aBuf[128];
			str_format(
				aBuf,
				sizeof(aBuf),
				"%s has finished and didn't go through start yet, wait for him or join another team.",
				Server()->ClientName(i));
			GameServer()->SendChatTarget(ClientID, aBuf);
			m_LastChat[ClientID] = Tick;
		}
		if(m_LastChat[i] + Server()->TickSpeed() + g_Config.m_SvChatDelay < Tick)
		{
			char aBuf[128];
			str_format(
				aBuf,
				sizeof(aBuf),
				"%s wants to start a new round, kill or walk to start.",
				Server()->ClientName(ClientID));
			GameServer()->SendChatTarget(i, aBuf);
			m_LastChat[i] = Tick;
		}
	}

	if(!Waiting)
	{
		m_TeeStarted[ClientID] = true;
	}

	if(m_TeamState[m_Core.Team(ClientID)] < TEAMSTATE_STARTED && !Waiting)
	{
		ChangeTeamState(m_Core.Team(ClientID), TEAMSTATE_STARTED);
		m_TeamSentStartWarning[m_Core.Team(ClientID)] = false;
		m_TeamUnfinishableKillTick[m_Core.Team(ClientID)] = -1;

		int NumPlayers = Count(m_Core.Team(ClientID));

		char aBuf[512];
		str_format(
			aBuf,
			sizeof(aBuf),
			"Team %d started with %d player%s: ",
			m_Core.Team(ClientID),
			NumPlayers,
			NumPlayers == 1 ? "" : "s");

		bool First = true;

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(m_Core.Team(ClientID) == m_Core.Team(i))
			{
				CPlayer *pPlayer = GetPlayer(i);
				// TODO: THE PROBLEM IS THAT THERE IS NO CHARACTER SO START TIME CAN'T BE SET!
				if(pPlayer && (pPlayer->IsPlaying() || TeamLocked(m_Core.Team(ClientID))))
				{
					SetDDRaceState(pPlayer, DDRACE_STARTED);
					SetStartTime(pPlayer, Tick);

					if(First)
						First = false;
					else
						str_append(aBuf, ", ", sizeof(aBuf));

					str_append(aBuf, GameServer()->Server()->ClientName(i), sizeof(aBuf));
				}
			}
		}

		if(g_Config.m_SvTeam < SV_TEAM_FORCED_SOLO && g_Config.m_SvMaxTeamSize != 2 && g_Config.m_SvPauseable)
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				CPlayer *pPlayer = GetPlayer(i);
				if(m_Core.Team(ClientID) == m_Core.Team(i) && pPlayer && (pPlayer->IsPlaying() || TeamLocked(m_Core.Team(ClientID))))
				{
					GameServer()->SendChatTarget(i, aBuf);
				}
			}
		}
	}
}


void CGameTeams::Tick()
{
	int Now = Server()->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_TeamUnfinishableKillTick[i] == -1 || m_TeamState[i] != TEAMSTATE_STARTED_UNFINISHABLE)
		{
			continue;
		}
		if(Now >= m_TeamUnfinishableKillTick[i])
		{
			if(m_Practice[i])
			{
				m_TeamUnfinishableKillTick[i] = -1;
				continue;
			}
			KillTeam(i, -1);
			GameServer()->SendChatTeam(i, "Your team was killed because it couldn't finish anymore and hasn't entered /practice mode");
		}
	}

	int Frequency = Server()->TickSpeed() * 60;
	int Remainder = Server()->TickSpeed() * 30;
	uint64_t TeamHasWantedStartTime = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacter *pChar = GameServer()->m_apPlayers[i] ? GameServer()->m_apPlayers[i]->GetCharacter() : nullptr;
		int Team = m_Core.Team(i);
		if(!pChar || m_TeamState[Team] != TEAMSTATE_STARTED || m_TeeStarted[i] || m_Practice[m_Core.Team(i)])
		{
			continue;
		}
		if((Now - pChar->m_StartTime) % Frequency == Remainder)
		{
			TeamHasWantedStartTime |= ((uint64_t)1) << m_Core.Team(i);
		}
	}
	TeamHasWantedStartTime &= ~(uint64_t)1;
	if(!TeamHasWantedStartTime)
	{
		return;
	}
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(((TeamHasWantedStartTime >> i) & 1) == 0)
		{
			continue;
		}
		if(Count(i) <= 1)
		{
			continue;
		}
		int NumPlayersNotStarted = 0;
		char aPlayerNames[256];
		aPlayerNames[0] = 0;
		for(int j = 0; j < MAX_CLIENTS; j++)
		{
			if(m_Core.Team(j) == i && !m_TeeStarted[j])
			{
				if(aPlayerNames[0])
				{
					str_append(aPlayerNames, ", ", sizeof(aPlayerNames));
				}
				str_append(aPlayerNames, Server()->ClientName(j), sizeof(aPlayerNames));
				NumPlayersNotStarted += 1;
			}
		}
		if(!aPlayerNames[0])
		{
			continue;
		}
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf),
			"Your team has %d %s not started yet, they need "
			"to touch the start before this team can finish: %s",
			NumPlayersNotStarted,
			NumPlayersNotStarted == 1 ? "player that has" : "players that have",
			aPlayerNames);
		GameServer()->SendChatTeam(i, aBuf);
	}
}

void CGameTeams::CheckTeamFinished(int Team)
{
	if(TeamFinished(Team))
	{
		CPlayer *TeamPlayers[MAX_CLIENTS];
		unsigned int PlayersCount = 0;

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(Team == m_Core.Team(i))
			{
				CPlayer *pPlayer = GetPlayer(i);
				if(pPlayer && pPlayer->IsPlaying())
				{
					m_TeeStarted[i] = false;
					m_TeeFinished[i] = false;

					TeamPlayers[PlayersCount++] = pPlayer;
				}
			}
		}

		if(PlayersCount > 0)
		{
			float Time = (float)(Server()->Tick() - GetStartTime(TeamPlayers[0])) / ((float)Server()->TickSpeed());
			if(Time < 0.000001f)
			{
				return;
			}

			if(m_Practice[Team])
			{
				ChangeTeamState(Team, TEAMSTATE_FINISHED);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf),
					"Your team would've finished in: %d minute(s) %5.2f second(s). Since you had practice mode enabled your rank doesn't count.",
					(int)Time / 60, Time - ((int)Time / 60 * 60));
				GameServer()->SendChatTeam(Team, aBuf);

				for(unsigned int i = 0; i < PlayersCount; ++i)
				{
					SetDDRaceState(TeamPlayers[i], DDRACE_FINISHED);
				}

				return;
			}

			char aTimestamp[TIMESTAMP_STR_LENGTH];
			str_timestamp_format(aTimestamp, sizeof(aTimestamp), FORMAT_SPACE); // 2019-04-02 19:41:58

			ChangeTeamState(Team, TEAMSTATE_FINISHED); // TODO: Make it better
		}
	}
}

const char *CGameTeams::SetCharacterTeam(int ClientID, int Team)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return "Invalid client ID";
	if(Team < 0 || Team >= MAX_CLIENTS + 1)
		return "Invalid team number";
	if(Team != TEAM_SUPER && m_TeamState[Team] > TEAMSTATE_OPEN)
		return "This team started already";
	if(m_Core.Team(ClientID) == Team)
		return "You are in this team already";
	if(!Character(ClientID))
		return "Your character is not valid";
	if(Team == TEAM_SUPER && !Character(ClientID)->m_Super)
		return "You can't join super team if you don't have super rights";
	if(Team != TEAM_SUPER && Character(ClientID)->m_DDRaceState != DDRACE_NONE)
		return "You have started racing already";
	// No cheating through noob filter with practice and then leaving team
	if(m_Practice[m_Core.Team(ClientID)])
		return "You have used practice mode already";

	SetForceCharacterTeam(ClientID, Team);
	return nullptr;
}

void CGameTeams::SetForceCharacterTeam(int ClientID, int Team)
{
	m_TeeStarted[ClientID] = false;
	m_TeeFinished[ClientID] = false;
	int OldTeam = m_Core.Team(ClientID);

	if(Team != OldTeam && (OldTeam != TEAM_FLOCK || g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO) && OldTeam != TEAM_SUPER && m_TeamState[OldTeam] != TEAMSTATE_EMPTY)
	{
		bool NoElseInOldTeam = Count(OldTeam) <= 1;
		if(NoElseInOldTeam)
		{
			m_TeamState[OldTeam] = TEAMSTATE_EMPTY;

			// unlock team when last player leaves
			SetTeamLock(OldTeam, false);
			ResetRoundState(OldTeam);
			// do not reset SaveTeamResult, because it should be logged into teehistorian even if the team leaves
		}
	}

	m_Core.Team(ClientID, Team);

	if(OldTeam != Team)
	{
		for(int LoopClientID = 0; LoopClientID < MAX_CLIENTS; ++LoopClientID)
			if(GetPlayer(LoopClientID))
				SendTeamsState(LoopClientID);

		if(GetPlayer(ClientID))
		{
			GetPlayer(ClientID)->m_VotedForPractice = false;
			GetPlayer(ClientID)->m_SwapTargetsClientID = -1;
		}
	}

	if(Team != TEAM_SUPER && (m_TeamState[Team] == TEAMSTATE_EMPTY || m_TeamLocked[Team]))
	{
		if(!m_TeamLocked[Team])
			ChangeTeamState(Team, TEAMSTATE_OPEN);

		ResetSwitchers(Team);
	}
}

int CGameTeams::Count(int Team) const
{
	if(Team == TEAM_SUPER)
		return -1;

	int Count = 0;

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(m_Core.Team(i) == Team)
			Count++;

	return Count;
}

void CGameTeams::ChangeTeamState(int Team, int State)
{
	m_TeamState[Team] = State;
}

void CGameTeams::KillTeam(int Team, int NewStrongID, int ExceptID)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_Core.Team(i) == Team && GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->m_VotedForPractice = false;
			if(i != ExceptID)
			{
				GameServer()->m_apPlayers[i]->KillCharacter(WEAPON_SELF);
				if(NewStrongID != -1 && i != NewStrongID)
				{
					GameServer()->m_apPlayers[i]->Respawn(true); // spawn the rest of team with weak hook on the killer
				}
			}
		}
	}
}

bool CGameTeams::TeamFinished(int Team)
{
	if(m_TeamState[Team] != TEAMSTATE_STARTED)
	{
		return false;
	}
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(m_Core.Team(i) == Team && !m_TeeFinished[i])
			return false;
	return true;
}

int64_t CGameTeams::TeamMask(int Team, int ExceptID, int Asker)
{
	int64_t Mask = 0;

	if(Team == TEAM_SUPER)
		return 0xffffffffffffffff & ~(1 << ExceptID);

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ExceptID)
			continue; // Explicitly excluded
		if(!GetPlayer(i))
			continue; // Player doesn't exist

		if(!(GetPlayer(i)->GetTeam() == TEAM_SPECTATORS || GetPlayer(i)->IsPaused()))
		{ // Not spectator
			if(i != Asker)
			{ // Actions of other players
				// if(!Character(i))
				// 	continue; // Player is currently dead
				if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_ONLY_TEAM)
				{
					if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
						continue; // In different teams
				}
				else if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_OFF)
				{
					if(m_Core.GetSolo(Asker))
						continue; // When in solo part don't show others
					if(m_Core.GetSolo(i))
						continue; // When in solo part don't show others
					if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
						continue; // In different teams
				}
			} // See everything of yourself
		}
		else if(GetPlayer(i)->m_SpectatorID != SPEC_FREEVIEW)
		{ // Spectating specific player
			if(GetPlayer(i)->m_SpectatorID != Asker)
			{ // Actions of other players
				if(!Character(GetPlayer(i)->m_SpectatorID))
					continue; // Player is currently dead
				if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_ONLY_TEAM)
				{
					if(m_Core.Team(GetPlayer(i)->m_SpectatorID) != Team && m_Core.Team(GetPlayer(i)->m_SpectatorID) != TEAM_SUPER)
						continue; // In different teams
				}
				else if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_OFF)
				{
					if(m_Core.GetSolo(Asker))
						continue; // When in solo part don't show others
					if(m_Core.GetSolo(GetPlayer(i)->m_SpectatorID))
						continue; // When in solo part don't show others
					if(m_Core.Team(GetPlayer(i)->m_SpectatorID) != Team && m_Core.Team(GetPlayer(i)->m_SpectatorID) != TEAM_SUPER)
						continue; // In different teams
				}
			} // See everything of player you're spectating
		}
		else
		{ // Freeview
			if(GetPlayer(i)->m_SpecTeam)
			{ // Show only players in own team when spectating
				if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
					continue; // in different teams
			}
		}

		Mask |= 1LL << i;
	}
	return Mask;
}

void CGameTeams::SendTeamsState(int ClientID)
{
	if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
		return;

	if(!m_pGameContext->m_apPlayers[ClientID])
		return;

	CMsgPacker Msg(NETMSGTYPE_SV_TEAMSSTATE);
	CMsgPacker MsgLegacy(NETMSGTYPE_SV_TEAMSSTATELEGACY);

	for(unsigned i = 0; i < MAX_CLIENTS; i++)
	{
		Msg.AddInt(m_Core.Team(i));
		MsgLegacy.AddInt(m_Core.Team(i));
	}

	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	int ClientVersion = m_pGameContext->m_apPlayers[ClientID]->GetClientVersion();
	if(!Server()->IsSixup(ClientID) && VERSION_DDRACE < ClientVersion && ClientVersion < VERSION_DDNET_MSG_LEGACY)
	{
		Server()->SendMsg(&MsgLegacy, MSGFLAG_VITAL, ClientID);
	}
}

int CGameTeams::GetDDRaceState(CPlayer *Player)
{
	if(!Player)
		return DDRACE_NONE;

	CCharacter *pChar = Player->GetCharacter();
	if(pChar)
		return pChar->m_DDRaceState;
	return DDRACE_NONE;
}

void CGameTeams::SetDDRaceState(CPlayer *Player, int DDRaceState)
{
	if(!Player)
		return;

	CCharacter *pChar = Player->GetCharacter();
	if(pChar)
		pChar->m_DDRaceState = DDRaceState;
}

int CGameTeams::GetStartTime(CPlayer *Player)
{
	if(!Player)
		return 0;

	CCharacter *pChar = Player->GetCharacter();
	if(pChar)
		return pChar->m_StartTime;
	return 0;
}

void CGameTeams::SetStartTime(CPlayer *Player, int StartTime)
{
	if(!Player)
		return;

	CCharacter *pChar = Player->GetCharacter();
	if(pChar)
		pChar->m_StartTime = StartTime;
}

void CGameTeams::SetCpActive(CPlayer *Player, int CpActive)
{
	if(!Player)
		return;

	CCharacter *pChar = Player->GetCharacter();
	if(pChar)
		pChar->m_CpActive = CpActive;
}

float *CGameTeams::GetCpCurrent(CPlayer *Player)
{
	if(!Player)
		return NULL;

	CCharacter *pChar = Player->GetCharacter();
	if(pChar)
		return pChar->m_CpCurrent;
	return NULL;
}

void CGameTeams::RequestTeamSwap(CPlayer *pPlayer, CPlayer *pTargetPlayer, int Team)
{
	if(!pPlayer || !pTargetPlayer)
		return;

	char aBuf[512];
	if(pPlayer->m_SwapTargetsClientID == pTargetPlayer->GetCID())
	{
		str_format(aBuf, sizeof(aBuf),
			"%s has already requested to swap with %s.",
			Server()->ClientName(pPlayer->GetCID()), Server()->ClientName(pTargetPlayer->GetCID()));

		GameServer()->SendChatTeam(Team, aBuf);
		return;
	}

	str_format(aBuf, sizeof(aBuf),
		"%s has requested to swap with %s. Please wait %d seconds then type /swap %s.",
		Server()->ClientName(pPlayer->GetCID()), Server()->ClientName(pTargetPlayer->GetCID()), g_Config.m_SvSaveSwapGamesDelay, Server()->ClientName(pPlayer->GetCID()));

	GameServer()->SendChatTeam(Team, aBuf);

	pPlayer->m_SwapTargetsClientID = pTargetPlayer->GetCID();
	m_LastSwap[Team] = Server()->Tick();
}

void CGameTeams::SwapTeamCharacters(CPlayer *pPlayer, CPlayer *pTargetPlayer, int Team)
{
	if(!pPlayer || !pTargetPlayer)
		return;

	char aBuf[128];

	int Since = (Server()->Tick() - m_LastSwap[Team]) / Server()->TickSpeed();
	if(Since < g_Config.m_SvSaveSwapGamesDelay)
	{
		str_format(aBuf, sizeof(aBuf),
			"You have to wait %d seconds until you can swap.",
			g_Config.m_SvSaveSwapGamesDelay - Since);

		GameServer()->SendChatTeam(Team, aBuf);

		return;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_Core.Team(i) == Team && GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->m_SwapTargetsClientID = -1;
		}
	}

	int TimeoutAfterDelay = g_Config.m_SvSaveSwapGamesDelay + g_Config.m_SvSwapTimeout;
	if(Since >= TimeoutAfterDelay)
	{
		str_format(aBuf, sizeof(aBuf),
			"Your swap request timed out %d seconds ago. Use /swap again to re-initiate it.",
			Since - g_Config.m_SvSwapTimeout);

		GameServer()->SendChatTeam(Team, aBuf);

		return;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_Core.Team(i) == Team && GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->GetCharacter()->ResetHook();
			GameServer()->m_World.ReleaseHooked(i);
		}
	}

	swap(m_TeeStarted[pPlayer->GetCID()], m_TeeStarted[pTargetPlayer->GetCID()]);
	swap(m_TeeFinished[pPlayer->GetCID()], m_TeeFinished[pTargetPlayer->GetCID()]);

	GameServer()->m_World.SwapClients(pPlayer->GetCID(), pTargetPlayer->GetCID());

	str_format(aBuf, sizeof(aBuf),
		"%s has swapped with %s.",
		Server()->ClientName(pPlayer->GetCID()), Server()->ClientName(pTargetPlayer->GetCID()));

	GameServer()->SendChatTeam(Team, aBuf);
}

void CGameTeams::OnCharacterSpawn(int ClientID)
{
	m_Core.SetSolo(ClientID, false);
	int Team = m_Core.Team(ClientID);


	if(m_Core.Team(ClientID) >= TEAM_SUPER || !m_TeamLocked[Team])
	{
		if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO)
			SetForceCharacterTeam(ClientID, TEAM_FLOCK);
		else
			SetForceCharacterTeam(ClientID, ClientID); // initialize team
		CheckTeamFinished(Team);
	}
}

void CGameTeams::OnCharacterDeath(int ClientID, int Weapon)
{
	m_Core.SetSolo(ClientID, false);

	int Team = m_Core.Team(ClientID);
	bool Locked = TeamLocked(Team) && Weapon != WEAPON_GAME;

	if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO && Team != TEAM_SUPER)
	{
		ChangeTeamState(Team, CGameTeams::TEAMSTATE_OPEN);
		ResetRoundState(Team);
	}
	else if(Locked)
	{
		SetForceCharacterTeam(ClientID, Team);

		if(GetTeamState(Team) != TEAMSTATE_OPEN)
		{
			ChangeTeamState(Team, CGameTeams::TEAMSTATE_OPEN);

			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "Everyone in your locked team was killed because '%s' %s.", Server()->ClientName(ClientID), Weapon == WEAPON_SELF ? "killed" : "died");

			m_Practice[Team] = false;

			KillTeam(Team, Weapon == WEAPON_SELF ? ClientID : -1, ClientID);
			if(Count(Team) > 1)
			{
				GameServer()->SendChatTeam(Team, aBuf);
			}
		}
	}
	else
	{
		if(m_TeamState[m_Core.Team(ClientID)] == CGameTeams::TEAMSTATE_STARTED && !m_TeeStarted[ClientID])
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "This team cannot finish anymore because '%s' left the team before hitting the start", Server()->ClientName(ClientID));
			GameServer()->SendChatTeam(Team, aBuf);
			GameServer()->SendChatTeam(Team, "Enter /practice mode to avoid being killed in 60 seconds");

			m_TeamUnfinishableKillTick[Team] = Server()->Tick() + 60 * Server()->TickSpeed();
			ChangeTeamState(Team, CGameTeams::TEAMSTATE_STARTED_UNFINISHABLE);
		}
		SetForceCharacterTeam(ClientID, TEAM_FLOCK);
		CheckTeamFinished(Team);
	}
}

void CGameTeams::SetTeamLock(int Team, bool Lock)
{
	if(Team > TEAM_FLOCK && Team < TEAM_SUPER)
		m_TeamLocked[Team] = Lock;
}

void CGameTeams::ResetInvited(int Team)
{
	m_Invited[Team] = 0;
}

void CGameTeams::SetClientInvited(int Team, int ClientID, bool Invited)
{
	if(Team > TEAM_FLOCK && Team < TEAM_SUPER)
	{
		if(Invited)
			m_Invited[Team] |= 1ULL << ClientID;
		else
			m_Invited[Team] &= ~(1ULL << ClientID);
	}
}

void CGameTeams::KillSavedTeam(int ClientID, int Team)
{
	KillTeam(Team, -1);
}

void CGameTeams::ResetSavedTeam(int ClientID, int Team)
{
	if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
	{
		ChangeTeamState(Team, CGameTeams::TEAMSTATE_OPEN);
		ResetRoundState(Team);
	}
	else
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_Core.Team(i) == Team && GameServer()->m_apPlayers[i])
			{
				SetForceCharacterTeam(i, TEAM_FLOCK);
			}
		}
	}
}

int CGameTeams::GetFirstEmptyTeam() const
{
	for(int i = 1; i < MAX_CLIENTS; i++)
		if(m_TeamState[i] == TEAMSTATE_EMPTY)
			return i;
	return -1;
}
