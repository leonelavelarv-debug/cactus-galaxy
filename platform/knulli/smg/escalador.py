#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Escalador de Cactus Galaxy: hace smg/shaders-gles-escalador.bin a partir de smg/shaders-gles.bin, con otra copia a
# pantalla para la resolucion reducida (50 % o 75 %). Sin compilar nada: nebula lee los shaders ya traducidos de la
# biblioteca y solo cambian las 3 entradas de esa copia (GLSL ES 3.00, 3.10 y 3.20; las que llevan copy_filter).
#
#   python3 escalador.py <smg/shaders-gles.bin> <smg/shaders-gles-escalador.bin>
#
# La copia de siempre (renderer_gles.cpp, display_copy_hlsl): 4 lecturas bilineales en diagonal, FXAA en los bordes y
# nitidez fuera de ellos; agranda el EFB de 320x264 a 640 y las letras quedan borrosas (el FXAA las suaviza).
# La nueva, en la misma pasada: Catmull-Rom en 5 lecturas bilineales (la cruz de pesos, sin las esquinas), recortada al
# rango de los 4 texels mas cercanos (sin halos ni anillos) y despues nitidez con la cruz como desenfoque, tambien
# recortada. Su fuerza es la de GALAXY_GLES_SHARPEN; con 0 (y siempre al 100 %, donde nebula la pone a 0) queda la
# copia de siempre tal cual. Probada en la PC con una simulacion en numpy (texto con borde): +3,3 dB en el texto frente
# a la actual. El lanzador elige la biblioteca con NEBULA_ESCALADOR=1|0.
import re
import struct
import sys
import zlib

MAGIC, VERSION = 0x534C474E, 1


def read_library(path):
    """[(clave, bloque, texto)] en el orden de la biblioteca (los bloques agrupan shaders parecidos: comprimen mejor)."""
    data = open(path, "rb").read()
    magic, version, count, blocks = struct.unpack_from("<IIII", data, 0)
    if magic != MAGIC or version != VERSION:
        raise SystemExit("%s no es una biblioteca GLSL de esta version" % path)
    entries = [struct.unpack_from("<QIIII", data, 16 + 24 * i) for i in range(count)]
    at = 16 + 24 * count
    raw_blocks = []
    for _ in range(blocks):
        packed, raw = struct.unpack_from("<II", data, at)
        text = zlib.decompress(data[at + 8:at + 8 + packed])
        if len(text) != raw:
            raise SystemExit("bloque danado en %s" % path)
        raw_blocks.append(text)
        at += 8 + packed
    return [(key, b, o, raw_blocks[b][o:o + n]) for key, b, o, n, _ in entries], len(raw_blocks)


def write_library(path, shaders, block_count):
    """Cada shader en su bloque de antes y en el mismo orden dentro de el; las entradas ordenadas por clave."""
    contents = [[] for _ in range(block_count)]
    for key, block, offset, text in shaders:
        contents[block].append((offset, key, text))
    entries, blocks = [], []
    for index, items in enumerate(contents):
        data = b""
        for _, key, text in sorted(items):
            entries.append((key, index, len(data), len(text), 0))
            data += text
        blocks.append(data)
    entries.sort()
    out = [struct.pack("<IIII", MAGIC, VERSION, len(entries), len(blocks))]
    out += [struct.pack("<QIIII", *e) for e in entries]
    for block in blocks:
        packed = zlib.compress(block, 9)
        out.append(struct.pack("<II", len(packed), len(block)) + packed)
    open(path, "wb").write(b"".join(out))


CENTER = re.compile(r"^([ \t]*)vec4 (_\w+) = textureLod\((\w+), (_\w+) \* (_\w+), 0\.0\);\n", re.M)


def matching_brace(text, at):
    """El indice justo despues de la llave que cierra la que abre en text[at] (que debe ser '{')."""
    assert text[at] == "{"
    depth = 0
    for i in range(at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError("llaves sin cerrar")


def new_filter(indent, result, lin, fetch, pos, inv, ubo):
    lines = [
        "{",
        "    vec2 e_tc = floor(%s - vec2(0.5)) + vec2(0.5);" % pos,
        "    vec2 e_f = %s - e_tc;" % pos,
        "    vec2 e_w0 = e_f * (vec2(-0.5) + e_f * (vec2(1.0) - 0.5 * e_f));",
        "    vec2 e_w1 = vec2(1.0) + e_f * e_f * (vec2(-2.5) + 1.5 * e_f);",
        "    vec2 e_w2 = e_f * (vec2(0.5) + e_f * (vec2(2.0) - 1.5 * e_f));",
        "    vec2 e_w3 = e_f * e_f * (vec2(-0.5) + 0.5 * e_f);",
        "    vec2 e_w12 = e_w1 + e_w2;",
        "    vec2 e_p = e_tc + e_w2 / e_w12;",
        "    vec2 e_m = e_tc - vec2(1.0);",
        "    vec2 e_q = e_tc + vec2(2.0);",
        "    vec4 e_c = textureLod(%s, e_p * %s, 0.0);" % (lin, inv),
        "    vec4 e_u = textureLod(%s, vec2(e_p.x, e_m.y) * %s, 0.0);" % (lin, inv),
        "    vec4 e_d = textureLod(%s, vec2(e_p.x, e_q.y) * %s, 0.0);" % (lin, inv),
        "    vec4 e_l = textureLod(%s, vec2(e_m.x, e_p.y) * %s, 0.0);" % (lin, inv),
        "    vec4 e_r = textureLod(%s, vec2(e_q.x, e_p.y) * %s, 0.0);" % (lin, inv),
        "    float e_wc = e_w12.x * e_w12.y;",
        "    float e_wu = e_w12.x * e_w0.y;",
        "    float e_wd = e_w12.x * e_w3.y;",
        "    float e_wl = e_w0.x * e_w12.y;",
        "    float e_wr = e_w3.x * e_w12.y;",
        "    vec4 e_cr = (e_c * e_wc + e_u * e_wu + e_d * e_wd + e_l * e_wl + e_r * e_wr) / (e_wc + e_wu + e_wd + e_wl + e_wr);",
        "    ivec2 e_sz = textureSize(%s, 0) - ivec2(1);" % fetch,
        "    ivec2 e_i = clamp(ivec2(floor(e_tc)), ivec2(0), e_sz);",
        "    ivec2 e_j = clamp(e_i + ivec2(1), ivec2(0), e_sz);",
        "    vec4 e_a = texelFetch(%s, e_i, 0);" % fetch,
        "    vec4 e_b = texelFetch(%s, ivec2(e_j.x, e_i.y), 0);" % fetch,
        "    vec4 e_g = texelFetch(%s, ivec2(e_i.x, e_j.y), 0);" % fetch,
        "    vec4 e_h = texelFetch(%s, e_j, 0);" % fetch,
        "    vec4 e_lo = min(min(e_a, e_b), min(e_g, e_h));",
        "    vec4 e_hi = max(max(e_a, e_b), max(e_g, e_h));",
        "    e_cr = clamp(e_cr, e_lo, e_hi);",
        "    vec4 e_bl = (e_u + e_d + e_l + e_r) * 0.25;",
        "    vec4 e_o = clamp(e_cr + (e_cr - e_bl) * %s.copy_filter.x, e_lo, e_hi);" % ubo,
        "    %s = vec4(e_o.xyz, e_cr.w);" % result,
        "}",
    ]
    return "".join(indent + line + "\n" for line in lines)


def patch(glsl):
    texel = re.search(r"textureSize\((\w+), 0\)", glsl)
    if texel is None:
        raise ValueError("sin textureSize")
    fetch = texel.group(1)
    out, at, count = [], 0, 0
    for m in CENTER.finditer(glsl):
        if m.start() < at:
            continue
        indent, center, lin, pos, inv = m.groups()
        tail = glsl[m.end():m.end() + 400]
        if "(%s + vec2(-0.5)) * %s" % (pos, inv) not in tail.split("\n", 1)[0]:
            continue
        # El resultado: "vec4 R;" y luego (con o sin SPIRV_CROSS_BRANCH) el if del FXAA.
        decl = re.compile(r"^[ \t]*vec4 (_\w+);\n(?:[ \t]*SPIRV_CROSS_BRANCH\n)?[ \t]*if \(\((\w+)\.copy_filter\.w > 0\.5\)",
                          re.M).search(glsl, m.end())
        if decl is None:
            raise ValueError("sin el if del FXAA")
        result, ubo = decl.group(1), decl.group(2)
        first = glsl.index("{", decl.end())
        after_if = matching_brace(glsl, first)
        rest = glsl[after_if:]
        m_else = re.match(r"\s*else\s*", rest)
        if m_else is None:
            raise ValueError("sin else")
        second = after_if + m_else.end()
        end = matching_brace(glsl, second)
        end = glsl.index("\n", end) + 1
        original = glsl[m.start():end]
        decl_line = glsl[decl.start():glsl.index("\n", decl.start()) + 1]
        original = original.replace(decl_line, "", 1)
        body = "".join("    " + line + "\n" if line else "\n" for line in original.rstrip("\n").split("\n"))
        block = (indent + "vec4 %s;\n" % result +
                 indent + "if (%s.copy_filter.x <= 0.0)\n" % ubo +
                 indent + "{\n" + body + indent + "}\n" +
                 indent + "else\n" +
                 new_filter(indent, result, lin, fetch, pos, inv, ubo))
        out.append(glsl[at:m.start()])
        out.append(block)
        at = end
        count += 1
    out.append(glsl[at:])
    return "".join(out), count


def main(argv):
    if len(argv) != 3:
        print("uso: python3 escalador.py <shaders-gles.bin> <shaders-gles-escalador.bin>")
        return 2
    shaders, block_count = read_library(argv[1])
    changed = 0
    result = []
    for key, block, offset, text in shaders:
        if b"copy_filter" in text:
            glsl, count = patch(text.decode("utf-8"))
            if count == 0:
                raise SystemExit("la copia %016x no tiene el filtro esperado" % key)
            if glsl.count("{") != glsl.count("}"):
                raise SystemExit("la copia %016x quedo con llaves sin cerrar" % key)
            text = glsl.encode("utf-8")
            changed += 1
            print("copia %016x: %d lecturas cambiadas por el escalador" % (key, count))
        result.append((key, block, offset, text))
    if changed == 0:
        raise SystemExit("no hay copia a pantalla en %s" % argv[1])
    write_library(argv[2], result, block_count)
    print("%s: %d shaders, %d copias con el escalador" % (argv[2], len(result), changed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
