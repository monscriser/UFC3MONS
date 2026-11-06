# BETA 1.5 Linux smoke through AppRun -> UFC3 (the AppImage contents), llvmpipe, both editions.
set -u
E="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho"; O="$E/BETA-1.5"; L=/root/chk15/l15/squashfs-root
C="/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/dlc-test/casos"
for ed in orig repro2x; do
  G="$C/$ed/game"; T="/root/smoke15-$ed"
  rm -rf "$T" "$G/.ufc3mons-p11-camisas/game.lock"; mkdir -p "$T/settings" "$T/fb"
  Xvfb :83 -screen 0 1280x800x24 -fbdir "$T/fb" > "$T/xvfb.log" 2>&1 & XP=$!
  sleep 2
  DISPLAY=:83 UFC3_SETTINGS_ROOT="$T/settings" UFC_FORCE_SESSION_LOGS=1 UFC_QA_MEASURE=1 UFC_PROCMAP_VERIFY=1 "$L/AppRun" --play --game "$G" > "$T/ufc3.out" 2>&1 & UP=$!
  sleep 150
  ALIVE=$(pgrep -f 'squashfs-root/usr/bin/data/ufc3-' >/dev/null && echo yes || echo no)
  python3 "$E/BETA-1.3/fb2png.py" "$T/fb/Xvfb_screen0" "$O/smoke-linux-$ed.png" >/dev/null
  for p in $(pgrep -f 'squashfs-root/usr/bin/data/ufc3-'); do kill -INT $p; done; sleep 5
  for p in $(pgrep -f 'squashfs-root/usr/bin/data/ufc3-'); do kill $p; done
  kill $UP $XP 2>/dev/null; wait 2>/dev/null
  D=$(ls -dt "$G/.ufc3mons-p11-camisas/logs/linux-"* | head -1)
  mkdir -p "$O/provas/smoke-linux-$ed"; cp "$D/launch.txt" "$D/ufc3.log" "$D/stderr.txt" "$O/provas/smoke-linux-$ed/" 2>/dev/null
  echo "== $ed alive_after_150s=$ALIVE $(grep -E '^build_id=|^edition=' "$D/launch.txt" | tr '\n' ' ')"
  grep -E "librexgpu-xenos.so=|librexruntime.so=" "$D/launch.txt" | cut -c1-40
  echo "   log lines=$(wc -l < "$D/ufc3.log") crash=$(grep -ci 'ufc-crash\|segmentation fault' "$D/ufc3.log") procmap-verify=$(grep -c 'ufc-procmap-verify' "$D/stderr.txt" 2>/dev/null)"
done
