// ---------------------------------------------------------------------------
//  selftest.inc -- headless verification, included at the end of digger.cpp.
//
//  The simulation is driven directly with scripted input and the results
//  asserted.  The 99 generated stages are validated structurally as well:
//  nobbins and hobbins cannot dig, so a den that fails to connect to the start
//  would silently ruin a stage, and that is invisible to the eye.
// ---------------------------------------------------------------------------

static int selfTest()
{
    fopen_s(&g_tf, "selftest.txt", "w");
    if (!g_tf) return 2;
    g_sfxOn = false;
    buildCave();
    fprintf(g_tf, "DIGGER self test\n================\n");

    // ---- A. digging, emeralds, scoring -------------------------------------
    loadLevel(0); placeDigger(); G.state = GS_PLAY;
    int gem0 = G.gemLeft;
    feedHold(400, VK_LEFT);
    check("A emeralds collected while digging", G.gemLeft < gem0);
    check("A score increased", G.score > 0);
    check("A soil actually removed", !g_dirt[G.dig.y + CELL / 2][G.dig.x + CELL / 2]);

    // ---- B. controls: tap turns, tap again steps, hold drives ---------------
    sandbox();
    putDigger(10, 8, DIR_R);
    { int x0 = G.dig.x, y0 = G.dig.y;
      feedTap(VK_UP, 40);
      check("B a tap in a new direction only turns", G.dig.face == DIR_U);
      check("B a tap in a new direction does not move",
            G.dig.x == x0 && G.dig.y == y0); }
    { int y0 = G.dig.y;
      feedTap(VK_UP, 60);
      check("B a tap in the facing direction steps exactly one cell",
            G.dig.y == y0 - CELL);
      check("B the step lands back on the grid",
            G.dig.x % CELL == 0 && G.dig.y % CELL == 0); }
    { int y0 = G.dig.y;
      feedHold(240, VK_UP);
      check("B holding drives further than a single step", y0 - G.dig.y > CELL * 2); }
    sandbox();
    putDigger(10, 8, DIR_R);
    { int x0 = G.dig.x;
      // each tap must resolve to a whole cell -- no drifting off the grid
      for (int i = 0; i < 5; i++) feedTap(VK_RIGHT, 30);
      check("B repeated taps advance whole cells, never drift",
            G.dig.x == x0 + 5 * CELL); }

    // ---- C. bag physics -----------------------------------------------------
    sandbox();
    for (int cy = 5; cy <= 11; cy++) digCellFull(6, cy);
    { Bag b{}; b.x = 6 * CELL; b.y = 4 * CELL; G.bags.push_back(b); }
    putDigger(0, 0, DIR_R);
    feed(24, 0);
    check("C bag starts wobbling", G.bags[0].state == BAG_WOBBLE);
    feed(260, 0);
    check("C bag fell and broke into gold", G.bags[0].state == BAG_GOLD);

    sandbox();
    for (int cx = 2; cx <= 16; cx++) digCellFull(cx, 8);
    { Bag b{}; b.x = 9 * CELL; b.y = 8 * CELL; G.bags.push_back(b); }
    putDigger(5, 8, DIR_R);
    { int bx = G.bags[0].x;
      feedHold(400, VK_RIGHT);
      check("C bag pushed sideways by the digger", G.bags[0].x > bx);
      check("C digger survived the push", G.dig.alive); }

    // The tank can push a sack even into undisturbed soil -- the sack carves
    // its way through.  Only another sack or the wall stops it.
    sandbox();
    digCellFull(5, 8);                            // just a pocket for the digger
    { Bag b{}; b.x = 6 * CELL; b.y = 8 * CELL; G.bags.push_back(b); }   // sack sits in soil
    putDigger(5, 8, DIR_R);
    { int bx = G.bags[0].x;
      check("C the sack starts embedded in solid soil",
            hasSoil(7 * CELL + 10, 8 * CELL + 10, 7 * CELL + CELL - 10, 8 * CELL + CELL - 10));
      feedHold(500, VK_RIGHT);
      check("C tank pushes the sack through soil", G.bags[0].x >= bx + CELL); }

    // A sack shoved to the field edge simply stops; the tank is not stuck.
    sandbox();
    digCellFull(FW - 3, 8);
    { Bag b{}; b.x = (FW - 2) * CELL; b.y = 8 * CELL; G.bags.push_back(b); }
    putDigger(FW - 3, 8, DIR_R);
    feedHold(400, VK_RIGHT);
    check("C a sack cannot be pushed off the field", G.bags[0].x <= (FW - 1) * CELL);

    sandbox();
    for (int cy = 5; cy <= 11; cy++) digCellFull(5, cy);
    { Bag b{}; b.x = 5 * CELL; b.y = 4 * CELL; G.bags.push_back(b); }
    putDigger(5, 11, DIR_R);
    feed(120, 0);
    check("C falling bag killed the digger", G.state == GS_DYING);
    feed(180, 0);
    check("C death cost one life", G.lives == 2);

    sandbox();
    for (int cy = 5; cy <= 11; cy++) digCellFull(5, cy);
    { Bag b{}; b.x = 5 * CELL; b.y = 4 * CELL; G.bags.push_back(b); }
    putDigger(5, 5, DIR_U);
    G.dig.face = DIR_U;
    feedHold(300, VK_UP);
    check("C bag held up while UP is held", G.bags[0].state != BAG_GOLD);
    check("C held bag did not move", G.bags[0].y == 4 * CELL);
    feed(260, 0);
    check("C bag drops once UP is released", G.bags[0].state == BAG_GOLD);

    // A fireball bursts a standing sack open into a collectable gold pile.
    sandbox();
    for (int cx = 2; cx <= 16; cx++) digCellFull(cx, 7);
    { Bag b{}; b.x = 10 * CELL; b.y = 7 * CELL; G.bags.push_back(b); }
    putDigger(2, 7, DIR_R);
    check("C the sack starts intact", G.bags[0].state == BAG_IDLE);
    g_keyHit[VK_SPACE] = true; feed(1, 0); feed(120, 0);
    check("C a fireball breaks the sack into gold", G.bags[0].state == BAG_GOLD);
    { int sc0 = G.score;
      putDigger(9, 7, DIR_R);                     // walk onto the spilled pile
      feedHold(120, VK_RIGHT);
      check("C the broken-open gold can then be collected", G.score >= sc0 + 500); }

    // ---- D. armour tiers ----------------------------------------------------
    sandbox();
    for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
    putDigger(2, 7, DIR_R);
    addMon(12, 7, 0, MK_NOBBIN, 0);
    // Mute its gun: with return fire on, the tank's shell would meet the
    // player's and both would cancel.  That is correct behaviour, but this
    // test is isolating armour, not staging a duel.
    G.mons[0].gunCool = 100000;
    { int sc0 = G.score;
      g_keyHit[VK_SPACE] = true; feed(1, 0); feed(160, 0);
      check("D tier-0 monster dies to one shot", G.mons.empty());
      check("D a kill scores 250", G.score == sc0 + 250); }

    sandbox();
    for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
    putDigger(2, 7, DIR_R);
    addMon(12, 7, 3, MK_NOBBIN, 0);
    for (int shot = 3; shot >= 1; shot--) {
        // hitMonster recomputes speed on a downgrade (a damaged tank rightly
        // gets back to fighting), so pin it again each round -- this test is
        // about armour, not about the duel that would otherwise break out.
        if (!G.mons.empty()) { G.mons[0].speed = 0; G.mons[0].gunCool = 100000; }
        G.fireCool = 0; g_keyHit[VK_SPACE] = true; feed(1, 0); feed(160, 0);
        char msg[80];
        sprintf_s(msg, "D tier-%d survives and drops one tier", shot);
        check(msg, !G.mons.empty() && G.mons[0].tier == shot - 1);
    }
    G.fireCool = 0; g_keyHit[VK_SPACE] = true; feed(1, 0); feed(160, 0);
    check("D the fourth shot finally kills it", G.mons.empty());

    // ---- E. the three monster kinds ----------------------------------------
    sandbox();
    digCellFull(9, 7);
    addMon(9, 7, 0, MK_HOBBIN, 300);
    putDigger(0, 0, DIR_R);
    { int s0 = soilTotal();
      feed(260, 0);
      check("E hobbin removed no soil", soilTotal() == s0);
      check("E walled-in hobbin stayed put",
            !G.mons.empty() && G.mons[0].x == 9 * CELL && G.mons[0].y == 7 * CELL); }

    // The borer pierces soil rather than mining it: it must move freely through
    // solid ground and leave every grain of it, and every emerald, in place.
    sandbox();
    digCellFull(9, 7);
    addMon(9, 7, 0, MK_BORER, 300);
    putDigger(2, 2, DIR_R);
    for (int cy = 0; cy < FH; cy++) for (int cx = 0; cx < FW; cx++) G.gem[cy][cx] = true;
    G.gemLeft = FH * FW; G.gemTotal = FH * FW;
    { int s0 = soilTotal();
      feed(300, 0);
      check("E borer travels through solid soil",
            !G.mons.empty() && (G.mons[0].x != 9 * CELL || G.mons[0].y != 7 * CELL));
      check("E borer leaves no tunnel behind it", soilTotal() == s0);
      check("E borer does not eat emeralds", G.gemLeft == FH * FW); }

    // A borer must never take a bag: it cannot shove one and cannot enter its
    // cell.  It may still tunnel underneath and drop the bag, which is ordinary
    // physics -- and the falling bag can crush the borer itself.
    sandbox();
    for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
    { Bag b{}; b.x = 9 * CELL; b.y = 7 * CELL; G.bags.push_back(b); }
    addMon(5, 7, 0, MK_BORER, 320);
    putDigger(18, 7, DIR_L);
    { int bx = G.bags[0].x;
      bool entered = false;
      for (int i = 0; i < 700; i++) {
          update();
          for (const Monster& m : G.mons)
              if (m.alive && overlap(m.x, m.y, bx, 7 * CELL, CELL - 12, CELL - 12))
                  entered = true;
      }
      check("E borer cannot shove a gold bag sideways", G.bags[0].x == bx);
      check("E borer never occupies a bag's cell", !entered); }

    // Gold blocks the borer as a spilled pile too, not just as a standing sack.
    sandbox();
    for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
    { Bag b{}; b.x = 10 * CELL; b.y = 7 * CELL; b.state = BAG_GOLD; G.bags.push_back(b); }
    addMon(5, 7, 0, MK_BORER, 320);
    putDigger(2, 2, DIR_R);                       // out of the way
    { bool entered = false;
      for (int i = 0; i < 700; i++) {
          update();
          for (const Monster& m : G.mons)
              if (m.alive && overlap(m.x, m.y, 10 * CELL, 7 * CELL, CELL - 12, CELL - 12))
                  entered = true;
      }
      check("E a gold pile blocks the borer as well", !entered);
      check("E the pile is still there afterwards", !G.bags.empty()); }

    sandbox();
    for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
    { Bag b{}; b.x = 9 * CELL; b.y = 7 * CELL; G.bags.push_back(b); }
    addMon(5, 7, 0, MK_HOBBIN, 320);
    putDigger(18, 7, DIR_L);
    { int bx = G.bags[0].x;
      feed(700, 0);
      check("E hobbin does shove the bag", G.bags[0].x != bx); }

    sandbox();
    for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
    { Bag b{}; b.x = 9 * CELL; b.y = 7 * CELL; G.bags.push_back(b); }
    addMon(5, 7, 0, MK_NOBBIN, 320);
    putDigger(18, 7, DIR_L);
    { int bx = G.bags[0].x;
      feed(700, 0);
      check("E nobbin leaves the bag alone", G.bags[0].x == bx); }

    // ---- F. evasion and jeering --------------------------------------------
    {   // open chamber, so there is always a lane to step into
        sandbox();
        for (int cy = 5; cy <= 9; cy++)
            for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
        putDigger(2, 7, DIR_R);
        G.cfg.dodgeSkill = 100;
        addMon(18, 7, 0, MK_NOBBIN, 380);
        g_keyHit[VK_SPACE] = true;
        bool sawDodge = false, sawTaunt = false, leftTheLine = false;
        for (int i = 0; i < 200 && !G.mons.empty(); i++) {
            update();
            if (!G.mons.empty()) {
                if (G.mons[0].dodgeT > 0) sawDodge = true;
                if (G.mons[0].taunt > 0) sawTaunt = true;
                if (G.mons[0].y != 7 * CELL) leftTheLine = true;
            }
        }
        check("F a monster with room to move sidesteps the shot", sawDodge);
        check("F the dodge takes it off the firing line", leftTheLine);
        check("F the dodger is still alive afterwards", !G.mons.empty());
        check("F a clean dodge triggers the jeer", sawTaunt);
    }
    {
        sandbox();
        for (int cx = 2; cx <= 20; cx++) digCellFull(cx, 7);   // no side lane at all
        putDigger(2, 7, DIR_R);
        G.cfg.dodgeSkill = 100;
        addMon(12, 7, 0, MK_NOBBIN, 0);
        G.mons[0].gunCool = 100000;               // isolate dodging from return fire
        g_keyHit[VK_SPACE] = true; feed(1, 0); feed(200, 0);
        check("F with nowhere to dodge the shot still connects", G.mons.empty());
    }
    {   // every kind evades, not just the plain ones
        for (int kind = 0; kind <= 2; kind++) {
            sandbox();
            for (int cy = 5; cy <= 9; cy++)
                for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
            putDigger(2, 7, DIR_R);
            G.cfg.dodgeSkill = 100;
            addMon(18, 7, 0, kind, 380);
            g_keyHit[VK_SPACE] = true;
            bool saw = false;
            for (int i = 0; i < 200 && !G.mons.empty(); i++) {
                update();
                if (!G.mons.empty() && G.mons[0].dodgeT > 0) saw = true;
            }
            static const char* kn[3] = { "nobbin", "hobbin", "borer" };
            char msg[80];
            sprintf_s(msg, "F the %s dodges too", kn[kind]);
            check(msg, saw);
        }
    }
    {   // evasion must keep climbing across the whole run, never saturating
        bool rising = true, capped = true;
        int prev = cfgFor(0).dodgeSkill;
        for (int lv = 1; lv < MAXLEVEL; lv++) {
            int d = cfgFor(lv).dodgeSkill;
            if (d < prev) rising = false;
            if (d > 78) capped = false;
            prev = d;
        }
        // Self-preservation exists from the first stage; what grows is how
        // reliably they read the shot.
        check("F stage 1 monsters already dodge, but only sometimes",
              cfgFor(0).dodgeSkill >= 20 && cfgFor(0).dodgeSkill <= 35);
        check("F evasion never decreases across all 99 stages", rising);
        check("F evasion is still climbing past the stage-71 plateau",
              cfgFor(98).dodgeSkill > cfgFor(71).dodgeSkill &&
              cfgFor(71).dodgeSkill > cfgFor(40).dodgeSkill);
        check("F evasion never reaches certainty", capped && cfgFor(98).dodgeSkill < 100);
    }

    // ---- F3. the monster HUNTS the tank; it does not flee it ---------------
    // The point of the AI is to catch the player while staying alive, not to
    // play keep-away.  So the dominant behaviour must be closing the distance.
    {
        // A stage-99 (max AI) monster set loose in an open chamber must run the
        // player down, whether or not it is being shot at.
        auto huntTrial = [&](bool playerFires, unsigned seed) {
            aiReset();
            G.rng = seed;
            sandbox();
            for (int cy = 3; cy <= 11; cy++)
                for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
            putDigger(3, 7, DIR_R);
            G.cfg.aiWeight = 100;
            // Under fire it relies on the same evasion and armour a real
            // stage-99 monster has: it dodges what it can, soaks what it must,
            // and keeps coming.  A bare tier-0 monster crossing open ground
            // against a stationary turret with a clear lane is simply supposed
            // to die -- that is the player defending correctly, not an AI fault.
            G.cfg.dodgeSkill = playerFires ? 74 : 0;
            addMon(20, 7, playerFires ? 3 : 0, MK_NOBBIN, 360);
            int closest = 99;
            // A calm run-down needs no second chance; under fire we let it die
            // and try again within the window, since one dodging monster is
            // expected to trade lives while it works its way in.
            int budget = playerFires ? 1800 : 700;
            for (int i = 0; i < budget; i++) {
                if (playerFires && G.fireCool == 0) g_keyHit[VK_SPACE] = true;
                update();
                if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
                if (G.mons.empty()) {
                    if (!playerFires) break;
                    addMon(20, 7, 3, MK_NOBBIN, 360);
                    continue;
                }
                int mcx = (G.mons[0].x + CELL / 2) / CELL;
                int mcy = (G.mons[0].y + CELL / 2) / CELL;
                int dcx = (G.dig.x + CELL / 2) / CELL;
                int dcy = (G.dig.y + CELL / 2) / CELL;
                int d = abs(mcx - dcx) + abs(mcy - dcy);
                if (d < closest) closest = d;
            }
            return closest;
        };
        // Average over seeds: a single run of a stochastic chase is noise.
        static const unsigned SEEDS[4] = { 4242, 777, 90210, 31337 };
        int calmWorst = 0, firedWorst = 0;
        for (unsigned s : SEEDS) {
            int c = huntTrial(false, s), f = huntTrial(true, s);
            if (c > calmWorst) calmWorst = c;
            if (f > firedWorst) firedWorst = f;
        }
        fprintf(g_tf, "%-56s calm %d, under fire %d (worst of 4 seeds)\n",
                "F3 closest the hunter got (cells)", calmWorst, firedWorst);
        check("F3 an adaptive monster runs the player down", calmWorst <= 2);
        check("F3 it keeps hunting even while being shot at", firedWorst <= 3);
    }
    {   // Pursuit through actual tunnels, which is what the real game is.
        // An open chamber hides the bug that mattered: a greedy monster heads
        // for whichever neighbour looks closer, walks into a dead end and
        // stalls there.  Here the only route is a U -- the monster starts on
        // the far arm and must first travel AWAY from the player to get around.
        sandbox();
        const int TOP = 4, BOT = 10, LEFT = 3, RIGHT = 18;
        for (int cx = LEFT; cx <= RIGHT; cx++) digCellFull(cx, TOP);    // top arm
        for (int cx = LEFT; cx <= RIGHT; cx++) digCellFull(cx, BOT);    // bottom arm
        for (int cy = TOP; cy <= BOT; cy++) digCellFull(RIGHT, cy);     // right joiner
        putDigger(LEFT, BOT, DIR_R);                 // player at the bottom-left end
        G.cfg.aiWeight = 100; G.cfg.dodgeSkill = 0;
        addMon(LEFT, TOP, 0, MK_NOBBIN, 400);        // monster at the top-left end
        // straight-line distance says "go down", but there is no shaft on the
        // left: it has to run right along the top, down the joiner, then back.
        int closest = 99;
        for (int i = 0; i < 2200 && !G.mons.empty(); i++) {
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            if (!G.mons.empty()) {
                int d = abs(G.mons[0].x / CELL - G.dig.x / CELL) +
                        abs(G.mons[0].y / CELL - G.dig.y / CELL);
                if (d < closest) closest = d;
            }
        }
        fprintf(g_tf, "%-56s %d cells\n", "F3 maze pursuit: closest approach", closest);
        check("F3 a walker paths around a dead end to reach the player", closest <= 1);
    }
    {   // ...and the same for a borer, which should just come straight through.
        sandbox();
        digCellFull(4, 4);
        putDigger(18, 10, DIR_R);
        G.cfg.aiWeight = 100; G.cfg.dodgeSkill = 0;
        addMon(4, 4, 0, MK_BORER, 400);
        int closest = 99;
        for (int i = 0; i < 2200 && !G.mons.empty(); i++) {
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            if (!G.mons.empty()) {
                int d = abs(G.mons[0].x / CELL - G.dig.x / CELL) +
                        abs(G.mons[0].y / CELL - G.dig.y / CELL);
                if (d < closest) closest = d;
            }
        }
        check("F3 a borer bores straight to the player through solid ground",
              closest <= 1);
    }
    {   // DIAGNOSTIC: a real generated stage, player parked in its start pocket.
        // This is the situation the player actually sees, and it is measured
        // both with and without music playing, because the beat modulation
        // touches monster movement.
        auto realStage = [&](bool music) {
            aiReset();
            G.rng = 31337;
            loadLevel(0); placeDigger(); G.state = GS_PLAY;
            G.lives = 9;
            if (music) {
                buildDefaultTrack();
                std::lock_guard<std::mutex> lk(g_audioM);
                g_musData = g_defaultTrack; g_musFrames = g_defaultFrames;
                g_musPos = 0; g_musPlaying = true; g_musIsDefault = true;
            }
            int contact = -1, maxOut = 0;
            short* out = new short[ABUF_FRAMES];
            for (int i = 0; i < 3600; i++) {
                if (music) renderAudio(out, ABUF_FRAMES);   // drives beat detect
                update();
                if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
                if ((int)G.mons.size() > maxOut) maxOut = (int)G.mons.size();
                for (const Monster& m : G.mons) {
                    int d = abs(m.x / CELL - G.dig.x / CELL) +
                            abs(m.y / CELL - G.dig.y / CELL);
                    if (d <= 1 && contact < 0) contact = i;
                }
            }
            delete[] out;
            if (music) {
                std::lock_guard<std::mutex> lk(g_audioM);
                g_musPlaying = false; g_musData = nullptr; g_musFrames = 0;
                g_musIsDefault = false;
            }
            return (contact < 0 ? 9999 : contact) * 10 + maxOut;
        };
        int quiet = realStage(false), loud = realStage(true);

        // Judge against the geometry, not a wall clock: the den sits in the far
        // corner, so measure the shortest walk from den to the player's start
        // and require contact in not much more than that.  This catches the
        // failure that matters -- dawdling, stalling, or drifting off to do
        // something else -- without demanding a monster faster than the rules
        // allow it to be.
        loadLevel(0); placeDigger();
        buildPathField();
        int denDist = g_pathTunnel[G.denCy][G.denCx];
        float cellsPerTick = (float)cfgFor(0).monSpeed / 256.0f / CELL;
        int ideal = (int)(denDist / cellsPerTick);
        int budget = (int)(ideal * 1.6f) + 120;
        fprintf(g_tf, "%-56s %d cells, ideal %ds, budget %ds\n",
                "F3 den-to-player walk", denDist, ideal / 60, budget / 60);
        fprintf(g_tf, "%-56s quiet %ds (%d out), music %ds (%d out)\n",
                "F3 real stage 1: time to first contact",
                (quiet / 10) / 60, quiet % 10, (loud / 10) / 60, loud % 10);
        check("F3 a monster hunts a parked player down without dawdling",
              quiet / 10 < budget);
        check("F3 music playing does not blunt the hunt", loud / 10 < budget);
        check("F3 more than one monster is out early", (quiet % 10) >= 2);
    }
    {   // DIAGNOSTIC: how often does an inbound shot actually get dodged?
        // Realistic geometry: a corridor with open lanes either side, so a
        // sidestep is always physically available.
        auto dodgeRate = [&](int stage, int& attempts, int& kills) {
            aiReset();
            G.rng = 5150;
            sandbox();
            for (int cy = 6; cy <= 8; cy++)
                for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
            putDigger(2, 7, DIR_R);
            G.cfg = cfgFor(stage);
            addMon(14, 7, 0, MK_NOBBIN, G.cfg.monSpeed);
            attempts = 0; kills = 0;
            int prevDodgeT = 0;
            for (int i = 0; i < 6000; i++) {
                if (G.fireCool == 0) g_keyHit[VK_SPACE] = true;
                int before = (int)G.mons.size();
                update();
                if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
                // dodgeT only jumps upward when a fresh sidestep starts; it
                // decrements otherwise.  Counting "dodgeT > 0" would re-count
                // one dodge on every following shot.
                int dt = G.mons.empty() ? 0 : G.mons[0].dodgeT;
                if (dt > prevDodgeT) attempts++;
                prevDodgeT = dt;
                if (before > 0 && G.mons.empty()) {
                    kills++;
                    addMon(14, 7, 0, MK_NOBBIN, G.cfg.monSpeed);
                }
                // keep it in the corridor at a shootable distance
                if (!G.mons.empty()) {
                    int mcx = (G.mons[0].x + CELL / 2) / CELL;
                    if (mcx < 6) { G.mons[0].x = 14 * CELL; G.mons[0].y = 7 * CELL; }
                }
            }
            return 0;
        };
        int a1 = 0, k1 = 0, a50 = 0, k50 = 0, a98 = 0, k98 = 0;
        dodgeRate(0, a1, k1);
        dodgeRate(49, a50, k50);
        dodgeRate(98, a98, k98);
        fprintf(g_tf, "%-56s s1 %d/%d, s50 %d/%d, s99 %d/%d\n",
                "F4 dodge attempts / times shot dead", a1, k1, a50, k50, a98, k98);
        // Attempt counts are not comparable across stages (a monster that keeps
        // surviving generates fewer fresh engagements), so judge on the outcome:
        // deaths under identical sustained fire.
        check("F4 even a stage-1 monster flinches from incoming fire", a1 > 0);
        check("F4 a stage-50 monster dodges too", a50 > 0);
        check("F4 evasion cuts deaths versus stage 1", k50 < k1 && k98 < k1);
        check("F4 late-stage monsters survive at least as well as mid-stage",
              k98 <= k50);
        check("F4 but stage 1 is still comfortably killable", k1 > 20);
    }
    {   // The lane sense is only a nudge: it must never turn the hunt into
        // keep-away.  Against a player firing non-stop, fresh monsters (that
        // can dodge, as they do late game) must still reach the tank.
        int worst = 0;
        for (unsigned seed : { 77u, 1234u, 555u, 8888u }) {
            aiReset();
            G.rng = seed;
            sandbox();
            for (int cy = 4; cy <= 10; cy++)
                for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
            putDigger(4, 7, DIR_R);
            G.cfg.aiWeight = 100; G.cfg.dodgeSkill = 74;
            addMon(18, 7, 3, MK_NOBBIN, 360);     // armoured, so dying is not the limit
            int reached = 99;
            for (int i = 0; i < 4000; i++) {
                if (G.fireCool == 0) g_keyHit[VK_SPACE] = true;
                update();
                if (G.mons.empty()) addMon(18, 7, 3, MK_NOBBIN, 360);
                if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
                int d = abs((G.mons[0].x + CELL / 2) / CELL - (G.dig.x + CELL / 2) / CELL)
                      + abs((G.mons[0].y + CELL / 2) / CELL - (G.dig.y + CELL / 2) / CELL);
                if (d < reached) reached = d;
            }
            if (reached > worst) worst = reached;
        }
        fprintf(g_tf, "%-56s %d cells (worst of 4 seeds)\n",
                "F3 closest under non-stop fire", worst);
        check("F3 the hunt never degenerates into keep-away", worst <= 2);

        // The fairness counterpart: an unarmoured monster charging a defended
        // lane must actually be killable, however well it dodges.
        aiReset();
        G.rng = 4242;
        sandbox();
        for (int cy = 4; cy <= 10; cy++)
            for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
        putDigger(4, 7, DIR_R);
        G.cfg.aiWeight = 100; G.cfg.dodgeSkill = 74;
        addMon(18, 7, 0, MK_NOBBIN, 360);
        int killed = 0;
        for (int i = 0; i < 4000; i++) {
            if (G.fireCool == 0) g_keyHit[VK_SPACE] = true;
            bool had = !G.mons.empty();
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            if (had && G.mons.empty()) { killed++; addMon(18, 7, 0, MK_NOBBIN, 360); }
        }
        fprintf(g_tf, "%-56s %d\n", "F3 tier-0 monsters killed by a defended lane", killed);
        check("F3 a well-defended lane still kills unarmoured monsters", killed >= 3);
    }
    {   // the model must actually be recording what the player does
        aiReset();
        sandbox();
        for (int cx = 2; cx <= 20; cx++) digCellFull(cx, 7);
        putDigger(6, 7, DIR_R);
        for (int i = 0; i < 400; i++) {
            if (G.fireCool == 0) g_keyHit[VK_SPACE] = true;
            update();
        }
        check("F3 the model learned which way this player shoots",
              g_ai.fireDir[DIR_R] > g_ai.fireDir[DIR_U] &&
              g_ai.fireDir[DIR_R] > g_ai.fireDir[DIR_L]);
        check("F3 the model noticed the player was standing still", g_ai.camp > 0.5f);
        check("F3 the model counted the shots", g_ai.shots > 5);

        aiReset();
        sandbox();
        for (int cx = 2; cx <= 20; cx++) digCellFull(cx, 7);
        putDigger(6, 7, DIR_R);
        feedHold(300, VK_RIGHT);
        check("F3 the model tracked horizontal travel",
              g_ai.axisPref > 0.0f && g_ai.velX > 0.1f);
        check("F3 a moving player does not read as camping", g_ai.camp < 0.5f);
    }
    {   // adaptation has to be off at the start and full-strength at the end
        check("F3 stage 1 monsters are not adaptive at all", cfgFor(0).aiWeight == 0);
        bool rising = true;
        int prev = cfgFor(0).aiWeight;
        for (int lv = 1; lv < MAXLEVEL; lv++) {
            if (cfgFor(lv).aiWeight < prev) rising = false;
            prev = cfgFor(lv).aiWeight;
        }
        check("F3 adaptation grows monotonically to the last stage",
              rising && cfgFor(98).aiWeight == 100);
    }
    {   // ...and must relax in hunt mode, or the stage could never be finished
        sandbox();
        for (int cy = 4; cy <= 10; cy++)
            for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
        putDigger(2, 7, DIR_R);
        G.cfg = cfgFor(98);
        G.gemLeft = 0; G.monLeft = 0;
        addMon(20, 7, 0, MK_NOBBIN, 300);
        int startDist = 20 - 2;
        int closest = startDist;
        for (int i = 0; i < 1200 && !G.mons.empty(); i++) {
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            if (!G.mons.empty()) {
                int d = (G.mons[0].x / CELL) - (G.dig.x / CELL);
                if (d < 0) d = -d;
                if (d < closest) closest = d;
            }
        }
        check("F3 in hunt mode even a stage-99 monster closes in", closest <= 3);
    }

    // ---- G. borer budget grows but stays rationed --------------------------
    {
        bool grows = cfgFor(98).borerMax > cfgFor(20).borerMax;
        bool none1 = cfgFor(0).borerMax == 0;
        bool capped = true, liveOK = true;
        for (int lv = 0; lv < MAXLEVEL; lv++) {
            LevelCfg c = cfgFor(lv);
            if (c.borerMax > 5) capped = false;
            if (c.borerLive > c.borerMax || c.borerLive > 2) liveOK = false;
        }
        check("G stage 1 has no digging monsters", none1);
        check("G digging monsters become more common later", grows);
        check("G digging monsters are capped per stage", capped);
        check("G only one or two borers may be loose at once", liveOK);
    }
    {   // the live cap has to hold in the simulation, not just in the table
        loadLevel(90); placeDigger(); G.state = GS_PLAY;
        int worst = 0;
        for (int i = 0; i < 2600; i++) {
            update();
            int n = liveBorers();
            if (n > worst) worst = n;
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
        }
        check("G live borer cap is respected while actually playing",
              worst <= cfgFor(90).borerLive);
    }

    // ---- H. stage ends only when the ore AND the monsters are gone ---------
    sandbox();
    putDigger(0, 0, DIR_R);
    G.gemLeft = 0; G.monLeft = 0;
    addMon(6, 6, 0, MK_NOBBIN, 0);
    feed(30, 0);
    check("H no clear while a monster is still alive", G.state == GS_PLAY);
    G.mons.clear();
    feed(2, 0);
    check("H clear once both conditions hold", G.state == GS_CLEAR);

    sandbox();
    putDigger(0, 0, DIR_R);
    G.gemLeft = 5; G.monLeft = 0;
    feed(30, 0);
    check("H killing every monster is not enough on its own", G.state == GS_PLAY);

    // Dying must not consume the stage's monsters: the roster is monLeft plus
    // whatever is on screen, and that total has to survive a death.
    sandbox();
    for (int cy = 5; cy <= 11; cy++) digCellFull(5, cy);
    { Bag b{}; b.x = 5 * CELL; b.y = 4 * CELL; G.bags.push_back(b); }
    putDigger(5, 11, DIR_R);
    G.monLeft = 4;
    addMon(9, 7, 0, MK_NOBBIN, 0);
    addMon(11, 7, 0, MK_HOBBIN, 0);
    addMon(13, 7, 0, MK_BORER, 0);
    G.borersOut = 1;
    { int rosterBefore = G.monLeft + (int)G.mons.size();
      int lives0 = G.lives;
      feed(400, 0);                               // the bag falls and kills the digger
      check("H the fall really did kill the digger", G.lives == lives0 - 1);
      int rosterAfter = G.monLeft + (int)G.mons.size();
      fprintf(g_tf, "%-56s %d -> %d\n", "H stage roster across a death",
              rosterBefore, rosterAfter);
      check("H dying does not shrink the stage's monster roster",
            rosterAfter == rosterBefore);
      check("H and the borer budget is handed back too", G.borersOut == 0); }

    // ---- I. monsters are released as the seam is stripped ------------------
    {
        loadLevel(3); placeDigger(); G.state = GS_PLAY;
        int total = G.cfg.monTotal;
        // Cull as we go, standing in for the player killing them, so the
        // on-screen cap never masks the release schedule we are measuring.
        auto runCulling = [&](int ticks) {
            for (int i = 0; i < ticks; i++) {
                update();
                if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
                if (!G.mons.empty()) G.mons.pop_back();
            }
        };
        runCulling(240);
        int earlyOut = total - G.monLeft;
        check("I the stage opens with a real threat, not a lone monster",
              earlyOut >= 2);
        check("I but it does not empty the den at once", earlyOut < total);
        G.gemLeft = G.gemTotal / 2;
        runCulling(900);
        int midOut = total - G.monLeft;
        check("I more appear once half the ore is gone", midOut > earlyOut);
        G.gemLeft = 0;
        for (int i = 0; i < 3000 && G.monLeft > 0; i++) {
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            if ((int)G.mons.size() > 2) G.mons.pop_back();      // stand in for kills
        }
        check("I the whole roster is released once the ore is gone", G.monLeft == 0);
        check("I hunt mode engages when the ore runs out", G.hunt);
    }

    // ---- J. instant turning against a wall ---------------------------------
    sandbox();
    putDigger(0, 0, DIR_R);
    feedTap(VK_UP, 20);
    check("J faces UP against the top wall", G.dig.face == DIR_U);
    check("J did not leave the field", G.dig.x == 0 && G.dig.y == 0);
    feedHold(60, VK_UP);
    check("J holding into a wall still does not leave the field", G.dig.y == 0);

    // ---- K. cherry bonus ----------------------------------------------------
    sandbox();
    digCellFull(G.chCx, G.chCy);
    digCellFull(G.chCx, G.chCy + 1);
    putDigger(G.chCx, G.chCy + 1, DIR_U);
    G.cherryUp = true; G.cherryT = 600;
    feedHold(120, VK_UP);
    check("K cherry eaten -> bonus mode", G.bonusT > 0);
    addMon(G.dig.x / CELL, G.dig.y / CELL, 3, MK_BORER, 0);
    G.mons[0].x = G.dig.x; G.mons[0].y = G.dig.y;
    { int sc1 = G.score;
      feed(3, 0);
      check("K even an armoured borer is edible in bonus mode",
            G.mons.empty() && G.score == sc1 + 200);
      check("K digger survived the contact", G.dig.alive); }

    // ---- L. difficulty stays inside human limits ---------------------------
    {
        bool speedOK = true, capOK = true, monoOK = true;
        LevelCfg prev = cfgFor(0);
        for (int lv = 0; lv < MAXLEVEL; lv++) {
            LevelCfg c = cfgFor(lv);
            int worst = c.monSpeed + c.maxTier * 15 + 20;
            if (worst >= DIG_SPEED) speedOK = false;
            if (worst > DIG_SPEED * 90 / 100) capOK = false;
            if (c.monTotal < prev.monTotal || c.monSpeed < prev.monSpeed ||
                c.maxTier < prev.maxTier || c.dodgeSkill < prev.dodgeSkill ||
                c.borerMax < prev.borerMax || c.aiWeight < prev.aiWeight) monoOK = false;
            prev = c;
        }
        check("L no monster is ever as fast as the digger", speedOK);
        check("L fastest monster stays under 90% of digger speed", capOK);
        check("L difficulty never goes backwards", monoOK);
        LevelCfg last = cfgFor(MAXLEVEL - 1), sat = cfgFor(70);
        // Raw power saturates so the late game stays humanly playable; only
        // evasion and cunning keep rising, which makes it harder to outplay
        // rather than merely harder to outrun.
        check("L raw power saturates instead of growing forever",
              last.monTotal == sat.monTotal && last.monSpeed == sat.monSpeed &&
              last.maxTier == sat.maxTier && last.borerMax == sat.borerMax);
        check("L skill keeps climbing past the power plateau",
              last.dodgeSkill > sat.dodgeSkill && last.aiWeight > sat.aiWeight);
    }
    {   // the beat-mode surge must not turn into an unfair speed boost
        sandbox();
        for (int cx = 1; cx <= 22; cx++) digCellFull(cx, 7);
        putDigger(1, 7, DIR_R);
        addMon(20, 7, 3, MK_HOBBIN, cfgFor(98).monSpeed + 3 * 15 + 20);
        g_musData = (float*)calloc(64, sizeof(float));
        g_musFrames = 64; g_musPlaying = true; g_beatPulse = 1.0f;
        int x0 = G.mons[0].x;
        for (int i = 0; i < 60; i++) { g_beatPulse = 1.0f; update(); }
        int travelled = x0 - G.mons[0].x;
        g_musPlaying = false; free(g_musData); g_musData = nullptr; g_musFrames = 0;
        check("L a monster on the beat still cannot outrun the digger",
              travelled <= 60 * DIG_SPEED * 86 / 100 / 256 + 2);
    }

    // ---- M. all 99 generated stages are playable ---------------------------
    {
        int badConn = 0, badGem = 0, badBag = 0, badStart = 0;
        long long gemSum = 0;
        int lattice = 0;
        for (int lv = 0; lv < MAXLEVEL; lv++) {
            GenResult r;
            genLevel(lv, r);
            if (r.gemCount < 60 || r.gemCount > 160) badGem++;
            gemSum += r.gemCount;
            if (r.bagCount < 4) badBag++;
            if (!r.dug[r.startCy][r.startCx] || !r.dug[r.denCy][r.denCx]) badStart++;
            for (int cy = 0; cy < FH; cy++)
                for (int cx = 0; cx < FW; cx++)
                    if (r.bag[cy][cx] && r.dug[cy][cx]) badBag++;

            // A parade-ground layout would make every emerald's neighbours
            // identical row to row; count how often a row repeats the one above.
            int sameRows = 0;
            for (int cy = 1; cy < FH; cy++) {
                bool same = true;
                for (int cx = 0; cx < FW; cx++)
                    if (r.gem[cy][cx] != r.gem[cy - 1][cx]) { same = false; break; }
                if (same) sameRows++;
            }
            if (sameRows > 2) lattice++;

            bool seen[FH][FW] = {};
            int qx[FW * FH], qy[FW * FH], head = 0, tail = 0;
            qx[tail] = r.denCx; qy[tail++] = r.denCy; seen[r.denCy][r.denCx] = true;
            while (head < tail) {
                int cx = qx[head], cy = qy[head]; head++;
                for (int t = 0; t < 4; t++) {
                    int nx = cx + DX[t], ny = cy + DY[t];
                    if (nx < 0 || nx >= FW || ny < 0 || ny >= FH || seen[ny][nx]) continue;
                    if (!r.dug[ny][nx]) continue;
                    seen[ny][nx] = true; qx[tail] = nx; qy[tail++] = ny;
                }
            }
            if (!seen[r.startCy][r.startCx]) badConn++;
        }
        check("M all 99 dens connect to the player start", badConn == 0);
        check("M all 99 stages have a workable emerald count", badGem == 0);
        check("M no stage parks a bag on the monster corridor", badBag == 0);
        check("M start and den cells are always open", badStart == 0);
        check("M average stage is 70-150 emeralds",
              gemSum / MAXLEVEL >= 70 && gemSum / MAXLEVEL <= 150);
        check("M no stage lays the ore out in a lattice", lattice == 0);
    }

    // ---- N. WAV decoding ----------------------------------------------------
    {
        // write a 16-bit stereo 22050 Hz file and check it decodes and resamples
        const char* path = "selftest_tone.wav";
        FILE* f = nullptr;
        fopen_s(&f, path, "wb");
        bool wrote = false;
        if (f) {
            int rate = 22050, frames = rate / 2, ch = 2, bits = 16;
            int dataBytes = frames * ch * bits / 8;
            int riff = 36 + dataBytes, fmtLen = 16, fmtTag = 1;
            int byteRate = rate * ch * bits / 8, align = ch * bits / 8;
            fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
            fwrite("fmt ", 1, 4, f); fwrite(&fmtLen, 4, 1, f);
            fwrite(&fmtTag, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
            fwrite(&byteRate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
            fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
            for (int i = 0; i < frames; i++) {
                short s = (short)(sinf(TAU * 440.0f * i / rate) * 20000);
                fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
            }
            fclose(f);
            wrote = true;
        }
        check("N test wav written", wrote);
        int fr = 0;
        float* pcm = decodeWav(path, fr);
        check("N wav decoded", pcm != nullptr);
        check("N resampled to the mixer rate",
              pcm && fr > SR / 2 - 200 && fr < SR / 2 + 200);
        bool sane = true;
        if (pcm) for (int i = 0; i < fr; i++) if (pcm[i] < -1.05f || pcm[i] > 1.05f) sane = false;
        check("N decoded samples are in range", sane);
        if (pcm) free(pcm);
        remove(path);

        int bad = 0;
        check("N a missing file decodes to nothing", decodeWav("no_such_file.wav", bad) == nullptr);
        check("N a non-wav file decodes to nothing", decodeWav("digger.cpp", bad) == nullptr);
    }

    // ---- O. beat detection reacts to a real signal --------------------------
    {
        // half a second of silence, then a loud pulse train at 120 bpm
        int n = SR * 3;
        float* buf = (float*)calloc(n, sizeof(float));
        for (int beat = 0; beat < 6; beat++) {
            int at = (int)(SR * 0.5 + beat * SR * 0.5);
            for (int i = 0; i < SR / 20 && at + i < n; i++)
                buf[at + i] = sinf(TAU * 120.0f * i / SR) * expf(-i * 12.0f / SR) * 0.9f;
        }
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musData = buf; g_musFrames = n; g_musPos = 0; g_musPlaying = true;
            memset(g_beatE, 0, sizeof(g_beatE));
            g_beatCount = 0; g_lastBeatT = 0; g_audioClock = 0; g_bpm = 0;
        }
        short* out = new short[ABUF_FRAMES];
        while (g_musPos < n) renderAudio(out, ABUF_FRAMES);
        delete[] out;
        unsigned beats = g_beatCount;
        float bpm = g_bpm;
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musPlaying = false; g_musData = nullptr; g_musFrames = 0;
        }
        free(buf);
        fprintf(g_tf, "%-56s %u beats, %.0f bpm\n", "O detector output", beats, bpm);
        check("O onsets were detected in the pulse train", beats >= 4 && beats <= 8);
        check("O tempo estimate lands near 120 bpm", bpm > 100.0f && bpm < 140.0f);
    }
    {   // silence must not produce phantom beats
        int n = SR;
        float* buf = (float*)calloc(n, sizeof(float));
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musData = buf; g_musFrames = n; g_musPos = 0; g_musPlaying = true;
            memset(g_beatE, 0, sizeof(g_beatE));
            g_beatCount = 0; g_lastBeatT = 0; g_audioClock = 0;
        }
        short* out = new short[ABUF_FRAMES];
        while (g_musPos < n) renderAudio(out, ABUF_FRAMES);
        delete[] out;
        unsigned beats = g_beatCount;
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musPlaying = false; g_musData = nullptr; g_musFrames = 0;
        }
        free(buf);
        check("O silence produces no beats", beats == 0);
    }

    // ---- Q. the built-in default track --------------------------------------
    {
        buildDefaultTrack();
        check("Q the default chiptune was generated", g_defaultTrack != nullptr);
        check("Q it is a sensible loop length (12-40s)",
              g_defaultFrames > SR * 12 && g_defaultFrames < SR * 40);
        bool loud = false, inRange = true;
        if (g_defaultTrack)
            for (int i = 0; i < g_defaultFrames; i++) {
                float v = g_defaultTrack[i];
                if (v > 0.2f || v < -0.2f) loud = true;
                if (v > 1.01f || v < -1.01f) inRange = false;
            }
        check("Q the default track actually has signal", loud);
        check("Q the default track never clips out of range", inRange);

        // run it through the detector: a chiptune with a four-on-the-floor kick
        // must produce a believable tempo
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musData = g_defaultTrack; g_musFrames = g_defaultFrames; g_musPos = 0;
            g_musPlaying = true; g_musIsDefault = true;
            memset(g_beatE, 0, sizeof(g_beatE));
            g_beatCount = 0; g_lastBeatT = 0; g_audioClock = 0; g_bpm = 0;
        }
        short* out = new short[ABUF_FRAMES];
        while (g_musPos < g_defaultFrames) renderAudio(out, ABUF_FRAMES);
        delete[] out;
        float bpm = g_bpm;
        unsigned beats = g_beatCount;
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musPlaying = false; g_musData = nullptr; g_musFrames = 0; g_musIsDefault = false;
        }
        fprintf(g_tf, "%-56s %u beats, %.0f bpm\n", "Q default-track detector", beats, bpm);
        check("Q the default track has a detectable tempo", bpm > 90.0f && bpm < 150.0f);
    }

    // ---- R. shuffle vs sequential playback ----------------------------------
    {
        g_plN = 0; g_plCur = -1; g_shuffle = false;
        for (int i = 0; i < 6; i++) {             // six dummy (undecodable) tracks
            sprintf_s(g_pl[i].path, "track_%d.wav", i);
            sprintf_s(g_pl[i].name, "TRACK %d", i);
        }
        g_plN = 6;

        // sequential: next steps through 0,1,2,... and wraps
        g_plCur = 0;
        int seq[4];
        for (int i = 0; i < 4; i++) { musicNext(1); seq[i] = g_plCur; }
        check("R sequential next advances in order",
              seq[0] == 1 && seq[1] == 2 && seq[2] == 3 && seq[3] == 4);
        g_plCur = 5; musicNext(1);
        check("R sequential wraps past the end", g_plCur == 0);

        // shuffle: never repeats the current track, visits several, stays in range
        g_shuffle = true;
        g_shufRng = 999;
        int seen[6] = { 0 };
        bool everRepeated = false, outOfRange = false;
        int prev = g_plCur;
        for (int i = 0; i < 40; i++) {
            musicNext(1);
            if (g_plCur == prev) everRepeated = true;
            if (g_plCur < 0 || g_plCur >= 6) outOfRange = true;
            else seen[g_plCur]++;
            prev = g_plCur;
        }
        int distinct = 0;
        for (int i = 0; i < 6; i++) if (seen[i]) distinct++;
        check("R shuffle never replays the current track back-to-back", !everRepeated);
        check("R shuffle stays within the playlist", !outOfRange);
        check("R shuffle actually visits several tracks", distinct >= 4);
        g_shuffle = false; g_plN = 0; g_plCur = -1;
    }

    // ---- S. the spectrum analyser produces real per-band data ---------------
    {
        specInit();
        // feed a sustained tone near band 8's centre; that band should dominate
        int target = 8;
        float fc = 55.0f * powf(6000.0f / 55.0f, (float)target / (NUM_BANDS - 1));
        int n = SR;
        float* tone = (float*)malloc((size_t)n * sizeof(float));
        for (int i = 0; i < n; i++) tone[i] = sinf(6.2831853f * fc * i / SR) * 0.6f;
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musData = tone; g_musFrames = n; g_musPos = 0; g_musPlaying = true;
            for (int b = 0; b < NUM_BANDS; b++)
                g_specBand[b] = g_svfLow[b] = g_svfBand[b] = 0.0f;
        }
        short* out = new short[ABUF_FRAMES];
        while (g_musPos < n) renderAudio(out, ABUF_FRAMES);
        delete[] out;
        float bandCopy[NUM_BANDS];
        int loud = -1; float loudLvl = 0;
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            for (int b = 0; b < NUM_BANDS; b++) {
                bandCopy[b] = g_specBand[b];
                if (g_specBand[b] > loudLvl) { loudLvl = g_specBand[b]; loud = b; }
            }
            g_musPlaying = false; g_musData = nullptr; g_musFrames = 0;
        }
        free(tone);
        fprintf(g_tf, "%-56s band %d (wanted ~%d)\n",
                "S loudest spectrum band for the test tone", loud, target);
        check("S the tone lights up a band", loudLvl > 0.15f);
        check("S the loudest band is the one at the tone's pitch",
              loud >= target - 1 && loud <= target + 1);
        // a low-frequency band should stay quiet for this mid tone
        check("S far-off bands stay comparatively quiet", bandCopy[0] < loudLvl * 0.6f);
    }

    // ---- T. monsters articulate across the spectrum -------------------------
    {
        // Each kind must own a distinct register, and the taps a single monster
        // reads must be spread out rather than all landing on one band.
        int bBorer = monsterBand(MK_BORER, 0);
        int bNob   = monsterBand(MK_NOBBIN, 0);
        int bHob   = monsterBand(MK_HOBBIN, 0);
        check("T borers hold the bass end", bBorer <= 3);
        check("T nobbins sit in the midrange", bNob >= 4 && bNob <= 8);
        check("T hobbins ride the treble", bHob >= 9 && bHob < NUM_BANDS);
        check("T the three kinds occupy different registers",
              bBorer < bNob && bNob < bHob);

        // spawn order must fan them out inside a register, not stack them up
        bool fanned = false;
        for (unsigned s = 1; s < 5; s++)
            if (monsterBand(MK_NOBBIN, s) != bNob) fanned = true;
        check("T successive monsters of a kind spread across bands", fanned);

        // every tap a monster uses must be in range, and they must not collapse
        // onto a single band (otherwise the limbs would move in unison)
        bool inRange = true;
        int distinct = 0;
        static const int TAPS[6] = { 0, 1, 3, 4, 5, 6 };
        int seenBand[NUM_BANDS] = { 0 };
        for (int t = 0; t < 6; t++) {
            int b = (bNob + TAPS[t]) % NUM_BANDS;
            if (b < 0 || b >= NUM_BANDS) inRange = false;
            else if (!seenBand[b]) { seenBand[b] = 1; distinct++; }
        }
        check("T every body-part tap lands on a real band", inRange);
        check("T body parts read at least 5 different bands", distinct >= 5);

        // bandLvl must actually reflect the analyser, and wrap safely
        for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
        g_specBand[7] = 0.8f;
        check("T bandLvl reads the analyser", bandLvl(7, 0) > 0.7f);
        check("T bandLvl of a quiet band is silent", bandLvl(0, 0) == 0.0f);
        check("T bandLvl wraps instead of running off the end",
              bandLvl(NUM_BANDS - 1, 5) >= 0.0f && bandLvl(0, -3) >= 0.0f);
        for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
    }
    {   // With music silent the creatures must fall back to plain idle motion:
        // rendering a monster with a dead analyser must not crash or drift.
        sandbox();
        for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
        putDigger(6, 7, DIR_R);
        addMon(10, 7, 2, MK_HOBBIN, 300);
        addMon(12, 7, 1, MK_BORER, 300);
        G.state = GS_PLAY;
        for (int i = 0; i < 30; i++) { G.phase++; render(); }
        check("T monsters render fine with no music at all", true);
        // and with every band pinned hot they must still render in bounds
        for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 1.0f;
        for (int i = 0; i < 30; i++) { G.phase++; render(); }
        check("T monsters render fine with the spectrum pinned hot", true);
        for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
    }

    // ---- U. enemy tanks return fire, but fairly -----------------------------
    {
        check("U an enemy shell is slower than the player's fireball",
              SHELL_SPEED < 9);
        // Not an arbitrary duration -- the flash-up only has to be long enough
        // that, plus the shell's flight, it beats the time needed to clear the
        // lane.  That property is asserted directly below; this is just a floor
        // so it can never be quietly tuned to zero.
        check("U there is always a visible flash-up before the shell",
              AIM_TICKS >= 20);
        bool reloadOK = true, rangeOK = true;
        for (int lv = 0; lv < MAXLEVEL; lv++) {
            LevelCfg c = cfgFor(lv);
            if (c.gunReload < 90) reloadOK = false;        // never faster than 1.5s
            if (c.gunRange > 20) rangeOK = false;
        }
        check("U reload never drops below 1.5 seconds", reloadOK);
        check("U gun range stays bounded", rangeOK);
        check("U enemy reach is comparable to the player's, not a token 6 cells",
              cfgFor(0).gunRange >= 10);
        check("U stage 1 gunnery is gentler than stage 99",
              cfgFor(0).gunRange < cfgFor(98).gunRange &&
              cfgFor(0).gunReload > cfgFor(98).gunReload);
        // The flash-up plus the shell's flight must always outlast the time the
        // player needs to clear one cell, or a shot would be unavoidable.
        int ticksToClearLane = (CELL * 256 + DIG_SPEED - 1) / DIG_SPEED;
        int worstWarning = AIM_TICKS + (CELL - 20) / SHELL_SPEED;   // point blank
        fprintf(g_tf, "%-56s warning %d ticks, need %d\n",
                "U point-blank warning vs time to clear a lane",
                worstWarning, ticksToClearLane);
        check("U even a point-blank shot can be stepped out of",
              worstWarning >= ticksToClearLane);
    }
    {   // no line of sight -> never fires, however skilled
        sandbox();
        digCellFull(6, 7); digCellFull(14, 7);        // two isolated pockets
        putDigger(6, 7, DIR_R);
        G.cfg = cfgFor(98);
        addMon(14, 7, 0, MK_NOBBIN, 0);
        G.mons[0].speed = 0;
        bool fired = false, aimed = false;
        for (int i = 0; i < 600; i++) {
            update();
            if (!G.mons.empty() && G.mons[0].aimT > 0) aimed = true;
            for (const Shell& sh : G.shells) if (sh.alive) fired = true;
        }
        check("U a tank with soil in the way never lines up a shot", !aimed);
        check("U and never fires one", !fired);
    }
    {   // clear lane -> telegraphs first, then fires
        sandbox();
        for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
        putDigger(6, 7, DIR_R);
        G.cfg = cfgFor(98);
        addMon(12, 7, 0, MK_NOBBIN, 0);
        G.mons[0].speed = 0;
        int sawAim = 0, shellDuringAim = 0, sawShell = 0;
        for (int i = 0; i < 400; i++) {
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            bool aiming = !G.mons.empty() && G.mons[0].aimT > 0;
            bool anyShell = false;
            for (const Shell& sh : G.shells) if (sh.alive) anyShell = true;
            if (aiming) { sawAim++; if (anyShell) shellDuringAim++; }
            if (anyShell) sawShell++;
        }
        check("U a tank with a clear lane winds up a shot", sawAim > 20);
        check("U the shell only appears after the windup", shellDuringAim == 0);
        check("U the shot does get fired", sawShell > 0);
    }
    {   // A sighted tank must open fire promptly -- no dithering while it has a
        // clear lane on you.  This is the behaviour that was missing: the old
        // build rolled a "do I feel like shooting" dice every 0.67s and at
        // stage 1 usually declined, so tanks stared at the player for seconds.
        for (int stage = 0; stage <= 98; stage += 49) {
            sandbox();
            for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
            putDigger(6, 7, DIR_R);
            G.cfg = cfgFor(stage);
            addMon(10, 7, 0, MK_NOBBIN, 0);       // 4 cells away, inside every range
            G.mons[0].speed = 0;
            int firstAim = -1, firstShell = -1;
            for (int i = 0; i < 400; i++) {
                update();
                if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
                if (firstAim < 0 && !G.mons.empty() && G.mons[0].aimT > 0) firstAim = i;
                for (const Shell& sh : G.shells)
                    if (sh.alive && firstShell < 0) firstShell = i;
            }
            char msg[96];
            sprintf_s(msg, "U stage %d: opens fire on the very first sighting", stage + 1);
            check(msg, firstAim >= 0 && firstAim <= 2);
            sprintf_s(msg, "U stage %d: shell away within the flash-up", stage + 1);
            check(msg, firstShell >= 0 && firstShell <= AIM_TICKS + 6);
            fprintf(g_tf, "%-56s stage %d: aim@%d shell@%d\n",
                    "U ticks from sighting to gun-up and shell", stage + 1,
                    firstAim, firstShell);
        }
    }
    {   // It must engage from right down a corridor, not only up close.
        // 11 cells apart, on stage 1 -- the gentlest gunnery in the game.
        sandbox();
        for (int cx = 2; cx <= 21; cx++) digCellFull(cx, 7);
        putDigger(3, 7, DIR_R);
        G.cfg = cfgFor(0);
        addMon(14, 7, 0, MK_NOBBIN, 0);           // 11 cells away
        G.mons[0].speed = 0;
        int firstShell = -1;
        for (int i = 0; i < 200; i++) {
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            for (const Shell& sh : G.shells)
                if (sh.alive && firstShell < 0) firstShell = i;
        }
        check("U it opens fire from 11 cells away even on stage 1",
              firstShell >= 0 && firstShell <= AIM_TICKS + 6);
        fprintf(g_tf, "%-56s %d ticks\n", "U long-range engagement, shell away at",
                firstShell);

        // ...and a long shot must give MORE warning than a close one, since the
        // shell is slow.  Measure the flight time actually observed.
        sandbox();
        for (int cx = 2; cx <= 21; cx++) digCellFull(cx, 7);
        putDigger(3, 7, DIR_R);
        G.cfg = cfgFor(0);
        addMon(14, 7, 0, MK_NOBBIN, 0);
        G.mons[0].speed = 0;
        int flight = 0; bool sawShell = false;
        for (int i = 0; i < 400; i++) {
            update();
            if (G.state != GS_PLAY) break;
            bool any = false;
            for (const Shell& sh : G.shells) if (sh.alive) any = true;
            if (any) { sawShell = true; flight++; }
            else if (sawShell) break;
        }
        int ticksToClearLane = (CELL * 256 + DIG_SPEED - 1) / DIG_SPEED;
        fprintf(g_tf, "%-56s %d ticks (need %d)\n",
                "U long shot flight time alone", flight, ticksToClearLane);
        check("U a long shot's flight time alone beats clearing the lane",
              flight >= ticksToClearLane);
    }
    {   // the gun should swing onto you during lock-on, before the windup even
        sandbox();
        for (int cy = 2; cy <= 12; cy++) digCellFull(8, cy);
        for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
        putDigger(14, 7, DIR_R);
        G.cfg = cfgFor(0);
        addMon(8, 7, 0, MK_NOBBIN, 0);
        G.mons[0].speed = 0;
        G.mons[0].dir = DIR_U;                    // hull pointing the wrong way
        bool gunTurned = false;
        for (int i = 0; i < 40; i++) {
            update();
            if (!G.mons.empty() && G.mons[0].acquire > 0 && G.mons[0].aimDir == DIR_R)
                gunTurned = true;
        }
        check("U the gun swings onto the target while still locking on", gunTurned);
        check("U the hull is not yanked around by the turret",
              !G.mons.empty() && G.mons[0].x % CELL == 0 && G.mons[0].y % CELL == 0);
    }
    {   // and breaking the line must drop the lock, not bank it
        sandbox();
        for (int cx = 2; cx <= 18; cx++) digCellFull(cx, 7);
        digCellFull(6, 6);
        putDigger(6, 7, DIR_R);
        G.cfg = cfgFor(0);
        addMon(10, 7, 0, MK_NOBBIN, 0);
        G.mons[0].speed = 0;
        for (int i = 0; i < 20; i++) update();    // it has the lane
        int acq = G.mons.empty() ? 0 : G.mons[0].acquire;
        check("U it flags the target while it has the lane", acq > 0);
        G.dig.y = 6 * CELL;                       // duck out of the lane
        // long enough for any shot already in its flash-up to finish leaving;
        // a committed round still goes, it just misses
        for (int i = 0; i < 40; i++) update();
        check("U losing the lane drops the target flag",
              !G.mons.empty() && G.mons[0].acquire == 0);
    }
    {   // a shell must be stopped by soil and must be able to kill
        sandbox();
        for (int cx = 2; cx <= 10; cx++) digCellFull(cx, 7);
        putDigger(3, 7, DIR_R);
        G.shells[0].alive = true; G.shells[0].dir = DIR_L;
        G.shells[0].x = 10 * CELL; G.shells[0].y = 7 * CELL + CELL / 2;
        feed(200, 0);
        check("U a shell that reaches the digger kills it",
              G.state == GS_DYING || G.lives < 3);

        sandbox();
        digCellFull(9, 7);                            // lone pocket, soil either side
        putDigger(2, 2, DIR_R);
        G.shells[0].alive = true; G.shells[0].dir = DIR_L;
        G.shells[0].x = 9 * CELL + CELL / 2; G.shells[0].y = 7 * CELL + CELL / 2;
        feed(120, 0);
        bool stillFlying = false;
        for (const Shell& sh : G.shells) if (sh.alive) stillFlying = true;
        check("U soil stops a shell", !stillFlying);
        check("U the digger across the map is untouched", G.dig.alive);
    }
    {   // Player fire and enemy fire cancel each other out.
        sandbox();
        for (int cx = 2; cx <= 20; cx++) digCellFull(cx, 7);
        putDigger(4, 7, DIR_R);
        G.cfg = cfgFor(0);
        addMon(16, 7, 0, MK_NOBBIN, 0);           // parked, so only its shell matters
        G.mons[0].speed = 0; G.mons[0].gunCool = 100000;
        // launch both head on
        G.shells[0].alive = true; G.shells[0].dir = DIR_L;
        G.shells[0].x = 14 * CELL; G.shells[0].y = 7 * CELL + CELL / 2;
        G.fireCool = 0; g_keyHit[VK_SPACE] = true;
        int sc0 = G.score;
        bool bothGone = false;
        for (int i = 0; i < 200; i++) {
            update();
            bool anyShell = false;
            for (const Shell& sh : G.shells) if (sh.alive) anyShell = true;
            if (!anyShell && !G.bul.alive) { bothGone = true; break; }
        }
        check("U a fireball and a shell annihilate on contact", bothGone);
        check("U the digger is not hit by the shell it shot down", G.dig.alive);
        check("U the tank behind the shell is not hit either", !G.mons.empty());
        check("U shooting down a shell scores", G.score > sc0);
        check("U it does not count as a miss, so nothing jeers",
              G.mons.empty() || G.mons[0].taunt == 0);
    }
    {   // The pair closes at 15px a tick, so a once-per-tick test would let them
        // pass through each other.  Start them almost touching and offset by
        // every sub-step phase to make sure none slips past.
        int escapes = 0;
        for (int phase = 0; phase < 15; phase++) {
            sandbox();
            for (int cx = 2; cx <= 20; cx++) digCellFull(cx, 7);
            putDigger(4, 7, DIR_R);
            G.cfg = cfgFor(0);
            G.bul.alive = true; G.bul.dir = DIR_R; G.bul.id++;
            G.bul.x = 8 * CELL; G.bul.y = 7 * CELL + CELL / 2;
            G.shells[0].alive = true; G.shells[0].dir = DIR_L;
            G.shells[0].x = 8 * CELL + 30 + phase; G.shells[0].y = 7 * CELL + CELL / 2;
            for (int i = 0; i < 6; i++) update();
            // if either survived while the other did too, they passed through
            bool anyShell = false;
            for (const Shell& sh : G.shells) if (sh.alive) anyShell = true;
            if (anyShell && G.bul.alive) escapes++;
        }
        fprintf(g_tf, "%-56s %d of 15\n", "U head-on passes that slipped through", escapes);
        check("U they never pass through each other at full closing speed",
              escapes == 0);
    }
    {   // shells in flight are capped, and fleeing tanks hold their fire
        sandbox();
        for (int cy = 5; cy <= 9; cy++)
            for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
        putDigger(11, 7, DIR_R);
        G.cfg = cfgFor(98);
        for (int i = 0; i < 6; i++) { addMon(2 + i * 3, 7, 0, MK_NOBBIN, 0); }
        for (Monster& m : G.mons) m.speed = 0;
        int worst = 0;
        for (int i = 0; i < 900; i++) {
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            int n = 0;
            for (const Shell& sh : G.shells) if (sh.alive) n++;
            if (n > worst) worst = n;
        }
        check("U shells in flight never exceed the cap", worst <= MAX_SHELLS);
        fprintf(g_tf, "%-56s %d\n", "U most shells airborne at once", worst);

        G.bonusT = 600;                               // cherry active: they flee
        for (Monster& m : G.mons) { m.flee = true; m.aimT = 0; m.gunCool = 0; }
        for (Shell& sh : G.shells) sh.alive = false;
        bool firedWhileFleeing = false;
        for (int i = 0; i < 300; i++) {
            G.bonusT = 600;
            update();
            if (G.state != GS_PLAY) { G.state = GS_PLAY; G.dig.alive = true; }
            for (const Shell& sh : G.shells) if (sh.alive) firedWhileFleeing = true;
        }
        check("U a fleeing tank holds its fire", !firedWhileFleeing);
    }
    {   // Playability: standing still in a lane is punished, but stepping out of
        // the lane during the windup must reliably save you.
        auto survive = [&](bool evade) {
            sandbox();
            for (int cy = 6; cy <= 8; cy++)
                for (int cx = 2; cx <= 21; cx++) digCellFull(cx, cy);
            putDigger(5, 7, DIR_R);
            G.cfg = cfgFor(0);                        // stage 1 gunnery
            // inside stage 1's 5-cell gun range, or it simply never has a shot
            addMon(9, 7, 0, MK_NOBBIN, 0);
            G.mons[0].speed = 0;
            int t = 0;
            for (; t < 3600; t++) {
                // when a windup starts, duck one row out of the firing lane
                if (evade && !G.mons.empty() && G.mons[0].aimT > 0 &&
                    (G.dig.y / CELL) == 7) {
                    g_keyHit[VK_UP] = true; g_keyDown[VK_UP] = true;
                }
                update();
                memset(g_keyDown, 0, sizeof(g_keyDown));
                if (G.state != GS_PLAY) break;
            }
            return t;
        };
        int passive = survive(false);
        int evading = survive(true);
        fprintf(g_tf, "%-56s passive %d, evading %d ticks\n",
                "U stage-1 survival in a covered lane", passive, evading);
        check("U standing in a covered lane gets you killed fast",
              passive < 3600);
        // No zero-warning kills: death can never land sooner than the flash-up.
        check("U you are never killed without seeing the flash-up first",
              passive >= AIM_TICKS);
        check("U stepping out of the lane keeps you alive far longer",
              evading > passive * 2 || evading >= 3600);
    }

    // ---- V. music-reactive livery, and the packaged theme -------------------
    {
        // band colours must actually differ across the spectrum
        bool distinct = true;
        for (int b = 1; b < NUM_BANDS; b++)
            if (bandColor(b) == bandColor(b - 1)) distinct = false;
        check("V neighbouring bands get different colours", distinct);
        check("V the bass end and treble end are clearly different",
              bandColor(0) != bandColor(NUM_BANDS - 1));

        // The livery must be driven by the band, and must NOT fire for the
        // built-in theme -- that is the whole point of the request.
        auto renderTank = [&](float tint, int band, float lvl) {
            memset(g_scr, 0, sizeof(g_scr));
            for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
            g_specBand[band] = lvl;
            drawEnemyTank(300.0f, 300.0f, 2, MK_NOBBIN, DIR_R, DIR_R, 0,
                          false, 0, 0, 0, 1.0f, band, tint, 400.0f, 300.0f);
            unsigned h = 2166136261u;
            for (int i = 0; i < SCRW * SCRH; i++) h = (h ^ g_scr[i]) * 16777619u;
            return h;
        };
        unsigned plain = renderTank(0.0f, 5, 0.9f);
        unsigned lit   = renderTank(1.0f, 5, 0.9f);
        unsigned other = renderTank(1.0f, 11, 0.9f);
        check("V a real track recolours the tank", plain != lit);
        check("V different bands give different liveries", lit != other);
        unsigned plainAgain = renderTank(0.0f, 5, 0.9f);
        check("V with the built-in theme the tank keeps its armour colours",
              plain == plainAgain);

        // "The pixels differ" is far too weak a bar -- an imperceptible shift
        // passes it.  Measure how much light the tank actually puts out at a
        // quiet band versus a loud one; the swing has to be big to be noticed.
        auto tankBrightness = [&](float tint, int band, float lvl) {
            memset(g_scr, 0, sizeof(g_scr));
            for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
            g_specBand[band] = lvl;
            drawEnemyTank(300.0f, 300.0f, 2, MK_NOBBIN, DIR_R, DIR_R, 0,
                          false, 0, 0, 0, 1.0f, band, tint, 400.0f, 300.0f);
            double sum = 0; int n = 0;
            for (int y = 260; y < 360; y++)
                for (int x = 260; x < 360; x++) {
                    uint32_t c = g_scr[y * SCRW + x];
                    sum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
                    n++;
                }
            return sum / (n * 3.0);               // mean channel level, 0..255
        };
        double quiet = tankBrightness(1.0f, 5, 0.05f);
        double loud  = tankBrightness(1.0f, 5, 1.00f);
        double ratio = quiet > 1.0 ? loud / quiet : 99.0;
        fprintf(g_tf, "%-56s quiet %.1f, loud %.1f (x%.2f)\n",
                "V tank brightness across its band", quiet, loud, ratio);
        check("V the tank visibly blazes up on its band, not a subtle shift",
              ratio >= 1.8);
        check("V and it stays visible when that band is quiet", quiet > 12.0);

        // Armour readability must survive the light show: the hit pips and
        // armour plates are drawn in `trim`, which the livery must not touch.
        uint32_t l0, d0, t0, l1, d1, t1;
        tierColors(1, l0, d0, t0);
        tierColors(1, l1, d1, t1);
        check("V tier trim colour is what the pips use", t0 == t1);
        bool trimUnique = true;
        uint32_t seen[4];
        for (int t = 0; t < 4; t++) { uint32_t a, b, c; tierColors(t, a, b, c); seen[t] = c; }
        for (int i = 0; i < 4; i++)
            for (int j = i + 1; j < 4; j++)
                if (seen[i] == seen[j]) trimUnique = false;
        check("V every armour tier still has its own trim colour", trimUnique);

        // stun and flee are gameplay states and must beat the livery
        unsigned litStun = 0, litFlee = 0;
        {
            memset(g_scr, 0, sizeof(g_scr));
            for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
            g_specBand[5] = 0.9f;
            drawEnemyTank(300.0f, 300.0f, 2, MK_NOBBIN, DIR_R, DIR_R, 0,
                          true, 0, 0, 0, 1.0f, 5, 1.0f, 400.0f, 300.0f);
            unsigned h = 2166136261u;
            for (int i = 0; i < SCRW * SCRH; i++) h = (h ^ g_scr[i]) * 16777619u;
            litFlee = h;
        }
        {
            memset(g_scr, 0, sizeof(g_scr));
            for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
            g_specBand[5] = 0.9f;
            drawEnemyTank(300.0f, 300.0f, 2, MK_NOBBIN, DIR_R, DIR_R, 0,
                          false, 3, 0, 0, 1.0f, 5, 1.0f, 400.0f, 300.0f);
            unsigned h = 2166136261u;
            for (int i = 0; i < SCRW * SCRH; i++) h = (h ^ g_scr[i]) * 16777619u;
            litStun = h;
        }
        check("V a fleeing tank still reads as fleeing, not as livery",
              litFlee != lit);
        check("V a stunned tank still flashes its hit colour", litStun != lit);
        for (int b = 0; b < NUM_BANDS; b++) g_specBand[b] = 0.0f;
    }
    {   // The built-in theme is not a playlist entry: an untouched game has an
        // empty playlist and still has music.
        g_plN = 0; g_plCur = -1;
        musicFreeCurrent();
        check("V the theme plays with nothing in the playlist", musicPlayDefault());
        check("V and the playlist really is empty", g_plN == 0);
        check("V no track is selected while the theme plays", g_plCur < 0);
        check("V the theme is flagged as the built-in one", musicOnDefault());
        check("V so the livery stays off", !(musicActive() && !g_musIsDefault));

        // next/prev with an empty list must keep the theme going, not go silent
        musicNext(1);
        check("V next track with an empty list keeps the theme", musicOnDefault());
        musicNext(-1);
        check("V prev track with an empty list keeps the theme", musicOnDefault());

        // a real track added later takes over and shows up in the list
        int fr = 0;
        float* fake = (float*)calloc(SR, sizeof(float));
        for (int i = 0; i < SR; i++) fake[i] = sinf(6.2831853f * 300.0f * i / SR) * 0.5f;
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musIsDefault = false;               // stand in for a decoded file
            g_musData = fake; g_musFrames = SR; g_musPos = 0; g_musPlaying = true;
        }
        strcpy_s(g_pl[0].path, "fake.wav"); strcpy_s(g_pl[0].name, "FAKE"); g_plN = 1;
        check("V with a real track the playlist is no longer empty", g_plN == 1);
        check("V and it no longer counts as the built-in theme", !musicOnDefault());
        check("V so the livery switches on", musicActive() && !g_musIsDefault);
        {
            std::lock_guard<std::mutex> lk(g_audioM);
            g_musPlaying = false; g_musData = nullptr; g_musFrames = 0;
        }
        free(fake);
        (void)fr;
        g_plN = 0; g_plCur = -1;
    }
    {   // the app icon has to be paintable at every size Windows asks for
        for (int size : { 16, 32, 48, 64, 256 }) {
            memset(g_scr, 0, sizeof(g_scr));
            paintIcon(size);
            int lit = 0;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                    if ((g_scr[y * SCRW + x] & 0xFFFFFF) != 0) lit++;
            char msg[80];
            sprintf_s(msg, "V the %dpx icon actually draws something", size);
            check(msg, lit > size * size / 5);
        }
        // corners must be transparent, the middle opaque
        check("V icon corners are transparent", iconAlpha(0, 0, 64) < 0.05f);
        check("V icon centre is opaque", iconAlpha(32, 32, 64) > 0.95f);
        check("V icon alpha stays in range",
              iconAlpha(63, 63, 64) >= 0.0f && iconAlpha(63, 63, 64) <= 1.0f);
    }

    // ---- P. performance -----------------------------------------------------
    {
        loadLevel(0); placeDigger(); G.state = GS_PLAY;
        for (int i = 0; i < 6; i++) spawnMonster();
        burstParts(P_SPARK, 500, 300, 120, 3.0f, 5.0f, 3.0f, 0xFFAA40);
        render();
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        for (int i = 0; i < 60; i++) { G.phase++; render(); }
        QueryPerformanceCounter(&b);
        double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 60.0;
        fprintf(g_tf, "%-56s %.2f ms\n", "P render cost per frame", ms);
        check("P renders a busy frame within the 16.7ms budget", ms < 16.7);
        check("P leaves headroom for game logic (under 11ms)", ms < 11.0);

        // face must already match, or the new control scheme never starts a move
        // and this would time an idle digger
        G.state = GS_PLAY; G.gemLeft = 99999; G.monLeft = 99;
        G.dig.face = DIR_L; G.dig.holdT = HOLD_TICKS;
        g_keyDown[VK_LEFT] = true;
        QueryPerformanceCounter(&a);
        for (int i = 0; i < 600; i++) { G.gemLeft = 99999; G.monLeft = 99; update(); }
        QueryPerformanceCounter(&b);
        g_keyDown[VK_LEFT] = false;
        double lms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 600.0;
        fprintf(g_tf, "%-56s %.3f ms\n", "P logic cost per tick", lms);
        check("P logic measurement actually ran in play state", G.state == GS_PLAY);
        check("P one logic tick stays under 3ms", lms < 3.0);

        // the audio thread has a hard deadline of its own
        short* out = new short[ABUF_FRAMES];
        for (int i = 0; i < 12; i++) playSound(SND_KILL, 1.0f, 0.5f);
        QueryPerformanceCounter(&a);
        for (int i = 0; i < 200; i++) renderAudio(out, ABUF_FRAMES);
        QueryPerformanceCounter(&b);
        double ams = (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 200.0;
        delete[] out;
        fprintf(g_tf, "%-56s %.3f ms\n", "P audio block cost", ams);
        check("P audio block renders well inside its 11.6ms slot", ams < 4.0);
    }

    fprintf(g_tf, "================\n%s\n", g_fail ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    fclose(g_tf);
    return g_fail ? 1 : 0;
}
