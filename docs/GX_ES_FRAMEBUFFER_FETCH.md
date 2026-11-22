# GX-ES paso 2: la mezcla de doble fuente con framebuffer fetch

Fases 2 y 3 del paso 2: la solución con `GL_ARM_shader_framebuffer_fetch` y cómo quedó implementada.

## Por qué framebuffer fetch y no otra cosa

| Opción | En la Mali-G31 (Nebula Probe 0.3) | Exacta | Notas |
|---|---|---|---|
| `GL_EXT_blend_func_extended` (`SRC1_ALPHA`, como D3D12/Vulkan) | **No está** | Sí | La que usa Mesa en la PC como referencia |
| `GL_ARM_shader_framebuffer_fetch` | **Sí** | Sí: el shader lee el color del EFB y hace la mezcla de GX | Elegida |
| `GL_EXT_shader_framebuffer_fetch` | No (sí en Mesa) | Sí | La misma ruta con otra sintaxis: es lo que se prueba en la PC |
| Dos pasadas (color, luego solo alfa) | — | No en general: falla con triángulos del mismo draw que se tapan entre sí | Descartada |
| Sacar la alfa del TEV por la salida 0 cuando la alfa del EFB no se escribe | — | Sí en 202 de los 241 | Necesita otra forma de programa por estado; queda como alternativa para drivers sin fetch (pendiente) |

## Cómo funciona

El GLSL de doble fuente que ya está en la biblioteca (`smg/shaders-gles.bin`) se transforma al enlazar el programa
(`gles::fetch_blend_glsl`, en `shader_library_gles.cpp`):

1. Agrega `#extension GL_ARM_shader_framebuffer_fetch : require` (o la `EXT`) después de `#version`.
2. Convierte las dos salidas (`layout(location = 0|1) out vec4 …`) en variables globales.
3. Renombra `main()` a `nb_main()`.
4. Agrega un `main()` nuevo que lee el EFB, llama al shader original y mezcla con los factores GX **escritos como
   constantes** (los factores salen del estado fijo del pipeline, `RenderStateKey`).

Lo que genera para el caso más común (SRCALPHA, INVSRCALPHA), en la Mali:

```glsl
#version 320 es
#extension GL_ARM_shader_framebuffer_fetch : require
...                                  // el shader del TEV, sin cambios, como nb_main()
highp vec4 _entryPointOutput_color;  // antes: layout(location = 0) out
highp vec4 _entryPointOutput_color1; // antes: layout(location = 1) out
...
layout(location = 0) out highp vec4 nb_efb;
void main()
{
    highp vec4 nb_dst = gl_LastFragColorARM;   // GL_EXT: inout nb_efb, y nb_dst = nb_efb
    nb_main();
    highp vec4 nb_src = clamp(_entryPointOutput_color, 0.0, 1.0);
    highp float nb_tev = clamp(_entryPointOutput_color1.a, 0.0, 1.0);
    highp vec3 nb_rgb = clamp(nb_src.rgb * vec3(nb_tev) + nb_dst.rgb * vec3(1.0 - nb_tev), 0.0, 1.0);
    highp float nb_a = clamp(nb_src.a * nb_src.a + nb_dst.a * (1.0 - nb_src.a), 0.0, 1.0);
    nb_efb = vec4(nb_rgb, nb_a);
}
```

## Lo que se evaluó

| Punto | Resultado |
|---|---|
| Sintaxis GLSL ES | `gl_LastFragColorARM` existe en GLSL ES 1.00, 3.00 y 3.x con la extensión. Se agrega la directiva tras `#version`, antes de cualquier otro token. **No validado fuera de la consola:** ni Mesa ni el validador de glslang conocen la extensión ARM. La variante EXT, con la misma transformación, compila, enlaza y da los resultados correctos en Mesa (3.20 y 3.00) |
| Acceso al color anterior | `gl_LastFragColorARM` es el color del attachment 0 antes de este fragmento; el EFB tiene un solo attachment (RGBA8). Su precisión es mediump: fp16 no representa k/255 exacto, pero el error (< 1/4096) es menor que medio nivel de 8 bits, así que el redondeo final da el mismo valor |
| Orden de operaciones | Se lee el EFB, corre el TEV, se mezcla y se escribe. Si el TEV descarta (alpha test), no se escribe nada, igual que con blending fijo |
| Fórmulas | Las de D3D12/Vulkan: color con la alfa del TEV (`SRC1`), alfa con la de la salida 0; los 8 factores de GX en origen y destino; resultado acotado a [0, 1] como el blending de un destino UNORM |
| Alfa de origen y destino | `nb_src.a` (6 bits o la constante) en la ecuación de alfa; `nb_dst.a` del EFB para `DSTALPHA` |
| `GXSetDstAlpha` | Con el bit 28, la alfa escrita es `nb_src.a` (la constante); las máscaras de escritura (`glColorMask`) siguen en el pipeline |
| Operaciones del TEV | No cambian: el shader original corre entero como `nb_main()` |
| Profundidad | Sin cambios. `early_fragment_tests` y `gl_FragDepth` quedan en el shader original, y la prueba de Z la sigue haciendo el pipeline. Un fragmento que no pasa la Z no escribe |
| Transparencias superpuestas | El fetch ve lo que escribió el fragmento anterior del mismo pixel, también dentro del mismo draw (la extensión es coherente). Probado: escena "superpuestas" |
| Render targets | Solo el EFB (attachment 0). Las copias EFB, el XFB y los peeks no cambian |
| Uber shader | Sus variantes de doble fuente **siguen con blending fijo** (se cuentan en el log). Solo dibuja lo que la biblioteca no tiene; 0.0 % de los draws en la medición base (ULTRA) |

## Implementación

| Archivo | Cambio |
|---|---|
| `runtime/include/galaxy/gx/gles/shader_library_gles.h`, `runtime/src/gx/gles/shader_library_gles.cpp` | `FramebufferFetch` y `fetch_blend_glsl`: la transformación del GLSL (texto; sirve para GLSL de la biblioteca y del traductor) |
| `runtime/include/galaxy/gx/gles/pipeline_cache_gles.h`, `runtime/src/gx/gles/pipeline_cache_gles.cpp` | `GALAXY_GLES_BLEND_FETCH`; `PairKey::blend` (solo los pipelines con el bit 27: el resto tiene la misma clave que antes); `build_program` transforma, enlaza y guarda el binario con un hash propio; si algo falla, vuelve al programa normal y lo dice en el log; `make_pipeline` apaga el blending fijo de esos pipelines |
| `runtime/include/galaxy/gx/gles/renderer_gles.h`, `runtime/src/gx/gles/renderer_gles.cpp` | `Pipeline::dual_source`/`fetch_blend`; contadores por draw en la línea `[gles] … frames/s`: `dual-source draws (… blended in the shader)`; `efb_framebuffer_for_tests` |
| `platform/knulli/game/gles_blend_tests.cpp`, `CMakeLists.txt` | Las pruebas (GX_ES_VISUAL_REGRESSION.md) |
| `platform/knulli/prueba-mezcla/` | El paquete de prueba para la consola |
| `platform/knulli/smg/Super Mario Galaxy.sh` | Documenta `GALAXY_GLES_BLEND_FETCH` |

**Lo que no cambia:**

- FIFO, CP, XF y BP, el generador de shaders, las claves y la biblioteca GLSL: la transformación se hace en la consola,
  sobre el GLSL que ya existe.
- La recompilación del juego.
- Las dependencias.
- Los pipelines sin el bit 27 (736 de 977): su clave, su programa y su binario son los de antes, como se comprobó en las
  escenas de control.

## Configuración y diagnóstico

| `GALAXY_GLES_BLEND_FETCH` | Ruta |
|---|---|
| sin poner o `1` (defecto) | Framebuffer fetch si el driver lo tiene: ARM primero, EXT después; si no tiene ninguno, blending fijo |
| `0` | Blending fijo (como antes del paso 2): la ruta B de las comparaciones |

Qué escribe en el log:

- Al arrancar: `[gles-blend] dual-source alpha: blended in the shader (GL_ARM_shader_framebuffer_fetch)`, o el motivo de
  usar blending fijo.
- Si un programa no se pudo transformar o enlazar: `[gles-blend] a dual-source program stays on fixed-function
  blending: …` (las 6 primeras veces). Ese pipeline se dibuja como antes.
- Cada 600 frames, en la línea `[gles] …`: `N dual-source draws (M blended in the shader)` por frame.

## Ruta sin framebuffer fetch

Si el driver no tiene ninguna de las dos extensiones, o con `GALAXY_GLES_BLEND_FETCH=0`, se usa el blending fijo de
antes. Es la diferencia documentada: 35 pipelines con error grande y 206 con error de hasta 3 niveles. Un driver con
`GL_EXT_blend_func_extended` podría usar `SRC1_ALPHA` como Vulkan; no está implementado en el backend (la Mali no lo
tiene), solo en la prueba como referencia.
