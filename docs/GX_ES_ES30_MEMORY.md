# GX-ES: memoria de la ruta de OpenGL ES 3.0

Paso aparte del paso 2: toca anillos y memoria, que el paso 2 no permitía. Solo cambia la ruta **sin
`GL_EXT_buffer_storage`**: OpenGL ES 3.0, o `GALAXY_GLES_FORCE_ES30=1`. La ruta de 3.2, la que usa la RG35XX H, no
cambia.

## El problema

En la RG35XX H con KNULLI, la ruta de 3.0 murió con `Killed` (estado 137) en las dos pruebas:

- en `pruebamezcla.zip`, en B y en C;
- en la serie de ULTRA: el recorrido es30 duró 22 s y no llegó a la primera galaxia.

En 3.0 cada buffer con dirección GX (los cuatro anillos y los vértices decodificados residentes) tiene dos copias:

- el buffer de GL;
- una copia en la CPU, que es donde escribe el código de GX.

Antes de cada draw, `flush_writes` manda lo escrito con `glBufferSubData`. Tres cosas hacían crecer la memoria:

| Qué | Cuánto | Por qué |
|---|---|---|
| La copia en la CPU, con `new` + `memset` | **Toda, desde el inicio:** 192 MiB en la prueba; en el juego, 160 MiB de anillos y 64 MiB de vértices residentes | El `memset` toca todas las páginas. Un anillo solo usa su pico por frame (vértices: 17 de 28 MiB) |
| `glBufferSubData` sobre un buffer que la GPU todavía lee | Hasta una copia entera del buffer por llamada, en drivers que hacen *ghosting* (el de la Mali lo hace) | Antes de cada draw con datos nuevos; los anillos son de 100+ MiB |
| Con el hilo de GL, la copia de los bytes en su cola | El tamaño de cada rango, hasta que el hilo lo ejecuta | `push_data` copia lo que no cabe en el registro |

El juego en 3.2 ya tiene un pico de 700 MiB y un mínimo libre de 138 MiB (`informe.txt` de ULTRA, KNULLI). Con solo la
primera de las tres, la ruta de 3.0 ya no cabía.

## El cambio

| Archivo | Qué |
|---|---|
| `runtime/src/gx/gles/gles_context.cpp`, `gles_context.h` | **La copia en la CPU con `calloc`:** en ceros sin tocar las páginas, que se ocupan al escribirse. `flush_dirty` escribe con `glthread::write_buffer`. Log por buffer copiado de 8 MiB o más. Contadores de lo que se manda (`copy_stats`) |
| `runtime/src/gx/gles/gl_thread.cpp`, `gl_thread.h` | **`write_buffer`:** `glMapBufferRange` con `GL_MAP_UNSYNCHRONIZED_BIT \| GL_MAP_INVALIDATE_RANGE_BIT`, `memcpy` y `glUnmapBuffer`. Si el mapeo falla, o si `glUnmapBuffer` dice que se perdió, vuelve a `glBufferSubData`. Con el hilo de GL, el hilo lee la copia en la CPU directamente, sin copiarla en la cola |
| `runtime/src/gx/gles/renderer_gles.cpp`, `renderer_gles.h` | En 3.0, la línea `[gles] … frames/s` agrega: escrituras y KiB mandados por frame, MiB de copias y VmRSS |
| `platform/knulli/game/gles_blend_tests.cpp` | La salida, línea por línea. Si el kernel mata la prueba, el log queda hasta el último paso; antes se perdía lo que estaba en el búfer |
| `platform/knulli/prueba-mezcla/Prueba mezcla GX.sh` | Si un modo muere por una señal, guarda lo que dice el kernel (`dmesg`: falta de memoria) y MemAvailable |

**Por qué el mapeo sin sincronizar es correcto.** Hace la misma promesa que el mapeo persistente de 3.2: ningún draw
en vuelo lee lo que se escribe.

- Un anillo solo escribe en la parte del frame actual, y esa parte se reusa después del fence de su frame.
- Un bloque de vértices residentes se escribe una vez y no se reescribe mientras es de su entrada.
- Si el rango de un flush abarca bytes de otro bloque, son los mismos bytes de la copia (el bloque no cambió) o los de un
  bloque libre, que ningún draw lee.

**Por qué la copia se puede leer en el hilo de GL más tarde.** Lo que se escribe en la copia se manda antes del draw
que lo usa. Esa parte de la copia no se reescribe hasta que pasan los frames que la usan, y el hilo de GL ya ejecutó el
flush para entonces. Si se reescribe un bloque libre mientras el hilo copia, la escritura nueva vuelve a marcar el rango,
y el flush siguiente, en orden, lo manda otra vez.

**Sin tocar:** los tamaños de los anillos y los frames en vuelo, la ruta de 3.2, GX y el juego.

## Pruebas (PC, Mesa 25.2.8 llvmpipe)

| Prueba | Resultado |
|---|---|
| `nebula_gles_blend_tests` plain/fetch, `--comparar`: 3.2, 3.0 y 3.0 con hilo de GL | Todo cumple. Las imágenes de 3.0 son **idénticas byte a byte** a las de 3.2. fetch contra la mezcla calculada: 0 con error > 1 |
| Lo mismo sin traductor, con la biblioteca de la consola (3.0 con hilo) | Cumple |
| `nebula_gles_renderer_tests`: 3.2, `FORCE_ES30` y `FORCE_ES30` + hilo | 0 fallos |
| `nebula_gl_thread_tests`, `nebula_gles_shader_tests`, `nebula_texture_compact_tests`, `nebula_fifo_display_list_tests` | Pasan |
| `armar.sh` completo para aarch64 (con `NEBULA_MEDICION=1`) | Compila. Sin rutas ni datos de la PC |

**Pico de memoria de la prueba** (VmHWM de `gl.txt`, modo fetch):

| Ruta | Antes | Ahora |
|---|---|---|
| 3.2 | 605 MiB | 607 MiB (no cambia) |
| 3.0 | **805 MiB** | **632 MiB** |
| 3.0 con hilo de GL | — | 567 MiB |

En 3.0, la memoria de más pasa de 200 MiB (las copias enteras) a 25 MiB (lo que se escribe).

## Lo que no se puede ver en la PC

- **El *ghosting* de la Mali:** Mesa no lo hace. Solo la consola dirá si `glBufferSubData` era la otra causa.
- **La memoria del juego en 3.0:** en la PC no están los datos del juego.
  - Las copias del juego que se tocan: unos 90 MiB de los anillos (4 frames × pico por frame) y hasta 64 MiB de
    vértices residentes, que se van llenando.
  - Con los 138 MiB libres que deja la ruta de 3.2, puede quedar justo.
  - Si en la consola no alcanza: `GALAXY_GX_RESIDENT_DECODED_VERTICES=0` quita los 64 MiB de vértices residentes,
    con más carga en el anillo de vértices.

## En la consola

1. `pruebamezcla.zip`: `estado es30 plain: 0`, `estado es30 fetch: 0` y `comparacion es30: 0`. Las tres rutas deben dar
   las mismas métricas. Si muere, el log trae el último paso y lo que dijo el kernel.
2. La serie de ULTRA (paquete de medición): el recorrido es30 debe llegar al final y tener `resumen.json`.
   - En su `log.txt`, las líneas `[gles] buffer '…' copied` y, en `[gles] … frames/s`, `copies … VmRSS … MiB`.
   - En su `informe.txt`, el mínimo libre del sistema.
