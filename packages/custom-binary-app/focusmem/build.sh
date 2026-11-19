#!/bin/sh
# focusmem: libc 없는 정적 ARM 바이너리(시스템 콜 직접). NDK clang + lld 만 필요(Docker 불필요).
set -e
cd "$(dirname "$0")"
CL=$(ls ~/Library/Android/sdk/ndk/*/toolchains/llvm/prebuilt/*/bin/clang | tail -1)
$CL --target=armv7a-linux-gnueabi -march=armv7-a -marm -mfloat-abi=soft -O2 -ffreestanding -fno-builtin \
    -fno-stack-protector -nostdlib -static -fuse-ld=lld -Wl,-e,_start -Wl,--build-id=none -o focusmem focusmem.c
cp focusmem ../../camera-ui/app/src/main/assets/focusmem
echo "built + copied to app assets"
