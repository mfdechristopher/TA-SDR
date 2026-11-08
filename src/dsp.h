// dsp.h - platform independent receiver DSP for RTL-SDR (AM/SAM/USB/LSB/CW/NFM/WFM)
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace dsp {

static const double PI = 3.14159265358979323846;

struct C { float r, i; };

inline double bessel_i0(double x) {
    double s = 1, t = 1, q = x * x / 4;
    for (int k = 1; k < 60; k++) { t *= q / ((double)k * k); s += t; if (t < 1e-12 * s) break; }
    return s;
}

// ---------------------------------------------------------------- FFT
class FFT {
public:
    int N = 0;
    void init(int n) {
        N = n; tw.resize(n / 2); rev.resize(n);
        for (int k = 0; k < n / 2; k++) { tw[k].r = (float)cos(2 * PI * k / n); tw[k].i = (float)-sin(2 * PI * k / n); }
        int lg = 0; while ((1 << lg) < n) lg++;
        for (int i = 0; i < n; i++) { int r = 0; for (int b = 0; b < lg; b++) if (i & (1 << b)) r |= 1 << (lg - 1 - b); rev[i] = r; }
    }
    void forward(C* x) const {
        for (int i = 0; i < N; i++) { int j = rev[i]; if (j > i) std::swap(x[i], x[j]); }
        for (int len = 2; len <= N; len <<= 1) {
            int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; k++) {
                    C w = tw[k * step], a = x[i + k], b = x[i + k + half];
                    C t = { b.r * w.r - b.i * w.i, b.r * w.i + b.i * w.r };
                    x[i + k] = { a.r + t.r, a.i + t.i };
                    x[i + k + half] = { a.r - t.r, a.i - t.i };
                }
        }
    }
private:
    std::vector<C> tw; std::vector<int> rev;
};

// Spectrum estimator: windowed FFT -> dBFS (full-scale complex tone = 0 dB), DC-centred
class Spectrum {
public:
    int N = 0;
    void init(int n) {
        N = n; fft.init(n); win.resize(n); buf.resize(n);
        double sw = 0;
        for (int i = 0; i < n; i++) {
            double a = 2 * PI * i / (n - 1);
            win[i] = (float)(0.35875 - 0.48829 * cos(a) + 0.14128 * cos(2 * a) - 0.01168 * cos(3 * a));
            sw += win[i];
        }
        norm = (float)(1.0 / (sw * sw));
    }
    // iq: N interleaved complex samples. out: N dB values, index 0 = most negative freq
    void compute(const float* iq, float* out) {
        for (int i = 0; i < N; i++) { buf[i].r = iq[2 * i] * win[i]; buf[i].i = iq[2 * i + 1] * win[i]; }
        fft.forward(buf.data());
        for (int k = 0; k < N; k++) {
            int src = (k + N / 2) % N;
            float p = (buf[src].r * buf[src].r + buf[src].i * buf[src].i) * norm;
            out[k] = 10.0f * log10f(p + 1e-20f);
        }
    }
private:
    FFT fft; std::vector<float> win; std::vector<C> buf; float norm = 1;
};

// ---------------------------------------------------------------- filter design
inline std::vector<double> design_lowpass(double fc /*cycles/sample*/, int N, double beta) {
    std::vector<double> h(N);
    double M = (N - 1) / 2.0, sum = 0, i0b = bessel_i0(beta);
    for (int n = 0; n < N; n++) {
        double x = n - M;
        double s = (fabs(x) < 1e-9) ? 2 * fc : sin(2 * PI * fc * x) / (PI * x);
        double r = (M > 0) ? x / M : 0;
        double w = bessel_i0(beta * sqrt(std::max(0.0, 1 - r * r))) / i0b;
        h[n] = s * w; sum += h[n];
    }
    for (auto& v : h) v /= sum;
    return h;
}

// Decimating FIR on split I/Q arrays; real taps or complex taps (hi non-empty)
class DecimFir {
public:
    int D = 1, N = 1;
    void initReal(int d, const std::vector<double>& h) {
        D = d; N = (int)h.size(); hr.assign(h.begin(), h.end()); hi.clear(); reset();
    }
    void initComplex(int d, const std::vector<double>& h, double fcen /*cycles/sample*/) {
        D = d; N = (int)h.size(); hr.resize(N); hi.resize(N);
        double M = (N - 1) / 2.0;
        for (int n = 0; n < N; n++) {
            double ph = 2 * PI * fcen * (n - M);
            // stored reversed: process() correlates, so this yields true convolution
            hr[N - 1 - n] = (float)(h[n] * cos(ph)); hi[N - 1 - n] = (float)(h[n] * sin(ph));
        }
        reset();
    }
    void reset() { bi.assign(N - 1, 0.f); bq.assign(N - 1, 0.f); pos = 0; }
    // returns number of outputs written
    size_t process(const float* ii, const float* qq, size_t n, float* oi, float* oq) {
        size_t hist = N - 1, len = hist + n;
        if (bi.size() < len) { bi.resize(len); bq.resize(len); }
        memcpy(&bi[hist], ii, n * sizeof(float)); memcpy(&bq[hist], qq, n * sizeof(float));
        size_t cnt = 0, p = pos;
        const float* h = hr.data();
        if (hi.empty()) {
            for (; p + N <= len; p += D) {
                const float* a = &bi[p]; const float* b = &bq[p];
                float si = 0, sq = 0;
                for (int k = 0; k < N; k++) { si += h[k] * a[k]; sq += h[k] * b[k]; }
                oi[cnt] = si; oq[cnt] = sq; cnt++;
            }
        } else {
            const float* g = hi.data();
            for (; p + N <= len; p += D) {
                const float* a = &bi[p]; const float* b = &bq[p];
                float ri = 0, rq = 0, ci = 0, cq = 0;
                for (int k = 0; k < N; k++) { ri += h[k] * a[k]; rq += h[k] * b[k]; ci += g[k] * a[k]; cq += g[k] * b[k]; }
                oi[cnt] = ri - cq; oq[cnt] = rq + ci; cnt++;
            }
        }
        pos = p - (len - hist);
        memmove(bi.data(), &bi[len - hist], hist * sizeof(float));
        memmove(bq.data(), &bq[len - hist], hist * sizeof(float));
        return cnt;
    }
private:
    std::vector<float> hr, hi, bi, bq; size_t pos = 0;
};

// Arbitrary-ratio windowed-sinc resampler with built-in low-pass (one channel)
class Resampler {
public:
    void init(double rin, double rout, double cutoffHz, double lobes) {
        base = rin / rout; step = base;
        double g = std::min(1.0, 2 * cutoffHz / rin);
        L = std::max(4, (int)ceil(lobes / g));
        tab.assign((size_t)(P + 1) * 2 * L, 0.f);
        double i0b = bessel_i0(8.0);
        for (int p = 0; p <= P; p++) {
            double fr = (double)p / P, sum = 0;
            double* tmp = new double[2 * L];
            for (int k = 0; k < 2 * L; k++) {
                double t = (k - L + 1) - fr;
                double x = g * t;
                double s = (fabs(x) < 1e-9) ? g : g * sin(PI * x) / (PI * x);
                double r = t / L;
                double w = (fabs(r) >= 1) ? 0 : bessel_i0(8.0 * sqrt(1 - r * r)) / i0b;
                tmp[k] = s * w; sum += tmp[k];
            }
            for (int k = 0; k < 2 * L; k++) tab[(size_t)p * 2 * L + k] = (float)(tmp[k] / sum);
            delete[] tmp;
        }
        buf.assign(L, 0.f); pos = (double)(L - 1) + 1.0; // index of sample "0" is L
        pos = L;
    }
    void setAdjust(double adj) { step = base * (1.0 + adj); }
    // appends input, writes output samples; returns count
    size_t process(const float* in, size_t n, std::vector<float>& out) {
        size_t old = buf.size(); buf.resize(old + n);
        memcpy(&buf[old], in, n * sizeof(float));
        size_t cnt = 0, size = buf.size();
        const int T = 2 * L;
        for (;;) {
            long long phi = (long long)floor(pos * P + 0.5);
            long long i = phi / P; int ph = (int)(phi - i * P);
            if (i + L >= (long long)size) break;
            const float* x = &buf[(size_t)(i - L + 1)];
            const float* h = &tab[(size_t)ph * T];
            float s = 0;
            for (int k = 0; k < T; k++) s += h[k] * x[k];
            out.push_back(s); cnt++;
            pos += step;
        }
        long long drop = (long long)floor(pos) - L + 1;
        if (drop > 0) {
            buf.erase(buf.begin(), buf.begin() + drop);
            pos -= (double)drop;
        }
        return cnt;
    }
private:
    static const int P = 512;
    int L = 8; double base = 1, step = 1, pos = 0; std::vector<float> tab, buf;
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void highpass(double fc, double fs, double q = 0.7071) {
        double w = 2 * PI * fc / fs, al = sin(w) / (2 * q), c = cos(w), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void bandpass(double fc, double fs, double q) {
        double w = 2 * PI * fc / fs, al = sin(w) / (2 * q), c = cos(w), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    inline float run(float x) {
        double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float)y;
    }
    void clear() { z1 = z2 = 0; }
};

enum Mode { M_AM = 0, M_SAM, M_USB, M_LSB, M_CW, M_NFM, M_WFM, M_COUNT };

struct RxConfig {
    double fs = 2048000;
    int mode = M_USB;
    double shiftHz = 0;     // tuned frequency minus hardware centre frequency
    double bw = 2800;       // channel bandwidth (Hz)
    double cwPitch = 700;
    bool invert = false;    // conjugate input (spectrum mirror)
    int agcMode = 2;        // 0 off, 1 fast, 2 medium, 3 slow
    bool deemph = true; double deemphUs = 75;
    bool stereo = true;
    bool nb = false;
    double squelchDb = -200;
    double volume = 0.5;
};

class Receiver {
public:
    static const int RING = 65536;
    double stereoPhase = 0; // radians, trim of recovered 38 kHz phase
    float meterDb = -120; bool stereoLock = false; bool sqOpen = true; double pllHz = 0; bool samLock = false;

    Receiver() { for (int i = 0; i < 256; i++) lut[i] = (i - 127.5f) / 127.5f; ring.assign(RING * 2, 0.f); }

    void configure(const RxConfig& c) {
        bool rebuild = !built || c.fs != cfg.fs || c.mode != cfg.mode || c.bw != cfg.bw || c.cwPitch != cfg.cwPitch;
        cfg = c;
        setNco();
        setAgc();
        deA = 1 - exp(-1.0 / (48000.0 * cfg.deemphUs * 1e-6));
        if (rebuild) build();
    }
    void setRateAdjust(double a) { if (built) { rsA.setAdjust(a); rsB.setAdjust(a); } }

    // Ingest raw unsigned 8-bit IQ (RTL-SDR). Audio frames (interleaved stereo) appended to out.
    size_t process(const uint8_t* raw, size_t nbytes, std::vector<float>& out) {
        if (!built) return 0;
        size_t n = nbytes / 2; ensure(n);
        for (size_t k = 0; k < n; k++) { bufI[k] = lut[raw[2 * k]]; bufQ[k] = lut[raw[2 * k + 1]]; }
        return core(n, out);
    }
    // Signed 8-bit IQ (HackRF)
    size_t processI8(const int8_t* raw, size_t nbytes, std::vector<float>& out) {
        if (!built) return 0;
        size_t n = nbytes / 2; ensure(n);
        const float sc = 1.0f / 128.0f;
        for (size_t k = 0; k < n; k++) { bufI[k] = raw[2 * k] * sc; bufQ[k] = raw[2 * k + 1] * sc; }
        return core(n, out);
    }
    // Interleaved float32 IQ (Airspy, Airspy HF+), nsamples = complex samples
    size_t processF(const float* iq, size_t nsamples, std::vector<float>& out) {
        if (!built) return 0;
        size_t n = nsamples; ensure(n);
        for (size_t k = 0; k < n; k++) { bufI[k] = iq[2 * k]; bufQ[k] = iq[2 * k + 1]; }
        return core(n, out);
    }

private:
    void ensure(size_t n) { if (bufI.size() < n) { bufI.resize(n); bufQ.resize(n); mixI.resize(n); mixQ.resize(n); } }
    size_t core(size_t n, std::vector<float>& out) {
        const float a = 5e-5f;
        for (size_t k = 0; k < n; k++) {
            float i = bufI[k], q = bufQ[k];
            dcI += a * (i - dcI); dcQ += a * (q - dcQ);
            i -= dcI; q -= dcQ;
            if (cfg.invert) q = -q;
            bufI[k] = i; bufQ[k] = q;
        }
        if (cfg.nb) {
            for (size_t k = 0; k < n; k++) {
                float p = bufI[k] * bufI[k] + bufQ[k] * bufQ[k];
                if (nbHold > 0) { bufI[k] = bufQ[k] = 0; nbHold--; continue; }
                if (p > 12.0f * nbAvg + 1e-6f) { bufI[k] = bufQ[k] = 0; nbHold = 3; }
                else nbAvg += 0.002f * (p - nbAvg);
            }
        }
        // spectrum ring (last samples)
        size_t st = n > (size_t)RING ? n - RING : 0;
        for (size_t k = st; k < n; k++) { ring[2 * wpos] = bufI[k]; ring[2 * wpos + 1] = bufQ[k]; wpos = (wpos + 1) & (RING - 1); }
        // NCO mix to baseband
        double wr = cos(nco_w), wi = -sin(nco_w);   // e^{-j w}
        double pr = ncoR, pi_ = ncoI;
        for (size_t k = 0; k < n; k++) {
            float c = (float)pr, s = (float)pi_;
            float i = bufI[k], q = bufQ[k];
            mixI[k] = i * c - q * s; mixQ[k] = i * s + q * c;
            double nr = pr * wr - pi_ * wi, ni = pr * wi + pi_ * wr; pr = nr; pi_ = ni;
        }
        double m = sqrt(pr * pr + pi_ * pi_); ncoR = pr / m; ncoI = pi_ / m;
        // stage 1: decimate
        size_t maxo = n / dec1.D + 4;
        if (s1I.size() < maxo) { s1I.resize(maxo); s1Q.resize(maxo); s2I.resize(maxo); s2Q.resize(maxo); aud0.resize(maxo); aud1.resize(maxo); }
        size_t n1 = dec1.process(mixI.data(), mixQ.data(), n, s1I.data(), s1Q.data());
        size_t n2 = chan.process(s1I.data(), s1Q.data(), n1, s2I.data(), s2Q.data());
        if (n2 == 0) return 0;
        // channel power
        double pw = 0;
        for (size_t k = 0; k < n2; k++) pw += s2I[k] * s2I[k] + s2Q[k] * s2Q[k];
        pw /= n2;
        meterDb = (float)(10 * log10(pw + 1e-12));
        sqSm = 0.7 * sqSm + 0.3 * pw;
        double sqdb = 10 * log10(sqSm + 1e-12);
        if (sqOpen) { if (sqdb < cfg.squelchDb - 1.5) sqOpen = false; } else { if (sqdb > cfg.squelchDb) sqOpen = true; }

        demod(n2);
        // resample + post
        tmpA.clear(); tmpB.clear();
        rsA.process(aud0.data(), n2, tmpA);
        if (cfg.mode == M_WFM) rsB.process(aud1.data(), n2, tmpB);
        size_t no = tmpA.size();
        for (size_t k = 0; k < no; k++) {
            float l, r;
            if (cfg.mode == M_WFM) {
                float mid = tmpA[k], dif = (k < tmpB.size()) ? tmpB[k] : 0.f;
                float b = (float)stereoBlend * (cfg.stereo ? 1.f : 0.f);
                l = (mid + b * dif) * 1.1f; r = (mid - b * dif) * 1.1f;
                if (cfg.deemph) { deL += (float)deA * (l - deL); deR += (float)deA * (r - deR); l = deL; r = deR; }
                dcL += 0.0004f * (l - dcL); dcR += 0.0004f * (r - dcR); l -= dcL; r -= dcR;
            } else {
                float v = tmpA[k];
                if (cfg.mode == M_NFM) {
                    if (cfg.deemph) { deL += (float)deA * (v - deL); v = deL; }
                    v = hpf.run(v);
                }
                l = r = v;
            }
            float tg = sqOpen ? 1.f : 0.f;
            gate += 0.002f * (tg - gate);
            float g = gate * (float)cfg.volume * 2.0f;
            l = tanhf(l * g); r = tanhf(r * g);
            out.push_back(l); out.push_back(r);
        }
        tmpB.clear();
        return no;
    }

public:
    void snapshot(float* iq, int nsamp) {
        if (nsamp > RING) nsamp = RING;
        int start = (wpos - nsamp) & (RING - 1);
        for (int k = 0; k < nsamp; k++) { int p = (start + k) & (RING - 1); iq[2 * k] = ring[2 * p]; iq[2 * k + 1] = ring[2 * p + 1]; }
    }
    double channelRate() const { return Rc; }
    // passband edges relative to tuned frequency (Hz), for display
    void passband(double& lo, double& hi) const {
        switch (cfg.mode) {
        case M_USB: lo = ssbLo; hi = ssbLo + cfg.bw; break;
        case M_LSB: hi = -ssbLo; lo = -ssbLo - cfg.bw; break;
        default: lo = -cfg.bw / 2; hi = cfg.bw / 2; break;
        }
    }

private:
    RxConfig cfg; bool built = false;
    float lut[256]; float dcI = 0, dcQ = 0, nbAvg = 0; int nbHold = 0;
    std::vector<float> bufI, bufQ, mixI, mixQ, s1I, s1Q, s2I, s2Q, aud0, aud1, tmpA, tmpB, ring;
    int wpos = 0;
    double nco_w = 0, ncoR = 1, ncoI = 0;
    DecimFir dec1, chan; double Rc = 48000; double ssbLo = 150;
    Resampler rsA, rsB;
    // AGC
    double env = 0.01, agcAtt = 0.1, agcDec = 0.001, agcTarget = 0.5, gMan = 4.0;
    double dcAm = 0;
    // NFM / WFM
    float pI = 1, pQ = 0; double angScale = 1;
    double deA = 0.24; float deL = 0, deR = 0, dcL = 0, dcR = 0; Biquad hpf;
    Biquad pilotBpf; double pllTh = 0, pllF = 0, pllW0 = 0, pllA = 0, pllB = 0, pilotAmp = 0, stereoBlend = 0;
    // SAM
    double samTh = 0, samF = 0, samErr = 1; bool samLocked = false;
    double sqSm = 0; float gate = 1;

    void setNco() {
        double sh = cfg.shiftHz;
        if (cfg.mode == M_CW) sh -= cfg.cwPitch;
        nco_w = 2 * PI * sh / cfg.fs;
    }
    void setAgc() {
        double rc = Rc > 0 ? Rc : 48000;
        double att = (cfg.mode == M_AM || cfg.mode == M_SAM) ? 0.020 : 0.004;
        double dec = cfg.agcMode == 1 ? 0.08 : cfg.agcMode == 3 ? 1.5 : 0.4;
        if (cfg.mode == M_CW && cfg.agcMode != 3) dec *= 0.5;
        agcAtt = 1 - exp(-1.0 / (rc * att)); agcDec = 1 - exp(-1.0 / (rc * dec));
    }
    void build() {
        double fs = cfg.fs, rt;
        switch (cfg.mode) { case M_WFM: rt = 320e3; break; case M_NFM: rt = 80e3; break; case M_AM: case M_SAM: rt = 48e3; break; default: rt = 16e3; }
        int D = std::max(1, (int)floor(fs / rt + 0.5));
        Rc = fs / D;
        if (D == 1) dec1.initReal(1, std::vector<double>(1, 1.0));
        else {
            int N = 17 * D; if (N % 2 == 0) N++;
            dec1.initReal(D, design_lowpass(0.5 / D, N, 7.0));
        }
        // channel filter
        double bw = std::max(50.0, std::min(cfg.bw, 0.8 * Rc));
        double trans, fcen = 0;
        bool cplx = false;
        switch (cfg.mode) {
        case M_USB: case M_LSB: ssbLo = 150; trans = std::max(0.12 * bw, 80.0); cplx = true; fcen = (cfg.mode == M_USB ? 1 : -1) * (ssbLo + bw / 2); break;
        case M_CW: trans = std::max(0.2 * bw, 40.0); cplx = true; fcen = cfg.cwPitch; break;
        default: trans = std::max(0.2 * bw, 100.0); break;
        }
        int N = (int)(4.32 * Rc / trans); if (N % 2 == 0) N++; N = std::max(31, std::min(N, 6001));
        std::vector<double> h = design_lowpass(0.5 * bw / Rc, N, 7.0);
        if (cplx) chan.initComplex(1, h, fcen / Rc); else chan.initReal(1, h);
        // audio resamplers
        double fa = 48000, cut, lobes = 8;
        switch (cfg.mode) {
        case M_NFM: cut = 4000; break; case M_WFM: cut = 15000; lobes = 14; break;
        case M_AM: case M_SAM: cut = std::max(1500.0, bw / 2); break;
        case M_CW: cut = cfg.cwPitch + bw / 2 + 300; break;
        default: cut = ssbLo + bw + 300; break;
        }
        cut = std::min(cut, 0.45 * std::min(Rc, fa));
        rsA.init(Rc, fa, cut, lobes); rsB.init(Rc, fa, cut, lobes);
        angScale = Rc / (2 * PI * (cfg.mode == M_WFM ? 75000.0 : 5000.0));
        pI = 1; pQ = 0;
        hpf.highpass(300, fa); hpf.clear();
        pilotBpf.bandpass(19000, Rc, 25); pilotBpf.clear();
        pllTh = 0; pllF = 0; pllW0 = 2 * PI * 19000 / Rc;
        double wn = 2 * PI * 20 / Rc; pllA = 2 * 0.707 * wn; pllB = wn * wn;
        pilotAmp = 0; stereoBlend = 0; samTh = 0; samF = 0; samLocked = false;
        env = 0.01; dcAm = 0; deL = deR = dcL = dcR = 0;
        built = true;
        setAgc();
    }

    void demod(size_t n) {
        const double tgt = agcTarget, floorLvl = 2e-3;
        switch (cfg.mode) {
        case M_AM: {
            double adc = 2 * PI * 15 / Rc;
            for (size_t k = 0; k < n; k++) {
                float mag = sqrtf(s2I[k] * s2I[k] + s2Q[k] * s2Q[k]);
                double g = gMan;
                if (cfg.agcMode) { env += (mag > env ? agcAtt : agcDec) * (mag - env); g = tgt / std::max(env, floorLvl); }
                double o = mag * g; dcAm += adc * (o - dcAm); aud0[k] = (float)(o - dcAm);
            }
        } break;
        case M_SAM: {
            double adc = 2 * PI * 15 / Rc;
            for (size_t k = 0; k < n; k++) {
                float c = (float)cos(samTh), s = (float)-sin(samTh);
                float zr = s2I[k] * c - s2Q[k] * s, zi = s2I[k] * s + s2Q[k] * c;
                float mag = sqrtf(zr * zr + zi * zi) + 1e-9f;
                double err = zi / mag;
                samErr += 0.0005 * (fabs(err) - samErr);
                if (samLocked) { if (samErr > 0.5) samLocked = false; } else { if (samErr < 0.2) samLocked = true; }
                double bwl = samLocked ? 40.0 : 250.0, wn = 2 * PI * bwl / Rc;
                double A = 2 * 0.9 * wn, B = wn * wn;
                samF += B * err; samTh += samF + A * err;
                if (samTh > PI) samTh -= 2 * PI; else if (samTh < -PI) samTh += 2 * PI;
                double g = gMan;
                if (cfg.agcMode) { env += (mag > env ? agcAtt : agcDec) * (mag - env); g = tgt / std::max(env, floorLvl); }
                double o = zr * g; dcAm += adc * (o - dcAm); aud0[k] = (float)(o - dcAm);
            }
            samLock = samLocked; pllHz = samF * Rc / (2 * PI);
        } break;
        case M_USB: case M_LSB: case M_CW: {
            for (size_t k = 0; k < n; k++) {
                float mag = sqrtf(s2I[k] * s2I[k] + s2Q[k] * s2Q[k]);
                double g = gMan;
                if (cfg.agcMode) { env += (mag > env ? agcAtt : agcDec) * (mag - env); g = tgt / std::max(env, floorLvl); }
                aud0[k] = (float)(s2I[k] * g);
            }
        } break;
        case M_NFM: {
            for (size_t k = 0; k < n; k++) {
                float dr = s2I[k] * pI + s2Q[k] * pQ, di = s2Q[k] * pI - s2I[k] * pQ;
                pI = s2I[k]; pQ = s2Q[k];
                aud0[k] = (float)(atan2f(di, dr) * angScale * 0.5);
            }
        } break;
        case M_WFM: {
            double ampLp = pilotAmp;
            for (size_t k = 0; k < n; k++) {
                float dr = s2I[k] * pI + s2Q[k] * pQ, di = s2Q[k] * pI - s2I[k] * pQ;
                pI = s2I[k]; pQ = s2Q[k];
                float x = (float)(atan2f(di, dr) * angScale);
                float p = pilotBpf.run(x);
                double th = pllTh;   // phase aligned with this sample (before loop update)
                double err = p * cos(th) / 0.05;
                if (err > 4) err = 4; else if (err < -4) err = -4;
                pllF += pllB * err; pllTh += pllW0 + pllF + pllA * err;
                if (pllTh > 2 * PI) pllTh -= 2 * PI;
                ampLp += 0.0002 * (fabs(p) * (PI / 2) - ampLp);
                aud0[k] = x;
                aud1[k] = x * 2.0f * (float)sin(2 * th + stereoPhase);
            }
            pilotAmp = ampLp;
            double want = std::max(0.0, std::min(1.0, (pilotAmp - 0.025) / 0.02));
            stereoBlend += 0.2 * (want - stereoBlend);
            stereoLock = stereoBlend > 0.5; pllHz = pllF * Rc / (2 * PI);
        } break;
        }
    }
};

} // namespace dsp
