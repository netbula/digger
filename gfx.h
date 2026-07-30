// ---------------------------------------------------------------------------
//  gfx.h -- software framebuffer with anti-aliased drawing primitives.
//
//  Everything the game draws goes through here.  Shapes are rendered with
//  coverage-based anti-aliasing at native resolution rather than as blocky
//  pixel art, which is what lets the sprites look clean at 48px cells.
// ---------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <cstring>

// ------------------------------- geometry ----------------------------------
static const int CELL = 48;                     // cell size in pixels
static const int FW = 24, FH = 15;              // field size in cells
static const int FLDW = CELL * FW;              // 1152
static const int FLDH = CELL * FH;              // 720
static const int TOOLH = 46;                    // music toolbar, always on screen
static const int HUDH = 76;                     // game status row below it
static const int MARGIN = 16;
static const int SCRW = FLDW + 2 * MARGIN;              // 1184
static const int SCRH = TOOLH + HUDH + FLDH + MARGIN;   // 858
static const int FLDX = MARGIN;
static const int FLDY = TOOLH + HUDH;

static uint32_t g_scr[SCRW * SCRH];

// ------------------------------- directions --------------------------------
enum { DIR_R = 0, DIR_D, DIR_L, DIR_U, DIR_NONE };
static const int DX[4] = { 1, 0, -1, 0 };
static const int DY[4] = { 0, 1, 0, -1 };

// -------------------------------- colours ----------------------------------
static const uint32_t C_DIRT_TOP = 0x8A4A22;
static const uint32_t C_DIRT_BOT = 0x5A2C12;
static const uint32_t C_CAVE_TOP = 0x141A20;
static const uint32_t C_CAVE_BOT = 0x05070B;
static const uint32_t C_PANEL_TOP = 0x1E2836;
static const uint32_t C_PANEL_BOT = 0x0C1018;
static const uint32_t C_GEM = 0x2FE0D0;
static const uint32_t C_GEM_LT = 0xBFFFF6;
static const uint32_t C_GEM_DK = 0x0E7C7A;
static const uint32_t C_GOLD = 0xFFC93C;
static const uint32_t C_GOLD_LT = 0xFFF0A8;
static const uint32_t C_GOLD_DK = 0xA86C10;
static const uint32_t C_SACK = 0xC08A46;
static const uint32_t C_STEEL = 0x9AA8C0;
static const uint32_t C_STEEL_LT = 0xE4EEFF;
static const uint32_t C_HULL = 0x3FC8E0;
static const uint32_t C_HULL_LT = 0xB6F4FF;
static const uint32_t C_HULL_DK = 0x125C74;
static const uint32_t C_TREAD = 0x2A3240;
static const uint32_t C_TREAD_LT = 0x64748C;
static const uint32_t C_DRILL = 0xE8503C;
static const uint32_t C_DRILL_LT = 0xFFB0A0;
static const uint32_t C_FIRE_HOT = 0xFFF4C0;
static const uint32_t C_FIRE_MID = 0xFF9A28;
static const uint32_t C_FLEE = 0x4A78FF;
static const uint32_t C_WHITE = 0xFFFFFF;
static const uint32_t C_BLACK = 0x000000;

static inline uint32_t rgb(int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static inline uint32_t mixc(uint32_t a, uint32_t b, float t)
{
    if (t < 0) t = 0; if (t > 1) t = 1;
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return rgb(ar + (int)((br - ar) * t), ag + (int)((bg - ag) * t), ab + (int)((bb - ab) * t));
}
static inline uint32_t scalec(uint32_t c, float s)
{
    return rgb((int)(((c >> 16) & 255) * s), (int)(((c >> 8) & 255) * s), (int)((c & 255) * s));
}

// ------------------------------- pixel ops ---------------------------------
static inline void blend(int x, int y, uint32_t c, float a)
{
    if ((unsigned)x >= (unsigned)SCRW || (unsigned)y >= (unsigned)SCRH) return;
    if (a <= 0.0f) return;
    if (a >= 1.0f) { g_scr[y * SCRW + x] = c; return; }
    uint32_t d = g_scr[y * SCRW + x];
    int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    int sr = (c >> 16) & 255, sg = (c >> 8) & 255, sb = c & 255;
    g_scr[y * SCRW + x] = rgb(dr + (int)((sr - dr) * a),
                              dg + (int)((sg - dg) * a),
                              db + (int)((sb - db) * a));
}
// Additive, for glows and sparks.
static inline void blendAdd(int x, int y, uint32_t c, float a)
{
    if ((unsigned)x >= (unsigned)SCRW || (unsigned)y >= (unsigned)SCRH) return;
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    uint32_t d = g_scr[y * SCRW + x];
    g_scr[y * SCRW + x] = rgb(((d >> 16) & 255) + (int)(((c >> 16) & 255) * a),
                              ((d >> 8) & 255) + (int)(((c >> 8) & 255) * a),
                              (d & 255) + (int)((c & 255) * a));
}

static void fillRect(int x, int y, int w, int h, uint32_t c)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x1 > SCRW) x1 = SCRW; if (y1 > SCRH) y1 = SCRH;
    for (int j = y; j < y1; j++)
        for (int i = x; i < x1; i++) g_scr[j * SCRW + i] = c;
}
static void fillRectV(int x, int y, int w, int h, uint32_t top, uint32_t bot)
{
    for (int j = 0; j < h; j++) {
        uint32_t c = mixc(top, bot, h > 1 ? (float)j / (h - 1) : 0.0f);
        fillRect(x, y + j, w, 1, c);
    }
}

// ---------------------------- shape primitives -----------------------------
// All take float centres/extents and resolve edge coverage analytically, so
// nothing has jagged edges.
static void fillCircle(float cx, float cy, float r, uint32_t col, float alpha = 1.0f)
{
    if (r <= 0) return;
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) continue;
            blend(x, y, col, (cov > 1 ? 1.0f : cov) * alpha);
        }
    }
}
static void glowCircle(float cx, float cy, float r, uint32_t col, float intensity)
{
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d >= r) continue;
            float f = 1.0f - d / r;
            blendAdd(x, y, col, f * f * intensity);
        }
    }
}
// Signed distance to a rounded box, used for hulls, plating and panels.
static inline float sdRoundBox(float px, float py, float hx, float hy, float rad)
{
    float qx = fabsf(px) - hx + rad, qy = fabsf(py) - hy + rad;
    float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
    float m = qx > qy ? qx : qy;
    return sqrtf(ax * ax + ay * ay) + (m < 0 ? m : 0) - rad;
}
static void fillRoundRect(float x0, float y0, float x1, float y1, float rad,
                          uint32_t col, float alpha = 1.0f)
{
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    if (hx <= 0 || hy <= 0) return;
    if (rad > hx) rad = hx; if (rad > hy) rad = hy;
    int ix0 = (int)floorf(x0 - 1), ix1 = (int)ceilf(x1 + 1);
    int iy0 = (int)floorf(y0 - 1), iy1 = (int)ceilf(y1 + 1);
    for (int y = iy0; y <= iy1; y++) {
        for (int x = ix0; x <= ix1; x++) {
            float d = sdRoundBox(x + 0.5f - cx, y + 0.5f - cy, hx, hy, rad);
            float cov = 0.5f - d;
            if (cov <= 0) continue;
            blend(x, y, col, (cov > 1 ? 1.0f : cov) * alpha);
        }
    }
}
// Vertical gradient version -- the workhorse for anything that needs volume.
static void fillRoundRectV(float x0, float y0, float x1, float y1, float rad,
                           uint32_t top, uint32_t bot, float alpha = 1.0f)
{
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    if (hx <= 0 || hy <= 0) return;
    if (rad > hx) rad = hx; if (rad > hy) rad = hy;
    int ix0 = (int)floorf(x0 - 1), ix1 = (int)ceilf(x1 + 1);
    int iy0 = (int)floorf(y0 - 1), iy1 = (int)ceilf(y1 + 1);
    for (int y = iy0; y <= iy1; y++) {
        float t = (y1 > y0) ? (y + 0.5f - y0) / (y1 - y0) : 0.0f;
        uint32_t col = mixc(top, bot, t);
        for (int x = ix0; x <= ix1; x++) {
            float d = sdRoundBox(x + 0.5f - cx, y + 0.5f - cy, hx, hy, rad);
            float cov = 0.5f - d;
            if (cov <= 0) continue;
            blend(x, y, col, (cov > 1 ? 1.0f : cov) * alpha);
        }
    }
}
// Sphere-ish blob: radial gradient with the highlight offset up and left.
static void fillOrb(float cx, float cy, float r, uint32_t lite, uint32_t dark,
                    float alpha = 1.0f)
{
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    float hx = cx - r * 0.35f, hy = cy - r * 0.40f;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) continue;
            float lx = x + 0.5f - hx, ly = y + 0.5f - hy;
            float ld = sqrtf(lx * lx + ly * ly) / (r * 1.6f);
            if (ld > 1) ld = 1;
            blend(x, y, mixc(lite, dark, ld * ld), (cov > 1 ? 1.0f : cov) * alpha);
        }
    }
}
// Diamond / gem body: |dx|/rx + |dy|/ry <= 1 with a soft edge.
static void fillDiamond(float cx, float cy, float rx, float ry, uint32_t col,
                        float alpha = 1.0f)
{
    int x0 = (int)floorf(cx - rx - 1), x1 = (int)ceilf(cx + rx + 1);
    int y0 = (int)floorf(cy - ry - 1), y1 = (int)ceilf(cy + ry + 1);
    float sc = (rx < ry ? rx : ry);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = fabsf(x + 0.5f - cx) / rx, dy = fabsf(y + 0.5f - cy) / ry;
            float cov = (1.0f - (dx + dy)) * sc * 0.9f + 0.5f;
            if (cov <= 0) continue;
            blend(x, y, col, (cov > 1 ? 1.0f : cov) * alpha);
        }
    }
}
static void fillTri(float ax, float ay, float bx, float by, float cx2, float cy2,
                    uint32_t col, float alpha = 1.0f)
{
    float minx = ax < bx ? (ax < cx2 ? ax : cx2) : (bx < cx2 ? bx : cx2);
    float maxx = ax > bx ? (ax > cx2 ? ax : cx2) : (bx > cx2 ? bx : cx2);
    float miny = ay < by ? (ay < cy2 ? ay : cy2) : (by < cy2 ? by : cy2);
    float maxy = ay > by ? (ay > cy2 ? ay : cy2) : (by > cy2 ? by : cy2);
    int x0 = (int)floorf(minx - 1), x1 = (int)ceilf(maxx + 1);
    int y0 = (int)floorf(miny - 1), y1 = (int)ceilf(maxy + 1);
    float area = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
    if (fabsf(area) < 0.0001f) return;
    float sgn = area > 0 ? 1.0f : -1.0f;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * sgn;
            float e1 = ((cx2 - bx) * (py - by) - (cy2 - by) * (px - bx)) * sgn;
            float e2 = ((ax - cx2) * (py - cy2) - (ay - cy2) * (px - cx2)) * sgn;
            float m = e0 < e1 ? (e0 < e2 ? e0 : e2) : (e1 < e2 ? e1 : e2);
            float cov = m * 0.25f + 0.5f;                 // cheap edge softening
            if (cov <= 0) continue;
            blend(x, y, col, (cov > 1 ? 1.0f : cov) * alpha);
        }
    }
}
static void strokeLine(float ax, float ay, float bx, float by, float w,
                       uint32_t col, float alpha = 1.0f)
{
    float dx = bx - ax, dy = by - ay;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) { fillCircle(ax, ay, w * 0.5f, col, alpha); return; }
    int steps = (int)(len * 2) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        fillCircle(ax + dx * t, ay + dy * t, w * 0.5f, col, alpha);
    }
}

// ------------------------- orientation helper ------------------------------
// Lets one draw routine serve all four facings: work in (forward, side) space
// and let this map it to screen axes.
struct Ori { float cx, cy; int fx, fy, sx, sy; };
static inline Ori oriFor(float cx, float cy, int face)
{
    Ori o{ cx, cy, 1, 0, 0, 1 };
    switch (face) {
    case 0: o.fx = 1;  o.fy = 0;  o.sx = 0; o.sy = 1; break;   // right
    case 1: o.fx = 0;  o.fy = 1;  o.sx = 1; o.sy = 0; break;   // down
    case 2: o.fx = -1; o.fy = 0;  o.sx = 0; o.sy = 1; break;   // left
    case 3: o.fx = 0;  o.fy = -1; o.sx = 1; o.sy = 0; break;   // up
    }
    return o;
}
static inline void oriPt(const Ori& o, float f, float s, float& x, float& y)
{
    x = o.cx + f * o.fx + s * o.sx;
    y = o.cy + f * o.fy + s * o.sy;
}
static inline void oriBox(const Ori& o, float f0, float f1, float s0, float s1,
                          float& x0, float& y0, float& x1, float& y1)
{
    float ax, ay, bx, by;
    oriPt(o, f0, s0, ax, ay);
    oriPt(o, f1, s1, bx, by);
    x0 = ax < bx ? ax : bx; x1 = ax > bx ? ax : bx;
    y0 = ay < by ? ay : by; y1 = ay > by ? ay : by;
}
