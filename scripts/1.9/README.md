# UFC3MONS 1.9 scripts

Records of the local environment used for 1.9 (paths are local; they are not portable build commands).

| Script | Purpose |
| --- | --- |
| `pgo-1.9.sh` | Final PGO builds of the four game executables and the front door (same training profiles as BETA 1.8). |
| `package-1.9.sh` | Windows zip and Linux AppImage from the earlier package bases, with the 1.9 executables. |
| `rebuild-guard.sh` | Quick non-PGO test builds that reuse unchanged objects. |
| `titles/gerar_titulos.py` | Reborn category titles: draws the flyweight title for the pre-fight card and the Portuguese titles (card and scoreboard) into the DXT5 textures of the HUD packages, keeping every other block; writes `app/common/ufc_titles_data.h`. Needs your own Reborn 1.1 files and the Bahnschrift font that ships with Windows. |
| `titles/pacdds.py`, `titles/render.py` | DXT5 texture read/encode helpers and text rendering used by the titles generator. |
| `font-accents/` | Reborn menu font: draws the missing accents on the uppercase accented letters (`ufc_font_accents_data.h`). |
| `shirts-by-org/` | Reborn shirts by organization: builds the UFC and PRIDE variants of the shirt alias archive (`ufc_shirt_org_data.h`); `classes.csv` lists each shirt group's organization. |

The generated data headers only store the bytes that change; at first start the game makes the patched copies from
your own files after checking their SHA-256, and leaves the original files untouched.
