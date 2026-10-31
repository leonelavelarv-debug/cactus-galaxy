# GX-ES paso 2: comparación visual y pruebas automáticas

Fases 4 y 5 del paso 2. La herramienta es `nebula_gles_blend_tests` (`platform/knulli/game/gles_blend_tests.cpp`).
Dibuja escenas deterministas con el renderer GLES real (`RendererGles` + `PipelineCacheGles`), lee el EFB entero y lo
compara.

## Las tres renderizaciones

| | Modo | Qué es |
|---|---|---|
| **A** | `--modo referencia` | Referencia: los pipelines de doble fuente mezclan con `SRC1_ALPHA` (`GL_EXT_blend_func_extended`), como D3D12 y Vulkan. Necesita un driver con la extensión: Mesa sí, la Mali no |
| **B** | `--modo plain` | El backend GLES de antes del paso 2 (`GALAXY_GLES_BLEND_FETCH=0`) |
| **C** | `--modo fetch` | La corrección: la mezcla en el shader con framebuffer fetch |

Además, en las escenas exactas cada píxel se compara con la mezcla de GX **calculada en la CPU** (`gx_blend` en la
prueba, con las fórmulas de GX_ES_BLEND_ANALYSIS.md). Así la referencia A también queda comprobada, y la prueba no se
reduce a "las imágenes se parecen".

`--comparar <carpeta>` compara A, B y C dos a dos. Escribe `metricas.txt` (por escena: píxeles, % distintos, error medio y
máximo por canal) y `dif/` (imágenes de diferencias, ×32: rojo el color, azul la alfa, verde oscuro igual).

## Escenas

Todas miden 640x528 (el EFB a escala 1) y se reproducen igual en cada corrida: vértices, constantes y claves fijos.

| Escena | Qué tiene | Cubre del pedido |
|---|---|---|
| `exactas-galaxy` | Baldosas planas: 4 colores × 11 alfas (0, 2, 37, 64, 100, 127, 128, 170, 200, 254, 255) sobre un fondo con degradado de color y de alfa del EFB. Usa los 5 pares de factores de doble fuente de Galaxy, con la alfa del EFB apagada y encendida, y con `GXSetDstAlpha` | Combinaciones de blending, `GXSetDstAlpha`, alfa del TEV, transparencias |
| `exactas-controles` | Lo mismo con blends sin doble fuente: RGB8 con SRCALPHA, ONE/ONE, DSTALPHA/INVDSTALPHA, DSTCLR/ZERO | Pipelines no afectados |
| `gradientes` | Las mismas mezclas con alfa de 0 a 255 a lo ancho y color a lo alto | Objetos con alfa variable |
| `superpuestas` | Cuatro cuadros que se tapan, de mezclas distintas, uno con `GXSetDstAlpha` | Escenas con múltiples transparencias |
| `claves-reales` | Los **241 pipelines de doble fuente reales** de la vuelta (sus vertex y pixel shaders y su blend), uno por baldosa, con colores de vértice, konst y registros puestos a mano | Pipelines afectados |
| `controles-reales` | 256 pipelines reales sin doble fuente (con blending primero), igual | Pipelines no afectados |

**Lo que no cubre:** agua, partículas, iluminación, sombras y superposición de texturas *del juego*. Eso necesita
frames del juego, y en la PC no los hay: los datos del juego solo están en tu consola (GX_ES_BLEND_ANALYSIS.md,
sección 5).

- **Qué sí cubre:** los blends que esos efectos usan en Galaxy son los de `claves-reales`, que salen de la vuelta entera.
- **Qué no:** no se puede decir qué baldosa es "el agua". Las texturas no se cargan (la unidad de textura vacía da 0,0,0,1).
- **Dónde se verían los efectos:** en las capturas del juego en la consola (GX_ES_STEP2_RESULTS.md).

## Tolerancias

| Comparación | Tolerancia | Por qué |
|---|---|---|
| C frente a la mezcla calculada (escenas exactas) | ≤ 1 por canal | El shader calcula en float32 y la conversión a 8 bits redondea; 1 cubre la precisión mediump de `gl_LastFragColorARM` (fp16) en la Mali. En Mesa dio 0 |
| A frente a la mezcla calculada | ≤ 1 | La especificación de GL no obliga al blending fijo a redondear igual que el cálculo exacto; Mesa difiere en 1 en parte de los píxeles |
| A frente a C, todas las escenas | ≤ 1 por canal en todos los píxeles | Lo anterior en cada lado |
| B frente a C en los controles | **0** (idénticas) | Los pipelines sin doble fuente no cambian: misma clave, mismo programa, mismo estado |

## Resultados en la PC (x86-64, Mesa 25.2.8 llvmpipe, OpenGL ES 3.2)

**Contra la mezcla calculada** (cada píxel de las escenas exactas):

| Modo | `exactas-galaxy` (326 656 píxeles de doble fuente) | `exactas-controles` (336 864) |
|---|---|---|
| A referencia | 0 con error > 1; máx. 1 | 0 con error > 1; máx. 1 |
| B antes | **107 186 con error > 1; máx. 199** | 0 con error > 1; máx. 1 |
| C fetch | **0 con error > 1; máx. 0** | 0 con error > 1; máx. 1 |

En 187 863 de los 326 656 píxeles de doble fuente, la mezcla sin la alfa del TEV daría otro valor: son los píxeles
donde el problema se ve.

**Imagen contra imagen** (`metricas.txt`; 337 920 píxeles por imagen, toda la imagen; error en niveles de 0-255):

| Escena | Comparación | % distintos | Error medio R G B A | Error máx. R G B A |
|---|---|---|---|---|
| exactas-galaxy | A vs B | 58.92 | 11.33 9.64 9.55 0.00 | 180 199 193 0 |
| exactas-galaxy | A vs C | 15.33 | 0.059 0.055 0.042 0.032 | 1 1 1 1 |
| exactas-controles | A vs B, A vs C, B vs C | 0 | 0 | 0 |
| gradientes | A vs B | 49.62 | 4.93 4.08 4.50 0.00 | 122 142 153 0 |
| gradientes | A vs C | 25.11 | 0.102 0.100 0.100 0.040 | 1 1 1 1 |
| superpuestas | A vs B | 59.99 | 3.31 6.78 3.18 0.00 | 33 58 40 0 |
| superpuestas | A vs C | 17.37 | 0.082 0.046 0.058 0.000 | 1 1 1 0 |
| claves-reales | A vs B | 52.21 | 3.05 2.75 3.10 0.00 | 109 78 101 0 |
| claves-reales | A vs C | 22.52 | 0.105 0.100 0.105 0.002 | 1 1 1 1 |
| controles-reales | A vs B, A vs C, B vs C | 0 | 0 | 0 |

Los "% distintos" de A vs C son píxeles con 1 nivel de diferencia: el redondeo del blending fijo de Mesa (A) frente al
cálculo del shader (C). C coincide con la mezcla calculada en el 100 % de los píxeles de las escenas exactas.

**Por pipeline real** (baldosas de `claves-reales`, error máximo de B frente a A):

| Grupo | Pipelines | Con error > 1 | Máx. | Mediana |
|---|---|---|---|---|
| `GXSetDstAlpha` | 35 | 28 | 109 | 38 |
| RGBA6 sin alfa constante | 206 | 73 | 3 | 1 |

C frente a A, en todas las baldosas: máx. 1.

**Las mismas pruebas en otras configuraciones:**

| Configuración | Resultado |
|---|---|
| `GALAXY_GLES_FORCE_ES30=1` (GLSL ES 3.00, paleta 2D, buffers copiados) | Las mismas métricas exactas; las tres pasan |
| `GALAXY_GLES_THREAD=1` (hilo de GL) | Iguales |
| Sin traductor (`NEBULA_GLES_TRANSLATOR=OFF`, como la consola), con la biblioteca GLSL: B y C | Iguales; A vs C ≤ 1 en todo; 0 programas fuera de la biblioteca |
| Fetch en los programas | 208 programas transformados, 0 que vuelvan al blending fijo |

## Capturas

Hojas de contacto en `docs/gx_es_blend/`. Cada una tiene, en este orden:

1. A (referencia)
2. B (antes)
3. C (fetch)
4. diferencias A-B
5. diferencias A-C

Se muestra el color del EFB; la alfa se ve en el azul de las diferencias.

- `exactas-galaxy.png`: en B se ven las franjas de `GXSetDstAlpha` (rosadas y claras), que en A y C no están.
- `exactas-controles.png`: las tres iguales; las diferencias, vacías.
- `gradientes.png`, `superpuestas.png`: B más clara u oscura donde la alfa del TEV y la constante difieren.
- `claves-reales.png`: las baldosas de `GXSetDstAlpha` se ven en rojo en A-B; A-C solo tiene diferencias de 1.
- `controles-reales.png`: diferencias vacías.

Las imágenes completas (`<modo>/<escena>.png`, `fondo-*.png` y `dif/`) las escribe la prueba en su carpeta de salida.

## Las pruebas automáticas que pide el paso

| # | Prueba | Cómo | Estado |
|---|---|---|---|
| 1 | Pipelines afectados | `claves-reales` + escenas exactas, C contra A y contra el cálculo | Pasa en la PC |
| 2 | Pipelines no afectados | `exactas-controles`, `controles-reales`: B y C idénticas | Pasa en la PC |
| 3 | `GXSetDstAlpha` | Filas `+dstalpha` de las escenas exactas; alfa del EFB comparada | Pasa en la PC |
| 4 | Alfa del TEV | 11 alfas, entre ellas 1 (que cuenta como 0) y los bordes de 6 bits | Pasa en la PC |
| 5 | Combinaciones de blending | Los 5 pares de Galaxy × escritura de alfa × alfa constante, más 4 controles | Pasa en la PC |
| 6 | Framebuffer fetch | Modo C: EXT en Mesa; **ARM solo en la consola** | EXT pasa; ARM pendiente |
| 7 | Ruta sin la extensión | Modo B (`GALAXY_GLES_BLEND_FETCH=0`): sin errores en los controles; el error de doble fuente queda medido | Pasa en la PC |

**Pruebas que no corren en Linux** (dependen de D3D12) y no se cuentan como pasadas: `vertex_loader_tests`,
`shader_gen_tests`, `xf_lighting_tests`, `efb_conversion_tests` y `sampler_key_tests` (GX_ES_TEST_PLAN.md).

## Cómo repetirlo

```sh
cmake --build build-gles --target nebula_gles_blend_tests
export SDL_VIDEODRIVER=offscreen NEBULA_SEMILLA=platform/android/vk_seed_keys.bin
for m in referencia plain fetch; do build-gles/nebula_gles_blend_tests --modo $m salida/; done
build-gles/nebula_gles_blend_tests --comparar salida/      # metricas.txt y dif/; estado 0 si todo cumple
```
