# BETA 1.0 / 1.1 SDK changes

Replacement files for the ReXGlue SDK used by the BETA builds, plus shaders and scripts in `scripts/beta`.
`build.py` injects further code into `main.cpp` when building. `app/variants` holds the older R9 variants.

Not published, because they contain game code or bytes from game files: recompiled code, `ufc_offline_memory.h`,
`ufc_gym_patch.h`, `ufc_button_art_data_p6.h`, `ufc_shirt_alias_data.h` and the shader seed lists.
The scripts use paths from the author's PC, so the tree does not build as it is.
