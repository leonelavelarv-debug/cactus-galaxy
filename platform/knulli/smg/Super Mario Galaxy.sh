#!/bin/bash
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then controlfolder="$XDG_DATA_HOME/PortMaster"
else controlfolder="/roms/ports/PortMaster"; fi
source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

# Super Mario Galaxy (Nebula: el codigo de Petari compilado nativo, graficos OpenGL ES en la Mali-G31) para las
# Anbernic RG con Allwinner H700 (KNULLI, muOS) y la R36S con Rockchip RK3326 (ArkOS, dArkOS). El juego sale de TU
# disco (RMGE01): Cactus Galaxy Wrapper lo convierte la primera vez. GPL-3.0.
#   Salir: MENU + START (KNULLI, muOS) o SELECT + START (ArkOS, dArkOS).   Controles: smg/README.md.
# El perfil de controles segun el firmware y la consola (PortMaster da CFW_NAME, DEVICE_NAME y ANALOG_STICKS):
#   KNULLI y muOS (Anbernic RG con Allwinner H700): salir con MENU + START; ArkOS y dArkOS (R36S, Rockchip RK3326):
#   SELECT + START; ROCKNIX, AmberELEC y el resto: cualquiera de los dos. Los controles son para consolas con dos
#   sticks analogicos y cruceta.
#   NEBULA_FACE_LAYOUT=positions|labels  labels si en tu firmware A y B salen al reves (A deberia ser el de la derecha)
#   NEBULA_QUIT_COMBO=menu|select|both   fuerza la combinacion para salir
cfw=$(printf '%s' "${CFW_NAME:-}" | tr '[:upper:]' '[:lower:]')
soc=$(tr '\0' '\n' < /proc/device-tree/compatible 2>/dev/null | grep -Eo 'h700|h616|h618|rk3326|rk3566|rk3568' | head -1)
# Perfil por chip, con la H700 (RG35XX H, la medida) de referencia: la CPU en "performance" y la GPU y la memoria (dmc)
# a su frecuencia mas alta en todos; soc_cpu=chip sube ademas el tope de la CPU al maximo del chip (RK3326: sus
# Cortex-A35 rinden menos por MHz que los A53 de la H700); soc_perfil son los graficos por defecto (Ajustes manda).
case "$soc" in
  h700|h616|h618) soc_nombre="Allwinner H700/H616 (A53, Mali-G31)"; soc_cpu=sistema; soc_perfil=medio ;;
  rk3326)         soc_nombre="Rockchip RK3326 (A35, Mali-G31)";      soc_cpu=chip;    soc_perfil=bajo ;;
  rk3566|rk3568)  soc_nombre="Rockchip RK3566 (A55, Mali-G52)";      soc_cpu=sistema; soc_perfil=medio ;;
  *)              soc_nombre="desconocido";                          soc_cpu=sistema; soc_perfil=bajo ;;
esac
case "$cfw" in
  *knulli*|*muos*) quit_combo=menu ;;
  *arkos*|*darkos*) quit_combo=select ;;
  *) quit_combo=both ;;
esac
export NEBULA_QUIT_COMBO="${NEBULA_QUIT_COMBO:-$quit_combo}"
export NEBULA_FACE_LAYOUT="${NEBULA_FACE_LAYOUT:-positions}"
# La base de mandos de PortMaster, por si el firmware no trae el mapeo de este mando.
[ -f "$controlfolder/gamecontrollerdb.txt" ] && export NEBULA_GAMECONTROLLERDB="$controlfolder/gamecontrollerdb.txt"
GAMEDIR="/$directory/ports/smg"
cd $GAMEDIR
exec > >(tee "$GAMEDIR/log.txt") 2>&1
$ESUDO chmod +x "$GAMEDIR/nebula" 2>/dev/null || chmod +x "$GAMEDIR/nebula"
echo "firmware: ${CFW_NAME:-?} | consola: ${DEVICE_NAME:-?} (${soc:-soc ?}) | salir: $NEBULA_QUIT_COMBO + START | botones: $NEBULA_FACE_LAYOUT"
case "${ANALOG_STICKS:-2}" in 0|1) echo "aviso: esta consola tiene ${ANALOG_STICKS} stick(s); el juego necesita dos sticks y cruceta" ;; esac
echo "nebula: $(md5sum "$GAMEDIR/nebula" | cut -c1-12) del $(date -r "$GAMEDIR/nebula" '+%F %T' 2>/dev/null)"
# La memoria al abrir: la del sistema, el swap activo y si el kernel tiene zram (docs/GX_ES_MEMORY_GROWTH.md).
echo "memoria al abrir: $(grep -E 'MemTotal|MemAvailable|SwapTotal' /proc/meminfo | tr -s ' ' | tr '\n' ' ')"
echo "swap: $(tail -n +2 /proc/swaps 2>/dev/null | awk '{printf "%s %s KiB; ", $1, $3}')zram: $( { [ -e /sys/class/zram-control ] || [ -e /sys/block/zram0 ] || grep -qw zram /proc/modules 2>/dev/null || modinfo zram >/dev/null 2>&1; } && echo disponible || echo no)"
# Cactus Galaxy Wrapper (smg/cactus-galaxy-wrapper, dentro del port) se abre la primera vez: sin game.pak pide el
# disco (ISO, RVZ, WIA, WBFS, GCZ o CISO en smg/gamedata/ o roms/wii/; lo detecta en cuanto se copia), pregunta el
# idioma y lo convierte con smg/nebula-pak (nativo: sin Termux ni otro equipo); con game.pak, ofrece los idiomas del
# juego. La primera vez, antes de jugar, el cactus con lentes saluda en el idioma elegido. Despues arranca el juego
# directamente; manteniendo SELECT (o L1) al abrirlo vuelven las opciones.
# NEBULA_PAK_DELETE_IMAGE=1 borra la imagen al terminar de convertir.
screen_message() {
  echo "$1"
  if type pm_message >/dev/null 2>&1; then pm_message "$1"; else
    for tty in /dev/tty0 /dev/tty1; do [ -w "$tty" ] && printf '\033c%s\n' "$1" > "$tty" 2>/dev/null; done
  fi
}
for tool in nebula-pak cactus-ruso cactus-galaxy-wrapper; do [ -f "$GAMEDIR/$tool" ] && chmod +x "$GAMEDIR/$tool"; done
# Instalaciones anteriores con el juego ya hecho: el saludo de la primera vez (smg/.primer-arranque) no se repite.
[ -f "$GAMEDIR/gamedata/game.pak" ] && [ -f "$GAMEDIR/idioma.txt" ] && [ ! -f "$GAMEDIR/.primer-arranque" ] &&
  echo 1 > "$GAMEDIR/.primer-arranque"
# El wrapper ya no tiene entrada propia en Ports: se quita la de versiones anteriores.
rm -f "/$directory/ports/Cactus Galaxy Wrapper.sh" "$GAMEDIR/cactus.png" "$GAMEDIR/cactus-galaxy-wrapper.sh"
cactus=0
if [ -x "$GAMEDIR/cactus-galaxy-wrapper" ]; then
  wrapper=0
  if [ ! -f "$GAMEDIR/gamedata/game.pak" ] || [ ! -f "$GAMEDIR/idioma.txt" ]; then
    wrapper=1  # primera vez
  elif SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig" "$GAMEDIR/cactus-galaxy-wrapper" --held; then
    wrapper=1  # SELECT mantenido al abrir
  fi
  if [ "$wrapper" = 1 ]; then
    delete=""; [ "${NEBULA_PAK_DELETE_IMAGE:-0}" = "1" ] && delete="--delete-image"
    SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig" "$GAMEDIR/cactus-galaxy-wrapper" --smg "$GAMEDIR" --setup $delete
    cactus=$?
    echo "Cactus Galaxy Wrapper: salida $cactus"
    if [ "$cactus" = 1 ]; then  # salir sin jugar
      pm_finish; exit 0
    fi
    # Lo que se acaba de elegir en Ajustes (smg/opciones.cfg) vale ya en esta partida (ajustes.cfg sigue mandando).
    type lee_ajustes >/dev/null 2>&1 && lee_ajustes
  fi
fi
# Sin pantalla para la interfaz (salida 3) o sin ella: la version de texto, con el avance en la consola.
if [ ! -f "$GAMEDIR/gamedata/game.pak" ] && { [ "$cactus" = 3 ] || [ ! -x "$GAMEDIR/cactus-galaxy-wrapper" ]; } && [ -x "$GAMEDIR/nebula-pak" ] && [ -f "$GAMEDIR/cactus-galaxy-wrapper-texto.txt" ]; then
  image=$(find "$GAMEDIR/gamedata" -maxdepth 1 -type f \( -iname '*.iso' -o -iname '*.rvz' -o -iname '*.wia' \
    -o -iname '*.wbfs' -o -iname '*.gcz' -o -iname '*.ciso' \) 2>/dev/null | head -1)
  if [ -n "$image" ]; then
    name=$(basename "$image")
    screen_message "Cactus Galaxy Wrapper: preparando game.pak desde $name (solo la primera vez, unos minutos)..."
    shown=-1
    rm -f /tmp/smg-pak-error
    bash "$GAMEDIR/cactus-galaxy-wrapper-texto.txt" "$image" "$GAMEDIR" 2>&1 | while IFS= read -r line; do
      echo "$line"
      case "$line" in
        *%*MiB*)  # "37% (1234 / 3300 MiB)": la pantalla cada 2 %
          percent=${line%%%*}; percent=${percent// /}
          case "$percent" in ''|*[!0-9]*) continue ;; esac
          if [ $((percent - shown)) -ge 2 ] || [ "$percent" = 100 ]; then
            shown=$percent
            screen_message "Cactus Galaxy Wrapper: preparando game.pak desde $name
$line
(solo la primera vez; no apagues la consola)"
          fi ;;
        ERROR*|Error*) echo "$line" > /tmp/smg-pak-error ;;
      esac
    done
    if [ "${PIPESTATUS[0]}" = 0 ] && [ -f "$GAMEDIR/gamedata/game.pak" ]; then
      screen_message "game.pak listo."
      [ "${NEBULA_PAK_DELETE_IMAGE:-0}" = "1" ] && rm -f "$image"
    else
      screen_message "No se pudo hacer game.pak: $(cat /tmp/smg-pak-error 2>/dev/null || echo 'mira smg/log.txt')"
      sleep 8
    fi
  fi
fi
if [ ! -f "$GAMEDIR/gamedata/game.pak" ]; then
  screen_message "Falta smg/gamedata/game.pak: pon ahi (o en roms/wii) tu disco de Super Mario Galaxy USA (ISO o RVZ) o el game.pak."
  sleep 5
  pm_finish; exit 1
fi
mkdir -p "$GAMEDIR/nand" "$GAMEDIR/data" "$GAMEDIR/crashes"
# Portada y ficha en Ports, en el idioma del juego y donde la lee cada firmware (EmulationStation de KNULLI, ROCKNIX,
# ArkOS, dArkOS...; el catalogo de muOS): en segundo plano, el juego no espera. Detalles en smg/gamelist.py.
GAMELIST_PID=""
if command -v python3 >/dev/null 2>&1 && [ -f "$GAMEDIR/gamelist.py" ]; then
  python3 "$GAMEDIR/gamelist.py" "$GAMEDIR" --cfw "${CFW_NAME:-}" &
  GAMELIST_PID=$!
fi

# Ajustes (se pueden cambiar aqui, o en smg/ultra.cfg: lineas NOMBRE=valor, solo NEBULA_*, GALAXY_* y NOPAL_*, que mandan
# sobre lo de abajo sin editar este archivo; lo usa ULTRA-GLES para comparar ajustes):
# Se aceptan ultra.cfg y ultra.cfg.txt (editores que anaden .txt), finales de linea de Windows, la marca BOM del
# principio, espacios alrededor del =, comentarios con # y una ultima linea sin salto; cada ajuste leido queda en log.txt.
# Tambien smg/ajustes.cfg (el del port limpio) y smg/ultra/ultra.cfg (el del paquete de medicion), leidos antes:
# los de smg/ultra.cfg mandan.
# Primero smg/opciones.cfg: lo que se elige en Ajustes de Cactus Galaxy Wrapper (X en la pantalla de idiomas):
# GALAXY_GLES_RESOLUTION=auto|50|75|100 y NEBULA_VIBRACION=1|0 (0: el control no vibra). Los demas mandan sobre el.
# (Una funcion: el port limpio la llama al entrar, antes de Cactus Galaxy Wrapper, y otra vez despues de el, que
# acaba de escribir smg/opciones.cfg.)
lee_ajustes() {
for cfg in "$GAMEDIR/opciones.cfg" "$GAMEDIR/ajustes.cfg" "$GAMEDIR/ajustes.cfg.txt" "$GAMEDIR/ultra/ultra.cfg" "$GAMEDIR/ultra.cfg" "$GAMEDIR/ultra.cfg.txt"; do
  [ -f "$cfg" ] || continue
  while IFS= read -r line || [ -n "$line" ]; do
    line=${line%$'\r'}
    line=${line#$'\xef\xbb\xbf'}
    case "$line" in ''|'#'*) continue ;; *=*) ;; *) continue ;; esac
    key=$(printf '%s' "${line%%=*}" | tr -d ' \t')
    value=$(printf '%s' "${line#*=}" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
    case "$key" in
      NEBULA_*|GALAXY_*|NOPAL_*) export "$key=$value"; echo "ajuste de ${cfg#"$GAMEDIR"/}: $key=$value" ;;
    esac
  done < "$cfg"
done
}
lee_ajustes
# Mandos (SELECT en Cactus Galaxy Wrapper): con NEBULA_MANDOS=personalizado (smg/opciones.cfg) el juego toma ademas
# las asignaciones de smg/mandos.cfg (botones del Wii a botones del mando; lo demas como el perfil predeterminado).
if [ "${NEBULA_MANDOS:-predeterminado}" = personalizado ] && [ -f "$GAMEDIR/mandos.cfg" ]; then
  export NEBULA_MANDOS_ARCHIVO="$GAMEDIR/mandos.cfg"
  echo "mandos: perfil personalizado ($NEBULA_MANDOS_ARCHIVO)"
else
  unset NEBULA_MANDOS_ARCHIVO
  echo "mandos: perfil predeterminado"
fi
#   NEBULA_PERFIL=bajo|medio|normal      por defecto el del chip (soc_perfil arriba: medio en H700 y RK3566, bajo en
#                                        RK3326 y los demas). medio: resolucion interna fija de 50 % (320x264) con nitidez
#                                        0.5 al ampliarla, pensada para el pack de texturas HD, con el mismo salto de
#                                        frames, lecturas y caches que el bajo; bajo: pocos recursos, resolucion dinamica
#                                        (baja sola cuando la GPU no llega); normal: 100 % y sin saltos.
#                                        Prueba de rendimiento (21-nov, RG35XX H, 8 galaxias x6): 50 % frente a 75 %, +6 a
#                                        +8 % imagenes dibujadas (+20 % en las pesadas: BreakDownPlanet, CannonFleet); el
#                                        pack HD y la nitidez 0.5 no cuestan nada medible. 75 en Ajustes: como antes
NEBULA_PERFIL="${NEBULA_PERFIL:-$soc_perfil}"
if [ "$NEBULA_PERFIL" = "bajo" ] || [ "$NEBULA_PERFIL" = "medio" ]; then
  #   GALAXY_FRAMESKIP=auto|0|1|2|3      auto: ritmo estable de 60, 30 o 20 imagenes por segundo segun lo que
  #                                      cueste dibujar (GALAXY_FRAMESKIP_MAX=2: 20 como minimo); 1: siempre 30
  export GALAXY_FRAMESKIP="${GALAXY_FRAMESKIP:-auto}"
  export GALAXY_FRAMESKIP_MAX="${GALAXY_FRAMESKIP_MAX:-2}"
  #   GALAXY_FRAMESKIP_FAST=1|0          un frame saltado no prepara los draws de las display lists que va a tirar
  #                                      (estado, texturas, vertices, indices): cuesta una fraccion de uno dibujado; 0: como antes
  export GALAXY_FRAMESKIP_FAST="${GALAXY_FRAMESKIP_FAST:-1}"
  #   GALAXY_PEEK_MODE=async|sync|off    lo que el juego lee de la imagen (destellos del sol, puntero): async usa el
  #                                      frame anterior sin esperar a la GPU; sync espera (exacto, lento); off no lee
  export GALAXY_PEEK_MODE="${GALAXY_PEEK_MODE:-async}"
  #   GALAXY_GLES_RESOLUTION=auto        (perfil bajo) la resolucion interna dinamica: serie del 29-oct, GPU -48 %, p95 -20 %,
  #                                      recorrido -10 % frente a 100 fijo; 100: la del Wii siempre (en el perfil normal)
  if [ "$NEBULA_PERFIL" = "medio" ]; then
    export GALAXY_GLES_RESOLUTION="${GALAXY_GLES_RESOLUTION:-50}"
    #   GALAXY_GLES_SLOT_POR_IMAGEN=1|0  (perfil medio) el renderer cambia de slot al presentar una imagen y no en cada
    #                                    trozo que manda el juego (el 78 % no dibuja nada y cada uno esperaba a la GPU):
    #                                    44 galaxias x2 (5-nov), +21-23 % FPS mostrados, +16-21 % dibujados, p95 -17 %,
    #                                    12-35 MiB menos de memoria libre; 0: como antes (docs/PHASE3_PROGRESS.md)
    export GALAXY_GLES_SLOT_POR_IMAGEN="${GALAXY_GLES_SLOT_POR_IMAGEN:-1}"
    #   GALAXY_GX_DL_MARCAR=1|0|2        (perfil medio) el hilo de render solo compara con la memoria las display lists
    #                                    que un flush del juego toco; las demas valen sin memcmp. AstroGalaxy (5 pares,
    #                                    19-nov): CPU de render por imagen -9 a -10 %, +2,5 a +5 % imagenes nuevas, escena
    #                                    -4 a -5 %; 2 lo compara todo y avisa ("[gx] lista sin aviso"): 44 galaxias,
    #                                    36,4 millones de comparaciones, 0 avisos; 0: como antes (docs/PHASE3_PROGRESS.md)
    export GALAXY_GX_DL_MARCAR="${GALAXY_GX_DL_MARCAR:-1}"
    #   GALAXY_GX_DL_MARCAR_TODAS=1|0  (perfil medio) las marcas tambien en el clasificador de eventos PE del hilo del
    #                                    juego (comparaba siempre: 8-9 % del hilo). AstroGalaxy (2 pares, 20-nov): escena
    #                                    -7 a -9 %, +5 a +10 % imagenes nuevas; 44 galaxias verificadas con =2, 0 avisos
    export GALAXY_GX_DL_MARCAR_TODAS="${GALAXY_GX_DL_MARCAR_TODAS:-1}"
    #   GALAXY_GLES_COPIA_REDUCIDA=1|0   (perfil medio) con el EFB reducido, las copias de color a textura de pantalla
    #   GALAXY_GLES_COPIA_REDUCIDA_MIN_ANCHO=640  completa se escriben al tamano real del EFB (480x342 al 75 %) y no a 640x456;
    #                                    las chicas de efectos y las de profundidad se quedan igual. 12 galaxias x2
    #                                    (19-nov): GPU por imagen -2,2 ms (-7,6 %), capturas iguales bit a bit, sin
    #                                    cambio en imagenes nuevas/s; 0: como antes
    export GALAXY_GLES_COPIA_REDUCIDA="${GALAXY_GLES_COPIA_REDUCIDA:-1}"
    export GALAXY_GLES_COPIA_REDUCIDA_MIN_ANCHO="${GALAXY_GLES_COPIA_REDUCIDA_MIN_ANCHO:-640}"
  else
    export GALAXY_GLES_RESOLUTION="${GALAXY_GLES_RESOLUTION:-auto}"
  fi
  #   GALAXY_GLES_PEEK_FIELD=25          esas lecturas a 160x132 (25 %): se copian de la GPU 20 veces menos bytes
  export GALAXY_GLES_PEEK_FIELD="${GALAXY_GLES_PEEK_FIELD:-25}"
  #   GALAXY_GX_DISPLAY_LIST_CACHE_MB=24 y GALAXY_GX_DECODED_VERTEX_CACHE_MB=24  las display lists y los vertices ya
  #                                      convertidos que se guardan entre frames. Con 192 y 64 la memoria se acababa hacia
  #                                      la 13.a galaxia; con 32, en la 21.a; con 24, en la 28.a; con 16 de vertices
  #                                      la CPU tarda mucho mas (AstroGalaxy: 57 ms contra 18). Lo que llenaba la memoria
  #                                      eran los bloques de subida de texturas (docs/GX_ES_MEMORY_GROWTH.md).
  #                                      Lo que no cabe se vuelve a convertir (algo de CPU)
  export GALAXY_GX_DISPLAY_LIST_CACHE_MB="${GALAXY_GX_DISPLAY_LIST_CACHE_MB:-24}"
  export GALAXY_GX_DECODED_VERTEX_CACHE_MB="${GALAXY_GX_DECODED_VERTEX_CACHE_MB:-24}"
  #   GALAXY_EFB_COPY_DEST_BUDGET_MB=24  copias del EFB que se guardan (el pico medido: 10 MiB)
  export GALAXY_EFB_COPY_DEST_BUDGET_MB="${GALAXY_EFB_COPY_DEST_BUDGET_MB:-24}"
  #   GALAXY_TEXTURE_POOL_MB=8           texturas ya liberadas que se guardan para reusar (crecia a 27 MiB de 128)
  export GALAXY_TEXTURE_POOL_MB="${GALAXY_TEXTURE_POOL_MB:-8}"
  export GALAXY_DISC_PREFETCH_BYTES="${GALAXY_DISC_PREFETCH_BYTES:-2097152}"
else
  export GALAXY_FRAMESKIP="${GALAXY_FRAMESKIP:-0}"
fi
echo "chip: $soc_nombre (cpu: tope del $soc_cpu)"
echo "perfil: $NEBULA_PERFIL (frameskip $GALAXY_FRAMESKIP, peeks ${GALAXY_PEEK_MODE:-sync})"
#   GALAXY_GLES_PENDING=wait|skip|uber  shaders nuevos: esperar (tirones la 1a vez), saltar el dibujo, o uber
#   NEBULA_PAD_SCHEME=nintendo|xbox      A a la derecha (nintendo) o abajo (xbox)
export NEBULA_DATA_DIR="$GAMEDIR/data"
#   smg/shaders-gles.bin                 todos los shaders del juego ya en GLSL (hechos en la PC por armar.sh o
#                                        `nopal nebula shaders`): Nebula se los da directo al driver de OpenGL, sin
#                                        traducir nada en la consola. Uno que falte se dibuja con el shader generico (uber)
if [ -f "$GAMEDIR/shaders-gles.bin" ]; then
  export GALAXY_GLES_SHADER_LIBRARY="${GALAXY_GLES_SHADER_LIBRARY:-$GAMEDIR/shaders-gles.bin}"
fi
#   GALAXY_GLES_BLEND_FETCH=0            (prueba) la mezcla de GX con la alfa del TEV en blending fijo, como antes del
#                                        paso 2 (docs/GX_ES_FRAMEBUFFER_FETCH.md); por defecto el shader la hace con
#                                        framebuffer fetch si el driver lo tiene
#   GALAXY_GLES_FLUSH_DRAWS=n            (prueba) glFlush cada n draws de un mismo frame (una carga llega a ~2100); por
#                                        defecto no. No cambio el bloque que crece en el driver de la Mali
#   GALAXY_GLES_INDEX_COPY=0             (prueba) los indices en un buffer mapeado de forma persistente, como antes; por
#                                        defecto van con copia en la CPU y se mandan por rangos: asi el driver de la Mali
#                                        ve los cambios (mapeado, guardaba datos de cada rango de indices: hasta 124 MiB)
#   GALAXY_GLES_DRAW_RANGE=0             (prueba) glDrawElements como antes; por defecto glDrawRangeElements con los
#                                        vertices del draw. GALAXY_GLES_CHECK_RANGE=1 comprueba los indices (lento)
#   NEBULA_MEMTRACE=1                    (medicion) tras cada [memoria], las pilas que tienen los bloques grandes del
#                                        monton (NEBULA_MEMTRACE_MIN_KB=256 o mas), como modulo+desplazamiento
#   GALAXY_GX_MEMORY_LOG=60              cada 60 s una linea [memoria] en log.txt: VmRSS, memoria libre y lo que ocupa
#                                        cada cache (display lists, texturas, vertices, texturas y buffers de GL); 0: no
#   GALAXY_GLES_FORCE_ES30=1             (prueba) solo lo de OpenGL ES 3.0 con el driver de 3.2: anillos copiados,
#                                        paleta en textura 2D, GLSL ES 3.00 (docs/GX-ES.md)
#   GALAXY_TEXTURE_COMPACT=exacto|16bits|no  texturas en la memoria de video en 8 o 16 bits cuando caben: exacto (por
#                                        defecto) no cambia ni un texel (I4/I8 en 1 byte, IA4/IA8 y RGB565 en 2); 16bits
#                                        tambien CMPR y RGB5A3 (pierden los bits mas bajos del color); no: todo en RGBA8.
#                                        log.txt dice cuantas y cuanta memoria ahorran ([texture-compact])
export GALAXY_TEXTURE_COMPACT="${GALAXY_TEXTURE_COMPACT:-exacto}"
#   Packs de texturas: en smg/data/TexturePacks/ (hechos a la medida de la consola en la PC: `nopal texturas reducir`,
#   o `nopal nebula armar --texturas`). GALAXY_TEXTURE_DUMP=1 vuelca las texturas del juego a smg/data/TextureDumps/
#   (para editarlas en la PC y volver a ponerlas como pack).
#   GALAXY_TEXTURE_PACK_BUDGET_MB=96     memoria para los reemplazos (los que no se usan salen primero). 96 cabe con
#                                        lo que queda libre en 1 GB; un pack x2 va mejor con 128, si la memoria alcanza
#   Si hay un pack en cualquier parte de smg/data (archivos tex1_*.dds o .png hasta 5 carpetas abajo, sin contar
#   TextureDumps) se activa solo: se usa su carpeta RMGE01 (o la primera carpeta de data/ que lo contiene), aunque el
#   zip se haya descomprimido fuera de TexturePacks/. GALAXY_TEXTURE_PACKS=0 en smg/ajustes.cfg lo apaga.
if [ -z "${GALAXY_TEXTURE_PACK_DIR:-}" ] && [ -d "$GAMEDIR/data" ]; then
  tex=$(find "$GAMEDIR/data" -maxdepth 6 -path "$GAMEDIR/data/TextureDumps" -prune -o -type f \
    -path "$GAMEDIR/data/*/*" \( -iname 'tex1_*.dds' -o -iname 'tex1_*.png' \) -print 2>/dev/null | head -n 1)
  if [ -n "$tex" ]; then
    case "$tex" in
      "$GAMEDIR/data/TexturePacks/"*) pack="$GAMEDIR/data/TexturePacks" ;;
      *)
        pack=${tex%/*}
        while [ "$pack" != "$GAMEDIR/data" ]; do
          case "${pack##*/}" in [Rr][Mm][Gg][Ee]01|[Rr][Mm][Gg]) break ;; esac
          pack=${pack%/*}
        done
        if [ "$pack" = "$GAMEDIR/data" ]; then rel=${tex#"$GAMEDIR/data/"}; pack="$GAMEDIR/data/${rel%%/*}"; fi ;;
    esac
    export GALAXY_TEXTURE_PACK_DIR="$pack"
  fi
fi
if [ -n "${GALAXY_TEXTURE_PACK_DIR:-}" ]; then
  export GALAXY_TEXTURE_PACKS="${GALAXY_TEXTURE_PACKS:-1}"
  echo "texturas: pack en ${GALAXY_TEXTURE_PACK_DIR#"$GAMEDIR"/} ($(find "$GALAXY_TEXTURE_PACK_DIR" -type f \( -iname 'tex1_*.dds' -o -iname 'tex1_*.png' \) 2>/dev/null | wc -l) archivos), activas: $GALAXY_TEXTURE_PACKS"
else
  echo "texturas: no hay pack en smg/data"
fi
export GALAXY_TEXTURE_PACK_BUDGET_MB="${GALAXY_TEXTURE_PACK_BUDGET_MB:-96}"
#   NOPAL_DEPURAR=5555                   el Depurador de Nopal Estudio se conecta desde la PC (por WiFi, a la IP de la
#                                        consola y este puerto): FPS, tiempos del frame, RAM, CPU, log.txt en vivo, pausa
#                                        y avance de a un frame. Sin fijarlo no se abre nada
export GALAXY_CRASH_DUMP_DIR="$GAMEDIR/crashes"
export GALAXY_RUNTIME_SETTINGS_PATH="$GAMEDIR/settings.ini"
# uber por defecto (ULTRA-GLES): la primera vez con wait, 166 shaders compilados de golpe pararon el juego 62 s en
# 10 galaxias; con uber se dibuja con el shader generico mientras el suyo compila en otro hilo.
export GALAXY_GLES_PENDING="${GALAXY_GLES_PENDING:-uber}"
#   GALAXY_GLES_THREAD=1|0               1 (por defecto desde update52): las llamadas de GL van a un hilo propio, en otro
#                                        nucleo, mientras el de render traduce el frame siguiente; con 4 frames en vuelo
#                                        dio +16-19 % de frames dibujados y -30 % de p99 (ULTRA-GLES). 0: como antes
#                                        (ponlo en smg/ultra.cfg si algo se ve mal con el hilo).
export GALAXY_GLES_THREAD="${GALAXY_GLES_THREAD:-1}"
#   GALAXY_GLES_RING_INDEX_MB/MATRIX_MB  memoria por frame en vuelo para indices y matrices (el pico medido: <1 MiB)
#   GALAXY_GLES_RING_VERTEX_MB=24        y para vertices (el pico al jugar: 17-21 MiB; un frame que pasa, como la
#                                        carga de CosmosGardenGalaxy, sigue en el espacio de otro frame en vuelo)
export GALAXY_GLES_RING_VERTEX_MB="${GALAXY_GLES_RING_VERTEX_MB:-24}"
#   GALAXY_GLES_RING_CONSTANT_MB=5       y para las constantes de los shaders (el pico medido: 3.9 MiB)
export GALAXY_GLES_RING_CONSTANT_MB="${GALAXY_GLES_RING_CONSTANT_MB:-5}"
export GALAXY_GLES_RING_INDEX_MB="${GALAXY_GLES_RING_INDEX_MB:-3}"
export GALAXY_GLES_RING_MATRIX_MB="${GALAXY_GLES_RING_MATRIX_MB:-3}"
#   GALAXY_GLES_RESOLUTION=100           resolucion interna: 100 = 640x528 (alta definicion, la del Wii); 75 = 480x396;
#                                        50 = 320x264; auto = dinamica (baja cuando la GPU no llega, sube cuando le sobra)
#   GALAXY_GLES_RESOLUTION_MIN/MAX/START  los limites de la dinamica (50/100) y donde empieza (75)
export GALAXY_GLES_RESOLUTION="${GALAXY_GLES_RESOLUTION:-100}"
export GALAXY_GLES_RESOLUTION_MIN="${GALAXY_GLES_RESOLUTION_MIN:-50}"
export GALAXY_GLES_RESOLUTION_MAX="${GALAXY_GLES_RESOLUTION_MAX:-100}"
export GALAXY_GLES_RESOLUTION_START="${GALAXY_GLES_RESOLUTION_START:-75}"
#   GALAXY_GLES_SHARPEN=0.4              nitidez al ampliar la imagen interna (0.25-0.5 recomendado; 0: apagada; sin halos);
#                                        0.5 por defecto con la resolucion al 50 % (la prueba del 21-nov: sin costo medible)
if [ "$GALAXY_GLES_RESOLUTION" = 50 ]; then export GALAXY_GLES_SHARPEN="${GALAXY_GLES_SHARPEN:-0.5}"; fi
export GALAXY_GLES_SHARPEN="${GALAXY_GLES_SHARPEN:-0.4}"
#   GALAXY_GLES_SHADOWS=0.45             aclara las sombras (0.3-0.62 recomendado; 0: apagado); GALAXY_GLES_CONTRAST=0
export GALAXY_GLES_SHADOWS="${GALAXY_GLES_SHADOWS:-0.45}"
export GALAXY_GLES_CONTRAST="${GALAXY_GLES_CONTRAST:-0}"
#   GALAXY_NO_DOF=1 GALAXY_NO_BLUR=1     sin profundidad de campo ni desenfoques (de zona y de movimiento) del juego
export GALAXY_NO_DOF="${GALAXY_NO_DOF:-1}"
export GALAXY_NO_BLUR="${GALAXY_NO_BLUR:-1}"
#   GALAXY_GLES_AA=1                     suaviza los bordes dentados (antialiasing tipo FXAA en la misma copia); 0: apagado
export GALAXY_GLES_AA="${GALAXY_GLES_AA:-1}"
export NEBULA_PAD_SCHEME="${NEBULA_PAD_SCHEME:-nintendo}"
#   GALAXY_SHAKE_HOLD=1|0                1 (por defecto): mantener un boton de giro (X, Y o R2) sigue sacudiendo y el juego
#                                        ve la sacudida en todos los frames: Mario sube las enredaderas y los tallos con el
#                                        boton apretado; tocarlo sigue siendo un giro. 0: una sacudida por pulsacion, como antes
#   GALAXY_LANGUAGE=spanish|english|french  idioma del juego (textos y menus; el disco USA trae los tres). Sin fijarlo
#                                         aqui vale el que se eligio al instalar con Cactus Galaxy Wrapper
# El idioma elegido en Cactus Galaxy Wrapper (smg/idioma.txt; SELECT mantenido al abrir lo cambia), si no se fija aqui.
game_variant=
if [ -z "${GALAXY_LANGUAGE:-}" ] && [ -f "$GAMEDIR/idioma.txt" ]; then
  # "english", "english ptbr" o "english ru" (la traduccion PT-BR o RU va en ese idioma del juego, como mod)
  read -r GALAXY_LANGUAGE game_variant < "$GAMEDIR/idioma.txt"
  [ "${game_variant:-}" = ptbr ] && echo "idioma del juego: portugues de Brasil (parche PT-BR)"
  [ "${game_variant:-}" = ru ] && echo "idioma del juego: ruso (parche RU)"
fi
case "${GALAXY_LANGUAGE:-}" in spanish|english|french) ;; *) GALAXY_LANGUAGE=spanish ;; esac
export GALAXY_LANGUAGE
# Traducciones como mods (Cactus Galaxy Wrapper las deja en smg/patches/idioma-<clave>: lo que cambia el .xdelta del
# PT-BR, o el ruso que hace cactus-ruso): la del idioma elegido va a smg/mods (donde el juego busca mods) y las demas
# salen de ahi. Con un idioma propio del juego ("english", sin variante) no queda ninguna: el juego tal cual.
mkdir -p "$GAMEDIR/mods"
# El ruso de un paquete de prueba anterior (smg/mods/ruso-switch) quedaria activo siempre: se quita (ahora es idioma-ru).
rm -rf "$GAMEDIR/mods/ruso-switch" "$GAMEDIR/patches/ruso-switch"
for idioma_mod in ptbr ru; do
  mod_off="$GAMEDIR/patches/idioma-$idioma_mod"
  mod_on="$GAMEDIR/mods/idioma-$idioma_mod"
  if [ "${game_variant:-}" = "$idioma_mod" ]; then
    if [ -d "$mod_off" ]; then rm -rf "$mod_on" && mv "$mod_off" "$mod_on" && echo "parche de idioma $idioma_mod activado"; fi
  elif [ -d "$mod_on" ]; then
    rm -rf "$mod_off" && mv "$mod_on" "$mod_off" && echo "parche de idioma $idioma_mod desactivado"
  fi
done
echo "idioma del juego: $GALAXY_LANGUAGE"
#   GALAXY_EXPERIMENTAL_NATIVE_4_3=1     el modo 4:3 del propio juego (la pantalla es 4:3); 0: 16:9 con franjas
export GALAXY_EXPERIMENTAL_NATIVE_4_3="${GALAXY_EXPERIMENTAL_NATIVE_4_3:-1}"
# El host SDL activa el aspecto dinamico por defecto, que no se combina con el 4:3 fijo.
if [ "$GALAXY_EXPERIMENTAL_NATIVE_4_3" = "1" ]; then export GALAXY_EXPERIMENTAL_DYNAMIC_ASPECT=0; fi
#   NEBULA_SHOTS=1                       una captura (shots/present_N.ppm) cada 30 s, 10 como mucho (diagnostico)
if [ "${NEBULA_SHOTS:-1}" = "1" ]; then
  mkdir -p "$GAMEDIR/shots"
  export GALAXY_PRESENT_SNAPSHOT_DIR="$GAMEDIR/shots" GALAXY_PRESENT_SNAPSHOT_EVERY=1800 GALAXY_PRESENT_SNAPSHOT_LAST=18000
fi
#   NEBULA_PROFILE=150                   a los 150 s (y 30 y 60 s despues) anota en log.txt donde gastan su tiempo los hilos
export NEBULA_PROFILE="${NEBULA_PROFILE:-150}"

# ULTRA-GLES (medicion del rendimiento grafico; docs/RG35XXH.md):
#   NEBULA_ULTRA=0|1|bench               1: mide la partida; bench: recorre solo las galaxias (15 s de juego cada una
#                                        mas su carga: ~15-20 min las 44; con una partida aparte, sin tocar la tuya) y sale. Resultados en
#                                        smg/ultra/<fecha>[-etiqueta]/informe.txt (y frames.csv, resumen.json, capturas/)
#   NEBULA_ULTRA_ETIQUETA=nombre         nombre para la carpeta (p. ej. el ajuste que pruebas)
#   NEBULA_ULTRA_GALAXIAS=n              bench: solo las n primeras galaxias (pruebas cortas)
#   NEBULA_ULTRA=serie                   varios recorridos seguidos, uno por seccion de smg/ultra/serie.cfg ([nombre] y
#                                        sus NOMBRE=valor), y al final smg/ultra/<fecha>-serie/comparacion.txt
#   NEBULA_ULTRA_HILOS=1                 ademas, donde gasta el tiempo cada hilo, cada 20 s (lineas [sampler] de log.txt)
#   Comparar dos: python3 smg/ultra/compare.py smg/ultra/<A> smg/ultra/<B>
#   (Todos estos ajustes se pueden poner tambien en smg/ultra.cfg: ver "Ajustes" arriba.)
NEBULA_ULTRA="${NEBULA_ULTRA:-0}"
nand_dir="$GAMEDIR/nand"
[ "$NEBULA_ULTRA" = 0 ] && echo "ultra: apagado (para medir: NEBULA_ULTRA=bench en smg/ultra.cfg; ver smg/ultra/LEEME.txt)"
if [ "$NEBULA_ULTRA" != 0 ]; then
  etiqueta=$(printf "%s" "${NEBULA_ULTRA_ETIQUETA:-}" | tr -c "A-Za-z0-9._-" "_")
  [ "$NEBULA_ULTRA" = serie ] && etiqueta=serie
  ultra_dir="$GAMEDIR/ultra/$(date +%Y%m%d-%H%M%S)${etiqueta:+-$etiqueta}"
  mkdir -p "$ultra_dir"
  export GALAXY_ULTRA_DIR="$ultra_dir"
  if [ "$NEBULA_ULTRA" = bench ] || [ "$NEBULA_ULTRA" = serie ]; then
    export GALAXY_ULTRA=bench GALAXY_TEST_GALAXY_TOUR=auto GALAXY_TEST_GALAXY_TOUR_FRAMES="${NEBULA_ULTRA_FRAMES:-900}"
    [ -n "${NEBULA_ULTRA_GALAXIAS:-}" ] && export GALAXY_TEST_GALAXY_TOUR_LIMIT="$NEBULA_ULTRA_GALAXIAS"
    nand_dir="$GAMEDIR/ultra/nand-bench"
    # Un recorrido sin nadie delante: si el juego se queda en una pantalla fuera de escena (el aviso de Nunchuk
    # desconectado) pulsa A solo; si no, el recorrido no avanza y la serie no termina.
    export NEBULA_AUTO_RECONNECT="${NEBULA_AUTO_RECONNECT:-1}"
    # Nada que lea la imagen por su cuenta: serian tirones que no son del juego.
    unset GALAXY_PRESENT_SNAPSHOT_DIR GALAXY_PRESENT_SNAPSHOT_EVERY
    if [ "${NEBULA_ULTRA_HILOS:-0}" = 1 ]; then
      export NEBULA_PROFILE=20 NEBULA_PROFILE_EVERY=20 NEBULA_PROFILE_ROUNDS=200
      export NEBULA_PROFILE_DUMP="$ultra_dir/muestras.txt" GALAXY_GX_FRAME_TIMING_SAMPLE=30
    else
      export NEBULA_PROFILE=0
    fi
  else
    export GALAXY_ULTRA=1
  fi
  mkdir -p "$nand_dir"
  echo "ultra: $NEBULA_ULTRA -> $ultra_dir"
fi
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

#   NEBULA_PERF=1                        CPU, GPU y memoria a su frecuencia maxima mientras se juega (0: lo que tenga el sistema)
PERF_SAVED=""
perf_restore() {  # al reves de como se cambiaron (el minimo vuelve antes que el maximo)
  local list="" entry
  for entry in $PERF_SAVED; do list="$entry $list"; done
  for entry in $list; do
    echo "${entry#*=}" > "${entry%%=*}" 2>/dev/null || { [ -n "${ESUDO:-}" ] && echo "${entry#*=}" | $ESUDO tee "${entry%%=*}" >/dev/null 2>&1; }
  done
  PERF_SAVED=""
}
perf_set() {  # fichero valor: lo escribe y apunta el valor anterior para restaurarlo al salir
  local old; old=$(cat "$1" 2>/dev/null)
  [ "$old" = "$2" ] && return 0
  if echo "$2" > "$1" 2>/dev/null || { [ -n "${ESUDO:-}" ] && echo "$2" | $ESUDO tee "$1" >/dev/null 2>&1; }; then
    PERF_SAVED="$PERF_SAVED $1=$old"; echo "rendimiento: $1 $old -> $2"
  else echo "rendimiento: no se pudo escribir $2 en $1 (sigue en $old)"; fi
}
top_freq() { [ -r "$1" ] || return 0; tr ' ' '\n' < "$1" | grep -E '^[0-9]+$' | sort -n | tail -1; }
# Como estan las frecuencias (diagnostico).
for dir in /sys/devices/system/cpu/cpufreq/policy*; do
  [ -d "$dir" ] && echo "cpu $(basename "$dir"): $(cat "$dir/scaling_governor" 2>/dev/null) [$(cat "$dir/scaling_available_governors" 2>/dev/null)] $(cat "$dir/scaling_cur_freq" 2>/dev/null) kHz ($(cat "$dir/scaling_min_freq" 2>/dev/null)-$(cat "$dir/scaling_max_freq" 2>/dev/null), hasta $(cat "$dir/cpuinfo_max_freq" 2>/dev/null))"
done
for dir in /sys/class/devfreq/*; do
  [ -d "$dir" ] && echo "devfreq $(basename "$dir"): $(cat "$dir/governor" 2>/dev/null) [$(cat "$dir/available_governors" 2>/dev/null)] $(cat "$dir/cur_freq" 2>/dev/null) Hz ($(cat "$dir/min_freq" 2>/dev/null)-$(cat "$dir/max_freq" 2>/dev/null); $(cat "$dir/available_frequencies" 2>/dev/null))"
done
# Que da la bateria (diagnostico; ULTRA-GLES mide el consumo con lo que encuentre aqui).
for dir in /sys/class/power_supply/*; do
  [ -d "$dir" ] || continue
  info=""
  for f in type status capacity power_now power_avg current_now current_avg voltage_now voltage_avg voltage_ocv \
           energy_now charge_now charge_full; do
    [ -r "$dir/$f" ] && info="$info $f=$(head -c 40 "$dir/$f" 2>/dev/null | tr -d '\n')"
  done
  echo "bateria $(basename "$dir"):${info:- (sin datos legibles)}"
done
if [ "${NEBULA_PERF:-1}" = "1" ]; then
  for dir in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$dir" ] || continue
    if grep -qw performance "$dir/scaling_available_governors" 2>/dev/null; then
      perf_set "$dir/scaling_governor" performance
      # RK3326: el tope al maximo del chip (con "performance" la CPU va a ese tope).
      if [ "$soc_cpu" = chip ]; then
        top=$(cat "$dir/cpuinfo_max_freq" 2>/dev/null)
        [ -n "$top" ] && perf_set "$dir/scaling_max_freq" "$top"
      fi
    else
      top=$(cat "$dir/cpuinfo_max_freq" 2>/dev/null)
      [ -n "$top" ] && perf_set "$dir/scaling_max_freq" "$top" && perf_set "$dir/scaling_min_freq" "$top"
    fi
  done
  for dir in /sys/class/devfreq/*; do
    case "$(basename "$dir")" in *gpu*|*mali*|*1800000*|*dmc*) ;; *) continue ;; esac
    if grep -qw performance "$dir/available_governors" 2>/dev/null; then
      perf_set "$dir/governor" performance
    fi
    # Sin "performance" (o si el driver no le hace caso): el minimo en la frecuencia mas alta (la de su lista o, si
    # el driver no la da, su max_freq).
    top=$(top_freq "$dir/available_frequencies")
    [ -n "$top" ] || top=$(cat "$dir/max_freq" 2>/dev/null)
    if [ -n "$top" ]; then
      perf_set "$dir/max_freq" "$top"
      perf_set "$dir/min_freq" "$top"
    fi
  done
  # Con el pm_message_end de funcs.txt (su propio trap EXIT), que este reemplazaria.
  trap 'perf_restore; type pm_message_end >/dev/null 2>&1 && pm_message_end' EXIT
fi

# Salir: la combinacion del firmware con Start (el juego la atiende; gptokeyb mata con la misma si no contesta):
# MENU (HOTKEY=guide) en KNULLI y muOS; SELECT (el de gptokeyb sin HOTKEY) en ArkOS, dArkOS y el resto.
if [ "$NEBULA_QUIT_COMBO" = menu ]; then export HOTKEY=guide; else unset HOTKEY; fi
$GPTOKEYB "nebula" &
pm_platform_helper "$GAMEDIR/nebula"
# Memoria del juego y libre en el sistema, cada 60 s (diagnostico para el perfil bajo).
( while sleep 60; do
    pid=$(pidof nebula 2>/dev/null | cut -d' ' -f1); [ -n "$pid" ] || continue
    gpu=""; for f in /sys/class/devfreq/*/cur_freq; do case "$f" in *gpu*|*mali*|*1800000*) gpu="$(cat "$f" 2>/dev/null)";; esac; done
    echo "memoria: nebula $(grep VmRSS /proc/$pid/status | tr -s ' ' | cut -d' ' -f2) kB, libre $(grep MemAvailable /proc/meminfo | tr -s ' ' | cut -d' ' -f2) kB; cpu $(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2>/dev/null) kHz, gpu $gpu Hz; temp $(cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | tr '\n' ' ')mC"
  done ) &
MEMLOG_PID=$!
# NEBULA_CPU_MAX=1: el tope de frecuencia de la CPU al maximo del chip (cpuinfo_max_freq; en la RG35XX H 1,7 GHz frente a
# los 1,5 del sistema), solo mientras corre el recorrido de una seccion de la serie; al terminar se devuelve lo que habia.
# Mas calor y consumo: para medir si la CPU del juego es el limite.
cpu_max_on() {
  CPU_MAX_SAVED=""
  for dir in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -d "$dir" ] || continue
    top=$(cat "$dir/cpuinfo_max_freq" 2>/dev/null)
    [ -n "$top" ] || continue
    CPU_MAX_SAVED="$CPU_MAX_SAVED $dir:$(cat "$dir/scaling_max_freq" 2>/dev/null):$(cat "$dir/scaling_min_freq" 2>/dev/null)"
    echo "$top" > "$dir/scaling_max_freq" 2>/dev/null || { [ -n "${ESUDO:-}" ] && echo "$top" | $ESUDO tee "$dir/scaling_max_freq" >/dev/null 2>&1; }
    echo "$top" > "$dir/scaling_min_freq" 2>/dev/null || { [ -n "${ESUDO:-}" ] && echo "$top" | $ESUDO tee "$dir/scaling_min_freq" >/dev/null 2>&1; }
    echo "  cpu max: $(basename "$dir") tope $top kHz, ahora $(cat "$dir/scaling_cur_freq" 2>/dev/null) kHz"
  done
}
cpu_max_off() {
  for item in $CPU_MAX_SAVED; do
    dir=${item%%:*}; rest=${item#*:}; max=${rest%%:*}; min=${rest#*:}
    [ -n "$min" ] && { echo "$min" > "$dir/scaling_min_freq" 2>/dev/null || { [ -n "${ESUDO:-}" ] && echo "$min" | $ESUDO tee "$dir/scaling_min_freq" >/dev/null 2>&1; }; }
    [ -n "$max" ] && { echo "$max" > "$dir/scaling_max_freq" 2>/dev/null || { [ -n "${ESUDO:-}" ] && echo "$max" | $ESUDO tee "$dir/scaling_max_freq" >/dev/null 2>&1; }; }
  done
  CPU_MAX_SAVED=""
}
# comparacion.txt de la serie: se rehace tras cada recorrido (si se cierra el juego a medias queda lo medido hasta
# entonces). Sin Python no se puede comparar: el archivo lo dice y los resumen.json de cada carpeta siguen ahi.
serie_compare() {
  # compare.py pide dos carpetas como minimo: con el primer recorrido solo no hay nada que comparar todavia.
  [ "$(printf "%s\n" $runs | wc -l)" -ge 2 ] || return 0
  py=""
  for c in python3 python /usr/bin/python3 /usr/local/bin/python3 /opt/python/bin/python3; do
    command -v "$c" >/dev/null 2>&1 && { py="$c"; break; }
  done
  if [ -z "$py" ]; then
    printf 'No hay Python en esta consola: no se pudo comparar. Los resultados estan en cada carpeta (resumen.json).\nEn un PC: python3 compare.py%s\n' "$runs" > "$serie_dir/comparacion.txt"
    echo "ultra: sin python, no se puede comparar (ver $serie_dir/comparacion.txt)"
    return 0
  fi
  # shellcheck disable=SC2086
  if "$py" "$GAMEDIR/ultra/compare.py" $runs > "$serie_dir/comparacion.txt" 2>&1; then
    echo "ultra: comparacion en $serie_dir/comparacion.txt"
  else
    echo "ultra: compare.py fallo (ver $serie_dir/comparacion.txt)"
  fi
}
if [ "$NEBULA_ULTRA" = serie ]; then
  # Un recorrido por seccion de smg/ultra/serie.cfg, con sus ajustes encima de los de arriba; luego la comparacion.
  serie_cfg="$GAMEDIR/ultra/serie.cfg"
  serie_dir="$GALAXY_ULTRA_DIR"
  names=$(sed -n 's/^[[:space:]]*\[\([A-Za-z0-9._-]*\)\][[:space:]]*\r*$/\1/p' "$serie_cfg" 2>/dev/null)
  [ -n "$names" ] || { echo "ultra: serie sin secciones en $serie_cfg"; names=base; }
  runs=""
  status=0
  for name in $names; do
    run_dir="$serie_dir/$name"
    mkdir -p "$run_dir"
    echo "ultra: serie, recorrido $name -> $run_dir"
    (
      export GALAXY_ULTRA_DIR="$run_dir" NEBULA_ULTRA_ETIQUETA="$name"
      while IFS= read -r line || [ -n "$line" ]; do
        line=${line%$'\r'}
        key=$(printf '%s' "${line%%=*}" | tr -d ' \t')
        value=$(printf '%s' "${line#*=}" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
        case "$key" in NEBULA_*|GALAXY_*) export "$key=$value"; echo "  $name: $key=$value" ;; esac
      done < <(awk -v want="$name" '
        { sub(/\r$/, "") }
        /^[[:space:]]*\[/ { section = $0; gsub(/[][[:space:]]/, "", section); next }
        section == want && $0 ~ /=/ && $0 !~ /^[[:space:]]*#/ { print }' "$serie_cfg")
      # Muestreo de hilos en esta seccion: las pilas completas a muestras.txt de su carpeta.
      case "${NEBULA_PROFILE:-0}" in 0|'') ;; *) export NEBULA_PROFILE_DUMP="${NEBULA_PROFILE_DUMP:-$run_dir/muestras.txt}" ;; esac
      # NEBULA_ULTRA_BIN=nebula-xxx: otra version del juego compilada con otro ajuste (p. ej. smg/nebula-f4, cuatro
      # frames en vuelo), para medirla contra la normal.
      bin="${NEBULA_ULTRA_BIN:-nebula}"
      [ -x "$GAMEDIR/$bin" ] || { [ -f "$GAMEDIR/$bin" ] && chmod +x "$GAMEDIR/$bin"; }
      [ -x "$GAMEDIR/$bin" ] || { echo "  $name: falta smg/$bin, se usa nebula"; bin=nebula; }
      echo "  $name: $bin ($(md5sum "$GAMEDIR/$bin" | cut -c1-12))"
      # Un recorrido que se cuelga no para la serie: a los NEBULA_ULTRA_LIMITE segundos (900) se corta y sigue la
      # siguiente seccion (su informe parcial queda, se escribe cada 20 s).
      [ "${NEBULA_CPU_MAX:-0}" = "1" ] && cpu_max_on
      if command -v timeout >/dev/null 2>&1 && timeout -k 1 5 true >/dev/null 2>&1; then
        timeout -k 10 "${NEBULA_ULTRA_LIMITE:-900}" "./$bin" --pak "$GAMEDIR/gamedata/game.pak" --nand "$nand_dir"
      else
        "./$bin" --pak "$GAMEDIR/gamedata/game.pak" --nand "$nand_dir"
      fi
      run_status=$?
      [ "${NEBULA_CPU_MAX:-0}" = "1" ] && cpu_max_off
      exit "$run_status"
    )
    status=$?
    runs="$runs $run_dir"
    [ "$status" = 0 ] || echo "ultra: el recorrido $name termino con $status"
    if [ "$status" -ge 128 ] 2>/dev/null; then
      # Matado por una senal (137 = SIGKILL: falta de memoria o el limite de tiempo): lo que dice el kernel.
      dmesg 2>/dev/null | grep -iE "out of memory|killed process|oom" | tail -n 4
      grep -E "MemAvailable" /proc/meminfo
    fi
    serie_compare
  done
else
  ./nebula --pak "$GAMEDIR/gamedata/game.pak" --nand "$nand_dir"
  status=$?
fi
pkill -P "$MEMLOG_PID" 2>/dev/null; kill "$MEMLOG_PID" 2>/dev/null
echo "terminado: status $status"
perf_restore
pm_finish
# EmulationStation sin API (ArkOS, dArkOS, AmberELEC): guarda al cerrarse su copia de la entrada del juego y pisaria la
# ficha nueva (primera vez o idioma cambiado: smg/.ficha-pendiente). Se reinicia una vez, fuera de este proceso:
# se para, se escribe la ficha y vuelve a arrancar ya con ella.
gamelist_done=1
if [ -n "$GAMELIST_PID" ]; then  # si aun baja imagenes, se le dan 30 s; si no acaba, queda para la proxima vez
  for i in $(seq 30); do kill -0 "$GAMELIST_PID" 2>/dev/null || break; sleep 1; done
  kill -0 "$GAMELIST_PID" 2>/dev/null && gamelist_done=0
fi
if [ -f "$GAMEDIR/.ficha-pendiente" ] && [ "$gamelist_done" = 1 ]; then
  es_unit=""
  for unit in emulationstation emustation; do
    systemctl is-active --quiet "$unit" 2>/dev/null && { es_unit=$unit; break; }
  done
  if [ -n "$es_unit" ] && command -v systemd-run >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
    echo "ficha: se reinicia $es_unit para que cargue la ficha nueva"
    $ESUDO systemd-run --quiet --no-block --collect --unit="smg-ficha-$$" /bin/sh -c \
      'sleep 3; systemctl stop "$1"; "$2" "$3/gamelist.py" "$3" --tras-cerrar-es >> "$3/log.txt" 2>&1; systemctl start "$1"' \
      smg-ficha "$es_unit" "$(command -v python3)" "$GAMEDIR"
  else
    echo "ficha: escrita en ports/gamelist.xml; si no sale, START > Ajustes de juegos > Actualizar listas de juegos"
  fi
fi
