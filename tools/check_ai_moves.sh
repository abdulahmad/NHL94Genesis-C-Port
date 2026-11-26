#!/bin/sh
# Regression check: with the PC options on (anthem, sprites, referee, logos, announcer timing, between-play pause), the skaters
# and the AI must move after the opening face off. Usage: tools/check_ai_moves.sh ROM
ROM=${1:-/tmp/nhl94.bin}
OUT=$(HOLDMOVE=1 timeout 900 build/nhl94 --rom "$ROM" --headless --boot --input tools/oracle/scripts/full_game_30s.txt --frames 7000 \
  --pc-anthems --pc-sprites --pc-ref --pc-logos --white-ice --rule hold_play=on --rule announce_timing=1 2>&1 | grep '^holdmove')
# The opening face off drops at about frame 1200 in this script: the skaters must be moving within 3 samples of it, and keep moving later.
echo "$OUT" | awk '{ if ($3 >= 1200 && $3 <= 1440 && $5 > 20000) early++; if ($5 > 20000) moving++ }
  END { if (early >= 2 && moving >= 8) { print "ok: " moving " samples with the skaters moving"; exit 0 } print "FAIL: the skaters stand still after the opening face off (early " early+0 ", total " moving+0 ")"; exit 1 }'
