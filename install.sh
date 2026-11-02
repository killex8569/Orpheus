#!/usr/bin/env bash
# Compile Orpheus et l'installe pour l'utilisateur courant (sans sudo).
set -euo pipefail
cd "$(dirname "$0")"

cmake -S . -B build -G Ninja -Wno-dev -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/orpheus_tests -silent

install -Dm755 build/orpheus "$HOME/.local/bin/orpheus"
install -Dm644 icons/music.svg "$HOME/.local/share/icons/hicolor/scalable/apps/orpheus.svg"
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

echo "Orpheus installé : $HOME/.local/bin/orpheus (disponible dans le menu des applications)"
