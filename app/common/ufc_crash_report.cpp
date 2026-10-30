// Windows crash report (P14.6). When the game dies from an exception nobody handled (the emulator's own
// write-watch faults are handled earlier by its vectored handler and never reach this filter), write one line to
// the game log with the exception code, module and offset, flush the log, and save a small minidump next to it.
// Also logs std::terminate (uncaught C++ exception). Off with --ufc_crash_report=false.
// Test only: environment UFC_TEST_CRASH=<seconds> makes a thread read address 0 after that time.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/logging/api.h>

REXCVAR_DEFINE_BOOL(ufc_crash_report, true, "UFC", "Log unhandled crashes (code, module, offset) and save a minidump next to the log");

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_reported{false};

std::filesystem::path DumpFolder() {
  const std::string log = REXCVAR_GET(log_file);
  std::error_code error;
  if (!log.empty() && log != "NUL" && log != "nul") {
    const auto parent = std::filesystem::u8path(log).parent_path();
    if (!parent.empty() && std::filesystem::is_directory(parent, error)) return parent;
  }
  wchar_t temp[MAX_PATH];
  return GetTempPathW(MAX_PATH, temp) ? std::filesystem::path(temp) : std::filesystem::path(L".");
}

void WriteDump(EXCEPTION_POINTERS* pointers) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t name[96];
  swprintf(name, 96, L"ufc3-crash-%04d%02d%02d-%02d%02d%02d.dmp", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  const auto path = DumpFolder() / name;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), pointers, FALSE};
  const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                                    MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                                    pointers ? &info : nullptr, nullptr, nullptr);
  CloseHandle(file);
  REXLOG_ERROR("[ufc-crash] minidump {} {}", ok ? "saved" : "failed", path.string());
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* pointers) {
  if (!REXCVAR_GET(ufc_crash_report) || g_reported.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* r = pointers->ExceptionRecord;
  HMODULE module = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCSTR>(r->ExceptionAddress), &module))
    GetModuleFileNameA(module, name, MAX_PATH);
  const char* base = std::strrchr(name, '\\');
  const uintptr_t offset = module ? uintptr_t(r->ExceptionAddress) - uintptr_t(module) : uintptr_t(r->ExceptionAddress);
  REXLOG_ERROR("[ufc-crash] code=0x{:08X} module={} offset=0x{:X} access={} target=0x{:X} thread={}",
               uint32_t(r->ExceptionCode), base ? base + 1 : name, offset,
               r->NumberParameters > 0 ? int(r->ExceptionInformation[0]) : -1,
               r->NumberParameters > 1 ? uint64_t(r->ExceptionInformation[1]) : 0, GetCurrentThreadId());
  WriteDump(pointers);
  rex::FlushLogging();
  return EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void OnTerminate() {
  if (REXCVAR_GET(ufc_crash_report) && !g_reported.exchange(true)) {
    std::string what = "unknown";
    if (auto e = std::current_exception()) {
      try { std::rethrow_exception(e); } catch (const std::exception& x) { what = x.what(); } catch (...) {}
    }
    REXLOG_ERROR("[ufc-crash] std::terminate (uncaught C++ exception: {}) thread={}", what, GetCurrentThreadId());
    WriteDump(nullptr);
    rex::FlushLogging();
  }
  std::abort();
}

struct Install {
  Install() {
    SetUnhandledExceptionFilter(OnUnhandled);
    std::set_terminate(OnTerminate);
    if (const char* s = std::getenv("UFC_TEST_CRASH")) {
      const int seconds = std::atoi(s);
      if (seconds > 0)
        std::thread([seconds] {
          std::this_thread::sleep_for(std::chrono::seconds(seconds));
          volatile int* zero = nullptr;
          *zero = 1;  // test crash
        }).detach();
    }
  }
} install;
}  // namespace
#endif
