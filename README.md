# 🌵🌌 Cactus Galaxy Wrapper + Nebula para portátiles ARM64

**Super Mario Galaxy** nativo en consolas retro portátiles ARM64: la Anbernic RG35XX H y sus hermanas con Allwinner
H700, y la R36S con Rockchip RK3326. Funciona con **KNULLI**, **muOS**, **ArkOS**, **dArkOS** y **ROCKNIX**, como port
de PortMaster.

No es un emulador. Es el código descompilado del juego ([Petari](https://github.com/SMGCommunity/Petari)) compilado
para ARM64 sobre el host y el renderer de [Nebula](https://github.com/keepvaibin/nebula), con los gráficos de la Wii
traducidos a OpenGL ES 3.2 para la GPU Mali-G31. **Cactus Galaxy Wrapper** es el asistente que, en la propia consola,
convierte tu disco en el `game.pak` que juega el port: no hace falta PC, Termux ni red.

![Estado](https://img.shields.io/badge/Estado-En%20desarrollo-orange)
![Plataforma](https://img.shields.io/badge/Plataforma-ARM64%20%2F%20PortMaster-blue)
![GPU](https://img.shields.io/badge/GPU-OpenGL%20ES%203.2%20(Mali--G31)-green)
![Firmwares](https://img.shields.io/badge/CFW-KNULLI%20%C2%B7%20muOS%20%C2%B7%20ArkOS%20%C2%B7%20dArkOS%20%C2%B7%20ROCKNIX-lightgrey)
![Licencia](https://img.shields.io/badge/Licencia-GPL--3.0-red)

> Sin relación con Nintendo ni respaldo suyo. El paquete no trae código ni datos de Nintendo: el juego sale de **tu**
> disco.

---

## 📟 Consolas y firmwares

Los controles están pensados para consolas con **dos sticks analógicos y cruceta**.

| Consola | SoC / GPU | Firmwares | Estado |
|---|---|---|---|
| Anbernic RG35XX H | Allwinner H700 (4× Cortex-A53 a 1.5 GHz), Mali-G31 | KNULLI, muOS, ROCKNIX | ✅ Probado en KNULLI |
| RG35XX Pro, RG40XX H y otras H700 con dos sticks | Allwinner H700, Mali-G31 | KNULLI, muOS, ROCKNIX | 🟡 Sin probar (mismo hardware) |
| R36S y clones | Rockchip RK3326 (4× Cortex-A35 a 1.3 GHz), Mali-G31 | ArkOS, dArkOS, ROCKNIX | 🟡 Sin probar |

En la R36S la CPU rinde aproximadamente un 65–75 % de la de la H700. Con el driver Mali de ArkOS y dArkOS (OpenGL ES
3.2) debería funcionar igual. Con Panfrost (ROCKNIX, OpenGL ES 3.1) el renderer cambia a GLSL ES 3.10 y, si el driver
no da `GL_EXT_buffer_storage`, usa copias en la CPU (ya no es un requisito). Sin probar en ninguno de los dos.

---

## 🚀 Características principales

* **Conversión en la consola:** ISO, RVZ, WIA, WBFS (también partido), GCZ y CISO → `smg/gamedata/game.pak`, con
  `nebula-pak` nativo en C++. Muestra avance, velocidad y tiempo restante, comprueba el espacio libre y si la tarjeta
  es FAT32, y se puede cancelar sin dejar restos.
* **Cuatro idiomas de juego:** español, inglés, francés y **portugués de Brasil**. El PT-BR se hace aplicando al vuelo
  el parche xdelta de la traducción mientras se convierte (en RVZ/WIA vuelve a cifrar solo los bloques que cambian),
  sin escribir una ISO intermedia.
* **Interfaz en el idioma del sistema:** el wrapper se muestra en español, inglés, francés, portugués, italiano o
  alemán, según el firmware.
* **Un cactus de bienvenida:** la primera vez que arranca el juego, un saguaro con lentes cool saluda en el idioma
  elegido (*¡Viva México, Viva Sonora, Cabrones!*), y el juego empieza al pulsar A.
* **Perfil de bajos recursos:** salto de frames adaptativo (ritmo estable de 60/30/20 imágenes por segundo), lecturas
  del EFB sin esperar a la GPU, nitidez en la ampliación, cachés más pequeñas, y CPU/GPU en `performance` solo
  mientras juegas.
* **Controles según el firmware:** la combinación para salir se adapta a cada uno (MENU + START en KNULLI/muOS,
  SELECT + START en ArkOS/dArkOS), y A/B se pueden poner por posición o por etiqueta. Usa también la
  `gamecontrollerdb` de PortMaster.
* **Ficha y portada en cada menú:** descripción, género y desarrollador en el idioma del juego, con la portada de
  libretro-thumbnails. Va a la API de EmulationStation (KNULLI, ROCKNIX), a `gamelist.xml` (ArkOS, dArkOS) o al
  catálogo de muOS.
* **ULTRA-GLES:** medición del rendimiento gráfico en la consola. `NEBULA_ULTRA=bench` recorre las galaxias solo y
  deja un informe por galaxia (FPS, tiempo por fotograma p95/p99, tirones y su causa, CPU, GPU, memoria) y
  `smg/ultra/compare.py` compara dos ajustes (ver `smg/ultra/LEEME.txt`).
* **Instalador autoextraíble:** detecta el firmware y pone cada parte en su sitio. Al reinstalar conserva tu
  `game.pak`, el idioma y las partidas.

---

## 🧩 GX en OpenGL ES y compatibilidad de drivers

El GX del Wii se dibuja con un renderer OpenGL ES propio (`runtime/src/gx/gles/`). Pide **OpenGL ES 3.0 como mínimo** y
usa lo de 3.1/3.2 y `GL_EXT_buffer_storage` solo si el driver lo tiene.

| Driver | Qué usa Nebula | Estado |
|---|---|---|
| Mali-G31, libmali r20p0 (RG35XX H, KNULLI): OpenGL ES 3.2 | GLSL ES 3.20, anillos de subida mapeados de forma persistente, hilo de GL | ✅ Juego completo de las 44 galaxias sin cierres |
| Un driver OpenGL ES 3.1 (Panfrost en ROCKNIX) | GLSL ES 3.10 | 🟡 Sin probar |
| Un driver solo OpenGL ES 3.0 | GLSL ES 3.00, buffers con copia en la CPU, matrices en una textura 2D | 🟡 Probado forzando ese camino en la Mali (`GALAXY_GLES_FORCE_ES30=1`): pasan las pruebas del renderer y de mezcla; jugar entero no está medido |

**Mezcla de GX con alfa de doble fuente.** OpenGL ES no tiene `GL_EXT_blend_func_extended` (la Mali tampoco): 241 de los
977 programas de Galaxy la necesitan. Nebula hace la mezcla en el shader leyendo el color de destino con
`GL_ARM_shader_framebuffer_fetch` (o `GL_EXT_shader_framebuffer_fetch`). En la Mali de la RG35XX H, comparada con la
mezcla de la CPU: 0 píxeles con diferencia mayor a 1 y 0 % de draws en el shader genérico. Sin ninguna de esas dos
extensiones, vuelve a la mezcla fija de antes, que en 35 de esos 241 programas usa la alfa constante en vez de la del
TEV. `GALAXY_GLES_BLEND_FETCH=0` la apaga.

**Memoria (1 GB compartido con la GPU).** El anillo de índices se sube desde una copia en la CPU, porque el driver de la
Mali crecía una tabla por cada rango de un buffer mapeado de forma persistente. Los bloques de subida de texturas
se devuelven al empezar cada frame y las cachés tienen límite (display lists 24 MiB, vértices decodificados 24 MiB,
texturas recicladas 8 MiB). Resultado en la consola, en un recorrido de las 44 galaxias: el máximo del montón bajó de
379 MiB a 233 MiB y el mínimo de memoria libre subió de 36 MiB a 138 MiB.

**La biblioteca de shaders** se hace en la PC (`smg/shaders-gles.bin`, 4894 shaders en cinco formas GLSL), así que la
consola no lleva traductor. Si falta un programa, se dibuja con el genérico y la consola anota la clave en
`smg/data/shadercache/RMGE01/gles_keys.bin` para la siguiente compilación.

Detalles y pruebas: [`docs/GX-ES.md`](docs/GX-ES.md), [`docs/GX_ES_COMPATIBILITY.md`](docs/GX_ES_COMPATIBILITY.md),
[`docs/GX_ES_FRAMEBUFFER_FETCH.md`](docs/GX_ES_FRAMEBUFFER_FETCH.md), [`docs/GX_ES_MEMORY_GROWTH.md`](docs/GX_ES_MEMORY_GROWTH.md)
y [`docs/PHASE3_PROGRESS.md`](docs/PHASE3_PROGRESS.md).

---

## 📋 Requisitos previos

1. **PortMaster** instalado en la consola (da `control.txt`, las rutas y los mandos).
2. **Tu disco de Super Mario Galaxy USA (`RMGE01`)** en ISO, RVZ, WIA, WBFS, GCZ o CISO. Otras regiones no sirven: el
   wrapper las rechaza.
3. **Espacio en la tarjeta:** unos 25 MB para el port, más la imagen del disco (~4.4 GB en ISO, menos en RVZ), más
   `game.pak`: **~3.4 GB libres en ext4** o **~4.3 GB en exFAT**. FAT32 no sirve, porque no admite archivos de más de
   4 GB. Si al terminar no quieres conservar la imagen, el lanzador la puede borrar (ver *Ajustes*).
4. Para la portada en el menú hace falta red una vez. La conversión y el juego funcionan sin red.

---

## ⚙️ Instalación

1. Descarga la última versión desde [Releases](../../releases): `Instalar Super Mario Galaxy.sh` (instalador) o
   `smg.zip` (formato de PortMaster).
2. **Con el instalador:** cópialo a la carpeta de ports de tu firmware y ábrelo desde el menú **Ports**:

   | Firmware | Dónde copiar el instalador | Dónde queda el juego (`smg/`) |
   |---|---|---|
   | KNULLI, Batocera | `/userdata/roms/ports/` (`roms/ports` en la SD) | `/userdata/roms/ports/smg/` |
   | muOS | `/mnt/mmc/ROMS/Ports/` (o `/mnt/sdcard/ROMS/Ports/` en la SD 2) | `/mnt/mmc/ports/smg/` (o `/mnt/sdcard/ports/smg/`) |
   | ArkOS, dArkOS | `/roms/ports/` (`ports` en EASYROMS; `/roms2/ports/` con dos SD) | `/roms/ports/smg/` |
   | ROCKNIX, AmberELEC | `/storage/roms/ports/` (`roms/ports` en la SD) | `/storage/roms/ports/smg/` |

   El lanzador `Super Mario Galaxy.sh` se queda donde el menú lo lista; en muOS eso es `ROMS/Ports/`. Al terminar, el
   instalador se borra solo y actualiza la lista. En ArkOS y dArkOS lo hace reiniciando EmulationStation.
3. **Con PortMaster:** deja `smg.zip` en la carpeta `autoinstall/` (dentro de la carpeta de ports o de PortMaster) y
   abre PortMaster, que lo instala en las mismas rutas.
4. **A mano:** en KNULLI, ArkOS, dArkOS y ROCKNIX basta con descomprimir `smg.zip` dentro de `ports`. En muOS, `smg/` va
   a `ports/` y `Super Mario Galaxy.sh` a `ROMS/Ports/`.

---

## 🎮 Primer arranque y uso

1. Copia tu imagen del disco a `smg/gamedata/` o a `roms/wii/`.
2. Abre **Super Mario Galaxy** desde Ports. Como aún no hay `game.pak`, se abre **Cactus Galaxy Wrapper**:
   * Lista los discos que encuentra y comprueba que sean `RMGE01`. Si copias la imagen con el wrapper abierto, la
     detecta al momento.
   * Eliges el idioma del juego (◄ ► / A).
   * Convierte el disco con pantalla de progreso en tiempo real. Tarda lo que la tarjeta en escribir unos 4 GB.
   * ⚠️ **No apagues la consola ni saques la tarjeta mientras convierte.** Si hace falta parar, B cancela sin dejar
     restos.
3. Al terminar, el cactus con lentes te da la bienvenida: **pulsa A** y empieza el juego.
4. Las siguientes veces el juego arranca directamente. **Mantén SELECT (o L1) al abrirlo** para volver al wrapper y
   cambiar el idioma.

La primera vez que aparece cada efecto se compila su shader y el juego se detiene un instante. Se guardan en
`smg/data/shadercache`, así que la siguiente vez cargan sin parón.

### Controles (perfil WII HYBRID – RG35XX H)

Es el perfil `smg/config/input/wii/mario_galaxy.ini` (se elige con `GALAXY_INPUT_PROFILE`; `off` vuelve a los controles
de antes). Para cambiar un botón, edita ese archivo: son las mismas líneas `bind.switch_pro.*` y `tune.switch_pro.*` de
`smg/settings.ini`.

| Consola | Wii |
|---|---|
| Stick izquierdo | Stick del Nunchuk |
| B (abajo) | A: saltar |
| A (derecha), R2 | B: trozos de estrella (y volver en los menús) |
| R1 | Giro: sacude el Nunchuk (pulso sintético) |
| L1 | C (centrar la cámara) |
| L2 | Z (agacharse) |
| X, Y | 1, 2 |
| Stick derecho | Puntero (solo puntero); su clic lo centra |
| Cruceta | Cruceta (cámara) |
| START / SELECT | + / − |
| **MENU + START** (KNULLI, muOS) · **SELECT + START** (ArkOS, dArkOS) | Salir |

### Ajustes del lanzador

Se ponen al principio de `Super Mario Galaxy.sh`:

| Variable | Valores | Qué hace |
|---|---|---|
| `NEBULA_PERFIL` | `medio` (por defecto) · `bajo` · `normal` | `medio`: resolución interna fija de 75 % (480x396), con salto de frames y cachés pequeñas; `bajo`: igual pero con resolución dinámica 50-100 %; `normal`: 100 % y sin saltos. El medio no está medido en la consola |
| `GALAXY_FRAMESKIP` | `auto` · `0`–`3` | Salto de frames; `auto` mantiene 60, 30 o 20 imágenes por segundo |
| `GALAXY_PEEK_MODE` | `async` · `sync` · `off` | Lecturas de la imagen (destellos del sol, puntero) sin esperar, exactas o ninguna |
| `NEBULA_FACE_LAYOUT` | `positions` · `labels` | A/B por posición o por etiqueta (si en tu firmware salen cambiados) |
| `NEBULA_QUIT_COMBO` | `menu` · `select` · `both` | Fuerza la combinación para salir |
| `NEBULA_PAD_SCHEME` | `xbox` | Cambia A y B de lado |
| `GALAXY_INPUT_PROFILE` | `mario_galaxy` (por defecto) · `default` · `off` | Perfil de mando Wii: `default` es la base de cualquier juego (R1 sacude el Wiimote, el puntero no se oculta); `mario_galaxy` la ajusta a este juego; `off`: los controles de antes |
| `GALAXY_SHAKE` | `nunchuk` · `remote` · `both` | Solo con `GALAXY_INPUT_PROFILE=off`: qué acelerómetro virtual sacude el giro (X y R2 al Nunchuk, Y al Wiimote) |
| `GALAXY_SHAKE_REPORTS` | `4`–`60` (12 por defecto) | Duración de la sacudida del Nunchuk en reportes de 10 ms; súbelo a 20 o 30 si el giro falla a veces |
| `NEBULA_PAK_DELETE_IMAGE` | `1` | Borra la imagen del disco al terminar de convertir |

### Diagnóstico

Si algo falla, manda `smg/log.txt`, la carpeta `smg/crashes/` y alguna captura de `smg/shots/` (se toma una cada
30 s). La primera línea del log dice el firmware, la consola y el perfil de controles que se aplicó.

---

## 🛠️ Compilación desde el código fuente (para desarrolladores)

El detalle (renderer GLES, perfil bajo, pruebas) está en
[`docs/RG35XXH.md`](docs/RG35XXH.md).

```bash
# Clona el repositorio
git clone <la dirección de este repositorio> cactus-galaxy
cd cactus-galaxy

# nebula-pak (el convertidor) y sus pruebas en el PC: ISO, GCZ, RVZ y el parche xdelta
cmake -S platform/knulli/pak -B build-pak -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-pak
XDELTA3="$(command -v xdelta3)" platform/knulli/pak/tests/run_tests.sh build-pak/nebula-pak /tmp/paktest "$(command -v zstd)"

# nebula-pak y Cactus Galaxy Wrapper para la consola (aarch64; el wrapper necesita un SDL2 de aarch64)
cmake -S platform/knulli/pak -B build-pak-a64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DCMAKE_C_FLAGS=-mcpu=cortex-a53 -DCMAKE_CXX_FLAGS=-mcpu=cortex-a53 \
  -DCACTUS_SDL2_PREFIX=/ruta/a/sdl2-aarch64
cmake --build build-pak-a64

# El juego (módulo y host) para aarch64, con glibc <= 2.27 para que corra en todos los firmwares
AARCH64_XTOOLS=/ruta/x-tools/aarch64-rpi3-linux-gnu cmake -S native -B build-aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=platform/knulli/aarch64-toolchain.cmake \
  "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -static-libstdc++ -static-libgcc" \
  "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -static-libstdc++ -static-libgcc"
ninja -C build-aarch64

# Empaquetar: smg.zip (PortMaster) e "Instalar Super Mario Galaxy.sh" desde una carpeta con el lanzador y smg/
platform/knulli/smg/empaquetar.sh <carpeta-del-port> <carpeta-de-salida>
```

El workflow [`nebula-pak (consola ARM64)`](.github/workflows/nebula-pak-consola.yml) pasa las pruebas de
`nebula-pak` en x86-64 y compila el binario aarch64 en cada cambio.

### Estructura

| Carpeta | Qué es |
|---|---|
| `platform/knulli/pak/` | `nebula-pak`: convertidor nativo de discos a `game.pak` (ISO/WBFS/CISO/GCZ/WIA/RVZ, parches xdelta) |
| `platform/knulli/cactus/` | Cactus Galaxy Wrapper: interfaz SDL2 dibujada a mano a 640x480, con textos en 6 idiomas |
| `platform/knulli/game/` | Host de la consola (ventana, mandos, perfiles por firmware) y pruebas del renderer GLES |
| `platform/knulli/smg/` | El port: lanzador, `gamelist.py` (ficha por firmware), `port.json`, instalador y `empaquetar.sh` |
| `runtime/` | Runtime de Nebula; el renderer GX en OpenGL ES está en `runtime/src/gx/gles/` |
| `native/` | El juego compilado nativo (módulo y host) |
| `platform/knulli/` (resto) | Shim POSIX, Nebula Probe, NXAccel, sondas PowerPC y `build.sh`: ver [`platform/knulli/README.md`](platform/knulli/README.md) |
| `docs/` | [`RG35XXH.md`](docs/RG35XXH.md) (detalles del port) y [`NEBULA.md`](docs/NEBULA.md) (el README original de Nebula) |

---|---|
| `pak/` | `nebula-pak`: convertidor nativo de discos a `game.pak` (ISO/WBFS/CISO/GCZ/WIA/RVZ, parches xdelta) |
| `cactus/` | Cactus Galaxy Wrapper: interfaz SDL2 dibujada a mano a 640x480, con textos en 6 idiomas |
| `game/` | Host de la consola (ventana, mandos, perfiles por firmware) y pruebas del renderer GLES |
| `smg/` | El port: lanzador, `gamelist.py` (ficha por firmware), `port.json`, instalador y `empaquetar.sh` |
| `compat/` | Shim POSIX para los nombres de la CRT de MSVC que usa el runtime |
| `probe/` | Nebula Probe: port de PortMaster que mide la memoria, la CPU y la GPU del aparato |
| `nxaccel/` | NXAccel: código nativo para el preparador del port NextOS de Mario Kart Wii |
| `recomp-probes/`, `tools/ppc-lower/` | Sondas PowerPC y herramientas sobre `nebula-recomp-core` |
| `build.sh` | Compila la sonda y NXAccel para aarch64, y sus zips de PortMaster |

---

## 📜 Créditos y licencia

* **Nebula** (recompilador, host, runtime y renderer GX), de [keepvaibin](https://github.com/keepvaibin/nebula), GPL-3.0.
  Este repositorio parte de Nebula, con su historial completo (su README original está en
  [`docs/NEBULA.md`](docs/NEBULA.md)), y también es GPL-3.0.
* **Petari**, la descompilación de Super Mario Galaxy, de [SMGCommunity](https://github.com/SMGCommunity/Petari), CC0.
* glslang (BSD/Apache-2.0), SPIRV-Cross y SPIRV-Tools (Apache-2.0), zlib, liblzma (0BSD), zstd (BSD), bzip2 y
  font8x8 (dominio público). Sus licencias van en `smg/licenses/` y en
  [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
* **Traducción al portugués de Brasil** (v1.0, 2012): de **Brazilian Warriors** y **Nintendo BR**. Autores: Ray, xujozer, Joelson Kamppello, Angel Forgotten y ERT_Ganon.
  Se distribuye en [João13 Traduções](https://joao13traducoes.com/2018/01/wii-super-mario-galaxy-brazilian-warriors-e-nintendo-br/). El paquete trae el parche en formato xdelta, pero no está en el
  repositorio; todo el mérito de la traducción es de sus autores.
* Port, wrapper e instalador hechos con agentes de IA (Claude, de Anthropic), dirigidos y probados por
  🌵 **Señor Cactus**, igual que Nebula. La lógica del juego es la original:
  la de la descompilación.

*Super Mario Galaxy es una marca de Nintendo. Este proyecto no está afiliado a Nintendo.*
