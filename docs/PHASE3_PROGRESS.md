# Fase 3: avance

Registro por sesión del trabajo de rendimiento y estabilidad del renderizador GLES en la RG35XX H (KNULLI). Las
mediciones de la consola las hace el usuario con los paquetes de medición; lo que no se midió allí se dice como tal.

## Sesión 2026-11-01 (controles originales + solo mantener para sacudir)

### Trabajo realizado
- A petición del usuario: vuelta a los controles originales (los de `e5e4c008`: A salta, B salta/atrás, X/Y/R2 giran, L1 trozos de estrella,
  L2 Z, R1 recentra el puntero, clic del stick izquierdo C). Se quitaron el perfil WII HYBRID (`wii_profile.h`, los `.ini`, `GALAXY_INPUT_PROFILE`),
  `GALAXY_SHAKE` (X/R2 al Nunchuk) y `GALAXY_SHAKE_REPORTS`.
- Se conservó solo la función de mantener: `wii_shake.h` en el host; mantener X, Y o R2 sigue sacudiendo (enredaderas y tallos), tocar = el pulso de siempre.
  `GALAXY_SHAKE_HOLD=0` la apaga.

### Archivos modificados
Respecto a `e5e4c008`: `native_game_host_knulli.cpp` (la sacudida con `ShakeButton`), `wii_shake.h`, `wii_shake_tests.cpp`, `CMakeLists.txt`,
el lanzador (documenta `GALAXY_SHAKE_HOLD`) y `README.md` (una fila). Nada más del código cambió. Los documentos WII_HYBRID quedan marcados como retirados.

### Pruebas ejecutadas
- `nebula_wii_shake_tests`: pasa (tocar = pulso del runtime muestra por muestra; mantener 3 s = sacudida en 179 de 179 frames; antes 13).
- Compila para aarch64 sin avisos.

### Resultados
- **No validado en la consola.** Falta subir la enredadera con X, Y o R2 apretado.

### Próximo paso
- Probar la enredadera del video manteniendo un botón de giro.

## Sesión 2026-11-01 (enredaderas: mantener R1 para subir)

### Trabajo realizado
- Investigación en el código del juego (Petari): la enredadera (`Ride/Creeper.cpp`) y el tallo (`Ride/Plant.cpp`) suben a Mario mientras
  `MR::isPadSwing` sea verdadero (`exeHangUp`: +5 de velocidad por frame, tope 15), y `isPadSwing` es la sacudida del Wiimote o la del Nunchuk
  (`GamePadUtil.cpp`). El detector (`System/WPadHVSwing.cpp`, `updateSwing`) compara cada frame la muestra más nueva del acelerómetro con
  la de 20 muestras atrás (una por reporte, 100 por segundo) y da sacudida si están a 1 g o más.
- Causa de que costara subir: R1 daba un solo pulso de 12 reportes por pulsación; el juego veía sacudida unos 13 frames y se acababa. Había que machacar.
- Cambio: `platform/knulli/game/wii_shake.h` (`ShakeButton`). Tocar = el mismo pulso de antes, muestra por muestra. Mantener = 20 reportes
  alternando y 20 en reposo, repetido: con ese periodo cada muestra está a más de 1 g de la de 20 atrás y el juego ve sacudida en todos los frames.
  Vale para el Wiimote y el Nunchuk. `GALAXY_SHAKE_HOLD=0` vuelve a una sacudida por pulsación.

### Archivos modificados
`platform/knulli/game/{wii_shake.h,wii_shake_tests.cpp,native_game_host_knulli.cpp,CMakeLists.txt}`, el lanzador, `mario_galaxy.ini`, `README.md`.
`runtime/` no se tocó.

### Pruebas ejecutadas
- `nebula_wii_shake_tests` (PC): tocar o mantener sin repetir da exactamente el pulso del runtime (`advance_wiimote_shake_accel`), muestra por muestra;
  con una simulación del detector del juego (reportes a 100 Hz, frames a 60 Hz, calibraciones virtuales del Wiimote, 100 cuentas por g, y del
  Nunchuk, ~208): mantener 3 s daba sacudida en 13 de 179 frames antes y en 179 de 179 ahora, para los dos; un toque sigue dando 14 frames; al soltar no queda sacudida.
- `nebula_wii_profile_tests` sigue pasando; compila para aarch64 sin avisos.

### Resultados
- **No validado en la consola.** La simulación modela el detector del juego, no el juego entero: falta ver a Mario subir la enredadera con R1 apretado.
- Efecto colateral posible: mantener R1 fuera de una enredadera son giros seguidos (como sacudir sin parar un Wiimote real).

### Próximo paso
- Probar en la consola la enredadera del video y el tallo, con R1 mantenido; mandar `log.txt`.

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

## Versión estable

La última versión entregada (commit `234dd7e7`, paquete `u104`, nebula md5 `f2beda6cfca7`) es la **estable**:
etiqueta Git `estable`, copia en `deliver/estable/`. Incluye perfil `medio` por defecto, controles originales y
mantener X/Y/R2 para seguir sacudiendo. Sin validar en la consola: sacudida mantenida y perfil `medio`.

## Fase 1 de rendimiento: instrumentación de las esperas de frame-slot (sin commit)

- Problema: la espera de frame-slot (`RendererGles::begin_frame` → `glthread::wait_fence`) no distingue GPU, cola del
  hilo GL, swap/vsync ni latencia del sondeo de fences.
- Cambio (solo medición, sin tocar sincronización ni frames en vuelo): `glthread::fence_times` (cuándo el hilo GL
  inserta y publica cada fence), `Stats::swap_pending_ns` (swap con fences sin publicar), `RendererGles::note_slot_wait`
  (parte la espera en cola / GPU) y columnas nuevas al final de `frames.csv`: `espera_slot_cola_ms`,
  `espera_slot_gpu_ms`, `fence_cola_ms`, `fence_gpu_ms`, `fences`, `swap_con_fence_ms`, `gl_cpu_ms`, `gl_espera_ms`.
  Lanzador: temperatura en la línea `memoria:` cada 60 s.
- Pruebas en PC (x86, Mesa): `nebula_gl_thread_tests` ok; `nebula_gles_renderer_tests` 0 fallos con ULTRA y hilo GL
  (columnas presentes); `nebula_ultra_gles_tests` 3 fallos que ya existían sin el cambio. Valores no validados en consola.
- Paquete u105 (nebula md5 f9b43b77194d): serie calentamiento → medio (75) → bajo (auto) → medio2 (75), 6 galaxias.
- Resultado de u105 (frames.csv de medio/bajo/medio2): espera de frame-slot 327-367 ms/s, >99 % con la fence ya
  insertada (cola del hilo GL ~0); swap con fences pendientes 8-15 ms/s; ~125 fences/s; vida de una fence ~17-20 ms
  frente a 2,3-5,6 ms de GPU medida por trozo. Sin explicar: ~11 ms por fence.
- u106 (nebula md5 7d91f3a51cbc, sin commit): con GALAXY_ULTRA, un GL_TIMESTAMP_EXT antes de cada fence en el hilo GL
  (calibrado contra steady_clock cada ~1 s) parte esa vida en `fence_hasta_gpu_ms` y `fence_publicar_ms`
  (`fences_ts`, `fences_ts_malas`). x86: gl_thread y renderer ok, ultra con los 3 fallos previos. Sin validar en consola.
- Resultado de u106: se repiten las cifras de u105 (espera 326-368 ms/s, cola ~0, vida de fence 17-19 ms, swap con
  fences 8-9 ms/s). Las marcas de GPU: 100 % fuera de la vida de su fence en la Mali (~18 000 por recorrido); en
  Mesa (x86) caen dentro. La conversión glGetInteger64v/glQueryCounterEXT no vale tal cual en la Mali.
- u107 (nebula md5 dcb2d780324e, sin commit): además `fences.csv` en cada carpeta de ULTRA con los valores crudos
  (inserción, publicación, marca de GPU y cada calibración) para ajustar la conversión fuera de línea.
- Resultado de u107 (fences.csv, ~23 000 fences por recorrido): `glGetInteger64v(GL_TIMESTAMP_EXT)` devuelve en la Mali
  el reloj de pared (época Unix en ns), no el de la GPU: por eso fallaba la calibración. Las marcas de
  `glQueryCounterEXT` avanzan a 0,923 unidades por ns (≈ 24/26: contador de 24 MHz convertido como si fuera de 26),
  estable en todo el recorrido, con resolución de 2^19 unidades (~0,57 ms). Con un desfase local (la fence de menor
  latencia de cada tramo de 25-100 tomada como latencia ~0): vida media 21,2 ms (medio), 18,5 (bajo), 21,4 (medio2);
  antes de la marca 4,3-6,3 ms (coincide con las consultas TIME_ELAPSED por trozo), después 13-17 ms. Ninguna marca
  cae antes de su inserción. Sin distinguir todavía si lo de después es trabajo de la GPU que las consultas no ven
  (fragmentos de la Mali) o el hilo GL tardando en verla pasada.
- u108 (nebula md5 b75050fbd23b, sin commit): `fences.csv` añade `ultima_sin_pasar_ns` (la última revisión de
  `poll_fences` que encontró pendiente la fence más antigua, posterior a la inserción) y `vista_pasada_ns` (la
  primera vez que se vio pasada). Separa "el driver no la daba por pasada" de "el hilo GL no miró". Solo con ULTRA.
- Resultado de u108 (~23 000 fences por recorrido; 99 % con dato): la última revisión que encontró la fence sin
  pasar queda a 0,4 ms (p50; p90 1,9) de su publicación; vista pasada → publicada 0,01 ms (p50). Durante el 95-96 %
  de su vida el driver la da por no pasada, y sigue así ~14 ms (p50; bajo 11,6) después de la marca de GPU.
  Descartado: el hilo GL tardando en verla (hipótesis c). Las fences se completan en ráfagas: entre dos vistas
  pasadas, media 8,3-8,8 ms y mediana 1,4-1,6 ms. Pendiente: si las ráfagas coinciden con el final de cada pasada
  de render (fragmentos de la Mali al cerrar la pasada) o con otra espera del driver.
- u109 (sin commit): `fences.csv` añade, por fence, lo que tuvo su trozo: `draws`, `copias` (EFB), `limpiezas`,
  `lecturas` (peeks y readbacks) y `presenta`, para ver si las ráfagas de fences completadas caen al cerrar pasadas.
- Resultado de u109 (~23 000 fences por recorrido, medio/bajo/medio2 iguales): el 78 % de los trozos (un trozo =
  un begin_frame/end_frame con su fence y su slot) no tiene ningún draw: lecturas solas 22 %, vacíos 16 %, copias
  solas 15 %, presentación sola 15 %. Los draws (~250 por trozo con draws) van en ~22 % de los trozos. Unos 3,6 trozos
  por imagen mostrada. Las fences se completan en grupos de 1-4 (media 1,6) separados ~11 ms (p50). Indicio (por
  tiempos, no medido directamente): ~12 % de los trozos empieza justo al publicarse la fence de dos atrás, y ~80 % de
  esos no tiene draws. Fin de la Fase 1: la espera de frame-slot es la de trozos sin draws esperando a que la GPU
  termine el trozo con la escena, porque cada trozo ocupa un slot de los 2.

## Fase 2 (solo lectura): qué usa por slot un trozo sin draws

- Recursos que se rebobinan en cada begin_frame (todos suponen que la fence del slot pasó): `RendererGles::frames_[slot]`
  (retirados), los 4 `UploadRing::begin_frame`, `texture_tables_[slot]`, `gpu_segments_[slot]`; en
  `GxBackend` (gx_backend.cpp:12634-12658) `resident_vertices_.advance_frame`, `EfbCopyManager::begin_frame`
  (texturas XFB retiradas por slot), `TextureCache::begin_frame` (retirados, arenas de subida, staging de packs y vídeo).
- Por fence, sin slot: `Context::collect` (borrados diferidos), readbacks y peeks async.
- Trozos sin draws: copias y peeks escriben el anillo de constantes (`bind_utility_constants` desde
  `conversion_draw`) y retiran texturas; la presentación (blit) y los vacíos no escriben anillos.
- Candidato (sin implementar): rotar el slot por imagen presentada y no por trozo, sin rebobinar nada de lo anterior
  mientras el slot no cambie. Riesgos: un frame más de latencia posible, más memoria retenida por slot, anillos con
  el contenido de varios trozos, interpolación (kInterpSegments) y D3D12 comparten la suposición.

## Fase 2: experimento GALAXY_GLES_SLOT_POR_IMAGEN (sin commit)

- Cambio (apagado por defecto): `RendererGles::begin_frame` toma slot nuevo solo tras una presentación (o a los 8
  trozos sin presentar, `kMaxStretchesPerSlot`); mientras no cambia, no espera ni rebobina anillos, tablas de texturas,
  retirados ni consultas de GPU, y los slots prestados se mantienen hasta cambiar de slot (cada trozo les da su
  fence). `GxBackend` (gx_backend.cpp) avanza `resident_vertices_`, `EfbCopyManager` y `TextureCache` solo si
  `renderer_.slot_rotated()`. Con el interruptor apagado el camino es el de antes.
- Prueba en PC (`nebula_gles_renderer_tests`, con y sin hilo GL): 0 fallos; nueva sección: trozos sin presentar
  comparten slot, las subidas no se rebobinan y siguen intactas, tras presentar cambia de slot, tope de 8, sin
  errores de GL tras dar varias vueltas. Sin validar en la consola.
- Paquete u110 (nebula md5 245c7e3502d1): serie calentamiento → base → slot → base2 → slot2 (medio, 6 galaxias).
- Resultado de u110 (serie base → slot → base2 → slot2, 6 galaxias, medio): con el slot por imagen, FPS dibujados
  13,2/13,5 → 16,3/16,4 (+24 %), mostrados 33,1/32,7 → 41,8/41,8 (+26 %), p95 −11 %, p99 −15/−21 %, frames > 50 ms
  119/133 → 62/77, > 100 ms 9/10 → 9/9, recorrido 202 → 161 s. Espera de frame-slot 380 → 157 ms/s; la GPU medida
  sube de ~425 a ~633 ms/s (más ocupada); espera del swap 8-16 → ~238 ms/s; RSS pico igual (607-616 frente a
  605-611 MiB); errores de GL 0. "Tirones/min" sube (48-58 → 129-140) porque se cuentan contra el doble de la
  mediana del intervalo, que bajó de 29 a 19,6 ms: los frames largos de verdad bajaron. Capturas: las diferencias
  (19-29 dB) son de estado del juego (Mario en otra posición, HUD visible), no de dibujo; base2 frente a base
  también da 21 dB. Sin medir: latencia de entrada, partida jugada entera, muOS.
- u111 (mismo nebula 245c7e3502d1): "recorrido 44 slot" (serie [slot44]: 44 galaxias, medio, slot por imagen,
  memoria cada 30 s, límite 2400 s) y "jugar slot por imagen" (port limpio con smg/ultra/ultra.cfg: NEBULA_ULTRA=0
  y GALAXY_GLES_SLOT_POR_IMAGEN=1, que pisa el ultra.cfg de los paquetes de medición). Pendiente de la consola.
- Resultado de [slot44] (u111): las 44 galaxias completas, salida con estado 0, 0 errores de GL; 38,6 FPS mostrados y
  14,9 dibujados de media, p95 49,2 ms, p99 78,6 ms; RSS pico 691 MiB, malloc pico 259 MiB, mínimo disponible 111 MiB
  (antes, recorrido de 44 sin slot por imagen: malloc 233 MiB, mínimo libre 138 MiB, otra sesión). 28 paradas de más
  de 250 ms en juego estable (10 en IceVolcanoGalaxy; la mayor 3,05 s de espera de slot en CubeBubbleExLv1Galaxy,
  con GPU casi 0). Sin control en la misma sesión: se prepara [base44] (mismo nebula, interruptor a 0).
- Control [base44] (mismo nebula 245c7e3502d1, interruptor a 0, misma tarde): 44/44, 0 errores de GL; 31,9 FPS
  mostrados, 12,8 dibujados, p50 29,0 / p95 59,2 / p99 89,2 ms, recorrido 1396 s; 17 paradas > 250 ms en juego
  estable (10,7 s en total), carga más larga 737 ms; RSS pico 675 MiB, disponible mínimo 145 MiB y estable desde el
  primer cuarto (190, 145, 149, 148).
- [slot44] frente a [base44]: +21 % mostrados, +16 % dibujados, p95 −17 %, p99 −12 %, recorrido −16 %; pero 28
  paradas > 250 ms (19,3 s), carga más larga 2443 ms, y la memoria disponible baja todo el recorrido (193, 130, 123,
  111 MiB por cuartos; RSS final = pico 691). En [slot44], de los contadores de [memoria] la anónima/malloc sube
  (malloc 193 → 241 MiB) con listas, texturas y vértices estables. Sin el log de [base44] no se sabe si malloc sube
  igual. No pasa a ser el valor por defecto.
- Con el log de [base44]: malloc sube también sin el cambio (171 → 237 MiB; [slot44] 153 → 259), con listas,
  texturas y vértices iguales en los dos. La diferencia se mantiene desde la mitad (+19, +22, +22 MiB de malloc;
  ~35 MiB menos disponible): el slot por imagen retiene más, sin divergir. El crecimiento de malloc sin contabilizar
  (~65 MiB por recorrido de 44) existía antes: tema aparte.
- Paradas largas: en los dos recorridos son fences que el hilo GL dejó de revisar 1-3 s (la última revisión sin
  pasar fue 0-37 ms tras insertarla; luego ninguna hasta publicarla, varias juntas): el hilo GL estaba bloqueado en
  alguna llamada. Con el slot por imagen pasan de 23 a 102 fences de más de 250 ms (de 8 a 35 de más de 1 s); no se
  concentran en tramos largos sin presentar (la mayoría a 1-3 trozos de la presentación).
- u112 (nebula md5 4c7b48565df8, sin commit; binario sin despojar guardado como sm/nebula-u112.unstripped): con
  GALAXY_ULTRA, el hilo GL mide cada comando y los de más de 50 ms van a `gl_lentos.csv` (inicio, duración, offset
  de la función en el ejecutable, MemAvailable). En PC se simboliza con addr2line (p. ej. glClear, glReadPixels,
  glBlitFramebuffer). Serie [base44] → [slot44].
- Resultado de u112 (44 x2, gl_lentos.csv simbolizado con sm/nebula-u112.unstripped): comandos del hilo GL de más de
  50 ms: base44 25 (24 sync_call, máx. 87 ms; 1 glDrawRangeElements de 914 ms), slot44 234 (194 trabajos
  `enqueue` = el swap o la lectura de consultas, 50-87 ms; 39 sync_call, máx. 126 ms; 1 glDrawRangeElements de
  371 ms). Ninguno cae dentro de las ventanas de fences de más de 1 s (base 2 ventanas, slot 3): en ellas la fence
  se ve pasada justo al publicarse y la última revisión sin pasar fue 0-54 ms tras insertarla, o sea que en 1-2 s no
  hubo ninguna revisión que volviera. Sospecha: el hilo GL bloqueado dentro de `glClientWaitSync` de
  `poll_fences` (fuera de run_one, no medido) o desplanificado; posible relación con poca memoria (heap del tiler de
  la Mali, reclamación del kernel). Sin medir.
- Totales de u112 (misma sesión): slot44 frente a base44 +23 % mostrados (31,9 → 39,3), +21 % dibujados (12,7 →
  15,4), p95 −18 %, p99 −17 %, frames > 25 ms −40 %, recorrido −19 %. Esta vez slot44 tuvo MENOS paradas > 250 ms
  (13 frente a 24; 9,5 s frente a 11,5; > 1 s 3 y 3) y su memoria disponible no siguió bajando (169, 129, 130, 131
  MiB; mínimo 129 frente a 141). Con las dos parejas (u111 y u112): las paradas no dependen del slot por imagen
  (17/28 y 24/13); el coste de memoria es 12-35 MiB menos disponible; la mejora de rendimiento se repite.
- El usuario jugó con el slot por imagen (u113): "se siente bien". El lanzador lo activa por defecto en el perfil
  medio (`GALAXY_GLES_SLOT_POR_IMAGEN=${GALAXY_GLES_SLOT_POR_IMAGEN:-1}`); bajo y normal siguen sin él (no medidos).
  README: ajuste documentado. Pendiente aparte: las paradas de 1-2 s del hilo GL (existían sin el cambio).

## Versión estable (5-nov, noche)

La estable pasa a ser el paquete `deliver/estable/` de esta sesión (nebula md5 4c7b48565df8; etiqueta Git `estable`
movida a este commit): perfil medio con slot por imagen por defecto. Sustituye a la u104.
