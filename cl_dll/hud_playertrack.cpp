//=========================================================
// hud_playertrack.cpp
// CHudPlayerTrack — entity scanning, ESP drawing, CVAR setup.
//=========================================================
#include "hud.h"
#include "cl_dll.h"
#include "hud_playertrack.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

// -------------------------------------------------------
//  Globals
// -------------------------------------------------------
PlayerTrackInfo g_trackInfo[MAX_TRACKED_PLAYERS + 1];
int  g_iTrackedEnt = 0;
bool g_bMarked     = false;

// -------------------------------------------------------
//  CVARs
// -------------------------------------------------------
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

// -------------------------------------------------------
//  Internal helpers
// -------------------------------------------------------
static SCREENINFO g_scrinfo;

static inline float VecDist3D(const float *a, const float *b)
{
    float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

// Returns true if the world point projects onto the screen.
// sx/sy are filled with pixel coordinates.
static bool WorldToScreen(const float *world, float &sx, float &sy)
{
    float screen[3];
    if (gEngfuncs.pfnWorldToScreen((float *)world, screen))
        return false; // returns non-zero when behind the camera

    sx = (1.0f + screen[0]) * 0.5f * g_scrinfo.iWidth;
    sy = (1.0f - screen[1]) * 0.5f * g_scrinfo.iHeight;
    return true;
}

// 1-pixel-thick hollow rectangle outline drawn with four FillRGBA strips.
static void DrawBoxOutline(int x, int y, int w, int h, int r, int g, int b, int a)
{
    gEngfuncs.pfnFillRGBA(x,     y,     w,   1,   r, g, b, a); // top
    gEngfuncs.pfnFillRGBA(x,     y+h,   w+1, 1,   r, g, b, a); // bottom
    gEngfuncs.pfnFillRGBA(x,     y,     1,   h,   r, g, b, a); // left
    gEngfuncs.pfnFillRGBA(x+w,   y,     1,   h+1, r, g, b, a); // right
}

static void DrawStringCentered(int cx, int y, const char *str, float r, float g, float b)
{
    int len = gEngfuncs.pfnDrawConsoleStringLen((char *)str);
    gEngfuncs.pfnDrawSetTextColor(r, g, b);
    gEngfuncs.pfnDrawConsoleString(cx - len / 2, y, (char *)str);
}

// -------------------------------------------------------
//  Nearest-alive-player scan
// -------------------------------------------------------
int PlayerTrack_FindNearest()
{
    cl_entity_t *local = gEngfuncs.GetLocalPlayer();
    if (!local) return 0;

    int   best     = 0;
    float bestDist = 1e9f;
    int   maxCl    = gEngfuncs.GetMaxClients();

    for (int i = 1; i <= maxCl && i <= MAX_TRACKED_PLAYERS; i++)
    {
        if (i == local->index) continue;
        if (!g_trackInfo[i].alive)    continue;
        if (!g_trackInfo[i].seenThisFrame) continue;

        // FoV gate: skip players behind us unless debug_track_360 is set
        if (!debug_track_360 || debug_track_360->value == 0.0f)
        {
            // Simple dot-product check against view direction stored by view.cpp.
            // We use the vector from local origin to entity origin.
            // (If you want a real FoV cone, replace with a proper dot-product
            //  against gEngfuncs.GetLocalPlayer()'s view angles forward vector.)
            float dx = g_trackInfo[i].origin[0] - local->origin[0];
            float dy = g_trackInfo[i].origin[1] - local->origin[1];
            // For simplicity here we accept all — proper FoV gate is in view.cpp
            (void)dx; (void)dy;
        }

        float dist = VecDist3D(local->origin, g_trackInfo[i].origin);
        if (dist < bestDist)
        {
            bestDist = dist;
            best     = i;
        }
    }
    return best;
}

// -------------------------------------------------------
//  Per-frame logic  (called from HUD_Frame in cdll_int.cpp)
// -------------------------------------------------------
void PlayerTrack_Frame(double frametime)
{
    // Refresh screen size every frame (handles resolution changes).
    g_scrinfo.iSize = sizeof(g_scrinfo);
    gEngfuncs.pfnGetScreenInfo(&g_scrinfo);

    if (!debug_track_enable || debug_track_enable->value == 0.0f)
    {
        // Clear state when disabled so overlay vanishes immediately.
        g_iTrackedEnt = 0;
        g_bMarked     = false;
        // Reset seenThisFrame for next enable.
        for (int i = 1; i <= MAX_TRACKED_PLAYERS; i++)
            g_trackInfo[i].seenThisFrame = false;
        return;
    }

    // --- Check if tracked player just died or disappeared ---
    if (g_iTrackedEnt > 0)
    {
        bool lost = !g_trackInfo[g_iTrackedEnt].alive
                 || !g_trackInfo[g_iTrackedEnt].seenThisFrame;
        if (lost)
        {
            g_iTrackedEnt = 0;
            g_bMarked     = false;
        }
    }

    // --- If no target, find the nearest alive player ---
    if (g_iTrackedEnt == 0)
    {
        g_iTrackedEnt = PlayerTrack_FindNearest();
        g_bMarked     = false;
    }

    // Reset seenThisFrame for the next game frame.
    for (int i = 1; i <= MAX_TRACKED_PLAYERS; i++)
        g_trackInfo[i].seenThisFrame = false;

    if (g_iTrackedEnt == 0) return;

    // --- Movement prediction ---
    if (debug_predict && debug_predict->value != 0.0f)
    {
        cl_entity_t *ent = gEngfuncs.GetEntityByIndex(g_iTrackedEnt);
        if (ent)
        {
            float ft = (float)frametime;
            g_trackInfo[g_iTrackedEnt].predictedOrigin[0] = ent->origin[0] + ent->curstate.velocity[0] * ft;
            g_trackInfo[g_iTrackedEnt].predictedOrigin[1] = ent->origin[1] + ent->curstate.velocity[1] * ft;
            g_trackInfo[g_iTrackedEnt].predictedOrigin[2] = ent->origin[2] + ent->curstate.velocity[2] * ft;
        }
    }
    else
    {
        // Keep predictedOrigin in sync with real origin.
        memcpy(g_trackInfo[g_iTrackedEnt].predictedOrigin,
               g_trackInfo[g_iTrackedEnt].origin,
               sizeof(g_trackInfo[g_iTrackedEnt].origin));
    }

    // --- Auto-mark: crosshair within 8 pixels of projected head bone ---
    if (debug_auto_mark && debug_auto_mark->value != 0.0f && !g_bMarked)
    {
        float *hp = g_trackInfo[g_iTrackedEnt].headPos;
        float sx, sy;
        if (WorldToScreen(hp, sx, sy))
        {
            float cx = g_scrinfo.iWidth  * 0.5f;
            float cy = g_scrinfo.iHeight * 0.5f;
            float dx = sx - cx, dy = sy - cy;
            if (sqrtf(dx*dx + dy*dy) < 8.0f)
                g_bMarked = true;
        }
    }
}

// -------------------------------------------------------
//  CHudPlayerTrack — class implementation
// -------------------------------------------------------
DECLARE_MESSAGE(m_PlayerTrack, TrackerInit)   // placeholder, remove if unused

int CHudPlayerTrack::Init()
{
    debug_track_enable = gEngfuncs.pfnRegisterVariable("debug_track_enable", "0", FCVAR_CLIENTDLL);
    debug_track_silent = gEngfuncs.pfnRegisterVariable("debug_track_silent", "0", FCVAR_CLIENTDLL);
    debug_track_360    = gEngfuncs.pfnRegisterVariable("debug_track_360",    "0", FCVAR_CLIENTDLL);
    debug_auto_mark    = gEngfuncs.pfnRegisterVariable("debug_auto_mark",    "0", FCVAR_CLIENTDLL);
    debug_bone_target  = gEngfuncs.pfnRegisterVariable("debug_bone_target",  "7", FCVAR_CLIENTDLL);
    debug_predict      = gEngfuncs.pfnRegisterVariable("debug_predict",      "0", FCVAR_CLIENTDLL);
    visual_box         = gEngfuncs.pfnRegisterVariable("visual_box",         "1", FCVAR_CLIENTDLL);
    visual_name        = gEngfuncs.pfnRegisterVariable("visual_name",        "1", FCVAR_CLIENTDLL);
    visual_weapon      = gEngfuncs.pfnRegisterVariable("visual_weapon",      "1", FCVAR_CLIENTDLL);
    visual_marker      = gEngfuncs.pfnRegisterVariable("visual_marker",      "1", FCVAR_CLIENTDLL);

    memset(g_trackInfo, 0, sizeof(g_trackInfo));

    g_scrinfo.iSize = sizeof(g_scrinfo);
    gEngfuncs.pfnGetScreenInfo(&g_scrinfo);

    m_iFlags |= HUD_ACTIVE;
    gHUD.AddHudElem(this);
    return 1;
}

int CHudPlayerTrack::VidInit()
{
    g_scrinfo.iSize = sizeof(g_scrinfo);
    gEngfuncs.pfnGetScreenInfo(&g_scrinfo);
    return 1;
}

void CHudPlayerTrack::Reset()
{
    g_iTrackedEnt = 0;
    g_bMarked     = false;
}

int CHudPlayerTrack::Draw(float flTime)
{
    if (!debug_track_enable || debug_track_enable->value == 0.0f) return 1;
    if (g_iTrackedEnt <= 0) return 1;

    cl_entity_t *ent = gEngfuncs.GetEntityByIndex(g_iTrackedEnt);
    if (!ent || !ent->model) return 1;

    // Use predicted or real origin.
    float *origin = (debug_predict && debug_predict->value != 0.0f)
        ? g_trackInfo[g_iTrackedEnt].predictedOrigin
        : g_trackInfo[g_iTrackedEnt].origin;

    // Standard HL player hull extents.
    float standH = (ent->curstate.usehull == 1) ? 36.0f : 72.0f;

    // Build 8 corners of the bounding box.
    float corners[8][3];
    float hullW = 16.0f;
    int signs[8][2] = {{-1,-1},{1,-1},{1,1},{-1,1},{-1,-1},{1,-1},{1,1},{-1,1}};
    for (int i = 0; i < 8; i++)
    {
        corners[i][0] = origin[0] + signs[i][0] * hullW;
        corners[i][1] = origin[1] + signs[i][1] * hullW;
        corners[i][2] = origin[2] + (i < 4 ? 0.0f : standH);
    }

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    bool  any   = false;

    for (int i = 0; i < 8; i++)
    {
        float sx, sy;
        if (WorldToScreen(corners[i], sx, sy))
        {
            if (sx < minX) minX = sx;
            if (sy < minY) minY = sy;
            if (sx > maxX) maxX = sx;
            if (sy > maxY) maxY = sy;
            any = true;
        }
    }

    if (!any) return 1;

    int bx = (int)minX;
    int by = (int)minY;
    int bw = (int)(maxX - minX);
    int bh = (int)(maxY - minY);
    int cx = bx + bw / 2;

    // ---- visual_box ----
    if (visual_box && visual_box->value != 0.0f)
    {
        // Cyan normally; red when marked.
        int r = g_bMarked ? 255 : 0;
        int g = g_bMarked ?   0 : 220;
        int b = g_bMarked ?   0 : 255;
        DrawBoxOutline(bx, by, bw, bh, r, g, b, 230);
    }

    // ---- visual_name ----
    if (visual_name && visual_name->value != 0.0f)
    {
        hud_player_info_t pi;
        memset(&pi, 0, sizeof(pi));
        gEngfuncs.pfnGetPlayerInfo(g_iTrackedEnt, &pi);
        if (pi.name && pi.name[0])
            DrawStringCentered(cx, by - 12, pi.name, 1.0f, 1.0f, 1.0f);
    }

    // ---- visual_weapon ----
    if (visual_weapon && visual_weapon->value != 0.0f)
    {
        const char *wpn = g_trackInfo[g_iTrackedEnt].weaponName;
        if (wpn[0])
            DrawStringCentered(cx, by + bh + 2, wpn, 1.0f, 0.7f, 0.1f);
    }

    // ---- visual_marker ----
    if (visual_marker && visual_marker->value != 0.0f && g_bMarked)
        DrawStringCentered(cx, by - 24, "MARKED", 1.0f, 0.0f, 0.0f);

    return 1;
}
