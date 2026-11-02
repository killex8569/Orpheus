#!/usr/bin/env bash
# Compile Orpheus et l'installe pour l'utilisateur courant (sans sudo).
set -euo pipefail
cd "$(dirname "$0")"

# Dossier de compilation créé ailleurs (projet déplacé) : CMake refuserait de le réutiliser.
if [[ -f build/CMakeCache.txt ]] && ! grep -qxF "CMAKE_HOME_DIRECTORY:INTERNAL=$PWD" build/CMakeCache.txt; then
    echo "Dossier build/ issu d'un autre emplacement : recompilation complète."
    rm -rf build
fi

cmake -S . -B build -G Ninja -Wno-dev -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/orpheus_tests -silent

icons="$HOME/.local/share/icons/hicolor"
install -Dm755 build/orpheus "$HOME/.local/bin/orpheus"
install -Dm644 icons/orpheus.svg "$icons/scalable/apps/orpheus.svg"
# Versions bitmap : certains panneaux et menus ignorent les SVG.
if command -v rsvg-convert >/dev/null; then
    for s in 16 24 32 48 64 128 256 512; do
        mkdir -p "$icons/${s}x${s}/apps"
        rsvg-convert -w "$s" -h "$s" icons/orpheus.svg -o "$icons/${s}x${s}/apps/orpheus.png"
    done
fi
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/orpheus.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Orpheus
Comment=Lecteur de musique (YouTube, Spotify, cache local)
Exec=$HOME/.local/bin/orpheus
Icon=orpheus
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=orpheus
EOF

# Caches d'icônes et de menus : sans cela l'icône peut rester introuvable.
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f -t -q "$icons" || true
command -v update-desktop-database >/dev/null && update-desktop-database -q "$HOME/.local/share/applications" || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 --noincremental >/dev/null 2>&1 || true

echo "Orpheus installé : $HOME/.local/bin/orpheus (disponible dans le menu des applications)"
