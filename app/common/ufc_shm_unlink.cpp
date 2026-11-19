// Linux: the guest memory (/dev/shm/xenia_memory_<n>, ~4.8 GB sparse) must not outlive the process.
// librexruntime creates it with shm_open and only calls shm_unlink on a clean shutdown, which the
// hard exit (TerminateTitle + _Exit) skips: measured on the ROG Ally (18/11/2026), every run left its file
// and the RAM it held behind (~600 MB each; 7 runs = 4.6 GB in /dev/shm, then the next start died with
// SIGBUS right after "Guest memory arena mapped"). The mapping only needs the open descriptor, so the
// name is removed as soon as the memory exists; the kernel frees it when the process ends, however it ends.
// Validated on the Ally with an injected shm_unlink right after shm_open: game normal, nothing left in
// /dev/shm even after kill -9.
#include <rex/logging.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/mman.h>
#include <unistd.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#endif

void UfcUnlinkOwnGuestMemory() {
#ifndef _WIN32
  DIR* fds = opendir("/proc/self/fd");
  if (!fds) return;
  int removed = 0;
  while (dirent* e = readdir(fds)) {
    if (e->d_name[0] == '.') continue;
    char link[64], target[PATH_MAX];
    std::snprintf(link, sizeof link, "/proc/self/fd/%s", e->d_name);
    const ssize_t n = readlink(link, target, sizeof target - 1);
    if (n <= 0) continue;
    target[n] = 0;
    static constexpr char kPrefix[] = "/dev/shm/xenia_memory_";
    // An already removed name reads "... (deleted)": nothing to do then.
    if (std::strncmp(target, kPrefix, sizeof kPrefix - 1) != 0 || std::strstr(target, "(deleted)")) continue;
    const std::string name = std::string("/") + (target + sizeof("/dev/shm/") - 1);
    if (shm_unlink(name.c_str()) == 0) ++removed;
  }
  closedir(fds);
  REXLOG_INFO("[ufc-shm] guest memory name removed from /dev/shm ({} file(s)); freed when the game exits", removed);
#endif
}
