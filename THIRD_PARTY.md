# Third-party components

| Component | License | Source | Used for |
|-----------|---------|--------|----------|
| FBInk (NiLuJe) | GPL-3.0-or-later | https://github.com/NiLuJe/FBInk (revision in `tools/deps.lock`) | e-ink drawing & refresh |
| Stockfish 11 | GPL-3.0 (`COPYING-stockfish` in the chess package) | https://github.com/official-stockfish/Stockfish (tag `sf_11`) | chess engine, run as a separate process |
| Dropbear SSH (DROPBEAR_2026.94) | MIT-style (`LICENSE-dropbear` in the SSH package) | https://github.com/mkj/dropbear | SSH server; patched by `tools/patches/dropbear-apppass.patch` |
| DejaVu Fonts 2.37 | Bitstream Vera / DejaVu license (free, see `DejaVu-LICENSE`) | https://dejavu-fonts.github.io | UI font (Turkish + chess glyphs) |

Because FBInk is linked statically and is GPLv3+, this project is distributed under GPLv3 (see `LICENSE`).
