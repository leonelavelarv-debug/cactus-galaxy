#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ULTRA-GLES: el diagnostico de resoluciones (serie r75, r100, r50, r25, r75b de smg/ultra/serie.cfg).

Uso: python3 -I tools/ultra_resoluciones.py <carpeta de la serie> [escena ...]

Por recorrido y por escena (AstroGalaxy y las demas): FPS mostrados y dibujados, % del tiempo por debajo de 30
(imagenes de mas de 34,5 ms: mas de dos vsyncs a 60 Hz), intervalo p50/p95/p99, CPU de render, CPU del hilo GL,
esperas de slot y de swap, GPU medida por imagen dibujada, draws, indices y vertices de las vistas por imagen
dibujada, errores de GL. Despues, por escena, el ajuste GPU = A + B * pixeles del EFB (minimos cuadrados con los
cuatro tamanos) con su residuo: A es la parte que no cambia con el EFB (vertices, mosaicos, destinos de copia y
XFB de tamano fijo, costes por pasada, driver), no "coste de vertices".

Solo juego estable (carga = 0) y sin FileSelect. Las imagenes sin draws son repeticiones del salto de frames:
cuentan para los FPS mostrados y los intervalos, no para la GPU por imagen dibujada. La GPU de ULTRA llega con unos
frames de retraso y se atribuye por indice de frame: por eso se suma por escena y se divide entre las imagenes
dibujadas de esa escena, no imagen a imagen.
"""
import csv
import os
import sys

PIXELS = {"r100": 640 * 528, "r75": 480 * 396, "r75b": 480 * 396, "r50": 320 * 264, "r25": 160 * 132}
ORDER = ["r100", "r75", "r75b", "r50", "r25"]


def pct(values, q):
    values = sorted(values)
    return values[min(len(values) - 1, max(0, int(len(values) * q)))] if values else float("nan")


def stats(rows):
    t = sum(float(r["intervalo_ms"]) for r in rows)
    if t <= 0:
        return None
    drawn = [r for r in rows if float(r["draws"]) > 0]
    iv = [float(r["intervalo_ms"]) for r in rows]
    per_s = lambda k: sum(max(float(r[k]), 0.0) for r in rows) / t * 1000.0
    nd = max(len(drawn), 1)
    per_drawn = lambda k: sum(max(float(r[k]), 0.0) for r in rows) / nd
    return {
        "s": t / 1000.0,
        "fps_mostrados": len(rows) / t * 1000.0,
        "fps_dibujados": len(drawn) / t * 1000.0,
        "bajo30_pct": 100.0 * sum(v for v in iv if v > 34.5) / t,
        "p50": pct(iv, 0.5), "p95": pct(iv, 0.95), "p99": pct(iv, 0.99),
        "cpu_ms_s": per_s("cpu_ms"), "gl_cpu_ms_s": per_s("gl_cpu_ms"),
        "slot_ms_s": per_s("espera_slot_ms"), "swap_ms_s": per_s("espera_swap_ms"),
        "gpu_ms_s": per_s("gpu_ms"), "gpu_por_dibujada": per_drawn("gpu_ms"),
        "draws_por_dibujada": per_drawn("draws"), "indices_por_dibujada": per_drawn("vertices"),
        "vista_por_dibujada": per_drawn("vertices_vista") if "vertices_vista" in rows[0] else float("nan"),
        "errores_gl": sum(int(r["errores_gl"]) for r in rows),
    }


def fit(points):
    # GPU = A + B * px, least squares; returns A, B and the largest relative residual.
    n = len(points)
    if n < 3:
        return None
    sx = sum(x for x, _ in points); sy = sum(y for _, y in points)
    sxx = sum(x * x for x, _ in points); sxy = sum(x * y for x, y in points)
    den = n * sxx - sx * sx
    if den == 0:
        return None
    b = (n * sxy - sx * sy) / den
    a = (sy - b * sx) / n
    worst = max(abs(y - (a + b * x)) / y for x, y in points if y > 0)
    return a, b, worst


def main():
    serie = sys.argv[1]
    scenes_wanted = sys.argv[2:]
    runs = {}
    for name in ORDER:
        path = os.path.join(serie, name, "frames.csv")
        if os.path.exists(path):
            rows = [r for r in csv.DictReader(open(path))
                    if None not in r.values() and r["carga"] == "0" and r["escena"] not in ("FileSelect", "-", "")]
            runs[name] = rows
    keys = ["fps_mostrados", "fps_dibujados", "bajo30_pct", "p50", "p95", "p99", "cpu_ms_s", "gl_cpu_ms_s",
            "slot_ms_s", "swap_ms_s", "gpu_ms_s", "gpu_por_dibujada", "draws_por_dibujada", "indices_por_dibujada",
            "vista_por_dibujada", "errores_gl"]
    scenes = sorted({r["escena"] for rows in runs.values() for r in rows})
    groups = [("todas", None)] + [(s, s) for s in scenes if not scenes_wanted or s in scenes_wanted]
    for label, scene in groups:
        print("== %s" % label)
        print("  %-22s" % "" + "".join("%11s" % n for n in runs))
        table = {}
        for name, rows in runs.items():
            sel = rows if scene is None else [r for r in rows if r["escena"] == scene]
            table[name] = stats(sel) if sel else None
        for k in keys:
            print("  %-22s" % k + "".join("%11.2f" % table[n][k] if table[n] else "%11s" % "-" for n in runs))
        pts = [(PIXELS[n], table[n]["gpu_por_dibujada"]) for n in runs if table[n]]
        f = fit(pts)
        if f:
            a, b, worst = f
            g75 = a + b * PIXELS["r75"]
            print("  ajuste GPU por imagen dibujada = %.2f ms + %.3f ms por cada 10 000 px (residuo max %.0f %%); al "
                  "75 %%: fija %.0f %%" % (a, b * 1e4, worst * 100, 100 * a / g75 if g75 > 0 else float("nan")))


if __name__ == "__main__":
    main()
