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

#include <string.h>
#include <math.h>
#include <stdio.h>

//--------------------------------------------------
// Globals
//--------------------------------------------------
PlayerTrackInfo g_trackInfo[MAX_TRACKED_PLAYERS + 1];
int  g_iTrackedEnt = 0;
bool g_bMarked     = false;

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
// Internal screen info cache (refreshed each frame)
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
// Helper: world-to-screen projection.
// Returns true if the point is in front of the camera.
// Fills sx/sy with pixel coordinates.
//--------------------------------------------------
static bool WorldToScreen( const float *world, float &sx, float &sy )
{
	float screen[3];
	// pfnWorldToScreen returns non-zero when the point is BEHIND the camera
	if( gEngfuncs.pfnWorldToScreen( (float *)world, screen ) )
		return false;

	sx = ( 1.0f + screen[0] ) * 0.5f * (float)g_scrinfo.iWidth;
	sy = ( 1.0f - screen[1] ) * 0.5f * (float)g_scrinfo.iHeight;
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
// Helper: draw console text centred at pixel (cx,y)
//--------------------------------------------------
static void DrawStringCentred( int cx, int y, const char *str,
                                float r, float g, float b )
{
	int len = gEngfuncs.pfnDrawConsoleStringLen( (char *)str );
	gEngfuncs.pfnDrawSetTextColor( r, g, b );
	gEngfuncs.pfnDrawConsoleString( cx - len/2, y, (char *)str );
}

//==================================================
// PlayerTrack_FindNearest
// Scans all player slots, returns entity index of
// the closest alive player seen this frame, or 0.
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

		// FoV gate - skip players behind us unless debug_track_360 is set
		if( !( debug_track_360 && debug_track_360->value != 0.0f ) )
		{
			// Project onto screen; skip if not visible
			float sx, sy;
			if( !WorldToScreen( g_trackInfo[i].origin, sx, sy ) )
				continue;
			// Also reject if projected pixel is off-screen
			if( sx < 0 || sx > g_scrinfo.iWidth ||
			    sy < 0 || sy > g_scrinfo.iHeight )
				continue;
		}

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

	// ----- Check if tracked player died or vanished -----
	if( g_iTrackedEnt > 0 )
	{
		if( !g_trackInfo[g_iTrackedEnt].alive ||
		    !g_trackInfo[g_iTrackedEnt].seenThisFrame )
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

	// Reset seenThisFrame now; HUD_AddEntity will set it again next render
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
		        sizeof(g_trackInfo[g_iTrackedEnt].origin) );
	}

	// ----- Auto-mark: crosshair within 8px of projected head bone -----
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
// Registers CVARs and adds element to HUD draw list.
// Called from CHud::Init() in hud.cpp.
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

	memset( g_trackInfo, 0, sizeof(g_trackInfo) );

	g_scrinfo.iSize = sizeof(g_scrinfo);
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
	g_scrinfo.iSize = sizeof(g_scrinfo);
	gEngfuncs.pfnGetScreenInfo( &g_scrinfo );
	return 1;
}

//==================================================
// CHudPlayerTrack::Reset
// Called on map change / reconnect.
//==================================================
void CHudPlayerTrack::Reset( void )
{
	g_iTrackedEnt = 0;
	g_bMarked     = false;
	memset( g_trackInfo, 0, sizeof(g_trackInfo) );
}

//==================================================
// CHudPlayerTrack::Draw
// Renders box / name / weapon / MARKED overlay.
// Called by CHud::Redraw every rendered frame.
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

	// Use predicted or real origin
	float *origin = ( debug_predict && debug_predict->value != 0.0f )
	                ? g_trackInfo[g_iTrackedEnt].predictedOrigin
	                : g_trackInfo[g_iTrackedEnt].origin;

	// Standard HL player hull: 72 units tall standing, 36 crouching, ±16 wide
	float standH = ( ent->curstate.usehull == 1 ) ? 36.0f : 72.0f;
	const float HW = 16.0f;

	// Build 8 bounding box corners
	float corners[8][3];
	static const int sx2[8] = {-1, 1, 1,-1,-1, 1, 1,-1};
	static const int sy2[8] = {-1,-1, 1, 1,-1,-1, 1, 1};
	for( int i = 0; i < 8; i++ )
	{
		corners[i][0] = origin[0] + sx2[i] * HW;
		corners[i][1] = origin[1] + sy2[i] * HW;
		corners[i][2] = origin[2] + ( i < 4 ? 0.0f : standH );
	}

	// Project all 8 corners and find 2-D screen AABB
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
	int bw = (int)(maxX - minX);
	int bh = (int)(maxY - minY);
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
		memset( &pi, 0, sizeof(pi) );
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
