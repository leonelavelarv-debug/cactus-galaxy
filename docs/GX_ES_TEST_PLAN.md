# GX-ES: plan de pruebas

Commit `b73a2d86`.

- **Columna "Existe":** lo que ya hay y si corrió sobre este commit (x86-64 Linux, Mesa 25.2.8).
- **Columna "Falta":** lo que hay que agregar.

Una prueba se da por buena solo con su salida; los resultados de abajo son de corridas reales.

## Pruebas por módulo

| Módulo | Existe | Resultado en `b73a2d86` | Falta |
|---|---|---|---|
| FIFO | `runtime/src/gx/fifo_parser_tests.cpp`, `platform/knulli/game/fifo_display_list_tests.cpp` (`nebula_fifo_display_list_tests`), `owned_fifo_packet_tests.cpp` | `fifo_parser_tests`: PASS (incluye 16 777 216 transiciones del oráculo de blend); `owned_fifo_packet_tests`: PASS. Compiladas a mano en Linux contra `libgalaxy_gx_gles.a` | Agregarlas a `platform/knulli/game/CMakeLists.txt` para que corran con el port; correrlas en aarch64 (en la consola) |
| CP/XF/BP | `vs_constants_tests.cpp` (constantes del VS desde XF/CP), parte de `fifo_parser_tests` | `vs_constants_tests`: PASS (8192 imágenes) | Igual que arriba |
| Vertex Loader | `vertex_loader_tests.cpp` | **No compila fuera de Windows** (incluye `d3d12.h`) | Separar las pruebas de decodificación (todos los formatos de VAT, índices, matrices por vértice) de la parte de D3D12 |
| TEV | `shader_gen_tests.cpp` (D3D12/FXC); `nebula_gles_shader_tests` (traduce y enlaza en GLES) | `shader_gen_tests`: no compila fuera de Windows. `nebula_gles_shader_tests` en Mesa 3.0 con las 977 claves: 989 programas enlazan, 0 fallos; los 1978 shaders pasan el validador de glslang como GLSL ES 3.00 | Pruebas de resultado (no solo de compilación): un píxel por combinación de etapa (operaciones, bias, escala, comparaciones, konst, swaps, indirectas), con el valor del hardware calculado en la CPU (`rt_tev_eval.cpp` ya evalúa el TEV en la CPU para el trazador) |
| Texturas | `nebula_texture_compact_tests`; `texture_pack_tests.cpp` | Compactas: 0 fallos en 3.2 y en 3.0 | Decodificación de cada formato GX (I4…CMPR, TLUT) contra referencias; hoy solo la cubren las pruebas de Windows |
| Profundidad | `nebula_gles_renderer_tests`: peeks de Z, campo de profundidad, triángulo a z 0.25, culling | 0 fallos en 3.2, 3.2 con texture buffer, 3.2 sin buffer storage, `FORCE_ES30`, Mesa 3.0, con y sin hilo de GL | Superficies coplanares (R-06); Z temprana con descarte en 3.0 (hoy se salta y lo dice) |
| Transparencia | — | — | **Doble fuente (R-01)**; cada factor de blend de GX; resta; alfa de destino constante |
| EFB/XFB | `nebula_gles_renderer_tests`: clears, peeks de color, copia EFB, copia a pantalla con orientación, filtros (`GLES_TEST_EDGE_PPM`) | 0 fallos; la copia a pantalla da los mismos bytes en 3.2 y 3.0, con y sin AA | Cada formato de copia (R4, RA8, RGB565, Z24…) y la escala vertical, contra el shader de conversión evaluado en la CPU |
| Shader Cache | `nebula_glsl_aot --verificar`; binarios con y sin el anexo de bindings | 4894 de 4894 iguales a la traducción; 3914 de 3914 iguales a la biblioteca anterior en 3.10/3.20 | Prueba de que un programa cargado de un binario dibuja igual que uno enlazado (Mesa conserva los bindings, así que hace falta en la Mali) |
| Sincronización | `nebula_gl_thread_tests`; renderer con `GALAXY_GLES_THREAD=1`; async peeks | 0 fallos | Prueba de estrés: N frames con copias y peeks intercalados, comparando con el modo síncrono |

## Comparación de imágenes entre backends

**Qué se compara:** el EFB y el XFB de los mismos frames, dibujados por dos backends.

**Origen de los frames:** capturas `.gxdump` de la consola, o los FIFO grabados (`OwnedFifoPacket`, que ya contiene
los bytes y las dependencias de memoria de un frame). Los rinden:

- D3D12 en Windows (referencia);
- GLES 3.2 y GLES 3.0 en Mesa (PC);
- GLES en la Mali (consola, con `GALAXY_ULTRA_CAPTURE` o las capturas de prueba `set_debug_capture_paths`).

No sirve comparar capturas de partidas distintas: el juego no está en el mismo instante. Las capturas de ULTRA dieron
23-28 dB entre repeticiones por eso (`docs/RG35XXH.md`).

**Cómo:**

1. Reproducir el mismo FIFO con la misma memoria (frame determinista).
2. Leer el EFB a la resolución del Wii (640x528, escala 1, sin filtros de la copia a pantalla: `GALAXY_GLES_AA=0`,
   `GALAXY_GLES_SHARPEN=0`, `GALAXY_GLES_RESOLUTION=100`).
3. Comparar por canal y por píxel, y guardar un mapa de diferencias.

**Tolerancias**, por las diferencias de precisión entre GPUs:

| Contenido | Tolerancia | Por qué |
|---|---|---|
| Clears, copias sin filtro, peeks de Z | Exacto (0) | Valores enteros sin interpolar |
| Color de draws con TEV | ≤ 1/255 por canal en el 99.9 % de los píxeles; ≤ 2/255 en el resto | El TEV redondea a 8 bits en cada etapa; la interpolación de varyings y el muestreo bilineal difieren en el último bit entre GPUs |
| Bordes de triángulos | Se excluye una franja de 1 píxel alrededor de los bordes, o se compara la cobertura con ≤ 0.1 % de píxeles distintos | Reglas de cobertura y de punto de muestreo por GPU |
| Profundidad (z24) | ≤ 2 unidades de z24 en el interior | float32 frente a la Z de 24 bits; `fixup_clipspace` |
| Texturas con mips | ≤ 4/255 si el draw usa LOD bias (hasta resolver R-05); exacto en lo demás | Selección de mip distinta |
| Imagen final del XFB con filtros | PSNR ≥ 40 dB frente a la referencia con los mismos filtros | Los filtros mezclan muestras |

Un fallo por encima de la tolerancia se clasifica en el registro de riesgos (precisión, función que falta o error) antes
de tocar el código.

## Pruebas en la consola

| Prueba | Cómo | Criterio |
|---|---|---|
| Base 3.2 | Vuelta de ULTRA (`NEBULA_ULTRA=bench`) | 0 errores de GL; FPS y tiempos comparados con la base (`compare.py`) |
| Camino 3.0 | La misma vuelta con `GALAXY_GLES_FORCE_ES30=1` | 0 errores de GL; capturas iguales a las de 3.2 dentro de las tolerancias; costo de CPU medido |
| muOS | La vuelta en muOS | Arranca, 0 errores, mismas capturas |
| Memoria | `memoria:` en log.txt y ULTRA | Pico y mínimo libre; los picos de los anillos (`rings peak KiB`) |
| Shaders | ULTRA tras una vuelta | 0 compilados en el hilo de render; 0 % de draws con el uber |
| Doble fuente (tras R-01) | Captura de una escena con esos draws (las 241 claves dicen cuáles) | Igual a la referencia D3D12 dentro de las tolerancias |
