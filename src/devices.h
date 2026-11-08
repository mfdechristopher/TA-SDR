// devices.h - one interface for every supported SDR front end
//   RTL-SDR (incl. Blog V3/V4), HackRF One, Airspy (R2/Mini), Airspy HF+ (Discovery) + demo source (demo.h)
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include <rtl-sdr.h>
#include "hackrf.h"
#include "airspy.h"
#include "airspyhf.h"

enum { FMT_U8 = 0, FMT_I8 = 1, FMT_F32 = 2 };                 // sample formats delivered to the sink
enum { KIND_RTL = 0, KIND_HACKRF, KIND_AIRSPY, KIND_AIRSPYHF, KIND_DEMO };
typedef void (*SampleSink)(const void* data, size_t complexSamples, int fmt);

struct Device {
    virtual ~Device() {}
    virtual int kind() = 0;
    virtual bool open(std::string& err) = 0;
    virtual void close() = 0;
    virtual std::vector<double> rates() = 0;                   // complex sample rates offered (Hz)
    virtual bool setRate(double r) = 0;
    virtual bool setCenter(double hz) = 0;
    virtual double centerError() { return 0; }                 // actual minus requested centre (Hz)
    virtual double minHz() { return 1000; }
    virtual double maxHz() = 0;
    virtual bool validCenter(double hz) { return hz >= minHz() && hz <= maxHz(); }
    virtual int gainSteps() { return 0; }                      // 0 = no manual gain control
    virtual std::string gainText(int) { return "-"; }
    virtual void setGain(int) {}
    virtual bool hasAgc() { return false; }
    virtual void setAgc(bool) {}
    virtual const char* extraName() { return nullptr; }        // device-specific on/off switch
    virtual void setExtra(bool) {}
    virtual bool hasBias() { return false; }
    virtual void setBias(bool) {}
    virtual bool hasPpm() { return false; }
    virtual void setPpm(int) {}
    virtual bool hasHf() { return false; }
    virtual void setHf(int) {}
    virtual bool start(SampleSink sink) = 0;
    virtual void stop() = 0;
    virtual bool alive() { return true; }
    virtual std::string desc() = 0;
};

// ===================================================================== RTL-SDR
struct RtlDev : Device {
    int idx; rtlsdr_dev_t* dev = nullptr; HANDLE th = 0; SampleSink sink = nullptr; volatile LONG stopping = 0, running = 0;
    int tuner = 0; std::vector<int> gains; std::string name; int hf = 0;
    RtlDev(int i) : idx(i) {}
    int kind() override { return KIND_RTL; }
    bool open(std::string& err) override {
        if (rtlsdr_open(&dev, idx) < 0) { dev = nullptr; err = "Could not open the RTL-SDR. Is another program using it? Bind the WinUSB driver with Zadig (Bulk-In Interface 0)."; return false; }
        tuner = rtlsdr_get_tuner_type(dev);
        int n = rtlsdr_get_tuner_gains(dev, NULL); gains.clear();
        if (n > 0) { gains.resize(n); rtlsdr_get_tuner_gains(dev, gains.data()); }
        rtlsdr_set_direct_sampling(dev, hf);
        const char* tn[] = { "unknown", "E4000", "FC0012", "FC0013", "FC2580", "R820T/R820T2", "R828D" };
        char man[256] = "", prod[256] = "", ser[256] = ""; rtlsdr_get_usb_strings(dev, man, prod, ser);
        char b[300]; snprintf(b, sizeof b, "%s (%s)", prod, tn[tuner < 7 ? tuner : 0]); name = b; return true;
    }
    void close() override { if (dev) { rtlsdr_close(dev); dev = nullptr; } }
    std::vector<double> rates() override { return { 250000, 1024000, 1400000, 1800000, 1920000, 2048000, 2400000, 2560000, 2800000, 3200000 }; }
    bool setRate(double r) override { if (!dev) return true; bool ok = rtlsdr_set_sample_rate(dev, (uint32_t)r) >= 0; rtlsdr_set_tuner_bandwidth(dev, 0); return ok; }
    bool setCenter(double hz) override { return !dev || rtlsdr_set_center_freq(dev, (uint32_t)hz) >= 0; }
    double centerError() override { return dev ? rtlsdr_get_tuner_freq_error(dev) : 0; }
    double maxHz() override {
        switch (tuner) { case RTLSDR_TUNER_E4000: return 2200e6; case RTLSDR_TUNER_FC0012: return 948e6; case RTLSDR_TUNER_FC0013: return 1100e6;
        case RTLSDR_TUNER_FC2580: return 924e6; default: return 1766e6; }
    }
    int gainSteps() override { return gains.empty() ? 29 : (int)gains.size(); }
    std::string gainText(int i) override { if (gains.empty()) return "-"; i = std::max(0, std::min((int)gains.size() - 1, i)); char b[32]; snprintf(b, sizeof b, "%.1f dB", gains[i] / 10.0); return b; }
    void setGain(int i) override { if (dev && !gains.empty()) { rtlsdr_set_tuner_gain_mode(dev, 1); rtlsdr_set_tuner_gain(dev, gains[std::max(0, std::min((int)gains.size() - 1, i))]); } }
    bool hasAgc() override { return true; }
    void setAgc(bool on) override { if (dev && on) rtlsdr_set_tuner_gain_mode(dev, 0); }
    const char* extraName() override { return "RTL AGC"; }
    void setExtra(bool on) override { if (dev) rtlsdr_set_agc_mode(dev, on ? 1 : 0); }
    bool hasBias() override { return true; }
    void setBias(bool on) override { if (dev) rtlsdr_set_bias_tee(dev, on ? 1 : 0); }
    bool hasPpm() override { return true; }
    void setPpm(int p) override { if (dev) rtlsdr_set_freq_correction(dev, p); }
    bool hasHf() override { return true; }
    void setHf(int m) override { hf = m; if (dev) rtlsdr_set_direct_sampling(dev, m); }
    static void cb(unsigned char* buf, uint32_t len, void* ctx) { RtlDev* d = (RtlDev*)ctx; if (!d->stopping) d->sink(buf, len / 2, FMT_U8); }
    static DWORD WINAPI thread(LPVOID p) { RtlDev* d = (RtlDev*)p; rtlsdr_read_async(d->dev, cb, d, 0, 65536); InterlockedExchange(&d->running, 0); return 0; }
    bool start(SampleSink s) override { sink = s; stopping = 0; running = 1; rtlsdr_reset_buffer(dev); th = CreateThread(0, 0, thread, this, 0, 0); return th != 0; }
    void stop() override { stopping = 1; if (dev) rtlsdr_cancel_async(dev); if (th) { WaitForSingleObject(th, 3000); CloseHandle(th); th = 0; } running = 0; }
    bool alive() override { return running != 0; }
    std::string desc() override { return name; }
};

// ===================================================================== HackRF One
struct HackrfDev : Device {
    std::string serial, label; hackrf_device* dev = nullptr; SampleSink sink = nullptr; bool bias = false, amp = false; int gainIdx = 20;
    HackrfDev(const std::string& s, const std::string& l) : serial(s), label(l) {}
    int kind() override { return KIND_HACKRF; }
    bool open(std::string& err) override {
        hackrf_init();
        if (hackrf_open_by_serial(serial.c_str(), &dev) != HACKRF_SUCCESS) { dev = nullptr; err = "Could not open the HackRF. Is another program using it? Bind WinUSB with Zadig."; return false; }
        return true;
    }
    void close() override { if (dev) { hackrf_close(dev); dev = nullptr; } }
    std::vector<double> rates() override { return { 2e6, 4e6, 5e6, 8e6, 10e6, 16e6, 20e6 }; }
    bool setRate(double r) override { return !dev || hackrf_set_sample_rate(dev, r) == HACKRF_SUCCESS; }
    bool setCenter(double hz) override { return !dev || hackrf_set_freq(dev, (uint64_t)hz) == HACKRF_SUCCESS; }
    double minHz() override { return 1e6; }
    double maxHz() override { return 6000e6; }
    // slider = total gain in 2 dB steps, split between the LNA (0-40 in 8 dB steps) and VGA (0-62 in 2 dB steps)
    int gainSteps() override { return 52; }
    void split(int i, int& lna, int& vga) { int t = i * 2; lna = t >= 40 ? 40 : (t / 8) * 8; vga = std::min(62, t - lna); }
    std::string gainText(int i) override { int l, v; split(i, l, v); char b[48]; snprintf(b, sizeof b, "%d dB", l + v); return b; }
    void setGain(int i) override { gainIdx = i; if (!dev) return; int l, v; split(i, l, v); hackrf_set_lna_gain(dev, l); hackrf_set_vga_gain(dev, v); }
    const char* extraName() override { return "RF amp"; }
    void setExtra(bool on) override { amp = on; if (dev) hackrf_set_amp_enable(dev, on ? 1 : 0); }
    bool hasBias() override { return true; }
    void setBias(bool on) override { bias = on; if (dev && hackrf_is_streaming(dev) == HACKRF_TRUE) hackrf_set_antenna_enable(dev, on ? 1 : 0); }
    static int cb(hackrf_transfer* t) { HackrfDev* d = (HackrfDev*)t->rx_ctx; d->sink(t->buffer, t->valid_length / 2, FMT_I8); return 0; }
    bool start(SampleSink s) override {
        sink = s; if (!dev) return false;
        bool ok = hackrf_start_rx(dev, cb, this) == HACKRF_SUCCESS;
        if (ok && bias) hackrf_set_antenna_enable(dev, 1);   // firmware drops bias power while idle, so enable after starting
        return ok;
    }
    void stop() override { if (dev) hackrf_stop_rx(dev); }
    bool alive() override { return dev && hackrf_is_streaming(dev) == HACKRF_TRUE; }
    std::string desc() override { return label; }
};

// ===================================================================== Airspy (R2 / Mini)
struct AirspyDev : Device {
    uint64_t serial; std::string label; airspy_device* dev = nullptr; SampleSink sink = nullptr; std::vector<double> rt; int gainIdx = 10; bool agc = false;
    AirspyDev(uint64_t s, const std::string& l) : serial(s), label(l) { rt = { 2.5e6, 3e6, 6e6, 10e6 }; }
    int kind() override { return KIND_AIRSPY; }
    bool open(std::string& err) override {
        if (airspy_open_sn(&dev, serial) != AIRSPY_SUCCESS) { dev = nullptr; err = "Could not open the Airspy. Is another program using it? Bind WinUSB with Zadig."; return false; }
        airspy_set_sample_type(dev, AIRSPY_SAMPLE_FLOAT32_IQ);
        uint32_t cnt = 0; airspy_get_samplerates(dev, &cnt, 0);
        if (cnt > 0 && cnt < 32) { std::vector<uint32_t> r(cnt); airspy_get_samplerates(dev, r.data(), cnt); rt.assign(r.begin(), r.end()); std::sort(rt.begin(), rt.end()); }
        return true;
    }
    void close() override { if (dev) { airspy_close(dev); dev = nullptr; } }
    std::vector<double> rates() override { return rt; }
    bool setRate(double r) override { return !dev || airspy_set_samplerate(dev, (uint32_t)r) == AIRSPY_SUCCESS; }
    bool setCenter(double hz) override { return !dev || airspy_set_freq(dev, (uint32_t)hz) == AIRSPY_SUCCESS; }
    double minHz() override { return 24e6; }
    double maxHz() override { return 1750e6; }
    int gainSteps() override { return 22; }
    std::string gainText(int i) override { char b[32]; snprintf(b, sizeof b, "Sens %d", i); return b; }
    void setGain(int i) override { gainIdx = i; if (dev) airspy_set_sensitivity_gain(dev, (uint8_t)i); }
    bool hasAgc() override { return true; }
    void setAgc(bool on) override { agc = on; if (dev) { airspy_set_lna_agc(dev, on); airspy_set_mixer_agc(dev, on); if (!on) airspy_set_sensitivity_gain(dev, (uint8_t)gainIdx); } }
    bool hasBias() override { return true; }
    void setBias(bool on) override { if (dev) airspy_set_rf_bias(dev, on ? 1 : 0); }
    static int cb(airspy_transfer* t) { AirspyDev* d = (AirspyDev*)t->ctx; d->sink(t->samples, (size_t)t->sample_count, FMT_F32); return 0; }
    bool start(SampleSink s) override { sink = s; return dev && airspy_start_rx(dev, cb, this) == AIRSPY_SUCCESS; }
    void stop() override { if (dev) airspy_stop_rx(dev); }
    bool alive() override { return dev && airspy_is_streaming(dev) == 1; }
    std::string desc() override { return label; }
};

// ===================================================================== Airspy HF+ (Discovery / Ranger)
struct AirspyHfDev : Device {
    uint64_t serial; std::string label; airspyhf_device_t* dev = nullptr; SampleSink sink = nullptr; std::vector<double> rt; bool lna = false;
    AirspyHfDev(uint64_t s, const std::string& l) : serial(s), label(l) { rt = { 192e3, 256e3, 384e3, 768e3 }; }
    int kind() override { return KIND_AIRSPYHF; }
    bool open(std::string& err) override {
        if (airspyhf_open_sn(&dev, serial) != AIRSPYHF_SUCCESS) { dev = nullptr; err = "Could not open the Airspy HF+. Is another program using it? Bind WinUSB with Zadig."; return false; }
        uint32_t cnt = 0; airspyhf_get_samplerates(dev, &cnt, 0);
        if (cnt > 0 && cnt < 32) { std::vector<uint32_t> r(cnt); airspyhf_get_samplerates(dev, r.data(), cnt); rt.assign(r.begin(), r.end()); std::sort(rt.begin(), rt.end()); }
        airspyhf_set_hf_agc(dev, 1); return true;
    }
    void close() override { if (dev) { airspyhf_close(dev); dev = nullptr; } }
    std::vector<double> rates() override { return rt; }
    bool setRate(double r) override { return !dev || airspyhf_set_samplerate(dev, (uint32_t)r) == AIRSPYHF_SUCCESS; }
    bool setCenter(double hz) override { return !dev || airspyhf_set_freq(dev, (uint32_t)hz) == AIRSPYHF_SUCCESS; }
    double minHz() override { return 9e3; }
    double maxHz() override { return 260e6; }
    bool validCenter(double hz) override { return (hz >= 9e3 && hz <= 31e6) || (hz >= 60e6 && hz <= 260e6); }
    int gainSteps() override { return 9; }                                       // attenuator 48 dB ... 0 dB in 6 dB steps
    std::string gainText(int i) override { char b[32]; snprintf(b, sizeof b, "-%d dB", (8 - i) * 6); return b; }
    void setGain(int i) override { if (dev) { airspyhf_set_hf_agc(dev, 0); airspyhf_set_hf_att(dev, (uint8_t)(8 - i)); } }
    bool hasAgc() override { return true; }
    void setAgc(bool on) override { if (dev && on) airspyhf_set_hf_agc(dev, 1); }
    const char* extraName() override { return "HF LNA"; }
    void setExtra(bool on) override { lna = on; if (dev) airspyhf_set_hf_lna(dev, on ? 1 : 0); }
    static int cb(airspyhf_transfer_t* t) { AirspyHfDev* d = (AirspyHfDev*)t->ctx; d->sink(t->samples, (size_t)t->sample_count, FMT_F32); return 0; }
    bool start(SampleSink s) override { sink = s; return dev && airspyhf_start(dev, cb, this) == AIRSPYHF_SUCCESS; }
    void stop() override { if (dev) airspyhf_stop(dev); }
    bool alive() override { return dev && airspyhf_is_streaming(dev) == 1; }
    std::string desc() override { return label; }
};

// ===================================================================== discovery
struct DevEntry { int kind; int index; std::string label; std::string serialStr; uint64_t serial64; };

static inline void enumerateDevices(std::vector<DevEntry>& out) {
    out.clear();
    int n = (int)rtlsdr_get_device_count();
    for (int i = 0; i < n; i++) { const char* nm = rtlsdr_get_device_name(i); char b[300]; snprintf(b, sizeof b, "RTL-SDR #%d  %s", i, nm ? nm : ""); out.push_back({ KIND_RTL, i, b, "", 0 }); }
    hackrf_init();
    if (hackrf_device_list_t* l = hackrf_device_list()) {
        for (int i = 0; i < l->devicecount; i++) {
            std::string sn = l->serial_numbers[i] ? l->serial_numbers[i] : ""; std::string tail = sn.size() > 8 ? sn.substr(sn.size() - 8) : sn;
            const char* bn = hackrf_usb_board_id_name(l->usb_board_ids[i]); char b[200]; snprintf(b, sizeof b, "%s  ...%s", bn ? bn : "HackRF", tail.c_str());
            out.push_back({ KIND_HACKRF, i, b, sn, 0 });
        }
        hackrf_device_list_free(l);
    }
    if (getenv("TA_SDR_FAKE")) {   // developer aid: phantom devices to exercise the UI/error paths without hardware
        out.push_back({ KIND_HACKRF, 0, "[fake] HackRF One", "0000000000000000", 0 });
        out.push_back({ KIND_AIRSPY, 0, "[fake] Airspy R2", "", 0x1234 });
        out.push_back({ KIND_AIRSPYHF, 0, "[fake] Airspy HF+ Discovery", "", 0x5678 });
    }
    uint64_t ser[8];
    int na = airspy_list_devices(ser, 8); if (na < 0) na = 0; if (na > 8) na = 8;
    for (int i = 0; i < na; i++) { char b[120]; snprintf(b, sizeof b, "Airspy  %016llX", (unsigned long long)ser[i]); out.push_back({ KIND_AIRSPY, i, b, "", ser[i] }); }
    int nh = airspyhf_list_devices(ser, 8); if (nh < 0) nh = 0; if (nh > 8) nh = 8;
    for (int i = 0; i < nh; i++) { char b[120]; snprintf(b, sizeof b, "Airspy HF+  %016llX", (unsigned long long)ser[i]); out.push_back({ KIND_AIRSPYHF, i, b, "", ser[i] }); }
}
