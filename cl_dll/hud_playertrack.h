/***
*
*  hud_playertrack.h
*  Player debug tracker - shared declarations.
*  NEW FILE - place in cl_dll/
*
***/
#pragma once
#if !defined(HUD_PLAYERTRACK_H)
#define HUD_PLAYERTRACK_H

#include "ref_params.h"   // for ref_params_s

#define MAX_TRACKED_PLAYERS 32

// Per-player data written by multiple subsystems each frame.
// Index 0 is unused. Use entity indices 1..MAX_TRACKED_PLAYERS.
struct PlayerTrackInfo
{
	bool  seenThisFrame;        // set true in HUD_AddEntity each frame
	bool  alive;                // false = dead/dormant, skip for targeting
	float origin[3];            // last known entity origin
	float headPos[3];           // world-space head bone (from StudioSetupBones)
	float predictedOrigin[3];   // origin + velocity * frametime
	char  weaponName[64];       // weapon model name without path/.mdl
};

// Indexed [1..MAX_TRACKED_PLAYERS]. Slot 0 unused.
extern PlayerTrackInfo g_trackInfo[MAX_TRACKED_PLAYERS + 1];
extern int  g_iTrackedEnt;   // currently tracked entity index (0 = none)
extern bool g_bMarked;        // true when crosshair was on head bone

// Snapshot of ref_params written at the start of every V_CalcRefdef call.
// Used for manual world-to-screen projection (Xash3D has no pfnWorldToScreen).
extern ref_params_t g_refParams;

// CVARs
extern cvar_t *debug_track_enable;
extern cvar_t *debug_track_silent;
extern cvar_t *debug_track_360;
extern cvar_t *debug_auto_mark;
extern cvar_t *debug_bone_target;
extern cvar_t *debug_predict;
extern cvar_t *visual_box;
extern cvar_t *visual_name;
extern cvar_t *visual_weapon;
extern cvar_t *visual_marker;

// Class declaration - included by hud.h after CHudBase is defined
class CHudPlayerTrack : public CHudBase
{
public:
	int  Init( void );
	int  VidInit( void );
	int  Draw( float flTime );
	void Reset( void );
};

// Called from HUD_Frame in cdll_int.cpp every game frame
void PlayerTrack_Frame( double frametime );

// Returns index of nearest alive player or 0
int  PlayerTrack_FindNearest( void );

#endif // HUD_PLAYERTRACK_H
