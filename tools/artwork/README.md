# Setup artwork

Lomorsin pixel art, used by the setup program only (the game and its shortcut use
the game's own icon, read from your disc). It is not game art.

`lomorsin-zoids/`:

- `previa_lomorsin_zoids.png`: the supplied seal-zoid drawing.
- `lomorsin_zoids_512.png`, `icono_N.png`, `lomorsin_zoids.ico` (16-256): the
  drawing redrawn on its own pixel grid. 16, 24 and 32 show the head only.
- `cabecera_lomorsin_zoids_1200x240.png`: the setup window's header. The left
  side is empty so the localized page title is drawn there.

`make_lomorsin_zoids.py` made these files from the drawing. `tools/make_setup_art.py`
copies the icon to `tools/zoidssetup.ico` and encodes the header for the setup build.
