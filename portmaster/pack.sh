#!/bin/bash
# Assemble nhl94g.zip from this tree. Optional: copy an aarch64 binary in.
#   ./portmaster/pack.sh [path/to/nhl94g.aarch64]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-$ROOT/out/nhl94g.zip}"
STAGE="$ROOT/out/portmaster-nhl94g"
BIN="${1:-}"

rm -rf "$STAGE"
mkdir -p "$STAGE/nhl94g/roms" "$STAGE/nhl94g/conf" "$STAGE/nhl94g/libs.aarch64" "$STAGE/nhl94g/licenses"
cp "$ROOT/portmaster/NHL 94 Genesis.sh" "$STAGE/"
chmod +x "$STAGE/NHL 94 Genesis.sh"
cp "$ROOT/portmaster/nhl94g/port.json" "$ROOT/portmaster/nhl94g/gameinfo.xml" \
   "$ROOT/portmaster/nhl94g/nhl94g.gptk" "$ROOT/portmaster/nhl94g/settings.ini" \
   "$ROOT/portmaster/nhl94g/README.md" "$STAGE/nhl94g/"
cp "$ROOT/portmaster/nhl94g/licenses/NOTICE.txt" "$STAGE/nhl94g/licenses/"
# Duplicate the launch script inside the port folder (PortMaster accepts either).
cp "$ROOT/portmaster/NHL 94 Genesis.sh" "$STAGE/nhl94g/"
if [ -n "$BIN" ]; then
  cp "$BIN" "$STAGE/nhl94g/nhl94g.aarch64"
  chmod +x "$STAGE/nhl94g/nhl94g.aarch64"
fi
mkdir -p "$(dirname "$OUT")"
(cd "$STAGE" && zip -rq "$OUT" "NHL 94 Genesis.sh" nhl94g)
echo "wrote $OUT"
