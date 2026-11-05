set -e
W="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/deck-stalls-05-11"
python3 "$W/patch_procmap.py" "$W/orig-inc/ufc_linux_native.h" "$W/fix-inc/ufc_linux_native.h"
# the parser kept as QueryParse must be the original Query body, character for character
python3 - "$W/orig-inc/ufc_linux_native.h" "$W/fix-inc/ufc_linux_native.h" <<'PY'
import sys,re
o=open(sys.argv[1]).read().replace('\r\n','\n'); n=open(sys.argv[2]).read()
ob=o[o.index('inline bool Query(void* address,Region* out,size_t){'):]; ob=ob[ob.index('{'):ob.index('}close(fd);return found;\n}')]
nb=n[n.index('inline bool QueryParse(void* address,Region* out){'):]; nb=nb[nb.index('{'):nb.index('}close(fd);return found;\n}')]
print('parser body identical to original Query:', ob==nb)
PY
/usr/lib/llvm-19/bin/clang++ -std=c++20 -O2 -I"$W/fix-inc" "$W/test_procmap.cpp" -o /root/test_procmap
ulimit -n 4096
/root/test_procmap exact
/root/test_procmap timing
/root/test_procmap workload old 10
/root/test_procmap workload new 10
