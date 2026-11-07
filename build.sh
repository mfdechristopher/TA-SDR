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

cd "$ROOT/src"
$X-windres app.rc -O coff -o "$B/obj/app_res.o"
$X-g++ -O2 -ffast-math -std=c++17 -Wall -Wno-unused-function -Wno-misleading-indentation \
  -I"$B/rtl-sdr-blog/include" -I"$B/install/include/libusb-1.0" -Drtlsdr_STATIC \
  main.cpp "$B/obj/app_res.o" -o "$ROOT/TA-SDR.exe" \
  "$B/obj/librtlsdr.a" "$B/install/lib/libusb-1.0.a" \
  -static -static-libgcc -static-libstdc++ -mwindows -lwinmm -lcomctl32 -lgdi32 -luser32 -lsetupapi -lole32 -lcfgmgr32
$X-strip "$ROOT/TA-SDR.exe"
ls -la "$ROOT/TA-SDR.exe"
