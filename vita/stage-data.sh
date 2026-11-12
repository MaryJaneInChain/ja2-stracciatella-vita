#!/bin/sh
# Assemble vita-data/ja2-stracciatella/ -> copy to ux0:data/ja2-stracciatella/ (FTP / USB).
#   vita/stage-data.sh [path-to-JA2-install]   (default: the Wine/GOG install)
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
GAME="${1:-$HOME/.wine/drive_c/Program Files (x86)/Jagged Alliance 2}"
OUT=vita-data/ja2-stracciatella
V=ux0:data/ja2-stracciatella
[ -d "$GAME/Data" ] || { echo "no Data/ in $GAME" >&2; exit 1; }

mkdir -p "$OUT/data" "$OUT/.ja2"
# engine assets (EXTRA_DATA_DIR): externalized + mods
rsync -a --delete assets/externalized assets/mods "$OUT/"
# original game data (Tilecache is real data, Temp is runtime junk)
rsync -a --delete --exclude Temp "$GAME/Data" "$OUT/data/"
# saves, if any
[ -d "$HOME/.ja2/SavedGames" ] && rsync -a "$HOME/.ja2/SavedGames" "$OUT/.ja2/"

# don't clobber a config already tuned on the device
[ -f "$OUT/.ja2/ja2.json" ] || cat > "$OUT/.ja2/ja2.json" <<EOF
{
  "game_dir": "$V/data",
  "save_game_dir": "$V/.ja2/SavedGames",
  "mods": ["simplified-chinese-localization"],
  "res": "960x544",
  "resversion": "SIMPLIFIED_CHINESE",
  "fullscreen": true,
  "debug": true,
  "nosound": false
}
EOF
du -sh "$OUT"
