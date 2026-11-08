// Native test of dsp.h: synthesises RTL-SDR style IQ and checks demodulated audio
#include "dsp.h"
#include <cstdio>
#include <cstdlib>
#include <complex>
#include <chrono>
using namespace dsp;

static const double FS = 2048000.0;
static uint32_t rng = 12345;
static float frand() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xFFFF) / 32768.0f - 1.0f; }

typedef std::vector<std::complex<double>> Sig;

static void toU8(const Sig& s, std::vector<uint8_t>& raw, double noise = 0.004) {
    raw.resize(s.size() * 2);
    for (size_t k = 0; k < s.size(); k++) {
        double i = s[k].real() + noise * frand(), q = s[k].imag() + noise * frand();
        i = std::max(-1.0, std::min(1.0, i)); q = std::max(-1.0, std::min(1.0, q));
        raw[2 * k] = (uint8_t)lround(i * 127.5 + 127.5); raw[2 * k + 1] = (uint8_t)lround(q * 127.5 + 127.5);
    }
}

// run receiver, return stereo audio (after discarding warm-up)
static void run(Receiver& rx, const std::vector<uint8_t>& raw, std::vector<float>& audio) {
    audio.clear();
    const size_t blk = 65536;
    for (size_t off = 0; off + blk <= raw.size(); off += blk) rx.process(&raw[off], blk, audio);
}

static double goertzelDb(const std::vector<float>& a, int ch, double f, size_t start, size_t n) {
    double re = 0, im = 0;
    for (size_t k = 0; k < n; k++) {
        double x = a[2 * (start + k) + ch] * (0.5 - 0.5 * cos(2 * PI * k / n));
        double ph = 2 * PI * f * k / 48000.0; re += x * cos(ph); im -= x * sin(ph);
    }
    double p = (re * re + im * im) / ((double)n * n / 16.0); // amplitude^2 (Hann window: coherent gain 0.5, /2 for cos)
    return 10 * log10(p + 1e-20);
}
static double peakHz(const std::vector<float>& a, int ch, size_t start, size_t n, double lo, double hi) {
    double best = -1e9, bf = 0;
    for (double f = lo; f <= hi; f += 5) { double d = goertzelDb(a, ch, f, start, n); if (d > best) { best = d; bf = f; } }
    return bf;
}

static int fails = 0;
static void check(const char* name, bool ok, const char* fmt = "", double a = 0, double b = 0, double c = 0) {
    char buf[200]; snprintf(buf, sizeof buf, fmt, a, b, c);
    printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", name, buf); if (!ok) fails++;
}

int main() {
    const size_t N = (size_t)(FS * 2.0);
    std::vector<uint8_t> raw; std::vector<float> au; Sig s(N);

    // ---- AM: carrier at +300 kHz offset, 1 kHz tone 60%
    {
        printf("AM\n");
        double fo = 300000; for (size_t k = 0; k < N; k++) { double t = k / FS; double m = 1 + 0.6 * sin(2 * PI * 1000 * t); s[k] = 0.08 * m * std::polar(1.0, 2 * PI * fo * t); }
        toU8(s, raw); Receiver rx; RxConfig c; c.fs = FS; c.mode = M_AM; c.bw = 8000; c.shiftHz = fo; rx.configure(c);
        run(rx, raw, au); size_t n = 32768, st = au.size() / 2 - n - 100;
        double pk = peakHz(au, 0, st, n, 200, 3000), lv = goertzelDb(au, 0, pk, st, n);
        check("AM tone at 1000 Hz", fabs(pk - 1000) < 15, "(got %.0f Hz, %.1f dB)", pk, lv);
        check("AM level sane", lv > -20 && lv < 0, "(%.1f dB)", lv);
        // SAM with a 150 Hz tuning error
        c.mode = M_SAM; c.shiftHz = fo + 150; Receiver rx2; rx2.configure(c); run(rx2, raw, au);
        st = au.size() / 2 - n - 100; pk = peakHz(au, 0, st, n, 200, 3000); lv = goertzelDb(au, 0, pk, st, n);
        check("SAM tone at 1000 Hz (150 Hz off-tune)", fabs(pk - 1000) < 15, "(got %.0f Hz, %.1f dB, lock=%.0f)", pk, lv, (double)rx2.samLock);
    }
    // ---- USB / LSB sideband selectivity
    {
        printf("SSB\n");
        double fo = -400000; // dial frequency offset from centre
        for (size_t k = 0; k < N; k++) { double t = k / FS; s[k] = 0.05 * std::polar(1.0, 2 * PI * (fo + 1500) * t) + 0.05 * std::polar(1.0, 2 * PI * (fo - 1000) * t); }
        toU8(s, raw);
        Receiver rx; RxConfig c; c.fs = FS; c.mode = M_USB; c.bw = 2800; c.shiftHz = fo; rx.configure(c); run(rx, raw, au);
        size_t n = 32768, st = au.size() / 2 - n - 100;
        double u = goertzelDb(au, 0, 1500, st, n), rej = goertzelDb(au, 0, 1000, st, n);
        check("USB passes upper tone (1500 Hz)", u > -20, "(%.1f dB)", u);
        check("USB rejects lower tone", u - rej > 40, "(rejection %.1f dB)", u - rej);
        c.mode = M_LSB; rx.configure(c); run(rx, raw, au); st = au.size() / 2 - n - 100;
        double l = goertzelDb(au, 0, 1000, st, n), rej2 = goertzelDb(au, 0, 1500, st, n);
        check("LSB passes lower tone (1000 Hz)", l > -20, "(%.1f dB)", l);
        check("LSB rejects upper tone", l - rej2 > 40, "(rejection %.1f dB)", l - rej2);
    }
    // ---- CW
    {
        printf("CW\n");
        double fo = 123456; for (size_t k = 0; k < N; k++) { double t = k / FS; s[k] = 0.05 * std::polar(1.0, 2 * PI * fo * t); }
        toU8(s, raw);
        Receiver rx; RxConfig c; c.fs = FS; c.mode = M_CW; c.bw = 300; c.cwPitch = 700; c.shiftHz = fo; rx.configure(c); run(rx, raw, au);
        size_t n = 32768, st = au.size() / 2 - n - 100; double pk = peakHz(au, 0, st, n, 300, 1200);
        check("CW pitch 700 Hz when on carrier", fabs(pk - 700) < 15, "(got %.0f Hz)", pk);
        c.shiftHz = fo + 100; rx.configure(c); run(rx, raw, au); st = au.size() / 2 - n - 100; pk = peakHz(au, 0, st, n, 300, 1200);
        check("CW pitch shifts 100 Hz when tuned 100 Hz off", fabs(pk - 600) < 15, "(got %.0f Hz)", pk);
    }
    // ---- NFM
    {
        printf("NFM\n");
        double fo = 200000, ph = 0; for (size_t k = 0; k < N; k++) { double t = k / FS; ph += 2 * PI * 3000 * sin(2 * PI * 1000 * t) / FS; s[k] = 0.1 * std::polar(1.0, 2 * PI * fo * t + ph); }
        toU8(s, raw);
        Receiver rx; RxConfig c; c.fs = FS; c.mode = M_NFM; c.bw = 12500; c.shiftHz = fo; c.deemph = false; rx.configure(c); run(rx, raw, au);
        size_t n = 32768, st = au.size() / 2 - n - 100; double pk = peakHz(au, 0, st, n, 300, 3000), lv = goertzelDb(au, 0, pk, st, n);
        check("NFM tone at 1000 Hz", fabs(pk - 1000) < 15, "(got %.0f Hz, %.1f dB)", pk, lv);
        check("NFM level sane", lv > -15 && lv < 3, "(%.1f dB)", lv);
    }
    // ---- WFM stereo: L = 1 kHz, R = 2.5 kHz
    {
        printf("WFM stereo\n");
        double fo = -250000, ph = 0;
        for (size_t k = 0; k < N; k++) {
            double t = k / FS, L = 0.8 * sin(2 * PI * 1000 * t), R = 0.8 * sin(2 * PI * 2500 * t);
            double comp = 0.45 * (L + R) + 0.45 * (L - R) * sin(2 * PI * 38000 * t) + 0.09 * sin(2 * PI * 19000 * t);
            ph += 2 * PI * 75000 * comp / FS; s[k] = 0.1 * std::polar(1.0, 2 * PI * fo * t + ph);
        }
        toU8(s, raw);
        Receiver rx; RxConfig c; c.fs = FS; c.mode = M_WFM; c.bw = 180000; c.shiftHz = fo; c.deemph = false; rx.configure(c); run(rx, raw, au);
        size_t n = 32768, st = au.size() / 2 - n - 100;
        double l1 = goertzelDb(au, 0, 1000, st, n), l2 = goertzelDb(au, 0, 2500, st, n), r1 = goertzelDb(au, 1, 1000, st, n), r2 = goertzelDb(au, 1, 2500, st, n);
        check("stereo pilot lock", rx.stereoLock, "");
        check("L has 1 kHz, R has 2.5 kHz", l1 > -12 && r2 > -12, "(L1k %.1f dB, R2.5k %.1f dB)", l1, r2);
        check("channel separation > 25 dB", (l1 - r1) > 25 && (r2 - l2) > 25, "(L:%.1f R:%.1f dB)", l1 - r1, r2 - l2);
    }

    // ---- other sample formats and high sample rates (HackRF / Airspy / Airspy HF+)
    {
        printf("Sample formats / high rates\n");
        struct Case { const char* name; double fs; int fmt; int mode; double secs; } cases[] = {
            { "float32 @ 768 ksps (Airspy HF+)", 768000, 2, M_AM, 2.0 },
            { "float32 @ 10 Msps (Airspy R2)", 10e6, 2, M_AM, 1.2 },
            { "int8    @ 8 Msps (HackRF)", 8e6, 1, M_USB, 1.2 },
            { "int8    @ 20 Msps (HackRF max)", 20e6, 1, M_NFM, 0.8 } };
        for (auto& cs : cases) {
            size_t n = (size_t)(cs.fs * cs.secs); double fo = cs.fs * 0.2, ph = 0;
            std::vector<float> f32; std::vector<int8_t> i8; if (cs.fmt == 2) f32.resize(n * 2); else i8.resize(n * 2);
            double w = 2 * PI * fo / cs.fs, wt = 2 * PI * 1000 / cs.fs, ta = 0, tp = 0;
            for (size_t k = 0; k < n; k++) {
                double env = 1, fm = 0;
                if (cs.mode == M_AM) env = 1 + 0.6 * sin(ta); ta += wt;
                if (cs.mode == M_NFM) { fm = 3000 * sin(tp) * 2 * PI / cs.fs; tp += wt; }
                double fr = fo + (cs.mode == M_USB ? 1500 : 0);
                ph += (cs.mode == M_USB ? 2 * PI * fr / cs.fs : w) + fm;
                double i = 0.08 * env * cos(ph) + 0.004 * frand(), q = 0.08 * env * sin(ph) + 0.004 * frand();
                if (cs.fmt == 2) { f32[2 * k] = (float)i; f32[2 * k + 1] = (float)q; }
                else { i8[2 * k] = (int8_t)lround(i * 127); i8[2 * k + 1] = (int8_t)lround(q * 127); }
            }
            Receiver rx; RxConfig c; c.fs = cs.fs; c.mode = cs.mode; c.bw = cs.mode == M_AM ? 8000 : cs.mode == M_NFM ? 12500 : 2800; c.shiftHz = fo; rx.configure(c);
            std::vector<float> au; const size_t blk = 32768; double busy = 0;
            for (size_t off = 0; off + blk <= n; off += blk) {
                auto t0 = std::chrono::steady_clock::now();
                if (cs.fmt == 2) rx.processF(&f32[2 * off], blk, au); else rx.processI8(&i8[2 * off], blk * 2, au);
                busy += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            }
            size_t nf = au.size() / 2, nn = 32768, st = nf - nn - 100;
            double pk = peakHz(au, 0, st, nn, 300, 3000);
            double want = cs.mode == M_USB ? 1500 : 1000;
            double load = busy / (cs.secs * ((double)(n / blk * blk) / n));
            char info[160]; snprintf(info, sizeof info, "(tone %.0f Hz, CPU load %.0f%% of one core)", pk, load * 100);
            printf("  [%s] %s %s\n", fabs(pk - want) < 15 ? "PASS" : "FAIL", cs.name, info); if (fabs(pk - want) >= 15) fails++;
        }
    }
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", fails);
    return fails != 0;
}
