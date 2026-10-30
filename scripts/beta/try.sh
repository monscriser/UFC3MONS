set -u
W="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-beta11"
H="/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/Hydro-Thunder-fontes-revisao-7"
python3 "$W/make_ufc_direct.py" "$W/patch_ufc_texture_direct.py" || exit 1
D=/root/ufc-port; rm -rf $D; mkdir -p $D
cp -a /mnt/e/UFC3-RexGlue-Lab/sources/rexglue-sdk-clean/src /mnt/e/UFC3-RexGlue-Lab/sources/rexglue-sdk-clean/include $D/
python3 "$W/patch_ufc_texture_direct.py" $D "$H/sdk-extra/ht_texture_direct" 2>&1 | tail -4
python3 "$W/make_ufc_verify.py" "$W/patch_ufc_texture_direct_verify.py" && python3 "$W/patch_ufc_texture_direct_verify.py" $D "$H/sdk-extra/ht_texture_direct" 2>&1 | tail -4
