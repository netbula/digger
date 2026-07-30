// ---------------------------------------------------------------------------
//  sprites.h -- every character and prop, drawn procedurally.
//
//  No bitmaps anywhere: each sprite is composed from anti-aliased orbs, round
//  rects, diamonds and triangles.  That is what keeps them crisp at 48px cells
//  and lets one routine cover all four facings via the Ori helper.
// ---------------------------------------------------------------------------
#pragma once
#include "gfx.h"
#include "audio.h"      // g_specBand: monsters move limb-by-limb to the spectrum

// ------------------------------ terrain cache ------------------------------
// The soil look (gradient + speckle + ambient occlusion along tunnel walls) is
// expensive per pixel, so it is cached and only the dug-out region is redrawn.
static uint8_t  g_dirt[FLDH][FLDW];
static uint32_t g_dirtPix[FLDH][FLDW];
static uint32_t g_cavePix[FLDH][FLDW];

static inline unsigned hash2(int x, int y)
{
    unsigned h = (unsigned)(x * 374761393 + y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void buildCave()
{
    for (int y = 0; y < FLDH; y++) {
        float v = (float)y / FLDH;
        uint32_t base = mixc(C_CAVE_TOP, C_CAVE_BOT, v);
        for (int x = 0; x < FLDW; x++) {
            unsigned h = hash2(x >> 2, y >> 2);
            float n = ((int)(h & 31) - 16) / 16.0f * 0.03f;    // cast first: h&31 is unsigned
            g_cavePix[y][x] = scalec(base, 1.0f + n);
        }
    }
}

static uint32_t soilPixel(int x, int y)
{
    if (!g_dirt[y][x]) return 0;
    // distance to the nearest opening, for the shading along tunnel walls
    int d = 8;
    for (int k = 1; k <= 7; k++) {
        bool open = false;
        if (x - k >= 0    && !g_dirt[y][x - k]) open = true;
        if (x + k < FLDW  && !g_dirt[y][x + k]) open = true;
        if (y - k >= 0    && !g_dirt[y - k][x]) open = true;
        if (y + k < FLDH  && !g_dirt[y + k][x]) open = true;
        if (open) { d = k; break; }
    }
    float shade = 0.34f + 0.095f * d;
    if (shade > 1.0f) shade = 1.0f;

    uint32_t base = mixc(C_DIRT_TOP, C_DIRT_BOT, (float)y / FLDH);
    // Rim light on the lip of soil sitting just under an opening, as if the
    // cave were lit from above.  Cheap, and it stops tunnels looking cut out.
    for (int k = 1; k <= 3; k++) {
        if (y - k >= 0 && !g_dirt[y - k][x]) {
            base = mixc(base, 0xE0B478, 0.50f * (1.0f - (k - 1) / 3.0f));
            break;
        }
    }
    unsigned h = hash2(x, y);
    float n = 1.0f + ((int)((h >> 3) & 63) - 32) / 32.0f * 0.075f;
    if ((h % 211) < 4) {                        // scattered pebbles
        base = mixc(base, 0xC8A070, 0.55f);
        n *= 1.05f;
    } else if ((h % 197) < 5) {
        base = scalec(base, 0.82f);
    }
    return scalec(base, shade * n);
}

static void rebuildSoil(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > FLDW - 1) x1 = FLDW - 1; if (y1 > FLDH - 1) y1 = FLDH - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            g_dirtPix[y][x] = soilPixel(x, y);
}
static void rebuildSoilAll() { rebuildSoil(0, 0, FLDW - 1, FLDH - 1); }

// Re-blends the cached soil over something already drawn, so a monster moving
// through undisturbed earth looks buried rather than pasted on top of it.
static void overlaySoil(int fx, int fy, int w, int h, float alpha)
{
    for (int y = 0; y < h; y++) {
        int sy = fy + y;
        if (sy < 0 || sy >= FLDH) continue;
        for (int x = 0; x < w; x++) {
            int sx = fx + x;
            if (sx < 0 || sx >= FLDW) continue;
            uint32_t c = g_dirtPix[sy][sx];
            if (c) blend(FLDX + sx, FLDY + sy, c, alpha);
        }
    }
}

static void drawField()
{
    for (int y = 0; y < FLDH; y++) {
        uint32_t* dst = &g_scr[(FLDY + y) * SCRW + FLDX];
        const uint32_t* sd = g_dirtPix[y];
        const uint32_t* sc = g_cavePix[y];
        for (int x = 0; x < FLDW; x++) dst[x] = sd[x] ? sd[x] : sc[x];
    }
}

// -------------------------------- particles --------------------------------
enum { P_SOIL = 0, P_SPARK, P_COIN, P_GEMBIT, P_SMOKE };
struct Part { float x, y, vx, vy, life, max, size; uint32_t col; int kind; };
static Part g_part[512];
static int  g_partN = 0;
static unsigned g_prs = 7717;

static inline float prand() { g_prs = g_prs * 1664525u + 1013904223u; return ((g_prs >> 9) & 0xFFFF) / 65535.0f; }

static void spawnPart(int kind, float x, float y, float vx, float vy,
                      float life, float size, uint32_t col)
{
    if (g_partN >= (int)(sizeof(g_part) / sizeof(g_part[0]))) return;
    Part& p = g_part[g_partN++];
    p.kind = kind; p.x = x; p.y = y; p.vx = vx; p.vy = vy;
    p.life = life; p.max = life; p.size = size; p.col = col;
}
static void burstParts(int kind, float x, float y, int n, float speed,
                       float life, float size, uint32_t col)
{
    for (int i = 0; i < n; i++) {
        float a = prand() * 6.2831853f;
        float s = speed * (0.35f + prand() * 0.65f);
        spawnPart(kind, x, y, cosf(a) * s, sinf(a) * s - speed * 0.25f,
                  life * (0.6f + prand() * 0.7f), size * (0.6f + prand() * 0.8f), col);
    }
}
static void updateParts()
{
    for (int i = 0; i < g_partN; i++) {
        Part& p = g_part[i];
        p.life -= 1.0f / 60.0f;
        if (p.life <= 0) { g_part[i] = g_part[--g_partN]; i--; continue; }
        p.x += p.vx; p.y += p.vy;
        if (p.kind == P_SOIL || p.kind == P_COIN || p.kind == P_GEMBIT) p.vy += 0.32f;
        else if (p.kind == P_SMOKE) { p.vy -= 0.04f; p.vx *= 0.96f; }
        else p.vy += 0.06f;
        p.vx *= 0.985f;
    }
}
static void drawParts()
{
    for (int i = 0; i < g_partN; i++) {
        const Part& p = g_part[i];
        float f = p.life / p.max;
        float sz = p.size * (p.kind == P_SMOKE ? (2.0f - f) : (0.35f + f * 0.65f));
        if (p.kind == P_SPARK)
            glowCircle(FLDX + p.x, FLDY + p.y, sz * 2.2f, p.col, f * 0.9f);
        else
            fillCircle(FLDX + p.x, FLDY + p.y, sz, p.col, f * (p.kind == P_SMOKE ? 0.35f : 1.0f));
    }
}

// -------------------------------- the digger -------------------------------
static void drawDigger(float px, float py, int face, int anim, bool hurt)
{
    Ori o = oriFor(px + CELL * 0.5f, py + CELL * 0.5f, face);
    float x0, y0, x1, y1;

    fillCircle(o.cx, o.cy + CELL * 0.30f, CELL * 0.33f, 0x000000, 0.28f);   // shadow

    // treads down both flanks
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        oriBox(o, -19.0f, 15.0f, sgn * 20.0f, sgn * 13.0f, x0, y0, x1, y1);
        fillRoundRectV(x0, y0, x1, y1, 5.0f, C_TREAD_LT, C_TREAD);
        for (int i = 0; i < 5; i++) {
            float f = -16.0f + ((i * 7 + anim) % 34);
            float ax, ay, bx, by;
            oriPt(o, f, sgn * 19.0f, ax, ay);
            oriPt(o, f, sgn * 14.0f, bx, by);
            strokeLine(ax, ay, bx, by, 2.6f, 0x8A9AB4, 0.85f);
        }
    }
    // hull
    oriBox(o, -17.0f, 14.0f, -13.0f, 13.0f, x0, y0, x1, y1);
    fillRoundRectV(x0, y0, x1, y1, 8.0f, C_HULL_LT, C_HULL_DK);
    oriBox(o, -14.0f, 11.0f, -10.0f, 10.0f, x0, y0, x1, y1);
    fillRoundRectV(x0, y0, x1, y1, 6.0f, C_HULL, C_HULL_DK);

    // visor and eyes
    oriBox(o, -6.0f, 9.0f, -9.0f, 9.0f, x0, y0, x1, y1);
    fillRoundRect(x0, y0, x1, y1, 5.0f, 0x0A2028, 0.92f);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float ex, ey;
        oriPt(o, 3.0f, sgn * 4.6f, ex, ey);
        fillCircle(ex, ey, 3.1f, hurt ? 0xFF6060 : C_GEM_LT);
        glowCircle(ex, ey, 6.5f, hurt ? 0xFF3030 : 0x40D0FF, 0.55f);
    }
    // grille
    for (int i = -1; i <= 1; i++) {
        float ax, ay, bx, by;
        oriPt(o, -10.0f, i * 5.0f, ax, ay);
        oriPt(o, -13.5f, i * 5.0f, bx, by);
        strokeLine(ax, ay, bx, by, 2.2f, 0x0E3A4A, 0.8f);
    }
    // drill
    float tipx, tipy, bax, bay, bbx, bby;
    oriPt(o, 23.0f, 0.0f, tipx, tipy);
    oriPt(o, 12.0f, -8.0f, bax, bay);
    oriPt(o, 12.0f, 8.0f, bbx, bby);
    fillTri(bax, bay, bbx, bby, tipx, tipy, C_DRILL);
    for (int i = 0; i < 3; i++) {
        float t = 0.20f + i * 0.24f;
        float w = 8.0f * (1.0f - t) + 1.5f;
        float ax, ay, bx, by;
        float f = 12.0f + t * 11.0f + ((anim + i * 3) % 6) * 0.3f;
        oriPt(o, f, -w, ax, ay);
        oriPt(o, f, w, bx, by);
        strokeLine(ax, ay, bx, by, 2.0f, C_DRILL_LT, 0.75f);
    }
    oriPt(o, 13.0f, 0.0f, tipx, tipy);
    fillCircle(tipx, tipy, 3.0f, 0xFFE0D0, 0.6f);
}

// -------------------------------- monsters ---------------------------------
// Tier is both the armour rating and the colour: hits knock it down one step
// at a time and only a tier-0 monster actually dies.
static void tierColors(int tier, uint32_t& lite, uint32_t& dark, uint32_t& trim)
{
    switch (tier) {
    case 0:  lite = 0xFF9AEE; dark = 0xA31C86; trim = 0xFFD0F4; break;
    case 1:  lite = 0xFFC066; dark = 0xA34E08; trim = 0xFFE0B0; break;
    case 2:  lite = 0x8CF0A0; dark = 0x1A7A38; trim = 0xD0FFD8; break;
    default: lite = 0xE4EEFF; dark = 0x4A5A78; trim = 0xFFFFFF; break;
    }
}
// Each monster is wired to the spectrum like a row of graphic-EQ bars: its body
// sits on its own band, and every limb, horn, jaw and eye is driven by a
// neighbouring band, so a single creature visibly articulates across the mix
// instead of pulsing as one lump.  `band` is the creature's home band; the
// caller spreads kinds across the register (see monsterBand).
// The colour a band "sounds like": deep red in the bass, up through amber and
// green, to violet at the top -- the same left-to-right reading order as the
// toolbar analyser, so a flashing tank is easy to match to a bar.
static uint32_t bandColor(int band)
{
    static const uint32_t RAMP[7] = {
        0xFF2A18, 0xFF7A10, 0xFFD020, 0x60E030, 0x20E0A0, 0x30A0FF, 0xA060FF
    };
    float t = (float)band / (float)(NUM_BANDS - 1) * 6.0f;
    int i = (int)t; if (i < 0) i = 0; if (i > 5) i = 5;
    return mixc(RAMP[i], RAMP[i + 1], t - (float)i);
}
static inline float bandLvl(int band, int offset)
{
    int b = (band + offset) % NUM_BANDS;
    if (b < 0) b += NUM_BANDS;
    return g_specBand[b];
}
// The enemy: a hostile tank.  kind 0 = plain gun tank (nobbin), 1 = dozer that
// shoves sacks (hobbin), 2 = drill tank that pierces soil (borer).
// `aimT` > 0 means it is winding up a shot and the barrel is charging -- that
// telegraph is what makes enemy fire fair to dodge.
// `tint` (0..1) fades in music-reactive recolouring -- used when a real track is
// playing rather than the built-in theme.
static void drawEnemyTank(float px, float py, int tier, int kind, int dir, int gunDir,
                          int anim, bool flee, int stun, int taunt, int aimT,
                          float beat, int band, float tint, float dgx, float dgy)
{
    float cx = px + CELL * 0.5f, cy = py + CELL * 0.5f;
    uint32_t lite, dark, trim;
    tierColors(tier, lite, dark, trim);
    if (kind == 2) {                              // drill tanks wear an earthy shell
        lite = mixc(lite, 0xD8A050, 0.45f);
        dark = mixc(dark, 0x4A2A0C, 0.45f);
    }

    // Band taps.  The gun barrel is the part that dances: it is the loudest
    // read on the tank, so putting the music there is what the eye follows.
    const float mus = (beat > 0.0f || g_specBand[band] > 0.0f) ? 1.0f : 0.0f;
    float hull   = bandLvl(band, 0) * mus;        // suspension bounce, home band
    float ext    = bandLvl(band, 2) * mus;        // barrel extends / recoils
    float elev   = bandLvl(band, 4) * mus;        // barrel swings off-axis
    float twist  = bandLvl(band, 6) * mus;        // turret rocks
    float muzzle = bandLvl(band, 8) * mus;        // muzzle glows

    // Music-driven livery.  Mixing between two already-saturated tier colours
    // reads as almost nothing (magenta toward red is barely a shift), and at a
    // 48px cell only the hull would change anyway.  So the livery goes most of
    // the way to the band colour and an additive halo carries the pulse -- on
    // the near-black tunnel background that halo is what actually catches the
    // eye.  `trim` is still untouched: the armour plates and hit pips drawn in
    // it are how the player counts remaining armour, and the halo is painted
    // *under* the tank so it never washes them out.
    uint32_t bandCol = bandColor(band);
    if (tint > 0.001f) {
        // A constant hue shift is invisible -- you only notice *change*.  So the
        // hull takes the band's hue outright and its brightness swings hard with
        // that band's level: dim between hits, blown out to near-white on them.
        // Floor the brightness so a quiet band never hides the tank.
        float b = 0.55f + 1.15f * hull;           // clips toward white on peaks
        lite = mixc(lite, scalec(bandCol, b), tint * 0.94f);
        dark = mixc(dark, scalec(bandCol, 0.14f + 0.40f * hull), tint * 0.94f);
    }
    // Flee and stun are gameplay states and must win over any livery.
    if (flee) { lite = 0x9AB4FF; dark = 0x1A2E8A; trim = 0xD0DCFF; }
    if (stun > 0 && ((stun / 3) & 1)) { lite = C_WHITE; dark = 0xC0C0C0; }

    Ori o = oriFor(cx, cy, dir);
    float bob = sinf(anim * 0.20f) * 1.0f - hull * 3.0f;
    if (taunt > 0) bob += sinf(taunt * 0.55f) * 4.0f;
    o.cy += bob;

    float x0, y0, x1, y1;
    fillCircle(cx, cy + CELL * 0.30f, CELL * 0.29f, 0x000000, 0.26f);

    // Band halo, drawn under the tank: against the near-black tunnel this is
    // what actually catches the eye, and it pulses rather than sitting still.
    if (tint > 0.001f) {
        float g1 = 0.12f + 0.95f * hull;
        glowCircle(cx, cy, 28.0f + hull * 22.0f, bandCol, g1 * 0.95f * tint);
        glowCircle(cx, cy, 16.0f + hull * 12.0f, bandCol, g1 * 0.65f * tint);
    }

    // treads down both flanks, rolling with travel
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        oriBox(o, -17.0f, 14.0f, sgn * 19.0f, sgn * 12.5f, x0, y0, x1, y1);
        uint32_t treadLite = mixc(C_TREAD_LT, dark, 0.35f);
        uint32_t treadDark = C_TREAD;
        if (tint > 0.001f) {                      // treads pick it up too
            treadLite = mixc(treadLite, bandCol, tint * (0.30f + 0.35f * hull));
            treadDark = mixc(treadDark, scalec(bandCol, 0.30f), tint * 0.45f);
        }
        fillRoundRectV(x0, y0, x1, y1, 4.5f, treadLite, treadDark);
        for (int i = 0; i < 5; i++) {
            float f = -15.0f + ((i * 7 + anim) % 31);
            float ax, ay, bx, by;
            oriPt(o, f, sgn * 18.0f, ax, ay);
            oriPt(o, f, sgn * 13.0f, bx, by);
            strokeLine(ax, ay, bx, by, 2.4f, mixc(0x8A9AB4, lite, 0.3f), 0.8f);
        }
    }
    // hull
    oriBox(o, -15.0f, 13.0f, -12.0f, 12.0f, x0, y0, x1, y1);
    fillRoundRectV(x0, y0, x1, y1, 7.0f, lite, dark);
    oriBox(o, -12.0f, 10.0f, -9.0f, 9.0f, x0, y0, x1, y1);
    fillRoundRectV(x0, y0, x1, y1, 5.0f, mixc(lite, dark, 0.35f), dark);

    // front attachment marks the variant
    if (kind == 1) {                              // dozer blade: it shoves sacks
        oriBox(o, 13.0f, 18.0f, -16.0f, 16.0f, x0, y0, x1, y1);
        fillRoundRectV(x0, y0, x1, y1, 3.0f, trim, mixc(trim, dark, 0.55f));
    }
    if (kind == 2) {                              // drill head: it pierces soil
        float tipx, tipy, bax, bay, bbx, bby;
        oriPt(o, 24.0f + ext * 3.0f, 0.0f, tipx, tipy);
        oriPt(o, 12.0f, -9.0f, bax, bay);
        oriPt(o, 12.0f, 9.0f, bbx, bby);
        fillTri(bax, bay, bbx, bby, tipx, tipy, 0xC08040);
        for (int i = 0; i < 3; i++) {
            float t = 0.22f + i * 0.24f;
            float w = 9.0f * (1.0f - t) + 1.5f;
            float f = 12.0f + t * 12.0f + ((anim + i * 3) % 6) * 0.3f;
            float ax, ay, bx, by;
            oriPt(o, f, -w, ax, ay);
            oriPt(o, f, w, bx, by);
            strokeLine(ax, ay, bx, by, 2.0f, 0xE8C88A, 0.8f);
        }
    }

    // ---- turret and the music-driven gun barrel ----
    // The turret traverses independently of the hull, so the gun can be seen
    // swinging onto you while the tank is still driving somewhere else.
    Ori g = oriFor(cx, cy + bob, gunDir);
    float side = (twist - 0.5f) * 5.0f;            // turret rocks on its band
    float turx, tury;
    oriPt(g, -1.0f, side, turx, tury);
    fillOrb(turx, tury, 11.0f + hull * 1.5f, lite, dark);
    fillCircle(turx, tury, 6.0f, mixc(dark, 0x000000, 0.35f), 0.55f);

    // barrel: length pumps on one band, swings off-axis on another
    float bLen = 20.0f + ext * 12.0f;
    float bOff = side + (elev - 0.5f) * 9.0f;
    float rootx, rooty, tipx2, tipy2;
    oriPt(g, 2.0f, side, rootx, rooty);
    oriPt(g, 2.0f + bLen, bOff, tipx2, tipy2);
    strokeLine(rootx, rooty, tipx2, tipy2, 7.5f, mixc(dark, 0x101418, 0.4f));
    strokeLine(rootx, rooty, tipx2, tipy2, 4.5f, mixc(lite, trim, 0.35f), 0.9f);
    fillCircle(tipx2, tipy2, 4.6f, mixc(dark, 0x000000, 0.5f));

    // muzzle: glows with its band, and flares hard while charging a shot
    if (muzzle > 0.02f)
        glowCircle(tipx2, tipy2, 6.0f + muzzle * 12.0f, 0xFF9A40, 0.25f + muzzle * 0.6f);
    if (aimT > 0) {
        // aimMax mirrors AIM_TICKS; the flash-up has to fill exactly as the
        // shot lands or the warning would read as longer than it is.
        const float aimMax = 24.0f;
        float chg = 1.0f - (float)aimT / aimMax; if (chg < 0) chg = 0; if (chg > 1) chg = 1;
        glowCircle(tipx2, tipy2, 8.0f + chg * 16.0f, 0xFF4020, 0.35f + chg * 0.65f);
        fillCircle(tipx2, tipy2, 2.0f + chg * 4.0f, 0xFFF0C0);
        // charging ring around the turret, so the warning reads even off-barrel
        for (int i = 0; i < 10; i++) {
            float a = i * 0.6283f - chg * 6.0f;
            fillCircle(turx + cosf(a) * 15.0f, tury + sinf(a) * 15.0f,
                       1.8f, 0xFF6030, 0.35f + chg * 0.5f);
        }
    }
    // armour plates, one per remaining tier
    for (int i = 0; i < tier; i++) {
        float f = -12.0f + i * 8.0f;
        float ax, ay, bx, by;
        oriPt(o, f, -11.0f, ax, ay);
        oriPt(o, f, 11.0f, bx, by);
        strokeLine(ax, ay, bx, by, 2.6f, trim, 0.9f);
    }
    // a hostile eye slit on the hull front, still tracking the player
    float ax2 = dgx - cx, ay2 = dgy - cy;
    float al = sqrtf(ax2 * ax2 + ay2 * ay2); if (al < 1) al = 1;
    ax2 /= al; ay2 /= al;
    float ex, ey;
    oriPt(o, 8.0f, 0.0f, ex, ey);
    fillCircle(ex, ey, 3.4f, 0x180810, 0.9f);
    fillCircle(ex + ax2 * 1.4f, ey + ay2 * 1.4f, 1.8f,
               flee ? 0x80C0FF : 0xFF5040);
    // remaining-hits pips, so the player can read toughness at a glance
    for (int i = 0; i <= tier; i++) {
        float dxp = cx - tier * 3.6f + i * 7.2f;
        fillCircle(dxp, cy - 25.0f, 2.4f, trim, 0.9f);
    }
    // jeering after a dodge: a bouncing exclamation over its head
    if (taunt > 0) {
        float a = taunt / 70.0f; if (a > 1) a = 1;
        float ty = cy - 40.0f - sinf(taunt * 0.3f) * 3.0f;
        strokeLine(cx + 16.0f, ty - 7.0f, cx + 16.0f, ty + 3.0f, 5.0f, 0xFFE050, a);
        fillCircle(cx + 16.0f, ty + 9.0f, 2.8f, 0xFFE050, a);
        for (int i = 0; i < 3; i++) {
            float r = 5.0f + i * 4.0f + (1.0f - a) * 10.0f;
            fillCircle(cx + 27.0f + i * 4.0f, ty - 2.0f - i * 3.0f, r * 0.35f,
                       0xFFE050, a * (0.5f - i * 0.12f));
        }
    }
}

// --------------------------------- app icon --------------------------------
// Painted with the same primitives as everything else, into the top-left corner
// of the framebuffer, then read back by the caller.  Keeps the project's "no
// asset files" property: the icon is code, not a .ico on disk.
static void paintIcon(int size)
{
    const float s = size / 64.0f;                 // design is authored at 64x64
    fillRect(0, 0, size, size, 0x000000);

    // rounded badge with a lit top edge
    fillRoundRectV(1.5f * s, 1.5f * s, 62.5f * s, 62.5f * s, 14.0f * s,
                   0x2A3550, 0x0C1220);
    fillRoundRect(1.5f * s, 1.5f * s, 62.5f * s, 22.0f * s, 14.0f * s, 0x3E4E74, 0.30f);

    // At 16px there is no room for scenery -- the tank alone has to carry the
    // icon, so the soil and gems are dropped and the vehicle is scaled up to
    // fill the badge.  Detail that cannot be resolved just turns to mud.
    const bool tiny = (size < 32);
    float ts = tiny ? 1.55f : 1.0f;               // tank scale
    float cx = (tiny ? 30.0f : 29.0f) * s;
    float cy = (tiny ? 32.0f : 28.0f) * s;

    if (!tiny) {
        for (int y = (int)(38.0f * s); y < (int)(60.0f * s); y++)
            for (int x = (int)(5.0f * s); x < (int)(59.0f * s); x++) {
                unsigned h = hash2(x, y);
                uint32_t c = ((h & 15) < 3) ? 0xA0561E : 0x76340F;
                blend(x, y, c, 0.92f);
            }
        for (int i = 0; i < 2; i++) {
            float gx = (17.0f + i * 30.0f) * s, gy = 50.0f * s;
            fillDiamond(gx, gy, 5.0f * s, 6.5f * s, C_GEM_DK);
            fillDiamond(gx, gy + 0.5f * s, 3.8f * s, 5.0f * s, C_GEM);
            fillCircle(gx - 1.2f * s, gy - 1.8f * s, 1.0f * s, 0xFFFFFF, 0.8f);
        }
    }

    const float u = s * ts;                       // one design unit, tank-local
    if (!tiny) fillCircle(cx, cy + 12.0f * u, 16.0f * u, 0x000000, 0.28f);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        fillRoundRectV(cx - 17.0f * u, cy + sgn * 11.0f * u - 4.0f * u,
                       cx + 13.0f * u, cy + sgn * 11.0f * u + 4.0f * u,
                       3.0f * u, C_TREAD_LT, C_TREAD);
        if (!tiny)                                // tread links vanish at 16px
            for (int i = 0; i < 4; i++)
                strokeLine(cx - 13.0f * u + i * 8.0f * u, cy + sgn * 11.0f * u - 3.0f * u,
                           cx - 13.0f * u + i * 8.0f * u, cy + sgn * 11.0f * u + 3.0f * u,
                           2.0f * u, 0x8A9AB4, 0.9f);
    }
    fillRoundRectV(cx - 15.0f * u, cy - 9.0f * u, cx + 11.0f * u, cy + 9.0f * u,
                   5.0f * u, C_HULL_LT, C_HULL_DK);
    fillRoundRectV(cx - 11.0f * u, cy - 6.0f * u, cx + 7.0f * u, cy + 6.0f * u,
                   4.0f * u, C_HULL, C_HULL_DK);
    fillOrb(cx - 2.0f * u, cy - 1.0f * u, 7.5f * u, C_HULL_LT, C_HULL_DK);
    strokeLine(cx + 1.0f * u, cy - 1.0f * u, cx + 22.0f * u, cy - 1.0f * u,
               6.5f * u, 0x123844);
    strokeLine(cx + 1.0f * u, cy - 1.0f * u, cx + 22.0f * u, cy - 1.0f * u,
               3.6f * u, C_HULL_LT, 0.95f);
    // drill tip and its glow
    fillTri(cx + 19.0f * u, cy - 6.5f * u, cx + 19.0f * u, cy + 4.5f * u,
            cx + 29.0f * u, cy - 1.0f * u, C_DRILL);
    glowCircle(cx + 27.0f * u, cy - 1.0f * u, 6.0f * u, 0xFF6030, 0.75f);
}
// Alpha for an icon pixel: the coverage of the rounded badge, so the corners
// come out transparent instead of black.
static float iconAlpha(int x, int y, int size)
{
    const float s = size / 64.0f;
    float cxm = 32.0f * s, cym = 32.0f * s;
    float hx = 30.5f * s, hy = 30.5f * s, rad = 14.0f * s;
    float d = sdRoundBox(x + 0.5f - cxm, y + 0.5f - cym, hx, hy, rad);
    float cov = 0.5f - d;
    if (cov < 0) return 0.0f;
    return cov > 1.0f ? 1.0f : cov;
}

// --------------------------------- props -----------------------------------
static void drawEmerald(float px, float py, int phase)
{
    float cx = px + CELL * 0.5f, cy = py + CELL * 0.5f;
    float pulse = 0.85f + 0.15f * sinf(phase * 0.06f);
    glowCircle(cx, cy, 17.0f, 0x0A6060, 0.30f * pulse);
    fillDiamond(cx, cy, 13.0f, 16.0f, C_GEM_DK);
    fillDiamond(cx, cy + 1.0f, 11.0f, 13.5f, C_GEM);
    fillTri(cx, cy - 13.0f, cx - 8.5f, cy - 1.0f, cx, cy - 1.0f, C_GEM_LT, 0.75f);
    fillTri(cx, cy - 13.0f, cx + 8.5f, cy - 1.0f, cx, cy - 1.0f, C_GEM_LT, 0.35f);
    fillTri(cx - 8.5f, cy - 1.0f, cx, cy + 14.0f, cx, cy - 1.0f, C_GEM, 0.6f);
    strokeLine(cx - 4.0f, cy - 7.0f, cx - 1.5f, cy - 2.0f, 2.4f, 0xFFFFFF, 0.85f * pulse);
    fillCircle(cx + 4.5f, cy + 4.0f, 1.5f, 0xFFFFFF, 0.5f * pulse);
}

static void drawBag(float px, float py, float wob)
{
    float cx = px + CELL * 0.5f + wob, cy = py + CELL * 0.5f;
    fillCircle(cx, cy + 20.0f, 16.0f, 0x000000, 0.30f);
    fillOrb(cx, cy + 5.0f, 18.5f, mixc(C_GOLD_LT, C_SACK, 0.35f), 0x6A3C0E);
    fillRoundRectV(cx - 15.0f, cy + 6.0f, cx + 15.0f, cy + 20.0f, 8.0f,
                   C_SACK, 0x5A320C);
    // neck and tie
    fillRoundRectV(cx - 7.0f, cy - 18.0f, cx + 7.0f, cy - 6.0f, 4.0f, 0xE0C090, 0x9A7040);
    strokeLine(cx - 8.0f, cy - 7.0f, cx + 8.0f, cy - 7.0f, 4.0f, 0x7A4A18);
    strokeLine(cx - 8.0f, cy - 4.0f, cx + 8.0f, cy - 4.0f, 3.0f, 0x8A5A20, 0.8f);
    // coin peeking out plus a highlight
    fillCircle(cx + 0.5f, cy - 14.0f, 4.6f, C_GOLD);
    fillCircle(cx - 0.8f, cy - 15.2f, 2.2f, C_GOLD_LT);
    fillCircle(cx - 7.0f, cy + 2.0f, 3.4f, 0xFFFFFF, 0.22f);
}

static void drawGold(float px, float py, int phase)
{
    float cx = px + CELL * 0.5f, cy = py + CELL * 0.62f;
    fillCircle(cx, cy + 8.0f, 17.0f, 0x000000, 0.28f);
    static const float ox[7] = { -13, -6, 1, 8, -9, 0, 9 };
    static const float oy[7] = { 6, 7, 6, 7, 0, -1, 0 };
    for (int i = 0; i < 7; i++) {
        fillCircle(cx + ox[i], cy + oy[i] + 1.0f, 6.6f, C_GOLD_DK);
        fillOrb(cx + ox[i], cy + oy[i], 6.0f, C_GOLD_LT, C_GOLD);
    }
    fillOrb(cx, cy - 8.0f, 6.4f, C_GOLD_LT, C_GOLD);
    float s = 0.5f + 0.5f * sinf(phase * 0.11f);
    fillCircle(cx - 4.0f, cy - 10.0f, 1.8f, 0xFFFFFF, 0.4f + 0.6f * s);
    glowCircle(cx, cy, 20.0f, 0x604010, 0.22f + 0.16f * s);
}

static void drawCherry(float px, float py, int phase)
{
    float cx = px + CELL * 0.5f, cy = py + CELL * 0.5f;
    float s = 0.7f + 0.3f * sinf(phase * 0.09f);
    glowCircle(cx, cy, 22.0f, 0x501010, 0.35f * s);
    strokeLine(cx - 8.0f, cy + 6.0f, cx + 1.0f, cy - 16.0f, 2.6f, 0x2E8B2E);
    strokeLine(cx + 8.0f, cy + 8.0f, cx + 1.0f, cy - 16.0f, 2.6f, 0x2E8B2E);
    fillTri(cx + 1.0f, cy - 16.0f, cx + 14.0f, cy - 20.0f, cx + 6.0f, cy - 11.0f, 0x46C846);
    fillOrb(cx - 8.0f, cy + 7.0f, 9.5f, 0xFF7070, 0x8E0C0C);
    fillOrb(cx + 8.0f, cy + 9.0f, 9.0f, 0xFF7070, 0x8E0C0C);
    fillCircle(cx - 11.0f, cy + 3.5f, 2.6f, 0xFFFFFF, 0.55f);
    fillCircle(cx + 5.0f, cy + 5.5f, 2.2f, 0xFFFFFF, 0.45f);
}

static void drawFireball(float px, float py, int phase)
{
    float f = 0.8f + 0.2f * sinf(phase * 0.7f);
    glowCircle(FLDX + px, FLDY + py, 17.0f * f, 0x804010, 0.9f);
    glowCircle(FLDX + px, FLDY + py, 10.0f * f, C_FIRE_MID, 0.9f);
    fillCircle(FLDX + px, FLDY + py, 5.2f * f, C_FIRE_HOT);
    fillCircle(FLDX + px, FLDY + py, 2.6f * f, 0xFFFFFF);
}
