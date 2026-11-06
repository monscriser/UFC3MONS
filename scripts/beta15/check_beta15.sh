# BETA 1.5: confere o pacote contra a BETA 1.4 (só exes com nome novo + DLLs de vídeo devem mudar).
set -u
E="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho"; O="$E/BETA-1.5"; T=/root/chk15
rm -rf $T; mkdir -p $T/w14 $T/w15 $T/l14 $T/l15
cd $T
python3 -c "import zipfile,sys;zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$E/BETA-1.4/UFC3MONS-BETA1.4-Windows.zip" w14
python3 -c "import zipfile,sys;z=zipfile.ZipFile(sys.argv[1]);bad=[n for n in z.namelist() if chr(92) in n];print('zip backslashes:',len(bad));z.extractall(sys.argv[2])" "$O/UFC3MONS-BETA1.5-Windows.zip" w15
echo "raiz zip 1.5: $(ls w15 | tr '\n' ' ')"
echo "--- Windows: arquivos diferentes da 1.4"; diff -rq w14 w15
(cd l14 && "$E/BETA-1.4/UFC3MONS-BETA1.4-Linux-x86_64.AppImage" --appimage-extract >/dev/null)
(cd l15 && "$O/UFC3MONS-BETA1.5-Linux-x86_64.AppImage" --appimage-extract >/dev/null)
echo "--- Linux: arquivos diferentes da 1.4"; diff -rq l14/squashfs-root l15/squashfs-root
echo "--- DLL/.so de vídeo = as testadas"
sha256sum w15/data/rexgpu-xenos.dll "$E/b11-pipeline-memo-05-11/out-win/rexgpu-xenos.dll" l15/squashfs-root/usr/bin/data/librexgpu-xenos.so "$E/beta15-05-11/out-linux-fix/librexgpu-xenos.so" | cut -c1-16
echo "--- nome BETA1.5 nos executáveis"
for f in w15/UFC3.exe w15/Launcher-Config.exe w15/data/ufc3-launcher-l1.exe w15/data/ufc3-original-v48.exe l15/squashfs-root/usr/bin/UFC3 l15/squashfs-root/usr/bin/data/ufc3-launcher-l1 l15/squashfs-root/usr/bin/data/ufc3-original-v48; do echo "$f 1.5=$(grep -c UFC3MONS-BETA1.5-2026-11-05 $f) 1.4=$(grep -c UFC3MONS-BETA1.4-2026-11-05 $f)"; done
echo "--- dados do jogo no pacote? $(find w15 l15 -iname '*.xex' -o -iname '*.iso' -o -iname '*.pac' | wc -l)"
echo "--- cvars novas na DLL/so: $(grep -c ufc_rt_update_skip w15/data/rexgpu-xenos.dll) $(grep -c ufc_pipeline_desc_memo l15/squashfs-root/usr/bin/data/librexgpu-xenos.so)"
