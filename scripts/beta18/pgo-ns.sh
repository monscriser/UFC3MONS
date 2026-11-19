#!/usr/bin/env bash
# Runs a command with the archived Codex/2026-10-20 and 2026-10-21 folders bind-mounted on their original
# /mnt/c paths (private mount namespace; the empty mount points are removed afterwards).
set -uo pipefail
C=/mnt/c/Users/Monscriser/Documents/Codex; A="/mnt/e/PROJETOS ANTIGOS/Codex-22-10"
made=()
for d in 2026-10-20 2026-10-21; do [ -d "$C/$d" ] || { mkdir "$C/$d"; made+=("$C/$d"); }; done
unshare -m sh -c 'mount --bind "$1/2026-10-20" "$2/2026-10-20" && mount --bind "$1/2026-10-21" "$2/2026-10-21" && shift 2 && exec "$@"' sh "$A" "$C" "$@"
rc=$?
for d in "${made[@]}"; do rmdir "$d"; done
exit $rc
