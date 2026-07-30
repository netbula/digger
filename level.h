// ---------------------------------------------------------------------------
//  level.h -- procedural generator for all 99 stages, plus the difficulty curve.
//
//  Emerald placement is deliberately organic: a cellular-automaton pass builds
//  irregular seams and pockets, then random-walk veins run through them.  A
//  lattice reads as a parade ground; this reads as ore.
//
//  Connectivity from the den to the player's start is a hard requirement --
//  nobbins and hobbins cannot dig, so a stranded den would ruin the stage.
// ---------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include <cstring>

static const int MAXLEVEL = 99;

// ------------------------------ difficulty ---------------------------------
// Everything saturates by stage 71 so the last third is demanding but bounded.
// Monsters stay clearly slower than the digger at every stage.
struct LevelCfg {
    int monTotal;      // monsters released over the whole stage
    int maxActive;     // how many may be on screen at once
    int monSpeed;      // 1/256 px per tick   (digger runs at DIG_SPEED)
    int maxTier;       // toughest armour that can spawn (hits needed = tier + 1)
    int bagCount;
    int hobChance;     // percent of spawns that are bag-shoving hobbins
    int borerMax;      // how many soil-piercing borers the stage may release
    int borerLive;     // and how many of those may be alive at once
    int dodgeSkill;    // percent chance a monster tries to sidestep a shot
    int aiWeight;      // 0-100: how much the adaptive player model steers them
    int gunRange;      // how many cells its gun reaches
    int gunReload;     // ticks between its shots
    int extraCorridor;
};
static const int DIG_SPEED = 560;                // ~2.7 cells/sec

static LevelCfg cfgFor(int lv)                   // lv is 0-based
{
    float t = (float)lv / 70.0f;
    if (t > 1.0f) t = 1.0f;
    LevelCfg c;
    c.monTotal      = 5   + (int)(13 * t + 0.5f);     //  5 -> 18
    c.maxActive     = 2   + (int)(3  * t + 0.5f);     //  2 -> 5
    // Sized for this field.  The playfield is 24x15, so a monster crossing it
    // at the old 290 took ~20 seconds and the stage felt deserted; the floor
    // matters more than the ceiling here.  The cap is set by the assertion that
    // the worst case (top armour + hobbin bonus) stays under 90% of DIG_SPEED.
    c.monSpeed      = 400 + (int)(20 * t + 0.5f);     // 400 -> 420
    c.maxTier       = (int)(3 * t + 0.5f);            //  0 -> 3   (max 4 hits)
    c.bagCount      = 6   + (int)(6  * t + 0.5f);     //  6 -> 12
    c.hobChance     = 10  + (int)(30 * t + 0.5f);     // 10 -> 40
    c.borerMax      = (int)(5 * t + 0.35f);           //  0 -> 5
    c.borerLive     = c.borerMax > 0 ? 1 + (int)(1.6f * t) : 0;   // 1 -> 2
    c.extraCorridor = 1   + (int)(4  * t + 0.5f);

    // Evasion and cunning are the two things that keep climbing for the whole
    // run rather than saturating at stage 71 -- speed and armour are capped so
    // the late game gets smarter, not merely faster.  Both stay short of
    // certainty so a well-aimed shot is never futile.
    float u = (float)lv / (float)(MAXLEVEL - 1);       // 0 at stage 1, 1 at stage 99
    // Self-preservation is instinct, not something learned by stage 40: even the
    // first monsters flinch from an incoming shot, they are just easier to catch
    // out.  This is rolled once per shot (see tryDodges), so it really is the
    // chance of evading a given fireball.
    c.dodgeSkill = 28 + (int)(46.0f * u + 0.5f);      // 28 -> 74 %
    c.aiWeight   = (int)(100.0f * u + 0.5f);          //  0 -> 100 %

    // Enemy gunnery.  Spot it, shoot it: there is no acquisition delay and no
    // "does it feel like it" roll at any stage.  The only thing between sighting
    // and the shell leaving the barrel is the fixed muzzle flash-up, which is
    // the player's cue.  Difficulty rides on reach and rate of fire instead.
    // Reach.  The player's fireball has no range limit at all, so a 6-cell gun
    // was a strange handicap.  A long shot is also the *easy* one to dodge --
    // the shell is slow, so flight time grows with distance and the warning
    // grows with it.  The dangerous case is point blank, and that one is pinned
    // by the flash-up (see AIM_TICKS).
    c.gunRange  = 12 + (int)(8.0f * u + 0.5f);        // 12 -> 20 cells
    c.gunReload = 210 - (int)(110.0f * u + 0.5f);     // 210 -> 100 ticks
    return c;
}

// ------------------------------ generation ---------------------------------
struct GenResult {
    uint8_t dug[FH][FW];
    uint8_t gem[FH][FW];
    uint8_t bag[FH][FW];
    int startCx, startCy, denCx, denCy, chCx, chCy;
    int gemCount, bagCount;
};

static unsigned g_lseed;
static inline unsigned lrnd()
{
    g_lseed = g_lseed * 1664525u + 1013904223u;
    return (g_lseed >> 8) & 0xFFFFFF;
}

static void carveH(GenResult& r, int cy, int x0, int x1)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (cy < 0 || cy >= FH) return;
    for (int x = x0; x <= x1; x++) if (x >= 0 && x < FW) r.dug[cy][x] = 1;
}
static void carveV(GenResult& r, int cx, int y0, int y1)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (cx < 0 || cx >= FW) return;
    for (int y = y0; y <= y1; y++) if (y >= 0 && y < FH) r.dug[y][cx] = 1;
}

static void genLevel(int lv, GenResult& r)
{
    g_lseed = 0x9E3779B9u ^ (unsigned)((lv + 1) * 2654435761u);
    for (int i = 0; i < 11; i++) lrnd();

    memset(r.dug, 0, sizeof(r.dug));
    memset(r.gem, 0, sizeof(r.gem));
    memset(r.bag, 0, sizeof(r.bag));

    r.denCx = FW - 1; r.denCy = 0;
    r.startCx = FW / 2; r.startCy = FH - 1;
    r.chCx = 0;        r.chCy = 0;

    // --- organic ore body ----------------------------------------------------
    // Seed noise, then smooth it so the emeralds clump into irregular seams
    // instead of falling into rows and columns.
    uint8_t a[FH][FW], b[FH][FW];
    int fill = 46 + (int)(lrnd() % 9);                  // 46-54% seed density
    for (int cy = 0; cy < FH; cy++)
        for (int cx = 0; cx < FW; cx++)
            a[cy][cx] = ((int)(lrnd() % 100) < fill) ? 1 : 0;

    int passes = 2 + (int)(lrnd() % 3);
    for (int p = 0; p < passes; p++) {
        for (int cy = 0; cy < FH; cy++) {
            for (int cx = 0; cx < FW; cx++) {
                int n = 0;
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++) {
                        if (!ox && !oy) continue;
                        int nx = cx + ox, ny = cy + oy;
                        if (nx < 0 || nx >= FW || ny < 0 || ny >= FH) n++;   // edges bias solid
                        else n += a[ny][nx];
                    }
                b[cy][cx] = (n >= 5) ? 1 : (n <= 2 ? 0 : a[cy][cx]);
            }
        }
        memcpy(a, b, sizeof(a));
    }
    // Punch irregular holes through the seams so they are not solid slabs.
    int keep = 58 + (int)(lrnd() % 18);                 // keep 58-75% of each seam
    for (int cy = 0; cy < FH; cy++)
        for (int cx = 0; cx < FW; cx++)
            r.gem[cy][cx] = (a[cy][cx] && (int)(lrnd() % 100) < keep) ? 1 : 0;

    // --- veins: random walks that thread across the field --------------------
    int veins = 2 + (int)(lrnd() % 3);
    for (int v = 0; v < veins; v++) {
        int cx = (int)(lrnd() % FW), cy = (int)(lrnd() % FH);
        int len = 14 + (int)(lrnd() % 26);
        int dir = (int)(lrnd() % 4);
        for (int s = 0; s < len; s++) {
            if (cx >= 0 && cx < FW && cy >= 0 && cy < FH) r.gem[cy][cx] = 1;
            if ((lrnd() % 100) < 42) dir = (int)(lrnd() % 4);
            cx += DX[dir]; cy += DY[dir];
            if (cx < 0) cx = 0; if (cx >= FW) cx = FW - 1;
            if (cy < 0) cy = 0; if (cy >= FH) cy = FH - 1;
        }
    }

    // --- guaranteed den -> start spine ---------------------------------------
    int vc = 2 + (int)(lrnd() % (FW - 4));
    int hr = 2 + (int)(lrnd() % (FH - 4));
    carveH(r, r.denCy, r.denCx, vc);
    carveV(r, vc, r.denCy, hr);
    carveH(r, hr, vc, r.startCx);
    carveV(r, r.startCx, hr, r.startCy);

    LevelCfg cfg = cfgFor(lv);
    for (int i = 0; i < cfg.extraCorridor; i++) {
        if (lrnd() & 1) {
            int cy = 1 + (int)(lrnd() % (FH - 2));
            carveH(r, cy, (int)(lrnd() % FW), (int)(lrnd() % FW));
        } else {
            int cx = 1 + (int)(lrnd() % (FW - 2));
            carveV(r, cx, (int)(lrnd() % FH), (int)(lrnd() % FH));
        }
    }
    r.dug[r.denCy][r.denCx] = 1;
    r.dug[r.startCy][r.startCx] = 1;
    r.dug[r.chCy][r.chCx] = 1;
    if (r.startCx > 0) r.dug[r.startCy][r.startCx - 1] = 1;
    if (r.startCx < FW - 1) r.dug[r.startCy][r.startCx + 1] = 1;

    for (int cy = 0; cy < FH; cy++)
        for (int cx = 0; cx < FW; cx++)
            if (r.dug[cy][cx]) r.gem[cy][cx] = 0;

    // --- keep the stage inside a sane length ---------------------------------
    int count = 0;
    for (int cy = 0; cy < FH; cy++)
        for (int cx = 0; cx < FW; cx++) count += r.gem[cy][cx];
    const int GEM_MAX = 150, GEM_MIN = 70;
    for (int guard = 0; count > GEM_MAX && guard < 4000; guard++) {
        int cx = (int)(lrnd() % FW), cy = (int)(lrnd() % FH);
        if (r.gem[cy][cx]) { r.gem[cy][cx] = 0; count--; }
    }
    for (int guard = 0; count < GEM_MIN && guard < 4000; guard++) {
        int cx = (int)(lrnd() % FW), cy = (int)(lrnd() % FH);
        if (!r.gem[cy][cx] && !r.dug[cy][cx]) { r.gem[cy][cx] = 1; count++; }
    }

    // --- gold bags -----------------------------------------------------------
    // Never on a corridor: only a hobbin can shove one, so a bag parked on the
    // spine would strand every other monster behind it.
    r.bagCount = 0;
    for (int guard = 0; guard < 1200 && r.bagCount < cfg.bagCount; guard++) {
        int cx = (int)(lrnd() % FW), cy = 1 + (int)(lrnd() % (FH - 2));
        if (r.dug[cy][cx] || r.bag[cy][cx]) continue;
        if (r.dug[cy + 1][cx]) continue;                 // must rest on soil
        int ddx = cx - r.startCx, ddy = cy - r.startCy;
        if (ddx * ddx + ddy * ddy < 9) continue;
        bool crowded = false;
        for (int t = 0; t < 4; t++) {
            int nx = cx + DX[t], ny = cy + DY[t];
            if (nx >= 0 && nx < FW && ny >= 0 && ny < FH && r.bag[ny][nx]) crowded = true;
        }
        if (crowded) continue;
        r.bag[cy][cx] = 1; r.bagCount++;
        r.gem[cy][cx] = 0;
    }

    r.gemCount = 0;
    for (int cy = 0; cy < FH; cy++)
        for (int cx = 0; cx < FW; cx++)
            if (r.gem[cy][cx]) r.gemCount++;
}
