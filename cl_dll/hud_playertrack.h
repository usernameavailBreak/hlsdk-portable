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

// Indexed [1..MAX_TRACKED_PLAYERS]. Slot 0 is unused.
extern PlayerTrackInfo g_trackInfo[MAX_TRACKED_PLAYERS + 1];
extern int  g_iTrackedEnt;   // currently tracked entity index (0 = none)
extern bool g_bMarked;        // true when crosshair was on head bone

// CVARs - declared extern so entity.cpp, view.cpp, input.cpp can read them
extern cvar_t *debug_track_enable;  // master on/off switch
extern cvar_t *debug_track_silent;  // 1 = server sees aim angles, screen doesn't snap
extern cvar_t *debug_track_360;     // 1 = track regardless of facing direction
extern cvar_t *debug_auto_mark;     // 1 = auto-set MARKED when crosshair on head
extern cvar_t *debug_bone_target;   // bone index used as "head" (default 7)
extern cvar_t *debug_predict;       // 1 = extrapolate with velocity
extern cvar_t *visual_box;          // 1 = draw ESP bounding box
extern cvar_t *visual_name;         // 1 = draw player name above box
extern cvar_t *visual_weapon;       // 1 = draw weapon name below box
extern cvar_t *visual_marker;       // 1 = draw "MARKED" text

// Class declaration - must be included AFTER CHudBase is defined (hud.h handles this)
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
