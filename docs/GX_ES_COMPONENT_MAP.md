# GX-ES: mapa de componentes

Commit auditado: `b73a2d86` (rama `rg35xxh-arm64`). Rutas relativas a la raíz del repositorio; `gx/` es
`runtime/src/gx/` (fuentes) o `runtime/include/galaxy/gx/` (cabeceras). Qué es cada acción:

- **Conservar**: código compartido por D3D12, Vulkan y GLES, sin nada de un API gráfico; GX-ES lo usa tal cual.
- **Adaptar**: compartido, pero GX-ES necesita un cambio acotado (una opción, una forma de shader).
- **Backend GLES (existe)**: ya hay una implementación GLES en `gx/gles/`; la acción es completarla, no escribirla.

El backend que pide GX-ES ya existe: `RendererGles`, `PipelineCacheGles`, el `TextureBackend` de GLES y
`gles::Context` corren Super Mario Galaxy en la RG35XX H con KNULLI (mediciones ULTRA-GLES desde el 23-oct-2026).
Lo que dice "Reescribir backend" en el pedido corresponde aquí a esos archivos.

## Núcleo GX (independiente del API)

| Componente | Archivo original | Función | Dependencias | Acción GX-ES |
|---|---|---|---|---|
| FIFO | `gx/fifo_parser.cpp`, `fifo_parser.h` (`FifoParser::run`, `FifoSink`, `FifoCursor`, `GxFatalError`) | Lee el flujo GX big-endian: NOP, LOAD_CP/XF/BP, LOAD_INDX A-D, CALL_DL (anidado hasta 4), INVAL_VTX_CACHE, DRAW 0x80-0xBF; display lists en caché (`CachedDrawPacketRun`); un opcode, registro o puntero desconocido lanza `GxFatalError` y escribe un `.gxdump` | `gx_state.h`, `gx_bitfields.h`, `GuestMemoryV1` (`galaxy/native_api.h`) | Conservar |
| FIFO propio de un frame | `gx/owned_fifo_packet.h` (`OwnedFifoPacket`), `owned_fifo_event_replay.h` | Registro sellado de un análisis del FIFO con sus dependencias de memoria y efectos PE/XFB en orden (adaptado de Aurora) | `dependency_event_capture.h` | Conservar |
| Entrada del juego nativo | `native/sdk/gx/gx_fifo.c` (Petari, GX SDK nativo); `gx/gx_shim.cpp` (`render_frame`) | El SDK GX del juego escribe el formato del hardware en un anillo de doble mapeo; el host lo analiza en su lugar | `GxBackend` | Conservar |
| CP | `gx/gx_state.cpp` (`GxState::load_cp`), `gx_bitfields.h` | VCD, VAT, bases y strides de arrays; bit de cambio | — | Conservar |
| XF | `gx/gx_state.cpp` (`GxState::load_xf`, `load_xf_indexed`) | Memoria de matrices, texgen, canales de color, luces, proyección, viewport; `kDirtyXfShader`, `kDirtyXfMatrices` | — | Conservar |
| BP | `gx/gx_state.cpp` (`GxState::load_bp`, `z_mode`, `scissor`, `efb_copy`) | TEV, texturas, TLUT, blend/zmode/cull, copias EFB (`EfbCopyParams`), tokens PE; `kDirtyTev`, `kDirtyRenderState`, `kDirtyTex` | — | Conservar |
| Vertex Loader | `gx/vertex_loader.cpp`, `vertex_loader.h` (`VertexLoader::load`, `GxVertexOut`), `vertex_dequant.h` | Cualquier VCD/VAT a un vértice canónico de 144 bytes (`GxVertexOut`) y listas de triángulos indexadas, en los anillos de subida | `UploadRing` (`upload_ring.h`), `gpu_types.h` | Conservar (la escritura en el anillo ya anota `gpu::note_upload_write` para GLES sin `EXT_buffer_storage`) |
| Vértices residentes | `gx/resident_vertex_arena.h`, `GxBackend::make_decoded_entry_resident` | Vértices decodificados de display lists que se reutilizan entre frames | `UploadMemory` | Conservar |
| Claves | `gx/shader_keys.cpp`, `shader_keys.h` (`PixelShaderKey`, `VertexShaderKey`, `RenderStateKey`, `PsoKey`) | Claves canónicas: lo que no cambia el código se pone a cero antes del hash; el estado fijo (blend, z, cull) va aparte del shader | `gx_state.h`; Aurora (canonicalización de blend) | Conservar |
| TEV | `gx/shader_gen.cpp` (`ShaderGenerator::generate_ps`), `uber_ps.hlsl`, `uber_shaders.cpp` | HLSL por clave: hasta 16 etapas, 4 indirectas, swaps, konst, comparaciones, alpha test en enteros de 8 bits, niebla, Z-textura, formato del EFB (RGBA6 con dither) | `shader_keys.h`, `uber_constants.h` | Adaptar (ver GX_ES_COMPATIBILITY.md: doble fuente sin `GL_EXT_blend_func_extended`) |
| Vertex shader | `gx/shader_gen.cpp` (`ShaderGenerator::generate_vs`), `uber_vs.hlsl` | Matriz de posición en línea o de la paleta, texgen (regular, post-matriz), iluminación por vértice de 1-2 canales | `uber_constants.h` (`GxVsConstants`, 1536 bytes) | Conservar |
| Shader → GLSL ES | `gx/gles/shader_translate_gles.cpp` (`gles::translate`), `gx/vk/shader_compile_vk.cpp` (glslang), SPIRV-Cross | HLSL → SPIR-V → GLSL ES 3.20/3.10/3.00, en la PC | glslang, SPIRV-Tools, SPIRV-Cross (solo en la PC) | Backend GLES (existe) |
| Biblioteca GLSL | `gx/gles/shader_library_gles.cpp`, `platform/knulli/game/glsl_aot.cpp` | `smg/shaders-gles.bin`: 4894 shaders en 5 formas; la consola no traduce | zlib (`third_party/archive/zlib`) | Backend GLES (existe) |
| Texturas: decodificación | `gx/texture_cache.cpp` (`TextureCache::get`, `load_tlut`, `register_efb_copy`, `invalidate_all`) | I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, C4, C8, C14X2, CMPR a RGBA8; TLUT; alias de copias EFB; invalidación explícita (`GXInvalidateTexAll`) | `texture_backend.h`; Aurora (caché por contenido, opcional) | Conservar |
| Texturas compactas | `gx/texture_compact.cpp` (`CompactFormat`) | R8, RG8, RGB565, RGB5A1, RGBA4 cuando caben exactos | — | Conservar |
| Packs de texturas | `gx/texture_pack.cpp`, `texture_pack_codecs.cpp` | Reemplazos estilo Dolphin (PNG, DDS BC1-7 decodificados con bcdec) | `third_party/bcdec`, `third_party/stb` | Conservar |
| Copias EFB (geometría) | `gx/efb_copy.cpp`, `efb_copy.h` (`EfbCopyManager`, `XfbTexture`, `XfbPresentSelection`, `compute_scaled_efb_copy_extent`) | Registro de XFB por dirección, rectángulos escalados, selección del XFB a presentar | `gpu_types.h` | Conservar |
| Conversión de copias | `gx/renderer_common.cpp` (`efb_conversion_shader_source`, `EfbConversionShaderConstants`) | El shader que convierte EFB → formato de copia (color y Z), filtro vertical de 3 tomas, gamma, intensidad (YUV) | — | Conservar |
| Orquestador | `gx/gx_backend.cpp`, `gx_backend.h` (`GxBackend::render_frame`, `on_draw`, `flush_draw_state`, `on_efb_copy`) | Hilo de simulación → cola → hilo de render; aplica el estado sucio a claves y constantes, pide pipelines, texturas y vértices, dibuja, copia, presenta, tokens PE | todo lo anterior; el renderer que elige `gx_backend.h` | Conservar (10 bloques `#if NEBULA_GPU_GLES` ya existentes) |
| Anillos de subida | `gx/upload_ring.cpp` (`UploadRing::allocate`, `upload_immutable`) | Memoria por frame en vuelo para vértices, índices, constantes y matrices | `gpu::create_upload_memory` | Conservar |
| Tipos de GPU | `gx/gpu_types.h` (`gpu::Address`, `UploadMemory`, `note_upload_write`) | Las formas comunes de los tres backends | — | Conservar |

## Backend gráfico

| Componente | Archivo original | Función | Dependencias | Acción GX-ES |
|---|---|---|---|---|
| Elección del backend | `gx/gx_backend.h` (`NEBULA_GPU_VULKAN` / `NEBULA_GPU_GLES` / D3D12) | Incluye el renderer y la caché de pipelines del backend compilado | macro de compilación (`platform/knulli/game/CMakeLists.txt` define `NEBULA_GPU_GLES`) | Conservar |
| Dispositivo GLES | `gx/gles/gles_context.cpp` (`gles::Context`, `Capabilities`, `create_buffer`, `create_texture`, `flush_writes`, `apply_bindings`) | Carga de funciones (`gl_api.h`), capacidades, buffers con direcciones GX, vallas por frame, borrado diferido | `third_party/khronos` (cabeceras GLES/EGL) | Backend GLES (existe) |
| Ventana y contexto | `platform/knulli/game/host_window_knulli.cpp` (`gles_create_context`, `gles_create_worker_context`) | SDL2 + EGL de Mali (fbdev); contexto 3.2 → 3.1 → 3.0; contextos compartidos para compilar | SDL2 del firmware, EGL | Backend GLES (existe) |
| EFB | `gx/gles/renderer_gles.cpp` (`RendererGles::create_efb`, `bind_efb`, `clear_efb`) | Textura RGBA8 + `GL_DEPTH_COMPONENT32F` en un framebuffer, 640x528 por la escala, o dividido (`GALAXY_GLES_RESOLUTION`) | `gles::Context` | Backend GLES (existe) |
| XFB y presentación | `gx/gles/renderer_gles.cpp` (`copy_efb_to_xfb`, `present`, `create_utility_programs`) | Copia a pantalla con la conversión EFB (y filtros de nitidez/AA), presentación con `glBlitFramebuffer` invertido | `efb_copy.h`, `renderer_common.cpp` | Backend GLES (existe) |
| Copias a textura | `gx/gles/renderer_gles.cpp` (`copy_efb_to_texture`, `record_conversion_blit`) | EFB → textura RGBA8 en la dirección de destino, con presupuesto (`GALAXY_EFB_COPY_DEST_BUDGET_MB`) | `TextureCache::register_efb_copy` | Backend GLES (existe) |
| Peeks | `gx/gles/renderer_gles.cpp` (`peek_efb`, campos asíncronos `submit_async_peek_field`, `capture_pointer_depth`) | `GXPeekARGB`/`GXPeekZ` y la profundidad del puntero, síncrono o con campos leídos por PBO | pixel-pack buffers, vallas | Backend GLES (existe) |
| Pipeline | `gx/gles/pipeline_cache_gles.cpp` (`PipelineCacheGles::get`, `uber_pipeline`, `build_program`, `link`) | Programa + estado fijo por `PsoKey`; workers con contexto propio; binarios del driver; uber de respaldo | `shader_library_gles.h`, `shader_sources_gles.h` | Backend GLES (existe) |
| Subida de texturas | `gx/gles/texture_backend_gles.cpp` (`TextureBackend`) | `glTexStorage2D` + `glTexSubImage2D` desde los bytes decodificados, formatos compactos | `gles::Context` | Backend GLES (existe) |
| Hilo de GL | `gx/gles/gl_thread.cpp` (`glthread::install`, `start`) | Graba las llamadas GL y las ejecuta en otro núcleo (`GALAXY_GLES_THREAD=1`, por defecto) | — | Backend GLES (existe) |
| Medición | `gx/gles/ultra_gles.cpp`, `gx/gles/nopal_depurar.cpp` | ULTRA-GLES (frames.csv, informe por escena) y el canal del Depurador de Nopal | — | Backend GLES (existe) |
| D3D12 | `gx/renderer_d3d12.cpp`, `pipeline_cache.cpp`, `texture_backend_d3d12.cpp`, `post_process.cpp`, `ray_tracing*.cpp`, `temporal_upscaler.cpp`, `streamline_bridge.cpp`, `host_window_win32.cpp` | Backend de Windows, trazado de rayos, posproceso, FSR | Direct3D 12, DXGI, FXC/DXC, Windows | No se usa en GX-ES (no se toca) |
| Vulkan | `gx/vk/*.cpp` | Backend de Android | Vulkan, VMA, volk | No se usa: en la RG35XX H no existe `libvulkan.so.1` (Nebula Probe 0.3) |
