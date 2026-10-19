#!/usr/bin/env python3
"""Generates the Bulmaca puzzles (apps/bulmaca/data/puzzles.txt) from tools/bulmaca/words.tsv.

Style: an 11×11 numbered crossword with black squares and the clues in a bar (the American look), filled
"free-form": words are laid across and down so that every run of two or more letters is a clued word, every
word crosses at least one other and no letters touch side by side outside a word. Clues inside cells
(the Scandinavian "çengel" look) would need ~12 px text on this screen; numbered cells keep 70+ px letters.

Deterministic: puzzle k uses random.Random(SEED + k); words used often in earlier puzzles are avoided.
Usage: tools/bulmaca/build-puzzles.py [words.tsv] [puzzles.txt] [count]

File format (UTF-8, one puzzle after another):
  P <number> <cols> <rows>
  G <row>            cols letters, upper case Turkish (İ, I, Ç, Ğ, Ö, Ş, Ü), '#' = black square
  A|D <row> <col> <ANSWER>\t<clue>    across / down words, ordered by start cell
"""
import random, sys

SIZE = 11
SEED = 20261019
TARGET_WORDS = 30
TRIES = 8  # layouts tried per puzzle; the densest wins
MAX_USE = 8  # a word appears in at most this many puzzles


def upper(w):
    return w.replace("i", "İ").replace("ı", "I").upper()


def load(path):
    words = []
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        w, rank, clue = line.rstrip("\n").split("\t")
        if 3 <= len(w) <= SIZE:
            words.append((upper(w), int(rank), clue))
    return words


class Grid:
    def __init__(self):
        self.c = [[None] * SIZE for _ in range(SIZE)]
        self.dirs = [[""] * SIZE for _ in range(SIZE)]  # directions of the words through each cell
        self.words = []  # (dir, r, c, word)

    def get(self, r, c):
        return self.c[r][c] if 0 <= r < SIZE and 0 <= c < SIZE else None

    def fits(self, w, r, c, d):
        """number of crossings if w fits at (r, c) in direction d ('A'/'D'), else -1"""
        dr, dc = (0, 1) if d == "A" else (1, 0)
        n = len(w)
        er, ec = r + dr * (n - 1), c + dc * (n - 1)
        if not (0 <= r < SIZE and 0 <= c < SIZE and 0 <= er < SIZE and 0 <= ec < SIZE):
            return -1
        if self.get(r - dr, c - dc) or self.get(er + dr, ec + dc):
            return -1
        cross = 0
        for i, ch in enumerate(w):
            rr, cc = r + dr * i, c + dc * i
            cur = self.c[rr][cc]
            if cur:
                if cur != ch or d in self.dirs[rr][cc]:
                    return -1
                cross += 1
            elif self.get(rr + dc, cc + dr) or self.get(rr - dc, cc - dr):
                return -1  # would touch a parallel letter
        if cross == len(w):
            return -1
        return cross

    def place(self, w, r, c, d):
        dr, dc = (0, 1) if d == "A" else (1, 0)
        for i, ch in enumerate(w):
            self.c[r + dr * i][c + dc * i] = ch
            self.dirs[r + dr * i][c + dc * i] += d
        self.words.append((d, r, c, w))

    def filled(self):
        return sum(1 for row in self.c for x in row if x)


def letters(w):
    return list(w)


def build(rng, words, used):
    pool = sorted(words, key=lambda x: (used.get(x[0], 0), rng.random()))
    by_letter = {}
    for i, (w, _, _) in enumerate(pool):
        for ch in set(letters(w)):
            by_letter.setdefault(ch, []).append(i)
    g = Grid()
    first = [x for x in pool[:300] if 7 <= len(x[0]) <= SIZE]
    w0 = rng.choice(first)[0]
    g.place(w0, SIZE // 2, rng.randint(0, SIZE - len(w0)), "A")
    in_grid = {w0}
    fails = 0
    while len(g.words) < TARGET_WORDS and fails < 8:
        best = None
        # candidate positions: every letter cell, the other direction
        slots = []
        for d0, r0, c0, w in g.words:
            for i in range(len(w)):
                r, c = (r0, c0 + i) if d0 == "A" else (r0 + i, c0)
                slots.append((r, c, "D" if d0 == "A" else "A"))
        rng.shuffle(slots)
        for r, c, d in slots[:30]:
            ch = g.c[r][c]
            if g.get(r - (d == "D"), c - (d == "A")) and g.get(r + (d == "D"), c + (d == "A")):
                continue  # already inside a word in that direction
            cand = by_letter.get(ch, [])
            start = rng.randrange(len(cand)) if len(cand) > 150 else 0
            for idx in (cand[start:start + 150] + cand[:max(0, start + 150 - len(cand))]):
                w = pool[idx][0]
                if w in in_grid or used.get(w, 0) >= MAX_USE:
                    continue
                for k, x in enumerate(w):
                    if x != ch:
                        continue
                    sr, sc = (r, c - k) if d == "A" else (r - k, c)
                    cr = g.fits(w, sr, sc, d)
                    if cr <= 0:
                        continue
                    # many crossings, long words, rarely used words
                    score = cr * 10 + min(len(w), 6) - used.get(w, 0) * 3 + rng.random()
                    if not best or score > best[0]:
                        best = (score, w, sr, sc, d)
        if not best:
            fails += 1
            continue
        _, w, r, c, d = best
        g.place(w, r, c, d)
        in_grid.add(w)
    return g


def runs(g):
    """every maximal run of >= 2 letters, as (dir, r, c)"""
    out = []
    for d in "AD":
        for a in range(SIZE):
            cell = (lambda i: g.c[a][i]) if d == "A" else (lambda i: g.c[i][a])
            i = 0
            while i < SIZE:
                if not cell(i):
                    i += 1
                    continue
                s = i
                while i < SIZE and cell(i):
                    i += 1
                if i - s >= 2:
                    out.append((d, a, s) if d == "A" else (d, s, a))
    return out


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "tools/bulmaca/words.tsv"
    dst = sys.argv[2] if len(sys.argv) > 2 else "apps/bulmaca/data/puzzles.txt"
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 400
    words = load(src)
    clue = {w: c for w, _, c in words}
    used = {}
    out = []
    stats = []
    for k in range(count):
        rng = random.Random(SEED + k)
        best = None
        for _ in range(TRIES):
            g = build(rng, words, used)
            key = (g.filled(), len(g.words))
            if not best or key > best[0]:
                best = (key, g)
        g = best[1]
        placed = {(d, r, c): w for d, r, c, w in g.words}
        if sorted(placed) != sorted(runs(g)):
            sys.exit(f"puzzle {k + 1}: runs without a clue {set(placed) ^ set(runs(g))}")
        for w in placed.values():
            used[w] = used.get(w, 0) + 1
        stats.append((g.filled(), len(g.words)))
        out.append(f"P {k + 1} {SIZE} {SIZE}")
        for row in g.c:
            out.append("G " + "".join(x or "#" for x in row))
        for (d, r, c) in sorted(placed, key=lambda t: (t[1], t[2], t[0])):
            w = placed[(d, r, c)]
            out.append(f"{d} {r} {c} {w}\t{clue[w]}")
    with open(dst, "w", encoding="utf-8") as f:
        f.write("# Bulmaca puzzles, generated by tools/bulmaca/build-puzzles.py from tools/bulmaca/words.tsv\n")
        f.write("# Clues: Turkish Wiktionary (CC BY-SA 4.0) via kaikki.org; see SOURCES. This file is CC BY-SA 4.0.\n")
        f.write("\n".join(out) + "\n")
    fl = [s[0] for s in stats]
    nw = [s[1] for s in stats]
    print(f"{count} puzzles -> {dst}; letters {min(fl)}..{max(fl)} (avg {sum(fl) / len(fl):.1f}/{SIZE * SIZE}), "
          f"words {min(nw)}..{max(nw)} (avg {sum(nw) / len(nw):.1f}); {len(used)} distinct words, max use {max(used.values())}")


if __name__ == "__main__":
    main()
