# GX-ES: análisis de compatibilidad

Commit `b73a2d86`. Las fuentes de los datos:

- **[código]**: lo dice el código citado.
- **[sonda]**: Nebula Probe 0.3 en la RG35XX H con KNULLI 42 (`platform/knulli/probe/results/rg35xxh-knulli42-probe-0.3.txt`).
- **[ULTRA]**: la medición base de ULTRA-GLES del 23-oct-2026 en la consola (`informe.txt`, 7 escenas, 321 s).
- **[claves]**: las 977 claves de pipeline de una vuelta entera al juego (`platform/android/vk_seed_keys.bin`: 892 programas, 741 pixel shaders y 232 vertex shaders distintos), contadas con un script sobre el formato `GXVK`.
- **[PC]**: pruebas en x86-64 con Mesa 25.2.8 (llvmpipe).

Lo que no tiene marca es una deducción y se dice así.

## El dispositivo, según la sonda

| | RG35XX H (Mali-G31 MP2, driver r20p0) |
|---|---|
| API | OpenGL ES 3.2, GLSL ES 3.20 [sonda]; sin Vulkan (`libvulkan.so.1` no existe) [sonda] |
| Límites | texturas 8192, 16 unidades, 1024 vectores uniformes por etapa, 15 varyings, 16 atributos, 4 draw buffers; UBO máx. 16384 B (log de la consola) |
| Extensiones que sí hay | `GL_ARM_shader_framebuffer_fetch`, `GL_EXT_buffer_storage`, `GL_EXT_geometry_shader`, `GL_EXT_disjoint_timer_query`, `GL_OES_get_program_binary` (1 formato), `GL_EXT_color_buffer_half_float`, `GL_OES_depth24`, `GL_OES_packed_depth_stencil` |
| Extensiones que no hay | `GL_EXT_blend_func_extended` (doble fuente), `GL_EXT_shader_framebuffer_fetch`, `GL_EXT_clip_control`, `GL_EXT_texture_compression_s3tc`/`dxt1`, `GL_KHR_parallel_shader_compile` |
| CPU | 4× Cortex-A53 a 1.5-1.7 GHz; memcpy 1.20 GB/s [sonda] |
| Memoria | 973 MiB en total, 744 MiB libres al arrancar [sonda]; el juego usa 596 MiB de media y 670 de pico, y deja 177 MiB libres como mínimo [ULTRA] |
| Costos medidos | ~7.3 µs de driver por draw; relleno de 170 Mpix/s con TEV especializado; compilar un programa cuesta 27 ms de mediana y cargar su binario 0.3 ms [sonda] |

## A. Partes que dependen solo de APIs de escritorio

Todas están fuera del camino de GX-ES; no hay que cambiarlas ni portarlas:

- **Direct3D 12 / Windows:** `renderer_d3d12.cpp`, `pipeline_cache.cpp` (FXC, `psos.bin`), `texture_backend_d3d12.cpp`, `placed_texture_allocator.cpp`, `host_window_win32.cpp`, `vblank_clock_win32.cpp`.
- **Solo D3D12:** el trazado de rayos (`ray_tracing*.cpp`, `rt_*.hlsl`), el posproceso (`post_process.cpp`, SMAA, FSR 1), `temporal_upscaler.cpp`, `streamline_bridge.cpp`, `ambient_occlusion.cpp`, `frame_interpolation.cpp` / `interp_gpu.cpp` (interpolación de frames).
- **Las pruebas `vertex_loader_tests.cpp`, `shader_gen_tests.cpp`, `xf_lighting_tests.cpp`, `efb_conversion_tests.cpp` y `sampler_key_tests.cpp`:** incluyen `Windows.h`/`d3d12.h`/`d3dcompiler.h` y no compilan en Linux [PC: intentado sobre este commit]. Son las que comprueban el cargador de vértices y el generador de shaders.

`RendererGles` declara vacías las entradas que esos sistemas usan (`rt_*`, `record_scene_ao`, `begin_replay_sequence`…), así
que `GxBackend` no cambia; el log lo dice (`post-process ... GLES`).

## B. Partes que funcionan sin cambios en ARM64

Todo el núcleo GX de GX_ES_COMPONENT_MAP.md está compilado para aarch64 en el port (`armar.sh`, clang + glibc 2.27) y
corre el juego en la consola [ULTRA]. Medido en la consola: el frente GX (FifoParser y claves de shader) procesa
1000 draws por frame en 1.19 ms, ~1.2 µs por draw [sonda]. Las partes específicas de x86 están protegidas:

- `__rdtsc` / `_mm_pause` en `gx_backend.cpp` tienen su alternativa (`qpc_now`, `yield`).
- Los "kernels SSSE3" de intercambio de bytes que nombra GRAPHICS_ARCHITECTURE.md son escalares portables en
  `fifo_parser.h` (no incluyen intrínsecos).

Las pruebas que sí corren en Linux pasaron sobre este commit en x86-64, enlazadas contra `libgalaxy_gx_gles.a`: [PC]

- `fifo_parser_tests`: PASS, con 16 777 216 transiciones del oráculo de blend.
- `vs_constants_tests`: PASS, 8192 imágenes de constantes.
- `owned_fifo_packet_tests`: PASS.

No se pudieron correr en aarch64 aquí: no hay emulador.

## C. Partes que necesitan cambios para GLES 3.0

Ya hechos en `b73a2d86` y probados solo en la PC; falta la consola. Detalle en GX-ES.md, sección "Fase: base OpenGL ES 3.0".

| Lo de 3.1/3.2 | En 3.0 |
|---|---|
| `GL_EXT_buffer_storage` | Copias en la CPU + `glBufferSubData` antes de cada draw |
| `glBindVertexBuffer` / `glVertexAttribFormat` | `glVertexAttribPointer` |
| Paleta de matrices en SSBO o texture buffer | Textura 2D RGBA32UI |
| GLSL ES 3.10/3.20 con `layout(binding)` | GLSL ES 3.00 con bindings tras enlazar |
| `layout(early_fragment_tests)` | No existe: difiere en 2 de 981 programas [claves, contado en `nebula_gles_shader_tests`] |

Pendiente en 3.0: nada del núcleo GX. La consola usa 3.2 y no pasa por ahí.

## D. Funciones que necesitan alternativa por el driver de la Mali-G31

| Función GX | Qué hace hoy el backend GLES [código] | Uso en Galaxy [claves] | Alternativa con lo que tiene la Mali |
|---|---|---|---|
| **Blending con alfa de doble fuente** (`GXSetDstAlpha` con blend que lee la alfa de la fuente: la alfa del TEV mezcla el color, la constante va al EFB) | `pipeline_cache_gles.cpp` traduce el factor `SRC_ALPHA` a `GL_SRC_ALPHA`, que lee la salida 0. El pixel shader pone en la salida 0 la alfa constante (`shader_gen.cpp`, `flags & 0x04`), y la alfa del TEV va a la salida 1, que GLES descarta: **esos draws mezclan el color con la alfa constante en vez de la del TEV** (deducido del código, no visto en pantalla) | **241 de 977 pipelines** (24.7 %), todos con blend y factor `GX_BL_SRCALPHA`/`GX_BL_INVSRCALPHA` | Sin `GL_EXT_blend_func_extended`: hacer la mezcla en el shader con `GL_ARM_shader_framebuffer_fetch` (`gl_LastFragColorARM`), color mezclado con la alfa del TEV y alfa constante, blending fijo apagado; o dos pasadas (color, luego solo alfa) |
| Logic op (`GXSetBlendMode` con `GX_BM_LOGIC`) | Se ignora (`RenderStateKey` lo codifica en el bit 8; el pipeline GLES no lo lee) | 0 de 977 | No hace falta para Galaxy; con framebuffer fetch, si apareciera |
| LOD bias de los samplers | Se ignora: "OpenGL ES samplers have no LOD bias" (`renderer_gles.cpp`) | No está en las claves (es estado del sampler) | OpenGL ES no tiene `GL_TEXTURE_LOD_BIAS`: el sesgo en el shader (`texture(s, uv, bias)` en el fragment shader) |
| Rango de profundidad y recorte en Z (D3D: 0..1) | SPIRV-Cross `fixup_clipspace` (`z = 2z - w`) y `glDepthRangef` | Todas | Sin `GL_EXT_clip_control` es la única vía; ver E |
| Líneas y puntos anchos | Ancho 1; tamaño de punto con `with_point_size` | 3 pipelines de líneas (topología 2), 0 de puntos | `GL_EXT_geometry_shader` existe (como el geometry shader de D3D12) |
| CMPR (S3TC) | Se decodifica en la CPU a RGBA8 (o RGB565/RGB5A1/RGBA4 con `GALAXY_TEXTURE_COMPACT=16bits`) | — | No hay S3TC en el driver: se queda así |
| Compilación en paralelo | Contextos EGL compartidos en hilos, binarios del driver, biblioteca GLSL hecha en la PC | — | `GL_KHR_parallel_shader_compile` no existe; lo actual ya lo sustituye |
| Z temprana con alpha test que descarta | `layout(early_fragment_tests)` en GLSL ES 3.10/3.20 | 28 programas con Z temprana y descarte, 2 de ellos con escritura de profundidad | Con 3.2 no hay diferencia; en 3.0, framebuffer fetch (GX-ES.md) |


**Corrección del paso 2** ([GX_ES_BLEND_ANALYSIS.md](GX_ES_BLEND_ANALYSIS.md)): de los 241, solo 35 (`GXSetDstAlpha`) mezclan con la alfa constante; los otros 206 mezclan con la alfa del TEV recortada a 6 bits (error de 1-3 niveles). Resuelto con framebuffer fetch; falta la consola.

## E. Operaciones con riesgo de precisión

| Operación | Cómo está [código] | Riesgo |
|---|---|---|
| Aritmética del TEV | Floats con cada etapa redondeada a la malla de 8 bits antes de comparaciones y alpha test, y registros como `int4` (`shader_gen.cpp`) | Bajo: la Mali da highp en fragment (23 bits de mantisa) [sonda]; los enteros del TEV caben |
| Profundidad | EFB `GL_DEPTH_COMPONENT32F` con Z invertida; los peeks devuelven el z24 escrito (prueba `nebula_gles_renderer_tests`) [PC] | Medio: `z = 2z - w` (`fixup_clipspace`) cambia la proyección respecto de D3D12, y puede cambiar qué superficie gana en un empate de Z (z-fighting en calcomanías). No medido |
| Formato del EFB | Siempre RGBA8; RGBA6 (973 de 977 pipelines usan `pixfmt` 1, RGBA6_Z24) y RGB8 se cuantizan en el shader, con dither | Bajo: emulado igual que en D3D12 (mismo HLSL) |
| Alfa de doble fuente | Ver D | **Alto** en 35 pipelines (alfa constante en vez de la del TEV: hasta 199 niveles medido); bajo en 206 (alfa de 6 bits: hasta 3 niveles). Paso 2: GX_ES_BLEND_ANALYSIS.md |
| LOD bias | Se ignora | Medio: puede cambiar qué mip se elige (texturas más borrosas o más nítidas que en el Wii). No medido |
| Texturas compactas | `exacto` no cambia ningún texel (pruebas `nebula_texture_compact_tests`); `16bits` pierde los bits bajos de CMPR y RGB5A3 | Bajo con `exacto` (por defecto) |
| Copias EFB a textura | Siempre RGBA8; el formato de destino (R4, RA8, Z24…) se aplica en el shader de conversión | Bajo: mismo shader que D3D12 |
| Resolución dividida | `GALAXY_GLES_RESOLUTION=auto` (50-100 %): copias y peeks a la resolución del Wii desde un EFB menor | Medio por diseño: menos detalle en copias pequeñas; elegido por el usuario |
| Rango de los cálculos de luz | `kVsKeyFiniteLightAttn`: operandos finitos en la atenuación | Bajo |

## F. Componentes que pueden subir el consumo de RAM

| Componente | Tamaño por defecto [código] | Observación |
|---|---|---|
| Anillos de subida | vértices 28 MiB, índices 6 MiB (3 MiB en el lanzador), constantes 6 MiB, matrices 8 MiB (3 MiB en el lanzador), **por frame en vuelo**, y en GLES hay 4 frames en vuelo (`NEBULA_GLES_FRAMES_IN_FLIGHT`). Con el lanzador: 4 × (28 + 3 + 6 + 3) = **160 MiB** | En un menú la consola usó como máximo v=4043 KiB, i=116, c=3880 y m=6 por frame (log de la consola). Falta el pico de una vuelta entera para dimensionarlos |
| Caché de display lists | 192 MiB (`GALAXY_GX_DISPLAY_LIST_CACHE_MB` en el lanzador) | Tope, no reserva |
| Texturas | pool de 128 MiB; packs 256 MiB; destinos de copia EFB 64 MiB | Topes; RGBA8 por defecto: 4 bytes por texel (las compactas ahorran) |
| Vértice canónico | 144 bytes por vértice (`GxVertexOut`), aunque el draw use solo la posición | ~6000 vértices por frame en el log de la consola ≈ 0.85 MiB por frame copiados; el ancho de banda de la memoria es 1.2 GB/s [sonda] |
| Copias en la CPU (solo GLES 3.0 sin `EXT_buffer_storage`) | El tamaño de cada anillo, una vez más | No se usa en la consola |
| Binarios de programa y biblioteca GLSL | 5.8 KiB por binario [sonda]; la biblioteca es de 2.6 MiB en disco, y en memoria se cargan 4 bloques de ~256 KiB (LRU) | Bajo |

## G. Funciones que sincronizan CPU y GPU

| Función | Cómo | Medido |
|---|---|---|
| Peeks del EFB (`GXPeekARGB`/`GXPeekZ`: destellos del sol, puntero) | `GALAXY_PEEK_MODE=sync` vacía el FIFO y espera a la GPU; `async` (perfil bajo, por defecto en el lanzador) contesta con el último campo leído por PBO | Con sync, 1506 de 4842 tirones se debían a lecturas del EFB [ULTRA, 23-oct] |
| Profundidad del puntero | `capture_pointer_depth`: un campo de profundidad por PBO con su valla | — |
| Frames en vuelo | `glFenceSync` por frame; el hilo de render espera la del frame N-4 antes de reutilizar los anillos (`Context::wait`) | 7.31 ms por frame esperando a la GPU [ULTRA] |
| Compilación de programas | Con `GALAXY_GLES_PENDING=wait`, el draw espera al programa; `uber` (por defecto) dibuja con el uber mientras tanto | 166 programas compilados mientras un draw esperaba: 62 s en total [ULTRA, con `wait`] |
| Copias EFB | En orden en la GPU (sin vuelta a la CPU); el destino es una textura | — |
| Tokens PE / draw done | `GxBackend` los liga a la valla del frame que los contiene | — |
| Hilo de GL | Las llamadas que devuelven algo esperan al hilo (`sync_call` en `gl_thread.cpp`) | El hilo GL es necesario: sin él, el recorrido tarda 241 s en vez de 201 s (series del 29-oct, `nopal/docs/RG35XXH.md`) |

## H. Partes que pueden generar demasiadas llamadas o cambios de estado

- **Draws:** 180 por frame y 31 cambios de programa de media [ULTRA]. El driver cuesta ~7.3 µs por draw [sonda], unos 1.3 ms por frame. La CPU del hilo de render es 35.5 ms por frame, de los cuales 7.4 ms están en los draws [ULTRA]: el costo está más en preparar los draws (`flush_draw_state`, carga de vértices) que en el driver.
- **Uniform blocks:** se atan con `glBindBufferRange` por draw si cambia la dirección; las constantes de VS (1536 B) y PS (768 B) se suben por draw cuando cambia el estado.
- **Texturas y samplers:** se vuelven a atar solo si cambian (`bound_` en `renderer_gles.cpp`).
- **Unión de draws:** `GxBackend` une draws consecutivos compatibles (`batch-break-*` en `[gx-frame-timing]`), y las display lists en caché se reproducen por tandas (`on_cached_prepared_draw_run`).
- **GLES 3.0:** `glVertexAttribPointer` por cada dirección nueva de vértices y `glBufferSubData` por draw; solo en ese camino.
- **Subidas de paleta:** solo en 3.0 (`palette uploads` en el log).
