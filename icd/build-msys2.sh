#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds the Venus Vulkan driver (venera_vulkan_virtio.dll, x86_64) natively on Windows in MSYS2 UCRT64, as
# icd/build.sh does from Linux: the pinned Mesa fork with icd/patches/*.patch applied, the same meson options.
#
#   icd/build-msys2.sh [build-dir]      (default build/icd-msys2; MSYS2 UCRT64 shell)
#
# Needs: mingw-w64-ucrt-x86_64-{gcc,meson,ninja,python-mako,python-packaging,python-yaml,glslang}, bison, flex,
# patch, and the SDK headers in refs/winkits/sdk (cmake/fetch-winkits.sh).
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build/icd-msys2}
mesa=$root/third_party/virtio-win-mesa
src=$out/src
# A Windows path: meson hands it to the native compiler as it is.
sdk_shared=$(cygpath -m "$root/refs/winkits/sdk/c/Include/10.0.26100.0/shared")
export USERPROFILE="${USERPROFILE:-$(cygpath -w "$HOME")}"   # meson's cmake lookups need a home directory

options="--buildtype=release -Db_ndebug=true -Dc_std=c11 \
    -Dplatforms=windows -Dvulkan-drivers=virtio -Dgallium-drivers=zink -Dgallium-yttrium=true \
    -Dopengl=true -Dgallium-wgl-dll-name=viogpu_wgl \
    -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled -Dgbm=disabled \
    -Dllvm=disabled -Dzlib=disabled -Dzstd=disabled -Dexpat=disabled -Dvalgrind=disabled \
    -Dlibunwind=disabled -Dbuild-tests=false -Dvideo-codecs= -Dmicrosoft-clc=disabled \
    -Dspirv-tools=disabled"

mkdir -p "$out"
stamp="$(git -C "$mesa" rev-parse HEAD) $options"
for p in "$root"/icd/patches/*.patch; do
    stamp="$stamp $(sha256sum "$p" | cut -d' ' -f1)"
done
if [ ! -f "$src/.stamp" ] || [ "$(cat "$src/.stamp")" != "$stamp" ]; then
    rm -rf "$src" "$out/meson"
    mkdir -p "$src"
    git -C "$mesa" archive HEAD | tar -x -C "$src"
    for p in "$root"/icd/patches/*.patch; do
        echo "icd: applying $(basename "$p")"
        (cd "$src" && patch -p1 --forward --quiet < "$p")
    done
    echo "$stamp" > "$src/.stamp"
fi

# pkg-config sees only DirectX-Headers: with MSYS2's whole set, Mesa links libsystre (regex) and the DLL needs
# libsystre-0.dll, which a Windows guest lacks; the nix build finds no regex either (NO_REGEX).
pc=$out/pkgconfig
mkdir -p "$pc"
cp "$(pkg-config --variable=pcfiledir DirectX-Headers)/DirectX-Headers.pc" "$pc/"
export PKG_CONFIG_LIBDIR="$(cygpath -m "$pc")" PKG_CONFIG_PATH=

build=$out/meson
if [ ! -f "$build/build.ninja" ]; then
    # shellcheck disable=SC2086 # word splitting intended: the options list
    meson setup "$build" "$src" $options \
        -Dc_args="-idirafter $sdk_shared" \
        -Dc_link_args="-static -static-libgcc" -Dcpp_link_args="-static -static-libgcc -static-libstdc++"
fi
ninja -C "$build" src/virtio/vulkan/libvulkan_virtio.dll
cp "$build/src/virtio/vulkan/libvulkan_virtio.dll" "$out/venera_vulkan_virtio.dll"
echo "icd: $out/venera_vulkan_virtio.dll"
