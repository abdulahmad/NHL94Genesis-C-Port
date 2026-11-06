#!/usr/bin/env python3
"""ROM-order gate for one C module.

Compares original C defs in src/<module>.c with 94G globals in <module>94.asm
that have a C function. Skips static C helpers and asm data labels (and unused
asm code with SYM_ but no C). Does not fail the whole tree: pass --module M.

Self-test (leftover #4): green on arena or title, red on fourway or menu.

  python3 tools/check_rom_order.py --module arena
  python3 tools/check_rom_order.py --asm ../nhl94genesis --module fourway
"""
import argparse
import os
import sys

import check_names as cn

ROOT = cn.ROOT


def find_asm(arg):
    name = 'NHL94Genesis'
    cands = (
        arg,
        os.environ.get('NHL_ASM'),
        os.path.join(ROOT, '..', 'nhl94genesis'),
        os.path.join(ROOT, '..', name),
        os.path.join(ROOT, 'ref', name),
    )
    for cand in cands:
        if cand and os.path.isdir(os.path.join(cand, 'src')):
            return os.path.normpath(cand)
    sys.exit('check_rom_order.py: no NHL94Genesis checkout (give --asm, set NHL_ASM, '
             'or clone it beside this repository)')


def c_path(cdir, module):
    name = 'start' if module == 'main' and not os.path.isfile(os.path.join(cdir, 'main.c')) else module
    path = os.path.join(cdir, name + '.c')
    if not os.path.isfile(path):
        sys.exit('check_rom_order.py: no %s' % os.path.relpath(path, ROOT).replace(os.sep, '/'))
    return path


def asm_path(asm, module):
    path = os.path.join(asm, 'src', module + '94.asm')
    if os.path.isfile(path):
        return path
    sys.exit('check_rom_order.py: no %s94.asm under %s/src' % (module, asm))


def nonstatic_originals(cdir, syms):
    """Non-static C defs that have SYM_, in source order per file."""
    by_file = {}
    all_names = set()
    for base, _, files in os.walk(cdir):
        for f in sorted(files):
            if not f.endswith('.c'):
                continue
            path = os.path.join(base, f)
            rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
            names = []
            for line in open(path, encoding='utf-8', errors='replace'):
                if line.startswith((' ', '\t', '#', '/', '*', '}')):
                    continue
                if line.startswith(('static ', 'static\t')):
                    continue
                m = cn.DEF.match(line.rstrip('\r\n'))
                if m and m.group(1) not in cn.KEYWORDS and m.group(1) in syms:
                    names.append(m.group(1))
                    all_names.add(m.group(1))
            by_file[rel] = names
    return by_file, all_names


def asm_globals_with_c(path, have_c):
    """94G globals in file order whose name is a C original (skip data / unused)."""
    out = []
    seen = set()
    for line in open(path, encoding='latin-1'):
        m = cn.LABEL.match(line)
        if not m:
            continue
        name = m.group(1)
        cname = cn.c_of(name)
        if cname not in have_c or cname in seen:
            continue
        seen.add(cname)
        out.append(cname)
    return out


def fmt(names):
    return ' '.join(names) if names else '(none)'


def check_module(module, cdir, asm, by_file, have_c):
    c_file = c_path(cdir, module)
    a_file = asm_path(asm, module)
    rel = os.path.relpath(c_file, ROOT).replace(os.sep, '/')
    c_names = by_file.get(rel, [])
    a_names = asm_globals_with_c(a_file, have_c)
    if c_names == a_names:
        print('%s: %d original routines, ROM order OK' % (module, len(c_names)))
        return 0
    print('%s: ROM order mismatch (%d C, %d asm with C)' % (module, len(c_names), len(a_names)))
    extra = [n for n in c_names if n not in set(a_names)]
    missing = [n for n in a_names if n not in set(c_names)]
    if extra:
        print('  extra in C:   %s' % fmt(extra))
    if missing:
        print('  missing in C: %s' % fmt(missing))
    print('  asm: %s' % fmt(a_names))
    print('  c:   %s' % fmt(c_names))
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--asm')
    ap.add_argument('--module', action='append', metavar='M',
                    help='C / 94G module stem (repeatable). Required.')
    a = ap.parse_args()
    if not a.module:
        ap.error('pass --module M (does not fail the whole tree; G5 checks one module)')

    cdir = cn.c_dir()
    syms = cn.symbols(cdir)
    asm = find_asm(a.asm)
    by_file, have_c = nonstatic_originals(cdir, syms)

    rc = 0
    for module in a.module:
        rc |= check_module(module, cdir, asm, by_file, have_c)
    return rc


if __name__ == '__main__':
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(0)
