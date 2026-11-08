#!/bin/sh
# Builds TA-SDR.exe on Linux (or WSL) with MinGW-w64. Fetches + builds libusb and librtlsdr itself.
#   sudo apt install mingw-w64 git autoconf automake libtool make
#   ./build.sh          -> TA-SDR.exe
#   ./build.sh test     -> native DSP self-test (synthetic signals), needs g++
set -e
ROOT=$(cd "$(dirname "$0")" && pwd)
if [ "$1" = "test" ]; then
  g++ -O2 -ffast-math -o "$ROOT/build-test" "$ROOT/src/test_dsp.cpp" && "$ROOT/build-test"; exit $?
fi
B="$ROOT/build"; X=x86_64-w64-mingw32; mkdir -p "$B/obj"
LIBUSB_REV=119604ccb22deade65551d3562c15c5b3a6d324e
RTL_REV=aed0ea19f3a273370a13c9009b96313c75d54c7b
HACKRF_REV=3a63b29953a3b763f428821e02110def9fb1cade
AIRSPY_REV=fc61ab6be57ed61f0e2bdd9c6dfae74cacef57d0
AIRSPYHF_REV=24fe8ffcb00b14f827268bbad89ae1392de055e5

if [ ! -f "$B/install/lib/libusb-1.0.a" ]; then
  [ -d "$B/libusb" ] || git clone https://github.com/libusb/libusb.git "$B/libusb"
  git -C "$B/libusb" checkout -q $LIBUSB_REV
  (cd "$B/libusb" && ./bootstrap.sh >/dev/null 2>&1 && mkdir -p b && cd b &&
   ../configure --host=$X --enable-static --disable-shared --prefix="$B/install" >/dev/null &&
   make -j"$(nproc)" >/dev/null && make install >/dev/null)
fi
if [ ! -f "$B/obj/librtlsdr.a" ]; then
  [ -d "$B/rtl-sdr-blog" ] || git clone https://github.com/rtlsdrblog/rtl-sdr-blog.git "$B/rtl-sdr-blog"
  git -C "$B/rtl-sdr-blog" checkout -q -f $RTL_REV
  git -C "$B/rtl-sdr-blog" apply "$ROOT/patches/librtlsdr-blog-pll-error.patch"
  R="$B/rtl-sdr-blog"
  for f in librtlsdr tuner_e4k tuner_fc0012 tuner_fc0013 tuner_fc2580 tuner_r82xx; do
    $X-gcc -O2 -Drtlsdr_STATIC -DVERSION='"0.6"' -DPACKAGE_VERSION='"0.6"' -I"$R/include" \
      -I"$B/install/include/libusb-1.0" -c "$R/src/$f.c" -o "$B/obj/$f.o"
  done
  $X-ar rcs "$B/obj/librtlsdr.a" "$B"/obj/tuner_*.o "$B/obj/librtlsdr.o"
fi

# HackRF, Airspy and Airspy HF+ driver libraries (BSD-3), compiled straight from source
fetch() { # name url rev  (shallow, pinned)
  if [ ! -d "$B/$1/.git" ]; then git init -q "$B/$1"; git -C "$B/$1" remote add origin "$2"; fi
  git -C "$B/$1" fetch -q --depth 1 origin "$3"
  git -C "$B/$1" checkout -q -f FETCH_HEAD
}
if [ ! -f "$B/obj/libvendor.a" ]; then
  fetch hackrf https://github.com/greatscottgadgets/hackrf.git $HACKRF_REV
  fetch airspy https://github.com/airspy/airspyone_host.git $AIRSPY_REV
  fetch airspyhf https://github.com/airspy/airspyhf.git $AIRSPYHF_REV
  LU="-I$B/install/include/libusb-1.0"
  $X-gcc -O2 $LU -I"$B/hackrf/host/libhackrf/src" -c "$B/hackrf/host/libhackrf/src/hackrf.c" -o "$B/obj/hackrf.o"
  A="$B/airspy/libairspy/src"
  for f in airspy iqconverter_float iqconverter_int16; do $X-gcc -O2 $LU -I"$A" -c "$A/$f.c" -o "$B/obj/as_$f.o"; done
  H="$B/airspyhf/libairspyhf/src"
  for f in airspyhf iqbalancer; do $X-gcc -O2 $LU -I"$H" -c "$H/$f.c" -o "$B/obj/hf_$f.o"; done
  $X-ar rcs "$B/obj/libvendor.a" "$B/obj/hackrf.o" "$B"/obj/as_*.o "$B"/obj/hf_*.o
fi

cd "$ROOT/src"
$X-windres app.rc -O coff -o "$B/obj/app_res.o"
$X-g++ -O2 -ffast-math -std=c++17 -Wall -Wno-unused-function -Wno-misleading-indentation \
  -I"$B/rtl-sdr-blog/include" -I"$B/install/include/libusb-1.0" -Drtlsdr_STATIC \
  -I"$B/hackrf/host/libhackrf/src" -I"$B/airspy/libairspy/src" -I"$B/airspyhf/libairspyhf/src" \
  main.cpp "$B/obj/app_res.o" -o "$ROOT/TA-SDR.exe" \
  "$B/obj/librtlsdr.a" "$B/obj/libvendor.a" "$B/install/lib/libusb-1.0.a" \
  -static -static-libgcc -static-libstdc++ -mwindows -lwinmm -lcomctl32 -lgdi32 -luser32 -lsetupapi -lole32 -lcfgmgr32 -lwinpthread
$X-strip "$ROOT/TA-SDR.exe"
ls -la "$ROOT/TA-SDR.exe"
