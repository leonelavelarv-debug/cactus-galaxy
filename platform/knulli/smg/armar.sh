#!/bin/bash
# Arma el port de Super Mario Galaxy en la PC (Linux o WSL de Windows 11), de las fuentes a los paquetes: compila para
# aarch64 el modulo del juego (native/), Nebula (platform/knulli/game), nebula-pak y Cactus Galaxy Wrapper
# (platform/knulli/pak); traduce en la PC todos los shaders del juego a GLSL (smg/shaders-gles.bin: la consola no
# lleva traductor y le da el GLSL directo al driver); junta la carpeta del port (Super Mario Galaxy.sh + smg/ con sus licencias), la limpia
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
#            NEBULA_PARCHES  opcional: una carpeta con parches .xdelta de traduccion (van a smg/patches/)
#            NEBULA_CLAVES   opcional: gles_keys.bin de sesiones en la consola (mas shaders en la biblioteca)
#            NEBULA_TRADUCTOR=1  opcional: Nebula con el traductor de shaders dentro (como antes)
#            NEBULA_MEDICION=1   opcional: ademas, <salida>/paquete-medicion: el mismo port con ULTRA-GLES (smg/ultra/,
#                                NEBULA_ULTRA=bench|serie, compare.py) para medir en la consola; el paquete normal va limpio
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

say "1/6 el modulo del juego (libsmg_game.so; la primera vez tarda: ~1900 unidades)"
cmake -S "$repo/native" -B "$build/juego" "${cfg[@]}" "-DCMAKE_EXE_LINKER_FLAGS=$static" \
  "-DCMAKE_SHARED_LINKER_FLAGS=$static" > "$build/juego.log"
cmake --build "$build/juego" -j "$jobs" --target smg_game >> "$build/juego.log" || fail "el modulo del juego no compilo: $build/juego.log"
# Sin el traductor de shaders (glslang + SPIRV-Cross): la consola recibe el GLSL hecho aqui (paso 3) y se lo da
# directo al driver. NEBULA_TRADUCTOR=1 lo deja dentro (lo que no este en la biblioteca se traduce en la consola).
traductor=OFF
[ "${NEBULA_TRADUCTOR:-0}" = 1 ] && traductor=ON
say "2/6 Nebula (nebula: host, GX en OpenGL ES 3.2; traductor de shaders: $traductor)"
cmake -S "$repo/platform/knulli/game" -B "$build/nebula" "${cfg[@]}" -DNEBULA_DEPS="$NEBULA_DEPS" -DNEBULA_GLES_TRANSLATOR=$traductor \
  -DNEBULA_SDL2_PREFIX="$sdl2" "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld --gcc-toolchain=$AARCH64_XTOOLS -static-libstdc++ -static-libgcc" \
  > "$build/nebula.log"
cmake --build "$build/nebula" -j "$jobs" --target nebula_knulli >> "$build/nebula.log" || fail "Nebula no compilo: $build/nebula.log"
say "3/6 la biblioteca GLSL (todos los shaders del juego traducidos en esta PC)"
# nebula_glsl_aot para esta PC (con el traductor), y los shaders de las claves de una vuelta entera al juego
# (platform/android/vk_seed_keys.bin) mas las que se le den: NEBULA_CLAVES="<gles_keys.bin> ..." (las que anota la
# consola al jugar, en smg/data/shadercache/).
host_cc=(); command -v clang++ >/dev/null && host_cc=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
cmake -S "$repo/platform/knulli/game" -B "$build/aot" -DCMAKE_BUILD_TYPE=Release "${generator[@]}" "${host_cc[@]}" \
  -DNEBULA_DEPS="$NEBULA_DEPS" -DNEBULA_GLES_TRANSLATOR=ON > "$build/aot.log"
cmake --build "$build/aot" -j "$jobs" --target nebula_glsl_aot >> "$build/aot.log" || fail "nebula_glsl_aot no compilo: $build/aot.log"
claves=("$repo/platform/android/vk_seed_keys.bin")
# Las que anoto el juego en la RG35XX H (KNULLI, 31-oct): 486 claves, 241 programas que la semilla no tiene.
[ -f "$here/claves-consola.bin" ] && claves+=("$here/claves-consola.bin")
for c in ${NEBULA_CLAVES:-}; do [ -f "$c" ] || fail "no existe $c (NEBULA_CLAVES)"; claves+=("$c"); done
"$build/aot/nebula_glsl_aot" "$build/shaders-gles.bin" "${claves[@]}" 2>> "$build/aot.log" || fail "la biblioteca GLSL fallo: $build/aot.log"
say "4/6 nebula-pak y Cactus Galaxy Wrapper"
cmake -S "$repo/platform/knulli/pak" -B "$build/pak" "${cfg[@]}" -DCACTUS_SDL2_PREFIX="$sdl2" > "$build/pak.log"
cmake --build "$build/pak" -j "$jobs" >> "$build/pak.log" || fail "nebula-pak no compilo: $build/pak.log"

say "5/6 la carpeta del port"
lib="$(find "$build/juego" -maxdepth 2 -name 'libsmg_game.so' | head -n 1)"
[ -n "$lib" ] || fail "no aparecio libsmg_game.so en $build/juego"
full="$out/completo"
rm -rf "$full" "$out/port"
mkdir -p "$full/smg/licenses" "$full/smg/gamedata"
cp "$here/Super Mario Galaxy.sh" "$full/"
(cd "$here/smg" && tar --exclude=__pycache__ --exclude=.gitignore -cf - .) | (cd "$full/smg" && tar -xf -)
# Parches de traduccion de terceros (no van en el repositorio): NEBULA_PARCHES=<carpeta con los .xdelta>.
if [ -n "${NEBULA_PARCHES:-}" ]; then
  cp "$NEBULA_PARCHES"/*.xdelta "$full/smg/patches/" 2>/dev/null || fail "no hay .xdelta en $NEBULA_PARCHES"
fi
cp "$here/README.md" "$full/smg/README.md"
printf '%s\n' "Pon aqui tu disco de Super Mario Galaxy (USA, RMGE01) en ISO, RVZ, WIA, WBFS, GCZ o CISO, y abre el juego:" \
  "Cactus Galaxy Wrapper lo convierte en game.pak (o pon directamente el game.pak)." > "$full/smg/gamedata/LEEME.txt"
strip=$(command -v llvm-strip || command -v llvm-strip-18 || true)
[ -n "$strip" ] || fail "falta llvm-strip (sudo apt install llvm)"
for b in "$build/nebula/nebula" "$lib" "$build/pak/nebula-pak" "$build/pak/cactus-galaxy-wrapper"; do
  [ -f "$b" ] || fail "no aparecio $b"
  "$strip" -o "$full/smg/$(basename "$b")" "$b"
done
chmod +x "$full/Super Mario Galaxy.sh" "$full/smg/nebula" "$full/smg/nebula-pak" "$full/smg/cactus-galaxy-wrapper"
cp "$build/shaders-gles.bin" "$full/smg/shaders-gles.bin"
# Para rehacer la biblioteca despues con las claves de la consola (nopal nebula shaders), sin volver a compilar.
mkdir -p "$out/herramientas"
"$strip" -o "$out/herramientas/nebula_glsl_aot" "$build/aot/nebula_glsl_aot"
cp "$repo/platform/android/vk_seed_keys.bin" "$out/herramientas/claves-semilla.bin"
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
if [ "${NEBULA_MEDICION:-0}" = 1 ]; then
  rm -rf "$out/port-medicion"
  mv "$full" "$out/port-medicion"
  # Mide al abrirlo, sin editar nada en la consola: la serie de smg/ultra/serie.cfg (NEBULA_ULTRA_MEDICION=bench para
  # un solo recorrido). Lo que se ponga en smg/ultra.cfg manda sobre esto.
  printf '%s\n' "# El paquete de medicion: al abrir el juego corre la serie de ULTRA-GLES (smg/ultra/serie.cfg) y sale." \
    "# Para jugar normal, instala el paquete limpio (o pon NEBULA_ULTRA=0 en smg/ultra.cfg)." \
    "NEBULA_ULTRA=${NEBULA_ULTRA_MEDICION:-serie}" > "$out/port-medicion/smg/ultra/ultra.cfg"
else
  rm -rf "$full"
fi

say "6/6 los paquetes"
"$here/empaquetar.sh" "$out/port" "$out/paquete"
if [ "${NEBULA_MEDICION:-0}" = 1 ]; then
  "$here/empaquetar.sh" "$out/port-medicion" "$out/paquete-medicion"
fi
say ""
say "Listo:"
say "  $out/port      copia Super Mario Galaxy.sh y smg/ a la SD (o usa el instalador)"
say "  $out/paquete   smg.zip para PortMaster e \"Instalar Super Mario Galaxy.sh\""
say "  $out/herramientas  nebula_glsl_aot y las claves semilla (nopal nebula shaders rehace smg/shaders-gles.bin)"
[ "${NEBULA_MEDICION:-0}" = 1 ] && say "  $out/paquete-medicion   el mismo port con ULTRA-GLES (para medir en la consola)"
