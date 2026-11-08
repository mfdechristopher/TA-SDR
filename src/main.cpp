// TA-SDR - self-contained Win32 RTL-SDR receiver
// Modes: AM, SAM, USB, LSB, CW, NFM, WFM(stereo). Full RTL-SDR tuning range incl. HF.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include <complex>
#include "dsp.h"
#include "demo.h"

using namespace dsp;

// ------------------------------------------------------------------ tables
struct ModeInfo { const char* name; double bwMin, bwMax, bwStep, bwDef; double stepDef; };
static const ModeInfo MODES[M_COUNT] = {
    { "AM",  2000, 20000, 100, 8000, 1000 },   { "SAM", 2000, 20000, 100, 8000, 1000 },
    { "USB", 1000, 6000, 50, 2800, 100 },      { "LSB", 1000, 6000, 50, 2800, 100 },
    { "CW",  50, 1000, 10, 300, 10 },          { "NFM", 4000, 50000, 500, 12500, 5000 },
    { "WFM", 100000, 270000, 5000, 200000, 100000 } };

struct Preset { const char* name; double f; int mode; };
static const Preset DEFAULTS[] = {
    { "-- Presets (ham bands + more) --", 0, 0 },
    { "Ham 160 m  1.900 LSB", 1900000, M_LSB },   { "Ham 80 m  3.850 LSB", 3850000, M_LSB },
    { "Ham 80 m CW  3.550", 3550000, M_CW },      { "Ham 60 m  5.3585 USB", 5358500, M_USB },
    { "Ham 40 m  7.200 LSB", 7200000, M_LSB },    { "Ham 40 m CW  7.030", 7030000, M_CW },
    { "Ham 30 m CW  10.120", 10120000, M_CW },    { "Ham 20 m  14.250 USB", 14250000, M_USB },
    { "Ham 20 m CW  14.030", 14030000, M_CW },    { "Ham 17 m  18.130 USB", 18130000, M_USB },
    { "Ham 15 m  21.300 USB", 21300000, M_USB },  { "Ham 15 m CW  21.030", 21030000, M_CW },
    { "Ham 12 m  24.950 USB", 24950000, M_USB },  { "Ham 10 m  28.400 USB", 28400000, M_USB },
    { "Ham 10 m  29.600 FM", 29600000, M_NFM },   { "Ham 6 m  50.125 USB", 50125000, M_USB },
    { "Ham 6 m  52.525 FM", 52525000, M_NFM },    { "Ham 2 m  144.200 USB", 144200000, M_USB },
    { "Ham 2 m  146.520 FM", 146520000, M_NFM },  { "Ham 2 m  145.825 APRS/ISS", 145825000, M_NFM },
    { "Ham 1.25 m  223.500 FM", 223500000, M_NFM }, { "Ham 70 cm  432.100 USB", 432100000, M_USB },
    { "Ham 70 cm  446.000 FM", 446000000, M_NFM }, { "Ham 33 cm  927.500 FM", 927500000, M_NFM },
    { "Ham 23 cm  1294.500 FM", 1294500000, M_NFM },
    { "AM broadcast  1000 kHz", 1000000, M_AM },  { "WWV  5 MHz", 5000000, M_AM },
    { "WWV  10 MHz", 10000000, M_AM },            { "WWV  15 MHz", 15000000, M_AM },
    { "CB ch 19  27.185 AM", 27185000, M_AM },    { "FM broadcast  98.1 MHz", 98100000, M_WFM },
    { "Airband  121.500 AM", 121500000, M_AM },   { "Marine ch 16  156.800 FM", 156800000, M_NFM },
    { "NOAA weather  162.550", 162550000, M_NFM }, { "GMRS/FRS  462.5625", 462562500, M_NFM },
    { "ADS-B  1090 MHz (spectrum)", 1090000000, M_AM },
};
static const int NDEFAULTS = sizeof(DEFAULTS) / sizeof(DEFAULTS[0]);

// user-editable preset list (index i here == Band combo item i+1)
struct UPreset { std::string name; double f; int mode; double bw; };   // bw 0 = mode default
static std::vector<UPreset> presets;

struct Band { double lo, hi; const char* name; };
static const Band BANDS[] = {
    { 530e3, 1710e3, "AM broadcast" }, { 1.8e6, 2.0e6, "160 m amateur" }, { 3.5e6, 4.0e6, "80 m amateur" },
    { 5.06e6, 5.45e6, "60 m amateur" }, { 7.0e6, 7.3e6, "40 m amateur" }, { 10.1e6, 10.15e6, "30 m amateur" },
    { 14.0e6, 14.35e6, "20 m amateur" }, { 18.068e6, 18.168e6, "17 m amateur" }, { 21.0e6, 21.45e6, "15 m amateur" },
    { 24.89e6, 24.99e6, "12 m amateur" }, { 26.965e6, 27.405e6, "CB (11 m)" }, { 28.0e6, 29.7e6, "10 m amateur" },
    { 50e6, 54e6, "6 m amateur" }, { 87.5e6, 108e6, "FM broadcast" }, { 108e6, 118e6, "Aero navigation" },
    { 118e6, 137e6, "Airband" }, { 144e6, 148e6, "2 m amateur" }, { 156e6, 162.025e6, "Marine VHF" },
    { 162.4e6, 162.55e6, "NOAA weather" }, { 222e6, 225e6, "1.25 m amateur" }, { 420e6, 450e6, "70 cm amateur" },
    { 462e6, 468e6, "FRS/GMRS" }, { 902e6, 928e6, "33 cm amateur" }, { 1090e6, 1090.1e6, "ADS-B" },
    { 1240e6, 1300e6, "23 cm amateur" } };
static const char* bandName(double f) {
    for (auto& b : BANDS) if (f >= b.lo && f <= b.hi) return b.name;
    return "";
}

static const double STEPS[] = { 1, 10, 50, 100, 500, 1000, 2500, 5000, 6250, 8333, 9000, 10000, 12500, 25000, 100000, 200000, 1000000 };
static const int NSTEPS = sizeof(STEPS) / sizeof(STEPS[0]);

// ------------------------------------------------------------------ IDs
enum {
    IDC_MODE = 100, IDC_BW, IDC_STEP, IDC_BAND, IDC_VOL, IDC_SQ, IDC_TAGC, IDC_GAIN, IDC_RAGC, IDC_BIAS, IDC_NB,
    IDC_DEEMPH, IDC_STEREO, IDC_AGC, IDC_PITCH, IDC_DEV, IDC_START, IDC_RATE, IDC_PPM, IDC_HF, IDC_ZOOM,
    IDC_REF, IDC_RNG, IDC_GO, IDC_GOBTN, IDC_RECA, IDC_RECI, IDC_PADD, IDC_PEDIT, IDC_PDEL, IDC_PRESET
};
#define WM_DEVEND (WM_APP + 1)
#define SPEC_N 16384

// ------------------------------------------------------------------ audio output (waveOut)
struct AudioOut {
    static const int NB = 6, FR = 1024, RINGF = 32768;
    HWAVEOUT h = 0; WAVEHDR hdr[NB]; short buf[NB][FR * 2];
    HANDLE thread = 0; volatile bool run = false; bool ok = false;
    CRITICAL_SECTION cs; std::vector<short> ring; size_t rd = 0, wr = 0, cnt = 0;
    AudioOut() { InitializeCriticalSection(&cs); ring.assign(RINGF * 2, 0); }
    size_t fill() { EnterCriticalSection(&cs); size_t c = cnt; LeaveCriticalSection(&cs); return c; }
    void push(const float* a, size_t frames) {
        EnterCriticalSection(&cs);
        for (size_t k = 0; k < frames * 2; k++) {
            float v = a[k]; if (v > 1) v = 1; else if (v < -1) v = -1;
            ring[wr * 2 + (k & 1)] = (short)(v * 32767);
            if (k & 1) { wr = (wr + 1) % RINGF; if (cnt < RINGF) cnt++; else rd = (rd + 1) % RINGF; }
        }
        LeaveCriticalSection(&cs);
    }
    void pull(short* dst, int frames) {
        EnterCriticalSection(&cs);
        for (int k = 0; k < frames; k++) {
            if (cnt) { dst[2 * k] = ring[rd * 2]; dst[2 * k + 1] = ring[rd * 2 + 1]; rd = (rd + 1) % RINGF; cnt--; }
            else { dst[2 * k] = dst[2 * k + 1] = 0; }
        }
        LeaveCriticalSection(&cs);
    }
    static DWORD WINAPI Thread(LPVOID p) {
        AudioOut* a = (AudioOut*)p;
        while (a->run) {
            for (int i = 0; i < NB; i++) if (a->hdr[i].dwFlags & WHDR_DONE) {
                a->pull(a->buf[i], FR);
                a->hdr[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(a->h, &a->hdr[i], sizeof(WAVEHDR));
            }
            Sleep(4);
        }
        return 0;
    }
    bool open() {
        WAVEFORMATEX w = {}; w.wFormatTag = WAVE_FORMAT_PCM; w.nChannels = 2; w.nSamplesPerSec = 48000;
        w.wBitsPerSample = 16; w.nBlockAlign = 4; w.nAvgBytesPerSec = 48000 * 4;
        if (waveOutOpen(&h, WAVE_MAPPER, &w, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { ok = false; return false; }
        for (int i = 0; i < NB; i++) {
            memset(&hdr[i], 0, sizeof(WAVEHDR)); memset(buf[i], 0, sizeof(buf[i]));
            hdr[i].lpData = (LPSTR)buf[i]; hdr[i].dwBufferLength = FR * 4;
            waveOutPrepareHeader(h, &hdr[i], sizeof(WAVEHDR));
            hdr[i].dwFlags |= WHDR_DONE;
        }
        run = true; ok = true; thread = CreateThread(0, 0, Thread, this, 0, 0);
        SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
        return true;
    }
    void close() {
        if (!ok) return;
        run = false; WaitForSingleObject(thread, 2000); CloseHandle(thread);
        waveOutReset(h);
        for (int i = 0; i < NB; i++) waveOutUnprepareHeader(h, &hdr[i], sizeof(WAVEHDR));
        waveOutClose(h); ok = false;
    }
};

// ------------------------------------------------------------------ application state
struct G {
    HINSTANCE hi; HWND hwnd; HFONT font, fontBig, fontSm; int dpi = 96;
    HWND cMode, cBw, cBwLbl, cStep, cBand, cVol, cSq, cSqLbl, cTagc, cGain, cGainLbl, cRagc, cBias, cNb, cDeemph, cStereo,
        cAgc, cPitch, cPitchLbl, cDev, cStart, cRate, cPpm, cHf, cZoom, cRef, cRng, cGo, cGoBtn, cRecA, cRecI;
    // radio
    double freq = 7100000, fs = 2048000, hwCenter = 0, hwErr = 0; bool hwValid = false;
    int mode = M_LSB, stepIdx = 5, zoomIdx = 0; double bw[M_COUNT]; double pitch = 700;
    int gainIdx = 20, agcMode = 2, ppm = 0, hfMode = 0, volume = 50, squelch = 0;
    bool tunerAgc = false, rtlAgc = false, bias = false, nb = false, deemph = true, stereo = true;
    double refDb = -10, rngDb = 90, viewCenter = 7100000;
    // device
    Device* dev = nullptr; bool running = false; volatile LONG stop = 0; std::vector<DevEntry> entries; std::vector<double> rates;
    double minF = 1000, maxF = 2200e6, wantFreq = 7100000; std::string devName, status, lastDev;
    // dsp
    Receiver rx; CRITICAL_SECTION rxCs; AudioOut audio; std::vector<float> audTmp;
    Spectrum spec; std::vector<float> snap, dbNew, dbAvg, colDb;
    float meterSm = -120;
    // recording
    FILE* recA = nullptr; FILE* recI = nullptr; unsigned recAFrames = 0; char recAName[MAX_PATH] = "", recIName[MAX_PATH] = "";
    // layout / drawing
    int PH = 0, plotW = 0, plotH = 0, specH = 0, axisH = 0; HDC plotDC = 0, wfDC = 0; HBITMAP plotBmp = 0, wfBmp = 0, wfOld = 0, plotOld = 0;
    uint32_t* wfBits = 0; uint32_t pal[256]; bool dragging = false; RECT rcFreq, rcMeter, rcStatus;
    double viewLo = 0, viewSpan = 1;
} g;

static int S(int v) { return MulDiv(v, g.dpi, 96); }
static HINSTANCE hInst;

// forward declarations
static void applyRx();
static void retuneHardware();
static void setFreq(double f, bool remember = true);
static void setMode(int m, bool keepBw = true);
static void startRadio();
static void stopRadio();
static void updateLabels();
static void adaptUiToDevice();

// ------------------------------------------------------------------ small helpers
static void setStatus(const std::string& s) { g.status = s; if (g.hwnd) InvalidateRect(g.hwnd, &g.rcStatus, FALSE); }
static std::string appDir() {
    char p[MAX_PATH]; GetModuleFileNameA(0, p, MAX_PATH);
    char* sl = strrchr(p, '\\'); if (sl) *sl = 0; return p;
}
static std::string fmtFreq(double f) {
    char b[64]; long long v = llround(f);
    snprintf(b, sizeof b, "%lld.%03lld.%03lld", v / 1000000, (v / 1000) % 1000, v % 1000); return b;
}
static void timestamp(char* out, size_t n) {
    SYSTEMTIME t; GetLocalTime(&t);
    snprintf(out, n, "%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

// ------------------------------------------------------------------ WAV recording
static void wavHeader(FILE* f, unsigned frames) {
    unsigned data = frames * 4, riff = 36 + data, sr = 48000, br = 48000 * 4, fmt = 16; unsigned short pcm = 1, ch = 2, ba = 4, bits = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}
static std::string recDir() { std::string d = appDir() + "\\recordings"; CreateDirectoryA(d.c_str(), 0); return d; }

// ------------------------------------------------------------------ sample sink (called on the device's own thread)
static void sampleSink(const void* data, size_t count, int fmt) {
    if (g.stop) return;
    double fill = (double)g.audio.fill(), target = 4096;
    double e = (fill - target) / target; if (e > 1) e = 1; else if (e < -1) e = -1;
    EnterCriticalSection(&g.rxCs);
    g.audTmp.clear();
    g.rx.setRateAdjust(0.002 * e);
    if (fmt == FMT_U8) g.rx.process((const uint8_t*)data, count * 2, g.audTmp);
    else if (fmt == FMT_I8) g.rx.processI8((const int8_t*)data, count * 2, g.audTmp);
    else g.rx.processF((const float*)data, count, g.audTmp);
    LeaveCriticalSection(&g.rxCs);
    size_t frames = g.audTmp.size() / 2;
    if (frames) {
        if (g.audio.ok) g.audio.push(g.audTmp.data(), frames);
        if (g.recA) {
            std::vector<short> t(g.audTmp.size());
            for (size_t k = 0; k < t.size(); k++) { float v = g.audTmp[k]; v = v > 1 ? 1 : v < -1 ? -1 : v; t[k] = (short)(v * 32767); }
            fwrite(t.data(), 2, t.size(), g.recA); g.recAFrames += (unsigned)frames;
        }
    }
    if (g.recI) fwrite(data, fmt == FMT_F32 ? 8 : 2, count, g.recI);
}

// ------------------------------------------------------------------ radio control
static void applyRx() {
    RxConfig c;
    c.fs = g.fs; c.mode = g.mode; c.bw = g.bw[g.mode]; c.cwPitch = g.pitch; c.shiftHz = g.freq - (g.hwCenter + g.hwErr);
    c.agcMode = g.agcMode; c.deemph = g.deemph; c.stereo = g.stereo; c.nb = g.nb;
    c.squelchDb = g.squelch == 0 ? -200 : -100 + g.squelch * 0.9; c.volume = g.volume / 100.0;
    EnterCriticalSection(&g.rxCs); g.rx.configure(c); LeaveCriticalSection(&g.rxCs);
}
// keep the signal off the dongle's DC spike: centre the hardware a quarter of the span away
static double pickCenter(double f) {
    double c = f + g.fs / 4;
    if (g.dev && !g.dev->validCenter(c)) { c = f - g.fs / 4; if (!g.dev->validCenter(c)) c = f; }
    return floor(c + 0.5);
}
static void retuneHardware() {
    g.hwCenter = pickCenter(g.freq); g.hwErr = 0;
    if (g.dev) {
        if (!g.dev->setCenter(g.hwCenter)) setStatus("The receiver could not tune to that frequency");
        g.hwErr = g.dev->centerError();
    }
    g.hwValid = true; g.viewCenter = g.freq;
}
static void ensureView() {
    g.viewSpan = g.fs / (1 << g.zoomIdx);
    double lo = g.hwCenter - g.fs / 2 + g.viewSpan / 2, hi = g.hwCenter + g.fs / 2 - g.viewSpan / 2;
    if (fabs(g.freq - g.viewCenter) > 0.42 * g.viewSpan) g.viewCenter = g.freq;
    g.viewCenter = std::max(lo, std::min(hi, g.viewCenter));
    g.viewLo = g.viewCenter - g.viewSpan / 2;
}
static void setFreq(double f, bool remember) {
    if (remember) g.wantFreq = f;                 // what the user asked for, even if this receiver can't reach it
    f = std::max(g.minF, std::min(f, g.maxF)); g.freq = f;
    if (!g.hwValid || fabs(f - (g.hwCenter + g.hwErr)) > 0.43 * g.fs) { retuneHardware(); }
    applyRx(); ensureView(); updateLabels();
    InvalidateRect(g.hwnd, &g.rcFreq, FALSE); InvalidateRect(g.hwnd, &g.rcStatus, FALSE);
}
static void setBwTrack() {
    const ModeInfo& m = MODES[g.mode];
    SendMessage(g.cBw, TBM_SETRANGE, TRUE, MAKELONG(0, (int)((m.bwMax - m.bwMin) / m.bwStep)));
    SendMessage(g.cBw, TBM_SETPOS, TRUE, (int)((g.bw[g.mode] - m.bwMin) / m.bwStep + 0.5));
}
static void setMode(int m, bool keepBw) {
    g.mode = m; ComboBox_SetCurSel(g.cMode, m); setBwTrack();
    g.deemph = (m == M_WFM || m == M_NFM) ? true : g.deemph;
    applyRx(); updateLabels(); InvalidateRect(g.hwnd, 0, FALSE);
}
static void updateLabels() {
    char b[64];
    snprintf(b, sizeof b, "%.1f kHz", g.bw[g.mode] / 1000.0); SetWindowTextA(g.cBwLbl, b);
    if (g.squelch == 0) strcpy(b, "Off"); else snprintf(b, sizeof b, "%.0f dB", -100 + g.squelch * 0.9);
    SetWindowTextA(g.cSqLbl, b);
    SetWindowTextA(g.cGainLbl, g.dev && g.dev->gainSteps() ? g.dev->gainText(g.gainIdx).c_str() : "-");
    snprintf(b, sizeof b, "%.0f Hz", g.pitch); SetWindowTextA(g.cPitchLbl, b);
}
static void applyGain() {
    if (!g.dev) return;
    if (g.dev->hasAgc() && g.tunerAgc) g.dev->setAgc(true);
    else { if (g.dev->hasAgc()) g.dev->setAgc(false); if (g.dev->gainSteps()) g.dev->setGain(g.gainIdx); }
    if (g.dev->extraName()) g.dev->setExtra(g.rtlAgc);
}
static void applySampleRate() {
    if (g.dev) g.dev->setRate(g.fs);
    g.hwValid = false; applyRx(); setFreq(g.freq);
}

// --- device list / selection
static void fillRates() {
    g.rates = g.dev->rates(); SendMessage(g.cRate, CB_RESETCONTENT, 0, 0);
    int best = 0; double bd = 1e18;
    for (size_t i = 0; i < g.rates.size(); i++) {
        char b[48]; if (g.rates[i] >= 1e6) snprintf(b, sizeof b, "%.3f Msps", g.rates[i] / 1e6); else snprintf(b, sizeof b, "%.0f ksps", g.rates[i] / 1e3);
        SendMessageA(g.cRate, CB_ADDSTRING, 0, (LPARAM)b);
        double d = fabs(g.rates[i] - g.fs); if (d < bd) { bd = d; best = (int)i; }
    }
    SendMessage(g.cRate, CB_SETCURSEL, best, 0); g.fs = g.rates[best];
}
static void adaptUiToDevice() {
    Device* d = g.dev; if (!d) return;
    g.minF = d->minHz(); g.maxF = d->maxHz(); fillRates();
    int n = d->gainSteps();
    SendMessage(g.cGain, TBM_SETRANGE, TRUE, MAKELONG(0, std::max(0, n - 1)));
    if (g.gainIdx >= n) g.gainIdx = std::max(0, n * 2 / 3);
    SendMessage(g.cGain, TBM_SETPOS, TRUE, g.gainIdx); EnableWindow(g.cGain, n > 0);
    EnableWindow(g.cTagc, d->hasAgc());
    const char* en = d->extraName(); SetWindowTextA(g.cRagc, en ? en : "(n/a)"); EnableWindow(g.cRagc, en != nullptr);
    EnableWindow(g.cBias, d->hasBias()); EnableWindow(g.cPpm, d->hasPpm()); EnableWindow(g.cHf, d->hasHf());
    d->setHf(g.hfMode);
    if (g.wantFreq < g.minF || g.wantFreq > g.maxF) setStatus("That frequency is outside this receiver's range; it will be restored if you switch back");
    else if (!g.running) setStatus("Press Start to begin receiving");
    g.hwValid = false; applyRx(); setFreq(g.wantFreq, false); updateLabels(); InvalidateRect(g.hwnd, 0, FALSE);
}
static void selectDevice() {
    if (g.running) return;
    int sel = ComboBox_GetCurSel(g.cDev);
    DevEntry e = { KIND_DEMO, 0, "Demo", "", 0 };
    if (sel >= 0 && sel < (int)g.entries.size()) e = g.entries[sel];
    if (g.dev) { delete g.dev; g.dev = nullptr; }
    g.dev = makeDevice(e);
    char b[300]; if (sel >= 0) { GetWindowTextA(g.cDev, b, sizeof b); g.lastDev = b; }
    adaptUiToDevice();
}
static void refreshDeviceList() {
    char cur[300] = ""; GetWindowTextA(g.cDev, cur, sizeof cur); if (!cur[0]) strncpy(cur, g.lastDev.c_str(), sizeof cur - 1);
    enumerateDevices(g.entries);
    SendMessage(g.cDev, CB_RESETCONTENT, 0, 0); int sel = -1;
    for (size_t i = 0; i < g.entries.size(); i++) { SendMessageA(g.cDev, CB_ADDSTRING, 0, (LPARAM)g.entries[i].label.c_str()); if (g.entries[i].label == cur) sel = (int)i; }
    int demoIdx = (int)SendMessageA(g.cDev, CB_ADDSTRING, 0, (LPARAM)"Demo (no hardware)");
    if (sel < 0) sel = (std::string(cur) == "Demo (no hardware)" || g.entries.empty()) ? demoIdx : 0;
    SendMessage(g.cDev, CB_SETCURSEL, sel, 0);
}

static void startRadio() {
    if (g.running || !g.dev) return;
    std::string err;
    g.stop = 0; g.hwValid = false;
    if (!g.dev->open(err)) { setStatus(err); return; }
    adaptUiToDevice();                                   // opened devices know their real sample-rate list
    if (!g.dev->setRate(g.fs)) setStatus("Warning: that sample rate was rejected by the device");
    g.dev->setPpm(g.ppm); g.dev->setHf(g.hfMode); applyGain();
    g.dev->setBias(g.bias);
    applyRx(); retuneHardware(); applyRx(); ensureView();
    if (!g.dev->start(sampleSink)) { setStatus("Could not start streaming from the device"); g.dev->close(); return; }
    if (!g.audio.ok && !g.audio.open()) setStatus("No audio output device available");
    g.devName = g.dev->desc(); g.running = true; SetWindowTextA(g.cStart, "Stop"); EnableWindow(g.cDev, FALSE);
    updateLabels();
    setStatus(g.dev->kind() == KIND_DEMO ? "Demo mode: simulated signals at 1, 7.03, 7.15, 10, 14.25, 27.185, 98.1, 121.5, 146.52 and 162.55 MHz" : "Receiving");
    InvalidateRect(g.hwnd, 0, FALSE);
}
static void stopRecordings();
static void stopRadio() {
    if (!g.running) return;
    g.stop = 1;
    if (g.dev) { g.dev->stop(); g.dev->close(); }
    stopRecordings();
    g.running = false; SetWindowTextA(g.cStart, "Start"); EnableWindow(g.cDev, TRUE); setStatus("Stopped");
}
static void stopRecordings() {
    if (g.recA) { wavHeader(g.recA, g.recAFrames); fclose(g.recA); g.recA = nullptr; SetWindowTextA(g.cRecA, "Rec Audio"); }
    if (g.recI) { fclose(g.recI); g.recI = nullptr; SetWindowTextA(g.cRecI, "Rec IQ"); }
}

// ------------------------------------------------------------------ frequency parsing ("7.1", "14200", "146.52M", "1000k")
static bool parseFreq(const char* s, double& out) {
    std::string t; for (; *s; s++) if (*s != ' ' && *s != ',' && *s != '_') t += *s;
    if (t.empty()) return false;
    char* end; double v = strtod(t.c_str(), &end); if (end == t.c_str()) return false;
    char u = (char)tolower(*end);
    if (u == 'k') v *= 1e3; else if (u == 'm') v *= 1e6; else if (u == 'g') v *= 1e9;
    else if (v < 2500) v *= 1e6; else if (v < 2.5e6) v *= 1e3;
    out = v; return v >= 1000 && v <= 7000e6;
}

// ------------------------------------------------------------------ drawing
static void makePalette() {
    struct St { double t; int r, g, b; } st[] = { { 0, 0, 0, 0 }, { .15, 0, 0, 110 }, { .30, 0, 50, 210 }, { .45, 0, 190, 210 },
        { .60, 40, 220, 40 }, { .75, 240, 240, 0 }, { .90, 255, 60, 0 }, { 1, 255, 255, 255 } };
    for (int i = 0; i < 256; i++) {
        double t = i / 255.0; int k = 0; while (k < 6 && t > st[k + 1].t) k++;
        double u = (t - st[k].t) / (st[k + 1].t - st[k].t);
        int r = (int)(st[k].r + u * (st[k + 1].r - st[k].r)), gg = (int)(st[k].g + u * (st[k + 1].g - st[k].g)), b = (int)(st[k].b + u * (st[k + 1].b - st[k].b));
        g.pal[i] = (r << 16) | (gg << 8) | b;
    }
}
static void layoutPlot() {
    RECT rc; GetClientRect(g.hwnd, &rc);
    g.plotW = std::max(100, (int)rc.right); g.plotH = std::max(100, (int)rc.bottom - g.PH);
    g.axisH = S(18); g.specH = std::max(S(90), (g.plotH - g.axisH) * 38 / 100);
    int wfH = g.plotH - g.specH - g.axisH;
    HDC wdc = GetDC(g.hwnd);
    if (g.plotDC) { SelectObject(g.plotDC, g.plotOld); DeleteObject(g.plotBmp); DeleteDC(g.plotDC); }
    g.plotDC = CreateCompatibleDC(wdc); g.plotBmp = CreateCompatibleBitmap(wdc, g.plotW, g.plotH); g.plotOld = (HBITMAP)SelectObject(g.plotDC, g.plotBmp);
    if (g.wfDC) { SelectObject(g.wfDC, g.wfOld); DeleteObject(g.wfBmp); DeleteDC(g.wfDC); }
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = g.plotW; bi.bmiHeader.biHeight = -wfH;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    g.wfDC = CreateCompatibleDC(wdc); g.wfBmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, (void**)&g.wfBits, 0, 0); g.wfOld = (HBITMAP)SelectObject(g.wfDC, g.wfBmp);
    memset(g.wfBits, 0, (size_t)g.plotW * wfH * 4);
    ReleaseDC(g.hwnd, wdc);
    g.colDb.assign(g.plotW, -150.f);
}
static void buildColumns() {
    if (g.dbAvg.empty() || g.colDb.empty()) return;
    int W = (int)g.colDb.size(); double binw = g.fs / SPEC_N, base = g.hwCenter - g.fs / 2;
    for (int x = 0; x < W; x++) {
        double f0 = g.viewLo + (double)x / W * g.viewSpan, f1 = g.viewLo + (double)(x + 1) / W * g.viewSpan;
        double i0 = (f0 - base) / binw, i1 = (f1 - base) / binw; float v;
        int a = (int)floor(i0), b = (int)floor(i1);
        if (b > a) { v = -200; for (int k = std::max(a, 0); k <= std::min(b, SPEC_N - 1); k++) v = std::max(v, g.dbAvg[k]); }
        else { double fr = (i0 + i1) / 2 - 0.5; int k = (int)floor(fr); double u = fr - k; int k0 = std::max(0, std::min(SPEC_N - 1, k)), k1 = std::max(0, std::min(SPEC_N - 1, k + 1)); v = (float)(g.dbAvg[k0] * (1 - u) + g.dbAvg[k1] * u); }
        g.colDb[x] = v;
    }
}
static void pushWaterfall() {
    int W = g.plotW, H = g.plotH - g.specH - g.axisH; if (!g.wfBits || H < 2) return;
    memmove(g.wfBits + W, g.wfBits, (size_t)W * (H - 1) * 4);
    for (int x = 0; x < W && x < (int)g.colDb.size(); x++) {
        double t = (g.colDb[x] - (g.refDb - g.rngDb)) / g.rngDb; t = t < 0 ? 0 : t > 1 ? 1 : t;
        g.wfBits[x] = g.pal[(int)(t * 255)];
    }
}
static double niceStep(double span) {
    double raw = span / 8, p = pow(10, floor(log10(raw))); double m = raw / p;
    return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
}
static double f2x(double f) { return (f - g.viewLo) / g.viewSpan * g.plotW; }
static double x2f(double x) { return g.viewLo + x / g.plotW * g.viewSpan; }

static void drawPlot(HDC hdc) {
    if (!g.plotDC) return;
    HDC d = g.plotDC; int W = g.plotW, SH = g.specH, AH = g.axisH, WH = g.plotH - SH - AH;
    HBRUSH bg = CreateSolidBrush(RGB(14, 16, 22)); RECT r = { 0, 0, W, SH }; FillRect(d, &r, bg); DeleteObject(bg);
    SelectObject(d, g.fontSm); SetBkMode(d, TRANSPARENT);
    // passband
    double lo, hi; g.rx.passband(lo, hi);
    if (g.mode == M_CW) { lo = -g.bw[g.mode] / 2; hi = g.bw[g.mode] / 2; }
    {
        double x0 = f2x(g.freq + lo), x1 = f2x(g.freq + hi); if (x1 - x0 < 2) x1 = x0 + 2;
        RECT pr = { (LONG)x0, 0, (LONG)x1, SH }; HBRUSH pb = CreateSolidBrush(RGB(34, 40, 78)); FillRect(d, &pr, pb); DeleteObject(pb);
    }
    // grid
    HPEN gp = CreatePen(PS_SOLID, 1, RGB(38, 42, 54)); HPEN old = (HPEN)SelectObject(d, gp);
    for (double db = ceil((g.refDb - g.rngDb) / 10) * 10; db <= g.refDb; db += 10) {
        int y = (int)((g.refDb - db) / g.rngDb * SH); MoveToEx(d, 0, y, 0); LineTo(d, W, y);
        char b[16]; snprintf(b, sizeof b, "%.0f", db); SetTextColor(d, RGB(110, 120, 140)); TextOutA(d, S(3), y + 1, b, (int)strlen(b));
    }
    double step = niceStep(g.viewSpan), f = ceil(g.viewLo / step) * step;
    for (; f <= g.viewLo + g.viewSpan; f += step) { int x = (int)f2x(f); MoveToEx(d, x, 0, 0); LineTo(d, x, SH); }
    SelectObject(d, old); DeleteObject(gp);
    // trace
    HPEN fp = CreatePen(PS_SOLID, 1, RGB(30, 90, 120)), lp = CreatePen(PS_SOLID, 1, RGB(90, 220, 255));
    SelectObject(d, fp);
    int n = (int)g.colDb.size(); std::vector<int> ys(n);
    for (int x = 0; x < n; x++) { double y = (g.refDb - g.colDb[x]) / g.rngDb * SH; y = y < 0 ? 0 : y > SH - 1 ? SH - 1 : y; ys[x] = (int)y; }
    for (int x = 0; x < n; x++) { MoveToEx(d, x, SH - 1, 0); LineTo(d, x, ys[x]); }
    SelectObject(d, lp); MoveToEx(d, 0, ys[0], 0);
    for (int x = 1; x < n; x++) LineTo(d, x, ys[x]);
    SelectObject(d, old); DeleteObject(fp); DeleteObject(lp);
    // axis strip
    RECT ar = { 0, SH, W, SH + AH }; HBRUSH ab = CreateSolidBrush(RGB(26, 28, 36)); FillRect(d, &ar, ab); DeleteObject(ab);
    SetTextColor(d, RGB(200, 205, 215));
    int dec = (int)std::max(0.0, ceil(-log10(step / 1e6))); if (dec > 6) dec = 6;
    for (f = ceil(g.viewLo / step) * step; f <= g.viewLo + g.viewSpan; f += step) {
        int x = (int)f2x(f); char b[32]; snprintf(b, sizeof b, "%.*f", dec, f / 1e6);
        SIZE sz; GetTextExtentPoint32A(d, b, (int)strlen(b), &sz);
        int tx = std::max(0, std::min(W - (int)sz.cx, x - (int)sz.cx / 2)); TextOutA(d, tx, SH + (AH - sz.cy) / 2, b, (int)strlen(b));
        HPEN tp = CreatePen(PS_SOLID, 1, RGB(120, 125, 140)); HPEN o2 = (HPEN)SelectObject(d, tp); MoveToEx(d, x, SH, 0); LineTo(d, x, SH + 4); SelectObject(d, o2); DeleteObject(tp);
    }
    // waterfall
    BitBlt(d, 0, SH + AH, W, WH, g.wfDC, 0, 0, SRCCOPY);
    // tuned marker
    HPEN mp = CreatePen(PS_SOLID, 1, RGB(255, 70, 70)); SelectObject(d, mp);
    int mx = (int)f2x(g.freq); MoveToEx(d, mx, 0, 0); LineTo(d, mx, g.plotH);
    SelectObject(d, old); DeleteObject(mp);
    // hardware-centre tick (DC spike location)
    int cx = (int)f2x(g.hwCenter);
    if (cx > 0 && cx < W) { HPEN cp = CreatePen(PS_DOT, 1, RGB(100, 100, 100)); SelectObject(d, cp); MoveToEx(d, cx, SH - 8, 0); LineTo(d, cx, SH); SelectObject(d, old); DeleteObject(cp); }
    BitBlt(hdc, 0, g.PH, W, g.plotH, d, 0, 0, SRCCOPY);
}

// frequency digits
static void digitLayout(HDC dc, int i, RECT& r) {
    SIZE sz; SelectObject(dc, g.fontBig); GetTextExtentPoint32A(dc, "0", 1, &sz);
    int cw = sz.cx, sw = cw / 2, x = g.rcFreq.left + S(6);
    for (int k = 0; k < i; k++) { x += cw; if (k == 0 || k == 3 || k == 6) x += sw; }
    r.left = x; r.right = x + cw; r.top = g.rcFreq.top; r.bottom = g.rcFreq.bottom;
}
static void drawTop(HDC hdc) {
    // frequency display
    RECT fr = g.rcFreq; HBRUSH b = CreateSolidBrush(RGB(10, 12, 16)); FillRect(hdc, &fr, b); DeleteObject(b);
    HPEN fp = CreatePen(PS_SOLID, 1, RGB(70, 75, 90)); HPEN o = (HPEN)SelectObject(hdc, fp); HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, fr.left, fr.top, fr.right, fr.bottom); SelectObject(hdc, o); SelectObject(hdc, ob); DeleteObject(fp);
    SelectObject(hdc, g.fontBig); SetBkMode(hdc, TRANSPARENT);
    long long v = llround(g.freq); char dg[11]; for (int i = 9; i >= 0; i--) { dg[i] = (char)('0' + v % 10); v /= 10; } dg[10] = 0;
    int first = 0; while (first < 7 && dg[first] == '0') first++;
    for (int i = 0; i < 10; i++) {
        RECT r; digitLayout(hdc, i, r); SetTextColor(hdc, i < first ? RGB(45, 50, 60) : RGB(120, 255, 140));
        DrawTextA(hdc, &dg[i], 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (i == 0 || i == 3 || i == 6) { RECT s = r; s.left = r.right - S(2); s.right = r.right + S(14); SetTextColor(hdc, RGB(90, 100, 115)); DrawTextA(hdc, ".", 1, &s, DT_LEFT | DT_VCENTER | DT_SINGLELINE); }
    }
    SelectObject(hdc, g.fontSm); SetTextColor(hdc, RGB(150, 160, 175)); TextOutA(hdc, fr.right - S(24), fr.bottom - S(16), "Hz", 2);
    // S-meter
    RECT mr = g.rcMeter; HBRUSH mb = CreateSolidBrush(RGB(10, 12, 16)); FillRect(hdc, &mr, mb); DeleteObject(mb);
    double lvl = g.meterSm; double t = (lvl + 100) / 100; t = t < 0 ? 0 : t > 1 ? 1 : t;
    RECT bar = { mr.left + 2, mr.top + 2, mr.left + 2 + (LONG)((mr.right - mr.left - 4) * t), mr.bottom - 2 };
    HBRUSH bb = CreateSolidBrush(t > 0.85 ? RGB(255, 80, 60) : t > 0.6 ? RGB(240, 220, 50) : RGB(60, 210, 90)); FillRect(hdc, &bar, bb); DeleteObject(bb);
    HPEN tp = CreatePen(PS_SOLID, 1, RGB(120, 125, 140)); o = (HPEN)SelectObject(hdc, tp);
    for (int db = -100; db <= 0; db += 10) { int x = mr.left + 2 + (int)((mr.right - mr.left - 4) * (db + 100) / 100.0); MoveToEx(hdc, x, mr.bottom - S(6), 0); LineTo(hdc, x, mr.bottom - 2); }
    SelectObject(hdc, o); DeleteObject(tp);
    char mt[48]; snprintf(mt, sizeof mt, "%.1f dBFS", lvl); SetTextColor(hdc, RGB(235, 235, 240)); SelectObject(hdc, g.fontSm);
    TextOutA(hdc, mr.left + S(5), mr.top + S(2), mt, (int)strlen(mt));
    // status
    RECT sr = g.rcStatus; HBRUSH sb = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &sr, sb); DeleteObject(sb);
    SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
    char l1[400], l2[400];
    snprintf(l1, sizeof l1, "%s   |   HW centre %s MHz   |   %.3f Msps   |   %s", g.running ? g.devName.c_str() : "Not running", fmtFreq(g.hwCenter).c_str(), g.fs / 1e6,
        bandName(g.freq)[0] ? bandName(g.freq) : "-");
    std::string flags;
    if (g.running) {
        if (g.mode == M_WFM) flags += g.rx.stereoLock ? "STEREO  " : "mono  ";
        if (g.mode == M_SAM) { char t2[48]; snprintf(t2, sizeof t2, g.rx.samLock ? "SAM LOCK %+.0f Hz  " : "SAM seeking  ", g.rx.pllHz); flags += t2; }
        if (g.squelch) flags += g.rx.sqOpen ? "SQ open  " : "SQ closed  ";
        if (g.recA) flags += "[REC AUDIO]  "; if (g.recI) flags += "[REC IQ]  ";
    }
    snprintf(l2, sizeof l2, "%s%s", flags.c_str(), g.status.c_str());
    TextOutA(hdc, sr.left, sr.top, l1, (int)strlen(l1)); TextOutA(hdc, sr.left, sr.top + S(18), l2, (int)strlen(l2));
}


// ------------------------------------------------------------------ preset storage
static std::string presetPath() { return appDir() + "\\ta-sdr-presets.txt"; }
static void defaultPresets() {
    presets.clear();
    for (int i = 1; i < NDEFAULTS; i++) presets.push_back({ DEFAULTS[i].name, DEFAULTS[i].f, DEFAULTS[i].mode, 0 });
}
static void savePresets() {
    FILE* f = fopen(presetPath().c_str(), "w"); if (!f) { setStatus("Could not write the presets file"); return; }
    fprintf(f, "# TA-SDR presets: name|frequency Hz|mode (0 AM,1 SAM,2 USB,3 LSB,4 CW,5 NFM,6 WFM)|bandwidth Hz (0 = default)\n");
    for (auto& p : presets) fprintf(f, "%s|%.0f|%d|%.0f\n", p.name.c_str(), p.f, p.mode, p.bw);
    fclose(f);
}
static void loadPresets() {
    FILE* f = fopen(presetPath().c_str(), "r"); if (!f) { defaultPresets(); return; }
    presets.clear(); char line[600];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        std::string t = line; while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
        std::vector<std::string> parts; size_t a = 0, b;
        while ((b = t.find('|', a)) != std::string::npos) { parts.push_back(t.substr(a, b - a)); a = b + 1; }
        parts.push_back(t.substr(a));
        if (parts.size() < 3) continue;
        UPreset p; p.name = parts[0]; p.f = atof(parts[1].c_str()); p.mode = atoi(parts[2].c_str()); p.bw = parts.size() > 3 ? atof(parts[3].c_str()) : 0;
        if (p.name.empty() || p.f < 1000 || p.f > 7000e6 || p.mode < 0 || p.mode >= M_COUNT) continue;
        presets.push_back(p);
    }
    fclose(f);
}
static void refillBand(int sel) {
    SendMessage(g.cBand, CB_RESETCONTENT, 0, 0);
    char h[64]; snprintf(h, sizeof h, "-- Presets (%d) --", (int)presets.size());
    SendMessageA(g.cBand, CB_ADDSTRING, 0, (LPARAM)h);
    for (auto& p : presets) SendMessageA(g.cBand, CB_ADDSTRING, 0, (LPARAM)p.name.c_str());
    SendMessage(g.cBand, CB_SETCURSEL, sel, 0);
}

// ------------------------------------------------------------------ preset editor dialog (modal, built in code)
struct PDlg { UPreset p; bool ok = false, done = false; HWND hName, hFreq, hMode, hBw; };
static HWND mkp(HWND par, const char* cls, const char* text, DWORD style, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), par, (HMENU)(INT_PTR)id, hInst, 0);
    SendMessage(c, WM_SETFONT, (WPARAM)g.font, TRUE); return c;
}
static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PDlg* d = (PDlg*)GetWindowLongPtr(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        d = (PDlg*)((CREATESTRUCT*)l)->lpCreateParams; SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)d);
        mkp(h, "STATIC", "Name", SS_LEFT, -1, 12, 15, 90, 16);
        d->hName = mkp(h, "EDIT", d->p.name.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 200, 110, 12, 248, 22);
        mkp(h, "STATIC", "Frequency", SS_LEFT, -1, 12, 47, 90, 16);
        char fb[64]; snprintf(fb, sizeof fb, "%.6f", d->p.f / 1e6); { char* e = fb + strlen(fb); while (e > fb && e[-1] == '0') *--e = 0; if (e > fb && e[-1] == '.') *--e = 0; }
        d->hFreq = mkp(h, "EDIT", fb, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 201, 110, 44, 110, 22);
        mkp(h, "STATIC", "MHz, or 14200k", SS_LEFT, -1, 228, 47, 140, 16);
        mkp(h, "STATIC", "Mode", SS_LEFT, -1, 12, 79, 90, 16);
        d->hMode = mkp(h, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 202, 110, 76, 90, 200);
        for (int i = 0; i < M_COUNT; i++) SendMessageA(d->hMode, CB_ADDSTRING, 0, (LPARAM)MODES[i].name);
        SendMessage(d->hMode, CB_SETCURSEL, d->p.mode, 0);
        mkp(h, "STATIC", "Bandwidth", SS_LEFT, -1, 12, 111, 90, 16);
        char bb[32]; if (d->p.bw > 0) snprintf(bb, sizeof bb, "%g", d->p.bw / 1000); else bb[0] = 0;
        d->hBw = mkp(h, "EDIT", bb, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 203, 110, 108, 60, 22);
        mkp(h, "STATIC", "kHz  (blank = mode default)", SS_LEFT, -1, 178, 111, 190, 16);
        mkp(h, "BUTTON", "OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 192, 146, 80, 26);
        mkp(h, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 280, 146, 80, 26);
        return 0; }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            char nm[256], fq[64], bw[32]; GetWindowTextA(d->hName, nm, 256); GetWindowTextA(d->hFreq, fq, 64); GetWindowTextA(d->hBw, bw, 32);
            std::string name = nm; for (auto& c : name) if (c == '|' || c == '\n' || c == '\r') c = ' ';
            while (!name.empty() && name.back() == ' ') name.pop_back(); while (!name.empty() && name[0] == ' ') name.erase(0, 1);
            double f; if (name.empty()) { MessageBoxA(h, "Please give the preset a name.", "TA-SDR", MB_OK | MB_ICONINFORMATION); return 0; }
            if (!parseFreq(fq, f)) { MessageBoxA(h, "I couldn't understand that frequency.\nTry 7.1 (MHz), 14200 (kHz), 146.52M.", "TA-SDR", MB_OK | MB_ICONINFORMATION); return 0; }
            int mode = (int)SendMessage(d->hMode, CB_GETCURSEL, 0, 0); if (mode < 0) mode = 0;
            double b = atof(bw) * 1000; if (b > 0) b = std::max(MODES[mode].bwMin, std::min(MODES[mode].bwMax, b)); else b = 0;
            d->p = { name, f, mode, b }; d->ok = true; d->done = true; return 0;
        }
        if (LOWORD(w) == IDCANCEL) { d->done = true; return 0; }
        break;
    case WM_CLOSE: d->done = true; return 0;
    }
    return DefWindowProc(h, m, w, l);
}
static bool presetDialog(const char* title, UPreset& p) {
    static bool reg = false;
    if (!reg) { WNDCLASSA wc = {}; wc.lpfnWndProc = DlgProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "TaSdrDlg"; wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1)); RegisterClassA(&wc); reg = true; }
    PDlg d; d.p = p;
    RECT pr; GetWindowRect(g.hwnd, &pr); int W = S(380), H = S(220);
    HWND dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "TaSdrDlg", title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        pr.left + (pr.right - pr.left - W) / 2, pr.top + S(120), W, H, g.hwnd, 0, hInst, &d);
    EnableWindow(g.hwnd, FALSE); SetFocus(d.hName); SendMessage(d.hName, EM_SETSEL, 0, -1);
    MSG m;
    while (!d.done && GetMessage(&m, 0, 0, 0) > 0) { if (!IsDialogMessage(dlg, &m)) { TranslateMessage(&m); DispatchMessage(&m); } }
    EnableWindow(g.hwnd, TRUE); DestroyWindow(dlg); SetForegroundWindow(g.hwnd);
    if (d.ok) p = d.p; return d.ok;
}
static void presetCmd(int id) {
    int sel = ComboBox_GetCurSel(g.cBand);
    if (id == IDC_PADD) {
        UPreset p; char nm[128]; const char* bn = bandName(g.freq);
        snprintf(nm, sizeof nm, "%s%s%s %s", bn, bn[0] ? " " : "", fmtFreq(g.freq).c_str(), MODES[g.mode].name); p.name = nm; p.f = g.freq; p.mode = g.mode; p.bw = g.bw[g.mode];
        if (presetDialog("Add preset", p)) { presets.push_back(p); savePresets(); refillBand((int)presets.size()); setStatus("Preset added: " + p.name); }
    } else if (id == IDC_PEDIT) {
        if (sel <= 0) { setStatus("Pick a preset in the Band list first, then press Edit"); return; }
        UPreset p = presets[sel - 1];
        if (presetDialog("Edit preset", p)) { presets[sel - 1] = p; savePresets(); refillBand(sel); setStatus("Preset updated: " + p.name); }
    } else if (id == IDC_PDEL) {
        if (sel <= 0) { setStatus("Pick a preset in the Band list first, then press Delete"); return; }
        std::string q = "Delete preset \"" + presets[sel - 1].name + "\"?";
        if (MessageBoxA(g.hwnd, q.c_str(), "TA-SDR", MB_YESNO | MB_ICONQUESTION) == IDYES) { presets.erase(presets.begin() + sel - 1); savePresets(); refillBand(0); setStatus("Preset deleted"); }
    } else if (id == IDC_PRESET) {
        if (MessageBoxA(g.hwnd, "Replace ALL presets with the built-in defaults?\nYour own presets will be lost.", "TA-SDR", MB_YESNO | MB_ICONWARNING) == IDYES) {
            defaultPresets(); DeleteFileA(presetPath().c_str()); refillBand(0); setStatus("Presets reset to defaults"); }
    }
}

// ------------------------------------------------------------------ settings
static std::string iniPath() { return appDir() + "\\ta-sdr.ini"; }
static void saveSettings() {
    std::string p = iniPath(); char b[64];
#define W(k, v) { snprintf(b, sizeof b, "%.0f", (double)(v)); WritePrivateProfileStringA("rx", k, b, p.c_str()); }
    W("freq", g.freq) W("mode", g.mode) W("rate", g.fs) W("step", g.stepIdx) W("zoom", g.zoomIdx) W("gain", g.gainIdx) W("tagc", g.tunerAgc)
    W("ragc", g.rtlAgc) W("bias", g.bias) W("nb", g.nb) W("deemph", g.deemph) W("stereo", g.stereo) W("agc", g.agcMode) W("ppm", g.ppm)
    WritePrivateProfileStringA("rx", "device", g.lastDev.c_str(), p.c_str());
    W("hf", g.hfMode) W("vol", g.volume) W("sq", g.squelch) W("pitch", g.pitch) W("ref", g.refDb) W("rng", g.rngDb)
    for (int m = 0; m < M_COUNT; m++) { char k[16]; snprintf(k, sizeof k, "bw%d", m); W(k, g.bw[m]) }
#undef W
}
static void loadSettings() {
    std::string p = iniPath();
    auto I = [&](const char* k, int d) { return (int)GetPrivateProfileIntA("rx", k, d, p.c_str()); };
    g.freq = I("freq", 7100000); g.mode = std::max(0, std::min(M_COUNT - 1, I("mode", M_LSB))); g.fs = I("rate", 2048000);
    g.stepIdx = std::max(0, std::min(NSTEPS - 1, I("step", 5))); g.zoomIdx = std::max(0, std::min(5, I("zoom", 0)));
    g.gainIdx = I("gain", 20); g.tunerAgc = I("tagc", 0); g.rtlAgc = I("ragc", 0); g.bias = I("bias", 0); g.nb = I("nb", 0);
    g.deemph = I("deemph", 1); g.stereo = I("stereo", 1); g.agcMode = std::max(0, std::min(3, I("agc", 2))); g.ppm = I("ppm", 0);
    g.hfMode = std::max(0, std::min(2, I("hf", 0))); g.volume = I("vol", 50); g.squelch = I("sq", 0); g.pitch = I("pitch", 700);
    g.refDb = I("ref", -10); g.rngDb = I("rng", 90);
    for (int m = 0; m < M_COUNT; m++) { char k[16]; snprintf(k, sizeof k, "bw%d", m); g.bw[m] = I(k, (int)MODES[m].bwDef); g.bw[m] = std::max(MODES[m].bwMin, std::min(MODES[m].bwMax, g.bw[m])); }
    if (g.fs < 100e3 || g.fs > 25e6) g.fs = 2048000;
    g.freq = std::max(1000.0, std::min(g.freq, 7000e6)); g.wantFreq = g.freq;
    { char b[300]; GetPrivateProfileStringA("rx", "device", "", b, sizeof b, p.c_str()); g.lastDev = b; }
}

// ------------------------------------------------------------------ UI construction
static HWND mk(const char* cls, const char* text, DWORD style, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g.hwnd, (HMENU)(INT_PTR)id, hInst, 0);
    SendMessage(c, WM_SETFONT, (WPARAM)g.font, TRUE); return c;
}
static HWND lbl(const char* t, int x, int y, int w) { return mk("STATIC", t, SS_LEFT, -1, x, y + 3, w, 16); }
static HWND combo(int id, int x, int y, int w, std::initializer_list<const char*> items, int sel) {
    HWND c = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id, x, y, w, 260);
    for (auto s : items) SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessage(c, CB_SETCURSEL, sel, 0); return c;
}
static HWND track(int id, int x, int y, int w, int lo, int hi, int pos) {
    HWND c = mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, id, x, y, w, 22);
    SendMessage(c, TBM_SETRANGE, TRUE, MAKELONG(lo, hi)); SendMessage(c, TBM_SETPOS, TRUE, pos); return c;
}
static HWND check(int id, const char* t, int x, int y, int w, bool on) {
    HWND c = mk("BUTTON", t, BS_AUTOCHECKBOX | WS_TABSTOP, id, x, y + 1, w, 20); SendMessage(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); return c;
}
static void createControls() {
    const int y1 = 72, y2 = 102, y3 = 132, y4 = 162;
    lbl("Mode", 8, y1, 34); g.cMode = combo(IDC_MODE, 44, y1, 66, { "AM", "SAM", "USB", "LSB", "CW", "NFM", "WFM" }, g.mode);
    lbl("Filter", 118, y1, 34); g.cBw = track(IDC_BW, 152, y1, 130, 0, 100, 0); g.cBwLbl = lbl("", 284, y1, 64);
    lbl("Step", 352, y1, 30);
    { char b[32]; HWND c = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_STEP, 384, y1, 84, 260);
      for (int i = 0; i < NSTEPS; i++) { if (STEPS[i] >= 1000) snprintf(b, sizeof b, "%g kHz", STEPS[i] / 1000); else snprintf(b, sizeof b, "%g Hz", STEPS[i]); SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)b); }
      SendMessage(c, CB_SETCURSEL, g.stepIdx, 0); g.cStep = c; }
    lbl("Band", 478, y1, 32);
    { HWND c = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_BAND, 512, y1, 188, 400);
      g.cBand = c; refillBand(0); }
    lbl("Vol", 710, y1, 24); g.cVol = track(IDC_VOL, 736, y1, 96, 0, 100, g.volume);
    lbl("Squelch", 842, y1, 50); g.cSq = track(IDC_SQ, 894, y1, 96, 0, 100, g.squelch); g.cSqLbl = lbl("", 992, y1, 50);

    g.cTagc = check(IDC_TAGC, "Tuner AGC", 8, y2, 84, g.tunerAgc); lbl("Gain", 96, y2, 30);
    g.cGain = track(IDC_GAIN, 128, y2, 130, 0, 28, g.gainIdx); g.cGainLbl = lbl("", 260, y2, 60);
    g.cRagc = check(IDC_RAGC, "RTL AGC", 326, y2, 70, g.rtlAgc); g.cBias = check(IDC_BIAS, "Bias-T", 400, y2, 60, g.bias);
    g.cNb = check(IDC_NB, "Noise blank", 464, y2, 84, g.nb); g.cDeemph = check(IDC_DEEMPH, "De-emph", 552, y2, 70, g.deemph);
    g.cStereo = check(IDC_STEREO, "Stereo", 626, y2, 60, g.stereo);
    lbl("Audio AGC", 694, y2, 62); g.cAgc = combo(IDC_AGC, 758, y2, 64, { "Off", "Fast", "Med", "Slow" }, g.agcMode);
    lbl("CW pitch", 832, y2, 52); g.cPitch = track(IDC_PITCH, 886, y2, 80, 30, 120, (int)(g.pitch / 10)); g.cPitchLbl = lbl("", 968, y2, 60);

    lbl("Device", 8, y3, 40); g.cDev = combo(IDC_DEV, 50, y3, 230, {}, 0);
    g.cStart = mk("BUTTON", "Start", BS_PUSHBUTTON | WS_TABSTOP, IDC_START, 288, y3, 64, 24);
    lbl("Sample rate", 362, y3, 66);
    g.cRate = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_RATE, 430, y3, 92, 260);
    lbl("PPM", 532, y3, 28); g.cPpm = mk("EDIT", "", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, IDC_PPM, 562, y3, 44, 22);
    { char b[16]; snprintf(b, sizeof b, "%d", g.ppm); SetWindowTextA(g.cPpm, b); }
    lbl("HF input", 616, y3, 52); g.cHf = combo(IDC_HF, 670, y3, 112, { "Auto (V3/V4)", "Force I branch", "Force Q branch" }, g.hfMode);
    lbl("Zoom", 792, y3, 32); g.cZoom = combo(IDC_ZOOM, 826, y3, 56, { "1x", "2x", "4x", "8x", "16x", "32x" }, g.zoomIdx);
    lbl("Ref", 892, y3, 24); g.cRef = track(IDC_REF, 918, y3, 70, -60, 10, (int)g.refDb);
    lbl("Range", 992, y3, 40); g.cRng = track(IDC_RNG, 1034, y3, 70, 30, 120, (int)g.rngDb);

    lbl("Go to", 8, y4, 38); g.cGo = mk("EDIT", "", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, IDC_GO, 48, y4, 110, 22);
    g.cGoBtn = mk("BUTTON", "Go", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_GOBTN, 164, y4, 40, 24);
    lbl("(7.1 = MHz,  14200 = kHz,  1000k,  146.52M)", 212, y4, 250);
    g.cRecA = mk("BUTTON", "Rec Audio", BS_PUSHBUTTON | WS_TABSTOP, IDC_RECA, 470, y4, 84, 24);
    g.cRecI = mk("BUTTON", "Rec IQ", BS_PUSHBUTTON | WS_TABSTOP, IDC_RECI, 560, y4, 70, 24);
    mk("BUTTON", "+ Preset", BS_PUSHBUTTON | WS_TABSTOP, IDC_PADD, 650, y4, 70, 24);
    mk("BUTTON", "Edit", BS_PUSHBUTTON | WS_TABSTOP, IDC_PEDIT, 724, y4, 54, 24);
    mk("BUTTON", "Delete", BS_PUSHBUTTON | WS_TABSTOP, IDC_PDEL, 782, y4, 60, 24);
    mk("BUTTON", "Reset list", BS_PUSHBUTTON | WS_TABSTOP, IDC_PRESET, 846, y4, 74, 24);

    refreshDeviceList();
    SendMessage(g.cBw, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    setBwTrack();
    g.PH = S(196);
    g.rcFreq = { S(8), S(8), S(8) + S(392), S(8) + S(58) };
    g.rcMeter = { S(412), S(8), S(412) + S(330), S(8) + S(24) };
    g.rcStatus = { S(412), S(38), S(412) + S(900), S(38) + S(36) };
}

// ------------------------------------------------------------------ events
static void doPreset(int idx) {
    if (idx <= 0 || idx > (int)presets.size()) return;
    const UPreset& p = presets[idx - 1];
    g.mode = p.mode; ComboBox_SetCurSel(g.cMode, g.mode);
    if (p.bw > 0) g.bw[p.mode] = std::max(MODES[p.mode].bwMin, std::min(MODES[p.mode].bwMax, p.bw));
    setBwTrack();
    g.stepIdx = 5; double want = MODES[p.mode].stepDef; for (int i = 0; i < NSTEPS; i++) if (STEPS[i] == want) g.stepIdx = i;
    ComboBox_SetCurSel(g.cStep, g.stepIdx);
    if (p.mode == M_WFM || p.mode == M_NFM) { g.deemph = true; Button_SetCheck(g.cDeemph, BST_CHECKED); }
    setFreq(p.f); applyRx(); updateLabels(); InvalidateRect(g.hwnd, 0, FALSE);
    SetFocus(g.hwnd);
}
static void tuneStep(int dir, int mult = 1) { setFreq(g.freq + dir * mult * STEPS[g.stepIdx]); }

static void onCommand(int id, int code) {
    switch (id) {
    case IDC_MODE: if (code == CBN_SELCHANGE) setMode(ComboBox_GetCurSel(g.cMode)); break;
    case IDC_STEP: if (code == CBN_SELCHANGE) g.stepIdx = ComboBox_GetCurSel(g.cStep); break;
    case IDC_BAND: if (code == CBN_SELCHANGE) doPreset(ComboBox_GetCurSel(g.cBand)); break;
    case IDC_TAGC: g.tunerAgc = Button_GetCheck(g.cTagc) == BST_CHECKED; applyGain(); break;
    case IDC_RAGC: g.rtlAgc = Button_GetCheck(g.cRagc) == BST_CHECKED; applyGain(); break;
    case IDC_BIAS: g.bias = Button_GetCheck(g.cBias) == BST_CHECKED; if (g.dev && g.running) g.dev->setBias(g.bias); break;
    case IDC_NB: g.nb = Button_GetCheck(g.cNb) == BST_CHECKED; applyRx(); break;
    case IDC_DEEMPH: g.deemph = Button_GetCheck(g.cDeemph) == BST_CHECKED; applyRx(); break;
    case IDC_STEREO: g.stereo = Button_GetCheck(g.cStereo) == BST_CHECKED; applyRx(); break;
    case IDC_AGC: if (code == CBN_SELCHANGE) { g.agcMode = ComboBox_GetCurSel(g.cAgc); applyRx(); } break;
    case IDC_RATE: if (code == CBN_SELCHANGE) { int i = ComboBox_GetCurSel(g.cRate); if (i >= 0 && i < (int)g.rates.size()) { g.fs = g.rates[i]; applySampleRate(); } } break;
    case IDC_DEV: if (code == CBN_DROPDOWN && !g.running) refreshDeviceList(); else if (code == CBN_SELCHANGE) selectDevice(); break;
    case IDC_HF: if (code == CBN_SELCHANGE) { g.hfMode = ComboBox_GetCurSel(g.cHf); if (g.dev) { g.dev->setHf(g.hfMode); if (g.running) { g.hwValid = false; setFreq(g.freq); } } } break;
    case IDC_ZOOM: if (code == CBN_SELCHANGE) { g.zoomIdx = ComboBox_GetCurSel(g.cZoom); g.viewCenter = g.freq; ensureView(); InvalidateRect(g.hwnd, 0, FALSE); } break;
    case IDC_PPM: if (code == EN_CHANGE) { char b[32]; GetWindowTextA(g.cPpm, b, 32); g.ppm = atoi(b); if (g.dev && g.running) { g.dev->setPpm(g.ppm); g.hwErr = g.dev->centerError(); applyRx(); } } break;
    case IDC_GOBTN: { char b[64]; GetWindowTextA(g.cGo, b, 64); double f; if (parseFreq(b, f)) { setFreq(f); SetFocus(g.hwnd); } else setStatus("Could not understand that frequency"); } break;
    case IDC_PADD: case IDC_PEDIT: case IDC_PDEL: case IDC_PRESET: presetCmd(id); break;
    case IDC_START: if (g.running) stopRadio(); else startRadio(); break;
    case IDC_RECA:
        if (g.recA) { stopRecordings(); setStatus("Audio recording saved"); break; }
        if (!g.running) { setStatus("Start the receiver first"); break; }
        { char ts[32], p[MAX_PATH]; timestamp(ts, sizeof ts); snprintf(p, sizeof p, "%s\\audio_%s_%.0fHz_%s.wav", recDir().c_str(), ts, g.freq, MODES[g.mode].name);
          g.recA = fopen(p, "wb"); if (g.recA) { g.recAFrames = 0; wavHeader(g.recA, 0); strcpy(g.recAName, p); SetWindowTextA(g.cRecA, "Stop Rec"); setStatus(std::string("Recording audio to ") + p); } }
        break;
    case IDC_RECI:
        if (g.recI) { stopRecordings(); setStatus("IQ recording saved"); break; }
        if (!g.running) { setStatus("Start the receiver first"); break; }
        { char ts[32], p[MAX_PATH]; timestamp(ts, sizeof ts); const char* ext = g.dev->kind() == KIND_HACKRF ? "cs8" : (g.dev->kind() == KIND_AIRSPY || g.dev->kind() == KIND_AIRSPYHF) ? "cf32" : "cu8";
          snprintf(p, sizeof p, "%s\\iq_%s_%.0fHz_%.0fsps.%s", recDir().c_str(), ts, g.hwCenter, g.fs, ext);
          g.recI = fopen(p, "wb"); if (g.recI) { SetWindowTextA(g.cRecI, "Stop Rec"); setStatus(std::string("Recording raw IQ to ") + p); } }
        break;
    }
}
static void onScroll(HWND h) {
    if (h == g.cBw) { const ModeInfo& m = MODES[g.mode]; g.bw[g.mode] = m.bwMin + SendMessage(g.cBw, TBM_GETPOS, 0, 0) * m.bwStep; applyRx(); }
    else if (h == g.cVol) { g.volume = (int)SendMessage(g.cVol, TBM_GETPOS, 0, 0); applyRx(); }
    else if (h == g.cSq) { g.squelch = (int)SendMessage(g.cSq, TBM_GETPOS, 0, 0); applyRx(); }
    else if (h == g.cGain) { g.gainIdx = (int)SendMessage(g.cGain, TBM_GETPOS, 0, 0); applyGain(); }
    else if (h == g.cPitch) { g.pitch = SendMessage(g.cPitch, TBM_GETPOS, 0, 0) * 10.0; applyRx(); }
    else if (h == g.cRef) { g.refDb = (double)(int)SendMessage(g.cRef, TBM_GETPOS, 0, 0); }
    else if (h == g.cRng) { g.rngDb = (double)(int)SendMessage(g.cRng, TBM_GETPOS, 0, 0); }
    updateLabels();
}
static bool inRect(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

static void onTimer() {
    if (!g.running) return;
    if (g.dev && !g.dev->alive()) { PostMessage(g.hwnd, WM_DEVEND, 0, 0); return; }
    EnterCriticalSection(&g.rxCs);
    g.rx.snapshot(g.snap.data(), SPEC_N);
    float m = g.rx.meterDb;
    LeaveCriticalSection(&g.rxCs);
    g.spec.compute(g.snap.data(), g.dbNew.data());
    for (int k = 0; k < SPEC_N; k++) g.dbAvg[k] += 0.45f * (g.dbNew[k] - g.dbAvg[k]);
    g.meterSm += 0.3f * (m - g.meterSm);
    ensureView(); buildColumns(); pushWaterfall();
    RECT pr = { 0, g.PH, g.plotW, g.PH + g.plotH };
    InvalidateRect(g.hwnd, &pr, FALSE); InvalidateRect(g.hwnd, &g.rcMeter, FALSE); InvalidateRect(g.hwnd, &g.rcStatus, FALSE);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: g.hwnd = h; return 0;
    case WM_SIZE: if (g.plotDC || g.PH) { layoutPlot(); ensureView(); } return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)lp; mm->ptMinTrackSize.x = S(1120); mm->ptMinTrackSize.y = S(620); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT pr = { 0, 0, g.plotW, g.PH };
        // panel background (outside child controls)
        HBRUSH bb = GetSysColorBrush(COLOR_BTNFACE); FillRect(dc, &pr, bb);
        drawTop(dc); drawPlot(dc);
        EndPaint(h, &ps); return 0;
    }
    case WM_CTLCOLORSTATIC: { HDC dc = (HDC)wp; SetBkMode(dc, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE); }
    case WM_TIMER: onTimer(); return 0;
    case WM_COMMAND: onCommand(LOWORD(wp), HIWORD(wp)); return 0;
    case WM_HSCROLL: onScroll((HWND)lp); return 0;
    case WM_DEVEND: if (g.running) { stopRadio(); setStatus("Device stopped (unplugged or USB error). Press Start to reconnect."); } return 0;
    case WM_LBUTTONDOWN: case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (msg == WM_LBUTTONDOWN) {
            SetFocus(h);
            if (inRect(g.rcFreq, x, y)) {
                HDC dc = GetDC(h);
                for (int i = 0; i < 10; i++) { RECT r; digitLayout(dc, i, r); if (x >= r.left && x < r.right) { double p10 = pow(10, 9 - i); setFreq(g.freq + (y < (r.top + r.bottom) / 2 ? p10 : -p10)); break; } }
                ReleaseDC(h, dc); return 0;
            }
            if (y >= g.PH) { g.dragging = true; SetCapture(h); }
        }
        if (g.dragging && (wp & MK_LBUTTON)) {
            double f = x2f(std::max(0, std::min(g.plotW - 1, x))), st = STEPS[g.stepIdx];
            setFreq(floor(f / st + 0.5) * st);
        }
        return 0;
    }
    case WM_LBUTTONUP: if (g.dragging) { g.dragging = false; ReleaseCapture(); } return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (inRect(g.rcFreq, pt.x, pt.y)) {
            HDC dc = GetDC(h);
            for (int i = 0; i < 10; i++) { RECT r; digitLayout(dc, i, r); if (pt.x >= r.left && pt.x < r.right) { setFreq(g.freq + d * pow(10, 9 - i)); break; } }
            ReleaseDC(h, dc);
        } else if (pt.y >= g.PH) tuneStep(d, (wp & MK_CONTROL) ? 10 : 1);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_RIGHT || wp == VK_UP) tuneStep(1, (GetKeyState(VK_CONTROL) & 0x8000) ? 10 : 1);
        else if (wp == VK_LEFT || wp == VK_DOWN) tuneStep(-1, (GetKeyState(VK_CONTROL) & 0x8000) ? 10 : 1);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1); stopRadio(); saveSettings(); g.audio.close(); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    hInst = hi; g.hi = hi; timeBeginPeriod(1);
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES }; InitCommonControlsEx(&ic);
    HDC sdc = GetDC(0); g.dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(0, sdc);
    InitializeCriticalSection(&g.rxCs);
    for (int m = 0; m < M_COUNT; m++) g.bw[m] = MODES[m].bwDef;
    loadSettings(); loadPresets(); makePalette();
    g.font = CreateFontA(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g.fontSm = CreateFontA(-S(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g.fontBig = CreateFontA(-S(44), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
    g.spec.init(SPEC_N); g.snap.assign(SPEC_N * 2, 0.f); g.dbNew.assign(SPEC_N, -150.f); g.dbAvg.assign(SPEC_N, -150.f);
    WNDCLASSA wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = 0; wc.lpszClassName = "TaSdrWnd"; wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1)); RegisterClassA(&wc);
    HWND w = CreateWindowExA(0, "TaSdrWnd", "TA-SDR  -  AM / SAM / SSB / CW / NFM / WFM", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, S(1180), S(760), 0, 0, hi, 0);
    SendMessage(w, WM_SETICON, ICON_BIG, (LPARAM)LoadImage(hi, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    SendMessage(w, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(hi, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    createControls(); selectDevice(); updateLabels(); setMode(g.mode);
    ComboBox_SetCurSel(g.cStep, g.stepIdx);
    layoutPlot();
    g.hwCenter = g.freq + g.fs / 4; g.viewCenter = g.freq; ensureView();
    if (!g.audio.open()) g.status = "No audio output device found";
    else g.status = "Pick a device and press Start (or choose 'Demo' to try it without hardware)";
    SetTimer(w, 1, 40, 0); ShowWindow(w, show); InvalidateRect(w, 0, FALSE); SetFocus(w);
    MSG m; while (GetMessage(&m, 0, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && GetFocus() == w) { WndProc(w, WM_KEYDOWN, m.wParam, m.lParam); continue; }
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN && GetFocus() == g.cGo) { onCommand(IDC_GOBTN, BN_CLICKED); continue; }
        TranslateMessage(&m); DispatchMessage(&m);
    }
    return 0;
}
