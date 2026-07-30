// ---------------------------------------------------------------------------
//  Digger -- a from-scratch remake of the 1983 arcade game.
//  Copyright (C) 2026 netbula
//
//  This program is free software: you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation, either version 3 of the License, or (at your option)
//  any later version.  It is distributed WITHOUT ANY WARRANTY; see the GNU
//  General Public License for more details, or <https://www.gnu.org/licenses/>.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  audio.h -- real-time software synthesiser on waveOut.
//
//  Two sources are mixed here: the effect voices (each synthesised sample by
//  sample so they layer instead of cutting each other off) and, when the music
//  player is active, a decoded PCM track.  Because the track passes through
//  our own mixer we also get its samples, which is what makes real beat
//  detection possible -- that is what the monsters dance to.
// ---------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

static const int SR = 44100;
static const int ABUF_FRAMES = 512;
static const int ABUF_COUNT = 4;
static const int MAX_VOICES = 28;

enum {
    SND_DIG = 0, SND_GEM, SND_GOLD, SND_FIRE, SND_FALL, SND_BREAK,
    SND_HIT, SND_KILL, SND_DEATH, SND_TUNE, SND_SPAWN, SND_CHERRY,
    SND_EAT, SND_TAUNT, SND_BORER
};

struct Voice {
    int   kind;
    bool  active;
    float t, dur, ph, lp, amp, f0;
    unsigned rs;
};
static Voice      g_voice[MAX_VOICES];
static std::mutex g_audioM;
static int        g_voiceRR = 0;
static bool       g_sfxOn = true;

// ------------------------------ music stream -------------------------------
// Owned by the player, mixed here.  g_musData is mono, already resampled to SR.
static float*        g_musData = nullptr;
static int           g_musFrames = 0;
static double        g_musPos = 0.0;
static bool          g_musPlaying = false;
static float         g_musVol = 0.55f;

// ----------------------------- beat detection ------------------------------
// Energy-based onset detection: compare the current block's energy against a
// running average of the last half second.
static const int   BEAT_HIST = 48;
static float       g_beatE[BEAT_HIST] = { 0 };
static int         g_beatEi = 0;
static float       g_beatPulse = 0.0f;      // decays after each onset, 0..1
static unsigned    g_beatCount = 0;         // increments on every onset
static double      g_lastBeatT = 0.0;
static double      g_audioClock = 0.0;
static float       g_bpm = 0.0f;
static double      g_beatGaps[8] = { 0 };
static int         g_beatGapI = 0;
static const int   VIS_BARS = 96;
static float       g_visBar[VIS_BARS] = { 0 };
static int         g_visI = 0;

// ---------------------------- spectrum analyser ----------------------------
// A bank of resonant band-pass filters (Chamberlin state-variable) run over
// the music signal, giving genuine per-band levels for the retro bar display
// rather than a decorative fake.  Log-spaced from bass to presence.
static const int   NUM_BANDS = 14;
static float       g_specBand[NUM_BANDS] = { 0 };   // smoothed 0..1, read by UI
static float       g_svfLow[NUM_BANDS] = { 0 };
static float       g_svfBand[NUM_BANDS] = { 0 };
static float       g_svfF[NUM_BANDS] = { 0 };
static float       g_svfGain[NUM_BANDS] = { 0 };    // per-band makeup, see below
static float       g_specEnv[NUM_BANDS] = { 0 };
static const float g_svfQ = 0.30f;                  // resonance (1/Q)
static bool        g_specInit = false;

static void specInit()
{
    for (int i = 0; i < NUM_BANDS; i++) {
        float t = (float)i / (NUM_BANDS - 1);
        float fc = 55.0f * powf(6000.0f / 55.0f, t);    // 55 Hz .. 6 kHz
        float f = 2.0f * sinf(3.14159265f * fc / SR);
        if (f > 0.99f) f = 0.99f;
        g_svfF[i] = f;

        // Calibrate: the discrete SVF's peak gain drops with frequency, which
        // would bias the display toward the bass.  Drive a scratch copy with a
        // tone at the band centre, measure the settled peak, and store its
        // inverse so every band answers a tone at its own pitch with level ~1.
        float low = 0, band = 0, peak = 0;
        const int N = 4096;
        for (int k = 0; k < N; k++) {
            float x = sinf(6.2831853f * fc * k / SR);
            low += f * band;
            float high = x - low - g_svfQ * band;
            band += f * high;
            float a = band < 0 ? -band : band;
            if (k > N / 2 && a > peak) peak = a;
        }
        g_svfGain[i] = (peak > 1e-4f) ? 1.0f / peak : 1.0f;
    }
    g_specInit = true;
}

static inline float noiseF(unsigned& s)
{
    s = s * 1664525u + 1013904223u;
    return (float)((int)((s >> 9) & 0x7FFF) - 16384) / 16384.0f;
}

static void playSound(int kind, float pitch = 1.0f, float vol = 1.0f)
{
    if (!g_sfxOn) return;
    static const float DUR[] = { 0.16f, 0.16f, 0.30f, 0.26f, 0.90f, 0.60f,
                                 0.22f, 0.70f, 1.20f, 0.55f, 0.30f, 0.45f,
                                 0.28f, 0.34f, 0.20f };
    std::lock_guard<std::mutex> lk(g_audioM);
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) if (!g_voice[i].active) { slot = i; break; }
    if (slot < 0) { slot = g_voiceRR; g_voiceRR = (g_voiceRR + 1) % MAX_VOICES; }
    Voice& v = g_voice[slot];
    v.kind = kind; v.active = true;
    v.t = 0; v.ph = 0; v.lp = 0;
    v.dur = DUR[kind];
    v.amp = vol;
    v.f0 = pitch;
    v.rs = (unsigned)(GetTickCount() * 2654435761u) ^ (unsigned)(slot * 40503u);
}
static void stopAllSounds()
{
    std::lock_guard<std::mutex> lk(g_audioM);
    for (Voice& v : g_voice) v.active = false;
}

static const float TAU = 6.28318530718f;

static float voiceSample(Voice& v)
{
    const float t = v.t;
    const float u = t / v.dur;
    float o = 0.0f;
    switch (v.kind) {
    case SND_DIG: {
        float e = expf(-t * 19.0f) * (t * 320.0f < 1 ? t * 320.0f : 1.0f);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * 0.14f;
        o = v.lp * 1.7f * e + sinf(TAU * 74.0f * t) * 0.30f * e;
    } break;
    case SND_BORER: {                            // a monster chewing through soil
        float e = expf(-t * 16.0f);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * 0.09f;
        o = v.lp * 1.5f * e + sinf(TAU * 52.0f * t) * 0.35f * e;
    } break;
    case SND_GEM: {
        float f = (820.0f + 1150.0f * u) * v.f0;
        v.ph += f / SR;
        float e = expf(-t * 24.0f) * (t * 420.0f < 1 ? t * 420.0f : 1.0f);
        float sq = (fmodf(v.ph, 1.0f) < 0.5f) ? 0.30f : -0.30f;
        o = (sinf(TAU * v.ph) * 0.55f + sq) * e;
    } break;
    case SND_GOLD: {
        static const float ar[4] = { 784.0f, 988.0f, 1175.0f, 1568.0f };
        int st = (int)(t / 0.062f); if (st > 3) st = 3;
        v.ph += ar[st] * v.f0 / SR;
        float lt = t - st * 0.062f;
        float e = expf(-lt * 12.0f) * (lt * 420.0f < 1 ? lt * 420.0f : 1.0f);
        o = ((fmodf(v.ph, 1.0f) < 0.5f) ? 0.5f : -0.5f) * e * 0.75f;
    } break;
    case SND_FIRE: {
        float f = (170.0f + 1600.0f * expf(-t * 17.0f)) * v.f0;
        v.ph += f / SR;
        float e = expf(-t * 9.5f);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * 0.38f;
        float sq = (fmodf(v.ph, 1.0f) < 0.5f) ? 0.46f : -0.46f;
        o = (sq + v.lp * 0.40f) * e;
    } break;
    case SND_FALL: {
        float atk = t * 7.0f < 1 ? t * 7.0f : 1.0f;
        float rel = (v.dur - t) * 5.0f < 1 ? (v.dur - t) * 5.0f : 1.0f;
        float e = atk * (rel > 0 ? rel : 0);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * (0.02f + 0.10f * expf(-t * 2.2f));
        v.ph += (48.0f + 190.0f * expf(-t * 1.7f)) / SR;
        o = v.lp * 1.5f * e + sinf(TAU * v.ph) * 0.38f * e;
    } break;
    case SND_BREAK: {
        float e = expf(-t * 7.5f);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * 0.55f;
        float ring = sinf(TAU * 1180.0f * t) * expf(-t * 15.0f)
                   + sinf(TAU * 1760.0f * t) * expf(-t * 19.0f) * 0.7f
                   + sinf(TAU * 2640.0f * t) * expf(-t * 25.0f) * 0.4f;
        o = v.lp * 0.95f * e + ring * 0.34f;
    } break;
    case SND_HIT: {
        float e = expf(-t * 14.0f);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * 0.62f;
        o = (sinf(TAU * 540.0f * v.f0 * t) * 0.48f
           + sinf(TAU * 1490.0f * v.f0 * t) * 0.34f
           + v.lp * 0.55f) * e;
    } break;
    case SND_KILL: {
        float e = expf(-t * 5.5f);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * (0.02f + 0.30f * expf(-t * 3.0f));
        v.ph += (42.0f + 95.0f * expf(-t * 2.2f)) / SR;
        o = v.lp * 1.55f * e + sinf(TAU * v.ph) * 0.5f * e;
    } break;
    case SND_DEATH: {
        v.ph += (70.0f + 500.0f * expf(-t * 2.1f)) / SR;
        float e = (t * 40.0f < 1 ? t * 40.0f : 1.0f) * expf(-t * 1.5f);
        float saw = fmodf(v.ph, 1.0f) * 2.0f - 1.0f;
        o = saw * 0.5f * e;
    } break;
    case SND_TUNE: {
        static const float sc[5] = { 1.0f, 1.26f, 1.5f, 2.0f, 2.52f };
        int st = (int)(t / 0.10f); if (st > 4) st = 4;
        v.ph += 523.0f * v.f0 * sc[st] / SR;
        float lt = t - st * 0.10f;
        float e = expf(-lt * 7.0f) * (lt * 320.0f < 1 ? lt * 320.0f : 1.0f);
        o = ((fmodf(v.ph, 1.0f) < 0.5f) ? 0.42f : -0.42f) * e;
    } break;
    case SND_SPAWN: {
        v.ph += (300.0f - 160.0f * u) * v.f0 / SR;
        float e = expf(-t * 8.0f);
        float n = noiseF(v.rs);
        v.lp += (n - v.lp) * 0.25f;
        o = (sinf(TAU * v.ph) * 0.4f + v.lp * 0.35f) * e;
    } break;
    case SND_CHERRY: {
        static const float sc[6] = { 1.0f, 1.19f, 1.5f, 1.78f, 2.0f, 2.38f };
        int st = (int)(t / 0.07f); if (st > 5) st = 5;
        v.ph += 660.0f * sc[st] / SR;
        float lt = t - st * 0.07f;
        float e = expf(-lt * 11.0f) * (lt * 400.0f < 1 ? lt * 400.0f : 1.0f);
        o = (sinf(TAU * v.ph) * 0.5f + sinf(TAU * v.ph * 2.0f) * 0.2f) * e;
    } break;
    case SND_EAT: {
        v.ph += (180.0f + 900.0f * u * u) * v.f0 / SR;
        float e = expf(-t * 11.0f) * (t * 400.0f < 1 ? t * 400.0f : 1.0f);
        o = ((fmodf(v.ph, 1.0f) < 0.5f) ? 0.45f : -0.45f) * e;
    } break;
    case SND_TAUNT: {                            // "you missed" -- four jeering blips
        int st = (int)(t / 0.078f); if (st > 3) st = 3;
        float f = (640.0f - st * 78.0f) * v.f0;
        v.ph += (f + sinf(TAU * 21.0f * t) * 46.0f) / SR;
        float lt = t - st * 0.078f;
        float e = expf(-lt * 15.0f) * (lt * 400.0f < 1 ? lt * 400.0f : 1.0f);
        float sq = (fmodf(v.ph, 1.0f) < 0.5f) ? 0.40f : -0.40f;
        o = (sq + sinf(TAU * v.ph * 2.01f) * 0.16f) * e;
    } break;
    }
    v.t += 1.0f / SR;
    if (v.t >= v.dur) v.active = false;
    return o * v.amp;
}

static void renderAudio(short* out, int n)
{
    std::lock_guard<std::mutex> lk(g_audioM);
    if (!g_specInit) specInit();
    float blockE = 0.0f;
    float peak = 0.0f;
    bool musicThisBlock = (g_musPlaying && g_musData != nullptr);
    for (int b = 0; b < NUM_BANDS; b++) g_specEnv[b] = 0.0f;
    for (int i = 0; i < n; i++) {
        float s = 0.0f;
        for (Voice& v : g_voice) if (v.active) s += voiceSample(v);
        s *= 0.52f;

        if (g_musPlaying && g_musData && g_musPos < g_musFrames) {
            int idx = (int)g_musPos;
            float m = g_musData[idx];
            blockE += m * m;
            float am = m < 0 ? -m : m;
            if (am > peak) peak = am;
            s += m * g_musVol;
            g_musPos += 1.0;

            // split the music sample across the band-pass bank
            for (int b = 0; b < NUM_BANDS; b++) {
                g_svfLow[b] += g_svfF[b] * g_svfBand[b];
                float high = m - g_svfLow[b] - g_svfQ * g_svfBand[b];
                g_svfBand[b] += g_svfF[b] * high;
                float a = g_svfBand[b] < 0 ? -g_svfBand[b] : g_svfBand[b];
                if (a > g_specEnv[b]) g_specEnv[b] = a;
            }
        }
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = (short)(s * 32000.0f);
    }
    g_audioClock += (double)n / SR;

    // Fast attack, slow fall -- the classic bouncing graphic-EQ response.
    if (musicThisBlock) {
        for (int b = 0; b < NUM_BANDS; b++) {
            float lvl = g_specEnv[b] * g_svfGain[b] * 1.6f;   // normalised makeup
            if (lvl > 1.0f) lvl = 1.0f;
            if (lvl > g_specBand[b]) g_specBand[b] = lvl;
            else g_specBand[b] += (lvl - g_specBand[b]) * 0.30f;
        }
    } else {
        for (int b = 0; b < NUM_BANDS; b++) {
            g_specBand[b] *= 0.82f;
            g_svfLow[b] = g_svfBand[b] = 0.0f;      // no stale ringing when paused
        }
    }

    if (g_musPlaying && g_musData) {
        blockE /= n;
        float avg = 0.0f;
        for (int i = 0; i < BEAT_HIST; i++) avg += g_beatE[i];
        avg /= BEAT_HIST;
        g_beatE[g_beatEi] = blockE;
        g_beatEi = (g_beatEi + 1) % BEAT_HIST;

        g_visBar[g_visI] = peak;
        g_visI = (g_visI + 1) % VIS_BARS;

        double since = g_audioClock - g_lastBeatT;
        if (avg > 1e-7f && blockE > avg * 1.42f && blockE > 1e-5f && since > 0.19) {
            if (since < 2.0) {
                g_beatGaps[g_beatGapI] = since;
                g_beatGapI = (g_beatGapI + 1) % 8;
                double sum = 0; int cnt = 0;
                for (int i = 0; i < 8; i++) if (g_beatGaps[i] > 0.05) { sum += g_beatGaps[i]; cnt++; }
                if (cnt) g_bpm = (float)(60.0 / (sum / cnt));
            }
            g_lastBeatT = g_audioClock;
            g_beatCount++;
            g_beatPulse = 1.0f;
        }
        g_beatPulse *= 0.965f;
    } else {
        g_beatPulse *= 0.9f;
    }
}

// ------------------------------ device plumbing ----------------------------
static HWAVEOUT    g_wo = nullptr;
static WAVEHDR     g_ahdr[ABUF_COUNT];
static short*      g_adata[ABUF_COUNT];
static bool        g_ainflight[ABUF_COUNT];
static volatile bool g_audioQuit = false;
static std::thread g_audioThread;

static void audioWorker()
{
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = SR;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2;
    wf.nAvgBytesPerSec = SR * 2;
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_wo = nullptr;
        return;
    }
    for (int i = 0; i < ABUF_COUNT; i++) {
        g_adata[i] = new short[ABUF_FRAMES];
        memset(&g_ahdr[i], 0, sizeof(WAVEHDR));
        g_ahdr[i].lpData = (LPSTR)g_adata[i];
        g_ahdr[i].dwBufferLength = ABUF_FRAMES * 2;
        waveOutPrepareHeader(g_wo, &g_ahdr[i], sizeof(WAVEHDR));
        g_ainflight[i] = false;
    }
    while (!g_audioQuit) {
        bool queued = false;
        for (int i = 0; i < ABUF_COUNT; i++) {
            if (g_ainflight[i]) {
                if (g_ahdr[i].dwFlags & WHDR_DONE) g_ainflight[i] = false;
                else continue;
            }
            renderAudio(g_adata[i], ABUF_FRAMES);
            g_ahdr[i].dwBufferLength = ABUF_FRAMES * 2;
            waveOutWrite(g_wo, &g_ahdr[i], sizeof(WAVEHDR));
            g_ainflight[i] = true;
            queued = true;
        }
        if (!queued) Sleep(2);
    }
    waveOutReset(g_wo);
    for (int i = 0; i < ABUF_COUNT; i++) {
        waveOutUnprepareHeader(g_wo, &g_ahdr[i], sizeof(WAVEHDR));
        delete[] g_adata[i];
    }
    waveOutClose(g_wo);
    g_wo = nullptr;
}

// -------------------------- built-in MIDI theme ----------------------------
static const int MUS_STEPS = 64;
static const int MUS_STEPMS = 227;
static const unsigned char MUS_MEL[MUS_STEPS] = {
    69,0,72,0, 76,0,72,0,  74,0,72,0, 69,0, 0,0,
    65,0,69,0, 72,0,69,0,  77,0,74,0, 72,0, 0,0,
    72,0,76,0, 79,0,76,0,  74,0,72,0, 76,0, 0,0,
    71,0,74,0, 79,0,74,0,  71,0,69,0, 67,0, 0,0,
};
static const unsigned char MUS_BAS[MUS_STEPS] = {
    45,0, 0,0, 45,0,52,0,  45,0, 0,0, 52,0,45,0,
    41,0, 0,0, 41,0,48,0,  41,0, 0,0, 48,0,41,0,
    48,0, 0,0, 48,0,55,0,  48,0, 0,0, 55,0,48,0,
    43,0, 0,0, 43,0,50,0,  43,0, 0,0, 50,0,43,0,
};
static HMIDIOUT      g_midi = nullptr;
static bool          g_musicOn = true;          // built-in theme enabled
static volatile bool g_musicQuit = false;
static std::thread   g_musThread;

static void midiMsg(int status, int d1, int d2)
{
    if (g_midi) midiOutShortMsg(g_midi, (DWORD)(status | (d1 << 8) | (d2 << 16)));
}
static void musicWorker()
{
    if (midiOutOpen(&g_midi, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_midi = nullptr;
        return;
    }
    midiMsg(0xC0, 80, 0);
    midiMsg(0xC1, 38, 0);
    midiMsg(0xB0, 7, 42);
    midiMsg(0xB1, 7, 36);
    int step = 0, lastM = 0, lastB = 0;
    while (!g_musicQuit) {
        // the built-in theme steps aside whenever a real track is playing
        if (!g_musicOn || g_musPlaying) {
            if (lastM) { midiMsg(0x80, lastM, 0); lastM = 0; }
            if (lastB) { midiMsg(0x81, lastB, 0); lastB = 0; }
            Sleep(60);
            continue;
        }
        int m = MUS_MEL[step], b = MUS_BAS[step];
        if (m) { if (lastM) midiMsg(0x80, lastM, 0); midiMsg(0x90, m, 40); lastM = m; }
        if (b) { if (lastB) midiMsg(0x81, lastB, 0); midiMsg(0x91, b, 38); lastB = b; }
        for (int t = 0; t < MUS_STEPMS && !g_musicQuit; t += 10) Sleep(10);
        step = (step + 1) % MUS_STEPS;
    }
    if (lastM) midiMsg(0x80, lastM, 0);
    if (lastB) midiMsg(0x81, lastB, 0);
    midiMsg(0xB0, 123, 0); midiMsg(0xB1, 123, 0);
    midiOutClose(g_midi);
    g_midi = nullptr;
}

static void audioStart()
{
    g_audioThread = std::thread(audioWorker);
    g_musThread = std::thread(musicWorker);
}
static void audioStop()
{
    g_musicQuit = true;
    if (g_musThread.joinable()) g_musThread.join();
    g_audioQuit = true;
    if (g_audioThread.joinable()) g_audioThread.join();
}
