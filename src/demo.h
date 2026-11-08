// demo.h - simulated stations, so the app can be tried without any hardware
#pragma once
#include "devices.h"
#include <math.h>
#include <string.h>



struct DemoSrc { int type; double f, amp; };
static const DemoSrc DEMO[] = {
    { 0, 1000000, 0.20 },  { 0, 10000000, 0.20 }, { 5, 7030000, 0.15 }, { 6, 7150000, 0.15 }, { 7, 14250000, 0.15 },
    { 0, 27185000, 0.20 }, { 4, 98100000, 0.20 },  { 3, 146520000, 0.20 }, { 0, 121500000, 0.20 }, { 3, 162550000, 0.15 } };
static const char* MORSE_MSG = "CQ CQ DE W1AW K";
static const char* morse(char c) {
    switch (c) { case 'C': return "-.-."; case 'Q': return "--.-"; case 'D': return "-.."; case 'E': return "."; case 'W': return ".--";
    case '1': return ".----"; case 'A': return ".-"; case 'K': return "-.-"; default: return ""; }
}
static bool morseKey(double t) {
    const double dit = 1.2 / 16;
    static std::string seq; // 1 = key down, 0 = up, one char per dit
    if (seq.empty()) {
        for (const char* p = MORSE_MSG; *p; p++) {
            if (*p == ' ') { seq += "0000"; continue; }
            for (const char* m = morse(*p); *m; m++) { seq += (*m == '.') ? "1" : "111"; seq += "0"; }
            seq += "00";
        }
        seq += "000000";
    }
    size_t i = (size_t)(t / dit) % seq.size(); return seq[i] == '1';
}
struct DemoDev;
static DWORD WINAPI demoThread(LPVOID p);

struct DemoDev : Device {
    volatile double fs = 2048000, center = 7e6; volatile LONG stopFlag = 0; HANDLE th = 0; SampleSink sink = nullptr;
    int kind() override { return KIND_DEMO; }
    bool open(std::string&) override { return true; }
    void close() override {}
    std::vector<double> rates() override { return { 250000, 1024000, 1400000, 1800000, 1920000, 2048000, 2400000, 2560000, 2800000, 3200000, 5000000, 10000000 }; }
    bool setRate(double r) override { fs = r; return true; }
    bool setCenter(double hz) override { center = hz; return true; }
    double maxHz() override { return 2200e6; }
    bool start(SampleSink s) override { sink = s; stopFlag = 0; th = CreateThread(0, 0, demoThread, this, 0, 0); return th != 0; }
    void stop() override { stopFlag = 1; if (th) { WaitForSingleObject(th, 3000); CloseHandle(th); th = 0; } }
    std::string desc() override { return "Demo signal generator (no hardware)"; }
};

static DWORD WINAPI demoThread(LPVOID p) {
    DemoDev* self = (DemoDev*)p;
    const size_t BLK = 32768, CH = 4;
    std::vector<float> ai(BLK), aq(BLK); std::vector<uint8_t> raw(BLK * 2);
    LARGE_INTEGER fq, t0, now; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
    double tsamp = 0; uint32_t rng = 7;
    static double ph[16]; for (int i = 0; i < 16; i++) ph[i] = 0;
    while (!self->stopFlag) {
        double fs = self->fs, c = self->center;
        std::fill(ai.begin(), ai.end(), 0.f); std::fill(aq.begin(), aq.end(), 0.f);
        for (size_t s = 0; s < sizeof(DEMO) / sizeof(DEMO[0]); s++) {
            const DemoSrc& d = DEMO[s];
            double span = fs * 0.48;
            double fmax = (d.type == 4) ? 120e3 : (d.type == 3 ? 15e3 : 4e3);
            double off = d.f - c; if (fabs(off) + fmax > span + 1) continue;
            for (size_t k = 0; k < BLK; k += CH) {
                double t = tsamp + k / fs, amp = d.amp, df = off;
                switch (d.type) {
                case 0: { double env = 0.5 + 0.5 * sin(2 * dsp::PI * 0.4 * t); amp *= 1 + 0.5 * (0.6 * sin(2 * dsp::PI * 440 * t) + 0.4 * env * sin(2 * dsp::PI * 1250 * t)); } break;
                case 5: amp *= morseKey(t) ? 1.0 : 0.0; break;
                case 6: { double sy = sin(2 * dsp::PI * 2.7 * t) > 0 ? 1.0 : 0.25; amp *= sy * (0.7 + 0.3 * sin(2 * dsp::PI * 700 * t)); df -= 900 + 300 * sin(2 * dsp::PI * 0.7 * t); } break; // LSB voice-ish
                case 7: { double sy = sin(2 * dsp::PI * 3.1 * t) > 0 ? 1.0 : 0.25; amp *= sy; df += 1100 + 500 * sin(2 * dsp::PI * 0.9 * t); } break;   // USB
                case 3: { double gate = fmod(t, 3.0) < 2.0 ? 1.0 : 0.0; amp *= gate; df += 3000 * sin(2 * dsp::PI * (900 + 300 * sin(2 * dsp::PI * 0.8 * t)) * t); } break;
                case 4: {
                    double L = 0.7 * sin(2 * dsp::PI * 440 * t) * (fmod(t, 2.0) < 1.0 ? 1 : 0.15), R = 0.7 * sin(2 * dsp::PI * 660 * t) * (fmod(t, 2.0) < 1.0 ? 0.15 : 1);
                    double comp = 0.45 * (L + R) + 0.45 * (L - R) * sin(2 * dsp::PI * 38000 * t) + 0.09 * sin(2 * dsp::PI * 19000 * t);
                    df += 75000 * comp;
                } break;
                }
                double w = 2 * dsp::PI * df / fs, cs = cos(ph[s]), sn = sin(ph[s]), wc = cos(w), ws = sin(w);
                for (size_t j = 0; j < CH; j++) {
                    ai[k + j] += (float)(amp * cs); aq[k + j] += (float)(amp * sn);
                    double nc = cs * wc - sn * ws, ns = cs * ws + sn * wc; cs = nc; sn = ns;
                }
                ph[s] = fmod(ph[s] + w * CH, 2 * dsp::PI);
            }
        }
        tsamp += BLK / fs;
        for (size_t k = 0; k < BLK; k++) {
            rng = rng * 1664525u + 1013904223u; float n1 = ((rng >> 8) & 0xFFFF) / 32768.f - 1.f;
            rng = rng * 1664525u + 1013904223u; float n2 = ((rng >> 8) & 0xFFFF) / 32768.f - 1.f;
            float i = ai[k] + 0.02f * n1, q = aq[k] + 0.02f * n2;
            i = std::max(-1.f, std::min(1.f, i)); q = std::max(-1.f, std::min(1.f, q));
            raw[2 * k] = (uint8_t)(i * 127.5f + 127.5f); raw[2 * k + 1] = (uint8_t)(q * 127.5f + 127.5f);
        }
        self->sink(raw.data(), BLK, FMT_U8);
        // pace to real time
        double due = tsamp;
        for (;;) { QueryPerformanceCounter(&now); double el = (double)(now.QuadPart - t0.QuadPart) / fq.QuadPart; if (el >= due || self->stopFlag) break; Sleep(1); }
    }
    return 0;
}



static inline Device* makeDevice(const DevEntry& e) {
    switch (e.kind) {
    case KIND_RTL: return new RtlDev(e.index);
    case KIND_HACKRF: return new HackrfDev(e.serialStr, e.label);
    case KIND_AIRSPY: return new AirspyDev(e.serial64, e.label);
    case KIND_AIRSPYHF: return new AirspyHfDev(e.serial64, e.label);
    default: return new DemoDev();
    }
}
