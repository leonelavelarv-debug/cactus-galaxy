# Qué se publica y qué se queda privado

Lista de trabajo para cada versión que se reparte. Este archivo no va en el código público (`fuente-publica.sh` lo deja
fuera). No es asesoría legal: es la lectura habitual de la GPL-3.0.

## Regla

La GPL pide el código fuente de **lo que se reparte**: los binarios del paquete y lo necesario para compilarlos e
instalarlos. Lo que no se reparte puede quedarse privado. El nombre y el logo de Señor Cactus quedan fuera de la GPL
(`platform/knulli/MARCA.md`).

## Público, con cada versión

Junto al autoinstalable (`Instalar Super Mario Galaxy.sh`) y `smg.zip`, el código de **esa misma versión**:

    platform/knulli/smg/fuente-publica.sh <commit de la version> <carpeta>

Da `cactus-galaxy-fuente-<commit>.tar.gz`: el árbol de ese commit sin historial ni otras ramas. Incluye:

- Nebula con sus cambios (runtime/, native/, cmake/, third_party/, tools/, crates/...): de él salen `nebula` y
  `libsmg_game.so`.
- El port: lanzador, instalador, `limpiar.py`, `empaquetar.sh`, `armar.sh`, el wrapper, `nebula-pak`, `cactus-ruso`,
  y las pruebas que acompañan al código.
- Licencias y `MARCA.md`.

## Privado

- Este repositorio entero (historial, ramas, experimentos): solo se publica el tar.gz de cada versión.
- ULTRA-GLES: las series de medición (`smg/smg/ultra/`), sus resultados y comparaciones.
- Bancos y sondas: `smgbench/`, `recomp-probes/`, `prueba-mezcla/`.
- Proyectos y herramientas aparte, mientras no se repartan: Nopal, cactus-porter, Choya Wii Converter, el reductor de
  packs de texturas, los otros ports en desarrollo.
- Mods y packs de texturas de terceros: nunca en el repositorio ni en el paquete (cada usuario los consigue).

## Antes de publicar

1. El autoinstalable y el tar.gz salen del **mismo commit**.
2. `fuente-publica.sh` comprueba que no hay datos personales; el paquete lo comprueba `armar.sh`.
3. La cuenta donde se publica no debe llevar a tu nombre si quieres seguir siendo anónimo.
