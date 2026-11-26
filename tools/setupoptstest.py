#!/usr/bin/env python3
"""The setup screen's options kept between runs (native/menuopts.c): make setupoptstest.

  1. A headless run with --setup-options FILE changes options on the setup screen (Team 1, Period Length,
     User Records, Penalties: tools/oracle/scripts' pad script format) and stops: FILE holds them.
  2. A new run with the same FILE, no presses: the setup screen's options ($FFD048-$FFD058, --watch) are
     those, from power on.
  3. A run without --setup-options (the tests', the golden runs'): the cartridge's defaults.
  4. A netplay session (--host / --join over the loopback, both given FILE): the cartridge's defaults; the
     file is never read or written in a session.
  5. The window (SDL's dummy drivers, NHL94_QUIT_AFTER), the program copied to out/setupoptstest/prog and
     started from another folder: it takes them from out/setup.ini beside the program.
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = '.exe' if os.name == 'nt' else ''
NHL94 = os.path.join(ROOT, 'build', 'nhl94' + EXE)
ROM = os.path.join(ROOT, 'NHL 94 (UE) [!].gen')
OUT = os.path.join(ROOT, 'out', 'setupoptstest')
FILE = os.path.join(OUT, 'setup.ini')
WATCH = 'FFD048,FFD04A,FFD04C,FFD04E,FFD050,FFD052,FFD054,FFD056,FFD058'
KEYS = ['play_mode', 'players', 'team1', 'team2', 'period_length', 'goalies', 'user_records', 'penalties', 'line_changes']
DEFAULTS = [0, 1, 11, 10, 1, 0, 1, 0, 1]       # (the cartridge's at the setup screen: defmenuoptions, User Records off)
CHANGED = [0, 1, 12, 10, 2, 0, 0, 1, 1]        # (what PRESSES leaves)
# Start at the title, then on the setup screen: down to Period Length, right; down to User Records, right;
# down to Penalties, right; up to Team 1, right
PRESSES = ('1200:START,1206:,1720:DOWN,1726:,1740:DOWN,1746:,1760:DOWN,1766:,1780:DOWN,1786:,1800:RIGHT,1806:,'
           '1820:DOWN,1826:,1840:DOWN,1846:,1860:RIGHT,1866:,1880:DOWN,1886:,1900:RIGHT,1906:,1920:UP,1926:,'
           '1940:UP,1946:,1960:UP,1966:,1980:UP,1986:,2000:UP,2006:,2020:RIGHT,2026:\n')
AT_SETUP = 1000                                 # (the setup screen is up from frame 836)


def options(stderr, upto=AT_SETUP):
    """the watched options as they stand at frame upto"""
    v = [0] * 9
    for f, a, x in re.findall(r'frame (\d+): \$FFD0([0-9A-F]{2}) = \$([0-9A-F]{4})', stderr):
        if int(f) <= upto:
            v[(int(a, 16) - 0x48) // 2] = int(x, 16)
    return v


def headless(*args, frames=AT_SETUP):
    r = subprocess.run([NHL94, '--rom', ROM, '--headless', '--frames', str(frames), '--watch', WATCH] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=300)
    if r.returncode:
        sys.exit('nhl94 failed (%d):\n%s' % (r.returncode, r.stderr[-2000:]))
    return r.stdout + r.stderr                  # (--watch on stdout, 'setup options:' on stderr)


def held():
    try:
        d = dict(l.strip().split('=', 1) for l in open(FILE) if '=' in l and not l.startswith('#'))
        return [int(d[k]) for k in KEYS]
    except (OSError, KeyError, ValueError):
        return None


def main():
    for p in (NHL94, ROM):
        if not os.path.exists(p):
            sys.exit('missing %s' % p)
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    fails = 0

    def check(ok, what):
        nonlocal fails
        print('  %s %s' % ('ok  ' if ok else 'FAIL', what))
        fails += not ok

    script = os.path.join(OUT, 'presses.txt')
    open(script, 'w').write(PRESSES)
    err = headless('--setup-options', FILE, '--input', script, frames=2200)
    check(options(err, 2200) == CHANGED, 'the presses changed the options: %s' % options(err, 2200))
    check(held() == CHANGED, 'and the file holds them: %s' % held())

    err = headless('--setup-options', FILE)
    check(options(err) == CHANGED, 'a new run with the file: the setup screen has them: %s' % options(err))
    check(held() == CHANGED, 'and the file is as it was')

    err = headless()
    check(options(err) == DEFAULTS, 'a run without --setup-options: the cartridge\'s: %s' % options(err))

    host = subprocess.Popen([NHL94, '--rom', ROM, '--headless', '--host', '41997', '--frames', str(AT_SETUP),
                             '--watch', WATCH, '--setup-options', FILE],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    guest = headless('--join', '127.0.0.1:41997', '--setup-options', FILE)
    herr = host.communicate(timeout=300)[0]
    check(options(herr) == DEFAULTS and options(guest) == DEFAULTS,
          'a netplay session: the cartridge\'s, host %s, guest %s' % (options(herr), options(guest)))
    check('setup options:' not in herr + guest and held() == CHANGED, 'and the file neither read nor written')

    prog = os.path.join(OUT, 'prog')
    os.makedirs(os.path.join(prog, 'out'))
    shutil.copy2(NHL94, prog)
    shutil.copy2(FILE, os.path.join(prog, 'out', 'setup.ini'))
    cwd = os.path.join(OUT, 'elsewhere')
    os.makedirs(cwd)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NHL94_QUIT_AFTER='20')
    r = subprocess.run([os.path.join(prog, 'nhl94' + EXE), '--rom', ROM], cwd=cwd, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
    beside = os.path.join(prog, 'out', 'setup.ini')
    got = [l.split(': ', 1)[1] for l in r.stderr.splitlines() if l.startswith('setup options: ')]
    check(r.returncode == 0 and got and os.path.samefile(got[0], beside),
          'the window, started from another folder, put them back from out/setup.ini beside the program')
    check(not os.path.exists(os.path.join(cwd, 'out', 'setup.ini')), 'and kept none in the working folder')
    print('setupoptstest: %s' % ('ok' if not fails else '%d failed' % fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
