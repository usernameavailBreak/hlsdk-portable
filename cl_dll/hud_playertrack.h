//=========================================================
// hud_playertrack.h
// Shared declarations for the player debug tracker.
// Include this wherever you need cvar access or g_trackInfo.
//=========================================================
#pragma once
#ifndef HUD_PLAYERTRACK_H
#define HUD_PLAYERTRACK_H

#define MAX_TRACKED_PLAYERS 32

// Per-player state filled by multiple subsystems each frame.
struct PlayerTrackInfo
{
    bool  seenThisFrame;       // set true in HUD_AddEntity, cleared in HUD_Frame
    bool  alive;               // false = dead/dormant, skip for tracking
    float origin[3];           // last known entity origin
    float headPos[3];          // world-space head bone (set by StudioDrawPlayer)
    float predictedOrigin[3];  // extrapolated with velocity * frametime
    char  weaponName[64];      // stripped .mdl name (set by StudioDrawPlayer)
};

// Indexed [1..MAX_TRACKED_PLAYERS]. Slot 0 is unused.
extern PlayerTrackInfo g_trackInfo[MAX_TRACKED_PLAYERS + 1];

extern int  g_iTrackedEnt;   // currently tracked entity index; 0 = no target
extern bool g_bMarked;        // true when crosshair aligned to head bone

// ---- CVARs (extern so view.cpp / entity.cpp / StudioRenderer can read them) ----
extern cvar_t *debug_track_enable;   // master switch
extern cvar_t *debug_track_silent;   // server sees aim, screen doesn't snap
extern cvar_t *debug_track_360;      // track in full sphere (no FoV gate)
extern cvar_t *debug_auto_mark;      // set g_bMarked when crosshair on head
extern cvar_t *debug_bone_target;    // which bone index is the "head" (default 7)
extern cvar_t *debug_predict;        // extrapolate position with velocity
extern cvar_t *visual_box;           // draw bounding box around target
extern cvar_t *visual_name;          // draw player name above box
extern cvar_t *visual_weapon;        // draw weapon name below box
extern cvar_t *visual_marker;        // draw "MARKED" text

// Called from CHud::Init() via CHudPlayerTrack::Init()
void PlayerTrack_Frame(double frametime);   // called from HUD_Frame in cdll_int.cpp
int  PlayerTrack_FindNearest();             // returns entity index [1..32] or 0

#endif // HUD_PLAYERTRACK_H
