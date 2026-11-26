#!/usr/bin/env python3
"""The window's save RAM between runs, from the outside: make sramtest.

The program is copied to out/sramtest/prog and its window runs with SDL's dummy video and audio
(NHL94_QUIT_AFTER closes it, 12 seconds in), each time started from another working folder, as a shortcut, a
terminal elsewhere, a file dropped on the program or the Finder can start it:

  1. The first run: the save RAM (a new cartridge's, which the game writes at power on) is beside the
     program in out/nhl94.srm, as settings.ini and overbar.ini are, and not in the working folder.
  2. A change to it (a byte the game keeps, its checksum made again, as the game would) is still there
     after a second run from another folder: the game read it (one it had not read it would clear and
     write back).
  3. A save RAM an earlier build kept in the working folder is taken up beside the program when there
     is none there yet.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = '.exe' if os.name == 'nt' else ''
NHL94 = os.path.join(ROOT, 'build', 'nhl94' + EXE)
ROM = os.path.join(ROOT, 'NHL 94 (UE) [!].gen')
OUT = os.path.join(ROOT, 'out', 'sramtest')
PROG = os.path.join(OUT, 'prog')
SRM = os.path.join(PROG, 'out', 'nhl94.srm')
AT, MARK = 0x1F00, 0x5A                        # (a byte of save RAM the game does not write at power on)


def run(cwd):
    os.makedirs(cwd, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NHL94_QUIT_AFTER='12')
    r = subprocess.run([os.path.join(PROG, 'nhl94' + EXE), '--rom', ROM], cwd=cwd, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
    if r.returncode:
        sys.exit('the window failed (%d):\n%s' % (r.returncode, r.stderr[-2000:]))
    return r.stderr


def valid(b):
    s = sum(b[:0x1FFE]) & 0xFF
    return len(b) == 0x2000 and b[0x1FFF] == s and b[0x1FFE] == (~s & 0xFF)


def marked(b):
    b = bytearray(b)
    b[AT] = MARK
    s = sum(b[:0x1FFE]) & 0xFF
    b[0x1FFF], b[0x1FFE] = s, ~s & 0xFF
    return bytes(b)


def main():
    for p in (NHL94, ROM):
        if not os.path.exists(p):
            sys.exit('missing %s' % p)
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(PROG)
    shutil.copy2(NHL94, PROG)
    fails = 0

    def check(ok, what):
        nonlocal fails
        print('  %s %s' % ('ok  ' if ok else 'FAIL', what))
        fails += not ok

    run(os.path.join(OUT, 'here1'))
    b = open(SRM, 'rb').read() if os.path.exists(SRM) else b''
    check(valid(b), 'the first run: a new cartridge\'s save RAM beside the program (%s)' % os.path.relpath(SRM, ROOT))
    check(not os.path.exists(os.path.join(OUT, 'here1', 'out', 'nhl94.srm')), 'and none in the working folder')
    if valid(b):
        open(SRM, 'wb').write(marked(b))
        run(os.path.join(OUT, 'here2'))
        b = open(SRM, 'rb').read()
        check(valid(b) and b[AT] == MARK, 'a second run from another folder kept it (byte $%04X: $%02X)' % (AT, b[AT]))
        os.makedirs(os.path.join(OUT, 'here3', 'out'))
        shutil.move(SRM, os.path.join(OUT, 'here3', 'out', 'nhl94.srm'))
        run(os.path.join(OUT, 'here3'))
        b = open(SRM, 'rb').read() if os.path.exists(SRM) else b''
        check(valid(b) and b[AT] == MARK, 'the working folder\'s save RAM (an earlier build\'s) taken up beside the program')
    print('sramtest: %s' % ('ok' if not fails else '%d failed' % fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
