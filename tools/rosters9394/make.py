#!/usr/bin/env python3
"""The 1993-94 roster sets (native/rosters9394.inc) from the research in this folder (docs/ROSTERS-1993-94.md).

  python3 -I tools/rosters9394/make.py [--cart NHL94.bin] [--rom95 NHL95.bin]

research/CODE.csv: each team's 1993-94 roster (Hockey-Reference: number, name, position, hand, size, regular
season record) and how many of its first 3 games (opening) and its first 3 after the March 21, 1994 trade
deadline (deadline) each man dressed for. research/ALLSTAR.csv: the January 22, 1994 All-Star Game's.

With --cart (the player's own NHL 94 image: read for the names only, nothing of it is written), each
player's spelling on the cartridge is found and kept in players.csv's cart column; with --rom95 the formula
for new players' ratings is fitted again (fit.json). Without them the columns and fit already here are used.
Writes players.csv (who plays where, in each set) and native/rosters9394.inc.
"""
import csv, json, os, struct, sys, unicodedata, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TEAMS = "ANH BOS BUF CGY CHI DAL DET EDM FLA HFD LA MTL NJ NYI NYR OTW PHI PIT QUE SJ STL TB TOR VAN WSH WPG ASE ASW".split()
HR = dict(zip(TEAMS[:26], "MDA BOS BUF CGY CHI DAL DET EDM FLA HAR LAK MTL NJD NYI NYR OTT PHI PIT QUE SJS STL TBL TOR VAN WSH WIN".split()))
ROSTER, MAXG, MAXF, MAXD = 26, 3, 15, 10
REGULAR_GP = 40          # a man this team played this often who missed the 3 games (hurt, scratched) is still on it

def ascii_name(s):
    s = unicodedata.normalize("NFKD", s.replace("ł", "l").replace("Ł", "L").replace("ø", "o").replace("Ø", "O"))
    return "".join(c for c in s if ord(c) < 128 and not unicodedata.combining(c)).strip()

def key(s): return "".join(c for c in ascii_name(s).lower() if c.isalnum())

def num(x, d=0.0):
    try: return float(x)
    except (TypeError, ValueError): return d

def inches(h):
    try: f, i = h.split("-"); return int(f) * 12 + int(i)
    except Exception: return 72

# ---- the cartridges, read as data ----------------------------------------------------------------------
def be16(b, a): return struct.unpack(">H", b[a:a + 2])[0]
def be32(b, a): return struct.unpack(">I", b[a:a + 4])[0]

def rom_players(path):
    """{team abbr: [(name, nib14), ...]} of a 94 / 95 Genesis image (its team table found as rom95g.c does)"""
    b = open(path, "rb").read()
    for a in range(0x200, 0x2000, 2):
        ps = [be32(b, a + 4 * t) for t in range(28)]
        if all(p and not p & 1 and p + 16 < len(b) and all(8 <= be16(b, p + 2 * k) < 0x800 for k in range(6)) for p in ps): break
    else: sys.exit(f"{path}: no team table")
    out = {}
    for t, p in enumerate(ps):
        q, pl = p + be16(b, p), []
        while be16(b, q) > 2:
            n = be16(b, q)
            name = b[q + 2:q + n].split(b"\0")[0].decode("latin1")
            nib = [(b[q + n + 1 + k // 2] >> (0 if k & 1 else 4)) & 15 for k in range(14)]
            pl.append((name, nib)); q += n + 8
        out[TEAMS[t]] = pl
    return out

# ---- who plays ------------------------------------------------------------------------------------------
def research():
    teams = {}
    for t in TEAMS[:26]:
        path = os.path.join(HERE, "research", HR[t] + ".csv")
        if not os.path.exists(path): print(f"{t}: no research yet ({path})"); continue
        with open(path, newline="", encoding="utf-8") as f: teams[t] = [r for r in csv.DictReader(f) if r.get("name")]
    return teams

def elsewhere(teams, t, name, col):
    """dressed for another team in the set's games: he belonged to it then"""
    return any(o != t and any(key(r["name"]) == key(name) and num(r[col]) > 0 for r in rows) for o, rows in teams.items())

def pick(teams, t, col):
    rows = teams[t]
    on = [r for r in rows if num(r[col]) > 0]
    star = lambda r: num(r["gp"]) >= REGULAR_GP or (num(r["gp"]) >= 15 and (num(r["g"]) + num(r["a"])) / num(r["gp"]) >= 0.8)   # (a regular, or a star who was hurt)
    hurt = [r for r in rows if num(r[col]) == 0 and star(r) and not elsewhere(teams, t, r["name"], col)]
    hurt.sort(key=lambda r: -num(r["gp"]))
    g = sorted([r for r in on + hurt if r["pos"] == "G"], key=lambda r: (-num(r[col]), -num(r["gp"])))[:MAXG]
    d = sorted([r for r in on + hurt if r["pos"] == "D"], key=lambda r: (-num(r[col]), -num(r["gp"])))[:MAXD]
    f = sorted([r for r in on + hurt if r["pos"] not in ("G", "D")], key=lambda r: (-num(r[col]), -num(r["gp"])))[:MAXF]
    for grp, want, test in ((g, 2, lambda r: r["pos"] == "G"), (d, 6, lambda r: r["pos"] == "D"), (f, 9, lambda r: r["pos"] not in ("G", "D"))):
        more = sorted([r for r in rows if test(r) and r not in grp and not elsewhere(teams, t, r["name"], col)], key=lambda r: -num(r["gp"]))
        while len(grp) < want and more: grp.append(more.pop(0))   # (the game needs 2 goalies, 9 forwards, 6 defensemen)
    while len(g) + len(d) + len(f) > ROSTER:                      # (the least played of the biggest group out)
        big = max((f, d), key=len); big.pop()
    return g, f, d

# ---- ratings for a man the cartridge does not have ------------------------------------------------------
LAB = ["weight", "agility", "speed", "off_awareness", "def_awareness", "shot_power", "checking", "handed_fighting",
       "stick_handling", "shot_accuracy", "endurance", "shoot_or_pass", "pass_accuracy", "aggressiveness"]

def feats(r):
    gp = max(num(r["gp"]), 1)
    if r["pos"] == "G": return [1, num(r["gaa"], 3.5), num(r["svpct"], .880) * 100, min(gp, 60) / 60]
    return [1, num(r["g"]) / gp, num(r["a"]) / gp, num(r["pim"]) / gp, num(r["plusminus"]) / gp, min(gp, 84) / 84,
            (inches(r["height"]) - 72) / 3, (num(r["weight"], 195) - 195) / 15]

def lstsq(X, y):
    n = len(X[0]); A = [[sum(x[i] * x[j] for x in X) + (1e-3 if i == j else 0) for j in range(n)] for i in range(n)]
    v = [sum(x[i] * yy for x, yy in zip(X, y)) for i in range(n)]
    for c in range(n):                                           # (Gauss-Jordan)
        p = max(range(c, n), key=lambda r: abs(A[r][c])); A[c], A[p] = A[p], A[c]; v[c], v[p] = v[p], v[c]
        for r in range(n):
            if r != c and A[c][c]:
                m = A[r][c] / A[c][c]; A[r] = [a - m * b for a, b in zip(A[r], A[c])]; v[r] -= m * v[c]
    return [v[i] / A[i][i] if A[i][i] else 0 for i in range(n)]

def fit(teams, target, groups):
    """per group (G, F, D) and rating: least squares from feats() to target (name key -> nib14)"""
    model = {}
    for gname in groups:
        rows = [r for rows in teams.values() for r in rows if group(r) == gname and key(r["name"]) in target]
        X = [feats(r) for r in rows]
        model[gname] = {"n": len(rows), "coef": [lstsq(X, [target[key(r["name"])][k] for r in rows]) if len(rows) > 12 else None for k in range(14)]}
    return model

def group(r): return "G" if r["pos"] == "G" else "D" if r["pos"] == "D" else "F"

def rate(r, model, fightmodel):
    g, x = group(r), feats(r)
    nib = []
    for k in range(14):
        c = model[g]["coef"][k]
        nib.append(max(0, min(15, round(sum(a * b for a, b in zip(c, x))))) if c else 3)
    if g != "G":                                                 # bit 0 his hand (1 right), bits 1-3 fighting
        c = fightmodel[g]["coef"][LAB.index("handed_fighting")]
        fight = max(0, min(7, round(sum(a * b for a, b in zip(c, x)) / 2))) if c else 2
        nib[7] = fight << 1 | (1 if r["shoots"] == "R" else 0)
    else:
        nib[7] = 1 if r["shoots"] == "R" else 0                  # (a goalie's: his glove hand)
    return nib

# ---- lines ------------------------------------------------------------------------------------------------
def lines_of(g, f, d):
    """the 8 line sets (G LD RD LW C RW X: roster slots from 1), from points a game"""
    slot = {id(r): i + 1 for i, r in enumerate(g + f + d)}
    val = lambda r: (num(r["g"]) + num(r["a"])) / max(num(r["gp"]), 1) + min(num(r["gp"]), 84) / 400
    fs = sorted(f, key=val, reverse=True); ds = sorted(d, key=val, reverse=True)
    def trio(n):
        out, used = [], set()
        for want in ("LW", "C", "RW"):
            c = [r for r in fs if r["pos"] == want and id(r) not in taken and id(r) not in used]
            r = c[0] if c else next(r for r in fs if id(r) not in taken and id(r) not in used)
            used.add(id(r)); out.append(r)
        taken.update(used); return out
    taken = set(); t1 = trio(1); t2 = trio(2); t3 = trio(3)
    t4 = trio(4) if len(fs) >= 12 else t3
    left = [r for r in fs if id(r) not in taken] or [t3[1]]
    D = (ds * 3)[:6]
    s = lambda r: slot[id(r)]
    L = lambda dd, tr, x: [1, s(dd[0]), s(dd[1]), s(tr[0]), s(tr[1]), s(tr[2]), s(x)]
    return [L(D[0:2], t1, t2[1]), L(D[2:4], t2, t1[1]), L(D[4:6], t3, t1[0]),          # SC1 SC2 CHK
            L([D[0], D[2]], t1, t2[0]), L([D[1], D[3]], t2, t1[2]),                     # PP1 PP2
            L(D[0:2], [t3[0], t2[1], t3[2]], t1[1]), L(D[2:4], [t2[0], t3[1], t1[2]], t2[1]),   # PK1 PK2
            L(D[4:6], t4, left[0])]                                                     # L8

def starts(g):
    top = max(num(r["gp"]) for r in g) or 1
    return [max(1, min(15, round(15 * num(r["gp"]) / top))) for r in g]

# ---- the All-Star teams ---------------------------------------------------------------------------------
def allstars(teams):
    path = os.path.join(HERE, "research", "ALLSTAR.csv")
    if not os.path.exists(path): return {}
    out = {"ASE": [], "ASW": []}
    with open(path, newline="", encoding="utf-8") as f:
        for a in csv.DictReader(f):
            conf = "ASE" if a["conference"].lower().startswith("e") else "ASW"
            club = next((t for t, c in HR.items() if c == a["team"]), None)   # (his club's row: his number, his record there)
            r = next((r for r in teams.get(club, []) if key(r["name"]) == key(a["name"])), None) or \
                next((r for rows in teams.values() for r in rows if key(r["name"]) == key(a["name"])), None)
            if r: out[conf].append(dict(r, opening="1", deadline="1"))
            else: print(f"All-Star {a['name']}: not in the research")
    return out

import difflib
def split_key(n):
    w = ascii_name(n).split()
    return key(w[0]) if w else "", key(" ".join(w[1:]))

NICK = [{"dave", "david"}, {"steve", "stephen", "steven"}, {"matt", "mathieu", "matthew"}, {"rich", "richard"}, {"frank", "frantisek"},
        {"alex", "alexei", "alexander", "alexandre", "alexnder"}, {"al", "allan", "alan"}, {"mike", "michael", "mikael"}, {"jon", "john"},
        {"ilja", "ilya"}, {"dmitri", "dmitry", "dimitri"}, {"vyacheslav", "viacheslav", "vachslav"}]

def same_first(a, b):
    if a == b or a.startswith(b) or b.startswith(a): return True       # (Ed / Eddie, Bob / Bobby, Theo / Theoren)
    if any(a in s and b in s for s in NICK): return True
    return difflib.SequenceMatcher(None, a, b).ratio() >= 0.8           # (the cartridges' "Christan", "Frantsek")

def matcher(names, taken=()):
    """a name's spelling among names (a cartridge's): the same letters, else the same first initial and a last name
    within a typo or a truncation of it (the cartridges' "Alexnder", "Vanbiesbrk", "Frank" Musil), the only one so"""
    exact = {key(n): n for n in names}
    parts = [(n,) + split_key(n) for n in set(names)]
    def find(name):
        k = key(name)
        if k in exact: return exact[k], False
        f, l = split_key(name)
        hits = [n for n, f2, l2 in parts if key(n) not in taken and same_first(f, f2) and difflib.SequenceMatcher(None, l, l2).ratio() >= 0.8]
        return (hits[0], True) if len(set(hits)) == 1 else (None, False)
    return find

def shown(name, n95):
    """his name on the roster screen: the cartridge's room is 19 letters (NHL 95's spelling, else his first initial, when longer)"""
    n = ascii_name(name)
    if len(n) <= 19: return n
    if n95 and len(n95) <= 19: return n95
    w = n.split()
    return (w[0][0] + ". " + " ".join(w[1:]))[:19]

def cstr(s): return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--cart"); ap.add_argument("--rom95"); a = ap.parse_args()
    teams = research()
    cart = rom_players(a.cart) if a.cart else None
    real = {key(r["name"]) for rows in teams.values() for r in rows}   # (a cartridge's name that is another real player's is his)
    cartfind = matcher([n for pl in (cart or {}).values() for n, _ in pl], real) if cart else None
    find95 = matcher([n for pl in rom_players(a.rom95).values() for n, _ in pl], real) if a.rom95 else None
    fuzzy = set()
    fitpath = os.path.join(HERE, "fit.json")
    if a.rom95 and cart:
        r95 = {key(n): nib for t, pl in rom_players(a.rom95).items() if t not in ("ASE", "ASW") for n, nib in pl}
        r94 = {key(n): nib for t, pl in cart.items() if t not in ("ASE", "ASW") for n, nib in pl}
        model = {"from95": fit(teams, r95, "GFD"), "from94": fit(teams, r94, "GFD")}
        json.dump(model, open(fitpath, "w"), indent=1)
    model = json.load(open(fitpath))
    prev = {}
    pc = os.path.join(HERE, "players.csv")
    if os.path.exists(pc):
        with open(pc, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f): prev[key(r["name"])] = r
    sets = {}
    asg = allstars(teams)
    for si, col in enumerate(("opening", "deadline")):
        for t in TEAMS:
            if t in asg:
                rows = asg[t]
                g = [r for r in rows if r["pos"] == "G"][:MAXG]; d = [r for r in rows if r["pos"] == "D"][:MAXD]
                f = [r for r in rows if r["pos"] not in ("G", "D")][:ROSTER - len(g) - len(d)]
            elif t in teams: g, f, d = pick(teams, t, col)
            else: continue
            sets[(si, t)] = (g, f, d)
    out = []
    for (si, t), (g, f, d) in sorted(sets.items()):
        for r, st in zip(g + f + d, starts(g) + [0] * (len(f) + len(d))):
            if cart:
                c, fz = cartfind(r["name"])
                if fz and c not in fuzzy: fuzzy.add(c); print(f"  {r['name']} is the cartridge's {c}")
            else: c = prev.get(key(r["name"]), {}).get("cart", "")
            if find95:
                n95, fz = find95(r["name"])
                if fz and ("95", n95) not in fuzzy: fuzzy.add(("95", n95)); print(f"  {r['name']} is NHL 95's {n95}")
            else: n95 = prev.get(key(r["name"]), {}).get("n95", "")
            out.append({"set": ("opening", "deadline")[si], "team": t, "number": r["number"], "name": ascii_name(r["name"]),
                        "pos": r["pos"], "shoots": r["shoots"], "start": st, "cart": c or "", "n95": n95 or "",
                        "nib": " ".join(map(str, rate(r, model["from95"], model["from94"]))) if not c else ""})
    with open(pc, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(out[0].keys())); w.writeheader(); w.writerows(out)
    # the .inc
    L = ["/* made by tools/rosters9394/make.py from tools/rosters9394/research: do not edit (docs/ROSTERS-1993-94.md) */"]
    names = {}
    for (si, t), (g, f, d) in sorted(sets.items()):
        nm = f"s{si}_{t}"; names[(si, t)] = nm
        L.append(f"static const struct season_player {nm}[] = {{")
        for r, st in zip(g + f + d, starts(g) + [0] * (len(f) + len(d))):
            o = next(x for x in out if x["set"] == ("opening", "deadline")[si] and x["team"] == t and x["name"] == ascii_name(r["name"]))
            nib = rate(r, model["from95"], model["from94"])
            pos = "G" if r["pos"] == "G" else "D" if r["pos"] == "D" else "F"
            L.append(f"    {{ {cstr(shown(r['name'], o['n95']))}, {cstr(o['cart']) if o['cart'] else 'NULL'}, {cstr(o['n95']) if o['n95'] else 'NULL'}, {int(num(r['number']))}, '{pos}', {st}, {{ {', '.join(map(str, nib))} }} }},")
        L.append("};")
    L.append("static const struct season_team season9394[2][28] = {")
    for si in range(2):
        L.append("  {")
        for t in TEAMS:
            if (si, t) not in sets: L.append(f"    {{ 0 }},  /* {t}: not researched yet */"); continue
            g, f, d = sets[(si, t)]
            ls = lines_of(g, f, d)
            L.append(f"    {{ {len(g) + len(f) + len(d)}, {len(g)}, {len(f)}, {{ {', '.join('{ ' + ', '.join(map(str, l)) + ' }' for l in ls)} }}, {names[(si, t)]} }},  /* {t} */")
        L.append("  },")
    L.append("};")
    open(os.path.join(ROOT, "native", "rosters9394.inc"), "w").write("\n".join(L) + "\n")
    print(f"{len(sets)} team rosters, {sum(1 for o in out if not o['cart'])} rows of players the cartridge does not have")

main()
