# Fase 3: avance

Registro por sesión del trabajo de rendimiento y estabilidad del renderizador GLES en la RG35XX H (KNULLI). Las
mediciones de la consola las hace el usuario con los paquetes de medición; lo que no se midió allí se dice como tal.

## Sesión 2026-10-31 / 2026-11-01

### Trabajo realizado

- Mezcla de doble fuente de GX en el shader con `GL_ARM_shader_framebuffer_fetch` (paso 2 de GX-ES), validada en la consola.
- Biblioteca GLSL rehecha con las claves anotadas por la consola (`platform/knulli/smg/claves-consola.bin`).
- Ruta de OpenGL ES 3.0: buffers con copia en la CPU (calloc) y escrituras sin sincronizar; ya no muere por memoria en la
  prueba de mezcla.
- Cierre en CosmosGardenGalaxy: un frame que llena un anillo sigue en otro slot; un lote de draws no se junta si el anillo
  va a saltar de slot.
- Memoria que crecía por galaxia: límites de cachés, bloques de subida de texturas devueltos en `begin_frame`, y anillo de
  índices con copia en la CPU (el driver de la Mali crecía una tabla por cada rango de un buffer mapeado persistente).
- Herramientas: censo de memoria (`GALAXY_GX_MEMORY_LOG`), `NEBULA_MEMTRACE`, paquete de medición (`NEBULA_MEDICION=1`).
- Nopal: `nopal nebula compilar --medicion` y `docs/NEBULA.md` al día (commit 400f544 de Nopal).

### Archivos modificados

- `runtime/src/gx/gles/`: `gles_context.{cpp,h}`, `gl_thread.{cpp,h}`, `gl_api.h`, `renderer_gles.{cpp,h}`
- `runtime/src/gx/`: `upload_ring.{cpp,h}`, `gx_backend.{cpp,h}`, `texture_cache.{cpp,h}`, `fifo_parser.h`
- `platform/knulli/game/`: `gles_blend_tests.cpp`, `gles_renderer_tests.cpp`, `memtrace_knulli.cpp`, `CMakeLists.txt`
- `platform/knulli/prueba-mezcla/`, `platform/knulli/smg/` (`armar.sh`, `Super Mario Galaxy.sh`, `serie.cfg`, `claves-consola.bin`)
- Documentos: `GX_ES_STEP2_RESULTS.md`, `GX_ES_VISUAL_REGRESSION.md`, `GX_ES_ES30_MEMORY.md`, `GX_ES_MEMORY_GROWTH.md`

### Pruebas ejecutadas

- PC (Mesa): pruebas de renderizador (desbordamiento de anillo, `fits`, triángulo indexado) y de mezcla.
- Consola (KNULLI): prueba de mezcla contra la CPU; ruta ES 3.0 de la prueba; recorrido completo de las 44 galaxias.

### Resultados

- Mezcla en la Mali: 0 píxeles con diferencia mayor a 1 contra la CPU (máximo 1); draws en uber: 0 %.
- Recorrido de 44 galaxias sin cierres (paquete nebula md5 e83d6dff83be): malloc máximo 379 → 233 MiB; memoria libre
  mínima 36 → 138 MiB.
- Cachés: display lists 24 MiB, vértices decodificados 24 MiB, texturas recicladas 8 MiB. Con 16 MiB en listas y vértices
  la CPU empeoró (57 ms en AstroGalaxy), por eso volvieron a 24.
- Rendimiento visto en el recorrido: ~34 FPS mostrados y ~13 dibujados, con salto de imágenes automático. No es una
  mejora de esta sesión: es el estado de partida para optimizar.
- Hipótesis descartadas con medición: `glFlush` cada 1000 draws, `glDrawRangeElements`, base de vértices.

### Errores pendientes

- Ruta ES 3.0 jugando entero: no verificada en la consola tras los arreglos (antes iba lenta y moría más tarde).
- muOS: sin validar. R36S: solo estimación, sin medición.
- Banderas experimentales apagadas por defecto (`GALAXY_GLES_INDEX_RECYCLE`, `GALAXY_GLES_VERTEX_BASE`,
  `GALAXY_GLES_FLUSH_DRAWS`): decidir si se limpian.
- 121 commits sin subir al remoto: el entorno de la sesión no puede hacer push (se sube con el bundle desde Termux).

### Próximo paso

Medir dónde se va el tiempo por frame (CPU del hilo de render, CPU del hilo GL, espera de GPU y espera de presentación,
por separado) con la serie de ULTRA sobre el paquete actual, y con eso elegir un único cambio acotado. No se tocan
cachés sin medición ni blending/TEV/iluminación sin pruebas de equivalencia.
