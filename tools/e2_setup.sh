#!/usr/bin/env bash
# Set up 1nput's ROM checks (make lockstep, make test, make golden-check) on a fresh Linux machine,
# with the fixes this needed on 2026-11-03 (plan step E2, docs/C_PORTS_HANDOFF.md):
#   * the asm reference pinned to NHL94Genesis af1b6f2, the commit src/symbols_gen.h was made from
#     (it assembles byte-exact under vasm with every symbol at the same address; later commits renamed labels)
#   * vasm from the GitHub mirror when sun.hasenbraten.de can't be reached
#   * Genesis Plus GX rebuilt with HOOK_CPU=1 (make oracle needs set_cpu_hook)
#   * out/callgraph.json and out/captures, which tools/hybrid/capture_all.sh expects
# Usage: tools/e2_setup.sh ROM      (the retail ROM, SHA-1 7b1489ab501258133bf03e5f0892b77d04312580)
# Then:  make oracle && make vectors && make lockstep
#        tools/hybrid/capture_all.sh && make test     (about 15 min, 2.5 GB in out/captures)
#        make golden && make golden-check
set -euo pipefail
cd "$(dirname "$0")/.."
ROM="${1:?usage: tools/e2_setup.sh ROM}"
ASM_PIN=af1b6f2
cp -n "$ROM" "NHL 94 (UE) [!].gen" 2>/dev/null || true
mkdir -p ref
if [ ! -x ref/vasm-src/vasm/vasmm68k_mot ] && ! curl -sfI -m 10 http://sun.hasenbraten.de/vasm/ >/dev/null; then
  rm -rf ref/vasm-src && mkdir -p ref/vasm-src
  git clone --depth 1 https://github.com/StarWolf3000/vasm-mirror.git ref/vasm-src/vasm
  tools/patches/vasm-build.sh ref/vasm-src/vasm
fi
tools/fetch_refs.sh
git -C ref/NHL94Genesis fetch --unshallow 2>/dev/null || true
git -C ref/NHL94Genesis checkout -q "$ASM_PIN"
cp "$ROM" ref/NHL94Genesis/nhl94retail.bin
rm -rf ref/NHL94Genesis/output build/asm
(cd ref/NHL94Genesis && node tools/prebuild.js)
tools/asm/build_vasm.sh
make -C ref/gpgx -f Makefile.libretro clean >/dev/null
make -C ref/gpgx -f Makefile.libretro HOOK_CPU=1 -j"$(nproc)" >/dev/null
python3 tools/callgraph.py
mkdir -p out/captures
echo "E2 setup done: now make oracle && make vectors && make lockstep"
