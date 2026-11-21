#!/bin/bash
# Instalar Super Mario Galaxy (Nebula) -- instalador autoextraible para PortMaster.
#
# Copia este archivo a la carpeta donde tu firmware lista los ports y abrelo desde el menu Ports:
#   KNULLI, Batocera    /userdata/roms/ports/          (SD: share/roms/ports)
#   muOS                /mnt/mmc/ROMS/Ports/  o  /mnt/sdcard/ROMS/Ports/   (SD: ROMS/Ports)
#   ArkOS, dArkOS       /roms/ports/  o  /roms2/ports/ con dos SD  (particion EASYROMS: ports)
#   ROCKNIX, AmberELEC  /storage/roms/ports/           (SD: roms/ports)
# Pone la carpeta del juego (smg/) en la carpeta de datos de PortMaster de ese firmware y el lanzador
# "Super Mario Galaxy.sh" donde el menu lo ve (las mismas rutas que usa PortMaster: harbourmaster/config.py), da la
# ficha al menu y se borra. Lo que ya tengas en smg/ (gamedata/game.pak, idioma, partidas guardadas) se conserva.
# Requiere PortMaster instalado (da control.txt, las rutas y los mandos). GPL-3.0.
R="${SMG_RAIZ:-}"  # solo para probarlo fuera de la consola: raiz falsa
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "$R/opt/system/Tools/PortMaster/" ]; then controlfolder="$R/opt/system/Tools/PortMaster"
elif [ -d "$R/opt/tools/PortMaster/" ]; then controlfolder="$R/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then controlfolder="$XDG_DATA_HOME/PortMaster"
else controlfolder="$R/roms/ports/PortMaster"; fi
self="$(realpath "$0" 2>/dev/null || readlink -f "$0" 2>/dev/null || echo "$0")"
if [ -f "$controlfolder/control.txt" ]; then
  source "$controlfolder/control.txt"
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
fi
cfw=$(printf '%s' "${CFW_NAME:-}" | tr '[:upper:]' '[:lower:]')

say() {
  echo "$1"
  if type pm_message >/dev/null 2>&1; then pm_message "$1"; else
    for tty in /dev/tty0 /dev/tty1; do [ -w "$tty" ] && printf '\033c%s\n' "$1" > "$tty" 2>/dev/null; done
  fi
}
fail() {
  say "No se pudo instalar Super Mario Galaxy: $1"
  [ -n "${work:-}" ] && rm -rf "$work"
  sleep 8
  exit 1
}

# Las rutas de cada firmware: ports (datos: smg/) y scripts (el .sh que lista el menu). El lanzador usa
# /$directory/ports/smg ($directory sale de control.txt), asi que los datos van ahi siempre que PortMaster lo da.
if [ -d "$R/opt/muos" ] || [[ "$cfw" == *muos* ]]; then
  # muOS: datos en <tarjeta>/ports, lanzadores en <tarjeta>/ROMS/Ports (la tarjeta en la que esta este archivo).
  card="mnt/mmc"
  case "$self" in
    */mnt/sdcard/*) card="mnt/sdcard" ;;
    */mnt/mmc/*) card="mnt/mmc" ;;
  esac
  [ -n "${directory:-}" ] && card="$directory"
  ports="$R/$card/ports"
  scripts="$R/$card/ROMS/Ports"
else
  if [ -n "${directory:-}" ] && [ -d "$R/$directory/ports" ]; then ports="$R/$directory/ports"
  elif [ -d "$R/userdata/roms/ports" ]; then ports="$R/userdata/roms/ports"      # KNULLI, Batocera
  elif [ -d "$R/opt/system/Tools" ]; then                                         # ArkOS, dArkOS
    if [ -d "$R/roms2/tools" ]; then ports="$R/roms2/ports"; else ports="$R/roms/ports"; fi
  elif [ -d "$R/storage/roms/ports" ]; then ports="$R/storage/roms/ports"        # ROCKNIX, JELOS, AmberELEC
  else ports="$R/roms/ports"; fi
  scripts="$ports"
  [ -d "$R/storage/roms/ports_scripts" ] && [ "$ports" = "$R/storage/roms/ports" ] && scripts="$R/storage/roms/ports_scripts"
  # El .sh va junto a este instalador si esta en otra carpeta de ports que el menu lee.
  here="$(dirname "$self")"
  case "$here" in */ports|*/ports_scripts|*/Ports) [ -w "$here" ] && scripts="$here" ;; esac
fi
[ -n "${SMG_PORTS_DIR:-}" ] && ports="$SMG_PORTS_DIR"
[ -n "${SMG_SCRIPTS_DIR:-}" ] && scripts="$SMG_SCRIPTS_DIR"
echo "firmware: ${CFW_NAME:-?} | datos: $ports/smg | lanzador: $scripts"

mkdir -p "$ports" "$scripts" 2>/dev/null || $ESUDO mkdir -p "$ports" "$scripts" || fail "no se puede crear $ports"
[ -w "$ports" ] && [ -w "$scripts" ] || fail "no se puede escribir en $ports"
need_kb=__TAMANO_KB__
free_kb=$(df -Pk "$ports" 2>/dev/null | awk 'NR==2 {print $4}')
[ -n "$free_kb" ] && [ "$free_kb" -lt "$need_kb" ] && fail "faltan $(( (need_kb - free_kb) / 1024 + 1 )) MB libres en $ports"

say "Instalando Super Mario Galaxy en $ports/smg ..."
work="$ports/.smg-instalando"
rm -rf "$work" && mkdir -p "$work" || fail "no se puede crear $work"
start=$(awk '/^__CARGA_SMG__$/ { print NR + 1; exit }' "$self")
[ -n "$start" ] || fail "este archivo esta incompleto (vuelve a copiarlo)"
if ! tail -n +"$start" "$self" | tar -xzf - -C "$work" 2>/dev/null; then
  rm -rf "$work" && mkdir -p "$work"
  tail -n +"$start" "$self" | python3 -c 'import sys, tarfile; tarfile.open(fileobj=sys.stdin.buffer, mode="r|gz").extractall(sys.argv[1])' "$work" ||
    fail "no se pudo descomprimir (copia de nuevo el archivo, puede estar cortado)"
fi
[ -f "$work/Super Mario Galaxy.sh" ] && [ -f "$work/smg/nebula" ] || fail "el paquete no trae el juego"

# smg/: lo nuevo encima de lo que haya (game.pak, idioma.txt, nand/ y data/ no vienen en el paquete y se quedan).
mkdir -p "$ports/smg" || fail "no se puede crear $ports/smg"
cp -rf "$work/smg/." "$ports/smg/" || fail "no se pudo copiar smg/ (espacio?)"
cp -f "$work/Super Mario Galaxy.sh" "$scripts/Super Mario Galaxy.sh" || fail "no se pudo copiar el lanzador"
# Restos de versiones anteriores: un .sh dentro de smg/ saldria como juego en algunos menus.
rm -f "$ports/smg/cactus-galaxy-wrapper.sh"
for tool in nebula nebula-pak cactus-ruso cactus-galaxy-wrapper gamelist.py; do
  [ -f "$ports/smg/$tool" ] && { chmod +x "$ports/smg/$tool" 2>/dev/null || $ESUDO chmod +x "$ports/smg/$tool"; }
done
chmod +x "$scripts/Super Mario Galaxy.sh" 2>/dev/null || $ESUDO chmod +x "$scripts/Super Mario Galaxy.sh"
# Si el lanzador queda en otra carpeta que los datos y una version anterior lo dejo en ports/, se quita esa copia.
[ "$(cd "$scripts" && pwd -P)" != "$(cd "$ports" && pwd -P)" ] && [ -f "$ports/Super Mario Galaxy.sh" ] &&
  rm -f "$ports/Super Mario Galaxy.sh"
rm -rf "$work"
sync

# Pack de texturas (opcional): un .zip con TexturePacks/RMGE01/ junto a este instalador o en la carpeta de ports
# (p. ej. "SMG1 HD x1 RG35XXH.zip", reducido en la PC para la consola) se pone en smg/data/ en lugar del que hubiera y
# se borra el zip. El paquete del port no trae texturas: el pack lo pone cada uno. Si algo falla, el juego queda igual.
pack_zip=""
for z in "$(dirname "$self")"/*.zip "$ports"/*.zip; do
  [ -f "$z" ] || continue
  case "$(basename "$z")" in smg.zip) continue ;; esac
  python3 -c 'import sys, zipfile; n = zipfile.ZipFile(sys.argv[1]).namelist(); sys.exit(0 if any(x.startswith("TexturePacks/RMGE01/") for x in n) else 1)' \
    "$z" 2>/dev/null || continue
  # Con varios, el mas reciente.
  if [ -z "$pack_zip" ] || [ "$z" -nt "$pack_zip" ]; then pack_zip="$z"; fi
done
if [ -n "$pack_zip" ]; then
  say "Instalando el pack de texturas $(basename "$pack_zip") ..."
  pack_kb=$(python3 -c 'import sys, zipfile; print(sum(i.file_size for i in zipfile.ZipFile(sys.argv[1]).infolist()) // 1024 + 1024)' "$pack_zip" 2>/dev/null)
  free_kb=$(df -Pk "$ports" 2>/dev/null | awk 'NR==2 {print $4}')
  tp="$ports/smg/data/.texturas-instalando"
  if [ -z "$pack_kb" ]; then
    say "El pack de texturas esta danado (vuelve a copiarlo). El juego se instalo sin el."; sleep 5
  elif [ -n "$free_kb" ] && [ "$free_kb" -lt "$pack_kb" ]; then
    say "Faltan $(( (pack_kb - free_kb) / 1024 + 1 )) MB para el pack de texturas. El juego se instalo sin el."; sleep 5
  elif rm -rf "$tp" && mkdir -p "$tp" &&
       python3 -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); z.extractall(sys.argv[2], [n for n in z.namelist() if n.startswith("TexturePacks/RMGE01/") and ".." not in n])' \
         "$pack_zip" "$tp" && [ -d "$tp/TexturePacks/RMGE01" ]; then
    mkdir -p "$ports/smg/data/TexturePacks"
    rm -rf "$ports/smg/data/TexturePacks/RMGE01"
    mv "$tp/TexturePacks/RMGE01" "$ports/smg/data/TexturePacks/RMGE01" && rm -rf "$tp" && sync &&
      { [ "${SMG_CONSERVAR_INSTALADOR:-0}" = 1 ] || rm -f "$pack_zip"; } &&
      say "Pack de texturas instalado en $ports/smg/data/TexturePacks/RMGE01."
  else
    rm -rf "$tp"
    say "No se pudo descomprimir el pack de texturas (vuelve a copiarlo). El juego se instalo sin el."; sleep 5
  fi
fi

# Mods (opcionales): un .zip, .7z o .rar con archivos del juego (.arc, .szs...) o riivolution/ junto a este
# instalador va a smg/mods/<nombre del archivo>/, donde Nebula los carga (los sueltos, cada uno en el sitio de su
# nombre en el disco); si trae una sola carpeta arriba, esa. El zip lo abre python; 7z y rar, el 7zzs de PortMaster.
sevenz=""
for c in "$controlfolder/7zzs.${DEVICE_ARCH:-aarch64}" "$controlfolder/7zzs"; do [ -x "$c" ] && { sevenz="$c"; break; }; done
mod_dirs="$(dirname "$self")"
[ "$(cd "$ports" && pwd -P)" != "$(cd "$mod_dirs" && pwd -P)" ] && mod_dirs="$mod_dirs
$ports"
printf '%s\n' "$mod_dirs" | while IFS= read -r dir; do
  for a in "$dir"/*.zip "$dir"/*.ZIP "$dir"/*.7z "$dir"/*.rar "$dir"/*.RAR; do
    [ -f "$a" ] || continue
    case "$(basename "$a")" in smg.zip) continue ;; esac
    case "$a" in *.zip|*.ZIP)
      python3 -c 'import sys, zipfile; sys.exit(any(n.startswith("TexturePacks/") for n in zipfile.ZipFile(sys.argv[1]).namelist()))' \
        "$a" 2>/dev/null || continue ;;
    esac
    nombre="$(basename "$a")"; nombre="${nombre%.*}"; nombre="${nombre#.}"
    tmp="$ports/smg/mods/.mod-instalando"
    rm -rf "$tmp" && mkdir -p "$tmp" || continue
    case "$a" in
      *.zip|*.ZIP) python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$a" "$tmp" 2>/dev/null ;;
      *) if [ -n "$sevenz" ]; then "$sevenz" x -y -o"$tmp" "$a" >/dev/null 2>&1; else false; fi ;;
    esac
    ok=$?
    # Packs de texturas (ya instalados arriba) y zips que no son de este juego: se dejan como estan.
    if [ -d "$tmp/TexturePacks" ]; then rm -rf "$tmp"; continue; fi
    if [ "$ok" != 0 ]; then
      rm -rf "$tmp"
      case "$a" in
        *.zip|*.ZIP) ;;
        *) [ -z "$sevenz" ] && say "No se pudo abrir $(basename "$a"): falta 7zzs de PortMaster. Descomprimelo en la PC dentro de smg/mods/." && sleep 5 ;;
      esac
      continue
    fi
    if [ -z "$(find "$tmp" -type f \( -iname '*.arc' -o -iname '*.szs' -o -iname '*.bcsv' -o -iname '*.bmg' -o -iname '*.thp' -o -iname '*.ast' -o -iname '*.brsar' \) | head -n 1)" ] &&
       [ -z "$(find "$tmp" -type d -iname riivolution | head -n 1)" ]; then
      rm -rf "$tmp"; continue
    fi
    # Una sola carpeta arriba (la del propio archivo) y a lo sumo notas: el mod es esa carpeta. Las carpetas del disco
    # (ObjectData, files, riivolution...) Nebula ya las busca un nivel abajo.
    raiz="$tmp"
    subdirs=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | wc -l)
    otros=$(find "$tmp" -mindepth 1 -maxdepth 1 -type f ! -iname '*.txt' ! -iname '*.md' ! -iname '*.png' ! -iname '*.jpg' ! -iname '*.pdf' | wc -l)
    if [ "$subdirs" = 1 ] && [ "$otros" = 0 ]; then
      sub="$(find "$tmp" -mindepth 1 -maxdepth 1 -type d)"
      case "$(basename "$sub" | tr '[:upper:]' '[:lower:]')" in
        files|riivolution|textures|movies|objectdata|stagedata|layoutdata|audiores|particledata|systemdata|mappartsdata|moviedata|us*|eu*|jp*|kr*|cn*|tw*) ;;
        *) raiz="$sub" ;;
      esac
    fi
    destino="$ports/smg/mods/$nombre"
    rm -rf "$destino"
    if mv "$raiz" "$destino"; then
      rm -rf "$tmp"
      [ "${SMG_CONSERVAR_INSTALADOR:-0}" = 1 ] || rm -f "$a"
      say "Mod instalado: $nombre (para quitarlo, borra smg/mods/$nombre)."
    else
      rm -rf "$tmp"
      say "No se pudo instalar el mod $nombre (espacio?)."; sleep 5
    fi
  done
done
sync

# El instalador ya no hace falta (el gzip ya comprobo que llego entero).
if [ "${SMG_CONSERVAR_INSTALADOR:-0}" != 1 ]; then rm -f "$self"; fi

# El menu: la ficha (en el idioma del juego, smg/gamelist.py) y que vea el lanzador nuevo.
if command -v python3 >/dev/null 2>&1; then
  say "Preparando la portada y la ficha de Ports..."
  python3 "$ports/smg/gamelist.py" "$ports/smg" --cfw "${CFW_NAME:-}" 2>&1 | tail -3
  # EmulationStation con API (KNULLI, Batocera, ROCKNIX): relee la lista (quita el instalador, mete el juego).
  python3 -c 'import urllib.request; urllib.request.urlopen("http://127.0.0.1:1234/reloadgames", timeout=10)' \
    >/dev/null 2>&1 && echo "lista de Ports recargada"
fi
say "Super Mario Galaxy instalado. Abrelo desde Ports: la primera vez te pedira tu disco (ISO, RVZ...)."
# EmulationStation sin API (ArkOS, dArkOS, AmberELEC): se reinicia para que vea el juego y su ficha.
es_unit=""
for unit in emulationstation emustation; do
  systemctl is-active --quiet "$unit" 2>/dev/null && { es_unit=$unit; break; }
done
sleep 4
if [ -n "$es_unit" ] && command -v systemd-run >/dev/null 2>&1; then
  $ESUDO systemd-run --quiet --no-block --collect --unit="smg-instalar-$$" /bin/sh -c \
    'sleep 2; systemctl stop "$1"; [ -f "$3/.ficha-pendiente" ] && "$2" "$3/gamelist.py" "$3" --tras-cerrar-es >> "$3/log.txt" 2>&1; systemctl start "$1"' \
    smg-instalar "$es_unit" "$(command -v python3 || echo python3)" "$ports/smg"
elif [ -n "$es_unit" ]; then
  say "Reinicia EmulationStation (o la consola) para ver Super Mario Galaxy en Ports."
  sleep 4
fi
exit 0
__CARGA_SMG__
