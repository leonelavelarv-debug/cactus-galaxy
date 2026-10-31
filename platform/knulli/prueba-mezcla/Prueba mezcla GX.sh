#!/bin/bash
# PORTMASTER: pruebamezcla.zip, Prueba mezcla GX.sh
# Prueba del paso 2 de GX-ES en la consola: la mezcla de doble fuente de GX (alfa del TEV) con y sin framebuffer
# fetch, en OpenGL ES 3.2 y en la ruta de 3.0, contra la referencia hecha en la PC. Guarda el driver (GL_VERSION,
# extensiones), la memoria y las imagenes en pruebamezcla/resultados/<firmware>-<fecha>/. GPL-3.0.
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi
source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

# KNULLI y la mayoria: ports/pruebamezcla; muOS: la carpeta de datos puede no estar junto al lanzador.
GAMEDIR=""
for d in "/$directory/ports/pruebamezcla" "$(cd "$(dirname "$0")" && pwd)/pruebamezcla"; do
  [ -d "$d" ] && GAMEDIR="$d" && break
done
[ -n "$GAMEDIR" ] || { echo "no encuentro la carpeta pruebamezcla"; exit 1; }
cd "$GAMEDIR" || exit 1

cfw=$(printf '%s' "${CFW_NAME:-desconocido}" | tr 'A-Z ' 'a-z_')
OUT="$GAMEDIR/resultados/$cfw-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
exec > >(tee "$OUT/log.txt") 2>&1
echo "firmware: $CFW_NAME  dispositivo: $DEVICE_NAME"
uname -a
grep -E "MemTotal|MemAvailable" /proc/meminfo

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export GALAXY_GLES_SHADER_LIBRARY="$GAMEDIR/shaders-gles-prueba.bin"
export NEBULA_SEMILLA="$GAMEDIR/claves-semilla.bin"
chmod +x "$GAMEDIR/nebula_gles_blend_tests"
pm_platform_helper "$GAMEDIR/nebula_gles_blend_tests"

# es32: el driver tal cual; es32-hilo: con el hilo de GL (como el juego); es30: GALAXY_GLES_FORCE_ES30=1.
for ruta in es32 es32-hilo es30; do
  dir="$OUT/$ruta"
  mkdir -p "$dir"
  cp -r "$GAMEDIR/referencia" "$dir/"
  extra=""
  [ "$ruta" = es32-hilo ] && extra="GALAXY_GLES_THREAD=1"
  [ "$ruta" = es30 ] && extra="GALAXY_GLES_FORCE_ES30=1"
  for modo in plain fetch; do
    env $extra "$GAMEDIR/nebula_gles_blend_tests" --modo "$modo" "$dir"
    echo "estado $ruta $modo: $?"
  done
  "$GAMEDIR/nebula_gles_blend_tests" --comparar "$dir"
  echo "comparacion $ruta: $?"
  rm -rf "$dir"/cache-*
done
grep -E "MemAvailable" /proc/meminfo
echo "resultados en $OUT (log.txt, */gl.txt, */metricas.txt, */dif/)"
pm_finish
