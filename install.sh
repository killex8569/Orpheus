#!/usr/bin/env bash
# Orpheus — installation sous Linux (pour l'utilisateur courant, sans sudo).
#
#   install.sh [--deps] [--no-tests] [--yes]
#
#   --deps      installe d'abord les dépendances avec le gestionnaire de paquets (sudo)
#   --no-tests  ne lance pas les tests unitaires après la compilation
#   --yes       répond « oui » à toutes les questions
#
# Installe : ~/.local/bin/orpheus, l'icône (hicolor) et l'entrée du menu des applications.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$HOME/.local/bin"
APPS_DIR="$HOME/.local/share/applications"
ICONS_DIR="$HOME/.local/share/icons/hicolor"

WITH_DEPS=0 RUN_TESTS=1 ASSUME_YES=0
for arg in "$@"; do
    case "$arg" in
        --deps) WITH_DEPS=1 ;;
        --no-tests) RUN_TESTS=0 ;;
        -y|--yes) ASSUME_YES=1 ;;
        -h|--help) sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Option inconnue : $arg (voir --help)" >&2; exit 2 ;;
    esac
done

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }
ask() {  # ask "Question ?" → 0 si oui
    [[ $ASSUME_YES == 1 ]] && return 0
    [[ -t 0 ]] || return 1
    local r; read -rp "$1 [o/N] " r
    [[ $r =~ ^[oOyY] ]]
}

# ---------- Dépendances ----------
. /etc/os-release 2>/dev/null || true
distro="${ID:-} ${ID_LIKE:-}"
deps_cmd="" ffmpeg_pkg=""
case "$distro" in
    *fedora*|*rhel*|*centos*)
        deps_cmd="sudo dnf install -y qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel mpv-devel cmake ninja-build gcc-c++ pkgconf-pkg-config librsvg2-tools yt-dlp"; ffmpeg_pkg="ffmpeg-free" ;;
    *debian*|*ubuntu*)
        deps_cmd="sudo apt-get install -y qt6-base-dev qt6-declarative-dev qt6-svg-dev qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript libmpv-dev cmake ninja-build g++ pkg-config librsvg2-bin yt-dlp"; ffmpeg_pkg="ffmpeg" ;;
    *arch*)
        deps_cmd="sudo pacman -S --needed --noconfirm qt6-base qt6-declarative qt6-svg mpv cmake ninja gcc pkgconf librsvg yt-dlp deno"; ffmpeg_pkg="ffmpeg" ;;
    *suse*)
        deps_cmd="sudo zypper install -y qt6-base-devel qt6-declarative-devel qt6-quickcontrols2-devel qt6-svg-devel mpv-devel cmake ninja gcc-c++ pkgconf rsvg-convert yt-dlp"; ffmpeg_pkg="ffmpeg" ;;
esac
# ffmpeg seulement s'il manque (évite un conflit avec une version déjà installée, ex. RPM Fusion).
[[ -n $deps_cmd ]] && ! command -v ffmpeg >/dev/null && deps_cmd+=" $ffmpeg_pkg"

missing=()
for c in cmake ninja c++ pkg-config; do command -v "$c" >/dev/null || missing+=("$c"); done
command -v pkg-config >/dev/null && ! pkg-config --exists mpv && missing+=("libmpv (dev)")

if [[ $WITH_DEPS == 1 || ${#missing[@]} -gt 0 ]]; then
    [[ ${#missing[@]} -gt 0 ]] && warn "Outils de compilation manquants : ${missing[*]}"
    if [[ -z $deps_cmd ]]; then
        [[ ${#missing[@]} -gt 0 ]] && die "Distribution non reconnue : installez Qt 6.5+ (base, declarative, svg), libmpv, cmake, ninja, un compilateur C++ et pkg-config."
    elif [[ $WITH_DEPS == 1 ]] || ask "Installer les dépendances avec : $deps_cmd ?"; then
        info "Installation des dépendances"
        eval "$deps_cmd"
    else
        die "Installez les dépendances puis relancez (ou utilisez --deps)."
    fi
fi

# Dépendances d'exécution : Orpheus démarre sans elles, mais la lecture YouTube en a besoin.
command -v ffmpeg >/dev/null || warn "ffmpeg introuvable : nécessaire pour enregistrer les morceaux (installez le paquet ffmpeg)."
command -v yt-dlp >/dev/null || warn "yt-dlp introuvable : installez-le, ou utilisez Paramètres → yt-dlp → « Installer / mettre à jour yt-dlp » dans Orpheus."
if ! command -v deno >/dev/null && [[ ! -x $HOME/.deno/bin/deno ]]; then
    warn "deno introuvable : YouTube en a besoin pour résoudre ses protections JavaScript."
    if command -v curl >/dev/null && ask "Installer deno dans ~/.deno (script officiel deno.land) ?"; then
        curl -fsSL https://deno.land/install.sh | sh -s -- -y --no-modify-path
    fi
fi

# ---------- Compilation ----------
cd "$ROOT"
# Dossier de compilation créé ailleurs (projet déplacé) : CMake refuserait de le réutiliser.
if [[ -f build/CMakeCache.txt ]] && ! grep -qxF "CMAKE_HOME_DIRECTORY:INTERNAL=$PWD" build/CMakeCache.txt; then
    warn "Dossier build/ issu d'un autre emplacement : recompilation complète."
    rm -rf build
fi

info "Compilation"
cmake -S . -B build -G Ninja -Wno-dev -DCMAKE_BUILD_TYPE=Release
cmake --build build
if [[ $RUN_TESTS == 1 ]]; then
    info "Tests unitaires"
    ./build/orpheus_tests -silent
fi

# ---------- Installation ----------
info "Installation dans $BIN_DIR"
install -Dm755 build/orpheus "$BIN_DIR/orpheus"
install -Dm644 icons/orpheus.svg "$ICONS_DIR/scalable/apps/orpheus.svg"
# Versions bitmap : certains panneaux et menus ignorent les SVG.
if command -v rsvg-convert >/dev/null; then
    for s in 16 24 32 48 64 128 256 512; do
        mkdir -p "$ICONS_DIR/${s}x${s}/apps"
        rsvg-convert -w "$s" -h "$s" icons/orpheus.svg -o "$ICONS_DIR/${s}x${s}/apps/orpheus.png"
    done
fi
mkdir -p "$APPS_DIR"
cat > "$APPS_DIR/orpheus.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Orpheus
Comment=Lecteur de musique (YouTube, Spotify, cache local)
Exec=$BIN_DIR/orpheus
Icon=orpheus
Terminal=false
Categories=AudioVideo;Audio;Player;
StartupWMClass=orpheus
EOF

# Caches d'icônes et de menus : sans cela l'icône peut rester introuvable.
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f -t -q "$ICONS_DIR" || true
command -v update-desktop-database >/dev/null && update-desktop-database -q "$APPS_DIR" || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 --noincremental >/dev/null 2>&1 || true

case ":$PATH:" in *":$BIN_DIR:"*) ;; *) warn "$BIN_DIR n'est pas dans le PATH : lancez Orpheus depuis le menu ou avec $BIN_DIR/orpheus." ;; esac
pgrep -x orpheus >/dev/null && warn "Orpheus est en cours d'exécution : relancez-le pour utiliser la nouvelle version."

version="$(sed -n 's/^project(orpheus VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
info "Orpheus $version installé : $BIN_DIR/orpheus (disponible dans le menu des applications)"
