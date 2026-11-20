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

## Meta: 30 FPS mostrados estables (meta A, sin caer a 20)

- Con la estable actual (slot por imagen) la GPU es el límite: el hilo de render pasa > 50 % esperando a la GPU o al
  swap; de la GPU medida, las copias del EFB a textura son el 45-47 % y la escena el 48-51 %.
- u114 (sin commit; binario sin despojar en sm/nebula-u114.unstripped): columnas nuevas en frames.csv
  `copias_prof`, `copia_origen_kpx`, `copia_destino_kpx`, `copias_y_borrado` (copias seguidas de un borrado completo
  del EFB antes de cualquier draw) y `gpu_copias_ms`. Solo contadores. Serie calentamiento → medio → bajo → medio2
  (todas con el perfil medio del lanzador, o sea con slot por imagen; bajo = resolución auto).
- Resultado de u114 (medio/bajo/medio2, estable con slot por imagen): 41,8-42,9 FPS mostrados, 16,0-17,5 dibujados;
  el 25-27 % del tiempo de juego va en imágenes de más de 34,5 ms (por debajo de 30). Copias del EFB a textura:
  1,9 por imagen, ninguna de profundidad, ~200 kpx de origen (= destino), 43-44 % de la GPU medida (bajo: −18 % en
  copias, −17 % GPU total, pero el tiempo bajo 30 no cambia: 26 %). `copias_y_borrado` = 0 en todo: descartada la
  recarga del EFB tras copia + borrado completo. El 54 % de las imágenes no tiene draws (repeticiones del salto);
  las imágenes de más de 34,5 ms son las dibujadas pesadas: ~365 draws, ~165 k vértices, CPU de render ~21 ms,
  CPU del hilo GL ~21 ms, esperas de slot ~13-17 ms y swap ~9-12 ms, GPU medida ~26-28 ms.
- u115 (sin commit; sm/nebula-u115.unstripped): `copias.csv` en cada carpeta de ULTRA, una línea por copia del EFB a
  textura con su GPU propia (el segmento de consulta de esa copia): frame, dirección de destino, rectángulo de
  origen, tamaño de destino, formato, media escala, borrado, copia nativa, intensidad, gpu_ms. Solo con hilo GL.
  Las pruebas de la PC no hacen copias a textura: la ruta se comprueba en la consola.
- Resultado de u115 (copias.csv, medio y bajo): ~5500-5900 copias por recorrido, tres tipos, cada uno a un único
  destino fijo: 640x456 formato 7 (A8, según la numeración de GX) 2239 copias, 57 % del tiempo de copias,
  10,4 ms cada una; 640x456 formato 4 (RGB565) 2235, 32 %, 5,9 ms; 160x160 formato 7, 1043, 10 %, 4,0 ms. Orden
  típico por trozo: A8 completa → RGB565 completa (1029), RGB565 sola (605), A8 → 160x160 → RGB565 (601). El coste no
  sigue al tamaño de la copia (160x160 = 25,6 kpx cuesta 4,0 ms; con el EFB a 320 de ancho, 2,3 ms): sigue al
  tamaño del EFB. La primera copia de cada trozo cuesta el doble (10,4 frente a 5,7 ms). Lectura: en la Mali el
  segmento de la copia se lleva el trabajo diferido de la pasada que cierra (sombreado de la escena y guardado del
  EFB), así que el 43 % "de copias" no es coste de las copias en sí. Corrige la lectura anterior.
- u116 (sin commit; sm/nebula-u116.unstripped): `copias.csv` añade `draws_antes` y `borrados_antes` (draws y
  borrados del EFB desde la copia a textura anterior), para saber si copias seguidas leen el mismo EFB.
- Resultado de u116: todas las copias a textura tienen draws antes (las grandes 100+, la de 160x160 10-99) y ningún
  borrado antes: cada copia cierra una pasada con trabajo propio; no hay copias seguidas del mismo EFB (descartado
  juntarlas). El coste es el de los draws de cada pasada. Con bajo (50 %, 0,44 de los píxeles del medio) la GPU
  medida baja ~17-25 %: si GPU = A + B·píxeles, ~2/3 no dependería de la resolución (vértices, mosaicos, costes por
  pasada). Diagnóstico preparado (mismo binario u116): serie r100 → r75 → r50 → r25 → r75b.

## Meta A (30 FPS mostrados sostenidos): diagnóstico de resoluciones (preparado, sin datos aún)

- Qué escala `GALAXY_GLES_RESOLUTION`: ancho y alto del EFB (color RGBA8, profundidad DEPTH_COMPONENT32F),
  `efb_num_/efb_den_` en `RendererGles::initialize` (25-100, ajustado a una fracción exacta): 100 = 640x528,
  75 = 480x396, 50 = 320x264, 25 = 160x132 (píxeles 1 / 0,5625 / 0,25 / 0,0625). No escalan: los destinos de las
  copias a textura (`compute_scaled_efb_copy_extent` con `efb_scale_` entero = 1: tamaño lógico, p. ej. 640x456),
  el XFB (`EfbCopyManager`, tamaño lógico), los campos de lectura (`GALAXY_GLES_PEEK_FIELD=25`: 160x132) y la
  ventana.
- Contadores: `vertices` de frames.csv = índices dibujados (index_count por draw; glDrawArrays: su cuenta);
  `vertices_vista` (u117) = vértices de la vista de vértices de cada draw indexado (SizeInBytes/stride: cota de los
  distintos que el shader puede leer), `vertices_kib` sus KiB. Ninguno es ejecuciones del shader.
- Con los datos de u114 (r75/r75b/bajo≈r50) y `tools/ultra_resoluciones.py`: AstroGalaxy pasa el 41-42 % del tiempo
  por debajo de 30 a 75 y a 50 %; su hilo de render gasta 700-710 ms/s de CPU con esperas de slot 0-32 y de swap
  4-54 ms/s, y la GPU medida 347-480 ms/s: en AstroGalaxy el límite es la CPU del hilo de render, no la GPU. En el
  total, GPU por imagen con draws ≈ 17,7 ms fijos + 0,76 ms/10 000 px (3 tamaños, poco fiable).
- Estático: los vertex shaders de la biblioteca declaran siempre 10 salidas (2 colores + 8 uv vec3 highp) y
  escriben a 0 de 5 a 7 uv que ningún fragment shader lee (estos declaran 0-4 entradas). Si el driver no las quita al
  enlazar, la Mali las guarda por vértice. Hipótesis, sin medir.
- u117 (sin commit; sm/nebula-u117.unstripped, nebula 5a267dbbc19a): serie calentamiento → r75 → r100 → r50 → r25 →
  r75b → perfil75 (muestreo de hilos). Análisis: `python3 -I tools/ultra_resoluciones.py <serie> AstroGalaxy`.
- Resultado de u117 (r75, r100, r50, r25, r75b; CPU 1512 MHz y GPU 696 MHz fijas, 60-70 °C): con 16 veces menos
  píxeles (100 → 25) el tiempo bajo 30 solo pasa de 31,3 a 22,2 % en el total. GPU por imagen con draws = 17,9 ms
  + 0,72 ms por 10 000 px (residuo máx. 4 %); al 75 % la parte que no escala es el 57 % (incluye destinos de copia y
  XFB fijos, vértices y mosaicos, costes por pasada: no es "coste de vértices"). AstroGalaxy: 38-45 % bajo 30 en
  TODAS las resoluciones, CPU del hilo de render 667-710 ms/s, esperas de slot 0-56 y de swap 6-52 ms/s: limitada
  por la CPU del hilo de render. En las demás la resolución sí ayuda (BreakDownPlanet 38 → 20 %, BattleShip 23 → 13 %,
  CannonFleet 17 → 6 %). Draws (~330-360) e índices (~170-180 k) por imagen iguales en todas: misma carga.
- perfil75 (muestras del hilo de render, 6 galaxias): 35 % esperando trabajo del juego, 16 % en wait_fence; de lo
  ocupado (~49 %): run_window 43 %, replay_cached_display_list 15 %, on_draw 13 %, find_cached_display_list 7 %
  (4,8 % dentro de libc: el memcmp de las listas), flush_draw_state 6 %, VertexLoader 5 %.
- Experimento u118 (sin commit, apagado por defecto): GALAXY_GX_DL_CONFIAR=1 da por vigente una lista en caché si
  ningún flush del juego la tocó (activa la invalidación por páginas sucias, que estaba apagada por defecto); =2
  verifica (compara igual y cuenta "[gx] lista sin aviso"). Con =1 fallan 3 casos de nebula_fifo_display_list_tests
  que cambian bytes sin aviso (es lo que ese modo deja de comprobar); con 0 y 2 pasan. Serie base → verif →
  confiar → base2 → confiar2.
- Resultado de u118 (GALAXY_GX_DL_CONFIAR): sin mejora. AstroGalaxy: CPU de render 726 ms/s con confiar2 frente a
  698/708 en base/base2, tiempo bajo 30 43,7 % frente a 43,0/43,9; total 28,3 % frente a 25,5/27,3. 0 errores de GL.
  Capturas: diferencias de momento (cámara/brillos), no de dibujo, a la vista. La verificación (=2) no se completó:
  ese recorrido se quedó en el aviso del Nunchuk a mitad de AstroGalaxy y A no respondía (único caso; confiar2 y
  base2 terminaron bien); hasta ahí, 0 "listas sin aviso". Descartado: el cambio se quitó del árbol local y se guardó
  en deliver/u118/experimento-dl-confiar.patch. Incidencia aparte: el aviso del Nunchuk a mitad de una galaxia que
  no se cierra con A ni con el toque automático (este solo actúa si el juego deja de contar imágenes de escena).
- u119 (mismo binario que u117, 5a267dbbc19a): AstroGalaxy sola: astro → astro_perfil (muestreo de todos los
  hilos) → astro2.
- Resultado de u119 (AstroGalaxy sola, 3 recorridos iguales salvo el muestreo): 44-46 % del tiempo bajo 30, p95
  45-47 ms, CPU del hilo de render 742-747 ms/s, del hilo GL 543-545 ms/s, esperas de slot ~0 y de swap 4-6 ms/s,
  GPU 25-26 ms por imagen con draws; 0 errores de GL. El hilo de render pasa el resto (~250 ms/s) esperando trabajo
  del juego. Muestreo (incluye arranque y carga: rondas cada 10 s desde los 10 s): hilo de render ocupado en
  run_window 54 %, replay 21 %, on_draw 13 %, find_cached_display_list 9 %; hilo del juego: 64 % código del juego y
  ~25 % parseando el mismo FIFO (find_cached_display_list 9 %, run_window 5 %, render_frame 7 % en libc); hilo GL:
  77 % dentro de libmali. Siguiente: muestreo solo durante AstroGalaxy (rondas a 43-59 s), dos repeticiones.
- Perfil de AstroGalaxy solo en juego (u119, rondas a 43-59 s, dos repeticiones casi iguales): hilo de render
  ocupado ~77 %: run_window 68 %, replay_cached_display_list 35 %, find_cached_display_list 18 % (11 % dentro de
  libc: el memcmp de las listas), on_cached_packet_draw_run 14-15 %, flush_draw_state 8-9 %. Hilo del juego:
  render_frame 48 % (FramePeEventClassifier::classify 30 %: vuelve a recorrer el FIFO; find_cached_display_list
  18 %, 12 % en libc; 13 % en libc directamente en render_frame), código del juego ~48 %.
- Por qué GALAXY_GX_DL_CONFIAR no ayudó: con la invalidación por flush, el juego vuelve a vaciar la caché de listas
  cuyos bytes no cambian y cada lista tirada se vuelve a interpretar.
- u120 (sin commit; sm/nebula-u120.unstripped, nebula 562acac136f3): GALAXY_GX_DL_MARCAR=1 marca (no tira) las
  listas que toca un flush; una lista sin marca no se compara; una marcada se compara y se desmarca. =2 verifica
  (compara todo; "[gx] lista sin aviso" si cambió sin marca). Pruebas: nebula_fifo_display_list_tests ok con 0 y 2;
  con 1 falla un caso (cambio sin aviso, lo que ese modo deja de comprobar); renderer 0 fallos.
- Resultado de u120 (GALAXY_GX_DL_MARCAR): verificación (=2) en 6 galaxias: ~6,8 millones de comparaciones, 0 listas
  cambiadas sin flush. Con =1 la CPU del hilo de render baja ~7,5 % (total 528 → 488-490 ms/s; AstroGalaxy 702-707 →
  648-653) y AstroGalaxy mejora (tiempo bajo 30: 43,1-43,3 % → 36,2-37,9 %). Pero el total empeora (26,0-26,3 % →
  28,4-28,8 %) por las galaxias limitadas por la GPU (CannonFleet 8-10 % → 18-20 %, p95 34 → 39 ms): se dibujan más
  imágenes (19,2-19,5 → 19,9-20,2 /s) y la GPU por imagen sube (31,7-32,9 → 33,1-33,6 ms). Hipótesis: el salto de
  frames automático decide dibujar más cuando la CPU sale más barata, aunque la GPU no llegue. 0 errores de GL;
  memoria pico igual (629-631 MiB frente a 631-634). Queda apagado por defecto; sin commit.
- Causa de la regresión de u120 en CannonFleet: el salto automático pasó de "1 de 3" a "1 de 2" dibujados (CPU por
  imagen dibujada 21,5 ms + 9,3 por saltada: cabía en 33 ms) mientras la GPU medida por imagen dibujada era de
  37-39 ms. u121 (sin commit; sm/nebula-u121.unstripped, nebula 884baa2d008d): GALAXY_FRAMESKIP_GPU=1 (apagado por
  defecto) exige que también la GPU medida de una imagen dibujada (media de las últimas con 3 ms o más,
  `RendererGles::recent_drawn_gpu_ms`) quepa en el ciclo; la línea [frameskip] del log la muestra. Serie base → gpu
  → ambos (con GALAXY_GX_DL_MARCAR=1) → base2 → gpu2 → ambos2. Renderer: 0 fallos.
- Resultado de u121 (6 galaxias, 0 errores de GL, sin cierres ni avisos del Nunchuk):
  · GALAXY_FRAMESKIP_GPU=1 solo: tiempo bajo 30 25,9/24,4 % → 21,6/22,9 %, p95 42,0/41,8 → 40,8/40,4 ms, mostrados
    42,8/43,0 → 43,9/43,8; a cambio, imágenes con draws 20,1/20,0 → 19,6/19,4 por segundo (−2,5 %): es en parte
    más salto de frames, no menos trabajo. Mejora BattleShip (17 → 12 %), BeltConveyer (28 → 17-20 %), CocoonEx
    (22 → 18-19 %), CannonFleet (8 → 5-8 %); AstroGalaxy igual (41-42 %); BreakDown dentro del ruido.
  · Con GALAXY_GX_DL_MARCAR=1 además: AstroGalaxy 41-42 → 34-37 %, CPU de render −8 %; pero CocoonEx empeora (26-30 %)
    y CannonFleet una de dos veces (17,7 %). Total 22,0/24,6 %. Causa sin medir.
- u122 (mismo binario 884baa2d008d): perfil de todos los hilos en 6 galaxias, base → marcar → base2 → marcar2.
- u123: paquete para jugar (port limpio, nebula de u121) con smg/ultra/ultra.cfg NEBULA_ULTRA=0 y GALAXY_FRAMESKIP_GPU=1.

### Estrategia del port de Vita para el observatorio (u124, sin cambios de codigo)

- Vita (galaxy-vita f8fd956, platform/src/gx/gx_fifo.cpp y vgx_galaxy.cpp): tres nucleos (juego, "player" que parsea,
  back end GL). El hilo del juego no parsea: publica su posicion de escritura cada 32 KiB y en cada flush; el player
  lee la memoria viva y lanza PE finish/token al llegar al comando; el juego solo espera en "draw done". Observatorio:
  43 fps de media (32-55) segun su README.
- Nebula por defecto (GxTimingConfig: render_live_memory_wait=1): cada render_frame espera a que el hilo de render
  termine de leer la memoria del trozo (wait_for_render_cpu_reads_idle), y antes el hilo del juego lo recorre otra vez
  para preclasificar eventos PE (FramePeEventClassifier::classify, 21 % de las muestras del hilo del juego en
  AstroGalaxy, s119). El envio por trozos de 32 KiB (stream_gx_fifo_submission) solo se activa con lectura viva.
- El modelo de Vita ya existe: GALAXY_RENDER_LIVE_MEMORY_WAIT=0 GALAXY_RENDER_MEMORY_SNAPSHOT=0
  GALAXY_RENDER_LIVE_MEMORY=1 (+ GALAXY_PE_PRECLASSIFY=0). Serie u124: base / vivo (sin barrera, sin trozos) / vita
  (sin barrera, sin preclasificacion, trozos de 32 KiB), x2. Sin medir todavia en la consola.
- Riesgos: el render lee memoria que el juego ya puede cambiar (como el GP real; depende de la disciplina de
  GXDrawDone); con preclasificacion en lectura viva, una lista con eventos PE reescrita entre ambas pasadas haria saltar
  "GX PE preclassifier/render parser event-count mismatch" (error, no imagen mala).
- Resultado u124 (6 galaxias x2, juego estable): "vivo" (solo sin barrera) = base en todo. "vita": AstroGalaxy por
  debajo de 30 del 43,5/43,2 % al 23,3/23,1 %, p95 45 -> 37-38 ms, 42,5 FPS mostrados (36); la escena dura 20,4 s en vez
  de 24,8 s, el juego avanza mas rapido. Pero CannonFleet 8,8 -> 18 %, BreakDown 25 -> 31-34 %, BattleShip 17 -> 24 %;
  en CannonFleet por imagen dibujada GPU 35 -> 47 ms, draws 337 -> 459, copias 3,8 -> 5,2, espera de swap 9,5 -> 24 ms
  (hipotesis sin medir: los trozos ya enviados no se pueden saltar). Tirones 122-139 -> 214-230/min. Capturas sin
  defectos visibles. Total por debajo de 30: 24,8 -> 24,4 %. u125 separa sin preclasificacion / con trozos.
- Resultado u125: "trozos" (con envio de 32 KiB y con preclasificacion) = base en todo. Todo el efecto de "vita" viene
  de GALAXY_PE_PRECLASSIFY=0: AstroGalaxy por debajo de 30 41-42 % -> 19-22 %, escena 24,7 -> 20,4 s; todas las escenas
  salvo BreakDown duran 4-17 % menos (el juego avanza mas rapido). A cambio, en CannonFleet por imagen dibujada GPU
  34 -> 46 ms, draws 335 -> 450, por debajo de 30 9 -> 19-20 %; tirones 129-133 -> 207-214/min. Total por debajo de 30
  24,3/24,4 -> 22,6/23,8 %. Hipotesis de los trozos descartada. Hipotesis sin medir: con el juego mas rapido hay mas
  frames del juego por imagen dibujada.
- Error de empaquetado: u123 llevaba el lanzador de juego, que solo lee smg/ajustes.cfg; GALAXY_FRAMESKIP_GPU=1 en
  smg/ultra/ultra.cfg no se aplicaba (u123 = configuracion estable). u126 (para jugar) usa smg/ajustes.cfg con los
  ajustes de "sinclas".
- u127 (perfil de hilos en AstroGalaxy): con sinclas el hilo de render pasa de 51 % de muestras trabajando (estable) a
  91-92 %: queda saturado. Dentro: run_window 76-81 %, replay_cached_display_list 36-41 %, find_cached_display_list
  21-22 %, display_list_bytes_current 15,5 %, on_cached_packet_draw_run 14-16 %. El hilo del juego no se muestreo con
  sinclas (AstroGalaxy empieza a los 27,6 s y el recorrido acaba a los 49 s; la ronda empezo en el hilo de render).
  u128: sinclas frente a sinclas + GALAXY_GX_DL_MARCAR=1, verificacion con MARCAR=2 en lectura viva y perfil a los 29 s.
- u128: GALAXY_GX_DL_MARCAR=1 sobre sinclas no mejora (total por debajo de 30 22,0/24,1 sinclas frente a 24,1/24,2;
  AstroGalaxy 18,5/23,2 frente a 21,9/18,9: dentro del ruido); verificacion en lectura viva: 1,4 millones de
  comparaciones, 0 sin aviso. Descartado como mejora. Perfil a los 29 s (sinclas, AstroGalaxy): hilo del juego 79 % de
  sus muestras dentro de libsmg_game.so (perfil plano: NameObjCategoryList::execute 61 %, calcAnim 25 %, draw 19 %,
  ModelManager::updateDL 14 %; JASAudioThread 11 %, mezcla DSP del host ~6 %), render_frame ~8 %; hilo de render 92 %.
  Los dos hilos de CPU estan llenos: el resto del mirador es la logica del propio juego.
- Vita (docs/TECHNICAL.md): ThinLTO de todo el codigo del juego (--lto-O2), hilo del juego -12 % en instrucciones.
  Nebula compila libsmg_game.so a -O3 sin LTO. Compilando u129 con -flto=thin en sm/juego-thinlto (sin tocar el repo).
- u129: libsmg_game.so con -O3 -flto=thin (sm/juego-thinlto, ~10 min con 2 nucleos; mismos simbolos exportados e
  importados salvo exp2 que ya no se importa; 12,9 MB despojado frente a 9,6 MB). Serie sinclas / lto x2 + perfil del
  hilo del juego con lto; el modulo va en smg/lto/ con una copia de nebula (NEBULA_ULTRA_BIN=lto/nebula).
- Resultado u129: el modulo ThinLTO se cargo (muestras hasta +0xbd23b0, fuera del texto del normal) y no fallo. Sin
  mejora: total por debajo de 30 21,0/22,6 % (normal) frente a 23,2/23,7 % (lto); AstroGalaxy 21,0/23,4 frente a
  19,0/25,4; tirones 200-207 frente a 230/min. Capturas equivalentes. Descartado. El ruido entre repeticiones (+-3 puntos
  por escena) tapa cambios de CPU de menos del ~10 %.
- Opcion 2 (los tirones de sinclas), con los frames.csv de u125: no son mas trabajo. En CannonFleet las imagenes
  completas (>= 100 draws) son las mismas (~220 por recorrido, 400-600 draws, GPU ~41 ms). La subida de "draws por
  imagen dibujada" es un efecto de la medida: la estable presenta tras cada imagen completa otra de 7 draws y 1 copia
  (~130 por recorrido) que en sinclas casi no aparece (25-32), y esas cuentan como dibujadas. Entre imagenes completas,
  CannonFleet: mediana 68,3 ms (estable) frente a 62-65 ms (sinclas), desviacion 18-31 frente a 15-21. Los tirones y el
  "por debajo de 30" se miden en presentaciones, casi todas repeticiones de la misma imagen; en sinclas sus intervalos
  son menos regulares (11-39 ms en lugar del ciclo 20/17/31 ms) sin que cambie la imagen en pantalla. AstroGalaxy:
  70 ms entre imagenes completas (desv. 12) frente a 61-63 ms (desv. 21) en la estable, con la escena 4,3 s mas corta.
  u130: las 44 galaxias estable/sinclas.
- Resultado u130 (44 galaxias, estable/sinclas, sin errores, CPU 1512 MHz y GPU 696 MHz fijos, 57-62 grados en ambos):
  el recorrido dura 1351 -> 1093 s (-19 %: el juego avanza mas rapido en casi todas las galaxias), pero las imagenes
  nuevas por segundo (>= 15 draws) bajan en 34 de 44 galaxias, a ~11-17/s en casi todas (BattleShip 33,8 -> 16,7;
  BeltConveyer 25,7 -> 15,2; AstroGalaxy 15,3 -> 13,4). El log del salto: los frames saltados cuestan 8-15 ms del hilo
  de render en la estable y 12-24 ms en sinclas, la GPU por imagen dibujada 17-22 -> 31 ms. Con el juego a su
  velocidad hay mas frames del juego por segundo y cada uno, saltado o no, se genera y se parsea entero: el salto de
  Nebula solo ahorra la parte de GL. sinclas cambia fluidez por velocidad; no es mejor estable.
- Vita (decomp/src/Game/System/GameSystem.cpp, platform/src/port/perf.cpp): el salto es en el bucle del juego. Un
  turno saltado corre update() y calcAnim() sin beginRender/draw/endRender (no hay comandos GX que generar ni parsear);
  salta si el turno empieza mas de media vsync tarde (hasta N seguidos, sin esperar el retrace) o si el back end tiene
  2 imagenes sin terminar (hasta 6 seguidos, esperando el retrace). Los cambios de escena se dibujan siempre.
- u131: salto de frames en el bucle del juego (GALAXY_GAME_FRAMESKIP=1, apagado por defecto). GameSystem::frameLoop
  pregunta al host (NEBULA_OPTION_FRAMESKIP_TURN) al empezar cada turno: 0 dibuja; 1 (el renderer va detras: un envio
  del turno anterior encontro la cola llena, hasta GALAXY_GAME_FRAMESKIP_PIPE=2 seguidos) y 2 (el turno empieza mas de
  media vsync tarde, hasta GALAXY_GAME_FRAMESKIP_LATE=3 seguidos, sin esperar el retrace) corren update() y calcAnim()
  sin beginRender/draw/endRender/endFrame. Un cambio de escena pedido se dibuja y reinicia el reloj. Parche sobre
  2fbe2a40 en deliver/u131/experimento-salto-juego.patch (copia de trabajo aparte work/wt-estable). Serie: las 44
  galaxias, la estable (sus binarios) frente a estable + parche con GALAXY_FRAMESKIP=0. Medida: imagenes nuevas por
  segundo (filas con 15 draws o mas) y duracion de escena (1200 frames del juego por galaxia).

## Rama optimizacion-h700 (19-nov, sin commits: nada aceptado aun en el H700)

- Version que corre: juego estable 2fbe2a40 (smg.zip de deliver/estable); medicion con nebula 884baa2d008d (2fbe2a40 +
  los cambios locales: GALAXY_GX_DL_MARCAR, GALAXY_FRAMESKIP_GPU, contadores de ULTRA), los mismos binarios de u130/u132.
- u132 (44 galaxias, sin preclasificar): RSS media 644 MiB, pico 683, minimo libre 141 MiB; sube hasta ~IceVolcano y se
  queda en 650-683 sin crecer (malloc 119 -> ~200 a la mitad -> 217; buffers GL 164 fijos; texturas GL ~50). Sin OOM.
  u130: estable 655/687/127, sin preclasificar 638/673/165. Ruido entre u130 y u132 con los mismos ajustes: 10-25 MiB.
- Reanalisis con imagenes nuevas por segundo (filas con 15 draws o mas, scripts imagenes.py):
  u120 (6 galaxias, modo estable): base 17,1 / base2 17,1; GALAXY_GX_DL_MARCAR=1 18,2 / 18,2 (+6,4 %, repetido; duracion de
  escenas 118 s en las cuatro). Se habia descartado con "tiempo por debajo de 30 FPS mostrados"; con el criterio de
  imagenes nuevas pasa. u121: GALAXY_FRAMESKIP_GPU=1 17,6/17,5 -> 17,1/17,0 (peor). u128 (sin preclasificar): MARCAR igual.
- En la rama: el salto en el bucle del juego (parche de u131, GALAXY_GAME_FRAMESKIP, apagado por defecto) aplicado sobre
  los cambios locales. Pruebas en x86-64: fifo_display_list ok (MARCAR 0 y 2; con 1 falla el caso de cambio sin aviso, por
  diseno), gles_renderer 0 fallos, gl_thread ok, gles_shader 22 enlazados 0 fallos, ultra_gles 0 fallos (con
  GALAXY_ULTRA=bench y GALAXY_ULTRA_DIR). nebula para aarch64 compila (build-gles-a64).
- u133 (serie para el H700): calentamiento; fs0_base/fs0_marcar x2 (6 galaxias, GALAXY_FRAMESKIP=0); base44, marcar44,
  juego44 (smg/juego/: estable + salto en el juego) y verif44 (MARCAR=2: sin "[gx] lista sin aviso" en 44 galaxias).
  Criterio: MARCAR=1 por defecto en el perfil medio solo si marcar44 supera a base44 en imagenes nuevas por segundo por
  encima del ruido, sin alargar escenas, con la misma RAM pico y verif44 limpio. Golf: no hay escena de ese nombre en el
  recorrido; arranque con cache fria: pendiente.
- u133 (resultado): marcar44 se cerro con Segmentation fault (codigo 139) al cargar FactoryGalaxy (12/44), tras 11
  galaxias; en esas 11: base44 19,4, verif44 19,2, marcar44 19,9 imagenes nuevas/s (+3 %, menos que el +6,4 % de u120).
  verif44: 36,4 millones de comparaciones, 0 listas cambiadas sin flush. RAM pico: base44 680, verif44 693, marcar44 665.
  fs0_* y juego44 (GALAXY_FRAMESKIP=0) no valen: ese modo presenta cada mini imagen (2 draws + 1 copia, ~11 por imagen
  completa) con su propio swap (~22 ms de espera); en AstroGalaxy 226 de 274 s se van en ellas y el juego va ~7 veces mas
  lento. En base44 (auto) son 31 filas. base44, media de las 44: imagen completa CPU 16,7 ms, GL 14,2, GPU 35,0 ms;
  saltada CPU 6,2 ms, gpu_copias 12,7 ms (denominador por confirmar). MARCAR no se acepta mientras no se aclare el cierre.
  u134: 12 galaxias (hasta FactoryGalaxy) base/marcar/juego (con salto del renderer en auto) x2.
- u134 (12 galaxias x2, salto del renderer en auto): ningun cierre; marcar y marcar2 cruzaron FactoryGalaxy. Imagenes
  nuevas/s: base 19,3 / base2 18,6 (ruido 3,6 %); marcar 19,6 / 19,6 (+1,6 % y +5,4 %); juego 18,4 / 18,1 con escenas mas
  largas (351/353 s frente a 311/317: el juego va mas lento). RAM pico 657-678 MiB en todas.
  Decision: salto en el bucle del juego descartado y quitado de la rama (git apply -R del parche de u131; el parche sigue en
  deliver/u131). MARCAR=1 no se acepta: la ganancia (+2 a +6 % en cuatro medidas) no sale del ruido de esta serie y queda
  sin explicar el cierre de u133; sigue como interruptor apagado.
- Desglose de GPU (sin binario nuevo: ya estaba en u134 base, copias.csv y el codigo). Los temporizadores por parte
  (kGpuScene/Copies/Xfb/Peeks/Present) no separan bien en la Mali: es de mosaicos y ejecuta los draws pendientes cuando
  una copia lee el EFB, asi que ese tiempo cae en "copias". Una copia de 640x456 sin draws antes cuesta 1,26 ms (312
  casos); con 1-20 draws antes, 7,5 (RGB565) y 9,6 ms (A8); con mas, 5,4-10,9. gpu_copias_ms de frames.csv es la
  diferencia del contador acumulado entre presentaciones (renderer_gles.cpp ~1183): cuenta resultados que llegan en ese
  intervalo, no las copias de ese frame; por eso los frames saltados muestran 10-13 ms. Con el EFB al 75 % (480 px) las
  copias van a destinos del tamano logico (640x456: compute_scaled_efb_copy_extent con escala 1, por el shader de
  conversion): parte fija que no baja con la resolucion (u117: GPU = 17,9 ms + 0,72 ms por 10 000 px).
- u135 (experimento, sin commit): GALAXY_GLES_COPIA_REDUCIDA=1 (apagado por defecto; renderer_gles.cpp,
  copy_efb_to_texture). Con el EFB dividido (3/4) y a 1x, una copia de color a textura se escribe al tamano fisico
  (640x456 -> 480x342) si ambos lados se dividen exacto. El alias del juego conserva ancho, alto y formato logicos
  (register_efb_copy); GLES lo muestrea con coordenadas normalizadas (handle.width solo lo usa el trazado de rayos de
  D3D12); la geometria de muestreo sale de los tamanos nuevos y los taps del filtro siguen a una fila logica.
  Fuera: profundidad, copy_output_efb_to_texture (la imagen mostrada), copias nativas de efectos, EFB a 2x+, XFB,
  peeks. GALAXY_GLES_COPIA_REDUCIDA_FORMATOS=4,7 limita por formato. Prueba nueva en nebula_gles_renderer_tests
  (origen, un texel fisico por texel, taps a una fila logica). nebula aarch64 ae689692ca55.
  Serie u135: calentamiento, base, reducida, base2, reducida2 (6 galaxias, GALAXY_GLES_RESOLUTION=75).
- u135 (resultado, 6 galaxias, EFB 75 %): GPU por imagen dibujada (gpu_ms por frame de origen / imagenes con 15+
  draws) base 25,8 / base2 25,2; reducida 23,7 / reducida2 23,3 (-2,0 ms, -8 %, en las 6 galaxias). Imagenes nuevas/s
  22,7 / 23,9 frente a 23,9 / 23,7 (sin cambio fuera del ruido); p95 45,3-45,6 y p99 49,4-50,5 iguales. RSS pico
  654-675 MiB en las cuatro; "copias del EFB" del renderer 2,2-2,6 -> 1,3 MiB. 0 errores de GL. Capturas: identicas bit
  a bit entre base y reducida en BattleShip, BreakDownPlanet, CannonFleet y Cocoon (en algun par); AstroGalaxy cambia
  de instante en todos los pares; BeltConveyerExGalaxy: base = base2 y reducida = reducida2 pero distintas entre si
  (7,8 % de pixeles, solo bordes): diferencia sistematica. Ahi se reducen 640x456 formato 7, 640x456 formato 4 y
  160x160 formato 7. Corregido el mensaje "[gles] copia reducida" (salia en cada ventana); nuevo
  GALAXY_GLES_COPIA_REDUCIDA_MIN_ANCHO. u136: 12 galaxias base/todas/solo4/base2/todas2/grandes (nebula 84cd0ee9898a).
- u136 (resultado, 12 galaxias, EFB 75 %): GPU por imagen dibujada (11 galaxias comunes) base 29,07 / base2 28,54
  (ruido 0,5); todas 26,57 / todas2 26,21 (-2,4 ms, -8 %); grandes (solo pantalla completa) 26,69 (-2,1); solo4 27,92
  (-0,9). Imagenes nuevas/s 19,1-20,4 en todas (sin cambio); p95 45,6-46,2, p99 62,5-64,1; RSS pico 653-669 MiB.
  Capturas: BeltConveyer de u135 era el instante (aqui base2 = todas = todas2 = solo4 = grandes, base distinta). Cada
  variante coincide bit a bit con alguna base en 9-10 de 11, salvo AstroGalaxy (el instante cambia en todos los pares)
  y DarkRoomGalaxy con "todas": todas = todas2 y distintas de las dos bases (6 %, bordes y textura del suelo), mientras
  grandes y solo4 = base2. Se dejan las copias chicas a tamano completo (MIN_ANCHO=640).
  base2 se cerro con Segmentation fault al entrar en FactoryGalaxy (12/12) con todo apagado: el cierre de u133 no era de
  MARCAR. Nuevo NEBULA_CRASH_LOG=1 (host_main.cpp, aarch64, apagado por defecto): senal, hilo, pc/lr/sp y pila con
  modulo+desplazamiento. Simbolos: sm/nebula-u137.unstripped (c5bf6a841910) y sm/build-game/libsmg_game.so (build id
  8bc1a409b5c18b97, el del paquete). u137: base/grandes/base2/grandes2, 12 galaxias, con NEBULA_CRASH_LOG=1.
- u137 (resultado, 12 galaxias): grandes 26,61 / grandes2 26,46 frente a base 28,58 / base2 28,90 ms de GPU por imagen
  (-2,2 ms, -7,6 %, ruido 0,3). Imagenes nuevas/s 19,0-20,0 en las cuatro (sin cambio); p95 45,9-46,3; p99 61,3/62,8
  base, 63,4/64,4 grandes; RSS pico 657-672. Capturas: cada captura de grandes coincide bit a bit con una sin reducir
  (de u136 o u137) salvo AstroGalaxy (el instante cambia siempre) y EggStarGalaxy (1,5 %, y en u136 grandes = base).
- Cierre de FactoryGalaxy atrapado (base2 de u137, todo apagado): SIGSEGV en la direccion 0, hilo "Nebula Game",
  pc libsmg_game.so+0x3606c4 = AudSoundObject::setCutoffToPort -> writePort (en linea) <- AudSoundObject::startSound
  <- MR::startSound <- StarPieceGroup::forceKillStarPieceAll <- SleepControlFunc::initSyncSleepController <-
  GameScene::init <- GameSystemSceneController::initializeScene <- FunctionAsyncExecutorThread::run. writePort
  comprueba isSoundAttached, llama OSDisableInterrupts (os_native.c: entrega el trabajo de interrupcion pendiente
  antes de desactivarlas, y ese trabajo puede terminar el sonido) y vuelve a leer getSound() sin comprobar.
  Arreglo (AudSoundObject.cpp, TARGET_PC): comprobar otra vez dentro de la seccion; en el codigo generado, las tres
  copias en linea (writePort, setMapCodeToPort, setCutoffToPort) comprueban despues de OSDisableInterrupts.
  libsmg_game sm/libsmg_game-u138.unstripped. u138: 6 recorridos cortos con el arreglo y 6 con el modulo viejo.
- u138 (resultado): 12 recorridos cortos alternados, cada uno entra una vez en FactoryGalaxy. Modulo viejo: viejo6 se
  cerro con la misma pila (setCutoffToPort/writePort, direccion 0); con el arreglo: 0 cierres en 6. En total el modulo
  anterior se cerro 4 veces en ~19 entradas (u133, u136, u137, u138). Commit 343de172 en optimizacion-h700 (solo
  AudSoundObject.cpp); bundle deliver/u139/optimizacion-h700.bundle (2fbe2a40..optimizacion-h700).
- Hilo de render en AstroGalaxy: ocupado ~93 % (perfil de u128); replay_cached_display_list 39,5 % inclusivo (15,8 %
  propio), find_cached_display_list 21 % (memcmp 12,9 %), run_window propio 7,6 %, load_bp 4,7 %,
  invalidate_dirty_decoded_packet_run_ranges 4,6 %, malloc/free ~4 %. MARCAR en AstroGalaxy, 5 pares (u120 x2, u133,
  u134 x2): CPU de render por imagen dibujada 28,9->25,9, 28,6->26,1, 23,6->21,4, 23,5->21,6, 23,9->21,5 ms; imagenes
  nuevas/s 15,5->16,3, 16,0->16,1, 15,6->16,0, 15,2->15,9, 15,3->15,9; escena -3,5 a -5 %. Con el cierre explicado (no era
  MARCAR) se acepta: commit 9d9f0fe6 (GALAXY_GX_DL_MARCAR=1 en el perfil medio del lanzador; 0 en el codigo).
  u139: perfil de hilos en AstroGalaxy con marcar / base / marcar2 (nebula c5bf6a841910, libsmg_game del arreglo).
- u139 (perfiles, AstroGalaxy, NEBULA_PROFILE=29): el muestreo del hilo de render cayo en la entrada a la galaxia
  (40-42 s; 10,6 MiB de texturas subidas en esa ventana), no en juego estable: no sirve para el render. El del hilo del
  juego (46-48 s, estable, marcar): 44 % esperando en wait_for_render_cpu_reads_idle (la barrera de memoria viva),
  25 % codigo del juego, 21 % FramePeEventClassifier::classify. Lineas [frameskip] en estable: 1 de 3 frames dibujado;
  ocupacion del render por frame dibujado 29,7-32,4 ms (marcar) / 32,1 (base) y por frame saltado 13,1-14,6 / 15,6: los
  dos saltados de cada ciclo son ~26 de ~56 ms del render (46 %) en frames que no se ven. Siguiente frente propuesto: el
  coste de los frames saltados en el hilo de render (replay de display lists sin draws).
- u140 (experimento, sin commit): GALAXY_FRAMESKIP_LISTAS=1 (apagado por defecto). En un frame saltado, la
  reproduccion de display lists en cache (FifoParser::replay_cached_display_list) pasa por encima de los draws en vez
  de llamar a cada tramo solo para que begin_cached_draw_run lo rechace; CP/XF/XF indexado/BP y sus eventos siguen en
  orden. FifoSink::skip_cached_draws / on_skipped_cached_draws; GxBackend las da con las mismas condiciones que el
  rechazo (salto rapido, sin trazado, interpolacion ni escenas temporales) y las de begin_cached_draw_run (sin captura
  de dependencias ni de draws). El estado GX no cambia (un tramo rechazado nunca llegaba a flush_draw_state); lo demas
  que hacia un draw (display_copy_on_output_ = false, vaciar un lote pendiente) lo hace on_skipped_cached_draws.
  Prueba nueva en nebula_fifo_display_list_tests. nebula d159a0467a2b. Serie: base/listas x2 en 12 galaxias + perfil
  de AstroGalaxy estable (NEBULA_PROFILE=36) con y sin.
- u140 (resultado): GALAXY_FRAMESKIP_LISTAS=1 peor y repetido. AstroGalaxy: escena 34,9/34,8 -> 36,0/35,9 s, imagenes
  nuevas/s 15,8/16,0 -> 15,4/15,5, CPU de render por imagen dibujada 21,7/21,6 -> 22,4/22,3 ms; 12 galaxias: 19,7/19,6
  -> 18,9/19,2 imagenes nuevas/s. El coste de los frames saltados en [frameskip] no bajo (8-13 ms con 0 draws): el
  rechazo de los tramos de draws era barato. Descartado y quitado del arbol (fifo_parser, gx_backend, prueba).
  Perfil estable (NEBULA_PROFILE=36, AstroGalaxy, MARCAR=1): render ocupado ~48 % (esperando al juego el resto);
  juego 44,5 % en wait_for_render_cpu_reads_idle, 23,8 % codigo del juego, 8,3 % memcmp de display lists + 2,7 %
  find_cached_display_list en el clasificador PE. Causa: el analizador del clasificador no avanza generaciones y la
  condicion de MARCAR pedia validated_generation != 0, asi que sus listas se comparaban siempre (y la verificacion
  =2 tampoco las miraba). GALAXY_GX_DL_MARCAR_TODAS=1 (fifo_parser.cpp, apagado por defecto): basta con `flushed`.
  u141: base/todas x2 en 12 galaxias + verif44 (MARCAR=2 + TODAS=1). nebula 2611e0cbfc2a.
- u141 (resultado): TODAS aceptado. AstroGalaxy escena 34,9/34,8 -> 31,8/32,7 s, imagenes nuevas/s 15,8/15,9 ->
  17,4/16,7; 12 galaxias 19,3/19,2 -> 19,5/19,6 y escenas 316/318 -> 307/303 s; p95 45,0 -> 43,8/44,2; p99 57,6/58,9 ->
  61,0/58,4; RSS pico 659-668. Capturas: iguales bit a bit a una base salvo AstroGalaxy y CannonFleet (instantes que ya
  variaban). verif44 (=2 + TODAS): 36,4 millones de comparaciones, 0 sin aviso; RSS pico 673, minimo libre 93 MiB.
  Commit a675844c (GALAXY_GX_DL_MARCAR_TODAS=1 en el perfil medio del lanzador). Siguiente: la espera del juego en
  wait_for_render_cpu_reads_idle (44 % del hilo del juego en AstroGalaxy estable).
- Copias reducidas aceptadas por el usuario (ahorro de GPU sin ganancia de imagenes nuevas): commit 0e7a0b71
  (GALAXY_GLES_COPIA_REDUCIDA=1 y MIN_ANCHO=640 en el perfil medio del lanzador; solo los cambios de la copia, el
  contador de ventana quitado). u142: validacion final de lo aceptado junto: estable44 (todo apagado, con el arreglo
  del cierre) / aceptado44 / ciclos (8 vueltas IceVolcanoGalaxy <-> AstroGalaxy, la de mas RSS en verif44: 673 MiB).
  nebula 2611e0cbfc2a (u141), lanzador de la rama.
- u142 (validacion final, 44 galaxias, EFB 75 %, una pasada cada una): estable44 (interruptores apagados) ->
  aceptado44: imagenes nuevas/s 18,1 -> 18,8 (15 galaxias con mas, 4 con menos), escenas 1169 -> 1119 s, GPU por imagen
  dibujada 31,95 -> 30,94 ms, CPU de render por imagen 16,45 -> 15,46 ms, p95 47,2 -> 46,5, p99 65,3 -> 65,7, intervalos
  >33,3 ms 23,6 -> 19,6 %; AstroGalaxy 15,3 -> 17,1 img/s y 36,0 -> 32,1 s. RSS pico 675 / 676, minimo libre 88 / 83 MiB.
  0 cierres. Capturas: 12 de 44 identicas; el resto, instantes distintos (el juego va mas rapido); EggStar: el destello
  del sol aparece o no tambien entre dos bases iguales (u141 base/base2). Ciclos (8 vueltas IceVolcano <-> Astro, lo
  aceptado): RSS de IceVolcano 612 la primera, luego 654-657 estable; malloc 218-219 MiB plano desde la tercera vuelta.
- GPU por imagen (aceptado44, 20 425 imagenes, EFB 75 %): GPU ~ 2,5 ms + 4,3e-5 ms por vertice + 0,29 ms por copia +
  0,072 ms por kpx de destino de copia (R2 0,73). Vertices: media 148 000 por imagen (~6,4 ms), 230 000-290 000 en las
  pesadas. GxVertexOut fijo de 144 bytes: 11,2 MiB de vertices por imagen. Los kpx de copia son un indicador de los
  efectos de pantalla completa (reducir las copias bajo 40 % los kpx y solo 1 ms la GPU). u143 (diagnostico):
  GALAXY_GLES_DIAG_RECORTE_1PX=1 (renderer_gles.cpp, apagado; la imagen sale mal) recorta cada draw del EFB a 1 px:
  queda la geometria y lo fijo. base / diag en 12 galaxias. nebula a44bae97b887.
- u143 (resultado, 12 galaxias, EFB 75 %): GPU por imagen dibujada base 27,9 ms; diag (draws a 1 px) 9,6 ms = 34 %
  (AstroGalaxy 18,0 -> 5,6; CannonFleet 35,5 -> 9,5; DarkRoom 41,6 -> 12,4; Factory 33,4 -> 11,9). Lo que queda en diag
  incluye borrados, copias, XFB y presentacion; ahi los vertices explican 1,5e-5 a 3,5e-5 ms por vertice (2,5-6 ms con
  169 000 de media). Los fragmentos son ~66 % (~18 ms). 0 draws con uber shader. Conclusion: el formato compacto de
  vertices (paso 2) ahorraria como mucho 1-3 ms; no es lo primero. Siguiente: repartir el coste de fragmentos entre
  efectos de pantalla completa y geometria de la escena.
- u144 (diagnostico 2): GALAXY_GLES_DIAG_RECORTE_1PX=2 recorta solo los draws de 12 indices o menos (efectos de
  pantalla completa, sprites, particulas, HUD); =3 solo los demas (geometria de la escena). El recorte se decide por
  GL draw (los lotes ya combinados cuentan como uno). base / efectos / escena en 12 galaxias. nebula 520866f60b64.
- u144 (resultado, 12 galaxias): GPU por imagen base 28,2; con los draws de <=12 indices a 1 px 15,5 (-12,7); con los
  de >12 indices a 1 px 16,3 (-11,9). Las dos bajadas suman 24,5 ms, mas que los 18,3 ms de todos los pixeles en u143:
  los costes no se suman (cada mitad abarata a la otra), lo que apunta a que la GPU esta limitada por el ancho de
  banda compartido (EFB, texturas y copias) mas que por una clase de draw. Reparto casi a medias en 11 de 12
  galaxias; en DarkRoomGalaxy los efectos pesan el doble (-23,8 frente a -12,8). 0 cierres.
- Texturas: la compactacion exacta ya esta activa por defecto (GALAXY_TEXTURE_COMPACT=exacto: I4/I8 -> R8, IA4/IA8 ->
  RG8, RGB565 -> RGB565; u144: 256 texturas compactadas, 11,9 MiB menos). Quedan en RGBA8 CMPR, RGB5A3 y paletas RGB5A3.
  u145 (sin cambios de codigo, nebula 520866f60b64): exacto / no / 16bits / exacto2 en 12 galaxias, para medir cuanto
  pesa el ancho de banda de las texturas en la GPU por imagen antes de decidir.
- u145 (resultado, 12 galaxias): GPU por imagen exacto 28,08 / exacto2 28,25; no (todo RGBA8) 28,06; 16bits 27,94.
  Sin diferencia fuera del ruido en ninguna galaxia (DarkRoom varia 37,9-42,7 tambien entre exacto y exacto2). El
  formato de las texturas no limita la GPU; se cierra la opcion 2 sin cambios. RSS pico 656-668. 0 cierres.
- Espera del juego (44 % en wait_for_render_cpu_reads_idle, AstroGalaxy estable): con GALAXY_RENDER_LIVE_MEMORY_WAIT=1
  el juego espera a que el render termine de leer cada trozo (cola vacia y render_cpu_reads_in_flight_ == 0). El modo
  copia ya existe (gx_backend.cpp ~6309: con render_memory_snapshot el render lee su propia copia de los rangos de
  dependencia y el juego solo espera a que haya sitio en la cola de 3); solo vale con WAIT=0 (render_config.cpp) y la
  copia es compacta por defecto. Nunca se habia medido en el H700 (u124-u126 eran memoria viva sin copia).
  u146 (sin cambios de codigo): base/copia x2 en 12 galaxias. Riesgos: memoria (copias de los rangos por trozo en
  cola), coste de copiar en el hilo del juego y, como sinclas, que el render se sature con mas frames.
- u146 (resultado): el modo copia (WAIT=0 + SNAPSHOT=1, compacta) se quedo sin memoria en las dos pasadas: el kernel
  mato el proceso (OOM, codigo 137) a los pocos segundos de entrar en AstroGalaxy (anon-rss 454 MiB, total-vm 6,5 GB;
  base: anonima ~300 MiB y minimo libre 122 MiB en la misma escena). Con ~120 MiB libres no caben las copias de los
  trozos en cola. Queda la variante GALAXY_RENDER_IMMUTABLE_RANGE_OWNERSHIP=1 (reutiliza copias de rangos que no
  cambian; requiere rangos estrictos), sin medir.
- u147 (corrida corta): base / copia_inm (WAIT=0 + SNAPSHOT=1 + GALAXY_RENDER_IMMUTABLE_RANGE_OWNERSHIP=1; la copia
  compacta ya es de rangos estrictos, que es lo que pide la reutilizacion), 3 galaxias, memoria cada 10 s.
- u147 (resultado, 3 galaxias): copia_inm cabe (minimo libre 99 MiB frente a 195 en base; +70-95 MiB) pero es mucho peor:
  AstroGalaxy 32,1 -> 58,3 s y 17,1 -> 9,8 imagenes nuevas/s; total 25,2 -> 17,3; render ocupado 49 -> 30 %. Copiar los
  rangos en el hilo del juego le cuesta mas que la espera que quita. Se cierra la via del modo copia: la barrera de
  memoria viva se queda como esta.
- Efectos a menor resolucion (pedido por el usuario). En este port el juego dibuja sus efectos en el propio EFB:
  reduce y desenfoca en una parte (su espacio de trabajo) y copia, y luego mezcla a pantalla completa. Bajar la
  resolucion solo puede ayudar en el espacio de trabajo. u148 (diagnostico): GALAXY_GLES_DIAG_RECORTE_1PX=4 recorta los
  draws de efecto (<=12 indices) con viewport de pantalla completa (2 * xf_viewport[0] >= 600), =5 los de viewport
  parcial. base / completos / parciales en 12 galaxias. nebula ca8849ae1c44.

u148 (efectos por fase, 12 galaxias, EFB 75 %): GPU por imagen dibujada, media de escenas: base 29,2 ms; completos
(DIAG_RECORTE_1PX=4, draws de efecto de 12 indices o menos con viewport de pantalla completa) 16,0 ms; parciales (=5,
mismos draws con viewport parcial: espacio de trabajo del desenfoque/resplandor) 29,4 ms. El espacio de trabajo no
cuesta nada medible; todo el coste de efectos (~13 ms) esta en las pasadas de pantalla completa, que tienen que salir a
la resolucion de la imagen. Bajar la resolucion de los efectos no puede ganar: via cerrada. Sin cierres en las tres
secciones. El codigo de diagnostico sigue sin commit y apagado por defecto.

### Ritmo parejo en las cinematicas (u150)

- Pedido del usuario. El juego avisa al host si hay cinematica (GameScene::draw3D, MR::isDemoActive, por el canal de
  contexto: NEBULA_CONTEXT_DEMO, sin cambiar la version de la API); runtime/include/galaxy/gx/cinematica.h guarda el
  estado (se pone a 0 al cambiar de escena). GALAXY_CINEMATICA_RITMO=1 (apagado por defecto), solo dentro de una
  cinematica: el salto automatico no baja (solo sube), la repeticion de un frame saltado no se presenta y cada imagen
  nueva espera en el hilo del swap hasta k vblanks despues de la anterior (cinematica.cpp: k 1-6 segun lo que tarda,
  sube tras 2 tardias seguidas, baja tras 20 tempranas). frames.csv: columna "cinematica".
- Hallazgo: el lanzador no fija GALAXY_PE_PRECLASSIFY, GALAXY_GX_FIFO_STREAM_KB ni GALAXY_RENDER_LIVE_MEMORY(_WAIT); el
  host los pone a 0 / 0 / 1 / 0 (host_main.cpp). Las series u130-u148 midieron "base" con 1 / 32 / 0 / 1. u150 mide con
  los del lanzador (el juego normal). nebula 9056fba88ebd, libsmg_game.so f6ce39640c92.
- u150 (12 galaxias, ajustes del lanzador, base/ritmo x2): el recorrido no pasa por ninguna cinematica (columna
  cinematica siempre 0, ningun "context: cinematica" en el log). Fuera de ellas el ritmo no cambia nada: nuevas/s
  15,0 / 15,0 / 15,1 / 15,1, mostradas 46,8 / 46,6 / 47,0 / 46,6, recorrido 217 / 218 / 216 / 217 s. Sin cierres.
  Con los ajustes del lanzador: 15 nuevas/s y 47 mostradas en esas 12 galaxias; u148 "base" (preclasificar + espera):
  20,8 y 37,8. u151: paquete jugable con NEBULA_ULTRA=1 y dos lanzadores (con y sin ritmo) para medir el prologo de
  una partida nueva.
- u151, prologo de partida nueva con el lanzador SIN ritmo (el usuario subio el mismo zip y log dos veces). Tramos
  (nuevas/s; hueco entre imagenes p50/p90/p99): cinematica del jardin (demo) 10,9 (83/120/310 ms, GPU 45 ms/img);
  jardin jugable 9,7 (GPU 70 ms/img, la mitad copias 160x114); PELICULA 1 (THP, 110 s) 25,4 (33/47/128 ms);
  juego en el castillo 8,2 (GPU 67 ms/img); PELICULA 2 (138 s) 25,2 (33/47/133 ms); cinematica de la Puerta Celestial
  16,3. El ataque al castillo es video THP: el juego decodifica ~50 cuadros/s (log: movie frames, 250-430 ms/s de CPU)
  pero el salto automatico dibuja 1 de cada 2 (dibujar un cuadro de video cuesta ~7 ms de GPU con la subida): se tira
  la mitad de lo decodificado y lo que queda sale irregular. El ritmo de cinematicas no actua ahi (MR::isDemoActive no
  se reporta durante una pelicula: GameScene::draw3D vuelve antes).
- u152: GALAXY_VIDEO_SIN_SALTO (apagado por defecto). El host marca cada cuadro de pelicula decodificado
  (svc_thp_video_decode -> cinematica::video_cuadro); mientras haya uno en los ultimos 250 ms: =1 el salto de cuadros
  queda en 0 (se dibuja cada cuadro decodificado), =2 ademas ritmo parejo (cinematica::esperar_turno). Paquete jugable
  con NEBULA_ULTRA=1 y lanzadores "(video sin salto)" (=1) y "(video ritmo)" (=2), ambos con GALAXY_CINEMATICA_RITMO=1.
  nebula 44dd423f0d38.
- El usuario: lo que falla en el ataque al castillo es el AUDIO que se entrecorta. Hipotesis por los datos de u151: el
  video 1 tuvo 5591 cuadros decodificados en ~111 s (50/s); si el THP es de ~60 cuadros/s, el juego lo lee ~17 % mas
  lento que el tiempo real y el audio del THP (que sale de los mismos cuadros, por la mezcla de JASDriver) se queda
  sin datos. u153: el juego anota en el log los datos de cada video (THPSimplePlayerWrapper: "THP: video WxH, fps,
  cuadros" y "THP: audio Hz") y cada ~5 s cuantas mezclas salieron sin datos ("THP: audio sin datos en X de Y
  mezclas"). GALAXY_VIDEO_SIN_SALTO=3 (nuevo): salto como siempre, sus repeticiones sin presentar (sin swap). Lanzadores:
  normal, "(video sin salto)" (=1), "(video sin repetir)" (=3). nebula c79f14371497, libsmg_game.so c2b96e076518.
- Causa del audio, por el codigo: MoviePlayerSimple::exePlaying decodifica un cuadro del THP por cada cuadro de juego
  (o cada 2 si el video es de 29,97): el video avanza al ritmo del juego, no del reloj, y su audio se mezcla al ritmo
  del reloj. Con el juego a ~50 cuadros/s el audio se queda sin datos. Durante el video el hilo del juego no va lleno
  (~48 %): lo frena la cadena hasta el swap con vsync (render: esperas de swap ~780 ms/s). u154: GALAXY_VIDEO_SIN_SALTO=3
  puesto en el paquete (smg/ultra/ultra.cfg, el repo sigue apagado) y, ademas, el juego anota "THP: cuadro sin leer a
  tiempo" (exePlaying devuelve 2: el disco no llego). libsmg_game.so de u154 con ese contador.
- u154 (GALAXY_VIDEO_SIN_SALTO=3) no cambia nada: los videos son de 59,94 cuadros/s (log: "THP: video 640x368, 59.94
  fps, 5591 frames", audio 32 kHz estereo); el video 1 duro 159,7-270,5 s = 50,5 cuadros/s igual que antes, y el audio
  se quedo sin datos en 30-55 de cada 285 mezclas (~16 %, justo lo que falta de 50,5 a 59,94). Ningun "cuadro sin leer
  a tiempo": el disco no es. u155: NEBULA_VIDEO_AUDIO_RELOJ=1 (apagado por defecto en el repo, puesto en el paquete):
  MoviePlayerSimple::exePlaying decodifica hasta 2 cuadros mas en un cuadro de juego mientras haya menos de 3 buffers
  de audio en cola (THPSimplePlayerWrapper::queuedAudioBuffers). libsmg_game.so de u155 con eso.
- u155 (NEBULA_VIDEO_AUDIO_RELOJ=1, cola minima 3, hasta 2 extra): video 1 en 98,1 s (antes 110,8; real 93,3), video 2
  en 123,2 s (real 118,1); audio sin datos en ~4 % de las mezclas (antes ~16 %); ~2100 cuadros extra en los dos videos;
  sin cierres. u156: cola minima 5 (~80 ms) y hasta 3 extra por cuadro de juego. libsmg_game.so de u156.
