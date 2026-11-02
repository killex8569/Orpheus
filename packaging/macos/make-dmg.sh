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

# Qt, libmpv et toutes les bibliothèques Homebrew (y compris celles de ffmpeg) vont dans
# Contents/Frameworks, avec des chemins relatifs à l'application.
"$(brew --prefix qt)/bin/macdeployqt" "$APP" \
    -qmldir="$ROOT/qml" \
    -executable="$MACOS/ffmpeg" \
    -executable="$MACOS/ffprobe" \
    -always-overwrite

# Vérification : plus aucune référence à Homebrew, sinon l'app ne marcherait que sur ce Mac.
bad=0
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    if otool -L "$f" | tail -n +2 | grep -E '/(opt/homebrew|usr/local)/' ; then
        echo "  ↳ dans $f" >&2
        bad=1
    fi
done < <(find "$APP" -type f -print0)
[[ $bad == 0 ]] || { echo "Des bibliothèques Homebrew sont encore référencées." >&2; exit 1; }

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
