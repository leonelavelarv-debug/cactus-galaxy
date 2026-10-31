# GX-ES paso 2: resultados

La mezcla de doble fuente de GX está corregida con framebuffer fetch. Está verificada en la PC y **en la RG35XX H con
KNULLI (OpenGL ES 3.2)**: la Mali acepta `GL_ARM_shader_framebuffer_fetch` y su resultado coincide con la mezcla de GX
calculada (0 píxeles con error > 1). Faltan la ruta de 3.0 en la consola (no llegó a correr por memoria) y muOS. Abajo
están los resultados y lo que falta, por firmware.

## Criterios de finalización

| # | Criterio | Estado |
|---|---|---|
| 1 | Reproducir el problema | Hecho: hasta 199 niveles de error frente a la mezcla de GX en la escena exacta; 107 186 píxeles con error > 1 (GX_ES_VISUAL_REGRESSION.md) |
| 2 | Causa exacta | Hecho. La salida 1 del shader (alfa del TEV) no llega al blender sin `GL_EXT_blend_func_extended`, y `GL_SRC_ALPHA` lee la salida 0. En 35 pipelines esa salida lleva la constante de `GXSetDstAlpha`; en 206, la alfa a 6 bits (GX_ES_BLEND_ANALYSIS.md). Corrige la auditoría, que decía "alfa constante" para los 241 |
| 3 | Solución verificable | Hecho: la mezcla en el shader con framebuffer fetch, activable y desactivable (GX_ES_FRAMEBUFFER_FETCH.md) |
| 4 | Los afectados pasan | En la PC (EXT): 0 píxeles con error > 1 frente a la mezcla calculada; los 241 pipelines reales ≤ 1 frente a la referencia SRC1. **En la Mali (ARM, KNULLI, 3.2): 0 píxeles con error > 1 (máx. 1)**; frente a la referencia de la PC, dentro de lo que ya difieren los controles (RGB 2, alfa 4-5) |
| 5 | Los no afectados siguen igual | En la PC y en la Mali (KNULLI, 3.2): imágenes idénticas byte a byte (controles sintéticos y 256 pipelines reales). Las pruebas que ya había siguen pasando |
| 6 | Comparaciones documentadas | GX_ES_VISUAL_REGRESSION.md, hojas en `docs/gx_es_blend/` |
| 7 | Ruta sin framebuffer fetch | La de antes (blending fijo), automática si el driver no lo tiene o con `GALAXY_GLES_BLEND_FETCH=0`; su error está medido |
| 8 | Prueba en la RG35XX H | **KNULLI, 3.2: pasa.** Por píxel (`pruebamezcla.zip`, con y sin hilo de GL) y en el juego (forma ARM aceptada, 0 errores de GL, sin costo medible en la serie de ULTRA). **Falta:** la ruta de 3.0 (muere por memoria, también sin la corrección; corregido en la PC en GX_ES_ES30_MEMORY.md) y muOS |

No se declara compatibilidad completa. Está verificado: EXT en Mesa (x86-64) y ARM en la Mali con KNULLI, en OpenGL ES 3.2.
Falta muOS y la ruta de 3.0 en la consola.

## Archivos modificados

| Archivo | Líneas | Qué |
|---|---|---|
| `runtime/src/gx/gles/shader_library_gles.cpp` | +121 | `fetch_blend_glsl`: la transformación del GLSL y las fórmulas de GX |
| `runtime/include/galaxy/gx/gles/shader_library_gles.h` | +13 | `FramebufferFetch`, la declaración |
| `runtime/src/gx/gles/pipeline_cache_gles.cpp` | +66 −4 | `GALAXY_GLES_BLEND_FETCH`, `PairKey::blend`, transformar al enlazar, binario propio, vuelta al blending fijo si falla, blend apagado en el pipeline, log |
| `runtime/include/galaxy/gx/gles/pipeline_cache_gles.h` | +18 | Lo anterior |
| `runtime/src/gx/gles/renderer_gles.cpp`, `renderer_gles.h` | +18 −1 | Contadores de draws de doble fuente; acceso al EFB para pruebas |
| `platform/knulli/game/gles_blend_tests.cpp` | nuevo, ~860 | Las pruebas de imagen (A/B/C, mezcla calculada, métricas, diferencias; tolerancia entre GPUs) |
| `platform/knulli/game/CMakeLists.txt` | +12 | Ese objetivo (con `$ORIGIN`: también corre en la consola) |
| `platform/knulli/prueba-mezcla/` | nuevo | El paquete de prueba para la consola: lanzador, `port.json`, `LEEME.txt`, `empaquetar-prueba.sh` |
| `platform/knulli/smg/Super Mario Galaxy.sh` | +3 | Documenta `GALAXY_GLES_BLEND_FETCH` |
| `docs/GX_ES_*.md` | | Estos cuatro documentos y las correcciones a la auditoría (R-01) |

**Sin tocar:**

- FIFO, CP, XF y BP; `shader_gen.cpp` y las claves; la biblioteca GLSL y `nebula_glsl_aot` (no hacen falta formas
  nuevas).
- La recompilación del juego.
- Las optimizaciones que el paso prohíbe: draws, hilos, resolución, anillos, memoria, frecuencias.

## Pruebas ejecutadas (PC, x86-64, Mesa 25.2.8 llvmpipe)

| Prueba | Resultado |
|---|---|
| `nebula_gles_blend_tests` referencia / plain / fetch y `--comparar` | Todo cumple (GX_ES_VISUAL_REGRESSION.md) |
| Lo mismo con `GALAXY_GLES_FORCE_ES30=1` | Cumple, con las mismas métricas |
| Lo mismo con `GALAXY_GLES_THREAD=1` | Cumple, con las mismas métricas |
| Lo mismo sin traductor, con la biblioteca GLSL (como la consola) | Cumple |
| Lanzador de la consola simulado en la PC (control.txt falso, binario x86 sin traductor): las rutas es32, es32-hilo y es30 | Estado 0 en todo; `log.txt`, `gl.txt`, `metricas.txt` y `dif/` escritos |
| `nebula_gles_renderer_tests` (3.2, Mesa 3.0, `FORCE_ES30` con hilo) | 0 fallos; el log dice `blended in the shader (GL_EXT_shader_framebuffer_fetch)` |
| `nebula_gles_shader_tests`, `nebula_texture_compact_tests` | 0 fallos |
| `armar.sh` completo para aarch64 | Compila. `nebula` lleva la ruta nueva, sin rutas ni datos de la PC; `smg.zip` 12.2 MB |

## Resultados en la RG35XX H

### KNULLI

**Primera corrida del juego (31-oct, log.txt de la consola, sin ULTRA: ~293 s, pantalla de título, selección de
partida y Castillo de Peach):**

- **El driver de la Mali acepta la forma ARM:** `[gles-blend] dual-source alpha: blended in the shader
  (GL_ARM_shader_framebuffer_fetch)`.
- **Ningún programa volvió al blending fijo:** ninguna línea `stays on fixed-function blending`.
- **El 99.1 % de los draws de doble fuente** (20.3 por frame de media en 32 ventanas de 600 frames) se mezclaron en el
  shader. El resto son draws con el uber: un programa que no estaba en la biblioteca, o mientras se enlazaban.
- **0 errores de GL** y sin cierres.
- **Picos de los anillos por frame:** vértices 17 464 KiB, índices 552, constantes 3 892, matrices 656.
  - El de vértices es mucho mayor que los ~4 MiB de los menús; los 28 MiB por frame no sobran tanto como parecía en la
    auditoría.
  - No se cambian en este paso.

**`pruebamezcla.zip` (31-oct; MemTotal 973 MiB, MemAvailable 759 MiB al empezar):**

| Ruta | B (antes) contra la mezcla calculada | C (fetch) contra la mezcla calculada | B vs C en los controles | Estado |
|---|---|---|---|---|
| es32 | 102 610 píxeles con error > 1, máx. 199 | **0 con error > 1, máx. 1** | idénticas | 0 y 0 |
| es32-hilo | igual | **igual** | idénticas | 0 y 0 |
| es30 | — | — | — | **137 (`Killed`) en B y en C** |

- **Frente a la referencia de la PC**, C difiere en RGB 2 y alfa 4-5. Los controles, que este cambio no toca, difieren
  igual: es la diferencia entre la Mali y Mesa (GX_ES_VISUAL_REGRESSION.md).
  - `--comparar` marcó FAIL las seis escenas porque usaba tolerancia 1 también entre GPUs.
  - Ahora: con otra GPU, RGB ≤ 2 y alfa ≤ 5. Con esos datos pasan las seis.
- **es30 murió después de cargar la caché de programas**, igual con y sin la corrección. El log no dice en qué paso:
  la salida de la prueba iba en búfer y se perdió con el proceso (ahora va línea por línea).
  - En 3.0 (sin `GL_EXT_buffer_storage`) cada anillo tiene una copia en la CPU, puesta a cero completa, además del
    buffer de GL. En la prueba son 192 MiB de anillos, así que la memoria se duplica.
  - Lo más probable es que el kernel lo matara por falta de memoria. No lo confirma ningún log.
  - **No se cambia en este paso**: son anillos y memoria. Va en un paso aparte (GX_ES_ES30_MEMORY.md).
  - La Mali tiene 3.2, así que esta ruta no se usa en esta consola salvo con `GALAXY_GLES_FORCE_ES30=1`.
  - La primera `--comparar` dio estado 0 sin imágenes. Ahora es FAIL.

**Serie de ULTRA (31-oct, 6 galaxias + selección, paquete de medición):**

| Recorrido | FPS mostrados | FPS dibujados | p95 (ms) | GPU (ms) | draws/frame | draws uber | Duración |
|---|---|---|---|---|---|---|---|
| calentamiento | 19.93 | 8.42 | 158.6 | 41.2 | 182.1 | 18.7 % | 342 s |
| fetch | 20.11 | 8.19 | 158.1 | 41.1 | 182.7 | 17.9 % | 322 s |
| antes (`BLEND_FETCH=0`) | 20.68 | 8.51 | 144.9 | 41.8 | 162.1 | 20.0 % | **606 s** |
| es30 | — | — | — | — | — | — | sin `resumen.json` |
| fetch2 | 19.70 | 8.26 | 160.5 | 40.4 | 181.5 | 18.0 % | 322 s |

- **Costo de la corrección: no medible.** fetch, fetch2 y el calentamiento (todos con fetch) varían entre sí más que
  cualquier diferencia de GPU.
- **"antes" salió raro:**
  - El recorrido tardó 606 s frente a 322 s, con 78 % más frames > 25 ms.
  - Al mismo tiempo dio 11 % menos draws por frame y p95 mejor.
  - Ninguna de las dos cosas sale del blending. Puede que se haya quedado en otro punto o atascado en una galaxia.
  - Hace falta su `informe.txt` para saberlo. No cuenta como medición de la ruta de antes.
- **es30 no terminó:** probablemente lo mismo que en `pruebamezcla`.
- **18 % de los draws van al uber** en todos los recorridos.
  - Son programas que la biblioteca GLSL no tiene: este paquete no trae traductor, y la biblioteca se armó solo con las
    claves semilla.
  - Los draws de doble fuente que caen en el uber siguen con el blending fijo, es decir, con el error de antes.
  - Se arregla rearmando la biblioteca con las claves que el juego guardó en la consola
    (`smg/data/shadercache/RMGE01/gles_keys.bin`). No es parte de este paso.
- **Capturas:** entre recorridos con los mismos ajustes, el PSNR va de 22 dB a inf. No sirven para comparar por píxel,
  solo a ojo.
- **Pantalla negra al final:** es el cambio entre recorridos de la serie. Ahí era donde moría es30.

**Segunda serie de ULTRA (31-oct, la biblioteca GLSL rehecha con las claves de la consola, 1133 programas):**

| Recorrido | FPS mostrados | FPS dibujados | p95 (ms) | GPU (ms) | draws/frame | draws uber | Duración |
|---|---|---|---|---|---|---|---|
| calentamiento | 33.31 | 13.08 | 52.8 | 8.25 | 186.2 | 0.00 % | 202 s |
| fetch | 34.63 | 13.87 | 49.0 | 8.33 | 182.4 | 0.04 % | 181 s |
| antes (`BLEND_FETCH=0`) | 34.80 | 13.35 | 48.2 | 8.12 | 183.7 | 0.03 % | 182 s |
| es30 | — | — | — | — | — | — | 22 s, sin galaxias |
| fetch2 | 34.66 | 14.13 | 48.9 | 8.33 | 182.8 | 0.00 % | 182 s |

- **El uber era lo que más costaba:** con los programas en la biblioteca, el uber baja del 18 % a 0, la GPU de 41 ms a 8 ms
  por frame y los FPS mostrados de 20 a 34.6.
  - Ahora todos los draws de doble fuente se mezclan en el shader.
  - No es una optimización de este paso: son los mismos shaders. La biblioteca solo tenía menos.
- **Costo de la corrección: no medible.** fetch y fetch2 dan GPU 8.33 ms, "antes" 8.12 y el calentamiento 8.25, dentro del
  ruido. FPS dibujados: 13.87 y 14.13 con fetch, 13.35 antes.
- **"antes" ya no sale raro:** 182 s, como fetch. Lo de la primera serie (606 s) no se repite. Su `informe.txt` no muestra
  nada fuera de lo normal: 0 errores de GL, 0 paradas de más de 250 ms, mínimo libre 138 MiB.
- **es30 volvió a morir** a los 22 s. Es lo que corrige el paso aparte de la memoria de 3.0 (GX_ES_ES30_MEMORY.md).

### muOS

**Sin medir.** Es la misma prueba, en una corrida aparte. El lanzador guarda cada firmware en su carpeta
(`resultados/<firmware>-<fecha>/`); un resultado de KNULLI no vale para muOS.

## Qué hacer en la consola (cada firmware por separado)

### 1. `pruebamezcla.zip`

Copiar "Prueba mezcla GX.sh" y `pruebamezcla/` a `ports` y ejecutarlo. Hace, sin intervención:

| Lo que pide el paso | Dónde queda |
|---|---|
| GL_VERSION, GL_RENDERER, GLSL | `es32/plain/gl.txt` (y en cada ruta) |
| GL_EXTENSIONS completas | `gl.txt`: lista con la cuenta |
| Confirmar `GL_ARM_shader_framebuffer_fetch` | `gl.txt`: "framebuffer fetch ARM"; `log.txt`: `[gles-blend] … (GL_ARM_shader_framebuffer_fetch)` |
| La ruta de 3.0 con `GALAXY_GLES_FORCE_ES30=1` | `es30/`: "contexto OpenGL ES 3.0 (GALAXY_GLES_FORCE_ES30), GLSL ES 300, paleta 2D, buffers copiados" y sus resultados |
| Memoria libre | `log.txt`: MemAvailable antes y después; `gl.txt`: VmHWM (pico de la prueba) y MemAvailable |
| Escenas reproducibles | `*/plain/*.png`, `*/fetch/*.png`, `dif/`, `metricas.txt` |
| La corrección en la Mali | `es32/fetch/gl.txt`: "exactas-galaxy: … 0 con error > 1"; `metricas.txt`: fetch frente a la referencia de la PC dentro de RGB 2 / alfa 5 (otra GPU) |

La prueba **pasa en la consola** si se cumplen las tres:

- `estado … fetch: 0` en las tres rutas;
- `comparacion …: 0`;
- `es32/plain` muestra el error grande y `es32/fetch` no.

La referencia A de la consola son las imágenes hechas en la PC: la Mali no tiene `GL_EXT_blend_func_extended`. Entre
GPUs distintas la tolerancia es RGB ≤ 2 y alfa ≤ 5: lo que ya difieren los controles. La prueba exacta es la de C contra
la mezcla calculada en `gl.txt`.

### 2. El juego

El `smg.zip` normal va limpio, sin ULTRA (`limpiar.py`). Para medir hace falta el **paquete de medición**: el mismo
port con ULTRA-GLES. Se arma con `NEBULA_MEDICION=1 armar.sh …` y queda en `<salida>/paquete-medicion/`. Su
`smg/ultra/serie.cfg` ya trae la serie de este paso; con `NEBULA_ULTRA=serie` en `smg/ultra.cfg` corre sola:

| Recorrido | Ajustes | Qué mide |
|---|---|---|
| calentamiento | — | Deja las cachés de programas hechas; no cuenta |
| fetch | (ninguno: fetch por defecto) | Estabilidad (0 errores de GL, sin cierres), `dual-source draws (… blended in the shader)` en log.txt, FPS |
| antes | `GALAXY_GLES_BLEND_FETCH=0` | La ruta de antes, para comparar FPS y capturas |
| es30 | `GALAXY_GLES_FORCE_ES30=1` | La ruta de 3.0 con el juego |
| fetch2 | (ninguno) | Repetición de fetch: el ruido de la medición |

Al final, `smg/ultra/<fecha>-serie/comparacion.txt` compara todo con el primero. Con las 44 galaxias son 5 recorridos de
15-20 minutos: entre una hora y cuarto y una hora y tres cuartos; `NEBULA_ULTRA_GALAXIAS=10` la acorta.

- **Anillos de subida:** `rings peak KiB v=… i=… c=… m=…` en las líneas `[gles] … frames/s` de log.txt. Se miden aquí;
  no se cambian en este paso.
- **Memoria:** la de ULTRA (`informe.txt`: memoria media, pico, mínimo libre).
- **Capturas:** `capturas/<galaxia>.ppm` de ULTRA, para verlas a ojo entre fetch y antes. No sirven para métricas
  por píxel (el juego no está en el mismo instante: 23-28 dB entre repeticiones).

## Limitaciones

- **La ruta ARM está probada solo en el driver r20p0 de la Mali-G31, con KNULLI.** Ni Mesa ni el validador de glslang
  conocen `GL_ARM_shader_framebuffer_fetch`.
  - Si otro driver (el de muOS) rechaza un programa, ese pipeline vuelve solo al blending fijo y el log lo dice
    (`a dual-source program stays on fixed-function blending`).
  - El juego no se rompe, pero esos draws siguen con el error de antes.
- **El uber shader** sigue con blending fijo en sus variantes de doble fuente. Solo afecta a lo que la biblioteca no
  tiene: 0.0 % de los draws en la base de ULTRA en la PC, **18 % en la consola** con la biblioteca de las claves semilla.
- **Costo:** la lectura del framebuffer y la mezcla en el shader cuestan algo de GPU en esos draws (en la Mali el EFB
  está en la memoria del tile). En la serie de ULTRA de KNULLI no se distingue del ruido (GPU 41.1 ms con fetch, 40.4-41.2
  en las repeticiones).
- **La comparación visual usa escenas sintéticas** con los shaders y estados reales, no frames del juego. Agua,
  partículas, sombras e iluminación del juego solo se verán en las capturas de la consola.
- **Los binarios de programa de los 241 pipelines son nuevos:** la primera vez que se usan se enlazan (uber mientras
  tanto). Los demás binarios se conservan.

## Problemas pendientes

1. **muOS:** correr `pruebamezcla.zip` y la serie de ULTRA por separado.
2. **Explicado** (GX_ES_MEMORY_GROWTH.md): los recorridos con fetch se cerraban al entrar a la 7.ª galaxia
   (CosmosGardenGalaxy, `UploadRing 'vertex' exhausted`), y la serie no tenía límite de galaxias. "antes" a veces no se
   cerraba ahí y seguía hasta que el sistema lo mataba por memoria: 606 s y 342 s. Las cifras de este documento son de
   las 6 galaxias que todos recorrieron. El cierre está corregido en ese paso aparte.
3. **La ruta de 3.0 muere por memoria** en la consola, con y sin la corrección. Se corrige en un paso aparte
   (GX_ES_ES30_MEMORY.md): pasa en la PC y falta en la consola.
4. ~~El 18 % de draws en el uber~~: con la biblioteca rehecha con el `gles_keys.bin` de KNULLI
   (`platform/knulli/smg/claves-consola.bin`, que `armar.sh` incluye solo; 892 → 1133 programas GX) es 0 %.
5. Si un driver rechaza la forma ARM: corregir la transformación con el mensaje del log.
6. Si el costo en GPU importa: medirlo. **No optimizar en este paso.**
7. Una alternativa sin extensiones para otros drivers: en 202 de los 241 pipelines (alfa del EFB sin escribir), sacar la
   alfa del TEV por la salida 0 sería exacto. No implementada.
8. Lo demás del registro de riesgos (R-02 en adelante) queda para los pasos siguientes.
