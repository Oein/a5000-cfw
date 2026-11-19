#!/bin/sh
# mkg.sh out src... [libs] : glibc (camera /usr/lib) dynamic executable via zig, linking the camera sysroot libs
o=$1; shift
exec zig c++ -target arm-linux-gnueabi.2.17 -mcpu=cortex_a5 -O2 -fno-exceptions -fno-rtti -nostdlib++ -fno-stack-protector -fno-sized-deallocation -Wl,--allow-shlib-undefined -Wl,--hash-style=sysv -o $o "$@" -L/Users/oein/oein/Pjs/self-util/sony-a5000-custom-firmware/app/native/sysroot/lib -lpthread -ldl /Users/oein/oein/Pjs/self-util/sony-a5000-custom-firmware/app/native/sysroot/lib/libstdc++.so.6 /Users/oein/oein/Pjs/self-util/sony-a5000-custom-firmware/app/native/sysroot/lib/libgcc_s.so.1
