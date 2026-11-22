# GX-ES paso 2: análisis del blending de doble fuente

Fase 1 del paso 2: reproducir el problema que encontró la auditoría (GX_ES_RISK_REGISTER.md, R-01) y encontrar su
causa exacta, antes de cambiar nada. Base: commit `bd452a1f` (la auditoría sobre `b73a2d86`).

## Resumen

- El problema existe y se reprodujo: con el backend GLES de antes del paso 2, la escena exacta da hasta **199 niveles**
  de error en un canal (de 255) frente a la mezcla de GX.
- **La auditoría simplificó la causa.** De los 241 pipelines de doble fuente:
  - **35** mezclan el color con la alfa constante de `GXSetDstAlpha`. Es el error grande.
  - **206** mezclan con la alfa del TEV recortada a 6 bits en vez de la de 8. El error es de 1 a 3 niveles.

## 1. Dónde está el blending

| Paso | Dónde | Qué hace |
|---|---|---|
| Registros GX | BP 0x41 `PE_CMODE0` (`bp::kBlendMode`), BP 0x42 `PE_CMODE1` (`bp::kConstAlpha`), BP 0x43 `PE_CONTROL` (`bp::kPeControl`) en `gx_bitfields.h` | `decode_blend` → `BlendMode` (blend, factores, resta, logic op, máscaras de color y alfa, alfa constante); `decode_pe_control` → formato de píxel, Z temprana |
| `GXSetDstAlpha` | `native/petari/src/RVL_SDK/gx/GXPixel.c` | Escribe `PE_CMODE1`: alfa constante y su habilitación |
| Clave de píxel | `runtime/src/gx/shader_keys.cpp`: `needs_dual_src_alpha`, `PixelShaderKey::flags` | bit 7 = doble fuente; bit 2 = alfa de destino constante |
| Estado fijo | `shader_keys.cpp` (`RenderStateKey::blend_bits`) | bits 0-7: blend, resta, factores; 25-26: máscaras; **27: doble fuente**; **28: alfa constante independiente** |
| Constante | `gx_backend.cpp` `fill_ps_constants`: `alpha_refs[3] = const_alpha / 255` | El valor de `GXSetDstAlpha` |
| Shader | `runtime/src/gx/shader_gen.cpp` (`dual_src`) | Salida 0: color del EFB (RGB a 8 bits por defecto; alfa a 6 bits, o la constante); salida 1: `(0, 0, 0, alfa del TEV a 8 bits)` |
| D3D12 / Vulkan | `pipeline_cache_vk.cpp` (`second_source`) | Los factores de color `SRC_ALPHA`/`INV_SRC_ALPHA` pasan a `SRC1_ALPHA`/`ONE_MINUS_SRC1_ALPHA`: leen la salida 1. Los de alfa no cambian |
| GLES antes del paso 2 | `runtime/src/gx/gles/pipeline_cache_gles.cpp` (`make_pipeline`, `gx_src_blend`, `gx_dst_blend`) | `GL_SRC_ALPHA` lee la salida 0. La salida 1 la descarta GL (hay un solo color attachment) |

## 2. Fuentes de color y alfa en un draw de doble fuente

```
TEV ─► frag (float, malla de 8 bits)
        ├─ salida 0 .rgb = color a 8 bits (RGBA6 con color de 8 bits: el defecto, GALAXY_EFB_TRUE_COLOR)
        ├─ salida 0 .a   = GXSetDstAlpha ? constante>>2 / 63 : alfa_TEV>>2 / 63      (alfa de 6 bits del EFB)
        └─ salida 1 .a   = alfa_TEV / 255                                             (lo que lee la mezcla)
```

Una alfa del TEV de 1 cuenta como 0 en ambas salidas: es el comportamiento que `shader_gen.cpp` reproduce del hardware.

**Mezcla de GX** (D3D12 y Vulkan hacen lo mismo):

- `color = src.rgb · Fs + dst.rgb · Fd`. `SRC_ALPHA` en los factores de color es la alfa del TEV (salida 1).
- `alfa = src.a · Fs_a + dst.a · Fd_a`. `SRC_ALPHA` es la salida 0; con el bit 28 (alfa constante), `alfa = src.a`.

## 3. Los 241 pipelines

Contados en `platform/android/vk_seed_keys.bin` (977 claves de pipeline de una vuelta entera; script en la sección 6):

| Grupo | Pipelines | Pixel shaders distintos | Qué leía GLES en lugar de la alfa del TEV | Error medido en el paso 2 |
|---|---|---|---|---|
| (a) `GXSetDstAlpha` (bits 27 y 28) | 35 | 28 | La alfa constante | En 28 de los 35, más de 1 nivel; máx. 109 en las claves reales y 199 en la escena exacta |
| (b) EFB RGBA6 sin alfa constante (bit 27) | 206 | 159 | La alfa del TEV a 6 bits | En 73 de los 206, más de 1 nivel; máx. 3 |

| Factores (fuente, destino) | Pipelines |
|---|---|
| SRCALPHA, INVSRCALPHA | 172 |
| SRCALPHA, ONE | 62 |
| SRCALPHA, SRCCLR | 5 |
| INVSRCALPHA, SRCALPHA | 1 |
| SRCALPHA, ZERO | 1 |

**Escritura de la alfa del EFB:**

- Apagada en 202 pipelines del grupo (b).
- Encendida en los 35 del grupo (a) y en 3 del (b).
- Solo alfa en 1 pipeline del (b).

Los errores medidos salen de las escenas de GX_ES_VISUAL_REGRESSION.md (claves reales con entradas hechas a mano), no del
juego.

## 4. Estados GX que producen el problema

Hay doble fuente cuando se cumplen a la vez (`needs_dual_src_alpha`):

1. Formato de píxel RGBA6_Z24 (`PE_CONTROL`, `pixel_format == 1`).
2. Blending encendido (`PE_CMODE0` bit 0), sin resta.
3. El factor de origen o el de destino es `GX_BL_SRCALPHA` o `GX_BL_INVSRCALPHA`.

El grupo (a) suma `PE_CMODE1` con la alfa constante habilitada y la escritura de alfa encendida (`GXSetDstAlpha(GX_TRUE,
k)` con `GXSetAlphaUpdate(GX_TRUE)`).

## 5. Capturas reproducibles

**No hay capturas del juego que se puedan reproducir en la PC.**

- Los datos del juego solo están en tu consola.
- Las capturas de ULTRA (`GALAXY_ULTRA_CAPTURE`) no son deterministas: entre dos repeticiones dan 23-28 dB de PSNR
  (docs/RG35XXH.md).
- Los `.gxdump` sirven para el parser, no para rehacer un frame.

**Lo que se hizo en su lugar:** escenas sintéticas deterministas (GX_ES_VISUAL_REGRESSION.md). Usan los 241 pipelines
reales (sus shaders y su estado) y escenas exactas donde cada píxel se compara con la mezcla calculada. Se reproducen
igual en cada corrida.

## 6. Formatos de framebuffer

- **EFB en GLES:** `GL_RGBA8` + `GL_DEPTH_COMPONENT32F`, un solo color attachment.
- **RGBA6 de GX:** se guarda en RGBA8. La alfa vale `round(a6/63·255)`; el color, 8 bits (o 6 con
  `GALAXY_EFB_TRUE_COLOR=0`).
- **Otros render targets:** GLES no usa los de movimiento, reactivo ni HDR (son de D3D12). La mezcla siempre ocurre sobre
  el attachment 0.

Recuento de las claves (Python, sobre el formato `GXVK`: 12 bytes de cabecera y registros de 464 bytes;
`blend_bits` en el byte 16 de cada registro):

```python
dual  = bb >> 27 & 1          # doble fuente
const = bb >> 28 & 1          # GXSetDstAlpha
src, dst = (bb >> 2) & 7, (bb >> 5) & 7
```

## 7. Comportamiento esperado

El de D3D12 y Vulkan, que es la semántica de GX:

- El color se mezcla con la alfa del TEV a 8 bits.
- La alfa del EFB es la salida 0 mezclada con los factores de alfa, o la constante.

En la PC se comprobó así:

- El cálculo de la mezcla en la CPU y el blending de doble fuente de Mesa (`GL_EXT_blend_func_extended`) coinciden con
  error ≤ 1 en 326 656 píxeles.
- El backend GLES de antes da hasta 199 de error en los mismos píxeles (GX_ES_VISUAL_REGRESSION.md).
