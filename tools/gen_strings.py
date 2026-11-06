#!/usr/bin/env python3
"""Generate C byte arrays for the inline String data that follows printz / printsmallz / appendz /
printbigz calls.

Accepts one or more current 94G asm files (String / StringStart, or already-expanded dc.w / dc.b).
Output header names stay. If OUT.h already exists, only the str_* names it already has are
emitted (so checks/input from logic94.asm and attract/data/setup from setup94.asm do not
duplicate). Array values must match the committed header; banners and source comments may change.

  python3 tools/gen_strings.py FILE.asm [FILE.asm ...] -o OUT.h
Each string is named str_<routine>_<n> (n counts the calls inside the routine)."""
import json, pathlib, re, sys

args = sys.argv[1:]
if '-o' not in args:
    sys.exit('usage: gen_strings.py FILE.asm [FILE.asm ...] -o OUT.h')
out = pathlib.Path(args[args.index('-o') + 1])
srcs = [pathlib.Path(p) for p in args[:args.index('-o')]]
if not srcs:
    sys.exit('gen_strings.py: no input files')
root = pathlib.Path(__file__).resolve().parent.parent

CALL = re.compile(r'\b(bsr|jsr)(\.\w)?\s+\(?(printz|printsmallz|appendz|printbigz)\b')
ARRAY = re.compile(r'static const uint8_t (str_\w+)\[(\d+)\] = \{ ([^}]+) \};')


def split_args(s):
    out, cur, q = [], '', False
    for ch in s:
        if ch == "'":
            q = not q
        if ch == ',' and not q:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def load_equates(paths):
    eq = {}
    pin = root / 'build' / 'asm' / 'symbols.json'
    if pin.exists():
        raw = json.load(open(pin))
        raw = raw.get('symbols', raw)
        for k, v in raw.items():
            if isinstance(v, int):
                eq[k] = v
            elif isinstance(v, dict) and 'addr' in v:
                eq[k] = v['addr']
    ram = None
    for p in paths:
        cand = p.parent / 'ram94.asm'
        if cand.exists():
            ram = cand
            break
    if ram:
        for line in ram.read_text(errors='replace').splitlines():
            m = re.match(r'^([A-Za-z_]\w*)\s+equ\s+(\S+)', line.split(';')[0])
            if not m:
                continue
            tok = m.group(2)
            if re.match(r'^\$[0-9A-Fa-f]+$', tok):
                eq[m.group(1)] = int(tok[1:], 16)
            elif re.match(r'^-\$[0-9A-Fa-f]+$', tok):
                eq[m.group(1)] = -int(tok[2:], 16)
            elif re.match(r'^-?\d+$', tok):
                eq[m.group(1)] = int(tok)
    return eq


eq = load_equates(srcs)


def evaluate(expr):
    expr = expr.strip()

    def repl(m):
        tok = m.group(0)
        if tok.startswith('$'):
            return str(int(tok[1:], 16))
        if re.match(r'^\d+$', tok):
            return tok
        if tok in eq:
            return str(eq[tok])
        raise ValueError('unknown symbol %s in %s' % (tok, expr))

    py = re.sub(r"\$[0-9A-Fa-f]+|[A-Za-z_]\w*|\d+", repl, expr)
    return eval(py, {"__builtins__": {}}) & 0xFF


def word_bytes(v):
    v &= 0xFFFF
    return [(v >> 8) & 0xFF, v & 0xFF] if v > 0xFF else [0, v]


def dc_word_vals(line):
    vals = []
    for tok in re.findall(r"'(?:[^']|'')*'|[^,]+", line):
        tok = tok.strip()
        if not tok:
            continue
        if tok.startswith("'"):
            vals += [ord(c) for c in tok[1:-1].replace("''", "'")]
            continue
        if tok.startswith('$'):
            v = int(tok[1:], 16) & 0xFFFF
        elif tok.startswith('-$'):
            v = -int(tok[2:], 16) & 0xFFFF
        elif re.match(r'^-?\d+$', tok):
            v = int(tok) & 0xFFFF
        else:
            # expressions (rare on dc.w length); byte-eval is wrong for >255
            v = evaluate(tok)
            if v <= 0xFF:
                vals += [0, v]
                continue
        vals += word_bytes(v)
    return vals


def string_macro_bytes(argstr):
    vals = []
    for a in split_args(argstr):
        if not a:
            continue
        if a.startswith("'"):
            vals += [ord(c) for c in a[1:-1].replace("''", "'")]
        else:
            vals.append(evaluate(a))
    length = 2 + len(vals)
    if length & 1:
        vals.append(0)
        length += 1
    return word_bytes(length) + vals


def read_dc_block(lines, j):
    vals = []
    while j < len(lines):
        b = lines[j].split(';')[0]
        m2 = re.search(r'\bdc\.([bw])\s+(.*)', b)
        if not m2:
            break
        if m2.group(1) == 'w':
            vals += dc_word_vals(m2.group(2))
        else:
            for tok in re.findall(r"'(?:[^']|'')*'|[^,]+", m2.group(2)):
                tok = tok.strip()
                if not tok:
                    continue
                if tok.startswith("'"):
                    vals += [ord(c) for c in tok[1:-1].replace("''", "'")]
                else:
                    vals.append(evaluate(tok))
        j += 1
    return vals, j


def payload_after_call(lines, i):
    j = i + 1
    while j < len(lines) and not lines[j].split(';')[0].strip():
        j += 1
    if j >= len(lines):
        return None
    body = lines[j].split(';')[0].strip()
    if body.startswith('StringStart'):
        vals, k = read_dc_block(lines, j + 1)
        while k < len(lines) and not re.search(r'\bStringEnd\b', lines[k].split(';')[0]):
            k += 1
        if k < len(lines):
            k += 1
        length = 2 + len(vals)
        if length & 1:
            vals.append(0)
            length += 1
        return word_bytes(length) + vals, k
    if re.match(r'^String\b', body) and not body.startswith('StringEnd'):
        argstr = body.split(None, 1)[1] if body.split(None, 1)[1:] else ''
        return string_macro_bytes(argstr), j + 1
    if re.search(r'\bdc\.[bw]\b', body):
        vals, k = read_dc_block(lines, j)
        return (vals, k) if vals else None
    return None


found = {}  # name -> (vals, comment)
routine, counts = None, {}
for src in srcs:
    lines = src.read_text(errors='replace').splitlines()
    i = 0
    while i < len(lines):
        l = lines[i]
        body = l.split(';')[0]
        m = re.match(r'^([A-Za-z_]\w*)', body)
        if m and not body.startswith('.'):
            routine = m.group(1)
        call = CALL.search(body)
        if call and routine:
            got = payload_after_call(lines, i)
            if got:
                vals, nxt = got
                n = counts.get(routine, 0) + 1
                counts[routine] = n
                name = 'str_%s_%d' % (routine, n)
                found[name] = (vals, '%s:%d %s' % (src.name, i + 1, call.group(3)))
                i = nxt
                continue
        i += 1

keep = None
old_vals = {}
old_order = []
if out.exists():
    text = out.read_text()
    for m in ARRAY.finditer(text):
        name, _n, body = m.group(1), m.group(2), m.group(3)
        old_order.append(name)
        old_vals[name] = [int(x.strip()) for x in body.split(',') if x.strip()]
    keep = set(old_order)

changed = []
missing = []
names = old_order if keep is not None else list(found)
if keep is not None:
    missing = [n for n in old_order if n not in found]
    for n in old_order:
        if n in found and found[n][0] != old_vals[n]:
            changed.append(n)

if missing or changed:
    if missing:
        print('FAIL: missing strings in %s: %s' % (out, ' '.join(missing)), file=sys.stderr)
    if changed:
        for n in changed:
            print('FAIL: %s values changed\n  old %s\n  new %s' % (n, old_vals[n], found[n][0]),
                  file=sys.stderr)
    sys.exit(1)

banner = ', '.join(s.name for s in srcs)
res = ['/* Generated by tools/gen_strings.py from %s. Do not edit. */' % banner,
       '#pragma once', '#include <stdint.h>', '']
emitted = 0
for name in names:
    if keep is not None and name not in keep:
        continue
    vals, comment = found[name]
    res.append('static const uint8_t %s[%d] = { %s };   /* %s */' % (
        name, len(vals), ', '.join(str(v) for v in vals), comment))
    emitted += 1
out.write_text('\n'.join(res) + '\n')
print('%d strings -> %s' % (emitted, out))
