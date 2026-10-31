# GX-ES: el backend GX de Nebula en OpenGL ES para la RG35XX H (auditoría y plan)

Auditoría del commit `f6676d56` (rama `rg35xxh-arm64`) contra el pedido de GX-ES. Se leyeron
[GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md), [ARCHITECTURE.md](ARCHITECTURE.md), [BUILDING.md](BUILDING.md) y
[RG35XXH.md](RG35XXH.md). Lo que se afirma aquí sale del código y de mediciones hechas en la consola; lo que no se ha
medido en la consola se dice.

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
| GLSL ES Shader Generator | HLSL → GLSL ES 3.20/3.10 (`shader_translate_gles.h`), hecho en la PC (`shader_library_gles.h`, `nebula_glsl_aot`) | La consola no traduce: 3914 shaders en `smg/shaders-gles.bin`. Verificado igual a la traducción (`--verificar`) |
| UberShader | `uber_vs.hlsl`, `uber_ps.hlsl` (6 variantes) | Dibuja mientras compila el especializado y lo que falte en la biblioteca |
| Texture Decoder y Cache | `texture_cache.cpp`, `texture_compact.cpp`, `texture_pack.cpp` | Todos los formatos GX que usa Galaxy; texturas compactas en GLES (R8, RG8, 16 bits) |
| EFB/XFB Manager | `efb_copy.cpp`, `renderer_gles.cpp` | EFB fuera de pantalla, copias a textura, copia a pantalla con filtros, peeks asíncronos |
| GLES Device | `gles/gles_context.cpp`, `platform/knulli/game/host_window_knulli.cpp` | SDL2 + EGL de Mali (fbdev); contextos EGL compartidos para compilar en otros hilos |
| GLES Renderer | `gles/renderer_gles.cpp`, `gles/gl_thread.cpp` | Anillos de vértices, índices y constantes reutilizados, frames en vuelo, hilo propio para las llamadas GL |
| Pipeline Cache | `gles/pipeline_cache_gles.cpp` | Compilación asíncrona en hilos con contexto propio, binarios del driver en disco, claves persistentes, uber de respaldo |
| GX Capture y Profiler | `.gxdump`, `GALAXY_GX_CAPTURE_TEXDIR`, `gles/ultra_gles.cpp`, `gles/nopal_depurar.cpp` | CPU y GPU (timer queries) por frame, presentación, FPS medio, 1 % bajo, p95/p99, tirones con causa, memoria; Depurador de Nopal Estudio en vivo |
| Calidad adaptativa | `GALAXY_GLES_RESOLUTION=auto`, `GALAXY_FRAMESKIP=auto` | Resolución dinámica y salto de frames con ritmo estable |

## Lo que no cumple el pedido

1. **OpenGL ES 3.0 como base.** Hoy hace falta OpenGL ES 3.1 (`gles_context.cpp` lo exige), `GL_EXT_buffer_storage`
   (anillos mapeados de forma persistente) y que el vertex shader lea storage buffers o texture buffers (la paleta de
   matrices). Usa funciones de 3.1 (`glVertexAttribFormat`, `glVertexAttribBinding`, `glBindVertexBuffer`,
   `glGetProgramResourceIndex`) y GLSL ES 3.10/3.20 con `layout(binding)`. En la RG35XX H no estorba: el driver de Mali
   da OpenGL ES 3.2 (`Mali-G31 ... OpenGL ES 3.2 v1.r20p0`, log de la consola); pero un driver solo 3.0 no arranca.
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
| P7 Optimización y PortMaster | En curso | Paquete PortMaster e instalador; series ULTRA | 30 FPS estables; base 3.0 |

## Siguiente fase propuesta: base OpenGL ES 3.0

Que el backend arranque con un driver solo OpenGL ES 3.0 y use lo de 3.1/3.2 solo si está, sin cambiar lo que se dibuja:

- Atributos de vértice con `glVertexAttribPointer` cuando no hay `glVertexAttribFormat`.
- Anillos de subida sin `GL_EXT_buffer_storage`: `glMapBufferRange` sin sincronizar por tramo de frame (o
  `glBufferSubData`), con las mismas vallas que hoy.
- Paleta de matrices en una textura 2D (`texelFetch`, OpenGL ES 3.0) cuando no hay storage ni texture buffers.
- GLSL ES 3.00 como cuarta forma de la biblioteca: sin `layout(binding)`, con `glUniformBlockBinding` y `glUniform1i`
  después de enlazar.
- Prueba: Mesa con `MESA_GLES_VERSION_OVERRIDE=3.0` da un contexto 3.0 real en la PC; los tests del renderer deben pasar
  ahí igual que en 3.2. En la consola, `GALAXY_GLES_FORZAR_30=1` lo prueba con el driver de Mali.

Checklist de aceptación: compila para aarch64 y x86-64; `nebula_gles_renderer_tests`, `nebula_gles_shader_tests` y
`nebula_texture_compact_tests` pasan en un contexto 3.0 y en uno 3.2; la biblioteca verifica sus formas 300; en la
consola, una vuelta de ULTRA con `GALAXY_GLES_FORZAR_30=1` sin errores de GL y con rendimiento comparado con la de 3.2.
