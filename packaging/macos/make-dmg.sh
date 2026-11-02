#!/usr/bin/env bash
# Rend Orpheus.app autonome (Qt, libmpv, ffmpeg, deno inclus) et produit le .dmg.
# Utilisé par .github/workflows/installers.yml, sur un Mac avec Homebrew.
#
#   packaging/macos/make-dmg.sh <dossier build> <dossier de sortie> <binaire deno> <version>
set -euo pipefail

BUILD="$1" OUT="$2" DENO="$3" VERSION="$4"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARCH="$(uname -m)"   # arm64 ou x86_64
STAGE="$BUILD/dmg"
APP="$STAGE/Orpheus.app"
MACOS="$APP/Contents/MacOS"

rm -rf "$STAGE"
mkdir -p "$STAGE" "$OUT"
cp -R "$BUILD/Orpheus.app" "$APP"

# Outils utilisés par yt-dlp ; Orpheus ajoute Contents/MacOS au PATH au démarrage.
cp "$(brew --prefix ffmpeg)/bin/ffmpeg" "$(brew --prefix ffmpeg)/bin/ffprobe" "$MACOS/"
cp "$DENO" "$MACOS/deno"
chmod u+w,a+x "$MACOS"/*

# Qt officiel (QT_ROOT_DIR, installé par install-qt-action) : le Qt de Homebrew, découpé en
# plusieurs paquets, laisse des chemins absolus que macdeployqt ne corrige pas.
QT_DIR="${QT_ROOT_DIR:?QT_ROOT_DIR doit pointer vers le Qt officiel}"

# Qt, libmpv et toutes les bibliothèques Homebrew (y compris celles de ffmpeg) vont dans
# Contents/Frameworks, avec des chemins relatifs à l'application.
"$QT_DIR/bin/macdeployqt" "$APP" \
    -qmldir="$ROOT/qml" \
    -executable="$MACOS/ffmpeg" \
    -executable="$MACOS/ffprobe" \
    -always-overwrite
# Seul le pilote SQLite sert ; les autres (ODBC, PostgreSQL, Mimer) dépendent de
# bibliothèques absentes.
find "$APP/Contents/PlugIns/sqldrivers" -type f ! -name 'libqsqlite*' -delete

# macdeployqt suit mal les dépendances « @rpath/… » des bibliothèques Homebrew (ex. brotli,
# utilisé par freetype pour libmpv) : on complète jusqu'à ce qu'il ne manque plus rien.
FW="$APP/Contents/Frameworks"
BREW="$(brew --prefix)"
macho_files() {
    find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 |
        while IFS= read -r -d '' f; do file -b "$f" | grep -q 'Mach-O' && printf '%s\0' "$f"; done
}
find_brew_lib() {
    local c
    for c in "$BREW/lib/$1" "$BREW"/opt/*/lib/"$1"; do
        [[ -f $c ]] && { echo "$c"; return 0; }
    done
    return 1
}
pass=0
while :; do
    pass=$((pass + 1))
    added=0
    while IFS= read -r -d '' f; do
        while IFS= read -r dep; do
            case "$dep" in
                @rpath/*.framework/*) continue ;;   # frameworks Qt : gérés par macdeployqt
                @rpath/*) name="${dep#@rpath/}" ;;
                "$BREW"/*|/usr/local/*)
                    name="$(basename "$dep")"
                    chmod u+w "$f"
                    install_name_tool -change "$dep" "@rpath/$name" "$f" 2>/dev/null ;;
                *) continue ;;
            esac
            [[ -f $FW/$name ]] && continue
            src="$(find_brew_lib "$name")" || { echo "Bibliothèque introuvable : $name (requise par $f)" >&2; exit 1; }
            cp "$src" "$FW/$name"
            chmod u+w "$FW/$name"
            install_name_tool -id "@rpath/$name" "$FW/$name" 2>/dev/null
            echo "  + $name (requise par $(basename "$f"))"
            added=$((added + 1))
        done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
    done < <(macho_files)
    [[ $added == 0 ]] && break
    [[ $pass -lt 10 ]] || { echo "Dépendances : trop de passes" >&2; exit 1; }
done
# « @rpath » doit mener à Contents/Frameworks depuis les bibliothèques et les exécutables.
for f in "$FW"/*.dylib; do
    install_name_tool -add_rpath @loader_path "$f" 2>/dev/null || true
done
for f in "$MACOS"/*; do
    file -b "$f" | grep -q 'Mach-O' && { install_name_tool -add_rpath @executable_path/../Frameworks "$f" 2>/dev/null || true; }
done

# Les bibliothèques Homebrew sont compilées pour le macOS de la machine de compilation :
# c'est donc la version minimale requise.
min_macos="$(sw_vers -productVersion | cut -d. -f1).0"
/usr/libexec/PlistBuddy -c "Set :LSMinimumSystemVersion $min_macos" "$APP/Contents/Info.plist"
echo "macOS minimum : $min_macos"

# Vérification : plus aucune référence à Homebrew ni au Qt de compilation, sinon l'app
# ne marcherait que sur ce Mac.
bad=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    if otool -L "$f" | tail -n +2 | grep -E "/(opt/homebrew|usr/local)/|$QT_DIR" ; then
        echo "  ↳ dans $f" >&2
        bad=1
    fi
done < <(find "$APP" -type f -print0)
[[ $bad == 0 ]] || { echo "Des bibliothèques extérieures à l'application sont encore référencées." >&2; exit 1; }

# Signature ad hoc (obligatoire sur Apple Silicon après modification des binaires) :
# chaque binaire, puis l'application entière.
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' && codesign --force --sign - "$f"
done < <(find "$APP" -type f -print0)
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

# Image disque : l'app et un raccourci vers Applications, à glisser l'une sur l'autre.
ln -s /Applications "$STAGE/Applications"
DMG="$OUT/Orpheus-$VERSION-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname "Orpheus $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Image disque prête : $DMG ($(du -h "$DMG" | cut -f1))"
