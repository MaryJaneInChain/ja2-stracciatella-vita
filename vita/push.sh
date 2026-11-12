#!/bin/sh
# Upload to the Vita over FTP (VitaShell: press SELECT to start the FTP server).
#   vita/push.sh            VPK + all of vita-data/ja2-stracciatella
#   vita/push.sh vpk        VPK + eboot.bin of the installed app only
# VITA=ip:port overrides the default address.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
URL="ftp://${VITA:-192.168.50.196:1337}"
up() { curl -s --noproxy '*' --ftp-create-dirs -T "$1" "$URL/$2" || { echo "FAILED: $1" >&2; exit 1; }; }

up build-vita/ja2-stracciatella.vpk ux0:/data/ja2-stracciatella.vpk
echo "vpk -> ux0:data/ja2-stracciatella.vpk"
# once installed, swapping eboot.bin is enough (no reinstall)
curl -s --noproxy '*' -T build-vita/eboot.bin "$URL/ux0:/app/JAST00001/eboot.bin" \
	&& echo "eboot -> ux0:app/JAST00001" || echo "app not installed yet: install the vpk in VitaShell"
[ "$1" = vpk ] && exit 0

cd vita-data
# skip runtime files left by Vita3K runs (vita-data is symlinked into its ux0:data)
files() { find ja2-stracciatella -type f ! -name ja2.log ! -path "*/ja2-stracciatella-tmp/*"; }
n=$(files | wc -l); i=0
# ja2.json is never overwritten if it already exists on the device
files | while read -r f; do
	i=$((i+1))
	if [ "$f" = ja2-stracciatella/.ja2/ja2.json ] && curl -s --noproxy '*' -I "$URL/ux0:/data/$f" >/dev/null 2>&1; then
		echo "keep existing $f"; continue
	fi
	up "$f" "ux0:/data/$f"
	[ $((i % 50)) -eq 0 ] && echo "$i/$n"
done
echo done
