# GX-ES: el cierre en CosmosGardenGalaxy y la memoria que crece por galaxia

Paso aparte, con permiso: toca los anillos y la memoria de la ruta normal (OpenGL ES 3.2), que el paso 2 no permitía
cambiar. Viene del `smg/log.txt` de la tercera serie de ULTRA en KNULLI (31-oct).

## Lo que mostró el log

**1. Con la corrección, el juego se cerraba siempre al entrar a CosmosGardenGalaxy** (la 7.ª parada del recorrido, Jardín
Céfiro).

- Los tres recorridos con fetch (calentamiento, fetch y fetch2) terminaron igual, al byte:
  `fatal: the renderer stopped: UploadRing 'vertex' exhausted: request 432 B at frame-used 29359728/29360128 B`
  (NEB-C02, estado 3).
- Un frame de la carga pide más de los 28 MiB de vértices por frame. En las otras galaxias el pico es de 17-21 MiB.
- **La serie no tenía límite de galaxias** (son 44 paradas). Los "6 galaxias" de todas las series de este paso eran este
  cierre, no un límite. Las cifras de FPS de GX_ES_STEP2_RESULTS.md son de esas 6 galaxias y siguen valiendo para ellas.

**2. "antes" no se cerraba ahí, y por eso tardaba más.**

- Con `GALAXY_GLES_BLEND_FETCH=0`, el pico de ese frame fue de 26.8 MiB, debajo de 28. El recorrido siguió hasta la
  12.ª galaxia (FactoryGalaxy) y a los 370 s el sistema lo mató (`Killed`, 137).
- Así se explican los 606 s y 342 s de "antes" en la primera y la tercera serie: no era el blending.
- Por qué con fetch ese frame pasa de 28 MiB y sin fetch no, no está investigado. Con fetch hay más draws por frame en
  todas las series (182 contra 162-167).

**3. La memoria crece galaxia por galaxia.**

En el recorrido "antes" (las líneas `memoria:` del lanzador):

| | VmRSS de nebula | Libre |
|---|---|---|
| Al empezar | 614 MiB | 253 MiB |
| AstroGalaxy | 707 MiB | 176 MiB |
| Hacia la 8.ª galaxia | 749 MiB | 89 MiB |
| Antes del `Killed` (12.ª) | 758 MiB | 45 MiB |

El log no dice qué parte crece. Los presupuestos de las cachés suman más de lo que deja libre la consola:

- display lists: 192 MiB por intérprete de FIFO, y hay tres (dibujo, dependencias y eventos);
- texturas reciclables: 128 MiB;
- copias del EFB: 64 MiB;
- vértices decodificados: 64 MiB, más su buffer residente de 64 MiB;
- reemplazos de texturas: 256 MiB, solo con packs.

## El cambio

### El anillo que se llena ya no cierra el juego

| Archivo | Qué |
|---|---|
| `runtime/include/galaxy/gx/upload_ring.h`, `runtime/src/gx/upload_ring.cpp` | **`set_overflow`:** un frame que llena su parte del anillo sigue en la del slot siguiente, si quien lo maneja la libera. Hasta `frames_in_flight - 1` slots por frame; si no alcanza, sigue siendo un error fatal. Esa parte deja de valer para las subidas inmutables: nueva generación del slot y nueva identidad del anillo. Log de las primeras 8 veces: `[ring] vertex: a frame needed more than 28 MiB …; it continues in slot N` |
| `runtime/src/gx/gles/renderer_gles.cpp`, `renderer_gles.h` | **`borrow_frame_slot`:** espera a que la GPU termine el último frame de ese slot y lo marca. `end_frame` le da a ese slot el fence de este frame, así que el siguiente frame que lo use espera a que este termine. Los cuatro anillos lo usan. En la línea `[gles] … frames/s`: `N ring segments continued into another slot` |
| `platform/knulli/game/gles_renderer_tests.cpp` | **Prueba:** un frame que pasa su parte dibuja el triángulo que queda fuera. Los 5 frames siguientes dan la vuelta a todos los slots sin pedir nada prestado. Pasar de los cuatro slots juntos sigue siendo fatal, y el frame siguiente dibuja |

- **Memoria:** no cuesta nada fijo. Usa los slots de los frames en vuelo, que ya existen.
- **Costo:** solo en el frame que se pasa. Espera a la GPU una vez por slot prestado, y el frame que sigue en ese slot
  espera al que se pasó.
- **D3D12 y Vulkan:** no ponen el manejador, así que no cambian.

**Primera prueba en la consola (31-oct): el anillo siguió en otro slot, pero el juego se cerró igual.**

- El log dice `[ring] vertex: a frame needed more than 28 MiB (29359728 B used, 432 B more); it continues in slot 3`.
- Enseguida: `fatal: … draw batch upload rings were not contiguous`.
- GxBackend junta draws seguidos en uno solo si sus vértices e índices quedan uno detrás del otro en el anillo. Cuando
  el anillo salta a otro slot, el draw que se iba a juntar ya no queda contiguo.

**Corrección:** `UploadRing::fits` dice si un tamaño cabe en lo que queda de la parte actual.
`GxBackend::batch_rings_fit` lo pregunta en los tres lugares donde un draw se junta con el anterior (`on_cached_packet_draw_run`,
`on_cached_draw_run_draw` y el draw normal).

- Si un primitivo de *n* vértices podría no caber, no se junta. Se mide con cotas altas: 4 × *n* vértices y 6 × *n* índices.
- El lote anterior se manda, y el primitivo empieza uno nuevo, que puede seguir en otro slot sin romper nada.
- Solo cambia algo cerca del final de una parte del anillo. En D3D12 y Vulkan, donde el anillo nunca salta, ahí se manda
  un lote antes del error de siempre.
- La prueba del renderer comprueba `fits` antes del salto.

### La medición de la memoria

| Archivo | Qué |
|---|---|
| `runtime/src/gx/gx_backend.cpp`, `gx_backend.h` | **`log_memory_census`:** cada `GALAXY_GX_MEMORY_LOG` segundos (60 por defecto, 0 lo apaga), una línea `[memoria]` en log.txt. Trae VmRSS, la memoria libre y las cachés: display lists de los tres intérpretes, texturas decodificadas, caché de contenido, texturas reciclables, vértices decodificados y rangos inmutables. En GLES también las texturas y buffers de GL vivos y las copias del EFB |
| `runtime/include/galaxy/gx/fifo_parser.h`, `texture_cache.h` | Lo que ocupan sus cachés (solo lectura) |
| `runtime/src/gx/gles/gles_context.cpp`, `gles_context.h` | Bytes y número de texturas de GL vivas, bytes de buffers |
| `platform/knulli/smg/smg/ultra/serie.cfg` | La serie de esta medición: un recorrido por las 44 galaxias, `[memoria]` cada 30 s, límite de 2400 s |

No se limita nada todavía: primero hay que ver qué caché crece.

### Primeras líneas `[memoria]` (la misma corrida, 7 galaxias)

| Momento | VmRSS | Libre | Display lists | Texturas decodificadas | Vértices decodificados | Texturas de GL | Buffers de GL |
|---|---|---|---|---|---|---|---|
| Al empezar | 500 | 324 | 0.2 | 2.0 | 0.4 | 10.8 | 224 |
| AstroGalaxy | 598 | 237 | 12.5 | 11.0 | 23.0 | 21.6 | 224 |
| 3.ª galaxia | 643 | 191 | 19.7 | 14.5 | 34.2 | 26.2 | 224 |
| 5.ª galaxia | 673 | 148 | 22.1 | 19.3 | 45.6 | 34.0 | 224 |
| 7.ª galaxia | 680 | 133 | 18.6 | 17.4 | 43.2 | 35.3 | 224 |

Todo en MiB.

- **Lo que más creció:** los vértices decodificados (+43; su tope es 64), las texturas de GL (+25) y las display lists
  (+18; solo las del intérprete de dibujo, los otros dos no guardan nada).
- **Los buffers de GL** son fijos: 224 MiB, los cuatro anillos más los vértices residentes.
- **Lo que no miden las cachés** también creció, unos 95 MiB de los 180. Puede ser el driver (programas), el montón
  o la memoria del juego.
- Con 7 galaxias no alcanza para saber qué sigue creciendo. Hace falta el recorrido completo.

### El recorrido completo (31-oct, con la corrección del lote)

**El cierre de CosmosGardenGalaxy está resuelto.** El anillo siguió en otro slot (`it continues in slot 0`) y el
recorrido pasó a la 8.ª galaxia y siguió hasta la 13.ª, FishTunnelGalaxy.

**Ahí se acabó la memoria.**

- A los 385 s entró a FishTunnelGalaxy con 33 MiB libres.
- Desde los ~430 s el sistema quedó trabado:
  - el hilo de render hacía 0.3 ms por frame y la GPU estaba al 0-2 %;
  - el audio casi no avanzaba (de 23 857 a 23 885 frames de jaudio en 5 s);
  - sin memoria libre, el kernel saca de la RAM el código y los archivos en uso y los vuelve a leer de la SD.
- A los 568 s una espera a la GPU pasó los 10 s y el juego se detuvo: `frame-specific GPU completion wait timed out`.

| Galaxia | VmRSS | Libre | Display lists | Vértices decodificados | Texturas de GL | Buffers de GL |
|---|---|---|---|---|---|---|
| Al empezar | 499 | 323 | 0.2 | 0.4 | 10.8 | 224 |
| 7.ª | 685 | 131 | 18.6 | 43.2 | 35.0 | 224 |
| 8.ª | 717 | 65 | 30.6 | 49.9 | 39.8 | 224 |
| 12.ª | 721 | 51 | 29.7 | 41.6 | 41.4 | 224 |
| 13.ª | 734 | 33 | 42.4 | 53.9 | 51.0 | 224 |
| Trabado | 729 | 32 | 42.0 | 58.3 | 54.8 | 224 |

Todo en MiB. Las texturas de GL incluyen las decodificadas (28) y las reciclables (10).

- **Lo que crece y se puede limitar sin perder imagen:**
  - las display lists, hasta 42 MiB de un presupuesto de 192;
  - los vértices decodificados, hasta 58 de 64, más su buffer residente de 64 MiB, fijo, dentro de los 224.
- **Las texturas** (55 MiB) son lo que el juego usa. No se tocan.
- **Lo que no miden las cachés** creció unos 85 MiB y se estabiliza hacia la 8.ª galaxia: driver, montón y memoria
  del juego.

### Los límites nuevos (lanzador de la RG35XX H)

| Ajuste | Antes | Ahora | Ahorro esperado |
|---|---|---|---|
| `GALAXY_GX_DISPLAY_LIST_CACHE_MB` | 192 | 32 | ~10 MiB a la 13.ª galaxia, más después |
| `GALAXY_GX_DECODED_VERTEX_CACHE_MB` (nuevo) | 64 (fijo) | 32 | ~26 MiB de vértices + 32 MiB del buffer residente |
| `GALAXY_GLES_RING_VERTEX_MB` | 28 | 24 | 16 MiB de anillo. El pico al jugar es de 17-21 MiB; los frames de carga que pasan (26.8 y 32.4 MiB, 2 de 74 ventanas) siguen en otro slot |

- `GALAXY_GX_DECODED_VERTEX_CACHE_MB` es código nuevo en `gx_backend`:
  `decoded_packet_run_cache_max_bytes()` lo lee una vez (entre 4 y 1024; si no, 64). Fija el presupuesto de la caché
  y el tamaño del buffer residente (`resident_decoded_vertex_capacity()`).
- **Costo:** lo que no quepa se vuelve a convertir en la CPU. Lo dirá la comparación de FPS de las primeras 6 galaxias
  con las series anteriores (fetch: 34.6 mostrados, 14.0 dibujados).

### Con los límites de 32 MiB (31-oct, tercer recorrido)

**Llegó a la 21.ª galaxia, IceVolcanoGalaxy, antes 13.ª.** Ahí el kernel lo mató por falta de memoria:

```
Out of memory: Kill process 1149 (Nebula Host Mai) score 739
Killed process 1149 … anon-rss:435232kB, file-rss:305684kB, shmem-rss:16688kB
```

- **`file-rss` (~300 MiB) es memoria de la GPU mapeada en el proceso**, no archivos. El oom_reaper no la soltó.
  Coincide con los buffers de GL (176 MiB) más las texturas de GL (~68) y lo del driver.
  - Por eso VmRSS (~720 MiB) cuenta también la memoria de la GPU.
  - La memoria de la CPU del proceso (`anon-rss`) eran 435 MiB.
- **Los límites se cumplieron:**
  - display lists: 28-32 MiB;
  - vértices decodificados: 16-32 MiB;
  - buffers de GL: 176 MiB, antes 224.
- **Lo que siguió creciendo:**
  - las texturas recicladas, de 16 a 27 MiB (con 128 de presupuesto);
  - las texturas de GL, de 42 a 68 MiB.
- **La memoria libre** quedó en 47-80 MiB entre la 8.ª y la 20.ª galaxia y bajó a 33 en la 21.ª.
- **IceVolcanoGalaxy ya iba muy lenta:** 2.2 FPS dibujados, GPU 38.6 ms, frame p99 de 2.3 s. Puede ser falta de
  memoria antes de la muerte, o esa galaxia.
- **FPS dibujados de las primeras 6 galaxias**, frente a la segunda serie con fetch:

  | | Astro | BattleShip | BeltConveyer | BreakDown | CannonFleet | Cocoon |
  |---|---|---|---|---|---|---|
  | Ahora | 10.0 | 14.5 | 17.2 | 16.6 | 14.4 | 13.1 |
  | Antes | 9.9 | 16.2 | 18.2 | 15.9 | 13.6 | 11.4 |

  Las diferencias están dentro de lo que ya varían las repeticiones. Los límites no costaron FPS medibles.

**Siguiente ajuste (lanzador):**

| Ajuste | Antes | Ahora |
|---|---|---|
| `GALAXY_TEXTURE_POOL_MB` | 128 | 8 |
| `GALAXY_GX_DISPLAY_LIST_CACHE_MB` | 32 | 24 |
| `GALAXY_GX_DECODED_VERTEX_CACHE_MB` | 32 | 24 |
| `GALAXY_GLES_RING_CONSTANT_MB` | 6 | 5 (el pico medido: 3.9 MiB) |

El ahorro esperado es de unos 40-50 MiB a la altura de la 21.ª galaxia. Las texturas que el juego usa no se tocan.

### Con los límites de 24 MiB (31-oct, cuarto recorrido)

**Llegó a la 28.ª galaxia, OceanRingGalaxy (antes 21.ª).** Murió al cargarla, otra vez por falta de memoria:
`anon-rss:481388kB, file-rss:262436kB, shmem-rss:16772kB`.

- **Las cachés ya no crecen:**

  | Caché | Uso |
  |---|---|
  | Display lists | ≤ 24 MiB |
  | Vértices decodificados | ≤ 24 MiB |
  | Texturas recicladas | ≤ 6 MiB |
  | Texturas de GL | 40-53 MiB |
  | Buffers de GL | 164 MiB |

- **La memoria libre** se mantuvo en 65-82 MiB desde la 12.ª galaxia, con un mínimo de 45. La carga de OceanRingGalaxy
  pidió más.
- **Lo que no miden las cachés** (VmRSS menos cachés, texturas y buffers) pasa de 285 MiB al empezar a ~460, y desde
  la 12.ª galaxia queda en 445-480.
  - La memoria del juego (88 MiB) se va ocupando, y el resto lo pide el driver o el programa.
  - Para separarlo, la línea `[memoria]` trae ahora la memoria anónima, la de archivos y GPU, lo que tiene malloc y el
    swap libre.
- **FPS:** 33.4 mostrados y 12.9 dibujados en 28 galaxias. Las primeras 6 siguen igual que en la segunda serie.
- **El anillo de vértices de 24 MiB** siguió en otro slot 50 veces, en 5 de 155 ventanas de estadísticas. El pico pasó
  de 20 MiB en 8 ventanas.

**Siguiente ajuste:**

| Ajuste | Antes | Ahora |
|---|---|---|
| Display lists | 24 | 16 |
| Vértices decodificados | 24 | 16 (también −8 MiB del buffer residente) |
| `GALAXY_EFB_COPY_DEST_BUDGET_MB` | 64 | 24 (el pico medido: 10 MiB) |

- El lanzador también anota al abrir la memoria del sistema, el swap activo y si el kernel tiene zram.
- **Con 1 GiB y sin swap, el margen es poco** para las cargas más pesadas. Si el kernel tiene zram, un swap comprimido
  en RAM daría margen sin escribir en la SD. Es un cambio del sistema, así que no se hace sin preguntar.

## Pruebas (PC, Mesa)

| Prueba | Resultado |
|---|---|
| `nebula_gles_renderer_tests`: 3.2, hilo de GL, `FORCE_ES30` y `FORCE_ES30` + hilo | 0 fallos, con la prueba nueva del anillo |
| `nebula_gles_blend_tests` plain/fetch y `--comparar` | Cumple |
| `nebula_gl_thread_tests`, `nebula_gles_shader_tests`, `nebula_texture_compact_tests`, `nebula_fifo_display_list_tests` | Pasan |
| `armar.sh` para aarch64 con `NEBULA_MEDICION=1` | Compila; sin datos de la PC |

## En la consola

Con el paquete de medición, la serie corre sola: un recorrido de unos 20-25 minutos.

1. **El cierre:** el recorrido debe pasar CosmosGardenGalaxy. En log.txt debe aparecer
   `[ring] vertex: a frame needed more than 28 MiB …` en vez de `fatal`.
2. **La memoria:** las líneas `[memoria]`, galaxia por galaxia, dicen qué caché crece. Con eso se decide qué limitar.
3. Si el sistema mata el recorrido, el lanzador guarda lo que dice el kernel.
