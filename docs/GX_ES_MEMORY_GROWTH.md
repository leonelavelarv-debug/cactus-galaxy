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

### Con los límites de 16 MiB (31-oct, quinto recorrido)

**Por primera vez, el recorrido terminó las 44 galaxias** (`tour: done`, estado 0, 1642 s). La memoria libre mínima fue
de 48 MiB.

- **Pero fue más lento:**
  - en total, 27.5 FPS mostrados y 10.8 dibujados (antes, 33.4 y 12.9);
  - la CPU del hilo de render, 18.0 ms por frame (antes, 10.8);
  - las galaxias grandes, mucho peor: AstroGalaxy, 4.8 FPS dibujados con 56.9 ms de CPU (antes, 10.0 y 18.2);
    CannonFleet, 8.7 (14.4); Factory, 8.8 (12.2).
  - Con 16 MiB no caben las display lists o los vértices de esas galaxias, y se vuelven a convertir cada frame.
- **Lo que crece es lo que pide malloc:** de 148 MiB al abrir a 290-355 MiB.
  - La memoria anónima va de 164 a 430 MiB, y la de archivos y GPU, entre 260 y 330.
  - No es una fuga: malloc sube en las cargas (FishTunnel +42, OceanRing +82) y baja después (OceanRing 355 →
    PeachCastle 313).
  - Las cachés medidas son solo 32 MiB de eso. El resto es memoria de trabajo de cada galaxia: texturas al decodificar,
    lectura del disco, el driver.
- **Al abrir, el lanzador anotó:** MemTotal 973 MiB, MemAvailable 693, sin swap, zram disponible en el kernel.
  - Activar zram fue descartado: no se toca el sistema.

**Siguiente ajuste (sin tocar el sistema):**

- **Punto medio en las cachés:** display lists a 24 MiB (con 24 iba a la velocidad de antes) y vértices decodificados a
  16 MiB.
- **`malloc_trim(0)` en cada línea `[memoria]`** (cada 60 s jugando, 30 en la serie). Devuelve al sistema las páginas
  libres del montón que glibc retiene después del pico de una carga. La línea dice cuántos MiB devolvió.

### Display lists 24 y vértices 16, con `malloc_trim` (31-oct, sexto recorrido)

**Murió al cargar la 29.ª galaxia** (PeachCastleFinalGalaxy, OOM; `anon-rss` 466 MiB).

- **Igual de lento que con 16 y 16:** AstroGalaxy dio 4.8 FPS dibujados con 57.2 ms de CPU, y CannonFleet, Factory,
  CosmosGarden y Ocean* siguieron lentos.
  - El costo de CPU viene de la caché de vértices a 16 MiB, no de las display lists.
- **`malloc_trim` devolvió poco:** 3-16 MiB cada vez. Lo que ocupa el montón está en uso; no es memoria libre que
  glibc retiene.
- **En OceanRingGalaxy, malloc subió de 280 a 379 MiB** en la carga.

**La causa: los bloques de subida de texturas.**

- `TextureCache` decodifica las texturas en bloques de subida por slot (`UploadArenaFrame`): 16 MiB de entrada por slot,
  4 slots.
- Un frame que sube más (la carga de una galaxia) agrega bloques de 16 MiB, y **esos bloques no se devolvían nunca**:
  solo se volvía al primero.
- En GLES son memoria del montón (`new[]`), así que cada carga grande dejaba ese slot más grande para siempre: hasta 4
  veces el pico de una carga.

**Corrección (`runtime/src/gx/texture_cache.cpp`):**

- `begin_frame` de un slot devuelve los bloques que agregó su frame anterior y se queda con el primero. El slot ya está
  libre de la GPU: el renderer esperó su fence.
- En GLES el bloque de entrada es de 4 MiB, no 16. `glTexSubImage` lee el bloque (o el hilo de GL lo copia a su cola)
  dentro del mismo frame, así que no hace falta más. Ahorra hasta 48 MiB fijos.
- En D3D12 el bloque sigue de 16 MiB; solo cambia que los bloques de más vuelven.
- La línea `[memoria]` trae ahora `subida`: lo que suman esos bloques.
- Con eso, los vértices decodificados vuelven a 24 MiB (la velocidad de antes). Las display lists siguen en 24.

### Con los bloques de subida corregidos (31-oct, séptimo recorrido)

**Las 44 galaxias, a la velocidad normal** (`tour: done`, estado 0, 1342 s):

- en total, 34.1 FPS mostrados y 13.2 dibujados; la CPU del hilo de render, 10.9 ms por frame; 0 errores de GL;
- AstroGalaxy, 9.8 FPS dibujados con 18.1 ms de CPU (como con 24/24 antes);
- OceanRingGalaxy, 12.3; PeachCastleFinalGalaxy, 11.5. En las corridas anteriores ahí moría.

**Memoria:**

- Los bloques de subida se quedaron en 16 MiB todo el recorrido (4 slots × 4 MiB). Antes crecían en cada carga.
- malloc: 107 MiB al abrir (antes 148), 197-283 hasta la 27.ª galaxia, y la memoria libre bajó a 89-122 MiB.
- **Lo que queda:** en la carga de OceanRingGalaxy, malloc vuelve a subir de 247 a 333 MiB y no baja hasta la 39.ª
  galaxia. La memoria libre mínima fue de 36 MiB (informe).
  - Esta vez alcanzó, pero el margen es poco.
  - Esos 86 MiB no son de ninguna caché medida. Falta saber qué los pide.

### Quién pide los 86 MiB de OceanRingGalaxy: `NEBULA_MEMTRACE`

`platform/knulli/game/memtrace_knulli.cpp`, solo en el ejecutable de la consola:

- **Qué es:** el `malloc`, `free`, `calloc`, `realloc`, `memalign`, `aligned_alloc` y `posix_memalign` del ejecutable,
  delante de los de glibc (`__libc_*`), para todo el proceso: el juego, el driver de GL y libstdc++.
  - Se exportan con `--export-dynamic-symbol`, para que los módulos los usen.
- **Qué guarda:** con `NEBULA_MEMTRACE=1`, cada bloque de `NEBULA_MEMTRACE_MIN_KB` (256) o más se anota con su pila
  (`_Unwind_Backtrace`, 10 marcos), en tablas estáticas: 65 536 bloques vivos y 4096 pilas.
- **Qué informa:** después de cada `[memoria]`, las 12 pilas que más tienen vivo: MiB vivos, pico, bloques, total pedido
  y los marcos como módulo+desplazamiento.
  - Con los binarios sin recortar de la misma compilación, llvm-symbolizer los convierte en funciones.
- **Apagado** (lo normal), cada llamada cuesta una lectura y un salto más que la de glibc.
- **Probado en la PC** con un programa aparte: bloques de `new`, `malloc`, `calloc` + `realloc` y `posix_memalign`,
  cuatro hilos liberando y pidiendo a la vez, y la vuelta a 0 al liberar todo. La pila sale bien simbolizada.
- La serie `[memoria]` lo activa.

### Lo que encontró `NEBULA_MEMTRACE` (31-oct, octavo recorrido)

Las 44 galaxias otra vez (estado 0). **Quien tiene los bloques grandes es el driver de la Mali**:
`libmali.so.0+0x53335b`.

| Galaxia | Vivo en ese sitio | Bloques |
|---|---|---|
| Al abrir | 6.7 MiB | 9 |
| CannonFleet (5.ª) | 22.7 | 15 |
| CosmosGarden (7.ª) | 42.7 | 16 |
| Factory (12.ª) | 83.4 | 20 |
| OceanPhantomCave (27.ª) | 44.0 | 14 |
| **OceanRing (28.ª)** | **124.0** | 15 |
| ReverseKingdom (32.ª) al final | 84.0 | 14 |

- **El salto de OceanRing es el driver:** +80 MiB en ese sitio, el mismo que el malloc de 247 → 333 MiB.
- **Los saltos casi se duplican** (+10, +20, +40, +80 MiB), como un buffer que crece al doble cuando no le alcanza y se
  queda en su pico.
- **Coinciden con frames de carga grandes:** en CosmosGarden y Factory el anillo de vértices se pasó de 24 MiB
  en ese mismo momento.
- **Lo segundo:** nuestras cosas, de 8 a 20 MiB cada una (los bloques de subida de texturas, 16; las display lists; un
  bloque de 8 MiB al inicio).
- **El sitio del driver no trae pila:** el desenrollador se detiene en libmali, que no tiene tablas.

**Hipótesis:** el driver arma en el montón la memoria de comandos de cada envío, y crece con los draws de un solo frame.
Una carga de galaxia dibuja miles de draws sin presentar, así que esa memoria sube a su pico y se queda ahí.

**Cambios:**

- **`GALAXY_GLES_FLUSH_DRAWS` (1000 por defecto, 0 lo apaga):** `glFlush` cada 1000 draws dentro de un frame. Los frames
  normales tienen 150-450 draws, así que nunca llegan.
  - La línea `[gles] … frames/s` dice cuántos draws tuvo el frame más grande y cuántos flushes hubo.
  - Probado en la PC con un flush cada 1, 2 y 3 draws (con y sin hilo de GL): las pruebas del renderer y las de mezcla
    dan lo mismo.
- **`NEBULA_MEMTRACE` anota cada bloque de 4 MiB o más al momento:**
  - cuándo, de qué tamaño y en qué hilo;
  - en qué frame y tras cuántos draws (`nebula_memtrace_note`, desde `RendererGles::draw`);
  - con sus primeros marcos.
- **Si el desenrollador se detiene,** memtrace sigue la cadena de registros de marco (x29) dentro de la pila.

### Con el flush cada 1000 draws (31-oct, noveno recorrido)

- **El flush no cambió nada:** el bloque del driver creció igual.
  - Hubo frames de hasta 2137 draws, con decenas de flushes por ventana.
  - El bloque siguió en 5 → 10 → 20 → 40 → 80 MiB, con 123.6 MiB vivos tras OceanRing.
- **Los bloques grandes nuevos se piden en el hilo de GL** (`Nebula GL`), en frames que llevaban solo 7, 26, 186 o 386
  draws.
  - No dependen de los draws de un frame: la hipótesis queda descartada.
  - Los pide el driver desde una llamada de GL. Después de libmali la pila sigue en el inicio del hilo, porque el driver no
    deja registros de marco.
- **`GALAXY_GLES_FLUSH_DRAWS` queda apagado por defecto** (se puede encender con un número). La línea de estadísticas
  sigue diciendo el frame con más draws.
- **Para saber qué llamada es:** el hilo de GL anota qué registro está ejecutando (`nebula_gl_thread_running`, en
  `gl_thread.cpp`), y memtrace lo agrega a cada bloque grande. Es la dirección de la función que ejecuta ese registro, y
  el simbolizador la convierte en el `w_gl…` de origen.

### La llamada: `glDrawElements` (31-oct, décimo recorrido)

Los cinco bloques que se duplican (5, 10, 20, 40, 80 MiB) se pidieron todos con la misma llamada de GL en curso:
`nebula+0x2262a0`. Es el registro del hilo de GL de **`glDrawElements`**
(`OffsetThunk<&real::glDrawElements>`).

**Lectura:**

- Con `glDrawElements`, el driver no sabe qué vértices usa un draw y lo calcula leyendo sus índices.
- Lo que crece al doble y no se libera tiene la forma de una tabla hash propia del driver: guarda esos resultados por
  posición del buffer de índices.
- El anillo de índices cambia de posición cada frame, así que la tabla solo crece. Al llenarse se duplica (la tabla
  nueva es el bloque grande que se ve) y no se vacía nunca.

**Corrección:**

- `RendererGles::draw` usa `glDrawRangeElements(modo, 0, n - 1, …)`, con `n` los vértices de la vista del draw
  (`vertex_view.SizeInBytes / stride`).
- Los índices de GX los genera el cargador de vértices y solo apuntan a los vértices cargados de ese draw o lote (son
  u16, a lo más 65 535).
  - El driver ya no tiene que calcular el rango, y se espera que no llene la tabla.
- **`GALAXY_GLES_DRAW_RANGE=0`** vuelve a `glDrawElements`.
- **`GALAXY_GLES_CHECK_RANGE=1`** (lento, para medir) lee los índices de cada draw.
  - Si alguno pasa de `n - 1`, lo cuenta y lo dibuja con su rango verdadero.
  - La línea `[gles] … frames/s` dice cuántos hubo.
  - La serie `[memoria]` lo activa: tiene que dar 0.
- **Prueba en el renderer:** el triángulo indexado se cubre y se recorta igual que el no indexado. Pasa en 3.2, con hilo
  de GL, en 3.0 con hilo y con la comprobación.

### Con `glDrawRangeElements` (31-oct, undécimo recorrido)

- **Las 44 galaxias pasan** y la comprobación da **0 draws con índices fuera de sus vértices** en las 250 ventanas: el
  rango que se pasa es correcto.
- **El bloque del driver creció igual** (5 → 10 → 20 → 40 → 80 MiB), ahora dentro de `glDrawRangeElements`. No era el
  cálculo del rango de índices.
- **La forma no cambia:** algo se guarda por cada draw, o por cada desplazamiento distinto que se le da, y la tabla se
  duplica al llenarse.
- **Lo que cambia en cada draw:**
  - el desplazamiento del buffer de vértices (`glBindVertexBuffer`);
  - el de índices;
  - los de las constantes (`glBindBufferRange`).
  - Todos salen de anillos y son distintos cada frame.

**Prueba del primero, `GALAXY_GLES_VERTEX_BASE=1`:**

- El buffer de vértices se ata en `desplazamiento mod stride`: con stride 144 y alineación 16, solo 9 desplazamientos
  por buffer.
- El resto va como vértice base: `glDrawRangeElementsBaseVertex`, o el primero en `glDrawArrays`.
- En la PC da lo mismo: pruebas del renderer (3.2, hilo de GL, comprobación de rango) y de mezcla, con imágenes
  idénticas.
- La serie corre dos recorridos de 8 galaxias, sin y con el cambio, para ver si el bloque deja de doblarse.

### Con `GALAXY_GLES_VERTEX_BASE=1` (31-oct, dos recorridos de 8 galaxias)

| | 5 MiB | 10 MiB | 20 MiB | 40 MiB |
|---|---|---|---|---|
| Sin cambios (`rango`) | frame 3719 | 18 297 | 23 991 | — |
| Vértices en pocos desplazamientos (`base`) | frame 1150 | 2322 | 20 538 | 26 168 |

- **Con el vértice base, el bloque crece antes, no después.** Lo que guarda el driver no depende del desplazamiento del
  buffer de vértices.
  - Con vértice base crece más rápido. Puede ser que el driver lo guarde como un dato más de cada draw.
  - `GALAXY_GLES_VERTEX_BASE` queda apagado.
- **Queda el buffer de índices** (el de las constantes, después).
  - Los drivers de Mali guardan datos por buffer de índices y los invalidan cuando el buffer cambia por la API.
  - Un buffer mapeado de forma persistente cambia sin que el driver se entere, y lo guardado no se invalida nunca.

**Dos pruebas, una por recorrido:**

- **`GALAXY_GLES_INDEX_COPY=1`:** el anillo de índices con copia en la CPU, como en OpenGL ES 3.0. Cada rango escrito se
  manda con `glMapBufferRange` + `glUnmapBuffer`, así que el driver ve los cambios.
  - `tracking_writes()` se activa si hay un buffer así.
- **`GALAXY_GLES_INDEX_RECYCLE=3000`:** un anillo de índices nuevo cada 3000 frames.
  - El viejo se borra con la eliminación diferida, después de un fence posterior a todos los frames que lo usaron.
  - Con él, el driver suelta lo que guardaba.
- **Probado en la PC** (renderer tests): con copia, con hilo de GL, un anillo nuevo cada frame y cada 2 frames con
  copia y comprobación de rango. Todo pasa.

### Era el buffer de índices (31-oct, dos recorridos de 8 galaxias)

| Recorrido (8 galaxias) | Bloques que se duplican en el driver | malloc al final | Libre al final |
|---|---|---|---|
| `rango` (antes, la serie anterior) | 5, 10 y 20 MiB (frames 3719, 18 297, 23 991) | 238 MiB | 160 MiB |
| **`copia`** (`GALAXY_GLES_INDEX_COPY=1`) | **ninguno** | 215 MiB | 191 MiB |
| `renueva` (`GALAXY_GLES_INDEX_RECYCLE=3000`, 9 anillos nuevos) | uno de 5 MiB, en el frame 22 809 | 212 MiB | 176 MiB |

- **Con la copia en la CPU, el bloque no aparece nunca.** El driver de la Mali guardaba datos de cada rango del buffer de
  índices mapeado de forma persistente. Como no veía los cambios, no los soltaba.
  - Con los cambios mandados por la API, invalida lo que guardó.
  - Con un buffer nuevo cada 3000 frames, lo suelta al borrarlo.
- **Rendimiento:** en los cuatro recorridos, el 65 % de los frames saltados. La GPU, 2.5-2.6 ms por frame presentado.
  El hilo de render, 436-441 ms/s. Sin diferencias medibles.
- **Queda por defecto la copia** (`GALAXY_GLES_INDEX_COPY=0` vuelve al buffer mapeado).
  - Los índices son pocos KiB por draw.
  - La copia en la CPU es `calloc`: ocupa lo que se escribe del anillo de 12 MiB.
- `GALAXY_GLES_INDEX_RECYCLE` queda como opción, apagada.

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
