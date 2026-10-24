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
    runs = [load(folder) for folder in argv[1:]]
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
