R=/usr/lib/llvm-19/bin; W=/mnt/c/Users/Monscriser/Documents/Codex/2026-11-14/new-chat-3
for f in "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-fix-switch/linux/fix.o" "$W/work/beta18/linux/fix24.o" "$W/work/ufc3mons-beta18-build-tree/build/linux-reborn/ufc3-launcher-l1" "$W/work/ufc3mons-beta18-build-tree/build/win-reborn/ufc3-launcher-l1.exe"; do
  echo "== $f"; $R/llvm-nm -S --defined-only "$f" 2>/dev/null | grep -E ' sub_82B50588$'
  d=$($R/llvm-objdump -dr --no-show-raw-insn --disassemble-symbols=sub_82B50588 "$f" 2>/dev/null)
  echo "  cmp 0x83669280: $(grep -ci '0x83669280' <<<"$d")   refs sub_83669280: $(grep -c 'sub_83669280' <<<"$d")   lines: $(wc -l <<<"$d")"
done
