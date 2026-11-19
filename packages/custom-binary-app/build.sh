#!/bin/sh
# Build helper for the a5kui tree (libuicore + platform adapters).
#   ./build.sh host            host simulator (local clang)
#   ./build.sh a5000-ndk       a5000 bionic ARM (a5k-ndk docker) -> platform/a5000/build/ndk
#   ./build.sh a5000-cross     a5000 glibc ARM (a5k-cross docker)
#   ./build.sh a5000-inproc    Track B route (b): a5kinproc.so, loaded into im.elf (glibc)
#   ./build.sh launcher        strip ndk binaries into the launcher + assemble APK
set -e
here=$(cd "$(dirname "$0")" && pwd)   # app/native
case "$1" in
  host)
    make -C "$here/platform/host" "${@:2}" ;;
  a5000-ndk)
    docker run --rm -v "$here":/work a5k-ndk sh -c '
      make -C /work/platform/a5000 TARGET=ndk &&
      arm-linux-androideabi-strip --strip-unneeded /work/platform/a5000/build/ndk/a5kui  -o /work/platform/a5000/build/ndk/liba5kui.so &&
      arm-linux-androideabi-strip --strip-unneeded /work/platform/a5000/build/ndk/a5kctl -o /work/platform/a5000/build/ndk/liba5kctl.so' ;;
  a5000-cross)
    docker run --rm -v "$here":/work a5k-cross make -C /work/platform/a5000 TARGET=cross ;;
  a5000-inproc)
    # Track B route (b): uicore + in-process backend -> a5kinproc.so (glibc, loaded into im.elf).
    docker run --rm -v "$here":/work a5k-cross make -C /work/platform/a5000-inproc "${@:2}" ;;
  launcher)
    "$0" a5000-ndk
    cp "$here/platform/a5000/build/ndk/liba5kui.so"  "$here/platform/a5000/launcher/app/src/main/jniLibs/armeabi/"
    cp "$here/platform/a5000/build/ndk/liba5kctl.so" "$here/platform/a5000/launcher/app/src/main/jniLibs/armeabi/"
    cd "$here/platform/a5000/launcher" && ./gradlew :app:assembleRelease --offline
    python3 -I "$here/../android-app/tools/check-api-level.py" \
      "$here/platform/a5000/launcher/app/build/outputs/apk/release/a5kui-launcher-release.apk" 10 ;;
  *) echo "usage: build.sh host|a5000-ndk|a5000-cross|a5000-inproc|launcher"; exit 2 ;;
esac
