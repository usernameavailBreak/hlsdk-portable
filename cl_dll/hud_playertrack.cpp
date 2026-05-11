/***
*
*  hud_playertrack.cpp
*  Player debug tracker - full implementation.
*  NEW FILE - place in cl_dll/
*  Add "hud_playertrack.cpp" to the cl_dll source list in CMakeLists.txt
*
***/

#include "hud.h"
#include "cl_util.h"
#include "hud_playertrack.h"
#include "ref_params.h"
#include "event_api.h"
#include "pmtrace.h"
#include "pm_defs.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

//--------------------------------------------------
// Globals
//--------------------------------------------------
PlayerTrackInfo g_trackInfo[MAX_TRACKED_PLAYERS + 1];
int  g_iTrackedEnt = 0;
bool g_bMarked     = false;

// Snapshot of ref_params from the last V_CalcRefdef call.
// Written by view.cpp. Used here for manual world-to-screen projection
// because Xash3D's cl_enginefuncs_s does not expose pfnWorldToScreen.
struct ref_params_s g_refParams;

//--------------------------------------------------
// CVARs
//--------------------------------------------------
cvar_t *debug_track_enable = NULL;
cvar_t *debug_track_silent = NULL;
cvar_t *debug_track_360    = NULL;
cvar_t *debug_auto_mark    = NULL;
cvar_t *debug_bone_target  = NULL;
cvar_t *debug_predict      = NULL;
cvar_t *visual_box         = NULL;
cvar_t *visual_name        = NULL;
cvar_t *visual_weapon      = NULL;
cvar_t *visual_marker      = NULL;

//--------------------------------------------------
// Screen info cache
//--------------------------------------------------
static SCREENINFO g_scrinfo;

//--------------------------------------------------
// Helper: 3-D distance
//--------------------------------------------------
static inline float VecDist3D( const float *a, const float *b )
{
	float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
	return sqrtf( dx*dx + dy*dy + dz*dz );
}

//--------------------------------------------------
// Helper: dot product
//--------------------------------------------------
static inline float Dot3( const float *a, const float *b )
{
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

//--------------------------------------------------
// Helper: line-of-sight check.
// Traces from eye position to the target world point
// using only world geometry (PM_WORLD_ONLY).
// Returns true if there is nothing blocking the path.
//--------------------------------------------------
static bool IsPlayerVisible( const float *toTarget )
{
	// Bail if we haven't gotten a valid frame yet
	if( g_refParams.fov_x <= 0.0f )
		return false;

	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 ); // point hull
	gEngfuncs.pEventAPI->EV_PlayerTrace(
	    (float *)g_refParams.vieworg,  // from: eye position
	    (float *)toTarget,             // to: target head bone
	    PM_WORLD_ONLY,                 // only check world brushes
	    -1,                            // don't skip any specific player
	    &tr );

	// If fraction < ~1 something solid is between us and the target
	return ( tr.fraction >= 0.99f );
}

//--------------------------------------------------
// Helper: manual world-to-screen projection.
// Uses precomputed forward/right/up from g_refParams
// (written by view.cpp at the start of V_CalcRefdef).
// Returns true if the point is in front of the camera.
//--------------------------------------------------
static bool WorldToScreen( const float *world, float &sx, float &sy )
{
	// Bail early if g_refParams hasn't been populated yet (fov_x = 0 on startup)
	if( g_refParams.fov_x <= 0.0f )
		return false;

	float delta[3];
	delta[0] = world[0] - g_refParams.vieworg[0];
	delta[1] = world[1] - g_refParams.vieworg[1];
	delta[2] = world[2] - g_refParams.vieworg[2];

	// ref_params_s has precomputed forward/right/up - use them directly
	float fwd = Dot3( delta, g_refParams.forward );
	if( fwd <= 0.01f ) return false; // point is behind the camera

	float rgt = Dot3( delta, g_refParams.right );
	float upv = Dot3( delta, g_refParams.up );

	float w = (float)g_scrinfo.iWidth;
	float h = (float)g_scrinfo.iHeight;

	float halfW = tanf( g_refParams.fov_x * 0.5f * (float)(M_PI / 180.0) );

	// fov_y may be 0 in some engine states; derive it from fov_x and aspect ratio
	float fov_y = g_refParams.fov_y;
	if( fov_y <= 0.0f )
		fov_y = 2.0f * atan2f( tanf( g_refParams.fov_x * 0.5f * (float)(M_PI / 180.0) ) * h, w ) * (float)(180.0 / M_PI);

	float halfH = tanf( fov_y * 0.5f * (float)(M_PI / 180.0) );

	sx = ( w * 0.5f ) + ( rgt / fwd ) * ( w * 0.5f ) / halfW;
	sy = ( h * 0.5f ) - ( upv / fwd ) * ( h * 0.5f ) / halfH;

	return true;
}

//--------------------------------------------------
// Helper: draw a 1-pixel-thick hollow rectangle
//--------------------------------------------------
static void DrawBoxOutline( int x, int y, int w, int h,
                            int r, int g, int b, int a )
{
	gEngfuncs.pfnFillRGBA( x,     y,     w,   1,   r, g, b, a );
	gEngfuncs.pfnFillRGBA( x,     y+h,   w+1, 1,   r, g, b, a );
	gEngfuncs.pfnFillRGBA( x,     y,     1,   h,   r, g, b, a );
	gEngfuncs.pfnFillRGBA( x+w,   y,     1,   h+1, r, g, b, a );
}

//--------------------------------------------------
// Helper: draw console string horizontally centred.
// Xash3D pfnDrawConsoleStringLen takes (string, *length, *height).
//--------------------------------------------------
static void DrawStringCentred( int cx, int y, const char *str,
                                float r, float g, float b )
{
	int len = 0, height = 0;
	gEngfuncs.pfnDrawConsoleStringLen( (char *)str, &len, &height );
	gEngfuncs.pfnDrawSetTextColor( r, g, b );
	gEngfuncs.pfnDrawConsoleString( cx - len / 2, y, (char *)str );
}

//==================================================
// PlayerTrack_FindNearest
// Returns entity index of closest alive player seen
// this frame, or 0 if none found.
//==================================================
int PlayerTrack_FindNearest( void )
{
	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if( !local ) return 0;

	int   best     = 0;
	float bestDist = 1.0e9f;
	int   maxCl    = gEngfuncs.GetMaxClients();

	for( int i = 1; i <= maxCl && i <= MAX_TRACKED_PLAYERS; i++ )
	{
		if( i == local->index )            continue;
		if( !g_trackInfo[i].alive )        continue;
		if( !g_trackInfo[i].seenThisFrame ) continue;

		// FoV gate: skip players behind us unless debug_track_360 is set.
		if( !( debug_track_360 && debug_track_360->value != 0.0f ) )
		{
			float sx, sy;
			if( !WorldToScreen( g_trackInfo[i].origin, sx, sy ) )
				continue;
			if( sx < 0 || sx > g_scrinfo.iWidth ||
			    sy < 0 || sy > g_scrinfo.iHeight )
				continue;
		}

		// Visibility check: skip players with no line of sight
		if( !IsPlayerVisible( g_trackInfo[i].headPos ) )
			continue;

		float dist = VecDist3D( local->origin, g_trackInfo[i].origin );
		if( dist < bestDist )
		{
			bestDist = dist;
			best     = i;
		}
	}
	return best;
}

//==================================================
// PlayerTrack_Frame
// Main per-frame logic. Called from HUD_Frame().
//==================================================
void PlayerTrack_Frame( double frametime )
{
	g_scrinfo.iSize = sizeof( g_scrinfo );
	gEngfuncs.pfnGetScreenInfo( &g_scrinfo );

	if( !debug_track_enable || debug_track_enable->value == 0.0f )
	{
		g_iTrackedEnt = 0;
		g_bMarked     = false;
		for( int i = 1; i <= MAX_TRACKED_PLAYERS; i++ )
			g_trackInfo[i].seenThisFrame = false;
		return;
	}

	// ----- Death / disappearance / LOS check -----
	if( g_iTrackedEnt > 0 )
	{
		bool lost = !g_trackInfo[g_iTrackedEnt].alive
		         || !g_trackInfo[g_iTrackedEnt].seenThisFrame
		         || !IsPlayerVisible( g_trackInfo[g_iTrackedEnt].headPos );
		if( lost )
		{
			g_iTrackedEnt = 0;
			g_bMarked     = false;
		}
	}

	// ----- Auto-select nearest if no target -----
	if( g_iTrackedEnt == 0 )
	{
		g_iTrackedEnt = PlayerTrack_FindNearest();
		g_bMarked     = false;
	}

	// Reset seenThisFrame; HUD_AddEntity sets it again next render pass
	for( int i = 1; i <= MAX_TRACKED_PLAYERS; i++ )
		g_trackInfo[i].seenThisFrame = false;

	if( g_iTrackedEnt == 0 ) return;

	// ----- Movement prediction -----
	if( debug_predict && debug_predict->value != 0.0f )
	{
		cl_entity_t *ent = gEngfuncs.GetEntityByIndex( g_iTrackedEnt );
		if( ent )
		{
			float ft = (float)frametime;
			g_trackInfo[g_iTrackedEnt].predictedOrigin[0] = ent->origin[0] + ent->curstate.velocity[0] * ft;
			g_trackInfo[g_iTrackedEnt].predictedOrigin[1] = ent->origin[1] + ent->curstate.velocity[1] * ft;
			g_trackInfo[g_iTrackedEnt].predictedOrigin[2] = ent->origin[2] + ent->curstate.velocity[2] * ft;
		}
	}
	else
	{
		memcpy( g_trackInfo[g_iTrackedEnt].predictedOrigin,
		        g_trackInfo[g_iTrackedEnt].origin,
		        sizeof( g_trackInfo[g_iTrackedEnt].origin ) );
	}

	// ----- Auto-mark: flag when crosshair is within 8px of head bone -----
	if( debug_auto_mark && debug_auto_mark->value != 0.0f && !g_bMarked )
	{
		float sx, sy;
		if( WorldToScreen( g_trackInfo[g_iTrackedEnt].headPos, sx, sy ) )
		{
			float cx = g_scrinfo.iWidth  * 0.5f;
			float cy = g_scrinfo.iHeight * 0.5f;
			float dx = sx - cx, dy = sy - cy;
			if( sqrtf( dx*dx + dy*dy ) < 8.0f )
				g_bMarked = true;
		}
	}
}

//==================================================
// CHudPlayerTrack::Init
//==================================================
int CHudPlayerTrack::Init( void )
{
	debug_track_enable = CVAR_CREATE( "debug_track_enable", "0", FCVAR_CLIENTDLL );
	debug_track_silent = CVAR_CREATE( "debug_track_silent", "0", FCVAR_CLIENTDLL );
	debug_track_360    = CVAR_CREATE( "debug_track_360",    "0", FCVAR_CLIENTDLL );
	debug_auto_mark    = CVAR_CREATE( "debug_auto_mark",    "0", FCVAR_CLIENTDLL );
	debug_bone_target  = CVAR_CREATE( "debug_bone_target",  "7", FCVAR_CLIENTDLL );
	debug_predict      = CVAR_CREATE( "debug_predict",      "0", FCVAR_CLIENTDLL );
	visual_box         = CVAR_CREATE( "visual_box",         "1", FCVAR_CLIENTDLL );
	visual_name        = CVAR_CREATE( "visual_name",        "1", FCVAR_CLIENTDLL );
	visual_weapon      = CVAR_CREATE( "visual_weapon",      "1", FCVAR_CLIENTDLL );
	visual_marker      = CVAR_CREATE( "visual_marker",      "1", FCVAR_CLIENTDLL );

	memset( g_trackInfo, 0, sizeof( g_trackInfo ) );
	memset( &g_refParams, 0, sizeof( g_refParams ) );

	g_scrinfo.iSize = sizeof( g_scrinfo );
	gEngfuncs.pfnGetScreenInfo( &g_scrinfo );

	m_iFlags |= HUD_ACTIVE;
	gHUD.AddHudElem( this );
	return 1;
}

//==================================================
// CHudPlayerTrack::VidInit
//==================================================
int CHudPlayerTrack::VidInit( void )
{
	g_scrinfo.iSize = sizeof( g_scrinfo );
	gEngfuncs.pfnGetScreenInfo( &g_scrinfo );
	return 1;
}

//==================================================
// CHudPlayerTrack::Reset
//==================================================
void CHudPlayerTrack::Reset( void )
{
	g_iTrackedEnt = 0;
	g_bMarked     = false;
	memset( g_trackInfo, 0, sizeof( g_trackInfo ) );
}

//==================================================
// CHudPlayerTrack::Draw
//==================================================
int CHudPlayerTrack::Draw( float flTime )
{
	if( !debug_track_enable || debug_track_enable->value == 0.0f )
		return 1;
	if( g_iTrackedEnt <= 0 )
		return 1;

	cl_entity_t *ent = gEngfuncs.GetEntityByIndex( g_iTrackedEnt );
	if( !ent || !ent->model )
		return 1;

	float *origin = ( debug_predict && debug_predict->value != 0.0f )
	                ? g_trackInfo[g_iTrackedEnt].predictedOrigin
	                : g_trackInfo[g_iTrackedEnt].origin;

	// Standard HL hull: 72 tall standing, 36 crouching, +-16 wide
	float standH = ( ent->curstate.usehull == 1 ) ? 36.0f : 72.0f;
	const float HW = 16.0f;

	// Build 8 bounding-box corners
	float corners[8][3];
	static const int sgnX[8] = {-1, 1, 1,-1,-1, 1, 1,-1};
	static const int sgnY[8] = {-1,-1, 1, 1,-1,-1, 1, 1};
	for( int i = 0; i < 8; i++ )
	{
		corners[i][0] = origin[0] + sgnX[i] * HW;
		corners[i][1] = origin[1] + sgnY[i] * HW;
		corners[i][2] = origin[2] + ( i < 4 ? 0.0f : standH );
	}

	// Project all 8 corners; find 2-D screen AABB
	float minX =  1.0e9f, minY =  1.0e9f;
	float maxX = -1.0e9f, maxY = -1.0e9f;
	bool  any   = false;

	for( int i = 0; i < 8; i++ )
	{
		float px, py;
		if( WorldToScreen( corners[i], px, py ) )
		{
			if( px < minX ) minX = px;
			if( py < minY ) minY = py;
			if( px > maxX ) maxX = px;
			if( py > maxY ) maxY = py;
			any = true;
		}
	}

	if( !any ) return 1;

	int bx = (int)minX;
	int by = (int)minY;
	int bw = (int)( maxX - minX );
	int bh = (int)( maxY - minY );
	int cx = bx + bw / 2;

	// ---- visual_box ----
	if( visual_box && visual_box->value != 0.0f )
	{
		int cr = g_bMarked ? 255 : 0;
		int cg = g_bMarked ?   0 : 220;
		int cb = g_bMarked ?   0 : 255;
		DrawBoxOutline( bx, by, bw, bh, cr, cg, cb, 220 );
	}

	// ---- visual_name ----
	if( visual_name && visual_name->value != 0.0f )
	{
		hud_player_info_t pi;
		memset( &pi, 0, sizeof( pi ) );
		gEngfuncs.pfnGetPlayerInfo( g_iTrackedEnt, &pi );
		if( pi.name && pi.name[0] )
			DrawStringCentred( cx, by - 12, pi.name, 1.0f, 1.0f, 1.0f );
	}

	// ---- visual_weapon ----
	if( visual_weapon && visual_weapon->value != 0.0f )
	{
		const char *wpn = g_trackInfo[g_iTrackedEnt].weaponName;
		if( wpn[0] )
			DrawStringCentred( cx, by + bh + 2, wpn, 1.0f, 0.7f, 0.1f );
	}

	// ---- visual_marker ----
	if( visual_marker && visual_marker->value != 0.0f && g_bMarked )
		DrawStringCentred( cx, by - 24, "MARKED", 1.0f, 0.0f, 0.0f );

	return 1;
}
