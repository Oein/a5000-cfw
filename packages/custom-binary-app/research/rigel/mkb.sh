#!/bin/sh
# mkb.sh out src.c... : build bionic (Android 2.3) dynamic executable (zig cc -c + arm-none-eabi-ld)
set -e
o=$1; shift; objs=""
T=$(mktemp -d /private/tmp/claude-501/-Users-oein-oein-Pjs-self-util-sony-a5000-custom-firmware/9055c8e9-7bd9-4619-a47d-a5a711ce1b30/scratchpad/re-rigel/tmp.XXXX)
arm-none-eabi-as -mcpu=cortex-a5 -o $T/crt.o /private/tmp/claude-501/-Users-oein-oein-Pjs-self-util-sony-a5000-custom-firmware/9055c8e9-7bd9-4619-a47d-a5a711ce1b30/scratchpad/re-rigel/src/bcrt.S
for s in "$@"; do b=$T/$(basename $s .c).o; arm-none-eabi-gcc -march=armv7-a -mthumb -mfloat-abi=softfp -mfpu=vfpv3-d16 -fno-short-enums -O1 -fno-pic -std=gnu99 -Wall -Wno-builtin-declaration-mismatch -ffreestanding -fno-stack-protector -fno-builtin -c -o $b $s; objs="$objs $b"; done
arm-none-eabi-ld -o $o --dynamic-linker=/system/bin/linker --hash-style=sysv -Bdynamic --no-warn-mismatch $T/crt.o $objs /Users/oein/oein/Pjs/self-util/sony-a5000-custom-firmware/app/native/sysroot-android/lib/libc.so /Users/oein/oein/Pjs/self-util/sony-a5000-custom-firmware/app/native/sysroot-android/lib/libdl.so
rm -rf $T
