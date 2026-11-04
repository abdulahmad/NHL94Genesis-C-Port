#!/usr/bin/env python3
"""Which of the game's routines the checks reach (plan step E5).

Build with coverage, run the checks, then summarise:

  make CC="gcc --coverage" nhl94          # a separate checkout or worktree: it rebuilds build/
  bash test/crosscheck.sh ROM             # and/or make golden-check, make lockstep
  python3 tools/coverage.py               # writes test/coverage.txt

test/coverage.txt lists, per game module (src/*.c), how many functions and lines ran, then every
original routine (a function with a SYM_ address) that never ran, and the partly-run ones with
their line coverage. A routine that is never reached was never compared with the cartridge either,
so the list is the to-do list for new scripts (or a reason it can't be reached)."""
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OBJ = os.path.join(ROOT, 'build', 'obj', 'src')
SYM = re.compile(r'^#define SYM_(\w+) 0x')


def originals():
    names = set()
    for line in open(os.path.join(ROOT, 'src', 'symbols_gen.h'), encoding='utf-8'):
        m = SYM.match(line)
        if m:
            names.add(m.group(1))
    return names


def gcov(module):
    """{function: (lines, executed lines)} from gcov's function summary of src/<module>.c"""
    gcda = os.path.join(OBJ, module + '.gcda')
    if not os.path.exists(gcda):
        return None
    r = subprocess.run(['gcov', '-n', '-f', '-o', OBJ, os.path.join(ROOT, 'src', module + '.c')],
                       cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    out, fn = {}, None
    for line in r.stdout.splitlines():
        m = re.match(r"Function '(\w+)'", line)
        if m:
            fn = m.group(1)
            continue
        m = re.match(r'Lines executed:([\d.]+)% of (\d+)', line)
        if m and fn:
            total = int(m.group(2))
            out[fn] = (total, round(float(m.group(1)) * total / 100))
            fn = None
    return out


def main():
    syms = originals()
    modules = sorted(f[:-2] for f in os.listdir(os.path.join(ROOT, 'src')) if f.endswith('.c'))
    rows, never, partly = [], [], []
    for mod in modules:
        fns = gcov(mod)
        if fns is None:
            continue
        orig = {f: v for f, v in fns.items() if f in syms}
        ran = sum(1 for v in orig.values() if v[1] > 0)
        lines = sum(v[0] for v in orig.values())
        hit = sum(v[1] for v in orig.values())
        rows.append((mod, len(orig), ran, lines, hit))
        for f, (t, e) in sorted(orig.items()):
            if e == 0:
                never.append((mod, f, t))
            elif e < t:
                partly.append((mod, f, t, e))
    if not rows:
        sys.exit('coverage.py: no .gcda files in build/obj/src (build with CC="gcc --coverage", then run the checks)')
    n = sum(r[1] for r in rows)
    ran = sum(r[2] for r in rows)
    lines = sum(r[3] for r in rows)
    hit = sum(r[4] for r in rows)
    out = ['# Coverage of the original routines by the checks that were run (tools/coverage.py)',
           '# %d of %d routines ran (%.1f%%); %d of %d of their lines (%.1f%%)' % (ran, n, 100.0 * ran / n, hit, lines, 100.0 * hit / lines),
           '', '## By module (routines ran / routines, lines ran / lines)', '']
    for mod, k, r, t, e in rows:
        out.append('%-10s %4d / %-4d  %6d / %-6d %5.1f%%' % (mod, r, k, e, t, 100.0 * e / t if t else 100.0))
    out += ['', '## Never ran (%d)' % len(never), '']
    out += ['%-10s %-34s %d lines' % (m, f, t) for m, f, t in never]
    out += ['', '## Partly ran (%d): lines ran / lines' % len(partly), '']
    out += ['%-10s %-34s %d / %d' % (m, f, e, t) for m, f, t, e in sorted(partly, key=lambda x: x[3] / x[2])]
    open(os.path.join(ROOT, 'test', 'coverage.txt'), 'w').write('\n'.join(out) + '\n')
    print(out[1].lstrip('# '))
    print('wrote test/coverage.txt')


if __name__ == '__main__':
    main()
