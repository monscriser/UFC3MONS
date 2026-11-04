# Runtime with A3 (xma_context + kernel xma), A5 (content_manager), A15 (xthread, Windows only), A11 (Linux threading). Windows + Linux.
set -e
A="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/a3-xma-loop"; C="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/a5-longpath/fix/src/system/xam/content_manager.cpp"
cd "$A"; mkdir -p fix-obj all-win all-linux
KW="src/system/CMakeFiles/rexruntime.dir/__/kernel/xboxkrnl/xboxkrnl_audio_xma.cpp.obj"; KL="${KW%.obj}.o"
CW="src/system/CMakeFiles/rexruntime.dir/xam/content_manager.cpp.obj"; CL="${CW%.obj}.o"
TW="src/system/CMakeFiles/rexruntime.dir/xthread.cpp.obj"; TL="${TW%.obj}.o"; TS="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/a15-resume/fix/xthread.cpp"
XW="src/audio/CMakeFiles/rexaudio.dir/xma_context.cpp.obj"; XL="${XW%.obj}.o"
python3 compile_xma_win.py "$A/fix/xma_context.cpp" "$A/fix-obj/xma_context.cpp.obj" "$XW"
python3 compile_xma_win.py "$A/fix/xboxkrnl_audio_xma.cpp" "$A/fix-obj/xboxkrnl_audio_xma.cpp.obj" "$KW"
python3 compile_xma_win.py "$C" "$A/fix-obj/content_manager.cpp.obj" "$CW"
python3 compile_xma_win.py "$TS" "$A/fix-obj/xthread.cpp.obj" "$TW"
python3 link_runtime_win.py --fix --xobj "$A/fix-obj/xma_context.cpp.obj" --kobj "$A/fix-obj/xboxkrnl_audio_xma.cpp.obj" --rep "$CW=$A/fix-obj/content_manager.cpp.obj" --rep "$TW=$A/fix-obj/xthread.cpp.obj"
cp out-win/win-fix/librexruntime.dll all-win/
python3 compile_linux.py "$A/fix/xma_context.cpp" "$A/fix-obj/xma_context.cpp.o" "$XL"
python3 compile_linux.py "$A/fix/xboxkrnl_audio_xma.cpp" "$A/fix-obj/xboxkrnl_audio_xma.cpp.o" "$KL"
python3 compile_linux.py "$C" "$A/fix-obj/content_manager.cpp.o" "$CL"
python3 compile_linux.py "$TS" "$A/fix-obj/xthread.cpp.o" "$TL"
python3 -c "import json,subprocess,sys; c=json.load(open('/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/a11-priority/compile.command.json')); sys.exit(subprocess.run(c).returncode)"
TP=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/files-mentioned-by-the-user-texto-2/work/runtime-allocinv-linux-build/threading_posix.cpp.o
python3 link_runtime_linux.py --fix --rep "$TP=/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/a11-priority/threading_posix.cpp.o" --xobj "$A/fix-obj/xma_context.cpp.o" --kobj "$A/fix-obj/xboxkrnl_audio_xma.cpp.o" --rep "$CL=$A/fix-obj/content_manager.cpp.o" --rep "$TL=$A/fix-obj/xthread.cpp.o"
cp out-linux/linux-fix/librexruntime.so all-linux/
sha256sum all-win/librexruntime.dll all-linux/librexruntime.so
