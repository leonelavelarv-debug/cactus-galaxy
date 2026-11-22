#!/usr/bin/env bash
# NEBULA_PARCHES de armar.sh sin compilar el port (parches.sh): una carpeta con solo .xdelta, solo .rusmg o ambos se
# copia; una vacia (o con otros archivos) y una que no existe dan un error claro y no copian nada.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=../parches.sh
. "$here/parches.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fallos=0
caso() {  # caso <nombre> <archivos de la carpeta...> -- <esperado: ok|error> <archivos copiados esperados...>
  local nombre=$1; shift
  local carpeta="$tmp/$nombre" destino="$tmp/$nombre-destino" esperado copiado
  mkdir -p "$carpeta" "$destino"
  while [ "$1" != "--" ]; do printf 'x' > "$carpeta/$1"; shift; done
  shift
  esperado=$1; shift
  if copia_parches "$carpeta" "$destino" > "$tmp/salida" 2> "$tmp/error"; then resultado=ok; else resultado=error; fi
  copiado=$(cd "$destino" && ls | sort | tr '\n' ' ')
  local esperado_copiado; esperado_copiado=$(printf '%s\n' "$@" | sed '/^$/d' | sort | tr '\n' ' ')
  if [ "$resultado" = "$esperado" ] && [ "$copiado" = "$esperado_copiado" ]; then
    echo "ok    $nombre ($resultado: $(cat "$tmp/salida" "$tmp/error" | head -1))"
  else
    echo "FALLO $nombre: $resultado (esperado $esperado), copiado [$copiado] (esperado [$esperado_copiado])"
    fallos=1
  fi
}
caso solo-xdelta   Super_Mario_Galaxy_PT-BR.xdelta -- ok Super_Mario_Galaxy_PT-BR.xdelta
caso solo-rusmg    Super_Mario_Galaxy_RUS.rusmg -- ok Super_Mario_Galaxy_RUS.rusmg
caso ambos         Super_Mario_Galaxy_PT-BR.xdelta Super_Mario_Galaxy_RUS.rusmg LEEME.txt -- ok \
                   Super_Mario_Galaxy_PT-BR.xdelta Super_Mario_Galaxy_RUS.rusmg
caso vacia         -- error
caso sin-parches   LEEME.txt traduccion.zip -- error
# Una carpeta que no existe.
if copia_parches "$tmp/no-existe" "$tmp" > /dev/null 2> "$tmp/error"; then
  echo "FALLO no-existe: se acepto"; fallos=1
else
  echo "ok    no-existe (error: $(cat "$tmp/error"))"
fi
# armar.sh usa la funcion (y no el cp de antes).
if grep -q 'copia_parches "$NEBULA_PARCHES"' "$here/armar.sh" && ! grep -q 'no hay .xdelta en' "$here/armar.sh"; then
  echo "ok    armar.sh usa copia_parches"
else
  echo "FALLO armar.sh no usa copia_parches"; fallos=1
fi
exit $fallos
