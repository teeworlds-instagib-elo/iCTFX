/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_GAMECORE_H
#define GAME_GAMECORE_H

#include <base/math.h>
#include <base/system.h>

#include <map>
#include <vector>

#include "collision.h"
#include <engine/shared/protocol.h>
#include <game/generated/protocol.h>
#include <math.h>

#include "mapitems.h"
#include "prng.h"
#include "teamscore.h"

class CTuneParam
{
	int m_Value;

public:
	void Set(int v) { m_Value = v; }
	int Get() const { return m_Value; }
	CTuneParam &operator=(int v)
	{
		m_Value = (int)(v * 100.0f);
		return *this;
	}
	CTuneParam &operator=(float v)
	{
		m_Value = (int)(v * 100.0f);
		return *this;
	}
	operator float() const { return m_Value / 100.0f; }
};

class CTuningParams
{
public:
	CTuningParams()
	{
		const float TicksPerSecond = 50.0f;
#define MACRO_TUNING_PARAM(Name, ScriptName, Value, Description) m_##Name.Set((int)(Value * 100.0f));
#include "tuning.h"
#undef MACRO_TUNING_PARAM
	}

	static const char *ms_apNames[];

#define MACRO_TUNING_PARAM(Name, ScriptName, Value, Description) CTuneParam m_##Name;
#include "tuning.h"
#undef MACRO_TUNING_PARAM

	static int Num()
	{
		return sizeof(CTuningParams) / sizeof(int);
	}
	bool Set(int Index, float Value);
	bool Set(const char *pName, float Value);
	bool Get(int Index, float *pValue) const;
	bool Get(const char *pName, float *pValue) const;
};

inline void StrToInts(int *pInts, int Num, const char *pStr)
{
	int Index = 0;
	while(Num)
	{
		char aBuf[4] = {0, 0, 0, 0};
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds" // false positive
#endif
		for(int c = 0; c < 4 && pStr[Index]; c++, Index++)
			aBuf[c] = pStr[Index];
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		*pInts = ((aBuf[0] + 128) << 24) | ((aBuf[1] + 128) << 16) | ((aBuf[2] + 128) << 8) | (aBuf[3] + 128);
		pInts++;
		Num--;
	}

	// null terminate
	pInts[-1] &= 0xffffff00;
}

inline void IntsToStr(const int *pInts, int Num, char *pStr)
{
	while(Num)
	{
		pStr[0] = (((*pInts) >> 24) & 0xff) - 128;
		pStr[1] = (((*pInts) >> 16) & 0xff) - 128;
		pStr[2] = (((*pInts) >> 8) & 0xff) - 128;
		pStr[3] = ((*pInts) & 0xff) - 128;
		pStr += 4;
		pInts++;
		Num--;
	}

#if defined(__GNUC__) && __GNUC__ >= 7
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow" // false positive
#endif
	// null terminate
	pStr[-1] = 0;
#if defined(__GNUC__) && __GNUC__ >= 7
#pragma GCC diagnostic pop
#endif
}

inline vec2 CalcPos(vec2 Pos, vec2 Velocity, float Curvature, float Speed, float Time)
{
	vec2 n;
	Time *= Speed;
	n.x = Pos.x + Velocity.x * Time;
	n.y = Pos.y + Velocity.y * Time + Curvature / 10000 * (Time * Time);
	return n;
}

template<typename T>
inline T SaturatedAdd(T Min, T Max, T Current, T Modifier)
{
	if(Modifier < 0)
	{
		if(Current < Min)
			return Current;
		Current += Modifier;
		if(Current < Min)
			Current = Min;
		return Current;
	}
	else
	{
		if(Current > Max)
			return Current;
		Current += Modifier;
		if(Current > Max)
			Current = Max;
		return Current;
	}
}

float VelocityRamp(float Value, float Start, float Range, float Curvature);

// hooking stuff
enum
{
	HOOK_RETRACTED = -1,
	HOOK_IDLE = 0,
	HOOK_RETRACT_START = 1,
	HOOK_RETRACT_END = 3,
	HOOK_FLYING,
	HOOK_GRABBED,

	COREEVENT_GROUND_JUMP = 0x01,
	COREEVENT_AIR_JUMP = 0x02,
	COREEVENT_HOOK_LAUNCH = 0x04,
	COREEVENT_HOOK_ATTACH_PLAYER = 0x08,
	COREEVENT_HOOK_ATTACH_GROUND = 0x10,
	COREEVENT_HOOK_HIT_NOHOOK = 0x20,
	COREEVENT_HOOK_RETRACT = 0x40,
	//COREEVENT_HOOK_TELE=0x80,
};

// show others values - do not change them
enum
{
	SHOW_OTHERS_NOT_SET = -1, // show others value before it is set
	SHOW_OTHERS_OFF = 0, // show no other players in solo or other teams
	SHOW_OTHERS_ON = 1, // show all other players in solo and other teams
	SHOW_OTHERS_ONLY_TEAM = 2 // show players that are in solo and are in the same team
};

class CWorldCore
{
public:
	CWorldCore()
	{
		mem_zero(m_apCharacters, sizeof(m_apCharacters));
		m_pPrng = 0;
	}

	int RandomOr0(int BelowThis)
	{
		if(BelowThis <= 1 || !m_pPrng)
		{
			return 0;
		}
		// This makes the random number slightly biased if `BelowThis`
		// is not a power of two, but we have decided that this is not
		// significant for DDNet and favored the simple implementation.
		return m_pPrng->RandomBits() % BelowThis;
	}

	CTuningParams m_Tuning[2];
	class CCharacterCore *m_apCharacters[MAX_CLIENTS];
	CPrng *m_pPrng;

	int m_Lobby;
};

class CCharacterCore
{
	friend class CCharacter;
	CCollision *m_pCollision;
	std::map<int, std::vector<vec2>> *m_pTeleOuts;

	class CCharacter * m_pCharacter;

public:
	CWorldCore *m_pWorld;

	vec2 m_Pos;
	vec2 m_Vel;
	bool m_Hook;
	bool m_Collision;

	vec2 m_HookPos;
	vec2 m_HookDir;
	vec2 m_HookTeleBase;
	int m_HookTick;
	int m_HookState;
	int m_HookedPlayer;
	class CBot * m_HookedBot;
	int m_ActiveWeapon;

	bool m_NewHook;

	int m_Jumped;
	int m_JumpedTotal;
	int m_Jumps;

	int m_Direction;
	int m_Angle;
	CNetObj_PlayerInput m_Input;
	vec2 m_OldHookPositions[200];

	int m_TriggeredEvents;

	void Init(CWorldCore *pWorld, CCollision *pCollision, std::map<int, std::vector<vec2>> *pTeleOuts = nullptr, CCharacter * character = nullptr);
	void Reset();
	void Tick(bool UseInput);
	void Move();

	void Read(const CNetObj_CharacterCore *pObjCore);
	void Write(CNetObj_CharacterCore *pObjCore);
	void Quantize();

	// DDRace

	int m_Id;
	bool m_pReset;
	class CCollision *Collision() { return m_pCollision; }

	vec2 m_LastVel;
	int m_Colliding;
	bool m_LeftWall;

	// DDNet Character
	void SetTeleOuts(std::map<int, std::vector<vec2>> *pTeleOuts);
	void ReadDDNet(const CNetObj_DDNetCharacter *pObjDDNet);
	bool m_Solo;
	bool m_Jetpack;
	bool m_NoCollision;
	bool m_EndlessHook;
	bool m_EndlessJump;
	bool m_NoHammerHit;
	bool m_NoGrenadeHit;
	bool m_NoLaserHit;
	bool m_NoShotgunHit;
	bool m_NoHookHit;
	bool m_Super;
	bool m_HasTelegunGun;
	bool m_HasTelegunGrenade;
	bool m_HasTelegunLaser;
	int m_FreezeEnd;
	bool m_DeepFrozen;
	bool m_LiveFrozen;
	CTuningParams m_Tuning;

private:
	int m_MoveRestrictions;
	static bool IsSwitchActiveCb(int Number, void *pUser);
};

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

inline CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i + 1) & INPUT_STATE_MASK;
		if(i & 1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}

#endif
