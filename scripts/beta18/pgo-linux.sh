# BETA 1.8 PGO Linux: same training profile as Windows (llvm-mingw clang 23), converted to the llvm 19 format;
# the IR function hashes match the Linux objects (no out-of-date warning on the test chunk).
set -e
WS=/mnt/c/Users/Monscriser/Documents/Codex/2026-11-14/new-chat-3
export UFC3_BUILD_TREE=$WS/work/ufc3mons-beta18-build-tree UFC3_CODEX_ARCHIVE='/mnt/e/PROJETOS ANTIGOS/Codex-22-10'
export UFC3_WINDOWS_CXX=/root/ht/llvm-mingw/bin/x86_64-w64-mingw32-clang++
H=$WS/work/UFC3MONS-1.0.12-UFC3MONS/scripts/beta18
P=/mnt/e/ufc3mons-pgo/original-llvm19.profdata; R=/mnt/e/ufc3mons-pgo/use
common="-Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin"
PGO_JOBS=${PGO_JOBS:-12} bash $WS/work/beta18/pgo-ns.sh python3 $H/pgo_recomp.py linux-reborn use $R/linux-reborn $P
PGO_JOBS=${PGO_JOBS:-12} bash $WS/work/beta18/pgo-ns.sh python3 $H/pgo_recomp.py linux-original use $R/linux-original $P
UFC_OUT_SUFFIX=-pgo UFC_EXTRA_CXXFLAGS="-fprofile-use=$P $common" UFC_RECOMP_OBJS_LINUX_REBORN=$R/linux-reborn UFC3_REBORN_SWITCH_FIX=$R/linux-reborn python3 -u $H/build.py linux-reborn
UFC_OUT_SUFFIX=-pgo UFC_EXTRA_CXXFLAGS="-fprofile-use=$P $common" UFC_RECOMP_OBJS_LINUX_ORIGINAL=$R/linux-original python3 -u $H/build.py linux-original
