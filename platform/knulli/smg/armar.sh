#!/bin/bash
# Arma el port de Super Mario Galaxy en la PC (Linux o WSL de Windows 11), de las fuentes a los paquetes: compila para
# aarch64 el modulo del juego (native/), Nebula (platform/knulli/game), nebula-pak y Cactus Galaxy Wrapper
# (platform/knulli/pak); junta la carpeta del port (Super Mario Galaxy.sh + smg/ con sus licencias), la limpia
# (limpiar.py) y la empaqueta (empaquetar.sh). Es lo que hace `nopal nebula compilar` (Nopal).
#
#   armar.sh --deps <carpeta>                 descarga (git) glslang, SPIRV-Tools, SPIRV-Headers, SPIRV-Cross y SDL2 en
#                                             las versiones probadas y compila SDL2 para aarch64 (hace falta AARCH64_XTOOLS)
#   armar.sh <salida> [carpeta de compilacion]
#       <salida>/port      la carpeta para la SD: Super Mario Galaxy.sh y smg/ (van a ports/ o donde diga tu firmware)
#       <salida>/paquete   smg.zip (PortMaster) e "Instalar Super Mario Galaxy.sh"
#
# Variables: AARCH64_XTOOLS  la toolchain de crosstool-NG (x-tools/aarch64-rpi3-linux-gnu: glibc 2.27, corre en todos
#                            los firmwares); se compila con clang y lld
#            NEBULA_DEPS     la carpeta de --deps (glslang con External/spirv-tools, SPIRV-Cross, sdl2-a64)
#            NEBULA_SDL2     SDL2 para aarch64 (por defecto $NEBULA_DEPS/sdl2-a64; solo se enlaza: en la consola se
#                            usa la del firmware)
# Sin datos de la PC en los binarios (sin RPATH ni rutas) ni en los paquetes (fechas fijas, sin usuario).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
say() { printf '%s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)
jobs=$(nproc 2>/dev/null || echo 4)

# Las versiones probadas (las de la serie ULTRA del 30-oct).
GLSLANG=275822a6261ee689aadb1da5f09a0ec2f058685c
SPIRV_TOOLS=2ec8457ab33d539b6f1fecc998360c0b8b05ed4f
SPIRV_HEADERS=1a22b167081842915a1c78a0b5b5a353a23284aa
SPIRV_CROSS=d8e3e2b141b8c8a167b2e3984736a6baacff316c
SDL2=c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc  # 2.30.9

fetch() {  # fetch <url> <commit> <carpeta>: solo ese commit (sin la historia)
  [ -d "$3/.git" ] && [ "$(git -C "$3" rev-parse HEAD)" = "$2" ] && return 0
  rm -rf "$3"
  git init -q "$3"
  git -C "$3" fetch -q --depth 1 "$1" "$2"
  git -C "$3" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

toolchain() {
  : "${AARCH64_XTOOLS:?pon AARCH64_XTOOLS=<x-tools/aarch64-rpi3-linux-gnu>}"
  [ -d "$AARCH64_XTOOLS/aarch64-rpi3-linux-gnu/sysroot" ] || fail "AARCH64_XTOOLS no tiene aarch64-rpi3-linux-gnu/sysroot"
  command -v clang++ >/dev/null && command -v ld.lld >/dev/null || fail "hacen falta clang y lld (sudo apt install clang lld)"
  export AARCH64_XTOOLS
}

if [ "${1:-}" = "--deps" ]; then
  deps="$(mkdir -p "${2:?--deps <carpeta>}" && cd "$2" && pwd)"
  toolchain
  say "glslang + SPIRV-Tools + SPIRV-Headers"
  fetch https://github.com/KhronosGroup/glslang "$GLSLANG" "$deps/glslang"
  fetch https://github.com/KhronosGroup/SPIRV-Tools "$SPIRV_TOOLS" "$deps/glslang/External/spirv-tools"
  fetch https://github.com/KhronosGroup/SPIRV-Headers "$SPIRV_HEADERS" "$deps/glslang/External/spirv-tools/external/spirv-headers"
  say "SPIRV-Cross"
  fetch https://github.com/KhronosGroup/SPIRV-Cross "$SPIRV_CROSS" "$deps/SPIRV-Cross"
  say "SDL2 (para enlazar; en la consola va la del firmware)"
  fetch https://github.com/libsdl-org/SDL "$SDL2" "$deps/SDL2"
  cmake -S "$deps/SDL2" -B "$deps/build-sdl2-a64" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$repo/platform/knulli/aarch64-toolchain.cmake" -DCMAKE_INSTALL_PREFIX="$deps/sdl2-a64" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_RPATH=OFF > "$deps/sdl2.log"
  cmake --build "$deps/build-sdl2-a64" -j "$jobs" >> "$deps/sdl2.log"
  cmake --install "$deps/build-sdl2-a64" >> "$deps/sdl2.log"
  say "Listo: NEBULA_DEPS=$deps"
  exit 0
fi

out="${1:?uso: armar.sh <salida> [carpeta de compilacion]  (o armar.sh --deps <carpeta>)}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
build="${2:-$out/compilar}"
mkdir -p "$build"
build="$(cd "$build" && pwd)"
toolchain
: "${NEBULA_DEPS:?pon NEBULA_DEPS=<carpeta de armar.sh --deps>}"
sdl2="${NEBULA_SDL2:-$NEBULA_DEPS/sdl2-a64}"
[ -f "$sdl2/include/SDL2/SDL.h" ] && [ -e "$sdl2/lib/libSDL2-2.0.so" ] || fail "no hay SDL2 para aarch64 en $sdl2 (armar.sh --deps)"
[ -d "$NEBULA_DEPS/glslang" ] && [ -d "$NEBULA_DEPS/SPIRV-Cross" ] || fail "faltan glslang o SPIRV-Cross en $NEBULA_DEPS (armar.sh --deps)"
tc="$repo/platform/knulli/aarch64-toolchain.cmake"
static="-fuse-ld=lld --gcc-toolchain=$AARCH64_XTOOLS -static-libstdc++ -static-libgcc"
cfg=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$tc" "${generator[@]}")

say "1/5 el modulo del juego (libsmg_game.so; la primera vez tarda: ~1900 unidades)"
cmake -S "$repo/native" -B "$build/juego" "${cfg[@]}" "-DCMAKE_EXE_LINKER_FLAGS=$static" \
  "-DCMAKE_SHARED_LINKER_FLAGS=$static" > "$build/juego.log"
cmake --build "$build/juego" -j "$jobs" --target smg_game >> "$build/juego.log" || fail "el modulo del juego no compilo: $build/juego.log"
say "2/5 Nebula (nebula: host, GX en OpenGL ES 3.2)"
cmake -S "$repo/platform/knulli/game" -B "$build/nebula" "${cfg[@]}" -DNEBULA_DEPS="$NEBULA_DEPS" \
  -DNEBULA_SDL2_PREFIX="$sdl2" "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld --gcc-toolchain=$AARCH64_XTOOLS -static-libstdc++ -static-libgcc" \
  > "$build/nebula.log"
cmake --build "$build/nebula" -j "$jobs" --target nebula_knulli >> "$build/nebula.log" || fail "Nebula no compilo: $build/nebula.log"
say "3/5 nebula-pak y Cactus Galaxy Wrapper"
cmake -S "$repo/platform/knulli/pak" -B "$build/pak" "${cfg[@]}" -DCACTUS_SDL2_PREFIX="$sdl2" > "$build/pak.log"
cmake --build "$build/pak" -j "$jobs" >> "$build/pak.log" || fail "nebula-pak no compilo: $build/pak.log"

say "4/5 la carpeta del port"
lib="$(find "$build/juego" -maxdepth 2 -name 'libsmg_game.so' | head -n 1)"
[ -n "$lib" ] || fail "no aparecio libsmg_game.so en $build/juego"
full="$out/completo"
rm -rf "$full" "$out/port"
mkdir -p "$full/smg/licenses" "$full/smg/gamedata"
cp "$here/Super Mario Galaxy.sh" "$full/"
(cd "$here/smg" && tar --exclude=__pycache__ -cf - .) | (cd "$full/smg" && tar -xf -)
cp "$here/README.md" "$full/smg/README.md"
strip=$(command -v llvm-strip || command -v llvm-strip-18 || true)
[ -n "$strip" ] || fail "falta llvm-strip (sudo apt install llvm)"
for b in "$build/nebula/nebula" "$lib" "$build/pak/nebula-pak" "$build/pak/cactus-galaxy-wrapper"; do
  [ -f "$b" ] || fail "no aparecio $b"
  "$strip" -o "$full/smg/$(basename "$b")" "$b"
done
chmod +x "$full/Super Mario Galaxy.sh" "$full/smg/nebula" "$full/smg/nebula-pak" "$full/smg/cactus-galaxy-wrapper"
L="$full/smg/licenses"
cp "$repo/LICENSE" "$L/GPL-3.0.txt"
cp "$repo/native/petari/LICENSE" "$L/LICENSE"
cp "$NEBULA_DEPS/SPIRV-Cross/LICENSE" "$L/SPIRV-Cross-LICENSE"
cp "$NEBULA_DEPS/glslang/LICENSE.txt" "$L/glslang-LICENSE.txt"
cp "$repo/third_party/disc/bzip2/COPYING" "$L/bzip2-COPYING"
cp "$repo/third_party/archive/xz/COPYING.0BSD" "$L/liblzma-COPYING.0BSD"
cp "$repo/third_party/archive/zlib/LICENSE" "$L/zlib-LICENSE"
cp "$repo/third_party/disc/zstd/LICENSE" "$L/zstd-LICENSE"
cp "$repo/platform/knulli/cactus/font8x8/README" "$L/font8x8-README"
# Ningun binario lleva rutas de esta PC.
for b in "$full/smg/nebula" "$full/smg/libsmg_game.so" "$full/smg/nebula-pak" "$full/smg/cactus-galaxy-wrapper"; do
  rutas=(-e "$repo" -e "$build")
  [ "${#HOME}" -ge 6 ] && rutas+=(-e "$HOME/")
  if grep -qa "${rutas[@]}" "$b"; then fail "$(basename "$b") lleva rutas de esta PC"; fi
done
python3 "$here/limpiar.py" "$full" "$out/port" > /dev/null
rm -rf "$full"

say "5/5 los paquetes"
"$here/empaquetar.sh" "$out/port" "$out/paquete"
say ""
say "Listo:"
say "  $out/port      copia Super Mario Galaxy.sh y smg/ a la SD (o usa el instalador)"
say "  $out/paquete   smg.zip para PortMaster e \"Instalar Super Mario Galaxy.sh\""
