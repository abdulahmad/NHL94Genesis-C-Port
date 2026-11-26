#!/usr/bin/env python3
"""The Sega CD's scouting report (Ron Barr's voice, native/segacd.c and src/setup.c), from the outside:
make scouttest.

No disc is needed: a made-up one (an ISO 9660 data track with HOCKEY.BIN, GRAPH2.BIN's scouting text, a
clip of CLIP seconds for every line but the blank one, RON0001.PCM on, and every team's welcome) goes in a folder of
out/, and nhl94 runs headless there (the disc is looked for in the working folder too) with
--segacd-voice 1 and SEGACD_LOG. Then:

  1. The first paragraph (and his voice) comes up as the page opens, not after a wait.
  2. Each next paragraph waits for the voice before it, no longer: the gap is the clip's length
     (the game counts it in its own frames, not by Vcount, which waitx sets each frame).
  3. Start leaves while he talks: the game's next song comes up promptly (the fade out is over).
"""
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = '.exe' if os.name == 'nt' else ''
NHL94 = os.path.join(ROOT, 'build', 'nhl94' + EXE)
ROM = os.path.join(ROOT, 'NHL 94 (UE) [!].gen')
OUT = os.path.join(ROOT, 'out', 'scouttest')
CLIP = 2.0                                     # seconds of each clip
VOICE_RATE = 16276                             # the RF5C164's rate (native/segacd.c)
TEAMS = ["ANAHEIM", "BOSTON", "BUFFALO", "CALGORY", "CHICAGO", "DALLAS", "DETROIT", "EDMONTON", "FLORIDA", "HARTFORD",
         "LAKINGS", "MONTREAL", "NJERSEY", "NEWYORK", "NYRANGER", "OTTAWA", "PHILLY", "PITTSBUR", "QUEBEC", "SANJOSE",
         "STLOUIS", "TAMPBAY", "TORONTO", "VANCUVER", "WASHINGT", "WINNIPEG", "ALLSTARE", "ALLSTARW"]


def both32(v):
    return struct.pack('<I', v) + struct.pack('>I', v)


def dirent(name, lba, size, is_dir):
    n = len(name)
    e = bytearray(33 + n + (n % 2 == 0))
    e[0] = len(e)
    e[2:10] = both32(lba)
    e[10:18] = both32(size)
    e[25] = 2 if is_dir else 0
    e[32] = n
    e[33:33 + n] = name
    return bytes(e)


def make_disc(path):
    """a data track: the PVD at 16, the root from 18 (its first sector . and .., then the files')"""
    lines = b''.join(b'\r\n' if n == 0x1D else b'Line %d here.\r\n' % n for n in range(0x110))
    clip = b'\x90' * int(VOICE_RATE * CLIP) + b'\xff'
    files = [(b'HOCKEY.BIN', b'\0' * 64), (b'GRAPH2.BIN', struct.pack('>II', 8, 8 + len(lines)) + lines)]
    files += [(b'RON%04d.PCM' % n, clip) for n in range(1, 0x111) if n != 0x1D + 1]   # (the blank line, $1D: no clip)
    files += [(t.encode() + b'.PCM', clip) for t in TEAMS]
    sectors, cur = [], b''
    for name, _ in files:
        e = dirent(name + b';1', 0, 0, 0)
        if len(cur) + len(e) > 2048:
            sectors.append(cur)
            cur = b''
        cur += e
    sectors.append(cur)
    root, ndir = 18, len(sectors) + 1
    lba, at = root + ndir, []
    for _, data in files:
        at.append(lba)
        lba += (len(data) + 2047) // 2048
    img = bytearray(lba * 2048)
    img[16 * 2048:16 * 2048 + 7] = b'\1CD001\1'
    img[16 * 2048 + 156:16 * 2048 + 190] = dirent(b'\0', root, ndir * 2048, 1)
    img[17 * 2048:17 * 2048 + 6] = b'\xffCD001'
    dots = dirent(b'\0', root, ndir * 2048, 1) + dirent(b'\1', root, ndir * 2048, 1)
    img[root * 2048:root * 2048 + len(dots)] = dots
    sec, cur = root + 1, b''
    for (name, data), where in zip(files, at):
        e = dirent(name + b';1', where, len(data), 0)
        if len(cur) + len(e) > 2048:
            img[sec * 2048:sec * 2048 + len(cur)] = cur
            sec, cur = sec + 1, b''
        cur += e
    img[sec * 2048:sec * 2048 + len(cur)] = cur
    for (_, data), where in zip(files, at):
        img[where * 2048:where * 2048 + len(data)] = data
    with open(path, 'wb') as f:
        f.write(img)


def run(script, frames):
    with open(os.path.join(OUT, 'input.txt'), 'w') as f:
        f.write(script + '\n')
    env = dict(os.environ, SEGACD_LOG='1')
    r = subprocess.run([NHL94, '--rom', ROM, '--headless', '--frames', str(frames), '--input', 'input.txt', '--segacd-voice', '1',
                        '--sram', 'scout.srm'], cwd=OUT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        sys.exit('nhl94 failed (%d):\n%s' % (r.returncode, r.stderr[-2000:]))
    if not re.search(r'Sega CD disc: .*scout\.iso', r.stderr):
        sys.exit('the made-up disc was not the one found (a disc beside build/nhl94 comes first):\n' + r.stderr[-2000:])
    return [(int(m.group(1)), m.group(2), int(m.group(3), 16))
            for m in re.finditer(r'segacd: frame (\d+) (\w+) ([0-9A-F]+)', r.stderr)]


def main():
    for p in (NHL94, ROM):
        if not os.path.exists(p):
            sys.exit('missing %s' % p)
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    make_disc(os.path.join(OUT, 'scout.iso'))
    fails = 0

    def check(ok, what):
        nonlocal fails
        print('  %s %s' % ('ok  ' if ok else 'FAIL', what))
        fails += not ok

    clip = int((int(VOICE_RATE * CLIP) + 1) * 60 / VOICE_RATE + 0.999)
    ev = run('1200:START,1206:,1320:START,1326:', 2400)
    opened = [f for f, e, n in ev if e == 'song' and n == 0x7A]
    paras = [(f, n) for f, e, n in ev if e == 'paragraph']
    check(opened and paras, 'the scouting report opens and his paragraphs come (%d)' % len(paras))
    if opened and paras:
        check(paras[0][1] == 0x1B and paras[0][0] - opened[0] < 60,
              'the first paragraph ($%02X) %d frames after the page opens (< 60)' % (paras[0][1], paras[0][0] - opened[0]))
        voiced = [f for f, n in paras if n != 0x1D]       # (the blank lines between have no clip and do not wait)
        gaps = [b - a for a, b in zip(voiced, voiced[1:])]
        check(len(gaps) >= 4 and all(clip <= g <= clip + 60 for g in gaps[:4]),
              'each next paragraph after the clip before it (%d frames), no longer: %s' % (clip, gaps[:4]))
    ev = run('1200:START,1206:,1320:START,1326:,1700:START,1706:', 2000)
    off = [f for f, e, n in ev if e == 'turnoff' and f > 1700]
    song = [f for f, e, n in ev if e == 'song' and f > 1700]
    check(off and off[0] - 1700 < 60, 'Start while he talks: the report left %s frames later' % (off[0] - 1700 if off else None))
    check(song and song[0] - 1700 < 120, 'and the next song %s frames later' % (song[0] - 1700 if song else None))
    print('scouttest: %s' % ('ok' if not fails else '%d failed' % fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
