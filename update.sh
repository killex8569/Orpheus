#!/usr/bin/env bash
# Orpheus — mise à jour sous Linux : récupère la dernière version (git), recompile et réinstalle.
#
#   update.sh [--force] [options de install.sh]
#
#   --force  recompile et réinstalle même si les sources n'ont pas changé
#   Les autres options (--deps, --no-tests, --yes) sont transmises à install.sh.
#
# Vos données (morceaux, playlists, réglages) ne sont pas touchées.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HERE="$ROOT"

FORCE=0 pass=()
for arg in "$@"; do
    case "$arg" in
        --force) FORCE=1 ;;
        -h|--help) sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) pass+=("$arg") ;;
    esac
done

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }
version() { sed -n 's/^project(orpheus VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt"; }

command -v git >/dev/null || die "git est nécessaire pour la mise à jour."
git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
    || die "$ROOT n'est pas un dépôt git : clonez https://github.com/killex8569/Orpheus puis lancez install.sh."
git -C "$ROOT" symbolic-ref -q HEAD >/dev/null \
    || die "Le dépôt n'est sur aucune branche (HEAD détachée) : faites « git checkout master » puis relancez."

before="$(git -C "$ROOT" rev-parse HEAD)"
old_version="$(version)"
info "Version actuelle : $old_version — recherche de mises à jour"
# --ff-only : jamais de fusion automatique ; git refuse si des modifications locales gênent.
git -C "$ROOT" pull --ff-only || die "git pull impossible (modifications locales ?). Voir « git status »."
after="$(git -C "$ROOT" rev-parse HEAD)"

if [[ $before == "$after" && $FORCE == 0 && -x $HOME/.local/bin/orpheus ]]; then
    info "Orpheus est déjà à jour ($old_version). Utilisez --force pour réinstaller quand même."
    exit 0
fi

[[ $before != "$after" ]] && info "Nouvelle version : $old_version → $(version)"
# Le script d'installation peut lui-même avoir changé : on lance la version à jour.
exec bash "$HERE/install.sh" ${pass[@]+"${pass[@]}"}
