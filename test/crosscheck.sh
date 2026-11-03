#!/usr/bin/env bash
# The gens94-c cross-check: play gens94-c's scripted runs on this build and compare each run's golden
# file with gens94-c's test/expected.txt (docs/C_PORTS_HANDOFF.md, plan steps E1 and E4). gens94-c is
# INDIO's cut of this game to the original alone, so the two must play the same frames.
#   test/crosscheck.sh ROM [GENS94C] [NHL94]    GENS94C: the gens94-c checkout (default ../gens94-c),
#                                               NHL94: the build to check (default build/nhl94)
set -uo pipefail
cd "$(dirname "$0")/.."
rom=${1:?usage: test/crosscheck.sh ROM [GENS94C] [NHL94]}
g=${2:-../gens94-c}
bin=${3:-build/nhl94}
out=out/crosscheck
mkdir -p "$out"
pids=()
while read -r want frames name script opts; do
    [ "${want#\#}" != "$want" ] && continue
    timeout 900 "$bin" --rom "$rom" --frames "$frames" --golden "$out/$name.golden" \
        --input "$g/test/scripts/${script:-$name}.txt" $opts > "$out/$name.log" 2>&1 &
    pids+=("$!:$want:$name")
done < "$g/test/expected.txt"
bad=0
for p in "${pids[@]}"; do
    IFS=: read -r pid want name <<< "$p"
    wait "$pid" || { echo "$name: did not run ($(tail -1 "$out/$name.log"))"; bad=1; continue; }
    got=$(tr -d '\r' < "$out/$name.golden" | sha256sum | cut -c1-64)
    if [ "$got" = "$want" ]; then echo "$name: identical"; else echo "$name: DIFFERENT"; bad=1; fi
done
exit $bad
