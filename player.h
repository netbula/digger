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
//  player.h -- the built-in music player.
//
//  Decoding goes through Media Foundation, which is part of Windows, so every
//  format the OS can play works: mp3, m4a/aac, wma, flac, wav.  A hand-written
//  RIFF reader stays as a fallback for when MF is unavailable.
//
//  The point of decoding ourselves rather than handing the file to the OS is
//  that we keep the samples: the beat detector in audio.h analyses the real
//  signal, which is what the monsters move to.
//
//  The UI is a permanent toolbar across the top -- transport, seek, volume and
//  a playlist drop-down -- so tracks can be changed at any time without
//  interrupting play.
// ---------------------------------------------------------------------------
#pragma once
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "gfx.h"
#include "audio.h"

static const int PL_MAX = 64;
struct PlaylistItem { char path[MAX_PATH]; char name[96]; };
static PlaylistItem g_pl[PL_MAX];
static int  g_plN = 0;
static int  g_plCur = -1;
static bool g_plOpen = false;                    // playlist drop-down visible
static bool g_shuffle = false;                   // play order: false=list, true=random
static unsigned g_shufRng = 0x1234abcd;
static char g_plStatus[160] = "NO TRACK  -  CLICK THE FOLDER OR PRESS O";
static char g_plFormat[32] = "";

// The built-in default track: an original chiptune, generated once and kept
// resident.  It is playlist item 0 with an empty path, and (unlike a real
// Eagles record) it is ours to ship, so it can be the default background music
// legitimately.  It plays through the mixer, so it drives the spectrum too.
static float* g_defaultTrack = nullptr;
static int    g_defaultFrames = 0;
static bool   g_musIsDefault = false;

static void buildDefaultTrack()
{
    if (g_defaultTrack) return;
    const float bpm = 124.0f;
    const float step = 60.0f / bpm / 4.0f;             // one sixteenth note
    const int   STEPS = 128;                           // 8 bars
    const int   frames = (int)(step * STEPS * SR);
    float* buf = (float*)calloc(frames, sizeof(float));
    if (!buf) return;

    // Am - F - C - G, two bars each: a bassline plus an arpeggiated square lead.
    static const int root[8] = { 45, 45, 41, 41, 48, 48, 43, 43 };   // MIDI roots
    static const int triad[8][3] = {
        {57,60,64},{57,60,64},{53,57,60},{53,57,60},
        {60,64,67},{60,64,67},{55,59,62},{55,59,62}
    };
    auto midiHz = [](int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); };
    unsigned ns = 0x2468ace0;

    for (int st = 0; st < STEPS; st++) {
        int bar = (st / 16) % 8;
        int i0 = st * (int)(step * SR);
        int i1 = (st + 1) * (int)(step * SR);
        if (i1 > frames) i1 = frames;

        int leadNote = triad[bar][st % 3] + ((st % 8) >= 4 ? 12 : 0);
        float leadHz = midiHz(leadNote);
        float bassHz = midiHz(root[bar] - 12);
        bool kick = (st % 4) == 0;
        bool hat  = (st % 2) == 1;

        for (int i = i0; i < i1; i++) {
            float lt = (float)(i - i0) / SR;               // time within the step
            float o = 0.0f;
            // square lead with a plucky decay
            float le = expf(-lt * 6.0f);
            o += ((fmodf(leadHz * lt, 1.0f) < 0.5f) ? 0.16f : -0.16f) * le;
            // bass, steadier
            o += ((fmodf(bassHz * lt, 1.0f) < 0.5f) ? 0.20f : -0.20f) * (0.6f + 0.4f * le);
            // kick: pitch-swept sine, strong transient for the beat detector
            if (kick) {
                float kf = 120.0f * expf(-lt * 24.0f) + 45.0f;
                o += sinf(6.2831853f * kf * lt) * expf(-lt * 9.0f) * 0.55f;
            }
            // hat: short filtered noise
            if (hat) {
                float he = expf(-lt * 60.0f);
                o += noiseF(ns) * he * 0.12f;
            }
            if (i >= 0 && i < frames) buf[i] += o;
        }
    }
    // gentle limiter
    for (int i = 0; i < frames; i++) {
        float v = buf[i] * 0.9f;
        if (v > 0.98f) v = 0.98f; if (v < -0.98f) v = -0.98f;
        buf[i] = v;
    }
    g_defaultTrack = buf;
    g_defaultFrames = frames;
}

// ------------------------------ MF decoding --------------------------------
static bool g_mfUp = false;
static bool mfEnsure()
{
    if (g_mfUp) return true;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    g_mfUp = true;
    return true;
}
static void mfShutdown() { if (g_mfUp) { MFShutdown(); g_mfUp = false; } }

// Mixes to mono and resamples to SR.  Returns nullptr if MF cannot open it.
static float* decodeViaMF(const char* path, int& outFrames)
{
    outFrames = 0;
    if (!mfEnsure()) return nullptr;

    wchar_t wpath[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH * 2)) return nullptr;

    IMFSourceReader* rd = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(wpath, nullptr, &rd)) || !rd) return nullptr;
    rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    // Ask for exactly what the mixer wants; MF inserts a resampler when it can.
    int rate = SR, channels = 1;
    IMFMediaType* want = nullptr;
    bool ok = false;
    if (SUCCEEDED(MFCreateMediaType(&want))) {
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        want->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, SR);
        want->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
        ok = SUCCEEDED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                               nullptr, want));
        want->Release();
    }
    if (!ok) {
        // Fall back to plain 16-bit PCM in whatever layout the file has, and do
        // the downmix and resample ourselves further down.
        IMFMediaType* plain = nullptr;
        if (FAILED(MFCreateMediaType(&plain))) { rd->Release(); return nullptr; }
        plain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        plain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        plain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        HRESULT hr = rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                             nullptr, plain);
        plain->Release();
        if (FAILED(hr)) { rd->Release(); return nullptr; }
    }
    IMFMediaType* actual = nullptr;
    if (SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual))
        && actual) {
        UINT32 v = 0;
        if (SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &v)) && v) rate = (int)v;
        if (SUCCEEDED(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &v)) && v) channels = (int)v;
        actual->Release();
    }
    if (rate < 4000 || channels < 1 || channels > 8) { rd->Release(); return nullptr; }

    const long long MAXFRAMES = (long long)rate * 15 * 60;
    long long cap = rate * 8, n = 0;
    float* mono = (float*)malloc((size_t)cap * sizeof(float));
    if (!mono) { rd->Release(); return nullptr; }

    for (;;) {
        DWORD flags = 0;
        IMFSample* samp = nullptr;
        HRESULT hr = rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                                    nullptr, &flags, nullptr, &samp);
        if (FAILED(hr)) { if (samp) samp->Release(); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (samp) samp->Release(); break; }
        if (!samp) continue;

        IMFMediaBuffer* buf = nullptr;
        if (SUCCEEDED(samp->ConvertToContiguousBuffer(&buf)) && buf) {
            BYTE* p = nullptr; DWORD cur = 0;
            if (SUCCEEDED(buf->Lock(&p, nullptr, &cur))) {
                long long frames = cur / (2 * channels);
                if (n + frames > cap) {
                    while (n + frames > cap) cap *= 2;
                    float* bigger = (float*)realloc(mono, (size_t)cap * sizeof(float));
                    if (!bigger) { buf->Unlock(); buf->Release(); samp->Release(); goto done; }
                    mono = bigger;
                }
                const short* s = (const short*)p;
                for (long long i = 0; i < frames; i++) {
                    float acc = 0.0f;
                    for (int c = 0; c < channels; c++) acc += s[i * channels + c] / 32768.0f;
                    mono[n++] = acc / channels;
                }
                buf->Unlock();
            }
            buf->Release();
        }
        samp->Release();
        if (n >= MAXFRAMES) break;
    }
done:
    rd->Release();
    if (n < 1) { free(mono); return nullptr; }

    if (rate != SR) {
        double ratio = (double)SR / rate;
        long long dn = (long long)(n * ratio);
        float* dst = (float*)malloc((size_t)dn * sizeof(float));
        if (!dst) { free(mono); return nullptr; }
        for (long long i = 0; i < dn; i++) {
            double sp = i / ratio;
            long long i0 = (long long)sp, i1 = (i0 + 1 < n) ? i0 + 1 : n - 1;
            float fr = (float)(sp - i0);
            dst[i] = mono[i0] * (1.0f - fr) + mono[i1] * fr;
        }
        free(mono);
        mono = dst; n = dn;
    }
    outFrames = (int)n;
    return mono;
}

// ------------------------- RIFF fallback decoder ---------------------------
static bool readChunkHeader(FILE* f, char id[4], unsigned& size)
{
    if (fread(id, 1, 4, f) != 4) return false;
    if (fread(&size, 4, 1, f) != 1) return false;
    return true;
}
static float* decodeWav(const char* path, int& outFrames)
{
    outFrames = 0;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return nullptr;

    char id[4]; unsigned sz;
    if (!readChunkHeader(f, id, sz) || memcmp(id, "RIFF", 4) != 0) { fclose(f); return nullptr; }
    char wave[4];
    if (fread(wave, 1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) { fclose(f); return nullptr; }

    int channels = 0, rate = 0, bits = 0, fmtTag = 0;
    long dataPos = 0; unsigned dataSize = 0;
    while (readChunkHeader(f, id, sz)) {
        if (memcmp(id, "fmt ", 4) == 0) {
            unsigned char fmt[40] = { 0 };
            unsigned wantB = sz < sizeof(fmt) ? sz : (unsigned)sizeof(fmt);
            if (fread(fmt, 1, wantB, f) != wantB) break;
            fmtTag   = fmt[0] | (fmt[1] << 8);
            channels = fmt[2] | (fmt[3] << 8);
            rate     = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bits     = fmt[14] | (fmt[15] << 8);
            if (fmtTag == 0xFFFE && sz >= 26) fmtTag = fmt[24] | (fmt[25] << 8);
            if (sz > wantB) fseek(f, sz - wantB, SEEK_CUR);
        } else if (memcmp(id, "data", 4) == 0) {
            dataPos = ftell(f); dataSize = sz;
            fseek(f, sz, SEEK_CUR);
        } else fseek(f, sz, SEEK_CUR);
        if (sz & 1) fseek(f, 1, SEEK_CUR);
    }
    if (!dataPos || !dataSize || channels < 1 || channels > 8 || rate < 4000) {
        fclose(f); return nullptr;
    }
    int bytes = bits / 8;
    if (bytes < 1 || bytes > 4) { fclose(f); return nullptr; }

    long long srcFrames = (long long)dataSize / (bytes * channels);
    if (srcFrames < 1) { fclose(f); return nullptr; }
    if (srcFrames > (long long)rate * 15 * 60) srcFrames = (long long)rate * 15 * 60;

    float* src = (float*)malloc((size_t)srcFrames * sizeof(float));
    unsigned char* row = (unsigned char*)malloc((size_t)bytes * channels);
    if (!src || !row) { free(src); free(row); fclose(f); return nullptr; }

    fseek(f, dataPos, SEEK_SET);
    for (long long i = 0; i < srcFrames; i++) {
        if (fread(row, 1, (size_t)bytes * channels, f) != (size_t)bytes * channels) {
            srcFrames = i; break;
        }
        float acc = 0.0f;
        for (int c = 0; c < channels; c++) {
            const unsigned char* p = row + c * bytes;
            float v = 0.0f;
            if (fmtTag == 3 && bytes == 4)      { float fv; memcpy(&fv, p, 4); v = fv; }
            else if (bytes == 1)                 v = ((int)p[0] - 128) / 128.0f;
            else if (bytes == 2)                 v = (float)(short)(p[0] | (p[1] << 8)) / 32768.0f;
            else if (bytes == 3) { int s24 = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
                                   v = (float)(s24 >> 8) / 8388608.0f; }
            else { int s32 = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
                   v = (float)s32 / 2147483648.0f; }
            acc += v;
        }
        src[i] = acc / channels;
    }
    free(row);
    fclose(f);
    if (srcFrames < 1) { free(src); return nullptr; }

    if (rate == SR) { outFrames = (int)srcFrames; return src; }
    double ratio = (double)SR / rate;
    long long dstFrames = (long long)(srcFrames * ratio);
    float* dst = (float*)malloc((size_t)dstFrames * sizeof(float));
    if (!dst) { free(src); return nullptr; }
    for (long long i = 0; i < dstFrames; i++) {
        double sp = i / ratio;
        long long i0 = (long long)sp, i1 = (i0 + 1 < srcFrames) ? i0 + 1 : srcFrames - 1;
        float fr = (float)(sp - i0);
        dst[i] = src[i0] * (1.0f - fr) + src[i1] * fr;
    }
    free(src);
    outFrames = (int)dstFrames;
    return dst;
}

// MF first, RIFF second.  `usedMF` reports which path produced the samples.
static float* decodeAudio(const char* path, int& outFrames, bool* usedMF = nullptr)
{
    float* d = decodeViaMF(path, outFrames);
    if (usedMF) *usedMF = (d != nullptr);
    if (d) return d;
    return decodeWav(path, outFrames);
}

// ------------------------------ transport ----------------------------------
static void musicFreeCurrent()
{
    float* old = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_audioM);
        g_musPlaying = false;
        // never free() the resident default buffer -- it is reused, not owned here
        if (!g_musIsDefault) old = g_musData;
        g_musData = nullptr; g_musFrames = 0; g_musPos = 0; g_musIsDefault = false;
    }
    if (old) free(old);
}
// The built-in theme is NOT a playlist entry -- it is what plays when the
// playlist is empty.  So an untouched game shows an empty playlist, and the
// first track you add takes over.
static bool musicPlayDefault()
{
    buildDefaultTrack();
    if (!g_defaultTrack) return false;
    musicFreeCurrent();
    {
        std::lock_guard<std::mutex> lk(g_audioM);
        g_musData = g_defaultTrack; g_musFrames = g_defaultFrames;
        g_musPos = 0; g_musPlaying = true; g_musIsDefault = true;
        memset(g_beatE, 0, sizeof(g_beatE));
        g_bpm = 0.0f;
    }
    g_plCur = -1;
    strcpy_s(g_plFormat, "CHIPTUNE");
    // Worded so it cannot be mistaken for a playlist entry -- it is not one.
    strcpy_s(g_plStatus, "BUILT-IN THEME");
    return true;
}
static bool musicPlayIndex(int idx)
{
    if (idx < 0 || idx >= g_plN) return false;
    musicFreeCurrent();
    int frames = 0;
    bool viaMF = false;
    float* data = decodeAudio(g_pl[idx].path, frames, &viaMF);
    if (!data) {
        g_plCur = idx;
        sprintf_s(g_plStatus, "CANNOT DECODE  %s", g_pl[idx].name);
        strcpy_s(g_plFormat, "");
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(g_audioM);
        g_musData = data; g_musFrames = frames; g_musPos = 0; g_musPlaying = true;
        g_musIsDefault = false;
        memset(g_beatE, 0, sizeof(g_beatE));
        g_bpm = 0.0f;
    }
    g_plCur = idx;
    const char* ext = strrchr(g_pl[idx].name, '.');
    sprintf_s(g_plFormat, "%s %s", ext ? ext + 1 : "AUDIO", viaMF ? "/ MF" : "/ RIFF");
    for (char* p = g_plFormat; *p; ++p) *p = (char)toupper((unsigned char)*p);
    sprintf_s(g_plStatus, "%s", g_pl[idx].name);
    return true;
}
static void musicTogglePlay()
{
    if (!g_musData) {
        if (g_plN) musicPlayIndex(g_plCur < 0 ? 0 : g_plCur);
        else musicPlayDefault();
        return;
    }
    std::lock_guard<std::mutex> lk(g_audioM);
    g_musPlaying = !g_musPlaying;
}
static void musicNext(int delta)
{
    // nothing in the list: the built-in theme is all there is to play
    if (!g_plN) { if (!(g_musPlaying && g_musIsDefault)) musicPlayDefault(); return; }
    if (g_shuffle && g_plN > 1) {
        // pick a different track at random
        int idx;
        do {
            g_shufRng = g_shufRng * 1664525u + 1013904223u;
            idx = (int)((g_shufRng >> 8) % (unsigned)g_plN);
        } while (idx == g_plCur);
        musicPlayIndex(idx);
        return;
    }
    int idx = (g_plCur < 0 ? 0 : g_plCur) + delta;
    while (idx < 0) idx += g_plN;
    idx %= g_plN;
    musicPlayIndex(idx);
}
static void musicToggleShuffle() { g_shuffle = !g_shuffle; }
static void musicSeekFrac(float f)
{
    if (!g_musData || g_musFrames <= 0) return;
    if (f < 0) f = 0; if (f > 1) f = 1;
    std::lock_guard<std::mutex> lk(g_audioM);
    g_musPos = (double)f * g_musFrames;
}
static void musicSetVol(float v)
{
    if (v < 0) v = 0; if (v > 1) v = 1;
    std::lock_guard<std::mutex> lk(g_audioM);
    g_musVol = v;
}
static float musicFrac()
{
    if (!g_musData || g_musFrames <= 0) return 0.0f;
    return (float)(g_musPos / g_musFrames);
}
static void musicTick()
{
    if (!g_musData) return;
    bool ended = false, loop = false;
    {
        std::lock_guard<std::mutex> lk(g_audioM);
        if (g_musPlaying && g_musPos >= g_musFrames) {
            ended = true;
            // a lone track (the built-in default, or a single file) loops rather
            // than falling silent
            if (g_plN <= 1) { g_musPos = 0; loop = true; }
        }
    }
    if (ended && !loop) musicNext(1);             // roll into the next one
}
static bool musicActive() { return g_musPlaying && g_musData != nullptr; }
// True only while the built-in theme is what you are hearing.
static bool musicOnDefault() { return g_musPlaying && g_musIsDefault; }

// ------------------------------ playlist io --------------------------------
static void musicAddPath(const char* path)
{
    if (g_plN >= PL_MAX) return;
    strcpy_s(g_pl[g_plN].path, path);
    const char* base = strrchr(path, '\\');
    strncpy_s(g_pl[g_plN].name, base ? base + 1 : path, _TRUNCATE);
    g_plN++;
}
static void musicOpenDialog(HWND owner)
{
    static char buf[64 * 1024];
    buf[0] = 0;
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter =
        "Audio files\0*.mp3;*.wav;*.m4a;*.aac;*.wma;*.flac;*.mp4;*.wmv\0"
        "MP3\0*.mp3\0WAV\0*.wav\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.lpstrTitle = "Add tracks to the playlist";
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST |
                OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn)) return;

    const char* dir = buf;
    const char* p = buf + strlen(buf) + 1;
    int before = g_plN;
    if (*p == 0) musicAddPath(buf);
    else {
        char full[MAX_PATH];
        while (*p && g_plN < PL_MAX) {
            sprintf_s(full, "%s\\%s", dir, p);
            musicAddPath(full);
            p += strlen(p) + 1;
        }
    }
    // The first real track added takes over from the built-in theme.
    if (g_plN > before && (!musicActive() || musicOnDefault())) musicPlayIndex(before);
    else if (g_plN > before) sprintf_s(g_plStatus, "%d IN PLAYLIST", g_plN);
}
// Called once at startup: take any files from the command line, and fall back to
// the built-in theme when none were given.  The theme never occupies a playlist
// slot, so an untouched game shows an empty playlist.
static void musicBootstrap(const char* cmd)
{
    buildDefaultTrack();
    if (cmd) {
        char tok[MAX_PATH];
        int n = 0;
        bool quoted = false;
        for (const char* p = cmd;; ++p) {
            if (*p == '"') { quoted = !quoted; continue; }
            bool end = (*p == 0) || (!quoted && (*p == ' ' || *p == '\t'));
            if (!end) { if (n < MAX_PATH - 1) tok[n++] = *p; }
            else if (n) {
                tok[n] = 0; n = 0;
                if (tok[0] != '-' && tok[0] != '/') musicAddPath(tok);
            }
            if (*p == 0) break;
        }
    }
    // supplied tracks take over; otherwise the built-in theme plays
    if (g_plN > 0) musicPlayIndex(0);
    else musicPlayDefault();
}

// ------------------------------ the toolbar --------------------------------
// Always on screen, never pauses the game.  Layout is in logical coordinates
// and hit testing reuses the same numbers.
static const float TB_Y = TOOLH * 0.5f;
static const float TB_BTN_R = 15.0f;
static const float TB_PREV = 28.0f, TB_PLAY = 64.0f, TB_NEXT = 100.0f,
                   TB_SHUF = 136.0f, TB_OPEN = 172.0f, TB_LIST = 208.0f;
static const float TB_NAME_X = 234.0f;
static const float TB_TIME_X = 440.0f;           // fixed slot, clear of the name
static const float TB_SEEK_X0 = 560.0f, TB_SEEK_X1 = 800.0f, TB_SEEK_H = 8.0f;
static const float TB_VOL_X0 = 832.0f, TB_VOL_X1 = 928.0f;
static const float TB_SPEC_X = 952.0f;           // retro bar spectrum origin
static const float TB_SPEC_BW = 10.0f;           // per-bar pitch
static const float TB_BPM_X = 1160.0f;           // small numeric readout, right
static const float TB_ROW_H = 26.0f;
static const int   TB_ROWS = 8;

// The retro graphic-EQ display: stacked LED segments per band, green low,
// amber mid, red top, each bar capped by a slowly falling peak marker.
static float g_specPeak[NUM_BANDS] = { 0 };
static void drawSpectrum()
{
    const int SEG = 8;
    const float top = 7.0f, bot = TOOLH - 6.0f;
    const float segH = (bot - top) / SEG;
    for (int b = 0; b < NUM_BANDS; b++) {
        float lvl = g_specBand[b];
        if (lvl > 1.0f) lvl = 1.0f;
        if (lvl > g_specPeak[b]) g_specPeak[b] = lvl;
        else g_specPeak[b] = g_specPeak[b] * 0.92f + lvl * 0.08f;
        float x0 = TB_SPEC_X + b * TB_SPEC_BW;
        float x1 = x0 + TB_SPEC_BW - 3.0f;
        int lit = (int)(lvl * SEG + 0.5f);
        for (int s = 0; s < SEG; s++) {
            float sy1 = bot - s * segH, sy0 = sy1 - segH + 1.5f;
            float f = (float)s / (SEG - 1);
            uint32_t col = (f < 0.5f) ? mixc(0x18C048, 0xE0D820, f * 2.0f)
                                      : mixc(0xE0D820, 0xF03018, (f - 0.5f) * 2.0f);
            if (s < lit) fillRoundRect(x0, sy0, x1, sy1, 1.5f, col, 0.96f);
            else         fillRoundRect(x0, sy0, x1, sy1, 1.5f, 0x14202C, 0.55f);
        }
        int pk = (int)(g_specPeak[b] * SEG + 0.5f);
        if (pk > 0 && pk <= SEG) {
            float py = bot - (pk - 1) * segH - segH + 1.0f;
            fillRoundRect(x0, py - 1.0f, x1, py + 1.5f, 1.0f, 0xFFFFFF, 0.85f);
        }
    }
}

static void drawToolbar()
{
    fillRectV(0, 0, SCRW, TOOLH, 0x232E40, 0x141C28);
    fillRect(0, TOOLH - 2, SCRW, 2, 0x0A0E14);

    struct { float x; int kind; } btn[6] = {
        { TB_PREV, 0 }, { TB_PLAY, 1 }, { TB_NEXT, 2 },
        { TB_SHUF, 5 }, { TB_OPEN, 3 }, { TB_LIST, 4 }
    };
    for (int i = 0; i < 6; i++) {
        float cx = btn[i].x, cy = TB_Y;
        bool on = (btn[i].kind == 4 && g_plOpen) || (btn[i].kind == 5 && g_shuffle);
        fillCircle(cx, cy, TB_BTN_R, on ? 0x2E86C8 : 0x33445E);
        fillCircle(cx, cy - 1.0f, TB_BTN_R - 1.5f, on ? 0x49A6E8 : 0x44607F);
        switch (btn[i].kind) {
        case 0:
            fillTri(cx + 5, cy - 6, cx + 5, cy + 6, cx - 3, cy, 0xE8F4FF);
            fillRect((int)(cx - 6), (int)(cy - 6), 2, 12, 0xE8F4FF);
            break;
        case 1:
            if (musicActive()) {
                fillRect((int)(cx - 5), (int)(cy - 6), 3, 12, 0xE8F4FF);
                fillRect((int)(cx + 2), (int)(cy - 6), 3, 12, 0xE8F4FF);
            } else fillTri(cx - 4, cy - 7, cx - 4, cy + 7, cx + 7, cy, 0xE8F4FF);
            break;
        case 2:
            fillTri(cx - 5, cy - 6, cx - 5, cy + 6, cx + 3, cy, 0xE8F4FF);
            fillRect((int)(cx + 4), (int)(cy - 6), 2, 12, 0xE8F4FF);
            break;
        case 3:
            fillRoundRect(cx - 7, cy - 4, cx + 7, cy + 6, 2.0f, 0xE8C060);
            fillRoundRect(cx - 7, cy - 7, cx - 1, cy - 3, 1.0f, 0xE8C060);
            break;
        case 4:
            for (int r = 0; r < 3; r++) {
                fillCircle(cx - 6, cy - 5 + r * 5.0f, 1.5f, 0xE8F4FF);
                fillRect((int)(cx - 2), (int)(cy - 6 + r * 5.0f), 9, 2, 0xE8F4FF);
            }
            break;
        case 5: {                                 // shuffle: two crossing arrows
            uint32_t c = g_shuffle ? 0xFFFFFF : 0xC8D4E4;
            strokeLine(cx - 8, cy - 5, cx + 8, cy + 5, 2.0f, c, 0.95f);
            strokeLine(cx - 8, cy + 5, cx + 8, cy - 5, 2.0f, c, 0.95f);
            fillTri(cx + 8, cy + 5, cx + 3, cy + 6, cx + 6, cy + 1, c);
            fillTri(cx + 8, cy - 5, cx + 3, cy - 6, cx + 6, cy - 1, c);
        } break;
        }
    }
    // seek bar
    fillRoundRect(TB_SEEK_X0, TB_Y - TB_SEEK_H * 0.5f, TB_SEEK_X1, TB_Y + TB_SEEK_H * 0.5f,
                  4.0f, 0x0C1420);
    float fr = musicFrac();
    if (fr > 0.0f)
        fillRoundRectV(TB_SEEK_X0, TB_Y - TB_SEEK_H * 0.5f,
                       TB_SEEK_X0 + (TB_SEEK_X1 - TB_SEEK_X0) * fr, TB_Y + TB_SEEK_H * 0.5f,
                       4.0f, 0x7CE0FF, 0x2070C0);
    fillCircle(TB_SEEK_X0 + (TB_SEEK_X1 - TB_SEEK_X0) * fr, TB_Y, 6.5f,
               g_musData ? 0xFFFFFF : 0x60707F);
    // volume
    fillRoundRect(TB_VOL_X0, TB_Y - 3.5f, TB_VOL_X1, TB_Y + 3.5f, 3.5f, 0x0C1420);
    fillRoundRectV(TB_VOL_X0, TB_Y - 3.5f,
                   TB_VOL_X0 + (TB_VOL_X1 - TB_VOL_X0) * g_musVol, TB_Y + 3.5f,
                   3.5f, 0xA0FFC0, 0x209060);
    fillCircle(TB_VOL_X0 + (TB_VOL_X1 - TB_VOL_X0) * g_musVol, TB_Y, 6.0f, 0xFFFFFF);
    // retro bar spectrum, flashed a touch brighter on each detected beat
    drawSpectrum();
    if (g_beatPulse > 0.05f) {
        float bp = g_beatPulse;
        fillRect((int)TB_SPEC_X, (int)(TOOLH - 4), (int)(NUM_BANDS * TB_SPEC_BW - 3), 2,
                 mixc(0x50182C, 0xFF80B0, bp));
    }

    if (g_plOpen && g_plN == 0) {
        // an empty list is the normal state while the built-in theme plays
        fillRoundRectV(TB_NAME_X - 12.0f, TOOLH + 2.0f, TB_NAME_X + 460.0f,
                       TOOLH + 40.0f, 8.0f, 0x1C2636, 0x0E1622, 0.97f);
    }
    if (g_plOpen && g_plN) {
        int rows = g_plN < TB_ROWS ? g_plN : TB_ROWS;
        float h = rows * TB_ROW_H + 8.0f;
        fillRoundRectV(TB_NAME_X - 12.0f, TOOLH + 2.0f, TB_NAME_X + 460.0f, TOOLH + h,
                       8.0f, 0x1C2636, 0x0E1622, 0.97f);
        int first = 0;
        if (g_plCur >= TB_ROWS) first = g_plCur - TB_ROWS + 1;
        for (int i = 0; i < rows && first + i < g_plN; i++) {
            float y = TOOLH + 6.0f + i * TB_ROW_H;
            if (first + i == g_plCur) {
                fillRoundRect(TB_NAME_X - 6.0f, y, TB_NAME_X + 454.0f, y + TB_ROW_H - 4.0f,
                              5.0f, 0x2A5C86, 0.95f);
                fillCircle(TB_NAME_X + 4.0f, y + (TB_ROW_H - 4.0f) * 0.5f, 4.0f, 0x7CE0FF);
            }
        }
    }
}

// Returns true when the toolbar consumed the click.  `openFiles` is set when
// the caller needs to raise the file dialog.
static bool toolbarClick(float lx, float ly, bool drag, bool& openFiles)
{
    openFiles = false;
    if (g_plOpen && g_plN && ly > TOOLH && ly < TOOLH + TB_ROWS * TB_ROW_H + 8.0f &&
        lx > TB_NAME_X - 12.0f && lx < TB_NAME_X + 460.0f && !drag) {
        int row = (int)((ly - TOOLH - 6.0f) / TB_ROW_H);
        int first = 0;
        if (g_plCur >= TB_ROWS) first = g_plCur - TB_ROWS + 1;
        if (row >= 0 && row < TB_ROWS && first + row < g_plN) musicPlayIndex(first + row);
        return true;
    }
    if (ly > TOOLH) return false;

    if (lx >= TB_SEEK_X0 - 10.0f && lx <= TB_SEEK_X1 + 10.0f) {
        musicSeekFrac((lx - TB_SEEK_X0) / (TB_SEEK_X1 - TB_SEEK_X0));
        return true;
    }
    if (lx >= TB_VOL_X0 - 10.0f && lx <= TB_VOL_X1 + 10.0f) {
        musicSetVol((lx - TB_VOL_X0) / (TB_VOL_X1 - TB_VOL_X0));
        return true;
    }
    if (drag) return true;

    const float bx[6] = { TB_PREV, TB_PLAY, TB_NEXT, TB_SHUF, TB_OPEN, TB_LIST };
    for (int i = 0; i < 6; i++) {
        float dx = lx - bx[i], dy = ly - TB_Y;
        if (dx * dx + dy * dy <= TB_BTN_R * TB_BTN_R * 1.6f) {
            if (i == 0) musicNext(-1);
            else if (i == 1) musicTogglePlay();
            else if (i == 2) musicNext(1);
            else if (i == 3) musicToggleShuffle();
            else if (i == 4) openFiles = true;
            else g_plOpen = !g_plOpen;
            return true;
        }
    }
    return true;                                  // clicks on the bar never reach the game
}
