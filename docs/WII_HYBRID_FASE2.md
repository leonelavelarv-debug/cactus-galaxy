# Wii Hybrid Input — Fase 2: arquitectura

Decisiones del usuario (2026-11-01): el mapeo de su documento tal cual, y perfiles en archivos `.ini` en vez de JSON.
Principio: **extender lo que Nebula ya tiene, no reemplazar módulos que funcionan** (reglas 12 del documento).

## Capas

```
SDL2 GameController ──▶ PadSample ──▶ evaluate_pad_bindings(tabla) ──▶ LogicalInput ──▶ WiimoteInputSnapshot ──▶ el juego
 host_window_knulli       (runtime)        ▲                                          native_game_host_knulli
                                           │
                       ControlsConfig (runtime_settings.ini)
                                           ▲
                          perfil WII HYBRID (smg/config/input/wii/<juego>.ini)   ← nuevo, solo en el host de la consola
                                           ▲
                          console_bindings (GALAXY_SHAKE, solo con el perfil apagado)
```

| Módulo pedido | Dónde vive | Estado |
|---|---|---|
| Wii Input Mapper | `input_bindings.{h,cpp}` (tabla destino ← hasta 4 fuentes, `LogicalInput`), sin cambios | Existente |
| Perfil WII HYBRID | `platform/knulli/game/wii_profile.h` (cargador) + `native_game_host_knulli.cpp` (`profiled_controls`) | **Fase 3** |
| Motion Simulator | pulsos de sacudida (`advance_wiimote_shake_accel`, `advance_nunchuk_shake_accel`) y `tilt.h` | Existente; ajustes en Fase 4 |
| IR Pointer Emulator | `pointer_control.h` + `publish_wiimote_ir_dots`, `tune.*pointer*` | Existente; ajustes en Fase 5 |
| Perfiles por juego | `smg/config/input/wii/default.ini` y `mario_galaxy.ini`, elegidos con `GALAXY_INPUT_PROFILE` | **Fase 3** |
| Configuración gráfica | `draw_native_settings_overlay` de `host_window_knulli.cpp` (hoy vacía) | Fase 7 |

## Formato de perfil

Líneas `bind.switch_pro.<destino>=botones` y `tune.switch_pro.<ajuste>=valor`, **las mismas que `smg/settings.ini`**, más `name=`
y comentarios con `#`. Un perfil se aplica sobre los controles del runtime: lo que no menciona queda como está. Solo el esquema
`switch_pro` (el de la consola); con `NEBULA_PAD_SCHEME=xbox` el perfil no hace nada y `log.txt` lo dice.

Se eligió `.ini` porque el repo no tiene lector de JSON y esto no agrega dependencias ni código de análisis nuevo: reutiliza
`apply_controls_setting`, que ya valida cada fuente (`binding_source_allowed`).

## Qué no cambia

`runtime/` (y con él Windows y las pruebas de controles), el renderizador y los hilos de GL. El perfil solo cambia la tabla
que el host de la consola evalúa; el costo por lectura es el mismo (`merged` se reconstruye solo si el runtime cambia sus controles).

## Mapeo del perfil (el del documento)

| Consola | Wii |
|---|---|
| Stick izquierdo | Stick del Nunchuk |
| Stick derecho | Puntero IR (clic: recentrar) |
| B (abajo) | A |
| A (derecha), R2 | B |
| R1 | Sacudida (`mario_galaxy`: del Nunchuk; `default`: del Wiimote) |
| L1 / L2 | Nunchuk C / Z |
| X / Y | 1 / 2 |
| START / SELECT | + / − |
| Cruceta | Cruceta |

Dos cosas que el documento no decía y se decidieron: el recentrado del puntero va en el clic del stick derecho (R1 pasó a
sacudir), y la sacudida del perfil de Galaxy es la del Nunchuk porque el usuario pidió el giro con el Nunchuk; `default.ini`
usa el Wiimote, que es lo normal en otros juegos.

## Qué se mide en cada fase

Fase 3: prueba de la tabla (`nebula_wii_profile_tests`) y prueba en la consola con `[input-profile]` y `[input-shake]` en `log.txt`.
Fase 8: latencia de la lectura SDL al snapshot y CPU del hilo principal, con y sin perfil.
