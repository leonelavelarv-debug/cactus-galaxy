# Cactus Galaxy Wrapper

La interfaz de la consola (RG35XX H, KNULLI) que convierte el disco de Super Mario Galaxy del jugador en el
`game.pak` del port: un desierto de noche bajo una galaxia, con un saguaro que crece con el avance y florece al
terminar. La conversión la hace `nebula-pak` (`platform/knulli/pak`) como proceso aparte.

- Va dentro del port: el lanzador de Super Mario Galaxy la abre (`--setup`) la primera vez (sin `game.pak` o sin
  idioma elegido) y cuando se mantiene SELECT o L1 al abrir el juego (`--held` lo mira en 0,35 s). Sin `game.pak`
  pide el disco y lo detecta en cuanto se copia; con un único disco válido va directo al idioma y lo convierte; con
  `game.pak` ofrece los idiomas del juego (A: jugar; los que necesitan otro game.pak, una traducción o el idioma que
  esta sustituyó, lo rehacen desde el disco). Salida 0: arrancar el juego; 1: volver sin jugar; 3: sin pantalla.
- Busca discos en `smg/gamedata/`, `smg/` y `roms/wii/`; los comprueba con `nebula-pak --identify`.
- Idioma del juego: al convertir pregunta Español, English o Français (lo que trae el disco USA) y lo guarda en
  `smg/idioma.txt`, que el lanzador del juego pasa como `GALAXY_LANGUAGE`. En el menú, ◄► lo cambia en cualquier
  momento, también con el `game.pak` ya hecho.
- Português do Brasil (traducción de Brazilian Warriors y Nintendo BR, en [João13 Traduções](https://joao13traducoes.com/2018/01/wii-super-mario-galaxy-brazilian-warriors-e-nintendo-br/)): con un .xdelta de la traducción en `smg/patches/` (nombre con PT-BR/ptbr/portugues), el
  idioma "Português" aparece al instalar; elegido, `nebula-pak --patch` aplica el parche al disco mientras hace
  game.pak (ISO, RVZ, WIA, WBFS, GCZ o CISO; sin ISO intermedio). nebula-pak dice qué idioma del juego sustituye la
  traducción (`PATCH_LANGUAGE`: los ficheros cambiados en UsEnglish/UsSpanish/UsFrench); `smg/idioma.txt` queda
  como `english ptbr` y `gamedata/game.pak.idioma` como `ptbr english`. En el menú, ◄► ofrece entonces Português y
  los otros dos idiomas.
- Ruso, igual que el portugués: con un .xdelta en `smg/patches/` cuyo nombre lleve "rus", "russian", "рус" o "ru"
  como palabra aparte (`smg-ru.xdelta`, `SMG_RU_v1.xdelta`), aparece "Russkiy" (en letras latinas: la fuente de la
  interfaz no tiene cirílico); `smg/idioma.txt` queda como `english ru` y `game.pak.idioma` como `ru english`. Con
  las dos traducciones, la lista de idiomas muestra solo los nombres, más juntos. El parche tiene que ser del disco
  USA (RMGE01), como el de PT-BR: el ruso de reWiision (2010) está hecho para el disco PAL y no aplica tal cual.
- Ruso desde la traducción de Switch (el .zip tal como se descarga, p. ej. `Super_Mario_Galaxy_Switch_2025_RUS.zip`,
  en `smg/patches/`, con "rus" o "ru" en el nombre; traducción de reWiision —2MON, ReD— adaptada a Switch por СаХаР
  y N69): sus archivos son del Switch (orden de bytes al revés, letras de alta resolución, unos 55 MB), así que no
  se copian. Al elegir "Russkiy", `cactus-ruso` (platform/knulli/pak) toma de `game.pak` el Message.arc inglés y el
  Font.arc del juego y hace un mod en `smg/patches/ruso-switch`: cada mensaje que la traducción tiene (por su nombre)
  en ruso, con sus etiquetas pasadas a Wii, y los demás en inglés; a cada fuente del juego le agrega las letras que
  el ruso usa y le faltan (cirílico, «», …), reducidas a su celda, en hojas nuevas. El `game.pak` no cambia y no hace
  falta el disco si ya existe (unos segundos). `smg/idioma.txt` queda como `english ru` y el lanzador mueve el mod a
  `smg/mods/` (donde el juego lee los mods) solo con ese idioma; con otro lo devuelve a `smg/patches/`. Si hay
  también un .xdelta ruso, se usa el .xdelta. La pantalla de Game Over sigue en inglés.
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
