#!/bin/bash
# Prueba de rendimiento de Super Mario Galaxy: el pack de texturas HD y la resolucion interna al 50 % y al 75 %.
# Usa el port ya instalado (ports/smg) sin cambiarlo y con una partida aparte (smg/prueba/nand: la tuya no se toca).
# El juego recorre solo las mismas galaxias en cada prueba (15 s de juego cada una); no toques nada mientras.
# Resultados: smg/prueba/<fecha>/comparacion.txt (todas frente a la primera) y una carpeta por prueba con informe.txt.
# Las pruebas son las secciones de smg/prueba/serie.cfg (se crea la primera vez; se puede editar). GPL-3.0.
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then controlfolder="$XDG_DATA_HOME/PortMaster"
else controlfolder="/roms/ports/PortMaster"; fi
source "$controlfolder/control.txt"

# Galaxias por prueba (8: unos 5 min cada prueba, ~25 min las cinco; 44: todas, ~20 min cada una).
PRUEBA_GALAXIAS=${PRUEBA_GALAXIAS:-8}

SMG="/$directory/ports/smg"
JUEGO="/$directory/ports/Super Mario Galaxy.sh"
aviso() {
  echo "$1"
  for tty in /dev/tty0 /dev/tty1; do [ -w "$tty" ] && printf '\033c%s\n' "$1" > "$tty" 2>/dev/null; done
  sleep "${2:-6}"
}
[ -f "$JUEGO" ] && [ -x "$SMG/nebula" ] || { aviso "Prueba: falta el port (ports/Super Mario Galaxy.sh y ports/smg)."; exit 1; }
[ -f "$SMG/gamedata/game.pak" ] && [ -f "$SMG/idioma.txt" ] ||
  { aviso "Prueba: abre primero Super Mario Galaxy una vez (convierte el disco y elige idioma)."; exit 1; }
if ! ls "$SMG/data/TexturePacks/RMGE01" >/dev/null 2>&1 || [ -z "$(ls -A "$SMG/data/TexturePacks/RMGE01" 2>/dev/null)" ]; then
  aviso "Prueba: no hay pack de texturas en smg/data/TexturePacks/RMGE01. Instalalo primero."; exit 1
fi
mkdir -p "$SMG/prueba"

# compare.py (de smg/ultra en el codigo fuente): compara los resumen.json de cada prueba.
cat > "$SMG/prueba/compare.py" <<'__FIN_COMPARE__'
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ULTRA-GLES: compara dos (o mas) ejecuciones medidas con NEBULA_ULTRA (carpetas smg/ultra/<fecha>-<etiqueta>).
#
#   python3 compare.py <carpeta A> <carpeta B> [<carpeta C> ...]
#
# La primera es la base. Para cada metrica (en total y por galaxia) dice cuanto cambia y si es una mejora, un
# empeoramiento o esta dentro del ruido (umbrales abajo: con una sola ejecucion de cada lado no se puede saber mas;
# repite la prueba si un resultado importa). Compara tambien las capturas de cada galaxia (PSNR: 40 dB o mas, igual a
# la vista; menos de 30, revisar) y lista los ajustes que difieren. Solo Python 3, sin modulos de fuera: corre en la
# consola o en un PC.
#
# Ruido: las ejecuciones con los mismos ajustes que la base (una repeticion, p. ej. base2 en serie.cfg) dicen cuanto
# cambia todo sin cambiar nada. Un cambio dentro de esa diferencia se marca "=" aunque pase el umbral, y una captura
# solo se marca REVISAR si queda claramente por debajo del PSNR entre la base y su repeticion (el juego no esta en el
# mismo instante exacto en cada pasada: musica, cargas en otro hilo).
import json
import math
import os
import sys

# (clave, nombre, unidad, mejor si sube, umbral relativo, umbral absoluto)
METRICS = [
    ("fps", "FPS mostrados", "", True, 0.02, 0.5),
    ("fps_dibujados", "FPS dibujados", "", True, 0.02, 0.5),
    ("frame_ms.p95", "frame p95", "ms", False, 0.05, 0.5),
    ("frame_ms.p99", "frame p99", "ms", False, 0.05, 1.0),
    ("fps_1pct_bajo", "1% mas lento", "", True, 0.05, 1.0),
    ("tirones_min", "tirones/min", "", False, 0.20, 1.0),
    ("cpu_render_ms", "CPU render", "ms", False, 0.05, 0.2),
    ("gpu_ms", "GPU", "ms", False, 0.05, 0.2),
    ("draws", "draws/frame", "", False, 0.05, 5.0),
    ("uber_pct", "draws uber", "%", False, 0.20, 0.5),
    ("texturas_kib_s", "texturas", "KiB/s", False, 0.20, 50.0),
    ("compilados", "shaders en hilo", "", False, 0.20, 2.0),
    ("compilar_ms", "compilar", "ms", False, 0.20, 200.0),
    ("frames_lentos", "frames >25 ms", "", False, 0.10, 10.0),
    ("dl_comparadas_kib", "DL comparadas", "KiB", False, 0.10, 2.0),
    ("gl_cpu_ms", "hilo GL (CPU)", "ms", False, 0.05, 0.2),
    ("gl_espera_ms", "espera al hilo GL", "ms", False, 0.10, 0.2),
    ("bateria_mw", "bateria", "mW", False, 0.03, 50.0),
    # El recorrido es el mismo en tiempo de juego: cuanto menos dure en reloj, mas deprisa fue el juego.
    ("duracion_s", "duracion del recorrido", "s", False, 0.03, 3.0),
]

# Ajustes que no cambian lo que se mide (las etiquetas, el muestreo de hilos).
IGNORED = {"NEBULA_ULTRA_ETIQUETA", "NEBULA_PROFILE", "NEBULA_PROFILE_DUMP", "NEBULA_PROFILE_EVERY",
           "NEBULA_PROFILE_ROUNDS", "GALAXY_GX_FRAME_TIMING_SAMPLE"}


def settings(run):
    return {k: v for k, v in run.get("ajustes", {}).items() if k not in IGNORED}


def load(folder):
    path = folder if folder.endswith(".json") else os.path.join(folder, "resumen.json")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    data["_carpeta"] = os.path.dirname(path) if folder.endswith(".json") else folder
    # El consumo de la bateria con las metricas del total (n/d sin lectura o si se cargo durante la ejecucion).
    battery = data.get("bateria", {})
    if isinstance(data.get("total"), dict):
        mw = battery.get("mw_media", -1)
        data["total"]["bateria_mw"] = mw if battery.get("cargando_muestras", 0) == 0 else -1
        if isinstance(data.get("duracion_s"), (int, float)) and data["duracion_s"] > 0:
            data["total"]["duracion_s"] = data["duracion_s"]
    return data


def get(stats, key):
    value = stats
    for part in key.split("."):
        if not isinstance(value, dict) or part not in value:
            return None
        value = value[part]
    return value if isinstance(value, (int, float)) else None


def verdict(base, other, higher_better, rel, absolute, noise=0.0):
    if base is None or other is None or base < 0 or other < 0:
        return "", "  n/d"
    delta = other - base
    pct = delta / base * 100.0 if base else (0.0 if not delta else math.inf)
    if abs(delta) <= max(abs(base) * rel, absolute, noise):
        mark = "="
    elif (delta > 0) == higher_better:
        mark = "MEJOR"
    else:
        mark = "peor"
    return mark, ("%+6.1f%%" % pct) if math.isfinite(pct) else "  nuevo"


def ppm(path, step=2):
    """(width, height, bytes muestreados cada `step` pixeles en cada eje) de un PPM P6 de 8 bits."""
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError("no es un PPM P6 de 8 bits")
    width, height = int(fields[1]), int(fields[2])
    pixels = data[pos + 1:pos + 1 + width * height * 3]
    rows = []
    for y in range(0, height, step):
        row = pixels[y * width * 3:(y + 1) * width * 3]
        rows.append(b"".join(row[x * 3:x * 3 + 3] for x in range(0, width, step)))
    return width, height, b"".join(rows)


def psnr(a_path, b_path):
    wa, ha, a = ppm(a_path)
    wb, hb, b = ppm(b_path)
    if (wa, ha) != (wb, hb):
        return None, "%dx%d frente a %dx%d" % (wa, ha, wb, hb)
    se = sum((x - y) * (x - y) for x, y in zip(a, b))
    if se == 0:
        return math.inf, ""
    mse = se / len(a)
    return 10.0 * math.log10(255.0 * 255.0 / mse), ""


def label(run):
    return os.path.basename(os.path.normpath(run["_carpeta"]))


def noise_of(base_stats, repeats, key):
    """La mayor diferencia entre la base y sus repeticiones (mismos ajustes) en esta metrica."""
    base = get(base_stats, key)
    values = [get(r, key) for r in repeats]
    values = [v for v in values if v is not None]
    if base is None or not values:
        return 0.0
    return max(abs(v - base) for v in values)


def table(title, base_stats, others, names, repeats=()):
    print("\n" + title)
    header = "  %-16s %10s" % ("", names[0][:10])
    for name in names[1:]:
        header += "  %10s %7s %-5s" % (name[:10], "cambio", "")
    print(header)
    for key, name, unit, higher, rel, absolute in METRICS:
        base = get(base_stats, key)
        noise = noise_of(base_stats, repeats, key)
        line = "  %-16s %10s" % (name + (" (%s)" % unit if unit else ""), "n/d" if base is None else "%.2f" % base)
        for stats in others:
            value = get(stats, key)
            mark, change = verdict(base, value, higher, rel, absolute, noise)
            line += "  %10s %7s %-5s" % ("n/d" if value is None else "%.2f" % value, change, mark)
        print(line)


def main(argv):
    if len(argv) < 3:
        print("uso: python3 compare.py <carpeta A> <carpeta B> [<carpeta C> ...]")
        return 2
    folders = [f for f in argv[1:] if os.path.isfile(f if f.endswith(".json") else os.path.join(f, "resumen.json"))]
    for f in argv[1:]:
        if f not in folders:
            print("(sin resumen.json, no se compara: %s)" % f)
    if len(folders) < 2:
        print("faltan recorridos medidos para comparar")
        return 2
    runs = [load(folder) for folder in folders]
    names = [label(r) for r in runs]
    base = runs[0]
    print("ULTRA-GLES: comparacion (base: %s)" % names[0])
    for run in runs:
        mem = run.get("memoria", {})
        bat = run.get("bateria", {})
        battery = ""
        if bat.get("mw_media", -1) >= 0:
            battery = ", bateria %d%% -> %d%%%s" % (bat.get("pct_inicio", -1), bat.get("pct_fin", -1),
                                                   " (CARGANDO)" if bat.get("cargando_muestras", 0) else "")
        print("  %-28s %s, %s, %.0f s, memoria pico %.0f MiB%s" % (
            label(run), run.get("inicio", "?"), run.get("modo", "?"), run.get("duracion_s", 0),
            mem.get("rss_pico_kib", 0) / 1024.0, battery))

    # Los ajustes que cambian entre ejecuciones.
    keys = sorted(set().union(*[set(r.get("ajustes", {})) for r in runs]))
    differing = [k for k in keys if k not in IGNORED and
                 len({r.get("ajustes", {}).get(k, "(sin poner)") for r in runs}) > 1]
    if differing:
        print("\nAjustes que difieren:")
        for k in differing:
            print("  %-36s %s" % (k, " | ".join(r.get("ajustes", {}).get(k, "(sin poner)") for r in runs)))
    else:
        print("\nMismos ajustes en todas (lo que cambie es ruido o el codigo).")

    repeats = [r for r in runs[1:] if settings(r) == settings(base)]
    if repeats:
        print("\nRepeticiones de la base (mismos ajustes): %s. Lo que cambie menos que ellas es ruido (\"=\")."
              % ", ".join(label(r) for r in repeats))
    table("Total (juego estable)", base.get("total", {}), [r.get("total", {}) for r in runs[1:]], names,
          [r.get("total", {}) for r in repeats])

    # Por galaxia: las que estan en la base y en cada otra.
    scenes = [s["nombre"] for s in base.get("escenas", []) if s.get("estable", {}).get("frames", 0) >= 30]
    by_name = [{s["nombre"]: s for s in r.get("escenas", [])} for r in runs]
    print("\nPor galaxia (FPS dibujados, frame p99 ms, tirones):")
    for scene in scenes:
        b = by_name[0][scene]["estable"]
        line = "  %-26.26s %5.1f %6.1f %4d" % (scene, get(b, "fps_dibujados") or 0, get(b, "frame_ms.p99") or 0,
                                              b.get("tirones", 0))
        for other in by_name[1:]:
            if scene not in other:
                line += "  |  (no esta)"
                continue
            o = other[scene]["estable"]
            mark, change = verdict(get(b, "fps_dibujados"), get(o, "fps_dibujados"), True, 0.02, 0.5)
            pmark, _ = verdict(get(b, "frame_ms.p99"), get(o, "frame_ms.p99"), False, 0.05, 1.0)
            line += "  |  %5.1f %6.1f %4d  %-5s" % (get(o, "fps_dibujados") or 0, get(o, "frame_ms.p99") or 0,
                                                  o.get("tirones", 0), mark if mark != "=" else pmark)
        print(line)

    # Capturas.
    base_shots = os.path.join(base["_carpeta"], "capturas")
    if os.path.isdir(base_shots):
        print("\nCapturas (PSNR frente a la base; inf = identicas, >= 40 dB igual a la vista; REVISAR: < 30 dB y")
        print("  3 dB o mas por debajo de la repeticion de la base, cuando la hay):")
        for name in sorted(os.listdir(base_shots)):
            if not name.endswith(".ppm"):
                continue

            def shot_psnr(run):
                other = os.path.join(run["_carpeta"], "capturas", name)
                if not os.path.isfile(other):
                    return None, "(falta)"
                try:
                    return psnr(os.path.join(base_shots, name), other)
                except (OSError, ValueError) as error:
                    return None, str(error)

            floors = [v for v, _ in (shot_psnr(r) for r in repeats) if v is not None]
            floor = min(floors) if floors else math.inf
            line = "  %-30s" % name[:-4][:30]
            for run in runs[1:]:
                value, why = shot_psnr(run)
                if value is None:
                    line += "  %12s" % why[:12]
                else:
                    flag = "" if value >= 30 or (run not in repeats and value >= floor - 3.0) or run in repeats \
                        else " REVISAR"
                    line += "  %9s dB%s" % ("inf" if math.isinf(value) else "%.1f" % value, flag)
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
__FIN_COMPARE__

# Las pruebas, en un orden que alterna (el calor de la consola sube con el tiempo) y con la primera repetida al final:
# lo que cambie entre esas dos es el ruido, y compare.py no marca como mejora o peor nada dentro de el.
[ -f "$SMG/prueba/serie.cfg" ] || cat > "$SMG/prueba/serie.cfg" <<'__FIN_SERIE__'
# Una prueba por seccion, en este orden; cada una con sus NOMBRE=valor encima de tus ajustes.
# GALAXY_TEXTURE_PACKS=1|0 (pack HD puesto o quitado), GALAXY_GLES_RESOLUTION=50|75|100 (resolucion interna).
[normal-75]
GALAXY_TEXTURE_PACKS=0
GALAXY_GLES_RESOLUTION=75
[hd-75]
GALAXY_TEXTURE_PACKS=1
GALAXY_GLES_RESOLUTION=75
[hd-50]
GALAXY_TEXTURE_PACKS=1
GALAXY_GLES_RESOLUTION=50
[normal-50]
GALAXY_TEXTURE_PACKS=0
GALAXY_GLES_RESOLUTION=50
[normal-75-otra]
GALAXY_TEXTURE_PACKS=0
GALAXY_GLES_RESOLUTION=75
__FIN_SERIE__

# Lo que hace cada prueba, ya dentro del lanzador del port (mismos ajustes, perfil, frecuencias y salida).
cat > "$SMG/prueba/correr.sh" <<'__FIN_CORRER__'
serie_dir="$GAMEDIR/prueba/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$serie_dir" "$GAMEDIR/prueba/nand"
export GALAXY_ULTRA=bench GALAXY_TEST_GALAXY_TOUR=auto GALAXY_TEST_GALAXY_TOUR_FRAMES=900
export GALAXY_TEST_GALAXY_TOUR_LIMIT="${PRUEBA_GALAXIAS:-8}" NEBULA_AUTO_RECONNECT=1 NEBULA_PROFILE=0
unset GALAXY_PRESENT_SNAPSHOT_DIR GALAXY_PRESENT_SNAPSHOT_EVERY
limite=$((180 + ${PRUEBA_GALAXIAS:-8} * 75))
names=$(sed -n 's/^[[:space:]]*\[\([A-Za-z0-9._-]*\)\][[:space:]]*\r*$/\1/p' "$GAMEDIR/prueba/serie.cfg")
py=""; for c in python3 python; do command -v "$c" >/dev/null 2>&1 && { py=$c; break; }; done
runs=""
for name in $names; do
  run_dir="$serie_dir/$name"
  mkdir -p "$run_dir"
  echo "prueba: $name -> $run_dir"
  (
    export GALAXY_ULTRA_DIR="$run_dir" NEBULA_ULTRA_ETIQUETA="$name"
    while IFS= read -r line; do
      key=$(printf '%s' "${line%%=*}" | tr -d ' \t'); value=$(printf '%s' "${line#*=}" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
      case "$key" in NEBULA_*|GALAXY_*) export "$key=$value"; echo "  $name: $key=$value" ;; esac
    done < <(awk -v want="$name" '{ sub(/\r$/, "") } /^[[:space:]]*\[/ { s = $0; gsub(/[][[:space:]]/, "", s); next }
                                   s == want && /=/ && !/^[[:space:]]*#/ { print }' "$GAMEDIR/prueba/serie.cfg")
    if command -v timeout >/dev/null 2>&1; then
      timeout -k 10 "$limite" ./nebula --pak "$GAMEDIR/gamedata/game.pak" --nand "$GAMEDIR/prueba/nand"
    else
      ./nebula --pak "$GAMEDIR/gamedata/game.pak" --nand "$GAMEDIR/prueba/nand"
    fi
  )
  echo "prueba: $name termino con $?"
  runs="$runs $run_dir"
  if [ -n "$py" ] && [ "$(printf '%s\n' $runs | wc -l)" -ge 2 ]; then
    # shellcheck disable=SC2086
    "$py" "$GAMEDIR/prueba/compare.py" $runs > "$serie_dir/comparacion.txt" 2>&1
  fi
done
[ -n "$py" ] || echo "sin python: compara en la PC con python3 compare.py$runs" > "$serie_dir/comparacion.txt"
echo "prueba: resultados en $serie_dir/comparacion.txt"
true
__FIN_CORRER__

# El lanzador del port tal cual, con dos cambios en la copia: su log va a smg/prueba/log.txt y, en vez de una partida,
# corre las pruebas. Si el lanzador no es el esperado, no se adivina: se avisa.
juego_linea='./nebula --pak "$GAMEDIR/gamedata/game.pak" --nand "$nand_dir"'
log_linea='exec > >(tee "$GAMEDIR/log.txt") 2>&1'
if [ "$(grep -cxF "$juego_linea" "$JUEGO")" != 1 ] || [ "$(grep -cxF "$log_linea" "$JUEGO")" != 1 ]; then
  aviso "Prueba: este Super Mario Galaxy.sh no es el que espera la prueba (actualiza el port)."; exit 1
fi
awk -v j="$juego_linea" -v l="$log_linea" '
  $0 == j { print "source \"$GAMEDIR/prueba/correr.sh\""; next }
  $0 == l { print "exec > >(tee \"$GAMEDIR/prueba/log.txt\") 2>&1"; next }
  { print }' "$JUEGO" > "$SMG/prueba/juego.sh"
aviso "Prueba de rendimiento: $(grep -c '^\[' "$SMG/prueba/serie.cfg") pruebas de $PRUEBA_GALAXIAS galaxias (unos $(( $(grep -c '^\[' "$SMG/prueba/serie.cfg") * (2 + PRUEBA_GALAXIAS / 2) )) min). No toques nada; al final vuelve solo." 8
export PRUEBA_GALAXIAS
exec bash "$SMG/prueba/juego.sh"
