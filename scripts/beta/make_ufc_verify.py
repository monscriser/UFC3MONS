"""Generates patch_ufc_texture_direct_verify.py from the Hydro rev7 script (UFC has no ht_texture_blit cvar)."""
import sys
s = open("/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7/tools/wsl/patch_texture_direct_verify.py").read()
a = 'REXCVAR_DEFINE_BOOL(ht_texture_blit, true, "GPU",'
assert s.count(a) == 2, s.count(a)
s = s.replace(a, 'REXCVAR_DEFINE_BOOL(non_seamless_cube_map, false, "GPU",')
open(sys.argv[1], "w").write(s); print("generated")
