# Fixed GPU library: the shared parity-full ufc_linux_native.h is overlaid (private mount namespace,
# nothing on disk changes) by fix-inc/ufc_linux_native.h for this compile only.
set -euo pipefail
W="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/deck-stalls-05-11"; B15="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-05-11"
P="/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/continue-o-projeto-ufc3mons-port-nativo/work/parity-full/include/ufc_linux_native.h"
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sha256sum "$P" | cut -c1-16
unshare -m sh -c 'mount --bind "$1" "$2" && cd "$3" && python3 "$4" --fix' \
  sh "$W/fix-inc/ufc_linux_native.h" "$P" "$B15" "$SCRIPT_DIR/build_gpu_linux_b15.py" 2>&1 | tail -2
sha256sum "$P" | cut -c1-16
