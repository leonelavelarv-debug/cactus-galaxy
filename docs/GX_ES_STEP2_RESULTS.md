# GX-ES paso 2: resultados

La mezcla de doble fuente de GX está corregida con framebuffer fetch y verificada **en la PC**. **No está probada en la
RG35XX H**: la ruta `GL_ARM_shader_framebuffer_fetch`, la que usará la Mali, solo puede compilarse y medirse en la
consola. Abajo están el paquete y la lista de pruebas que faltan, por firmware.

## Criterios de finalización

| # | Criterio | Estado |
|---|---|---|
| 1 | Reproducir el problema | Hecho: hasta 199 niveles de error frente a la mezcla de GX en la escena exacta; 107 186 píxeles con error > 1 (GX_ES_VISUAL_REGRESSION.md) |
| 2 | Causa exacta | Hecho. La salida 1 del shader (alfa del TEV) no llega al blender sin `GL_EXT_blend_func_extended`, y `GL_SRC_ALPHA` lee la salida 0. En 35 pipelines esa salida lleva la constante de `GXSetDstAlpha`; en 206, la alfa a 6 bits (GX_ES_BLEND_ANALYSIS.md). Corrige la auditoría, que decía "alfa constante" para los 241 |
| 3 | Solución verificable | Hecho: la mezcla en el shader con framebuffer fetch, activable y desactivable (GX_ES_FRAMEBUFFER_FETCH.md) |
| 4 | Los afectados pasan | En la PC (EXT): 0 píxeles con error > 1 frente a la mezcla calculada; los 241 pipelines reales ≤ 1 frente a la referencia SRC1. **En la Mali (ARM): pendiente** |
| 5 | Los no afectados siguen igual | En la PC: imágenes idénticas byte a byte (controles sintéticos y 256 pipelines reales). Las pruebas que ya había siguen pasando |
| 6 | Comparaciones documentadas | GX_ES_VISUAL_REGRESSION.md, hojas en `docs/gx_es_blend/` |
| 7 | Ruta sin framebuffer fetch | La de antes (blending fijo), automática si el driver no lo tiene o con `GALAXY_GLES_BLEND_FETCH=0`; su error está medido |
| 8 | Prueba en la RG35XX H | **Parcial (KNULLI):** el juego corre con la forma ARM aceptada por el driver, 99.1 % de los draws de doble fuente mezclados en el shader, 0 errores de GL. Falta la comparación por píxel (`pruebamezcla.zip`), la serie de ULTRA y muOS |

No se declara compatibilidad completa: lo verificado es la variante EXT en Mesa, en x86-64.

## Archivos modificados

| Archivo | Líneas | Qué |
|---|---|---|
| `runtime/src/gx/gles/shader_library_gles.cpp` | +121 | `fetch_blend_glsl`: la transformación del GLSL y las fórmulas de GX |
| `runtime/include/galaxy/gx/gles/shader_library_gles.h` | +13 | `FramebufferFetch`, la declaración |
| `runtime/src/gx/gles/pipeline_cache_gles.cpp` | +66 −4 | `GALAXY_GLES_BLEND_FETCH`, `PairKey::blend`, transformar al enlazar, binario propio, vuelta al blending fijo si falla, blend apagado en el pipeline, log |
| `runtime/include/galaxy/gx/gles/pipeline_cache_gles.h` | +18 | Lo anterior |
| `runtime/src/gx/gles/renderer_gles.cpp`, `renderer_gles.h` | +18 −1 | Contadores de draws de doble fuente; acceso al EFB para pruebas |
| `platform/knulli/game/gles_blend_tests.cpp` | nuevo, 825 | Las pruebas de imagen (A/B/C, mezcla calculada, métricas, diferencias) |
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

**Falta:** `pruebamezcla.zip` (la comparación por píxel en la Mali), la serie de ULTRA (FPS con y sin la corrección) y
la ruta de 3.0. ULTRA no arrancó en esa corrida: ningún archivo de ajustes tenía `NEBULA_ULTRA`. El paquete de
medición ahora lo trae puesto (`smg/ultra/ultra.cfg`).

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
| La corrección en la Mali | `es32/fetch/gl.txt`: "exactas-galaxy: … 0 con error > 1"; `metricas.txt`: fetch frente a la referencia de la PC ≤ 1 |

La prueba **pasa en la consola** si se cumplen las tres:

- `estado … fetch: 0` en las tres rutas;
- `comparacion …: 0`;
- `es32/plain` muestra el error grande y `es32/fetch` no.

La referencia A de la consola son las imágenes hechas en la PC: la Mali no tiene `GL_EXT_blend_func_extended`. Los
pipelines sin doble fuente de la Mali también se comparan con la PC, con tolerancia 1 (redondeo de cada GPU).

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

- **La ruta ARM no está compilada ni probada en ningún driver.** Ni Mesa ni el validador de glslang conocen
  `GL_ARM_shader_framebuffer_fetch`.
  - Si el driver de la Mali rechaza un programa, ese pipeline vuelve solo al blending fijo y el log lo dice
    (`a dual-source program stays on fixed-function blending`).
  - El juego no se rompe, pero esos draws siguen con el error de antes.
- **El uber shader** sigue con blending fijo en sus variantes de doble fuente. Solo afecta a lo que la biblioteca no
  tiene (0.0 % de los draws en la base de ULTRA).
- **Costo:** la lectura del framebuffer y la mezcla en el shader cuestan algo de GPU en esos draws (en la Mali el EFB
  está en la memoria del tile). No está medido; lo dirá la vuelta 1 frente a la 2.
- **La comparación visual usa escenas sintéticas** con los shaders y estados reales, no frames del juego. Agua,
  partículas, sombras e iluminación del juego solo se verán en las capturas de la consola.
- **Los binarios de programa de los 241 pipelines son nuevos:** la primera vez que se usan se enlazan (uber mientras
  tanto). Los demás binarios se conservan.

## Problemas pendientes

1. Correr `pruebamezcla.zip` y la serie de ULTRA (paquete de medición) en KNULLI. Luego, por separado, en muOS.
2. Si el driver rechaza la forma ARM: corregir la transformación con el mensaje del log.
3. Si el costo en GPU importa: medirlo. **No optimizar en este paso.**
4. Una alternativa sin extensiones para otros drivers: en 202 de los 241 pipelines (alfa del EFB sin escribir), sacar la
   alfa del TEV por la salida 0 sería exacto. No implementada.
5. Lo demás del registro de riesgos (R-02 en adelante) queda para los pasos siguientes.
