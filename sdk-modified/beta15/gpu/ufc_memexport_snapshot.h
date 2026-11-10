#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include "../../include/ufc_linux_mapping.h"
#endif

namespace ufc_memexport {

enum class Access { kUnreadable, kReadOnly, kWritable };

struct Region {
  size_t length = 0;
  uintptr_t allocation = 0;
  Access access = Access::kUnreadable;
};

struct Snapshot {
  struct Run {
    size_t offset;
    size_t length;
    uintptr_t allocation;
  };

  uint8_t* destination = nullptr;
  size_t length = 0;
  std::unique_ptr<uint8_t[]> before;
  size_t before_capacity = 0;
  std::vector<Run> runs;
};

inline void Prepare(Snapshot& snapshot, uint8_t* destination, size_t length) {
  snapshot.destination = destination;
  snapshot.length = length;
  if (length > snapshot.before_capacity) {
    snapshot.before.reset(new uint8_t[length]);
    snapshot.before_capacity = length;
  }
  // VMAs can split at page boundaries after protection changes. Reserve the
  // maximum practical run count before callers acquire the global memory lock.
  // Retain capacity across memexport draws to avoid repeated heap allocations.
  snapshot.runs.clear();
  const size_t required_runs = length / 4096 + 2;
  if (snapshot.runs.capacity() < required_runs) snapshot.runs.reserve(required_runs);
}

inline Snapshot Prepare(uint8_t* destination, size_t length) {
  Snapshot snapshot;
  Prepare(snapshot, destination, length);
  return snapshot;
}

// Read only mapped, readable runs. Keep their offsets so holes do not shift the readback.
template <class Query>
void CaptureInto(Snapshot& snapshot, Query query) {
  snapshot.runs.clear();
  for (size_t offset = 0; offset < snapshot.length;) {
    const Region region = query(snapshot.destination + offset);
    if (!region.length) break;
    const size_t size = std::min(region.length, snapshot.length - offset);
    if (region.allocation && region.access != Access::kUnreadable) {
      std::memcpy(snapshot.before.get() + offset, snapshot.destination + offset, size);
      snapshot.runs.push_back({offset, size, region.allocation});
    }
    offset += size;
  }
}

template <class Query>
Snapshot Capture(uint8_t* destination, size_t length, Query query) {
  Snapshot snapshot = Prepare(destination, length);
  CaptureInto(snapshot, query);
  return snapshot;
}

struct WriteResult {
  size_t unavailable = 0;
  bool protection_restore_failed = false;
};

template <class Query, class WriteProtected>
WriteResult Apply(const Snapshot& snapshot, const uint8_t* source, Query query,
                  WriteProtected write_protected) {
  WriteResult result{snapshot.length, false};
  for (const auto& run : snapshot.runs) {
    for (size_t offset = 0; offset < run.length;) {
      const size_t index = run.offset + offset;
      auto* destination = snapshot.destination + index;
      const Region region = query(destination);
      if (!region.length) break;
      const size_t size = std::min(region.length, run.length - offset);
      const auto* before = snapshot.before.get() + index;
      if (region.allocation == run.allocation && region.access != Access::kUnreadable) {
        const auto* after = source + index;
        if (!std::memcmp(before, after, size)) {
          result.unavailable -= size;
        } else {
          // Memexport writes dwords. Preserve CPU writes to cells untouched by this draw.
          auto copy = [&] {
            size_t position = 0;
            auto equal_cell = [&](size_t cell) {
              return std::memcmp(before + cell, after + cell, std::min(size - cell, size_t{4})) == 0;
            };
            while (position < size) {
              while (position < size && equal_cell(position)) {
                position += std::min(size - position, size_t{4});
              }
              const size_t start = position;
              while (position < size && !equal_cell(position)) {
                position += std::min(size - position, size_t{4});
              }
              if (position != start) {
                std::memcpy(destination + start, after + start, position - start);
              }
            }
          };
          if (region.access == Access::kWritable) {
            copy();
            result.unavailable -= size;
          } else if (write_protected(destination, size, copy, result.protection_restore_failed)) {
            result.unavailable -= size;
          }
        }
      }
      offset += size;
    }
  }
  return result;
}

#ifdef _WIN32
inline Region QueryWin32Region(uint8_t* address) {
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof(info))) return {};
  const auto offset = reinterpret_cast<uintptr_t>(address) -
                      reinterpret_cast<uintptr_t>(info.BaseAddress);
  if (offset >= info.RegionSize) return {};
  Region region{info.RegionSize - offset,
                               reinterpret_cast<uintptr_t>(info.AllocationBase)};
  if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD)) return region;
  switch (info.Protect & 0xffu) {
    case PAGE_READONLY:
    case PAGE_EXECUTE_READ:
      region.access = Access::kReadOnly;
      break;
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
      region.access = Access::kWritable;
      break;
  }
  return region;
}

template <class Copy>
bool WithWin32WriteAccess(uint8_t* address, size_t size, Copy copy, bool& restore_failed) {
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & PAGE_GUARD)) return false;
  const DWORD access = info.Protect & 0xffu;
  if (access != PAGE_READONLY && access != PAGE_EXECUTE_READ) return false;
  const DWORD writable = access == PAGE_EXECUTE_READ ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
  DWORD previous;
  if (!VirtualProtect(address, size, writable, &previous)) return false;
  copy();
  DWORD ignored;
  if (!VirtualProtect(address, size, previous, &ignored)) restore_failed = true;
  return true;
}

inline Region QueryNativeRegion(uint8_t* address) {
  return QueryWin32Region(address);
}

template <class Copy>
inline bool WithNativeWriteAccess(uint8_t* address, size_t size, Copy copy,
                                  bool& restore_failed) {
  return WithWin32WriteAccess(address, size, copy, restore_failed);
}
#else
inline Region QueryLinuxRegion(uint8_t* address) {
  ufc_linux_mapping::Region info{};
  if (!ufc_linux_mapping::Query(address, &info, sizeof(info))) return {};
  const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
  const uintptr_t current = reinterpret_cast<uintptr_t>(address);
  if (current < base || current - base >= info.RegionSize) return {};

  Region region{info.RegionSize - size_t(current - base), 0, Access::kUnreadable};
  // Anonymous mappings have no stable identity when /proc/self/maps splits a VMA
  // after mprotect. Decline writeback there rather than accepting a remap at this address.
  if (info.State != ufc_linux_mapping::Committed || !info.Inode || !info.AllocationBase ||
      (info.Protect & (ufc_linux_mapping::NoAccess | ufc_linux_mapping::Guard))) {
    return region;
  }

  uint64_t identity = uint64_t(info.AllocationBase);
  identity ^= info.Device + 0x9E3779B97F4A7C15ULL + (identity << 6) + (identity >> 2);
  identity ^= info.Inode + 0x9E3779B97F4A7C15ULL + (identity << 6) + (identity >> 2);
  region.allocation = uintptr_t(identity ? identity : 1);
  if (!(info.Protect & ufc_linux_mapping::Read)) return region;
  region.access = (info.Protect & ufc_linux_mapping::Write) ? Access::kWritable : Access::kReadOnly;
  return region;
}

template <class Copy>
inline bool WithLinuxWriteAccess(uint8_t* address, size_t size, Copy copy,
                                 bool& restore_failed) {
  ufc_linux_mapping::Region info{};
  if (!ufc_linux_mapping::Query(address, &info, sizeof(info)) || info.State != ufc_linux_mapping::Committed ||
      !(info.Protect & ufc_linux_mapping::Read) ||
      (info.Protect & (ufc_linux_mapping::Write | ufc_linux_mapping::NoAccess | ufc_linux_mapping::Guard))) {
    return false;
  }

  const uint32_t expected = info.Protect;
  uint32_t previous = 0;
  if (!ufc_linux_mapping::Protect(address, size, expected | ufc_linux_mapping::Write, &previous)) return false;
  if (previous != expected) {
    uint32_t ignored = 0;
    if (!ufc_linux_mapping::Protect(address, size, previous, &ignored)) restore_failed = true;
    return false;
  }

  copy();
  uint32_t ignored = 0;
  if (!ufc_linux_mapping::Protect(address, size, previous, &ignored)) restore_failed = true;
  return true;
}

inline Region QueryNativeRegion(uint8_t* address) {
  return QueryLinuxRegion(address);
}

template <class Copy>
inline bool WithNativeWriteAccess(uint8_t* address, size_t size, Copy copy,
                                  bool& restore_failed) {
  return WithLinuxWriteAccess(address, size, copy, restore_failed);
}
#endif

}  // namespace ufc_memexport
