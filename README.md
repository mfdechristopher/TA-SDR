# TA-SDR

**A single-file Windows receiver for RTL-SDR dongles.** No installer, no DLLs, no 400 MB runtime.
Just one `.exe` and the quiet confidence of a 466 KB binary.

Hears everything your $30 dongle can: **AM, SAM, USB, LSB, CW, NFM, WFM (stereo)**, from HF
to about 1.7 GHz. Ham bands 160 m through 23 cm are one dropdown away, so you can finally
find out what 40 m sounds like at 2 a.m. (Answer: other people who are also awake at 2 a.m.)

## Run it

1. Plug in the dongle. Run [Zadig](https://zadig.akeo.ie), pick **Bulk-In, Interface (Interface 0)**,
   install **WinUSB**. Once. Windows insists.
2. Run `TA-SDR.exe`, pick your device, press **Start**.

No dongle? Pick **Demo (no hardware)** for simulated stations on 40 m, 20 m, FM, airband, and more.
It is the only SDR where the propagation is always good.

## Features

- Click/drag spectrum, mouse-wheel on any frequency digit, arrow keys, or type `7.1`, `14200`, `146.52M`
- Waterfall, zoom to 32x, adjustable filter width, CW pitch, squelch, audio AGC, noise blanker
- HF "just works": direct sampling on V3/generic dongles, upconverter on the Blog V4
- Record audio (WAV) or raw IQ (`.cu8`)
- Compensates for the R820T's wandering PLL, so your CW note stays where you put it

## Build it

```sh
sudo apt install mingw-w64 git autoconf automake libtool make
./build.sh        # fetches libusb + librtlsdr, cross-compiles TA-SDR.exe
./build.sh test   # DSP self-test with synthetic signals, no radio required
```

## Honest disclaimer

Written and verified without a dongle in the room: DSP is tested against synthetic signals and the
GUI under Wine, but real-hardware and real-speaker testing is up to you. If it misbehaves, open an
issue; "it sounds like a haunted dial-up modem" is a perfectly acceptable bug report.

## License

GPL-2.0, inherited from [librtlsdr](https://github.com/rtlsdrblog/rtl-sdr-blog) (patched slightly to
report PLL frequency error; see `patches/`). libusb is LGPL. 73!
