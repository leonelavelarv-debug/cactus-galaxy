#!/usr/bin/env python3
# Prepara un port limpio para PortMaster a partir de una carpeta del port (Super Mario Galaxy.sh + smg/): sin las
# herramientas de medicion (ULTRA-GLES: smg/ultra/, las series, el muestreo de hilos, las capturas periodicas, el
# registro de memoria cada 60 s), sin binarios de prueba y con los ajustes del usuario en smg/ajustes.cfg.
#
#   python3 limpiar.py <carpeta del port> <carpeta nueva>
#
# Despues: ./empaquetar.sh <carpeta nueva> <salida>
import os
import shutil
import sys


def cut(text, start, end, replacement=""):
    """Quita (o cambia) desde la linea que empieza con `start` hasta la que empieza con `end` (sin incluirla)."""
    a = text.index(start)
    b = text.index(end, a)
    return text[:a] + replacement + text[b:]


def clean_launcher(s):
    # Los ajustes de smg/ajustes.cfg se leen nada mas entrar en la carpeta del juego, antes de abrir Cactus Galaxy Wrapper
    # (que tambien usa NEBULA_FACE_LAYOUT, NEBULA_QUIT_COMBO...) y antes de la primera linea del log, que los muestra.
    a = s.index("# Ajustes (se pueden cambiar aqui")
    b = s.index("done\n", s.index('for cfg in "$GAMEDIR/ultra.cfg"', a)) + len("done\n")
    block = s[a:b]
    s = s[:a] + s[b:]
    anchor = 'exec > >(tee "$GAMEDIR/log.txt") 2>&1\n'
    s = s.replace(anchor, anchor + block, 1)
    # Los ajustes del usuario: smg/ajustes.cfg.
    s = s.replace(
        "# Ajustes (se pueden cambiar aqui, o en smg/ultra.cfg: lineas NOMBRE=valor, solo NEBULA_*, GALAXY_* y NOPAL_*, que mandan\n"
        "# sobre lo de abajo sin editar este archivo; lo usa ULTRA-GLES para comparar ajustes):\n"
        "# Se aceptan ultra.cfg y ultra.cfg.txt (editores que anaden .txt), finales de linea de Windows, la marca BOM del\n",
        "# Ajustes (se pueden cambiar aqui, o en smg/ajustes.cfg: lineas NOMBRE=valor, solo NEBULA_*, GALAXY_* y NOPAL_*, que mandan\n"
        "# sobre lo de abajo sin editar este archivo).\n"
        "# Se aceptan ajustes.cfg y ajustes.cfg.txt (editores que anaden .txt), finales de linea de Windows, la marca BOM del\n")
    s = s.replace('for cfg in "$GAMEDIR/ultra.cfg" "$GAMEDIR/ultra.cfg.txt"; do',
                  'for cfg in "$GAMEDIR/ajustes.cfg" "$GAMEDIR/ajustes.cfg.txt"; do')
    s = s.replace("(ponlo en smg/ultra.cfg si algo se ve mal con el hilo)", "(ponlo en smg/ajustes.cfg si algo se ve mal con el hilo)")
    # Comentarios sin referencias a las mediciones ni a numeros de version.
    s = s.replace("# uber por defecto (ULTRA-GLES): la primera vez con wait,", "# uber por defecto: la primera vez con wait,")
    s = s.replace("1 (por defecto desde update52): las llamadas", "1 (por defecto): las llamadas")
    s = s.replace("-30 % de p99 (ULTRA-GLES). 0: como antes", "-30 % de p99. 0: sin hilo")
    # Sin capturas periodicas ni muestreo de hilos por defecto (diagnosticos).
    s = s.replace('if [ "${NEBULA_SHOTS:-1}" = "1" ]; then', 'if [ "${NEBULA_SHOTS:-0}" = "1" ]; then')
    s = s.replace("#   NEBULA_SHOTS=1                       una captura", "#   NEBULA_SHOTS=0                       1: una captura")
    s = s.replace("#   NEBULA_PROFILE=150                   a los 150 s (y 30 y 60 s despues) anota en log.txt donde gastan su tiempo los hilos\n"
                  'export NEBULA_PROFILE="${NEBULA_PROFILE:-150}"\n',
                  'export NEBULA_PROFILE=0\n')
    # ULTRA-GLES: fuera entero; la partida va en smg/nand.
    s = cut(s, "# ULTRA-GLES (medicion del rendimiento grafico", 'export SDL_GAMECONTROLLERCONFIG=',
            'nand_dir="$GAMEDIR/nand"\n')
    s = cut(s, "# Que da la bateria (diagnostico", 'if [ "${NEBULA_PERF:-1}" = "1" ]; then')
    s = cut(s, "# Memoria del juego y libre en el sistema, cada 60 s", 'if [ "$NEBULA_ULTRA" = serie ]; then')
    s = cut(s, 'if [ "$NEBULA_ULTRA" = serie ]; then', "pkill -P \"$MEMLOG_PID\"",
            './nebula --pak "$GAMEDIR/gamedata/game.pak" --nand "$nand_dir"\nstatus=$?\n')
    s = s.replace('pkill -P "$MEMLOG_PID" 2>/dev/null; kill "$MEMLOG_PID" 2>/dev/null\n', "")
    for word in ("ultra", "serie_compare", "memlog", "cpu_max", "compare.py", "update5", "update6"):
        assert word not in s.lower(), word
    return s


def main(argv):
    if len(argv) != 3:
        print("uso: python3 limpiar.py <carpeta del port> <carpeta nueva>")
        return 2
    src, dst = argv[1], argv[2]
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=True)
    smg = os.path.join(dst, "smg")
    for name in ("ultra", "shots", "crashes", "data", "nand", "__pycache__"):
        shutil.rmtree(os.path.join(smg, name), ignore_errors=True)
    for name in os.listdir(smg):
        if name.startswith(("nebula-f", "nebula-u")) or name in ("ultra.cfg", "ultra.cfg.txt", "log.txt", "idioma.txt"):
            os.remove(os.path.join(smg, name))
    launcher = os.path.join(dst, "Super Mario Galaxy.sh")
    with open(launcher, encoding="utf-8") as f:
        text = clean_launcher(f.read())
    with open(launcher, "w", encoding="utf-8") as f:
        f.write(text)
    print("port limpio en", dst)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
