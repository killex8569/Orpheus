#!/usr/bin/env bash
# Orpheus — désinstallation sous Linux.
#
#   uninstall.sh [--purge] [--clean] [--yes]
#
#   --purge  supprime aussi les données : bibliothèque audio, playlists, réglages
#            (~/.local/share/orpheus) et le cache (~/.cache/orpheus)
#   --clean  supprime aussi le dossier de compilation build/ des sources
#   --yes    ne demande pas de confirmation
#
# Sans --purge, vos morceaux et playlists sont conservés : une réinstallation les retrouve.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$HOME/.local/bin"
APPS_DIR="$HOME/.local/share/applications"
ICONS_DIR="$HOME/.local/share/icons/hicolor"
DATA_DIR="$HOME/.local/share/orpheus"
CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/orpheus"

PURGE=0 CLEAN=0 ASSUME_YES=0
for arg in "$@"; do
    case "$arg" in
        --purge) PURGE=1 ;;
        --clean) CLEAN=1 ;;
        -y|--yes) ASSUME_YES=1 ;;
        -h|--help) sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Option inconnue : $arg (voir --help)" >&2; exit 2 ;;
    esac
done

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*" >&2; }
ask() {
    [[ $ASSUME_YES == 1 ]] && return 0
    [[ -t 0 ]] || return 1
    local r; read -rp "$1 [o/N] " r
    [[ $r =~ ^[oOyY] ]]
}

if pgrep -x orpheus >/dev/null; then
    if ask "Orpheus est en cours d'exécution. Le fermer ?"; then
        pkill -x orpheus || true
        sleep 1
    else
        warn "Orpheus reste ouvert jusqu'à sa fermeture."
    fi
fi

info "Suppression du programme, de l'icône et de l'entrée de menu"
rm -fv "$BIN_DIR/orpheus" "$APPS_DIR/orpheus.desktop" "$ICONS_DIR/scalable/apps/orpheus.svg"
for s in 16 24 32 48 64 128 256 512; do
    rm -f "$ICONS_DIR/${s}x${s}/apps/orpheus.png"
done

command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f -t -q "$ICONS_DIR" || true
command -v update-desktop-database >/dev/null && update-desktop-database -q "$APPS_DIR" || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 --noincremental >/dev/null 2>&1 || true

if [[ $PURGE == 1 ]]; then
    if [[ -d $DATA_DIR ]]; then
        size="$(du -sh "$DATA_DIR" 2>/dev/null | cut -f1)"
        if ask "Supprimer DÉFINITIVEMENT $DATA_DIR ($size : morceaux, playlists, statistiques, réglages) ?"; then
            rm -rf "$DATA_DIR"
            info "Données supprimées"
        else
            warn "Données conservées : $DATA_DIR"
        fi
    fi
    rm -rf "$CACHE_DIR"
    [[ -n ${ORPHEUS_HOME:-} ]] && warn "ORPHEUS_HOME=$ORPHEUS_HOME est défini : ce dossier n'a pas été touché."
elif [[ -d $DATA_DIR ]]; then
    info "Données conservées dans $DATA_DIR (--purge pour les supprimer)"
fi

if [[ $CLEAN == 1 && -d $ROOT/build ]]; then
    rm -rf "$ROOT/build"
    info "Dossier de compilation supprimé"
fi

info "Orpheus désinstallé"
