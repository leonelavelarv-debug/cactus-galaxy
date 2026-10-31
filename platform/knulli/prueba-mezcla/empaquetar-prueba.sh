#!/bin/bash
# Arma pruebamezcla.zip (PortMaster) para la RG35XX H: la prueba de mezcla del paso 2 de GX-ES.
#   empaquetar-prueba.sh <nebula_gles_blend_tests aarch64> <biblioteca GLSL> <carpeta referencia> <salida.zip>
# La biblioteca debe tener las claves de la prueba ademas de las semilla:
#   nebula_glsl_aot <biblioteca> platform/android/vk_seed_keys.bin <carpeta de la prueba>/cache-fetch/gles_keys.bin
# La referencia es <carpeta>/referencia de `nebula_gles_blend_tests --modo referencia` en la PC (Mesa: tiene
# GL_EXT_blend_func_extended). Sin fechas ni usuario de la PC en el zip.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
[ $# -eq 4 ] || { sed -n 2,7p "$0"; exit 2; }
bin="$1"; lib="$2"; ref="$3"; out="$4"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/pruebamezcla/referencia"
cp "$here/Prueba mezcla GX.sh" "$work/"
cp "$here/port.json" "$here/LEEME.txt" "$work/pruebamezcla/"
cp "$bin" "$work/pruebamezcla/nebula_gles_blend_tests"
"${STRIP:-llvm-strip}" "$work/pruebamezcla/nebula_gles_blend_tests"
cp "$lib" "$work/pruebamezcla/shaders-gles-prueba.bin"
cp "$repo/platform/android/vk_seed_keys.bin" "$work/pruebamezcla/claves-semilla.bin"
for scene in exactas-galaxy exactas-controles gradientes superpuestas claves-reales controles-reales; do
  cp "$ref/$scene.png" "$work/pruebamezcla/referencia/"
done
cp "$repo/LICENSE" "$work/pruebamezcla/LICENSE.txt"
chmod 755 "$work/Prueba mezcla GX.sh" "$work/pruebamezcla/nebula_gles_blend_tests"
find "$work" -exec touch -h -d "2026-01-01 00:00:00" {} +
rm -f "$out"
(cd "$work" && TZ=UTC zip -X -q -r "$out.tmp" "Prueba mezcla GX.sh" pruebamezcla)
mv "$out.tmp" "$out"
echo "$out"
