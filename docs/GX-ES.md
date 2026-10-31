# GX-ES: el backend GX de Nebula en OpenGL ES para la RG35XX H (auditoría y plan)

Auditoría del commit `f6676d56` (rama `rg35xxh-arm64`) contra el pedido de GX-ES. Se leyeron
[GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md), [ARCHITECTURE.md](ARCHITECTURE.md), [BUILDING.md](BUILDING.md) y
[RG35XXH.md](RG35XXH.md). Lo que se afirma aquí sale del código y de mediciones hechas en la consola; lo que no se ha
medido en la consola se dice.

La auditoría completa del paso 1 (mapa de componentes, compatibilidad, riesgos, plan de migración y pruebas) está en
[GX_ES_AUDIT.md](GX_ES_AUDIT.md) y los documentos que enlaza.

## Resultado de la auditoría

GX-ES no hay que crearlo desde cero: Nebula ya tiene un backend OpenGL ES que corre Super Mario Galaxy en la RG35XX H
con KNULLI (medido con ULTRA-GLES en la consola desde el 23-oct-2026). Conserva el parser, la máquina de estado GX, el
cargador de vértices, el generador de shaders y la caché de texturas, compartidos con Direct3D 12 y Vulkan; solo el
dispositivo, el renderer, la caché de programas y la subida de texturas son propios de GLES.

| Módulo pedido | Dónde está | Estado y evidencia |
|---|---|---|
| FIFO Parser | `runtime/src/gx/fifo_parser.cpp` | Compartido. Un comando, registro o puntero desconocido lanza `GxFatalError` y escribe un `.gxdump`: nunca se ignora. Pruebas `fifo_parser_tests.cpp`, `nebula_fifo_display_list_tests` |
| GX State (CP, XF, BP) | `gx_state.cpp`, `gx_bitfields.h` | Compartido; registros, vistas decodificadas y bits de cambio |
| Vertex Loader | `vertex_loader.cpp` | Compartido; cualquier descriptor y formato de vértice a un vértice canónico |
| TEV Translator | `shader_gen.cpp` (claves `shader_keys.h`) | Un programa por combinación de TEV, con redondeo a la malla de 8 bits antes de comparaciones y alpha test; texturas indirectas, iluminación por canal |
| GLSL ES Shader Generator | HLSL → GLSL ES 3.20/3.10/3.00 (`shader_translate_gles.h`), hecho en la PC (`shader_library_gles.h`, `nebula_glsl_aot`) | La consola no traduce: 4894 shaders en `smg/shaders-gles.bin`. Verificado igual a la traducción (`--verificar`) |
| UberShader | `uber_vs.hlsl`, `uber_ps.hlsl` (6 variantes) | Dibuja mientras compila el especializado y lo que falte en la biblioteca |
| Texture Decoder y Cache | `texture_cache.cpp`, `texture_compact.cpp`, `texture_pack.cpp` | Todos los formatos GX que usa Galaxy; texturas compactas en GLES (R8, RG8, 16 bits) |
| EFB/XFB Manager | `efb_copy.cpp`, `renderer_gles.cpp` | EFB fuera de pantalla, copias a textura, copia a pantalla con filtros, peeks asíncronos |
| GLES Device | `gles/gles_context.cpp`, `platform/knulli/game/host_window_knulli.cpp` | SDL2 + EGL de Mali (fbdev); contextos EGL compartidos para compilar en otros hilos |
| GLES Renderer | `gles/renderer_gles.cpp`, `gles/gl_thread.cpp` | Anillos de vértices, índices y constantes reutilizados, frames en vuelo, hilo propio para las llamadas GL |
| Pipeline Cache | `gles/pipeline_cache_gles.cpp` | Compilación asíncrona en hilos con contexto propio, binarios del driver en disco, claves persistentes, uber de respaldo |
| GX Capture y Profiler | `.gxdump`, `GALAXY_GX_CAPTURE_TEXDIR`, `gles/ultra_gles.cpp`, `gles/nopal_depurar.cpp` | CPU y GPU (timer queries) por frame, presentación, FPS medio, 1 % bajo, p95/p99, tirones con causa, memoria; Depurador de Nopal Estudio en vivo |
| Calidad adaptativa | `GALAXY_GLES_RESOLUTION=auto`, `GALAXY_FRAMESKIP=auto` | Resolución dinámica y salto de frames con ritmo estable |

## Lo que no cumple el pedido

1. **OpenGL ES 3.0 como base.** ~~Hacía falta OpenGL ES 3.1, `GL_EXT_buffer_storage` y storage o texture buffers en
   el vertex shader.~~ Resuelto en la fase de abajo: el backend arranca con un driver solo 3.0 (probado en la PC; falta
   la vuelta en la consola con `GALAXY_GLES_FORCE_ES30=1`). En la RG35XX H no cambia nada: el driver de Mali da
   OpenGL ES 3.2 (`Mali-G31 ... OpenGL ES 3.2 v1.r20p0`) y sigue el camino de siempre, con los mismos shaders.
2. **Rendimiento.** La medición base en la consola (ULTRA, 23-oct) dio 21.0 FPS mostrados, 8.2 dibujados, 1 % bajo
   1.1 FPS, GPU 13.9 ms y CPU del hilo de render 35.5 ms por frame. Las series del 29 y 30-oct bajaron el recorrido de
   6 galaxias un 10-20 % con cada ajuste (`nopal/docs/RG35XXH.md`). **No se ha llegado a 30 FPS estables.** Lo que
   frena es la CPU del hilo de render y las esperas a la GPU, no los shaders (0 % de draws con uber en estado estable).
3. **muOS.** El lanzador y el instalador conocen las rutas de muOS, pero las mediciones son de KNULLI.

## Fases

Lo que pide cada fase y lo que ya está hecho, con su evidencia. Una fase que necesita la consola no se da por
terminada sin una medición en ella.

| Fase | Estado | Evidencia | Falta |
|---|---|---|---|
| P0 EGL/GLES real en la RG35XX H | Hecha | Nebula Probe 0.1-0.3 y ULTRA en la consola (RG35XXH.md, "Mediciones en el aparato") | — |
| P1 FIFO y GX State | Hecha (compartida) | Pruebas del parser; el juego corre en la consola | — |
| P2 Vertex Loader y geometría | Hecha (compartida) | Pruebas; `nebula_gles_renderer_tests` (triángulo GX, culling) | — |
| P3 TEV y texturas | Hecha | 892 programas de la vuelta entera traducidos y enlazados (`nebula_gles_shader_tests` con las claves); `nebula_texture_compact_tests` | — |
| P4 EFB/XFB y sincronización | Hecha | `nebula_gles_renderer_tests` (clears, peeks de color y profundidad, copia EFB, copia a pantalla con orientación) | — |
| P5 Integración y arranque de Galaxy | Hecha | El juego arranca y se juega en la consola (ULTRA por escenas) | — |
| P6 Compatibilidad visual y estabilidad | En curso | 0 errores de GL en la medición base; capturas por escena (`GALAXY_ULTRA_CAPTURE`) | Revisión visual por galaxia contra una referencia |
| P7 Optimización y PortMaster | En curso | Paquete PortMaster e instalador; series ULTRA | 30 FPS estables |
| Base OpenGL ES 3.0 | Hecha en la PC | Mesa en 3.0 real y `GALAXY_GLES_FORCE_ES30=1` (abajo) | La vuelta de ULTRA en la consola con `GALAXY_GLES_FORCE_ES30=1` |

## Fase: base OpenGL ES 3.0

Objetivo: que el backend arranque con un driver solo OpenGL ES 3.0 y use lo de 3.1/3.2 y `GL_EXT_buffer_storage` solo
si está, sin cambiar lo que se dibuja con los drivers que ya funcionaban.

### Qué hace

| Lo de 3.1+ | En OpenGL ES 3.0 | Dónde |
|---|---|---|
| Contexto 3.2 o 3.1 | Prueba 3.2, 3.1 y 3.0 | `host_window_knulli.cpp` `gles_create_context`; `gles_context.cpp` exige 3.0 |
| `GL_EXT_buffer_storage`: anillos mapeados persistentes | Cada `Buffer` tiene una copia en la CPU (`mapped` apunta a ella). Quien escribe memoria de subida lo anota (`gpu::note_upload_write`: `UploadRing::allocate` y los vértices residentes de `GxBackend`); `Context::flush_writes` manda el rango escrito de cada buffer con `glBufferSubData` antes de cada draw. Con el hilo de GL, `glBufferSubData` copia los bytes al grabarse | `gles_context.h/.cpp`, `gpu_types.h`, `upload_ring.cpp`, `gx_backend.cpp`, `renderer_gles.cpp` |
| `glVertexAttribFormat`/`glBindVertexBuffer` | `glVertexAttribPointer`/`glVertexAttribIPointer` cuando cambia la dirección de los vértices, solo para las ubicaciones que lee el programa | `RendererGles::set_vertex_pointers` |
| Paleta de matrices en SSBO o texture buffer | Textura 2D RGBA32UI de 4096x16 (1 MiB, lo mismo que mira el texture buffer) en la unidad 8: se copia el rango atado (6.6 KB por paleta) desde la memoria de la CPU cuando cambia la dirección o se escribió su buffer. El shader lee `texelFetch(i % 4096, i / 4096)` | `RendererGles::bind_palette_2d`; `shader_translate_gles.cpp` (`Texture2D<uint4>`) |
| GLSL ES 3.10/3.20 con `layout(binding)` | GLSL ES 3.00: las varyings se llaman por su ubicación (`nv_<n>`, igual en ambos lados), los samplers combinados tienen nombre (`nb_tex_0_samp_0`), y los bindings de bloques y samplers van en líneas `// nebula-bind` que `apply_bindings` pone tras enlazar (`glUniformBlockBinding`, `glUniform1i`). Los binarios de programa guardan esas líneas detrás (un binario no conserva bindings según la especificación) | `shader_translate_gles.cpp`, `shader_library_gles.cpp` (`bindings_of`), `gles_context.cpp` (`apply_bindings`), `pipeline_cache_gles.cpp` |
| `layout(early_fragment_tests)` | No existe en GLSL ES 3.00: se quita (ver "Diferencia conocida") | `shader_translate_gles.cpp` |
| Biblioteca GLSL con 4 formas | Quinta forma `300t` (3.00, paleta 2D): 4894 shaders, 2.6 MiB (antes 3914, 2.1 MiB) | `glsl_aot.cpp`; `armar.sh` y `nopal nebula shaders` la incluyen sin cambios |

Elección en `Context::initialize` (el log lo dice: `palette from a 2D texture ... buffers copied, vertex attributes
pointers, OpenGL ES 3.0 context`):

- `GALAXY_GLES_FORCE_ES30=1`: solo lo de 3.0 con cualquier driver (para probar este camino en la Mali).
- `GALAXY_GLES_BUFFER_STORAGE=0`: copias en vez de buffers persistentes aunque haya la extensión.
- `GALAXY_GLES_PALETTE=2d`: la paleta en textura 2D (implica GLSL ES 3.00); `=texture` sigue siendo el texture buffer.
- `GALAXY_GLES_GLSL=300|310|320`: fuerza la versión (310 y 320 solo si el contexto la tiene).

Lo que ya funcionaba no cambia: con 3.1/3.2 el GLSL es byte a byte el de antes (`nebula_glsl_aot --verificar` de la
biblioteca anterior con las formas `320t,320s,310t,310s`: 3914 de 3914 iguales) y los hashes de los binarios de
programa son los mismos (la consola no recompila nada).

### Diferencia conocida (no resuelta)

GX con Z temprana (`[earlydepthstencil]`) hace la prueba y la escritura de profundidad antes del alpha test: un
fragmento que el alpha test descarta escribe profundidad. GLSL ES 3.00 no tiene `early_fragment_tests`, así que en 3.0
ese fragmento no la escribe. Solo cambia algo si el programa tiene Z temprana, un alpha test que puede descartar y
escritura de profundidad activa. En las claves de la vuelta entera (`vk_seed_keys.bin`): **28 de 981 programas** tienen
Z temprana con descarte y **2** de ellos escriben profundidad (lo cuenta `nebula_gles_shader_tests` en 3.0). Con 3.1/3.2
(la Mali de la RG35XX H) no hay diferencia. Arreglo posible, no hecho: en esos programas, en vez de `discard`, devolver
el color del framebuffer (`GL_EXT_shader_framebuffer_fetch`) cuando no hay blending.

### Comandos y resultados (PC, x86-64, Mesa 25.2.8 llvmpipe)

```sh
# 3.0 real de Mesa; y la ruta de 3.0 sobre un contexto 3.2
MESA_GLES_VERSION_OVERRIDE=3.0 EGL_PLATFORM=surfaceless \
  GALAXY_GLES_TEST_KEYS=platform/android/vk_seed_keys.bin ./nebula_gles_shader_tests
MESA_GLES_VERSION_OVERRIDE=3.0 SDL_VIDEODRIVER=offscreen ./nebula_gles_renderer_tests
GALAXY_GLES_FORCE_ES30=1 [GALAXY_GLES_THREAD=1] SDL_VIDEODRIVER=offscreen ./nebula_gles_renderer_tests
MESA_GLES_VERSION_OVERRIDE=3.0 SDL_VIDEODRIVER=offscreen ./nebula_texture_compact_tests
./nebula_glsl_aot shaders-gles.bin platform/android/vk_seed_keys.bin && ./nebula_glsl_aot --verificar ...
glslang <cada .vert/.frag volcado>   # validador de referencia de Khronos, GLSL ES 3.00 estricto
```

| Prueba | Resultado |
|---|---|
| Contexto | Con Mesa en 3.0, 3.2 y 3.1 fallan (`EGL_BAD_MATCH`) y el contexto sale 3.0 |
| `nebula_gles_shader_tests` en 3.0 con las 977 claves | 989 programas compilan y enlazan, 0 fallos; los bindings se aplican sin error de GL |
| Validador de glslang (GLSL ES 3.00) | 1978 shaders, 0 errores (rechaza `early_fragment_tests` y `bitfieldExtract` en 3.00: la prueba sirve) |
| `nebula_gles_renderer_tests` | 0 fallos en: 3.2; 3.2 con texture buffer; 3.2 con `BUFFER_STORAGE=0`; `FORCE_ES30`; Mesa 3.0; los tres últimos también con hilo de GL. Prueba nueva: un draw con la matriz de la paleta (mueve el triángulo, y una segunda paleta del mismo anillo lo devuelve) |
| Sin traductor (`NEBULA_GLES_TRANSLATOR=OFF`) con la biblioteca de 5 formas | Las mismas pruebas del renderer, 0 fallos en 3.2, Mesa 3.0, `FORCE_ES30` (con y sin hilo de GL) y texture buffer |
| `nebula_texture_compact_tests` | 0 fallos en 3.2 y en 3.0 |
| Biblioteca | 4894 de 4894 iguales a la traducción (`--verificar`) |
| El caso Z temprana + descarte + escritura de profundidad | En 3.0 la prueba lo salta y lo dice (`skip GX early Z ...`): es la diferencia de arriba |

Lo que las pruebas no cubren: un driver 3.0 de verdad (Mesa con el límite puesto es lo más cercano en la PC; acepta
cosas que un driver estricto podría rechazar, por eso el validador de glslang), y que Mesa conserva los bindings en
sus binarios de programa (no puede demostrar que hacía falta guardarlos; se guardan porque la especificación lo pide).

### Riesgos

- Rendimiento del camino 3.0: `glBufferSubData` por draw y la copia de la paleta (6.6 KB cada vez que cambia) cuestan
  CPU en el hilo de render, que ya es el cuello de botella en la Mali. Solo se usa sin 3.1 o con `FORCE_ES30`; la
  medición en la consola dirá cuánto.
- La textura de paleta mira 1 MiB como el texture buffer: con `mtx_palette_base` desde la base del anillo (escenas
  temporales, apagadas en GLES) más allá de 1 MiB fallaría igual que con texture buffer.
- La diferencia de Z temprana de arriba (2 programas de 981).

### Checklist de aceptación

- [x] Compila para x86-64 (con y sin traductor)
- [x] Compila para aarch64 (`armar.sh` completo, de las fuentes a `smg.zip`: 13 min en 2 núcleos; `nebula` 3.4 MB,
  `smg/shaders-gles.bin` 2.6 MiB con las cinco formas)
- [x] `nebula_gles_renderer_tests`, `nebula_gles_shader_tests` y `nebula_texture_compact_tests` pasan en un contexto 3.0
  y en uno 3.2
- [x] La biblioteca verifica sus formas 300
- [x] Con 3.1/3.2 el GLSL y los binarios no cambian
- [ ] En la consola: una vuelta de ULTRA con `GALAXY_GLES_FORCE_ES30=1` sin errores de GL, comparada con la de 3.2

**Definición de terminado:** todo lo anterior marcado. Falta la vuelta en la consola: la fase queda "hecha en la PC"
hasta medirla.
