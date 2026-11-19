// UFC3MONS PGO training helper (only compiled into instrumented builds: -DUFC_PGO_GEN together with
// -fprofile-generate). The game ends with a hard exit (TerminateTitle + _Exit / ExitProcess), which skips
// the atexit handler that normally writes the profile, so nothing would be written. With this unit
// linked in, UFC_PGO_DUMP_SECS=N writes the profile every N seconds while the game runs; the last
// write before closing the game is the one that counts.
//   Linux:   LLVM_PROFILE_FILE=/path/ufc-%p.profraw UFC_PGO_DUMP_SECS=20 ./ufc3-launcher-l1
//   Windows: set LLVM_PROFILE_FILE=C:\pgo\ufc-%p.profraw & set UFC_PGO_DUMP_SECS=20 & ufc3-launcher.exe
// Normal builds compile this file to nothing.
#ifdef UFC_PGO_GEN
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

extern "C" int __llvm_profile_write_file(void);

namespace {
struct UfcPgoDumper {
  UfcPgoDumper() {
    const char* e = std::getenv("UFC_PGO_DUMP_SECS");
    if (!e) return;
    const int secs = std::max(1, std::atoi(e));
    std::thread([secs] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(secs));
        __llvm_profile_write_file();
      }
    }).detach();
  }
} ufc_pgo_dumper;
}  // namespace
#endif
