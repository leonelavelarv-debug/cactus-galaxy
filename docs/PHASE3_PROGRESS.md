# Fase 3: avance

Registro por sesión del trabajo de rendimiento y estabilidad del renderizador GLES en la RG35XX H (KNULLI). Las
mediciones de la consola las hace el usuario con los paquetes de medición; lo que no se midió allí se dice como tal.

## Sesión 2026-11-01 (Wii Hybrid Input: fases 1 a 3)

### Trabajo realizado
- Fase 1 (`docs/WII_HYBRID_FASE1.md`): análisis del sistema de entrada. Lo pedido ya existe en gran parte (tablas de botones,
  ajustes en `settings.ini`, puntero, sacudida e inclinación sintéticas); faltan perfiles por juego, interfaz y mediciones.
  Corregí en el mismo informe un error mío: el stick derecho ya es solo puntero en la consola.
- Fase 2 (`docs/WII_HYBRID_FASE2.md`): arquitectura. Decisiones del usuario: el mapeo de su documento tal cual y perfiles en `.ini`.
- Fase 3: el perfil WII HYBRID – RG35XX H. `platform/knulli/game/wii_profile.h` (cargador), `profiled_controls()` en el host de la
  consola, `smg/config/input/wii/{default,mario_galaxy}.ini`, `GALAXY_INPUT_PROFILE` (`mario_galaxy` por defecto, `off` = como antes).
  Mapeo: B→Wii A, A y R2→Wii B, R1→sacudida (Nunchuk en Galaxy), L1→C, L2→Z, X/Y→1/2, stick derecho→puntero (clic: recentrar).

### Archivos modificados
`platform/knulli/game/{native_game_host_knulli.cpp,wii_profile.h,wii_profile_tests.cpp,CMakeLists.txt}`,
`platform/knulli/smg/Super Mario Galaxy.sh`, `platform/knulli/smg/smg/config/input/wii/*.ini`, `README.md` y los dos documentos nuevos.
`runtime/` (y con él Windows) no se tocó.

### Pruebas ejecutadas
- `nebula_wii_profile_tests` (PC, con el `input_bindings.cpp` real): ambos perfiles, 30 líneas entendidas y 0 ignoradas, cada
  botón/gatillo/stick llega a su destino, y mover + apuntar + saltar + disparar + agacharse + girar a la vez no pierde ninguno. 36 comprobaciones, todas pasan.
- El cargador de archivos en la PC: carpeta ausente, `off`, nombre con `..` y archivo inexistente (el host sigue con los controles de antes).
- Compila para aarch64 (`nebula_knulli`).

### Resultados
- **No validado en la consola.** Falta ver que cada botón hace lo esperado jugando y que `log.txt` muestra `[input-profile]`.
- La interfaz de configuración (fase 7), la medición de latencia y CPU (fase 8) y el paquete probado en la consola (fase 9) siguen pendientes.

### Errores pendientes
- Mi prueba esperaba al principio `pressed == 0` en los sticks; el runtime marca el propio destino del stick como pulsado. Era un error de la prueba, no del código; corregido.
- R1 ya no recentra el puntero (pasó al clic del stick derecho). Si no te gusta, es una línea del `.ini`.

### Próximo paso
- Que pruebes el paquete en la consola y mandes `smg/log.txt`. Después, fases 4 y 5 (ajustes del pulso, centrado y suavizado del puntero) y la 7 (pantalla de configuración).

## Sesión 2026-11-01 (controles: sacudida del Nunchuk)

### Trabajo realizado
- A petición del usuario: el giro con el Nunchuk más fácil. La sacudida del Nunchuk ya existía en el host (`WiiTarget::NunchukShake`)
  pero no tenía ningún botón en la consola. Ahora, con `GALAXY_SHAKE=nunchuk` (por defecto), X y R2 sacuden el Nunchuk e Y el Wiimote;
  `remote` deja todo como antes y `both` manda X y R2 a los dos.
- `GALAXY_SHAKE_REPORTS` (4-60, 12 por defecto) alarga la sacudida del Nunchuk; 12 es lo de siempre, no cambia nada sin tocarlo.
- Cada sacudida deja `[input-shake] source=nunchuk|remote` en `log.txt`.

### Archivos modificados
- `platform/knulli/game/native_game_host_knulli.cpp` (solo el host de la consola; las tablas del runtime y Windows no se tocan),
  `platform/knulli/smg/Super Mario Galaxy.sh` (documentación de las variables), `README.md` (controles y ajustes).

### Pruebas ejecutadas
- Compila para aarch64 (`nebula_knulli`).
- La tabla de la consola, extraída y compilada en la PC con el `input_bindings.cpp` real: `nunchuk` pasa X y R2 de Shake a NunchukShake
  (Switch Pro y Xbox), `remote` no cambia nada y `both` los copia.

### Resultados
- **No validado en la consola.** No sé si el juego reconoce mejor el giro con la sacudida del Nunchuk; la hipótesis de que a veces falle
  por el muestreo (reportes a 100 Hz leídos a 60 Hz por el juego) no está medida.

### Errores pendientes
- Comprobar en la consola: que X y R2 giran, y contar en `log.txt` cuántos `[input-shake]` hay frente a los giros que hizo Mario.
- Si falla a veces: probar `GALAXY_SHAKE_REPORTS=20` y `30`, y comparar.

### Próximo paso
- El usuario prueba el paquete nuevo y manda `smg/log.txt`; con eso se decide si hace falta cambiar la forma del pulso (un cambio por experimento).

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
