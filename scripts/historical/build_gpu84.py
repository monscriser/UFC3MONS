# UFC3MONS Linux GPU plugin for R8.4, rebuilt from the exact R7 plugin sources (parity-full/gpu).
#   python3 build_gpu84.py control   -> unchanged sources, must reproduce the R7 plugin hash
#   python3 build_gpu84.py build     -> + ufc_request_range_fast (Hydro 8.9.1 single-range shortcut)
# Writes only under work/r8-input-lock/gpu84-<mode>.
from pathlib import Path
import shlex, json, subprocess, hashlib, shutil, sys
root = Path(__file__).resolve().parent
mode = sys.argv[1]
r7 = root.parent / 'parity-full'
out_dir = root / ('gpu84-' + mode)
if out_dir.exists():
    dest = root / '_to_delete' / f'gpu84-{mode}-{len(list((root / "_to_delete").glob("*")))}'
    dest.parent.mkdir(exist_ok=True); shutil.move(str(out_dir), str(dest))
src = out_dir / 'gpu'; obj = out_dir / 'obj'; logs = out_dir / 'logs'
shutil.copytree(r7 / 'gpu', src); obj.mkdir(); logs.mkdir()
old = Path('/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/files-mentioned-by-the-user-texto/work/linux-r5')
commands = [shlex.split(l) for l in (old / 'build.sh').read_text().splitlines() if l.startswith("'/usr/lib/llvm-19/bin/clang++'")]
base = next(c for c in commands if '-c' in c)
link = next(c for c in commands if '-shared' in c)
sdk = Path('/mnt/e/UFC3-RexGlue-Lab/sources/rexglue-sdk-clean')
replacements = {
    'command_processor.cpp': old / 'obj/command_processor.cpp.o',
    'vulkan_command_processor.cpp': old / 'obj/vulkan_command_processor.cpp.o',
    'vulkan_shared_memory.cpp': old / 'obj/shared_memory.cpp.o',
    'shared_memory.cpp': Path('/mnt/e/UFC3-RexGlue-Lab/build/sdk-linux/src/graphics/CMakeFiles/rexgpu-xenos.dir/shared_memory.cpp.o'),
    'render_target_cache.cpp': Path('/mnt/e/UFC3-RexGlue-Lab/build/sdk-linux/src/graphics/CMakeFiles/rexgpu-xenos.dir/vulkan/render_target_cache.cpp.o'),
    'deferred_command_buffer.cpp': Path('/mnt/e/UFC3-RexGlue-Lab/build/sdk-linux/src/graphics/CMakeFiles/rexgpu-xenos.dir/vulkan/deferred_command_buffer.cpp.o'),
    'spirv_translator_fetch.cpp': old / 'obj/spirv_translator_fetch.cpp.o'}

def rep(name, a, b):
    p = src / name; t = p.read_text(); assert t.count(a) == 1, (name, a[:60], t.count(a)); p.write_text(t.replace(a, b))

if mode in ('build', 'async'):
    rep('shared_memory.cpp', '#include <rex/assert.h>\n', '#include <rex/assert.h>\n#include <rex/cvar.h>\n#include <rex/logging.h>\n')
    rep('shared_memory.cpp', 'bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {\n', '''// UFC R8.4: Hydro 8.9.1 single-range shortcut. Most single-range requests only confirm that
// every page is already valid; the original builds, sorts and frees a vector to find that
// nothing needs uploading and returns true. Same bounds checks, same allocation, same valid
// bits under the same lock; any invalid page takes the original path unchanged.
REXCVAR_DEFINE_BOOL(ufc_request_range_fast, true, "GPU",
                    "Single-range shared memory requests: skip the merge/sort when every page is valid");
REXCVAR_DEFINE_BOOL(ufc_request_range_fast_verify, false, "GPU",
                    "TEST ONLY: after each fast hit also run the original path and count uploads it wanted");
namespace {
thread_local bool ufc_rr_in_verify = false;
uint64_t ufc_rr_fast_hits = 0, ufc_rr_verify_uploads = 0;
}  // namespace

bool SharedMemory::RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count) {
  if (count == 1 && ranges && !ufc_rr_in_verify && REXCVAR_GET(ufc_request_range_fast)) {
    const uint32_t start = ranges[0].first, length = ranges[0].second;
    if (!length) {
      return true;
    }
    if (start > kBufferSize || (kBufferSize - start) < length) {
      return false;
    }
    if (!EnsureHostGpuMemoryAllocated(start, length)) {
      return false;
    }
    const uint32_t page_first = start >> page_size_log2_;
    const uint32_t page_last = (start + length - 1) >> page_size_log2_;
    const bool verify = REXCVAR_GET(ufc_request_range_fast_verify);
    // Proof mode keeps the recursive global lock until the original path re-ran.
    std::unique_lock<std::recursive_mutex> verify_lock;
    if (verify) {
      verify_lock = global_critical_region_.Acquire();
    }
    bool all_valid = true;
    {
      auto global_lock = global_critical_region_.Acquire();
      for (uint32_t i = page_first >> 6; i <= (page_last >> 6); ++i) {
        uint64_t mask = UINT64_MAX;
        if (i == (page_first >> 6)) {
          mask &= ~((uint64_t(1) << (page_first & 63)) - 1);
        }
        if (i == (page_last >> 6) && (page_last & 63) != 63) {
          mask &= (uint64_t(1) << ((page_last & 63) + 1)) - 1;
        }
        if ((system_page_flags_valid_[i] & mask) != mask) {
          all_valid = false;
          break;
        }
      }
    }
    if (all_valid) {
      if (verify) {
        ++ufc_rr_fast_hits;
        const uint64_t before = ufc_rr_verify_uploads;
        ufc_rr_in_verify = true;
        const bool ok = RequestRanges(ranges, count);
        ufc_rr_in_verify = false;
        if ((ufc_rr_fast_hits & 0x3FFFF) == 0 || ufc_rr_verify_uploads != before) {
          REXGPU_WARN("[ufc-rrfast-verify] fast_hits={} original_uploads={}", ufc_rr_fast_hits,
                      ufc_rr_verify_uploads);
        }
        return ok;
      }
      return true;
    }
  }
''')
    rep('shared_memory.cpp', '  if (upload_ranges_.empty()) {\n    return true;\n  }\n\n  return UploadRanges(upload_ranges_);\n',
        '  if (upload_ranges_.empty()) {\n    return true;\n  }\n\n  if (ufc_rr_in_verify) {\n    ++ufc_rr_verify_uploads;\n  }\n  return UploadRanges(upload_ranges_);\n')

if mode == 'async':
    subprocess.run(['python3', str(root / 'patch_async_ufc.py'), str(src)], check=True)

def compile_one(name):
    command = list(base)
    # Same embedded source paths as the R7 build (__FILE__ in log/assert strings).
    command[1:1] = ['-ffile-prefix-map=' + str(src) + '=' + str(r7 / 'gpu'),
                    '-I' + str(r7 / 'include'), '-I' + str(src), '-I' + str(sdk / 'src/graphics/vulkan'), '-I' + str(sdk / 'src/graphics')]
    command[command.index('-c') + 1] = str(src / name); command[command.index('-o') + 1] = str(obj / (name + '.o'))
    (logs / (name + '.command.json')).write_text(json.dumps(command, indent=2))
    with (logs / (name + '.log')).open('w') as f:
        r = subprocess.run(command, stdout=f, stderr=subprocess.STDOUT)
    print(name, 'OK' if not r.returncode else 'FAIL', flush=True)
    return r.returncode

import concurrent.futures
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    codes = list(pool.map(compile_one, replacements))
assert not any(codes), codes
for name, previous in replacements.items():
    assert link.count(str(previous)) == 1
    link[link.index(str(previous))] = str(obj / (name + '.o'))
out = out_dir / 'librexgpu-xenos.so'; link[link.index('-o') + 1] = str(out)
link.extend(['-ldl'])
(logs / 'link.command.json').write_text(json.dumps(link, indent=2))
with (logs / 'link.log').open('w') as f:
    subprocess.run(link, stdout=f, stderr=subprocess.STDOUT, check=True)
digest = hashlib.sha256(out.read_bytes()).hexdigest()
r7_hash = json.loads((r7 / 'gpu-build-result.json').read_text())['sha256']
print(json.dumps({'mode': mode, 'sha256': digest, 'r7_sha256': r7_hash, 'matches_r7': digest == r7_hash}))
(out_dir / 'result.json').write_text(json.dumps({'mode': mode, 'file': str(out), 'sha256': digest, 'r7_sha256': r7_hash}, indent=2))
