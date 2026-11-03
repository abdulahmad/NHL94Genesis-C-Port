#!/usr/bin/env python3
"""The names gate: does every C function that is an original routine carry the asm repository's
current name, and sit in the C file of the asm module that holds it?

An original routine is a C function with a SYM_<name> address in symbols_gen.h (the rule
tools/routines.py uses for docs/ROUTINES.md). Each one gets a status:

  ok            the name is a current asm label, in the module this C file stands for
  other_module  the name is a current asm label, but the asm keeps it in another module
  renamed       the asm renamed it since (docs/name_map.csv): the new name is given, and the C
                name it becomes (an asm local Parent.x is the C function Parent_x)
  missing       no current label and no rename found (look by hand)

Genesis: labels come from the asm sources (src/*94.asm), renames from docs/name_map.csv (old,new).
SNES: labels, addresses and files come from docs/name_map.csv (address,name,...,file), so a
function is matched by its cartridge address as well as its name.

  python3 tools/check_names.py                 # summary; writes out/names_status.csv
  python3 tools/check_names.py --list renamed  # the rows of one status
  python3 tools/check_names.py --check         # the gate: fails if any status but ok grew
                                                 # past tools/names_baseline.json
  python3 tools/check_names.py --write-baseline

The asm repository is found at --asm, else $NHL_ASM, else ../<name> or ref/<name>
(NHL94Genesis or NHL94SNES)."""
import argparse
import csv
import json
import os
import re
import sys
from collections import Counter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
DEF = re.compile(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;]*\)\s*\{?\s*$')
SYM = re.compile(r'^#define SYM_(\w+) 0x([0-9A-Fa-f]+)u?')
LABEL = re.compile(r'^([A-Za-z_]\w*):?(?:\s|$)')
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof'}
# C files named for an older module than the asm's: the asm module each one's routines go to is
# decided per routine, so these are only the names that are the same module by another name
ALIAS = {'start': 'main'}


def c_dir():
    for d in ('game', 'src'):
        if os.path.isdir(os.path.join(ROOT, d)):
            return os.path.join(ROOT, d)
    sys.exit('check_names.py: no game/ or src/ here')


def find_asm(arg, platform):
    name = 'NHL94SNES' if platform == 'snes' else 'NHL94Genesis'
    # (../NAME before ref/NAME: the gate compares with the asm as it is now, and ref/ may be pinned to
    # the commit the C's symbols were made from, as tools/e2_setup.sh pins it)
    for cand in (arg, os.environ.get('NHL_ASM'), os.path.join(ROOT, '..', name),
                 os.path.join(ROOT, 'ref', name)):
        if cand and os.path.isdir(os.path.join(cand, 'src')):
            return os.path.normpath(cand)
    sys.exit('check_names.py: no %s checkout (give --asm, set NHL_ASM, or clone it beside this '
             'repository)' % name)


def symbols(cdir):
    syms = {}
    for base, _, files in os.walk(cdir):
        if 'symbols_gen.h' in files:
            for line in open(os.path.join(base, 'symbols_gen.h'), encoding='utf-8'):
                m = SYM.match(line)
                if m:
                    syms[m.group(1)] = int(m.group(2), 16)
    return syms


def c_functions(cdir, syms):
    out = []
    for base, _, files in os.walk(cdir):
        for f in sorted(files):
            if not f.endswith('.c'):
                continue
            path = os.path.join(base, f)
            module = ALIAS.get(f[:-2], f[:-2])
            for line in open(path, encoding='utf-8', errors='replace'):
                if line.startswith((' ', '\t', '#', '/', '*', '}')):
                    continue
                m = DEF.match(line.rstrip('\r\n'))
                if m and m.group(1) not in KEYWORDS and m.group(1) in syms:
                    out.append((os.path.relpath(path, ROOT).replace(os.sep, '/'), module, m.group(1)))
    return out


def genesis_asm(asm):
    labels = {}
    files = []
    for base, dirs, names in os.walk(os.path.join(asm, 'src')):
        dirs[:] = sorted(d for d in dirs if d != 'segacd')
        files += [os.path.join(base, n) for n in sorted(names) if n.endswith('.asm')]
    for path in files:
        rel = os.path.relpath(path, os.path.join(asm, 'src')).split(os.sep)
        module = rel[0] if len(rel) > 1 else re.sub(r'94$', '', rel[0][:-4])
        module = {'nhl': 'main'}.get(module, module)
        parent = None
        for line in open(path, encoding='latin-1'):
            m = LABEL.match(line)
            if m:
                parent = m.group(1)
                labels.setdefault(parent.lower(), (parent, module))
            elif parent and re.match(r'^\.(\w+):?(\s|$)', line):
                loc = parent + '.' + re.match(r'^\.(\w+)', line).group(1)
                labels.setdefault(loc.lower(), (loc, module))
    renames = {}
    path = os.path.join(asm, 'docs', 'name_map.csv')
    if os.path.exists(path):
        for row in csv.DictReader(open(path, encoding='utf-8')):
            if row.get('old') and row.get('new'):
                renames[row['old'].lower()] = row['new']
    return labels, renames, {}


def snes_asm(asm):
    labels, by_addr = {}, {}
    path = os.path.join(asm, 'docs', 'name_map.csv')
    for row in csv.DictReader(open(path, encoding='utf-8')):
        name, f = row.get('name') or '', row.get('file') or ''
        if not name:
            continue
        if name.startswith('.'):           # a local: its routine is the file's
            name = os.path.splitext(os.path.basename(f))[0] + name
        parts = f.split('/')
        module = parts[2] if len(parts) > 3 and parts[1] == 'routines' else (parts[1] if len(parts) > 1 else '')
        labels.setdefault(name.lower(), (name, module))
        try:
            by_addr[int(row['address'].lstrip('$'), 16)] = (name, module)
        except (KeyError, ValueError):
            pass
    return labels, {}, by_addr


def c_of(label):
    """the C name of an asm label: a local Parent.x is the C function Parent_x"""
    return label.replace('.', '_')


def by_c_name(labels):
    """the labels also under their C names (Parent_x for Parent.x)"""
    for k, v in list(labels.items()):
        labels.setdefault(c_of(k), v)
    return labels


def current(name, labels, renames):
    seen = set()
    key = name.lower()
    while key in renames and key not in seen:
        seen.add(key)
        key = renames[key].lower()
    return labels.get(key)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--asm')
    ap.add_argument('--list', metavar='STATUS')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--write-baseline', action='store_true')
    a = ap.parse_args()

    cdir = c_dir()
    syms = symbols(cdir)
    platform = 'snes' if any(a > 0x7FFFFF for a in syms.values()) else 'genesis'
    asm = find_asm(a.asm, platform)
    labels, renames, by_addr = (snes_asm if platform == 'snes' else genesis_asm)(asm)
    labels = by_c_name(labels)

    rows = []
    for path, module, name in c_functions(cdir, syms):
        addr = syms[name]
        hit = labels.get(name.lower())
        if platform == 'snes' and addr in by_addr and c_of(by_addr[addr][0]).lower() != name.lower():
            hit = None                     # the address now carries another name: a rename
            new = by_addr[addr]
        else:
            new = current(name, labels, renames) if hit is None else None
        if hit and c_of(hit[0]) == name and hit[1] == module:
            rows.append((path, name, '%06X' % addr, 'ok', hit[1], ''))
        elif hit and c_of(hit[0]) == name:
            rows.append((path, name, '%06X' % addr, 'other_module', hit[1], ''))
        elif hit:                          # same label, other case
            rows.append((path, name, '%06X' % addr, 'renamed', hit[1], hit[0]))
        elif new:
            rows.append((path, name, '%06X' % addr, 'renamed', new[1], new[0]))
        else:
            rows.append((path, name, '%06X' % addr, 'missing', '', ''))

    os.makedirs(os.path.join(ROOT, 'out'), exist_ok=True)
    with open(os.path.join(ROOT, 'out', 'names_status.csv'), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['c_file', 'function', 'address', 'status', 'asm_module', 'asm_name', 'c_name'])
        w.writerows(r + (r[5].replace('.', '_'),) for r in rows)

    counts = Counter(r[3] for r in rows)
    print('%s, asm %s: %d original routines: %s' % (platform, asm, len(rows),
          ', '.join('%s %d' % (k, counts.get(k, 0)) for k in ('ok', 'other_module', 'renamed', 'missing'))))
    if a.list:
        for r in rows:
            if r[3] == a.list:
                print('  %-34s %-28s %s %-10s %s' % (r[0], r[1], r[2], r[4], r[5]))

    base_path = os.path.join(ROOT, 'tools', 'names_baseline.json')
    if a.write_baseline:
        json.dump({k: counts.get(k, 0) for k in ('ok', 'other_module', 'renamed', 'missing')},
                  open(base_path, 'w'), indent=1)
        print('wrote tools/names_baseline.json')
    if a.check:
        base = json.load(open(base_path))
        worse = [k for k in ('other_module', 'renamed', 'missing') if counts.get(k, 0) > base.get(k, 0)]
        if worse:
            print('FAIL: more %s than tools/names_baseline.json (%s)' % (
                ', '.join(worse), ', '.join('%s %d > %d' % (k, counts.get(k, 0), base[k]) for k in worse)))
            return 1
        better = [k for k in ('other_module', 'renamed', 'missing') if counts.get(k, 0) < base.get(k, 0)]
        print('PASS' + (' (fewer %s: run --write-baseline to lock it in)' % ', '.join(better) if better else ''))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(0)
