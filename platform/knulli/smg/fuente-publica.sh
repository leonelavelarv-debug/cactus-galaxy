#!/bin/bash
# El codigo fuente publico de una version del port (lo que pide la GPL junto a los binarios que se reparten): un
# tar.gz sin historial ni otras ramas, de la version exacta que se armo, sin las herramientas propias de medicion y
# pruebas (ver platform/knulli/PUBLICAR.md, que tampoco va).
#   fuente-publica.sh [version, por defecto HEAD] [carpeta de salida, por defecto .]
# Se publica junto al autoinstalable de esa misma version (por ejemplo, en el mismo release de GitHub).
set -euo pipefail
ref="${1:-HEAD}"
out="$(mkdir -p "${2:-.}" && cd "${2:-.}" && pwd)"
cd "$(dirname "$0")/../../.."
corto=$(git rev-parse --short "$ref")
nombre="cactus-galaxy-fuente-$corto"
# Lo propio que no entra en ningun binario del paquete.
fuera=(
  platform/knulli/PUBLICAR.md          # esta lista
  platform/knulli/smg/smg/ultra        # ULTRA-GLES: series de medicion (el paquete publico no las lleva)
  platform/knulli/smgbench             # banco de pruebas de la logica
  platform/knulli/recomp-probes        # sondas del recompilador
  platform/knulli/prueba-mezcla        # paquete de prueba del mezclador GX
)
args=()
for p in "${fuera[@]}"; do args+=(":(exclude)$p"); done
git archive --format=tar.gz --prefix="$nombre/" -o "$out/$nombre.tar.gz" "$ref" -- . "${args[@]}"
# Sin datos personales (la misma comprobacion que el paquete).
if tar -xzOf "$out/$nombre.tar.gz" 2>/dev/null | grep -aqiE 'usoduni|leonel\.avelar|sahuaro de sonora|leonelavelarv'; then
  rm -f "$out/$nombre.tar.gz"; echo "el codigo lleva datos personales: no se genera" >&2; exit 1
fi
ls -l "$out/$nombre.tar.gz"
