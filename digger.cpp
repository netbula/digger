// ---------------------------------------------------------------------------
//  DIGGER  --  a remake of the 1983 Windmill Software arcade game.
//
//  Win32 / C++17, no third-party libraries and no asset files.  The frame is
//  rendered in software with anti-aliased primitives at 1184x812; sound is
//  synthesised sample by sample on waveOut; the built-in music player decodes
//  WAV through the same mixer so the monsters can dance to the real signal.
//
//  Build:  build.bat            Self test:  build.bat test
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>                            // GET_X_LPARAM / GET_Y_LPARAM
#include <cstdio>
#include <vector>

#include "gfx.h"
#include "audio.h"
#include "sprites.h"
#include "level.h"
#include "player.h"

// -------------------------------- entities ---------------------------------
static const int HOLD_TICKS = 12;               // ~200 ms: tap turns, hold drives

struct Digger {
    int  x, y, dir, face, acc;
    int  moveLeft;                              // pixels left in the current cell move
    int  holdT;
    bool stepQueued, alive;
    int  anim, pushBag;
};
enum { MK_NOBBIN = 0, MK_HOBBIN, MK_BORER };
struct Monster {
    int  x, y, dir, acc, speed;
    bool alive, flee;
    int  kind, tier, stun, anim, pushBag;
    int  dodgeDir, lastDodge, dodgeLock, dodgeT, taunt, digSfx;
    int  dodgeCool;          // must commit to the chase again before evading anew
    unsigned dodgeSeen;      // id of the last fireball this one reacted to
    int  band;               // home spectrum band it dances to
    int  aimT, gunCool;      // shot windup (telegraph) and reload
    int  acquire, aimDir;    // target lock-on progress, and the locked bearing
};
enum { BAG_IDLE = 0, BAG_WOBBLE, BAG_FALL, BAG_GOLD };
struct Bag { int x, y, state, wob, fallFrom, vacc, vspeed, hold; };
struct Bullet { int x, y, dir; bool alive; unsigned id; };
// Enemy shells.  Deliberately slower than the player's fireball and capped in
// number, so incoming fire can always be read and escaped.
static const int  MAX_SHELLS  = 3;
static const int  SHELL_SPEED = 6;               // px per tick (player fires 9)
// Muzzle flash-up before the shell leaves: the tank fires the instant it sights
// you, so this is the player's entire warning.  Kept just long enough that,
// added to the shell's travel time, stepping out of the lane always beats it --
// see the survival assertions in the self test.
static const int  AIM_TICKS   = 24;              // 0.4s
struct Shell { int x, y, dir; bool alive; };

enum GState { GS_TITLE, GS_PLAY, GS_DYING, GS_CLEAR, GS_OVER, GS_WIN };

// A running read on how this particular player behaves.  It survives across
// stages within one game, so the monsters adapt to your habits rather than to
// a fixed script: they learn how trigger-happy you are, which way you like to
// travel, and which way to jump when you shoot.
struct PlayerAI {
    float fireDir[4];        // weighted history of firing directions
    float aggression;        // shots per second, smoothed
    float camp;              // 0..1, how much of the time you stand still
    float axisPref;          // -1 mostly vertical .. +1 mostly horizontal
    float dodgeWin[4];       // which escape direction has actually worked
    float velX, velY;        // smoothed travel, used to cut you off
    int   shots, hits;
};
static PlayerAI g_ai{};
static void aiReset() { memset(&g_ai, 0, sizeof(g_ai)); }

struct Game {
    Digger dig{};
    std::vector<Monster> mons;
    std::vector<Bag>     bags;
    Bullet bul{};
    Shell  shells[MAX_SHELLS]{};
    bool   gem[FH][FW]{};
    int    gemLeft = 0, gemTotal = 0;
    int    startCx = 12, startCy = 14, denCx = 23, denCy = 0, chCx = 0, chCy = 0;

    LevelCfg cfg{};
    int  level = 0, score = 0, lives = 3, nextLife = 20000;
    int  monLeft = 0, spawnGap = 0, borersOut = 0;
    unsigned spawnSeq = 0;              // fans monsters out across the spectrum
    int  streak = 0, streakT = 0;
    int  bonusT = 0, cherryT = 0, eatChain = 0;
    bool cherryUp = false, hunt = false;
    int  fireCool = 0, digSfxT = 0;
    int  stateT = 0, phase = 0, shake = 0, levelT = 0;
    unsigned lastBeat = 0;
    GState state = GS_TITLE;
    bool paused = false;
    unsigned rng = 12345;
} G;

static inline unsigned rnd() { G.rng = G.rng * 1103515245u + 12345u; return (G.rng >> 16) & 0x7FFF; }

// ---------------------------- field primitives -----------------------------
static void clearSoil(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > FLDW - 1) x1 = FLDW - 1; if (y1 > FLDH - 1) y1 = FLDH - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) g_dirt[y][x] = 0;
}
static int digAt(int px, int py)
{
    int x0 = px + 3, x1 = px + CELL - 4, y0 = py, y1 = py + CELL - 1;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > FLDW - 1) x1 = FLDW - 1; if (y1 > FLDH - 1) y1 = FLDH - 1;
    int removed = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (g_dirt[y][x]) { g_dirt[y][x] = 0; removed++; }
    if (removed) rebuildSoil(x0 - 8, y0 - 8, x1 + 8, y1 + 8);
    return removed;
}
static void digCellFull(int cx, int cy)
{
    clearSoil(cx * CELL, cy * CELL, cx * CELL + CELL - 1, cy * CELL + CELL - 1);
}
static bool areaClear(int px, int py)
{
    if (px < 0 || py < 0 || px + CELL > FLDW || py + CELL > FLDH) return false;
    for (int y = py + 8; y <= py + CELL - 9; y++)
        for (int x = px + 8; x <= px + CELL - 9; x++)
            if (g_dirt[y][x]) return false;
    return true;
}
static bool hasSoil(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > FLDW - 1) x1 = FLDW - 1; if (y1 > FLDH - 1) y1 = FLDH - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) if (g_dirt[y][x]) return true;
    return false;
}
static inline bool overlap(int ax, int ay, int bx, int by, int mx, int my)
{
    int dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
    return dx < mx && dy < my;
}
static int bagAtCell(int cx, int cy)
{
    for (int i = 0; i < (int)G.bags.size(); i++) {
        const Bag& b = G.bags[i];
        if (b.state == BAG_GOLD) continue;
        if ((b.x + CELL / 2) / CELL == cx && (b.y + CELL / 2) / CELL == cy) return i;
    }
    return -1;
}
// Any gold in a cell, sack or spilled pile alike -- used to wall the borer off.
static bool goldOrBagAtCell(int cx, int cy)
{
    for (const Bag& b : G.bags)
        if ((b.x + CELL / 2) / CELL == cx && (b.y + CELL / 2) / CELL == cy) return true;
    return false;
}
static void addScore(int n)
{
    G.score += n;
    if (G.score >= G.nextLife) {
        G.lives++; G.nextLife += 20000;
        playSound(SND_TUNE, 1.5f, 0.9f);
    }
}

// ------------------------------ level setup --------------------------------
static void loadLevel(int lv)
{
    GenResult r;
    genLevel(lv, r);
    G.cfg = cfgFor(lv);

    memset(g_dirt, 1, sizeof(g_dirt));
    memset(G.gem, 0, sizeof(G.gem));
    G.bags.clear(); G.mons.clear();
    G.bul.alive = false; G.fireCool = 0;
    for (Shell& sh : G.shells) sh.alive = false;
    G.cherryUp = false; G.bonusT = 0; G.cherryT = 60 * 22; G.eatChain = 0;
    G.streak = 0; G.streakT = 0; G.gemLeft = 0;
    G.hunt = false; G.borersOut = 0;
    g_partN = 0;

    for (int cy = 0; cy < FH; cy++) {
        for (int cx = 0; cx < FW; cx++) {
            if (r.dug[cy][cx]) digCellFull(cx, cy);
            if (r.gem[cy][cx]) { G.gem[cy][cx] = true; G.gemLeft++; }
            if (r.bag[cy][cx]) { Bag b{}; b.x = cx * CELL; b.y = cy * CELL; G.bags.push_back(b); }
        }
    }
    G.gemTotal = G.gemLeft;
    G.startCx = r.startCx; G.startCy = r.startCy;
    G.denCx = r.denCx;     G.denCy = r.denCy;
    G.chCx = r.chCx;       G.chCy = r.chCy;

    G.monLeft = G.cfg.monTotal;
    G.spawnGap = 30;                     // the den opens promptly
    G.levelT = 0;
    rebuildSoilAll();
}
static void placeDigger()
{
    G.dig.x = G.startCx * CELL; G.dig.y = G.startCy * CELL;
    G.dig.dir = DIR_R; G.dig.face = DIR_R; G.dig.acc = 0;
    G.dig.moveLeft = 0; G.dig.holdT = 0; G.dig.stepQueued = false;
    G.dig.alive = true; G.dig.anim = 0; G.dig.pushBag = -1;
    digAt(G.dig.x, G.dig.y);
}
static void restartAfterDeath()
{
    // Dying must not cost the stage its monsters.  The ones on screen go back
    // into the pool to be released again -- otherwise the roster shrinks with
    // every life and the stage gets *easier* the more you fail, which also
    // quietly lowers the "kill them all" bar for clearing it.
    for (const Monster& m : G.mons)
        if (m.kind == MK_BORER && G.borersOut > 0) G.borersOut--;
    G.monLeft += (int)G.mons.size();
    G.mons.clear();
    G.bul.alive = false;
    for (Shell& sh : G.shells) sh.alive = false;
    G.bonusT = 0; G.eatChain = 0; G.streak = 0;
    G.spawnGap = 150;
    for (Bag& b : G.bags)
        if (b.state == BAG_WOBBLE || b.state == BAG_FALL) { b.state = BAG_IDLE; b.wob = 0; }
    placeDigger();
}
static void newGame()
{
    G.level = 0; G.score = 0; G.lives = 3; G.nextLife = 20000;
    aiReset();
    loadLevel(G.level); placeDigger();
    G.state = GS_PLAY; G.stateT = 0; G.paused = false;
    playSound(SND_TUNE, 1.0f, 0.9f);
}

// ------------------------------ monster spawn ------------------------------
// Different creatures own different parts of the mix, so a crowd of them reads
// as a spectrum spread across the field: borers hold the bass, nobbins the
// mids, hobbins the treble.  Within a register they fan out by spawn order.
static int monsterBand(int kind, unsigned seq)
{
    int lo, hi;
    switch (kind) {
    case MK_BORER:  lo = 0;  hi = 3; break;                  // 55-150 Hz, the heavies
    case MK_NOBBIN: lo = 4;  hi = 8; break;                   // midrange
    default:        lo = 9;  hi = NUM_BANDS - 1; break;       // hobbins ride the highs
    }
    return lo + (int)(seq % (unsigned)(hi - lo + 1));
}
static int liveBorers()
{
    int n = 0;
    for (const Monster& m : G.mons) if (m.alive && m.kind == MK_BORER) n++;
    return n;
}
static void spawnMonster()
{
    Monster m{};
    m.x = G.denCx * CELL; m.y = G.denCy * CELL;
    m.dir = DIR_L; m.acc = 0;
    m.alive = true; m.flee = false;
    m.dodgeDir = -1; m.dodgeLock = 0; m.dodgeT = 0; m.taunt = 0; m.digSfx = 0;
    m.dodgeSeen = 0; m.dodgeCool = 0;

    // Borers chew through soil, so they are rationed twice over: a per-stage
    // budget and a cap on how many may be loose at the same time.
    bool wantBorer = G.borersOut < G.cfg.borerMax &&
                     liveBorers() < G.cfg.borerLive &&
                     (int)(rnd() % 100) < 45;
    if (wantBorer) { m.kind = MK_BORER; G.borersOut++; }
    else m.kind = ((int)(rnd() % 100) < G.cfg.hobChance) ? MK_HOBBIN : MK_NOBBIN;

    m.tier = 0;
    if (G.cfg.maxTier > 0) {
        int roll = (int)(rnd() % 100);
        if (roll < 45) m.tier = 0;
        else if (roll < 75) m.tier = 1;
        else if (roll < 92) m.tier = 2;
        else m.tier = 3;
        if (m.tier > G.cfg.maxTier) m.tier = G.cfg.maxTier;
    }
    m.speed = G.cfg.monSpeed + m.tier * 15 +
              (m.kind == MK_HOBBIN ? 20 : 0) - (m.kind == MK_BORER ? 40 : 0);
    m.stun = 0; m.anim = 0; m.pushBag = -1; m.lastDodge = -1;
    m.band = monsterBand(m.kind, G.spawnSeq++);
    G.mons.push_back(m);
    G.monLeft--;
    playSound(m.kind == MK_BORER ? SND_BORER : SND_SPAWN,
              m.kind == MK_BORER ? 1.0f : 0.9f + (rnd() % 20) * 0.01f, 0.55f);
}

// ------------------------------ digger logic -------------------------------
// Decides whether a whole-cell move in `dir` may start, and arranges any push.
static bool beginMove(int dir)
{
    Digger& d = G.dig;
    d.pushBag = -1;
    int cx = d.x / CELL, cy = d.y / CELL;
    int ncx = cx + DX[dir], ncy = cy + DY[dir];
    if (ncx < 0 || ncx >= FW || ncy < 0 || ncy >= FH) return false;
    int bi = bagAtCell(ncx, ncy);
    if (bi >= 0) {
        if (DY[dir] != 0) return false;                  // no pushing up or down
        int bcx = ncx + DX[dir];
        if (bcx < 0 || bcx >= FW) return false;          // can't shove off the field
        if (bagAtCell(bcx, ncy) >= 0) return false;      // another bag is in the way
        // The tank can shove a sack even into undug soil: the push carves the
        // destination as the sack slides into it.  Only another sack or the
        // wall stops it.
        G.bags[bi].state = BAG_IDLE; G.bags[bi].wob = 0;
        d.pushBag = bi;
    }
    return true;
}
static void diggerStep()
{
    Digger& d = G.dig;
    if (d.pushBag >= 0) G.bags[d.pushBag].hold = 1;
    d.x += DX[d.dir]; d.y += DY[d.dir];
    if (d.pushBag >= 0) {
        Bag& pb = G.bags[d.pushBag];
        pb.x += DX[d.dir];
        digAt(pb.x, pb.y);                            // the sack carves its own path
        int bx = (pb.x + CELL / 2) / CELL, by = (pb.y + CELL / 2) / CELL;
        if (bx >= 0 && bx < FW && by >= 0 && by < FH && G.gem[by][bx]) {
            G.gem[by][bx] = false; G.gemLeft--;      // shoved over, no score
        }
    }

    int removed = digAt(d.x, d.y);
    if (removed > 40) {
        if (--G.digSfxT <= 0) {
            playSound(SND_DIG, 0.9f + (rnd() % 25) * 0.01f, 0.42f);
            G.digSfxT = 9;
        }
        float bx = (float)(d.x + CELL * 0.5f) - DX[d.dir] * 14.0f;
        float by = (float)(d.y + CELL * 0.5f) - DY[d.dir] * 14.0f;
        if ((rnd() & 3) == 0)
            burstParts(P_SOIL, bx, by, 2, 1.5f, 0.45f, 2.4f, 0x8A4A22);
    }

    int ecx = (d.x + CELL / 2) / CELL, ecy = (d.y + CELL / 2) / CELL;
    if (ecx >= 0 && ecx < FW && ecy >= 0 && ecy < FH && G.gem[ecy][ecx]) {
        G.gem[ecy][ecx] = false; G.gemLeft--;
        addScore(25);
        G.streak++; G.streakT = 55;
        burstParts(P_GEMBIT, ecx * CELL + CELL * 0.5f, ecy * CELL + CELL * 0.5f,
                   7, 2.2f, 0.5f, 2.2f, C_GEM_LT);
        if (G.streak >= 8) {
            G.streak = 0; addScore(250);
            playSound(SND_TUNE, 1.35f, 0.75f);
        } else {
            playSound(SND_GEM, 1.0f + G.streak * 0.06f, 0.55f);
        }
    }
}
static void fireBullet()
{
    if (G.bul.alive || G.fireCool > 0 || !G.dig.alive) return;
    G.bul.x = G.dig.x + CELL / 2; G.bul.y = G.dig.y + CELL / 2;
    G.bul.dir = G.dig.face; G.bul.alive = true;
    G.bul.id++;                                   // each shot gets one dodge roll
    G.fireCool = 40;
    for (int i = 0; i < 4; i++) g_ai.fireDir[i] *= 0.93f;
    g_ai.fireDir[G.dig.face] += 1.0f;
    g_ai.aggression = g_ai.aggression * 0.97f + 0.03f * 60.0f / 40.0f;
    g_ai.shots++;
    playSound(SND_FIRE, 1.0f, 0.7f);
}
static void killDigger()
{
    if (!G.dig.alive) return;
    G.dig.alive = false;
    G.state = GS_DYING; G.stateT = 130;
    G.shake = 26;
    burstParts(P_SPARK, (float)G.dig.x + CELL * 0.5f, (float)G.dig.y + CELL * 0.5f,
               26, 4.0f, 0.9f, 3.4f, 0xFF8040);
    burstParts(P_SMOKE, (float)G.dig.x + CELL * 0.5f, (float)G.dig.y + CELL * 0.5f,
               12, 1.4f, 1.4f, 6.0f, 0x707070);
    stopAllSounds();
    playSound(SND_DEATH, 1.0f, 1.0f);
}
static void hitMonster(Monster& m)
{
    if (m.tier > 0) {
        m.tier--;
        m.stun = 26;
        m.speed = G.cfg.monSpeed + m.tier * 15 +
                  (m.kind == MK_HOBBIN ? 20 : 0) - (m.kind == MK_BORER ? 40 : 0);
        addScore(50);
        burstParts(P_SPARK, (float)m.x + CELL * 0.5f, (float)m.y + CELL * 0.5f,
                   10, 3.0f, 0.4f, 2.4f, 0xFFE080);
        playSound(SND_HIT, 1.0f + m.tier * 0.12f, 0.8f);
    } else {
        m.alive = false;
        addScore(250);
        G.shake = 8;
        burstParts(P_SPARK, (float)m.x + CELL * 0.5f, (float)m.y + CELL * 0.5f,
                   18, 3.6f, 0.7f, 3.0f, 0xFF60C0);
        burstParts(P_SMOKE, (float)m.x + CELL * 0.5f, (float)m.y + CELL * 0.5f,
                   6, 1.0f, 1.0f, 5.0f, 0x604050);
        playSound(SND_KILL, 1.0f, 0.85f);
    }
}

// ------------------------------ monster logic ------------------------------
// Nobbins and hobbins are confined to open tunnels.  A borer passes straight
// through soil and leaves it undisturbed -- it carves no passage for the
// player to reuse and takes no emeralds, it is purely a threat that walls do
// not stop.  It can never touch a gold bag either.
static bool monsterCanEnter(const Monster& m, int cx, int cy, int dir)
{
    if (cx < 0 || cx >= FW || cy < 0 || cy >= FH) return false;
    if (m.kind != MK_BORER && !areaClear(cx * CELL, cy * CELL)) return false;
    // Gold stops a borer cold: it pierces earth, but not the metal.  It is
    // blocked by a spilled pile as well as a standing sack.
    if (m.kind == MK_BORER) return !goldOrBagAtCell(cx, cy);
    if (bagAtCell(cx, cy) >= 0) {
        if (m.kind != MK_HOBBIN || DY[dir] != 0) return false;
        int bcx = cx + DX[dir];
        if (bcx < 0 || bcx >= FW) return false;
        if (!areaClear(bcx * CELL, cy * CELL)) return false;
        if (bagAtCell(bcx, cy) >= 0) return false;
    }
    return true;
}
// ------------------------------ path finding -------------------------------
// Straight-line distance is useless in a tunnel network: the direction that
// looks closer is often a dead end, and a greedy monster just grinds against
// the wall there and never arrives.  So each tick we flood-fill the walk
// distance from the player across open cells; monsters then simply step
// downhill on that field, which is genuine pursuit through the maze.
static short g_pathTunnel[FH][FW];      // for nobbins/hobbins: open cells only
static const short PATH_INF = 30000;

static void buildPathField()
{
    for (int cy = 0; cy < FH; cy++)
        for (int cx = 0; cx < FW; cx++) g_pathTunnel[cy][cx] = PATH_INF;
    if (!G.dig.alive) return;

    int scx = (G.dig.x + CELL / 2) / CELL, scy = (G.dig.y + CELL / 2) / CELL;
    if (scx < 0 || scx >= FW || scy < 0 || scy >= FH) return;

    int qx[FW * FH], qy[FW * FH], head = 0, tail = 0;
    g_pathTunnel[scy][scx] = 0;
    qx[tail] = scx; qy[tail++] = scy;
    while (head < tail) {
        int cx = qx[head], cy = qy[head]; head++;
        short d = g_pathTunnel[cy][cx];
        for (int t = 0; t < 4; t++) {
            int nx = cx + DX[t], ny = cy + DY[t];
            if (nx < 0 || nx >= FW || ny < 0 || ny >= FH) continue;
            if (g_pathTunnel[ny][nx] != PATH_INF) continue;
            if (!areaClear(nx * CELL, ny * CELL)) continue;   // walls block walkers
            g_pathTunnel[ny][nx] = d + 1;
            qx[tail] = nx; qy[tail++] = ny;
        }
    }
}
// Walk distance to the player for this monster: through tunnels for walkers,
// straight-line for a borer, which ignores walls anyway.
static int pathCost(const Monster& m, int cx, int cy, int tx, int ty)
{
    if (m.kind == MK_BORER) {
        int dx = cx - tx, dy = cy - ty;
        if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
        return dx + dy;
    }
    if (cx < 0 || cx >= FW || cy < 0 || cy >= FH) return PATH_INF;
    return g_pathTunnel[cy][cx];
}

static void monsterStep(Monster& m)
{
    bool aligned = (m.x % CELL == 0) && (m.y % CELL == 0);
    if (aligned) {
        int cx = m.x / CELL, cy = m.y / CELL;
        m.pushBag = -1;
        // a queued sidestep takes priority over the chase
        if (m.dodgeDir >= 0) {
            if (monsterCanEnter(m, cx + DX[m.dodgeDir], cy + DY[m.dodgeDir], m.dodgeDir))
                m.dir = m.dodgeDir;
            m.dodgeDir = -1;
        } else {
            int dcx = (G.dig.x + CELL / 2) / CELL, dcy = (G.dig.y + CELL / 2) / CELL;
            // Adaptive weight.  In hunt mode it is cut right back: once the ore
            // is gone the stage can only end by killing everything, so the
            // survivors must come to you instead of playing keep-away.
            int aiW = G.cfg.aiWeight;
            if (G.hunt) aiW /= 4;
            if (m.flee || G.bonusT > 0) aiW = 0;

            // Cut you off rather than trail you: aim where you are heading.
            // (Only the borer aims at the lead point -- a walker has to follow
            // the tunnel field, which already leads to where you actually are.)
            int tx = dcx, ty = dcy;
            if (aiW > 0 && m.kind == MK_BORER) {
                tx += (int)(g_ai.velX * 2.4f * aiW / 100.0f);
                ty += (int)(g_ai.velY * 2.4f * aiW / 100.0f);
                if (tx < 0) tx = 0; if (tx >= FW) tx = FW - 1;
                if (ty < 0) ty = 0; if (ty >= FH) ty = FH - 1;
            }
            int here = pathCost(m, cx, cy, tx, ty);
            int best = -1, bestScore = 1 << 30, back = (m.dir + 2) & 3;
            bool any = false;
            for (int t = 0; t < 4; t++) {
                if (!monsterCanEnter(m, cx + DX[t], cy + DY[t], t)) continue;
                any = true;
                int nx = cx + DX[t], ny = cy + DY[t];
                // The chase dominates: walk distance to the player through the
                // tunnels is the main driver.  A monster hunts the tank; it does
                // not run from it, and it does not stall against a dead end.
                int cost = pathCost(m, nx, ny, tx, ty);
                int sc;
                if (cost >= PATH_INF) {
                    // unreachable from there -- only worth taking if we are
                    // already stranded ourselves
                    sc = (here >= PATH_INF) ? 400 : 4000;
                } else {
                    sc = cost * 22;
                }
                if (m.flee || G.bonusT > 0) sc = -sc;
                if (t == back) sc += 200;

                if (aiW > 0) {
                    // Smarts, not cowardice: only a light nudge to prefer NOT
                    // approaching straight down the barrel at range.  It is
                    // capped well below one step of chase value so it can pick
                    // between equally-good approaches but never call off the
                    // hunt, and it is switched off from close range (rel < 4)
                    // so the final pounce always commits.  Actual incoming
                    // shots are handled reactively by the in-flight dodge.
                    int fdx = DX[G.dig.face], fdy = DY[G.dig.face];
                    bool sameLine = (fdy == 0) ? (ny == dcy) : (nx == dcx);
                    int rel = (nx - dcx) * fdx + (ny - dcy) * fdy;
                    if (sameLine && rel >= 4 && rel <= 15) {
                        float aggro = g_ai.aggression;
                        if (aggro > 1.6f) aggro = 1.6f;
                        if (aggro < 0.4f) aggro = 0.4f;
                        float lr = g_ai.fireDir[DIR_L] + g_ai.fireDir[DIR_R];
                        float ud = g_ai.fireDir[DIR_U] + g_ai.fireDir[DIR_D];
                        float axisBias = 1.0f;
                        if (lr + ud > 0.5f)
                            axisBias = (fdy == 0) ? (0.7f + 0.9f * lr / (lr + ud))
                                                  : (0.7f + 0.9f * ud / (lr + ud));
                        float danger = (16.0f - rel) / 12.0f;          // 0..1
                        int pen = (int)(danger * 10.0f * aggro * axisBias * aiW / 100.0f);
                        if (pen > 12) pen = 12;                        // stays a nudge
                        sc += pen;
                    }
                }
                sc += G.hunt ? (int)(rnd() % 6) : (int)(rnd() % 18);
                if (sc < bestScore) { bestScore = sc; best = t; }
            }
            if (!any) return;
            m.dir = best;
        }
        int bi = bagAtCell(cx + DX[m.dir], cy + DY[m.dir]);
        if (bi >= 0 && m.kind == MK_HOBBIN) {
            G.bags[bi].state = BAG_IDLE; G.bags[bi].wob = 0; m.pushBag = bi;
        }
    }
    if (m.pushBag >= 0) G.bags[m.pushBag].hold = 1;
    int nx = m.x + DX[m.dir], ny = m.y + DY[m.dir];
    if (nx < 0 || ny < 0 || nx + CELL > FLDW || ny + CELL > FLDH) return;
    m.x = nx; m.y = ny;
    if (m.pushBag >= 0) G.bags[m.pushBag].x += DX[m.dir];

    // A borer inside the earth grinds and throws up dust, but the soil closes
    // behind it: no tunnel is left, and the emeralds it passes stay put.
    if (m.kind == MK_BORER && !areaClear(m.x, m.y)) {
        if (--m.digSfx <= 0) { playSound(SND_BORER, 0.85f, 0.26f); m.digSfx = 16; }
        if ((rnd() & 7) == 0)
            burstParts(P_SOIL, (float)m.x + CELL * 0.5f, (float)m.y + CELL * 0.5f,
                       1, 1.0f, 0.35f, 1.8f, 0x7A4020);
    }
}

// Evasion: when a shot is inbound and far enough away to react to, a monster
// may sidestep.  Skill scales with the stage; a clean dodge earns a jeer.
// Every kind of monster evades -- nobbin, hobbin and borer alike.  The skill
// climbs for the whole 99-stage run instead of levelling off, and the escape
// direction is chosen from what has actually worked against this player.
static void tryDodges()
{
    if (!G.bul.alive) return;
    int skill = G.cfg.dodgeSkill;
    if (G.hunt) skill /= 2;                      // the endgame has to stay winnable
    if (skill <= 0) return;

    bool horiz = (G.bul.dir == DIR_R || G.bul.dir == DIR_L);
    for (Monster& m : G.mons) {
        // dodgeT is only the jeer window; it must not lock out reacting to the
        // next shot, since the per-fireball id already prevents double rolls.
        // dodgeCool is different: after a sidestep it has to spend some time
        // actually closing in again.  Without it a monster under sustained fire
        // sidesteps every shot, drifts laterally forever and never arrives --
        // which is the "it just avoids me" behaviour we are trying to kill.
        if (!m.alive || m.stun > 0 || m.dodgeCool > 0) continue;
        int mcx = m.x + CELL / 2, mcy = m.y + CELL / 2;
        int along, across;
        if (horiz) { along = (mcx - G.bul.x) * DX[G.bul.dir]; across = mcy - G.bul.y; }
        else       { along = (mcy - G.bul.y) * DY[G.bul.dir]; across = mcx - G.bul.x; }
        if (across < 0) across = -across;
        // React early enough that it can reach the next grid point and then
        // clear the line before the shot arrives; the fireball covers 9px a
        // tick, so the far edge has to be generous.
        if (across > 20 || along < 60 || along > 520) continue;
        // One roll per fireball, not one per tick.  Rolling every tick gave the
        // monster ~37 chances at each shot, which turned any non-zero skill into
        // a near-certainty and flattened the whole difficulty curve.
        if (m.dodgeSeen == G.bul.id) continue;
        m.dodgeSeen = G.bul.id;
        if ((int)(rnd() % 100) >= skill) continue;   // this one it did not read

        int cx = m.x / CELL, cy = m.y / CELL;
        int a = horiz ? DIR_U : DIR_L, b = horiz ? DIR_D : DIR_R;
        bool okA = monsterCanEnter(m, cx + DX[a], cy + DY[a], a);
        bool okB = monsterCanEnter(m, cx + DX[b], cy + DY[b], b);
        int pick = -1;
        if (okA && okB) {
            // Both lanes open: step aside on the side that also gets it closer
            // to the tank, so evading still advances the hunt rather than
            // trading it away.  Learned preference only breaks ties.
            int dcx = (G.dig.x + CELL / 2) / CELL, dcy = (G.dig.y + CELL / 2) / CELL;
            int ca = pathCost(m, cx + DX[a], cy + DY[a], dcx, dcy);
            int cb = pathCost(m, cx + DX[b], cy + DY[b], dcx, dcy);
            if (ca != cb) pick = (ca < cb) ? a : b;
            else {
                float wa = g_ai.dodgeWin[a], wb = g_ai.dodgeWin[b];
                float lean = (wa + wb > 0.01f) ? wa / (wa + wb) : 0.5f;
                lean = 0.5f + (lean - 0.5f) * (G.cfg.aiWeight / 100.0f);
                pick = ((rnd() % 1000) < (unsigned)(lean * 1000)) ? a : b;
            }
        } else if (okA) pick = a;
        else if (okB) pick = b;
        if (pick < 0) continue;

        m.dodgeDir = pick;
        m.lastDodge = pick;                       // dodgeDir is consumed on the step
        m.dodgeLock = 34;
        m.dodgeT = 100;
        m.dodgeCool = 58;                         // now get back to hunting
    }
}
// ------------------------------ enemy gunnery ------------------------------
static int freeShellSlot()
{
    for (int i = 0; i < MAX_SHELLS; i++) if (!G.shells[i].alive) return i;
    return -1;
}
// True when the tank has an unobstructed straight shot at the digger, and hands
// back the direction to fire.  A shot is only ever taken down an open lane, so
// the player can always see it coming and always break it by stepping aside.
static bool enemyHasShot(const Monster& m, int& outDir)
{
    int mcx = (m.x + CELL / 2) / CELL, mcy = (m.y + CELL / 2) / CELL;
    int dcx = (G.dig.x + CELL / 2) / CELL, dcy = (G.dig.y + CELL / 2) / CELL;
    int dir, dist;
    if (mcy == dcy && mcx != dcx) {
        dir = (dcx > mcx) ? DIR_R : DIR_L;
        dist = dcx - mcx; if (dist < 0) dist = -dist;
    } else if (mcx == dcx && mcy != dcy) {
        dir = (dcy > mcy) ? DIR_D : DIR_U;
        dist = dcy - mcy; if (dist < 0) dist = -dist;
    } else return false;
    if (dist > G.cfg.gunRange) return false;

    // every cell between must be open -- a drill tank does not get to shoot
    // through the earth it can drive through
    for (int s = 1; s <= dist; s++) {
        int tx = mcx + DX[dir] * s, ty = mcy + DY[dir] * s;
        if (tx < 0 || tx >= FW || ty < 0 || ty >= FH) return false;
        if (!areaClear(tx * CELL, ty * CELL)) return false;
        if (goldOrBagAtCell(tx, ty)) return false;
    }
    outDir = dir;
    return true;
}
static void updateEnemyGuns()
{
    for (Monster& m : G.mons) {
        if (!m.alive) continue;
        if (m.gunCool > 0) m.gunCool--;
        if (m.flee || G.bonusT > 0) { m.aimT = 0; continue; }   // no shooting while fleeing
        if (m.stun > 0) { m.aimT = 0; continue; }

        if (m.aimT > 0) {
            if (--m.aimT == 0) {                  // windup finished: fire
                int slot = freeShellSlot();
                if (slot >= 0) {
                    Shell& sh = G.shells[slot];
                    sh.alive = true; sh.dir = m.aimDir;
                    sh.x = m.x + CELL / 2 + DX[m.aimDir] * 20;
                    sh.y = m.y + CELL / 2 + DY[m.aimDir] * 20;
                    m.gunCool = G.cfg.gunReload;
                    burstParts(P_SPARK, (float)sh.x, (float)sh.y, 8, 2.6f, 0.3f, 2.2f, 0xFFB060);
                    playSound(SND_FIRE, 0.62f, 0.55f);   // deeper than the player's
                }
            }
            continue;
        }
        int dir;
        if (!enemyHasShot(m, dir)) { m.acquire = 0; continue; }   // lost the line

        // Sighted -- open fire.  No acquisition delay at any stage: the gun
        // swings on and the muzzle starts flashing up in the same tick.
        // `acquire` is now just a "has the target" flag, so the barrel keeps
        // tracking you between shots while it reloads.
        m.aimDir = dir;
        m.acquire = 1;
        if (m.gunCool > 0) continue;
        if (freeShellSlot() < 0) continue;
        m.aimT = AIM_TICKS;
        playSound(SND_HIT, 0.5f, 0.30f);          // the gun snapping onto you
    }
}
// Player fire and enemy fire annihilate on contact.  Worth doing well: shooting
// down an incoming round is the most satisfying defensive move in the game.
static void clashEffect(int ax, int ay, int bx, int by)
{
    float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;
    burstParts(P_SPARK, mx, my, 18, 3.6f, 0.45f, 2.6f, 0xFFE8B0);
    burstParts(P_SMOKE, mx, my, 5, 0.9f, 0.8f, 4.5f, 0x8A8A8A);
    glowCircle(FLDX + mx, FLDY + my, 20.0f, 0xFFC060, 0.9f);
    G.shake = 7;
    addScore(50);                                 // a skill shot deserves paying
    playSound(SND_HIT, 1.7f, 0.9f);
}
// Checked at 1px granularity from *both* sides: the two projectiles close at
// 15px a tick, so a once-per-tick test would let them pass through each other.
static bool bulletMeetsShell(int bx, int by)
{
    if (!G.bul.alive) return false;
    for (Shell& sh : G.shells) {
        if (!sh.alive) continue;
        if (!overlap(bx, by, sh.x, sh.y, 14, 13)) continue;
        clashEffect(bx, by, sh.x, sh.y);
        sh.alive = false;
        G.bul.alive = false;
        return true;
    }
    return false;
}
static void updateShells()
{
    for (int i = 0; i < MAX_SHELLS; i++) {
        Shell& sh = G.shells[i];
        if (!sh.alive) continue;
        for (int s = 0; s < SHELL_SPEED && sh.alive; s++) {
            sh.x += DX[sh.dir]; sh.y += DY[sh.dir];
            if (sh.x < 3 || sh.y < 3 || sh.x >= FLDW - 3 || sh.y >= FLDH - 3) {
                sh.alive = false; break;
            }
            if (hasSoil(sh.x - 3, sh.y - 3, sh.x + 3, sh.y + 3)) {
                burstParts(P_SOIL, (float)sh.x, (float)sh.y, 4, 1.6f, 0.3f, 2.0f, 0x9A5A2A);
                sh.alive = false; break;
            }
            // a sack soaks an enemy shell rather than bursting -- only the
            // player's fireball opens them
            bool blocked = false;
            for (const Bag& b : G.bags)
                if (overlap(sh.x, sh.y, b.x + CELL / 2, b.y + CELL / 2, 22, 21)) {
                    blocked = true; break;
                }
            if (blocked) {
                burstParts(P_SPARK, (float)sh.x, (float)sh.y, 5, 2.0f, 0.3f, 2.0f, 0xFFD070);
                sh.alive = false; break;
            }
            if (bulletMeetsShell(G.bul.x, G.bul.y)) break;   // shot out of the air
            if ((s & 2) == 0)
                spawnPart(P_SPARK, (float)sh.x, (float)sh.y, 0, 0, 0.16f, 1.7f, 0xFF8030);
            if (G.dig.alive &&
                overlap(sh.x, sh.y, G.dig.x + CELL / 2, G.dig.y + CELL / 2, 20, 19)) {
                sh.alive = false;
                killDigger();
                break;
            }
        }
    }
}
static void bulletMissed()
{
    for (Monster& m : G.mons) {
        if (m.alive && m.dodgeT > 0 && m.taunt <= 0) {
            m.taunt = 80;
            if (m.lastDodge >= 0) {               // remember what saved it
                for (int i = 0; i < 4; i++) g_ai.dodgeWin[i] *= 0.97f;
                g_ai.dodgeWin[m.lastDodge] += 1.0f;
            }
            playSound(SND_TAUNT, 0.92f + (rnd() % 20) * 0.01f, 0.72f);
            break;                                   // one jeer is enough
        }
    }
}

// ------------------------------- bag logic ---------------------------------
static bool bagBlockedBelow(const Bag& b)
{
    if (b.y + CELL >= FLDH) return true;
    if (hasSoil(b.x + 8, b.y + CELL, b.x + CELL - 9, b.y + CELL)) return true;
    for (const Bag& o : G.bags) {
        if (&o == &b || o.state == BAG_GOLD) continue;
        if (overlap(b.x, b.y + 3, o.x, o.y, CELL - 8, CELL)) return true;
    }
    return false;
}
static void updateBags()
{
    for (int i = 0; i < (int)G.bags.size(); i++) {
        Bag& b = G.bags[i];
        if (b.state == BAG_GOLD) continue;
        if (b.hold && b.state != BAG_FALL) { b.state = BAG_IDLE; b.wob = 0; continue; }

        if (b.state == BAG_IDLE) {
            if (!bagBlockedBelow(b)) { b.state = BAG_WOBBLE; b.wob = 34; }
        } else if (b.state == BAG_WOBBLE) {
            if (bagBlockedBelow(b)) { b.state = BAG_IDLE; b.wob = 0; }
            else if (--b.wob <= 0) {
                b.state = BAG_FALL; b.fallFrom = b.y; b.vspeed = 140; b.vacc = 0;
                playSound(SND_FALL, 1.0f, 0.7f);
            }
        } else if (b.state == BAG_FALL) {
            b.vspeed += 22; if (b.vspeed > 2048) b.vspeed = 2048;
            b.vacc += b.vspeed;
            while (b.vacc >= 256) {
                b.vacc -= 256;
                if (bagBlockedBelow(b)) {
                    int fell = b.y - b.fallFrom;
                    int snap = ((b.y + CELL / 2) / CELL) * CELL;
                    if (snap + CELL <= FLDH && !hasSoil(b.x + 8, snap, b.x + CELL - 9, b.y - 1))
                        b.y = snap;
                    if (fell >= CELL) {
                        b.state = BAG_GOLD;
                        G.shake = 10;
                        burstParts(P_COIN, (float)b.x + CELL * 0.5f, (float)b.y + CELL * 0.5f,
                                   16, 3.2f, 0.7f, 3.0f, C_GOLD);
                        burstParts(P_SOIL, (float)b.x + CELL * 0.5f, (float)b.y + CELL * 0.8f,
                                   8, 2.0f, 0.5f, 2.6f, 0x8A4A22);
                        playSound(SND_BREAK, 1.0f, 0.85f);
                    } else b.state = BAG_IDLE;
                    b.vacc = 0;
                    break;
                }
                b.y++;
                if (G.dig.alive && overlap(b.x, b.y, G.dig.x, G.dig.y, CELL - 14, CELL - 14))
                    killDigger();
                for (Monster& m : G.mons)
                    if (m.alive && overlap(b.x, b.y, m.x, m.y, CELL - 14, CELL - 14)) {
                        m.alive = false; addScore(250);
                        burstParts(P_SPARK, (float)m.x + CELL * 0.5f, (float)m.y + CELL * 0.5f,
                                   14, 3.0f, 0.6f, 2.8f, 0xFF80C0);
                        playSound(SND_KILL, 0.85f, 0.8f);
                    }
            }
        }
    }
}

// -------------------------------- update -----------------------------------
static bool g_keyDown[256] = { false };
static bool g_keyHit[256] = { false };

static int heldDir()
{
    if (g_keyDown[VK_RIGHT] || g_keyDown['D']) return DIR_R;
    if (g_keyDown[VK_LEFT] || g_keyDown['A']) return DIR_L;
    if (g_keyDown[VK_DOWN] || g_keyDown['S']) return DIR_D;
    if (g_keyDown[VK_UP] || g_keyDown['W']) return DIR_U;
    return DIR_NONE;
}
static int tappedDir()
{
    if (g_keyHit[VK_RIGHT] || g_keyHit['D']) return DIR_R;
    if (g_keyHit[VK_LEFT] || g_keyHit['A']) return DIR_L;
    if (g_keyHit[VK_DOWN] || g_keyHit['S']) return DIR_D;
    if (g_keyHit[VK_UP] || g_keyHit['W']) return DIR_U;
    return DIR_NONE;
}

static void updatePlay()
{
    Digger& d = G.dig;
    for (Bag& b : G.bags) b.hold = 0;
    G.phase++;
    if (G.shake > 0) G.shake--;

    const bool beatMode = musicActive();
    bool onBeat = false;
    if (beatMode && g_beatCount != G.lastBeat) { G.lastBeat = g_beatCount; onBeat = true; }

    // ---- controls: tap to turn, tap again to step, hold to drive -----------
    int want = heldDir();
    int tap = tappedDir();
    if (tap != DIR_NONE) {
        if (tap != d.face) d.face = tap;            // a tap only turns on the spot
        else d.stepQueued = true;                   // already facing that way: one cell
    }
    if (want != DIR_NONE && want == d.face) d.holdT++; else d.holdT = 0;
    bool holdMove = (d.holdT >= HOLD_TICKS);

    bool upHeld = g_keyDown[VK_UP] || g_keyDown['W'];
    if (g_keyHit[VK_SPACE] || g_keyHit[VK_CONTROL]) fireBullet();
    if (G.fireCool > 0) G.fireCool--;

    if (d.alive && upHeld) {                         // hold a bag up from underneath
        for (Bag& b : G.bags) {
            if (b.state == BAG_GOLD || b.state == BAG_FALL) continue;
            int dx = b.x - d.x, dy = (b.y + CELL) - d.y;
            if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
            if (dx <= 22 && dy <= 6) b.hold = 1;
        }
    }

    bool aligned = (d.x % CELL == 0) && (d.y % CELL == 0);
    if (d.alive && d.moveLeft == 0 && aligned && (d.stepQueued || holdMove)) {
        if (beginMove(d.face)) { d.dir = d.face; d.moveLeft = CELL; }
        d.stepQueued = false;
    }
    if (d.alive && d.moveLeft > 0) {
        d.acc += DIG_SPEED;
        while (d.acc >= 256 && d.moveLeft > 0) { d.acc -= 256; diggerStep(); d.moveLeft--; }
        d.anim++;
    } else if (d.moveLeft == 0) d.pushBag = -1;

    // ---- keep reading the player -------------------------------------------
    {
        bool moving = (d.moveLeft > 0);
        g_ai.camp = g_ai.camp * 0.996f + (moving ? 0.0f : 0.004f);
        if (moving) {
            g_ai.velX = g_ai.velX * 0.94f + DX[d.dir] * 0.06f;
            g_ai.velY = g_ai.velY * 0.94f + DY[d.dir] * 0.06f;
            g_ai.axisPref = g_ai.axisPref * 0.995f +
                            ((d.dir == DIR_L || d.dir == DIR_R) ? 0.005f : -0.005f);
        } else {
            g_ai.velX *= 0.97f; g_ai.velY *= 0.97f;
        }
        g_ai.aggression *= 0.9994f;
    }

    if (G.streakT > 0 && --G.streakT == 0) G.streak = 0;

    // ---- bullet ------------------------------------------------------------
    // Pursuit map first: the dodge picks its escape lane from it, so it must be
    // current rather than a tick stale.
    buildPathField();
    tryDodges();
    if (G.bul.alive) {
        bool hitSomething = false;
        for (int s = 0; s < 9 && G.bul.alive; s++) {
            G.bul.x += DX[G.bul.dir]; G.bul.y += DY[G.bul.dir];
            if (G.bul.x < 3 || G.bul.y < 3 || G.bul.x >= FLDW - 3 || G.bul.y >= FLDH - 3) {
                G.bul.alive = false; break;
            }
            if (hasSoil(G.bul.x - 3, G.bul.y - 3, G.bul.x + 3, G.bul.y + 3)) {
                burstParts(P_SOIL, (float)G.bul.x, (float)G.bul.y, 5, 1.8f, 0.35f, 2.0f, 0x9A5A2A);
                G.bul.alive = false; break;
            }
            // meeting an incoming shell cancels both -- not a miss, so no jeer
            if (bulletMeetsShell(G.bul.x, G.bul.y)) { hitSomething = true; break; }
            if ((s & 3) == 0)
                spawnPart(P_SPARK, (float)G.bul.x, (float)G.bul.y, 0, 0, 0.18f, 2.0f, C_FIRE_MID);
            for (Monster& m : G.mons) {
                if (!m.alive) continue;
                if (overlap(G.bul.x, G.bul.y, m.x + CELL / 2, m.y + CELL / 2, 22, 21)) {
                    G.bul.alive = false; hitSomething = true;
                    g_ai.hits++;
                    hitMonster(m);
                    break;
                }
            }
            for (Bag& b : G.bags)
                if (b.state != BAG_GOLD &&
                    overlap(G.bul.x, G.bul.y, b.x + CELL / 2, b.y + CELL / 2, 22, 21)) {
                    // a fireball bursts the sack open into a collectable pile
                    b.state = BAG_GOLD; b.wob = 0;
                    burstParts(P_COIN, (float)b.x + CELL * 0.5f, (float)b.y + CELL * 0.5f,
                               14, 3.0f, 0.7f, 3.0f, C_GOLD);
                    burstParts(P_SPARK, (float)b.x + CELL * 0.5f, (float)b.y + CELL * 0.5f,
                               8, 2.6f, 0.4f, 2.4f, C_FIRE_MID);
                    playSound(SND_BREAK, 1.08f, 0.85f);
                    G.bul.alive = false; hitSomething = true; break;
                }
        }
        if (!G.bul.alive && !hitSomething) bulletMissed();
    }

    // ---- monsters ----------------------------------------------------------
    for (Monster& m : G.mons) {
        if (!m.alive) continue;
        if (m.dodgeT > 0) m.dodgeT--;
        if (m.dodgeCool > 0) m.dodgeCool--;
        if (m.taunt > 0) m.taunt--;
        if (m.stun > 0) { m.stun--; m.anim++; continue; }

        int sp = m.speed;
        if (m.dodgeLock > 0) { m.dodgeLock--; sp = sp * 9 / 5; }   // a dodge is a lunge
        if (G.bonusT > 0) sp /= 2;
        if (beatMode) {
            // Surge on the beat, coast between -- but it must average out to
            // roughly full speed, and it must never take over steering.  An
            // earlier version halved their speed and threw in a random
            // direction on every beat, which made them look like they were
            // wandering instead of hunting.
            sp = (int)(sp * (0.80f + 0.45f * g_beatPulse));
        }
        int cap = DIG_SPEED * 85 / 100;              // never outrun the player
        if (sp > cap) sp = cap;
        if (sp < 0) sp = 0;

        m.acc += sp;
        while (m.acc >= 256) { m.acc -= 256; monsterStep(m); }
        m.anim++;

        if (d.alive && overlap(m.x, m.y, d.x, d.y, CELL - 18, CELL - 18)) {
            if (G.bonusT > 0) {
                m.alive = false;
                int pts = 200 << (G.eatChain < 3 ? G.eatChain : 3);
                G.eatChain++;
                addScore(pts);
                burstParts(P_SPARK, (float)m.x + CELL * 0.5f, (float)m.y + CELL * 0.5f,
                           16, 3.2f, 0.6f, 2.8f, 0x80C0FF);
                playSound(SND_EAT, 1.0f + G.eatChain * 0.12f, 0.8f);
            } else killDigger();
        }
    }
    for (int i = (int)G.mons.size() - 1; i >= 0; i--)
        if (!G.mons[i].alive) G.mons.erase(G.mons.begin() + i);

    // ---- release monsters in step with how much ore is gone ----------------
    G.hunt = (G.gemLeft <= 0);
    G.levelT++;
    if (G.monLeft > 0) {
        float prog = G.gemTotal > 0 ? 1.0f - (float)G.gemLeft / G.gemTotal : 1.0f;
        // Mining progress sets the pace, but with a floor of two so there is a
        // real threat from the outset, plus a slow clock-driven trickle so the
        // pressure still builds for a player who mines cautiously.  Gating
        // purely on progress left a lone monster on a 24x15 field, which reads
        // as nothing coming after you at all.
        int wantOut = 2 + (int)(prog * (G.cfg.monTotal - 2) + 0.5f);
        int byClock = 1 + (int)(G.levelT / (14 * 60));
        if (byClock > wantOut) wantOut = byClock;
        if (wantOut > G.cfg.monTotal) wantOut = G.cfg.monTotal;
        if (G.hunt) wantOut = G.cfg.monTotal;
        int released = G.cfg.monTotal - G.monLeft;
        int activeCap = G.cfg.maxActive + (G.hunt ? 2 : 0);
        if (G.spawnGap > 0) G.spawnGap--;
        if (released < wantOut && (int)G.mons.size() < activeCap && G.spawnGap <= 0) {
            spawnMonster();
            G.spawnGap = G.hunt ? 70 : 110;
        }
    }

    updateEnemyGuns();
    updateShells();

    updateBags();
    for (Bag& b : G.bags) {
        if (b.state == BAG_GOLD && d.alive &&
            overlap(b.x, b.y, d.x, d.y, CELL - 10, CELL - 10)) {
            b.state = -1;
            addScore(500);
            burstParts(P_COIN, (float)b.x + CELL * 0.5f, (float)b.y + CELL * 0.5f,
                       12, 2.6f, 0.5f, 2.6f, C_GOLD_LT);
            playSound(SND_GOLD, 1.0f, 0.8f);
        }
    }
    for (int i = (int)G.bags.size() - 1; i >= 0; i--)
        if (G.bags[i].state == -1) G.bags.erase(G.bags.begin() + i);

    // ---- cherry / bonus ----------------------------------------------------
    if (G.bonusT > 0) {
        if (--G.bonusT == 0) { G.eatChain = 0; for (Monster& m : G.mons) m.flee = false; }
    } else if (!G.cherryUp) {
        if (--G.cherryT <= 0) { G.cherryUp = true; G.cherryT = 60 * 14; }
    } else {
        if (--G.cherryT <= 0) G.cherryUp = false;
        if (d.alive && overlap(G.chCx * CELL, G.chCy * CELL, d.x, d.y, CELL - 10, CELL - 10)) {
            G.cherryUp = false; G.cherryT = 60 * 30;
            G.bonusT = 60 * 10; G.eatChain = 0;
            for (Monster& m : G.mons) m.flee = true;
            addScore(1000);
            burstParts(P_SPARK, G.chCx * CELL + CELL * 0.5f, G.chCy * CELL + CELL * 0.5f,
                       22, 3.4f, 0.8f, 3.0f, 0xFF80A0);
            playSound(SND_CHERRY, 1.0f, 0.9f);
        }
    }

    updateParts();

    // The stage ends only when the seam is stripped AND nothing is left alive.
    if (G.gemLeft <= 0 && G.monLeft <= 0 && G.mons.empty()) {
        G.state = GS_CLEAR; G.stateT = 160;
        stopAllSounds();
        playSound(SND_TUNE, 1.0f, 0.95f);
    }
}

static void update()
{
    switch (G.state) {
    case GS_TITLE:
        G.phase++;
        if (g_keyHit[VK_SPACE] || g_keyHit[VK_RETURN]) newGame();
        break;
    case GS_PLAY:
        // The toolbar never pauses the game -- only P does.
        if (!G.paused) updatePlay();
        else G.phase++;
        break;
    case GS_DYING:
        G.phase++;
        updateBags();
        updateParts();
        if (G.shake > 0) G.shake--;
        if (--G.stateT <= 0) {
            if (--G.lives <= 0) { G.state = GS_OVER; G.stateT = 300; }
            else { restartAfterDeath(); G.state = GS_PLAY; }
        }
        break;
    case GS_CLEAR:
        G.phase++;
        updateParts();
        if (--G.stateT <= 0) {
            G.level++;
            if (G.level >= MAXLEVEL) { G.state = GS_WIN; G.stateT = 600; }
            else { loadLevel(G.level); placeDigger(); G.state = GS_PLAY; }
        }
        break;
    case GS_OVER:
    case GS_WIN:
        G.phase++;
        if (--G.stateT <= 0 || g_keyHit[VK_SPACE]) G.state = GS_TITLE;
        break;
    }
    memset(g_keyHit, 0, sizeof(g_keyHit));
}

// -------------------------------- rendering --------------------------------
static void render()
{
    fillRectV(0, 0, SCRW, SCRH, 0x0A0D12, 0x05070A);
    fillRectV(0, TOOLH, SCRW, HUDH - 6, C_PANEL_TOP, C_PANEL_BOT);
    fillRect(0, TOOLH + HUDH - 6, SCRW, 2, musicActive() ? 0xFF4090 : 0x2E86C8);

    if (G.state == GS_TITLE) {
        for (int i = 0; i < 9; i++) {
            float a = G.phase * 0.006f + i * 0.7f;
            drawEmerald(120.0f + i * 120.0f, 470.0f + sinf(a) * 26.0f, G.phase + i * 40);
        }
        drawDigger(SCRW * 0.5f - CELL * 0.5f - 210.0f, 330.0f, DIR_R, G.phase, false);
        drawBag(SCRW * 0.5f - CELL * 0.5f - 130.0f, 330.0f, 0.0f);
        drawCherry(SCRW * 0.5f - CELL * 0.5f - 50.0f, 330.0f, G.phase);
        // the title line-up doubles as a legend: bass, mid, treble
        float ttint = (musicActive() && !g_musIsDefault) ? 1.0f : 0.0f;
        drawEnemyTank(SCRW * 0.5f - CELL * 0.5f + 40.0f, 330.0f, 0, MK_NOBBIN, DIR_L,
                      DIR_L, G.phase, false, 0, 0, 0, g_beatPulse,
                      monsterBand(MK_NOBBIN, 0), ttint, SCRW * 0.5f - 190.0f, 350.0f);
        drawEnemyTank(SCRW * 0.5f - CELL * 0.5f + 130.0f, 330.0f, 2, MK_HOBBIN, DIR_L,
                      DIR_L, G.phase, false, 0, 0, 0, g_beatPulse,
                      monsterBand(MK_HOBBIN, 0), ttint, SCRW * 0.5f - 190.0f, 350.0f);
        drawEnemyTank(SCRW * 0.5f - CELL * 0.5f + 220.0f, 330.0f, 3, MK_BORER, DIR_L,
                      DIR_L, G.phase, false, 0, 0, 0, g_beatPulse,
                      monsterBand(MK_BORER, 0), ttint, SCRW * 0.5f - 190.0f, 350.0f);
        drawParts();
        drawToolbar();
        return;
    }

    int sx = 0, sy = 0;
    if (G.shake > 0) {
        sx = (int)((rnd() % 7) - 3) * G.shake / 8;
        sy = (int)((rnd() % 7) - 3) * G.shake / 8;
    }

    drawField();

    for (int cy = 0; cy < FH; cy++)
        for (int cx = 0; cx < FW; cx++)
            if (G.gem[cy][cx])
                drawEmerald((float)(FLDX + cx * CELL + sx), (float)(FLDY + cy * CELL + sy),
                            G.phase + cx * 13 + cy * 29);

    if (G.cherryUp && G.bonusT == 0)
        drawCherry((float)(FLDX + G.chCx * CELL + sx), (float)(FLDY + G.chCy * CELL + sy), G.phase);

    for (const Bag& b : G.bags) {
        float wob = (b.state == BAG_WOBBLE) ? sinf(b.wob * 0.9f) * 3.2f : 0.0f;
        if (b.state == BAG_GOLD)
            drawGold((float)(FLDX + b.x + sx), (float)(FLDY + b.y + sy), G.phase + b.x);
        else
            drawBag((float)(FLDX + b.x + sx), (float)(FLDY + b.y + sy), wob);
    }

    drawParts();

    float beat = musicActive() ? g_beatPulse : 0.0f;
    // Livery only comes on for a real track; the built-in theme keeps the tanks
    // in their plain armour colours.
    float tint = (musicActive() && !g_musIsDefault) ? 1.0f : 0.0f;
    for (const Monster& m : G.mons) {
        // the gun tracks its locked bearing; with no lock it rides the hull
        int gunDir = (m.aimT > 0 || m.acquire > 0) ? m.aimDir : m.dir;
        drawEnemyTank((float)(FLDX + m.x + sx), (float)(FLDY + m.y + sy),
                    m.tier, m.kind, m.dir, gunDir, m.anim, m.flee || G.bonusT > 0, m.stun,
                    m.taunt, m.aimT, beat, m.band, tint,
                    (float)(FLDX + G.dig.x + CELL * 0.5f), (float)(FLDY + G.dig.y + CELL * 0.5f));
        // a borer inside undisturbed earth shows through it, not over it
        if (m.kind == MK_BORER && !areaClear(m.x, m.y))
            overlaySoil(m.x + sx - 6, m.y + sy - 6, CELL + 12, CELL + 12, 0.62f);
    }

    bool showDigger = G.dig.alive || (G.state == GS_DYING && ((G.stateT / 5) & 1));
    if (showDigger)
        drawDigger((float)(FLDX + G.dig.x + sx), (float)(FLDY + G.dig.y + sy),
                   G.dig.face, G.dig.anim, G.fireCool > 30);

    if (G.bul.alive) drawFireball((float)(G.bul.x + sx), (float)(G.bul.y + sy), G.phase);
    // enemy shells: cooler and heavier-looking than the player's fireball
    for (const Shell& sh : G.shells) {
        if (!sh.alive) continue;
        float ex = (float)(FLDX + sh.x + sx), ey = (float)(FLDY + sh.y + sy);
        glowCircle(ex, ey, 13.0f, 0x903010, 0.8f);
        fillCircle(ex, ey, 4.4f, 0xFFC060);
        fillCircle(ex, ey, 2.2f, 0xFFF4D0);
    }

    for (int i = 0; i < G.lives - 1 && i < 8; i++) {
        float lx = SCRW - 44.0f - i * 30.0f, ly = TOOLH + HUDH * 0.5f;
        fillRoundRectV(lx - 11, ly - 8, lx + 11, ly + 8, 4.0f, C_HULL_LT, C_HULL_DK);
        fillRoundRect(lx - 6, ly - 4, lx + 7, ly + 3, 2.5f, 0x0A2028, 0.9f);
        fillCircle(lx + 2, ly - 1, 2.0f, C_GEM_LT);
        fillRect((int)(lx - 12), (int)(ly + 8), 24, 3, C_TREAD_LT);
    }
    if (G.bonusT > 0) {
        float w = 300.0f * G.bonusT / (60.0f * 10.0f);
        float y0 = TOOLH + HUDH - 22.0f;
        fillRoundRect(SCRW * 0.5f - 150, y0, SCRW * 0.5f + 150, y0 + 8, 4.0f, 0x102030);
        fillRoundRectV(SCRW * 0.5f - 150, y0, SCRW * 0.5f - 150 + w, y0 + 8, 4.0f,
                       0xFFE080, 0xE08020);
    }
    drawToolbar();
}

// -------------------------------- self test --------------------------------
static FILE* g_tf = nullptr;
static int   g_fail = 0;
static void check(const char* what, bool ok)
{
    fprintf(g_tf, "%-56s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) g_fail++;
}
static void feed(int ticks, int vk)
{
    memset(g_keyDown, 0, sizeof(g_keyDown));
    if (vk) g_keyDown[vk] = true;
    for (int i = 0; i < ticks; i++) update();
    memset(g_keyDown, 0, sizeof(g_keyDown));
}
// A press edge followed by holding the key, the way the real input path sees it.
static void feedHold(int ticks, int vk)
{
    memset(g_keyDown, 0, sizeof(g_keyDown));
    g_keyDown[vk] = true; g_keyHit[vk] = true;
    for (int i = 0; i < ticks; i++) update();
    memset(g_keyDown, 0, sizeof(g_keyDown));
}
static void feedTap(int vk, int settle)
{
    memset(g_keyDown, 0, sizeof(g_keyDown));
    g_keyDown[vk] = true; g_keyHit[vk] = true;
    update();                                    // one tick with the key down
    memset(g_keyDown, 0, sizeof(g_keyDown));
    for (int i = 0; i < settle; i++) update();
}
static void sandbox()
{
    loadLevel(0);
    memset(g_dirt, 1, sizeof(g_dirt));
    memset(G.gem, 0, sizeof(G.gem));
    G.gemLeft = 999; G.gemTotal = 999; G.monLeft = 0; G.spawnGap = 999999; G.lives = 3;
    G.bags.clear(); G.mons.clear();
    G.bul.alive = false; G.cherryUp = false; G.cherryT = 999999; G.bonusT = 0;
    G.state = GS_PLAY; G.paused = false; G.hunt = false;
    g_partN = 0;
}
static void putDigger(int cx, int cy, int dir)
{
    placeDigger();
    G.dig.x = cx * CELL; G.dig.y = cy * CELL;
    G.dig.dir = dir; G.dig.face = dir; G.dig.acc = 0;
    G.dig.moveLeft = 0; G.dig.holdT = 0; G.dig.stepQueued = false;
    digAt(G.dig.x, G.dig.y);
}
static Monster& addMon(int cx, int cy, int tier, int kind, int speed)
{
    Monster m{};
    m.x = cx * CELL; m.y = cy * CELL; m.alive = true;
    m.dir = DIR_L; m.tier = tier; m.kind = kind; m.speed = speed;
    m.pushBag = -1; m.dodgeDir = -1; m.dodgeSeen = 0;
    m.band = monsterBand(kind, G.spawnSeq++);
    G.mons.push_back(m);
    return G.mons.back();
}
static int soilTotal()
{
    int n = 0;
    for (int y = 0; y < FLDH; y++) for (int x = 0; x < FLDW; x++) n += g_dirt[y][x];
    return n;
}
static int selfTest();

// -------------------------------- Win32 ------------------------------------
static HWND  g_hwnd = nullptr;
static BITMAPINFO g_bmi{};
static bool  g_running = true;
static int   g_dstW = SCRW, g_dstH = SCRH;
static bool  g_dragging = false;
static HFONT g_fontBig = nullptr, g_fontMid = nullptr, g_fontSml = nullptr;
// Off-screen surface: the frame and the GDI text go here first and reach the
// window as a single blit.  Drawing text straight onto the window after
// StretchDIBits leaves the HUD flickering, one string at a time.
static HDC     g_memDC = nullptr;
static HBITMAP g_memBmp = nullptr, g_memOld = nullptr;

static void gdiText(HDC hdc, float lx, float ly, const char* s, HFONT f,
                    COLORREF col, int align)
{
    SelectObject(hdc, f);
    SetBkMode(hdc, TRANSPARENT);
    UINT ta = (align == 1) ? TA_CENTER : (align == 2 ? TA_RIGHT : TA_LEFT);
    SetTextAlign(hdc, ta | TA_TOP);
    int x = (int)(lx * g_dstW / SCRW), y = (int)(ly * g_dstH / SCRH);
    SetTextColor(hdc, RGB(0, 0, 0));
    TextOutA(hdc, x + 2, y + 2, s, (int)strlen(s));
    SetTextColor(hdc, col);
    TextOutA(hdc, x, y, s, (int)strlen(s));
}
static void fmtTime(char* out, size_t n, double sec)
{
    int s = (int)sec;
    sprintf_s(out, n, "%d:%02d", s / 60, s % 60);
}
static void drawToolbarText(HDC hdc)
{
    char buf[224], a[16], b[16];
    // track name, elided so it never runs into the seek bar
    char name[64];
    strncpy_s(name, g_plStatus, _TRUNCATE);
    if (strlen(name) > 15) { name[12] = 0; strcat_s(name, "..."); }
    gdiText(hdc, TB_NAME_X, TB_Y - 10.0f, name, g_fontSml, RGB(220, 230, 245), 0);

    fmtTime(a, sizeof(a), g_musFrames ? g_musPos / SR : 0.0);
    fmtTime(b, sizeof(b), g_musFrames ? (double)g_musFrames / SR : 0.0);
    // Left-aligned at a fixed slot: right-aligning it against the seek bar let
    // a long track name run straight into the clock.
    sprintf_s(buf, "%s / %s", a, b);
    gdiText(hdc, TB_TIME_X, TB_Y - 10.0f, buf, g_fontSml, RGB(165, 180, 200), 0);

    // small numeric BPM readout to the right of the spectrum
    if (g_bpm > 20 && g_bpm < 260) {
        sprintf_s(buf, "%.0f", g_bpm);
        gdiText(hdc, TB_BPM_X, TB_Y - 10.0f, buf, g_fontSml, RGB(255, 150, 200), 2);
    }

    if (g_plOpen && g_plN == 0) {
        gdiText(hdc, TB_NAME_X + 16.0f, TOOLH + 12.0f,
                "PLAYLIST EMPTY  -  PRESS O TO ADD MUSIC", g_fontSml,
                RGB(150, 165, 185), 0);
    }
    if (g_plOpen && g_plN) {
        int first = 0;
        if (g_plCur >= TB_ROWS) first = g_plCur - TB_ROWS + 1;
        int rows = g_plN < TB_ROWS ? g_plN : TB_ROWS;
        for (int i = 0; i < rows && first + i < g_plN; i++) {
            bool cur = (first + i == g_plCur);
            char row[64];
            strncpy_s(row, g_pl[first + i].name, _TRUNCATE);
            if (strlen(row) > 40) { row[37] = 0; strcat_s(row, "..."); }
            gdiText(hdc, TB_NAME_X + 16.0f, TOOLH + 9.0f + i * TB_ROW_H, row, g_fontSml,
                    cur ? RGB(160, 240, 255) : RGB(175, 188, 205), 0);
        }
    }
}
static void drawOverlayText(HDC hdc)
{
    char buf[128];
    drawToolbarText(hdc);

    if (G.state == GS_TITLE) {
        gdiText(hdc, SCRW * 0.5f, 140, "DIGGER", g_fontBig, RGB(255, 205, 60), 1);
        gdiText(hdc, SCRW * 0.5f, 268, "99 STAGES  -  ENEMY TANKS THAT DODGE, ADAPT AND SHOOT BACK",
                g_fontSml, RGB(120, 220, 240), 1);
        gdiText(hdc, SCRW * 0.5f, 592, "TAP A DIRECTION TO TURN   TAP AGAIN TO STEP   HOLD TO DRIVE",
                g_fontMid, RGB(230, 235, 245), 1);
        gdiText(hdc, SCRW * 0.5f, 632, "SPACE FIRE    [ ] TRACK    M PLAYLIST    O ADD MUSIC    P PAUSE",
                g_fontMid, RGB(190, 200, 215), 1);
        gdiText(hdc, SCRW * 0.5f, 712, ((G.phase / 30) & 1) ? "PRESS SPACE TO START" : "",
                g_fontMid, RGB(255, 210, 90), 1);
        return;
    }
    const float HY = (float)TOOLH;
    sprintf_s(buf, "%06d", G.score);
    gdiText(hdc, 24, HY + 10, "SCORE", g_fontSml, RGB(140, 160, 185), 0);
    gdiText(hdc, 24, HY + 30, buf, g_fontMid, RGB(255, 255, 255), 0);
    sprintf_s(buf, "STAGE %02d / %d", G.level + 1, MAXLEVEL);
    gdiText(hdc, 230, HY + 30, buf, g_fontMid, RGB(120, 220, 240), 0);
    sprintf_s(buf, "GEMS %d", G.gemLeft);
    gdiText(hdc, 470, HY + 30, buf, g_fontMid,
            G.gemLeft ? RGB(120, 240, 200) : RGB(120, 140, 150), 0);
    sprintf_s(buf, "MONSTERS %d", G.monLeft + (int)G.mons.size());
    gdiText(hdc, 640, HY + 30, buf, g_fontMid, RGB(255, 150, 210), 0);
    if (G.hunt) gdiText(hdc, 850, HY + 30, "HUNT", g_fontMid, RGB(255, 120, 120), 0);

    if (G.bonusT > 0)
        gdiText(hdc, SCRW * 0.5f, HY + HUDH - 46, "BONUS", g_fontSml, RGB(255, 220, 120), 1);
    if (G.state == GS_CLEAR) gdiText(hdc, SCRW * 0.5f, 420, "STAGE CLEARED", g_fontBig, RGB(255, 215, 80), 1);
    if (G.state == GS_OVER)  gdiText(hdc, SCRW * 0.5f, 420, "GAME OVER", g_fontBig, RGB(255, 90, 90), 1);
    if (G.state == GS_WIN)   gdiText(hdc, SCRW * 0.5f, 420, "ALL 99 STAGES CLEARED", g_fontBig, RGB(140, 255, 170), 1);
    if (G.paused && G.state == GS_PLAY)
        gdiText(hdc, SCRW * 0.5f, 420, "PAUSED", g_fontBig, RGB(235, 240, 250), 1);
    if (!g_sfxOn) gdiText(hdc, 24, SCRH - 26, "SFX OFF", g_fontSml, RGB(120, 130, 145), 0);
}

static void mouseToLogical(LPARAM lp, float& lx, float& ly)
{
    lx = (float)GET_X_LPARAM(lp) * SCRW / g_dstW;
    ly = (float)GET_Y_LPARAM(lp) * SCRH / g_dstH;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY: g_running = false; PostQuitMessage(0); return 0;
    case WM_CLOSE:   g_running = false; DestroyWindow(hwnd); return 0;
    case WM_LBUTTONDOWN: {
        float lx, ly; mouseToLogical(lp, lx, ly);
        bool openFiles = false;
        if (toolbarClick(lx, ly, false, openFiles)) g_dragging = true;
        if (openFiles) { g_dragging = false; musicOpenDialog(hwnd); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_dragging) {
            float lx, ly; mouseToLogical(lp, lx, ly);
            bool openFiles = false;
            toolbarClick(lx, ly, true, openFiles);
        }
        return 0;
    }
    case WM_LBUTTONUP: g_dragging = false; return 0;
    case WM_KEYDOWN: {
        bool repeat = (lp & (1 << 30)) != 0;
        if (wp < 256) { if (!repeat) g_keyHit[wp] = true; g_keyDown[wp] = true; }
        if (repeat) return 0;                    // toggles must not auto-repeat
        if (wp == VK_ESCAPE) {
            if (g_plOpen) g_plOpen = false;
            else { g_running = false; DestroyWindow(hwnd); }
        }
        // Music is switchable at any moment, mid-stage, without pausing.
        if (wp == 'M' || wp == VK_TAB) g_plOpen = !g_plOpen;
        if (wp == 'O') musicOpenDialog(hwnd);
        if (wp == VK_OEM_4) musicNext(-1);        // [
        if (wp == VK_OEM_6) musicNext(1);         // ]
        if (wp == VK_OEM_5) musicTogglePlay();    // backslash
        if (wp == 'P' && G.state == GS_PLAY) G.paused = !G.paused;
        if (wp == VK_F2) { g_sfxOn = !g_sfxOn; if (!g_sfxOn) stopAllSounds(); }
        if (wp == VK_F3) g_musicOn = !g_musicOn;
        if (wp == VK_F1) newGame();
        return 0;
    }
    case WM_KEYUP: if (wp < 256) g_keyDown[wp] = false; return 0;
    case WM_KILLFOCUS: memset(g_keyDown, 0, sizeof(g_keyDown)); g_dragging = false; return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Builds the window/taskbar icon in memory from paintIcon -- no .ico file and
// no resource-compile step, so the exe stays a single self-contained artefact.
static HICON buildAppIcon(int size)
{
    paintIcon(size);

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = size;
    bi.bV5Height = -size;                        // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC dc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) { if (color) DeleteObject(color); return nullptr; }

    uint32_t* px = (uint32_t*)bits;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            uint32_t c = g_scr[y * SCRW + x] & 0x00FFFFFF;
            int a = (int)(iconAlpha(x, y, size) * 255.0f + 0.5f);
            px[y * size + x] = ((uint32_t)a << 24) | c;
        }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);   // unused with alpha
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

static void enableDpiAwareness()
{
    HMODULE u = GetModuleHandleA("user32.dll");
    if (u) {
        typedef BOOL(WINAPI * PFN)(HANDLE);
        PFN f = (PFN)GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (f && f((HANDLE)-4)) return;
    }
    SetProcessDPIAware();
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int)
{
    if (lpCmd && strstr(lpCmd, "-test")) return selfTest();

    enableDpiAwareness();
    RECT wa{ 0, 0, 1280, 800 };
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int aw = wa.right - wa.left - 40, ah = wa.bottom - wa.top - 80;
    float sc = 1.0f;
    if (SCRW > aw || SCRH > ah) {
        float s1 = (float)aw / SCRW, s2 = (float)ah / SCRH;
        sc = s1 < s2 ? s1 : s2;
    }
    g_dstW = (int)(SCRW * sc); g_dstH = (int)(SCRH * sc);

    // Small icon is drawn at the size the title bar actually uses, so it gets
    // the simplified 16px artwork rather than a downscaled busy one.
    HICON iconBig = buildAppIcon(64);
    HICON iconSmall = buildAppIcon(GetSystemMetrics(SM_CXSMICON) <= 20 ? 16 : 32);

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "DiggerWndClass";
    wc.hIcon = iconBig;
    RegisterClassA(&wc);

    RECT r{ 0, 0, g_dstW, g_dstH };
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    AdjustWindowRect(&r, style, FALSE);
    int winW = r.right - r.left, winH = r.bottom - r.top;
    g_hwnd = CreateWindowA("DiggerWndClass", "DIGGER  -  C++ remake",
                           style, wa.left + (wa.right - wa.left - winW) / 2,
                           wa.top + (wa.bottom - wa.top - winH) / 2, winW, winH,
                           nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;
    if (iconBig)   SendMessageA(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconBig);
    if (iconSmall) SendMessageA(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    int fb = (int)(76 * sc), fm = (int)(26 * sc), fs = (int)(19 * sc);
    g_fontBig = CreateFontA(-fb, 0, 0, 0, FW_BLACK, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            VARIABLE_PITCH, "Segoe UI");
    g_fontMid = CreateFontA(-fm, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            VARIABLE_PITCH, "Segoe UI");
    g_fontSml = CreateFontA(-fs, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            VARIABLE_PITCH, "Segoe UI");

    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = SCRW;
    g_bmi.bmiHeader.biHeight = -SCRH;
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;

    audioStart();
    musicBootstrap(lpCmd);                        // installs + starts the default track
    buildCave();
    G.rng = GetTickCount();
    loadLevel(0);
    G.state = GS_TITLE;

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);
    double acc = 0.0;
    const double DT = 1.0 / 60.0;

    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        if (!g_running) break;

        QueryPerformanceCounter(&now);
        double el = double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart);
        prev = now;
        if (el > 0.25) el = 0.25;
        acc += el;
        int guard = 0;
        while (acc >= DT && guard++ < 8) { update(); acc -= DT; }
        musicTick();

        render();
        HDC hdc = GetDC(g_hwnd);
        if (!g_memDC) {
            g_memDC = CreateCompatibleDC(hdc);
            g_memBmp = CreateCompatibleBitmap(hdc, g_dstW, g_dstH);
            g_memOld = (HBITMAP)SelectObject(g_memDC, g_memBmp);
        }
        SetStretchBltMode(g_memDC, COLORONCOLOR);
        StretchDIBits(g_memDC, 0, 0, g_dstW, g_dstH, 0, 0, SCRW, SCRH,
                      g_scr, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
        drawOverlayText(g_memDC);
        BitBlt(hdc, 0, 0, g_dstW, g_dstH, g_memDC, 0, 0, SRCCOPY);
        ReleaseDC(g_hwnd, hdc);
        Sleep(1);
    }

    audioStop();
    musicFreeCurrent();
    if (g_memDC) {
        SelectObject(g_memDC, g_memOld);
        DeleteObject(g_memBmp);
        DeleteDC(g_memDC);
    }
    if (g_fontBig) DeleteObject(g_fontBig);
    if (g_fontMid) DeleteObject(g_fontMid);
    if (g_fontSml) DeleteObject(g_fontSml);
    return 0;
}

#include "selftest.inc"
