# Wii Hybrid Input — Fase 1: el sistema de entrada de Nebula

Análisis sin cambios de código (2026-11-01). Todo lo que sigue se leyó en el repositorio; los números de línea son de `rg35xxh-arm64` en `3bf6160e`.

## 1. Cómo llega hoy una pulsación al juego

| Capa | Dónde | Qué hace |
|---|---|---|
| Dispositivo | `platform/knulli/game/host_window_knulli.cpp` (~170-216) | SDL2 GameController → `PadSample` (bits de botones unificados `pad::k*Bit`, 6 ejes: sticks y gatillos). Perfil de la consola (`PadProfile`): `NEBULA_FACE_LAYOUT` (posiciones o etiquetas), combinación de salida (`NEBULA_QUIT_COMBO`). Esquema: `NEBULA_PAD_SCHEME` (SwitchPro por defecto; Xbox) |
| Mapeo | `runtime/include/galaxy/input_bindings.h`, `runtime/src/input_bindings.cpp` | `ControlsConfig` = una `BindingTable` por esquema (25 `WiiTarget` × 4 fuentes) + `SchemeTuning`. Defaults en `default_binding_table(scheme)` (versión 5 de controles) |
| Persistencia | `runtime/src/runtime_settings.cpp`; la consola usa `smg/settings.ini` (`GALAXY_RUNTIME_SETTINGS_PATH` en el lanzador) | Claves `bind.<esquema>.<destino>=…` y `tune.<esquema>.<ajuste>=…`; `controls_version`; se carga al arrancar (`native_runtime.cpp:34719`) y `live_controls()` relee cuando cambia `controls_config_generation()` |
| Evaluación | `evaluate_pad_bindings` (`input_bindings.cpp`) | `PadSample` + tabla → `LogicalInput` (máscara de destinos pulsados + stick + puntero). Zona muerta radial en `apply_radial_deadzone` (`pointer_control.h`); histéresis de sticks como botones (0.5 / 0.35) |
| Mando virtual | `platform/knulli/game/native_game_host_knulli.cpp` → `native_game_wiimote_snapshot` | Rellena `WiimoteInputSnapshot` (`native_input.h`): botones, `nunchuk.{stick_x,stick_y,c,z,accel}`, `accel` del Wiimote, puntos IR |
| Puntero | `pointer_control.h`, `publish_wiimote_ir_dots` | Velocidad por stick (`stick_pointer_velocity`, `pointer_speed`), límite ±1 (`kStickPointerLimit`), propietario del puntero (`PointerOwner`), ocultar en reposo (`PointerRestHide`) |
| Movimiento | `native_input.cpp` (`advance_wiimote_shake_accel`), host (`advance_nunchuk_shake_accel`), `tilt.h` | Pulsos sintéticos de aceleración (12 reportes alternando) y, en secciones de inclinación (bola, rayo), gravedad calculada desde el stick. **No hay acelerómetro ni giroscopio real en la consola** |
| Salto/atrás | `pointer_control.h` `JumpBackLatch` | B de mando = A de Wii al controlar a Mario y B de Wii en menús |

Mapeo actual en la consola (SwitchPro, posiciones Nintendo), tal como lo imprime `format_controls_settings` más lo de `console_bindings` (commit `3bf6160e`):

| Destino Wii | Botón de la RG35XX H |
|---|---|
| A (saltar) | A (derecha) |
| JumpBack (saltar / atrás en menús) | B (abajo) |
| B (trozos de estrella) | L1 |
| Z | L2 |
| C | clic del stick izquierdo |
| Stick del Nunchuk | stick izquierdo |
| Puntero IR | stick derecho (solo puntero: con `gyro_pointer` apagado, como en la consola, `evaluate_pad_bindings` no lo deja mover la cruceta aunque la tabla también lo liste) |
| Recentrar puntero | R1 |
| Sacudida del Nunchuk | X (arriba) y R2 |
| Sacudida del Wiimote | Y (izquierda) |
| + / − | START / SELECT |
| 1 / 2 | sin botón |

## 2. Lo que ya cubre el pedido

| Pedido | Estado |
|---|---|
| Mapeo completamente configurable | Sí: toda la tabla se edita en `smg/settings.ini` (`bind.switch_pro.shake=Y, X, ZR`) y con `GALAXY_SHAKE` / `GALAXY_SHAKE_REPORTS` |
| Zona muerta, sensibilidad, velocidad del puntero, invertir Y, ocultar en reposo | Sí: `SchemeTuning` (`tune.*`) |
| Detección de pulsaciones, simultáneas, estados por frame | Sí: `LogicalInput` + `s_previous` por mando |
| Sacudida simulada y estados de movimiento | Sí, sintética (pulsos); inclinación solo en secciones de bola y rayo, desde el stick |
| Puntero IR desde el stick derecho | Sí |
| Persistencia | Sí, un archivo para toda la consola |
| Sin asignaciones por frame | El host copia una `BindingTable` pequeña por mando en cada lectura (`console_bindings`); es lo único nuevo y no usa memoria dinámica |

## 3. Lo que falta de verdad

1. **Perfiles por juego en JSON** (`config/input/wii/*.json`): no existe. Hay un solo `settings.ini` y **no hay ninguna biblioteca JSON** en el repo (`third_party/` no trae una). Nebula además solo ejecuta Super Mario Galaxy USA (RMGE01): no hay SMG2 ni otros juegos que ejecutar, así que `mario_galaxy_2.json` y la "extensión a otros juegos" serían diseño sin forma de probarlo.
2. **Interfaz gráfica de configuración**: `draw_native_settings_overlay` está vacío en el host de la consola (`host_window_knulli.cpp:513`) y en el de SDL. Windows tiene su propio overlay; no se reutiliza (usa Win32/D3D12). No hay pantalla para cambiar botones ni probar puntero/sacudida.
3. **Pruebas de latencia y CPU del módulo de entrada**: no hay medición; el log de ULTRA mide frames, no entrada.
4. **Perfil dedicado "WII HYBRID – RG35XX H"** con nombre y selección: hoy son los defaults del esquema.

## 4. Conflictos entre el mapeo pedido y el actual

| Pedido | Hoy | Conflicto |
|---|---|---|
| R1 = Shake | R1 recentra el puntero | Perder el recentrado o moverlo |
| L1 = Nunchuk C | L1 = Wii B (trozos de estrella); C va en el clic del stick | Los trozos de estrella pasarían a otro botón (pedido: R2 "configurable") |
| X = Wii 1, Y = Wii 2 | X e Y hacen el giro (nuevo mapeo de hoy) | 1 y 2 no se usan en el juego; el giro quedaría solo en R1 |
| B = Wii A, A = Wii B | A (derecha) salta y B (abajo) también salta | Con el pedido, el botón derecho dispararía trozos de estrella y el de abajo saltaría |
| Stick derecho = puntero sin interferir con Mario | Ya es así (comprobado con el código: sin giroscopio el stick derecho es solo puntero) | Ninguno. *Corrección de la primera versión de este informe, que decía lo contrario* |

Ninguno es un error: son decisiones de diseño que cambian cómo se juega y deben elegirse antes de la Fase 3.

## 5. Archivos concretos por fase (propuesta, sin tocar nada todavía)

| Fase | Archivos | Cambio |
|---|---|---|
| 2 Arquitectura | este documento y `docs/WII_HYBRID_FASE2.md` | Decidir: extender lo existente (recomendado) en vez de un módulo nuevo |
| 3 Mapeo físico | `platform/knulli/game/native_game_host_knulli.cpp` (`console_bindings`), `platform/knulli/smg/Super Mario Galaxy.sh` | El perfil "WII HYBRID – RG35XX H" como tabla de la consola, sin tocar `runtime/` ni Windows |
| 4 Nunchuk y movimiento | mismo host; `runtime/src/native_input.cpp` solo si se cambia el pulso del Wiimote | Parámetros del pulso configurables; inclinación con el stick derecho solo en secciones donde el juego la pide |
| 5 Puntero | mismo host (`pointer_speed`, límites ya existen) | Centrado automático opcional, suavizado; modo absoluto/relativo solo si hay demanda |
| 6 Perfil Mario Galaxy | `platform/knulli/smg/` (archivo de perfil) | Cargar un archivo de perfil de la consola al arrancar |
| 7 Interfaz | `platform/knulli/game/host_window_knulli.cpp` (`draw_native_settings_overlay`) | Pantalla propia con `UiCanvas`, controlada con los botones |
| 8 Optimización | pruebas nuevas en `platform/knulli/game/` | Medir latencia (lectura SDL → snapshot) y CPU |
| 9 Compilar y probar | `armar.sh` | Paquete y prueba en la consola |

## 6. Riesgos

- El host de SDL (`platform/sdl/`) tiene su propia copia de varias funciones; si se toca lo compartido hay que mantener ambos.
- Las pruebas de controles del runtime (`controls_tests.cpp`) fijan los defaults de Windows: no se deben cambiar los defaults del runtime.
- Una pantalla de configuración dibujada en el hilo de render no debe bloquear; debe leer `sdl::input_state()` sin esperas.

## 7. Cómo se medirá lo que venga

- Latencia: marca de tiempo al leer el `PadSample` y al publicar el snapshot, registrada en `log.txt`.
- CPU: `thread_sampler` ya existente (`NEBULA_PROFILE`) para comparar antes y después.
- Funcional: prueba en la PC de las tablas (como la de `console_bindings`) y prueba en la consola con `[input-shake]` y registro de cada destino pulsado.
