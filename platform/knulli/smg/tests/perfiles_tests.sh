#!/usr/bin/env bash
# Los perfiles por chip del lanzador sin consola: el reconocimiento del chip y el bloque de rendimiento de
# "Super Mario Galaxy.sh" corren sobre un /sys y un /proc falsos de cada chip (H700, RK3326, RK3566, otro); se
# comprueba lo que escriben (governor, topes de CPU, GPU y memoria a su frecuencia mas alta) y que al salir vuelve todo.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
lanzador="$here/Super Mario Galaxy.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fallos=0

# El trozo del lanzador con las rutas del sistema cambiadas por la raiz falsa ($R).
trozo() {
  { sed -n '/^soc=\$(tr/,/^esac/p' "$lanzador"
    sed -n "/^PERF_SAVED=\"\"/,/^  trap 'perf_restore/p" "$lanzador" | sed "/trap 'perf_restore/d"
    echo fi
  } | sed -e 's#/sys/#$R/sys/#g' -e 's#/proc/device-tree#$R/proc/device-tree#g'
}
trozo > "$tmp/trozo.sh"

pon() { mkdir -p "$(dirname "$1")"; printf '%s\n' "$2" > "$1"; }
cpu() {  # cpu <raiz> <governor> <tope del sistema> <maximo del chip>
  local d="$1/sys/devices/system/cpu/cpufreq/policy0"
  pon "$d/scaling_available_governors" "conservative ondemand userspace powersave performance schedutil"
  pon "$d/scaling_governor" "$2"; pon "$d/scaling_max_freq" "$3"; pon "$d/scaling_min_freq" 408000
  pon "$d/cpuinfo_max_freq" "$4"; pon "$d/scaling_cur_freq" 408000
}
devfreq() {  # devfreq <raiz> <nombre> <governors> <frecuencias o -> <max_freq>
  local d="$1/sys/class/devfreq/$2"
  pon "$d/available_governors" "$3"; pon "$d/governor" simple_ondemand
  if [ "$4" != - ]; then pon "$d/available_frequencies" "$4"; fi
  pon "$d/max_freq" "$5"; pon "$d/min_freq" 0; pon "$d/cur_freq" 200000000
}
foto() { (cd "$1/sys" && find . -type f | sort | while read -r f; do printf '%s=%s\n' "$f" "$(cat "$f")"; done); }

caso() {  # caso <nombre> <compatible> <comprobaciones "ruta=valor"...>
  local nombre=$1 compatible=$2; shift 2
  local R="$tmp/$nombre"
  [ -n "$compatible" ] && { mkdir -p "$R/proc/device-tree"; printf '%b' "$compatible" > "$R/proc/device-tree/compatible"; }
  "prepara_$nombre" "$R"
  foto "$R" > "$tmp/$nombre-antes"
  # shellcheck disable=SC1090
  ( set +eu +o pipefail; R="$R"; NEBULA_PERF=1; ESUDO=""; . "$tmp/trozo.sh" > "$tmp/$nombre-log" 2>&1
    echo "soc=$soc perfil=$soc_perfil cpu=$soc_cpu" > "$tmp/$nombre-perfil"
    foto "$R" > "$tmp/$nombre-durante"
    perf_restore )
  foto "$R" > "$tmp/$nombre-despues"
  local mal=""
  for c in "$@"; do
    case "$c" in
      perfil:*) grep -q "${c#perfil:}" "$tmp/$nombre-perfil" || mal="$mal [$c: $(cat "$tmp/$nombre-perfil")]" ;;
      *) grep -qxF "./$c" "$tmp/$nombre-durante" || mal="$mal [$c: $(grep -F "./${c%%=*}=" "$tmp/$nombre-durante" || echo falta)]" ;;
    esac
  done
  cmp -s "$tmp/$nombre-antes" "$tmp/$nombre-despues" || mal="$mal [no volvio todo: $(diff "$tmp/$nombre-antes" "$tmp/$nombre-despues" | grep '^>' | head -2 | tr '\n' ' ')]"
  if [ -z "$mal" ]; then echo "ok    $nombre ($(cat "$tmp/$nombre-perfil"); $(grep -c '^rendimiento:' "$tmp/$nombre-log") cambios, restaurados)"
  else echo "FALLO $nombre:$mal"; fallos=1; fi
}

prepara_h700() {  # la RG35XX H de tu log: el DT dice h616; GPU sin "performance", maximo 696 MHz
  cpu "$1" schedutil 1512000 1704000
  devfreq "$1" gpu "simple_ondemand" "240000000 336000000 420000000 504000000 600000000 696000000" 0
}
prepara_rk3326() {  # R36S: A35, tope del sistema por debajo del chip; GPU y dmc con "performance"
  cpu "$1" ondemand 1296000 1512000
  devfreq "$1" ff400000.gpu "userspace powersave performance simple_ondemand" "200000000 300000000 400000000 480000000 520000000" 400000000
  devfreq "$1" dmc "userspace powersave performance dmc_ondemand" "194000000 328000000 450000000 666000000" 666000000
}
prepara_rk3566() {  # RG353: GPU sin lista de frecuencias (se toma su max_freq)
  cpu "$1" schedutil 1800000 1800000
  devfreq "$1" fde60000.gpu "simple_ondemand" - 800000000
}
prepara_otro() {
  cpu "$1" schedutil 1200000 1400000
  devfreq "$1" 13000000.gpu "simple_ondemand" "300000000 600000000" 600000000
}

caso h700 'anbernic,rg35xx-h\0allwinner,sun50i-h616\0' "perfil:perfil=medio cpu=sistema" \
  "devices/system/cpu/cpufreq/policy0/scaling_governor=performance" \
  "devices/system/cpu/cpufreq/policy0/scaling_max_freq=1512000" \
  "class/devfreq/gpu/min_freq=696000000" "class/devfreq/gpu/max_freq=696000000"
caso rk3326 'powkiddy,r36s\0rockchip,rk3326\0' "perfil:perfil=bajo cpu=chip" \
  "devices/system/cpu/cpufreq/policy0/scaling_governor=performance" \
  "devices/system/cpu/cpufreq/policy0/scaling_max_freq=1512000" \
  "class/devfreq/ff400000.gpu/governor=performance" "class/devfreq/ff400000.gpu/min_freq=520000000" \
  "class/devfreq/dmc/governor=performance" "class/devfreq/dmc/min_freq=666000000"
caso rk3566 'anbernic,rg353v\0rockchip,rk3566\0' "perfil:perfil=medio cpu=sistema" \
  "devices/system/cpu/cpufreq/policy0/scaling_governor=performance" \
  "devices/system/cpu/cpufreq/policy0/scaling_max_freq=1800000" \
  "class/devfreq/fde60000.gpu/min_freq=800000000"
caso otro '' "perfil:perfil=bajo cpu=sistema" \
  "devices/system/cpu/cpufreq/policy0/scaling_governor=performance" \
  "devices/system/cpu/cpufreq/policy0/scaling_max_freq=1200000" \
  "class/devfreq/13000000.gpu/min_freq=600000000"
exit "$fallos"
