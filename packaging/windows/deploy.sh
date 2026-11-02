#!/usr/bin/env bash
# Rassemble orpheus.exe et tout ce dont il a besoin dans un dossier autonome, à empaqueter
# ensuite par Inno Setup. À lancer dans un shell MSYS2 UCRT64 (voir .github/workflows/installers.yml).
#
#   packaging/windows/deploy.sh <dossier build> <dossier de sortie> <deno.exe>
set -euo pipefail

BUILD="$1" OUT="$2" DENO="$3"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${MINGW_PREFIX:-/ucrt64}"

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$BUILD/orpheus.exe" "$OUT/"
# Outils utilisés par yt-dlp : ffmpeg/ffprobe de MSYS2 (mêmes DLL que libmpv) et deno.
# Orpheus ajoute son propre dossier au PATH au démarrage.
cp "$PREFIX/bin/ffmpeg.exe" "$PREFIX/bin/ffprobe.exe" "$OUT/"
cp "$DENO" "$OUT/deno.exe"

# Qt : bibliothèques, plugins (plateforme, images, SQLite, TLS…) et modules QML importés.
windeployqt=""
for c in "$PREFIX/bin/windeployqt6.exe" "$PREFIX/bin/windeployqt-qt6.exe" "$PREFIX/bin/windeployqt.exe" \
         "$PREFIX/share/qt6/bin/windeployqt.exe" "$PREFIX/share/qt6/bin/windeployqt6.exe"; do
    [[ -x $c ]] && windeployqt="$c" && break
done
[[ -n $windeployqt ]] || { echo "windeployqt introuvable (paquet qt6-tools)" >&2; exit 1; }
args=(--qmldir "$ROOT/qml" --no-translations --compiler-runtime)
for c in "$PREFIX/bin/qtpaths6.exe" "$PREFIX/bin/qtpaths-qt6.exe" "$PREFIX/share/qt6/bin/qtpaths.exe"; do
    [[ -x $c ]] && args+=(--qtpaths "$c") && break
done
echo "windeployqt : $windeployqt ${args[*]}"
"$windeployqt" "${args[@]}" "$OUT/orpheus.exe"
plugins="."
[[ -d $OUT/plugins ]] && plugins="plugins"
# Seul le pilote SQLite sert.
if [[ -d $OUT/$plugins/sqldrivers ]]; then
    find "$OUT/$plugins/sqldrivers" -type f ! -name 'qsqlite*' -delete
fi
# Sans qt.conf, le Qt de MSYS2 cherche ses modules QML dans share/qt6/qml (organisation
# de MSYS2) au lieu du dossier qml/ installé à côté de l'exécutable.
[[ -d $OUT/qml ]] || { echo "windeployqt n'a pas créé le dossier qml/" >&2; exit 1; }
cat > "$OUT/qt.conf" <<EOF
[Paths]
Prefix = .
Plugins = $plugins
QmlImports = qml
Qml2Imports = qml
EOF
echo "qt.conf : Plugins = $plugins, QmlImports = qml"

# windeployqt ne copie que Qt : libmpv, les DLL de ffmpeg, libstdc++… sont ajoutées en suivant
# les imports de chaque exe/DLL (objdump) jusqu'à ce qu'il ne manque plus rien.
# Les DLL système (KERNEL32, USER32…) ne sont pas dans MSYS2 : ignorées.
queue=()
while IFS= read -r -d '' f; do queue+=("$f"); done < <(find "$OUT" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
# OpenSSL est chargé dynamiquement par le plugin TLS de Qt (HTTPS) : invisible pour objdump.
for dll in libssl-3-x64.dll libcrypto-3-x64.dll; do
    if [[ -f $PREFIX/bin/$dll && ! -f $OUT/$dll ]]; then
        cp "$PREFIX/bin/$dll" "$OUT/"
        queue+=("$OUT/$dll")
    fi
done
copied=0
while [[ ${#queue[@]} -gt 0 ]]; do
    f="${queue[0]}"
    queue=("${queue[@]:1}")
    [[ $(basename "$f") == deno.exe ]] && continue   # binaire autonome
    while IFS= read -r dll; do
        dll="${dll%$'\r'}"
        [[ -z $dll || -f $OUT/$dll ]] && continue
        if [[ -f $PREFIX/bin/$dll ]]; then
            cp "$PREFIX/bin/$dll" "$OUT/"
            queue+=("$OUT/$dll")
            copied=$((copied + 1))
        fi
    done < <(objdump -p "$f" | sed -n 's/^[[:space:]]*DLL Name: //p')
done

echo "Dossier autonome prêt : $OUT ($copied DLL hors Qt, $(du -sh "$OUT" | cut -f1))"
