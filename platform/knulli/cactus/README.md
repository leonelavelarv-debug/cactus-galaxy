# Cactus Galaxy Wrapper

La interfaz de la consola (RG35XX H, KNULLI) que convierte el disco de Super Mario Galaxy del jugador en el
`game.pak` del port: un desierto de noche bajo una galaxia, con un saguaro que crece con el avance y florece al
terminar. La conversión la hace `nebula-pak` (`platform/knulli/pak`) como proceso aparte.

- Va dentro del port: el lanzador de Super Mario Galaxy la abre (`--setup`) la primera vez (sin `game.pak` o sin
  idioma elegido) y cuando se mantiene SELECT o L1 al abrir el juego (`--held` lo mira en 0,35 s). Sin `game.pak`
  pide el disco y lo detecta en cuanto se copia; con un único disco válido va directo al idioma y lo convierte; con
  `game.pak` ofrece los idiomas del juego y las traducciones (A: jugar; una traducción nueva hace antes su mod). Salida 0: arrancar el juego; 1: volver sin jugar; 3: sin pantalla.
- Busca discos en `smg/gamedata/`, `smg/` y `roms/wii/`; los comprueba con `nebula-pak --identify`.
- Idioma del juego: al convertir pregunta Español, English o Français (lo que trae el disco USA) y lo guarda en
  `smg/idioma.txt`, que el lanzador del juego pasa como `GALAXY_LANGUAGE`. En el menú, ◄► lo cambia en cualquier
  momento, también con el `game.pak` ya hecho.
- Traducciones como mods, que se activan y desactivan al instante: `game.pak` es siempre el del disco, con sus tres
  idiomas, y cada traducción es un mod en `smg/patches/idioma-<clave>` (los archivos que cambia). Al jugar, el
  lanzador pone en `smg/mods/` (donde el juego lee los mods) solo el del idioma elegido; con Español, English o
  Français no queda ninguno: el juego tal cual (la pantalla de idiomas lo dice: "Sin parches: original"). El idioma
  que cambia cada mod va en `smg/patches/idioma-<clave>.ranura` (inglés salvo que el parche diga otro) y
  `smg/idioma.txt` queda como `english ptbr` o `english ru`. La primera vez que se elige una traducción, su mod se
  hace (A: "Se prepara el parche"); después elegirla o quitarla no cuesta nada, también con ◄► en el menú.
- Português do Brasil (traducción de Brazilian Warriors y Nintendo BR, en [João13 Traduções](https://joao13traducoes.com/2018/01/wii-super-mario-galaxy-brazilian-warriors-e-nintendo-br/)): con un .xdelta de la traducción en `smg/patches/` (nombre con PT-BR/ptbr/portugues), aparece
  "Português". Su mod lo hace `nebula-pak --patch-mod` desde el disco (una vez; en una instalación nueva, justo
  después de convertirlo, y el disco se borra después si se pidió): los archivos del disco parcheado que cambian
  (ISO, RVZ, WIA, WBFS, GCZ o CISO; sin ISO intermedio) y el idioma del juego que sustituye (`PATCH_LANGUAGE`).
  Un `game.pak` hecho antes con el parche dentro (`gamedata/game.pak.idioma`) sigue sirviendo; el idioma que el
  parche tapó se recupera convirtiendo el disco otra vez, ya sin parche.
- Ruso (Russkiy, en letras latinas: la fuente de la interfaz no tiene cirílico), con "rus", "russian", "рус" o "ru"
  como palabra aparte en el nombre: un `.rusmg`, el .zip de la traducción de Switch o un .xdelta del disco USA (el
  ruso de reWiision de 2010 es del disco PAL y no aplica tal cual). El `.rusmg` es lo que `cactus-ruso --extraer`
  saca del .zip de Switch (traducción de reWiision —2MON, ReD— adaptada por СаХаР y N69): solo el texto ruso y las
  letras que usa, sin los archivos del juego que trae el .zip; es lo que lleva el port. Con él (o el .zip), el mod lo
  hace `cactus-ruso` desde `game.pak` en unos segundos, sin el disco: el Message.arc inglés con cada mensaje que la
  traducción tiene (por su nombre) en ruso, con sus etiquetas pasadas a Wii, y los demás en inglés; y a cada fuente
  del juego las letras que el ruso usa y le faltan (cirílico, «», …), reducidas a su celda, en hojas nuevas. La
  pantalla de Game Over sigue en inglés. Con las dos traducciones, la lista de idiomas muestra solo los nombres.
- Antes del primer arranque del juego (después de la primera conversión) saluda un cactus con lentes de sol:
  "¡Viva México, Viva Sonora, Cabrones! En breve iniciará el juego." en el idioma elegido para el juego (español,
  inglés, francés, portugués o ruso en letras latinas; "Cabrones" no se traduce), con "Jugar ya" también en ese idioma. El juego solo
  empieza al pulsar A (aquí y en "¡LISTO!"). Solo una vez: `smg/.primer-arranque` lo recuerda.
- Idioma de la interfaz: el de KNULLI (`system.language` de `/userdata/system/batocera.conf`; si no, el de
  EmulationStation o `LANG`): español, English, français, português, italiano o Deutsch; si no se encuentra, español.
  `CACTUS_LANG=xx` o `--lang xx` lo fijan. Los errores habituales de nebula-pak (espacio, FAT32, imagen dañada,
  main.dol, cancelado) también salen traducidos; en una instalación nueva, el idioma del juego propuesto sigue al
  de la interfaz (español, francés o, para el resto, inglés).
- Controles: cruceta para elegir, A (derecha) convertir, B (abajo) salir o cancelar, X borrar el disco al terminar,
  MENU + START salir. `CACTUS_SWAP_AB=1` cambia A y B.
- `cactus-galaxy-wrapper --demo <carpeta>` escribe capturas PNG de cada pantalla y los fotogramas del cactus
  creciendo (sin pantalla ni SDL).

Se compila con `platform/knulli/pak/CMakeLists.txt` y `-DCACTUS_SDL2_PREFIX=<SDL2 para aarch64>`. Dibuja a
640x480 en memoria y lo muestra con SDL2 (la `libSDL2-2.0.so.0` de la consola).

Fuente: font8x8 de Daniel Hepper (<https://github.com/dhepper/font8x8>, revisión `8e279d2d`, dominio público;
`font8x8/README`), basada en las fuentes VGA de IBM, también de dominio público.

En la pantalla final (la despedida antes del primer arranque) va el logo de Señor Cactus, el autor del port, encima de
"Jugar ya", igual en los cinco idiomas (`logo_senor_cactus.h`: 104×104 RGBA cuadrado, generado desde su logo;
solo píxeles, sin metadatos de la imagen original). La frase dice "¡Viva México, Viva Sonora, Cabrones!" en
español y solo los dos vivas en inglés, francés, portugués y ruso.
