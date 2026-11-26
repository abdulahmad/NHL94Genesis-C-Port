#!/usr/bin/env bash
# The vanilla gate (docs/VANILLA.md): the cartridge's game stays the original, every customization stays outside it.
#   tools/check_vanilla.sh [--no-build]
# Fails when
#   1. src/ (the cartridge's game) includes a host header (native/, Overbar, PulseMan, SDL) or reads a setting
#      (getenv, the settings file) outside the mod files still listed in MOD_FILES below;
#   2. src/ reads a rule other than through RULE() (src/rules.h), which the vanilla build compiles out;
#   3. make VANILLA=1 nhl94 (the game with the extras left out) does not build.
# MOD_FILES are customizations that still live in src/ and are moving out to their own layer; the list only shrinks.
set -uo pipefail
cd "$(dirname "$0")/.."
MOD_FILES='mods/hold.c'
bad=0
game=$(ls src/*.c src/*.h | grep -vxF -f <(tr ' ' '\n' <<< "$MOD_FILES"))
if grep -nE '#include *[<"](SDL|native\.h|overbar/|mdrt/|optionsui\.h|announcer\.h|pcspr\.h|pcsfx\.h|segacd\.h|hats\.h|wide\.h|whiteice\.h|stands\.h|cardcolor)' $game; then
    echo "vanilla: src/ includes a host header (above)"; bad=1
fi
if grep -nwE 'getenv|settings_[a-z_]+|ob_[a-z_]+\(' $game; then
    echo "vanilla: src/ reads a setting or calls the host (above)"; bad=1
fi
if grep -nE '\brules\[' $(grep -vxE "src/rules\.[ch]" <<< "$game"); then
    echo "vanilla: src/ reads a rule as rules[...] (above): use RULE(), which the vanilla build makes the cartridge's 0"; bad=1
fi
if [ "${1:-}" != --no-build ]; then
    if ! make -s VANILLA=1 nhl94 >/tmp/check_vanilla_build.log 2>&1; then
        tail -20 /tmp/check_vanilla_build.log; echo "vanilla: make VANILLA=1 nhl94 failed (above)"; bad=1
    fi
fi
[ $bad = 0 ] && echo "vanilla: PASS"
exit $bad
