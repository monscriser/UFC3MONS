H="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-fix-switch"
export UFC_SWAP_OBJ_WIN_REBORN="CMakeFiles/ufc3_recomp.dir/generated/ufc3_recomp.229.cpp.obj=$H/win/fix.obj"
export UFC_SWAP_OBJ_LINUX_REBORN="/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/de/work/linux-update/pre/obj-reborn/ufc3_recomp.229.o=$H/linux/fix.o"
tr -d '\r' < "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-05-11/build_beta15.sh" > /root/b15.sh && bash /root/b15.sh
echo "exit=$?"
grep -h "swap" "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-05-11/beta15-build.log"
