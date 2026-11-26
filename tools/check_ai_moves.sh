#!/bin/sh
# Regression check: with the PC options on (anthem, sprites, referee, logos, announcer timing, between-play pause), the skaters
# and the AI must move after the opening face off. Usage: tools/check_ai_moves.sh ROM
ROM=${1:-/tmp/nhl94.bin}
OUT=$(HOLDMOVE=1 timeout 900 build/nhl94 --rom "$ROM" --headless --boot --input tools/oracle/scripts/full_game_30s.txt --frames 7000 \
  --pc-anthems --pc-sprites --pc-ref --pc-logos --white-ice --rule hold_play=on --rule announce_timing=1 2>&1 | grep '^holdmove')
echo "$OUT" | awk '{ if ($6 > 20000) moving++ } END { if (moving >= 8) { print "ok: " moving " samples with the skaters moving"; exit 0 } print "FAIL: the skaters hardly move after the face off (" moving+0 ")"; exit 1 }'
