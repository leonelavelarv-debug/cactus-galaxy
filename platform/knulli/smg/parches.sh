# Parches de traduccion de terceros para armar.sh (no van en el repositorio): copia_parches <carpeta> <destino>
# copia a <destino> los .xdelta (parches del ISO, p. ej. el PT-BR) y los .rusmg (el ruso que saca
# cactus-ruso --extraer) de <carpeta>, cualquiera de los dos o ambos. Sin ninguno, o sin la carpeta, explica el
# problema en stderr y devuelve 1. La prueba sin compilar el port: platform/knulli/smg/tests/parches_tests.sh.
copia_parches() {
  local carpeta=$1 destino=$2 f copiados=0
  if [ ! -d "$carpeta" ]; then
    printf 'NEBULA_PARCHES=%s no es una carpeta\n' "$carpeta" >&2
    return 1
  fi
  for f in "$carpeta"/*.xdelta "$carpeta"/*.rusmg; do
    [ -f "$f" ] || continue  # un patron sin coincidencias queda tal cual
    cp -f "$f" "$destino/" || { printf 'no se pudo copiar %s a %s\n' "$f" "$destino" >&2; return 1; }
    copiados=$((copiados + 1))
  done
  if [ "$copiados" -eq 0 ]; then
    printf 'NEBULA_PARCHES=%s no tiene parches: hace falta al menos un .xdelta (traduccion del ISO) o un .rusmg (ruso)\n' "$carpeta" >&2
    return 1
  fi
  printf 'parches de traduccion: %s copiado(s) de %s\n' "$copiados" "$carpeta"
}
