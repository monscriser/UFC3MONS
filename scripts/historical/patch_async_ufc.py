# UFC3MONS: Hydro 9.1.0 "ht_async_submit" adapted to the UFC Linux plugin WITHOUT header changes.
# The plugin links SDK objects built with the original class layout and a PCH, so no member may
# be added to VulkanCommandProcessor / DeferredCommandBuffer. Worker state lives in a namespace-
# scope struct; the deferred stream is moved by swapping the private vector through a
# pointer-to-member obtained by explicit instantiation (layout untouched).
#
# ufc_async_submit (default true): mid-frame EndSubmission(false) hands the finished command
# stream to a worker that records the VkCommandBuffer (through its own DeferredCommandBuffer,
# public Execute) and calls vkQueueSubmit in order. Swap submissions stay synchronous after
# draining the worker. CheckSubmissionFenceAndDeviceLoss drains before blocking waits and only
# polls fences of jobs already submitted; sparse binds and shutdown drain. A worker failure is
# treated as device loss. Disabled automatically when readback_resolve != none (V70 own queue
# submits), or GPU/resource timing is on. UFC_ASYNC_VERIFY=1: XXH3 of each stream at hand-off vs
# after recording (proves the stream was not modified while recorded).
# usage: python3 patch_async_ufc.py <gpu source dir>
import sys
from pathlib import Path
src = Path(sys.argv[1])
p = src / 'vulkan_command_processor.cpp'
s = p.read_text()

def rep(old, new):
    global s
    assert s.count(old) == 1, (old[:90], s.count(old))
    s = s.replace(old, new)

first_line = s.split('\n', 1)[0] + '\n'
rep(first_line, first_line + '''#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "xxhash.h"
''')

# Worker state and helpers, before the first member definition (the destructor).
rep('VulkanCommandProcessor::~VulkanCommandProcessor() = default;\n', '''REXCVAR_DEFINE_BOOL(ufc_async_submit, true, "GPU/Vulkan",
                    "CPU: record and submit mid-frame command buffers on a worker thread (Hydro 9.1.0)");

// Pointer to DeferredCommandBuffer::command_stream_ without changing the class (explicit
// instantiation may name private members).
namespace ufc_async_access {
struct StreamTag {
  using type = std::vector<uintmax_t> DeferredCommandBuffer::*;
  friend type Get(StreamTag);
};
template <typename Tag, typename Tag::type M>
struct Rob {
  friend typename Tag::type Get(Tag) { return M; }
};
template struct Rob<StreamTag, &DeferredCommandBuffer::command_stream_>;
}  // namespace ufc_async_access

namespace {
struct UfcAsyncJob {
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer buffer = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  std::vector<VkSemaphore> wait_semaphores;
  std::vector<VkPipelineStageFlags> wait_stages;
  std::vector<uintmax_t> stream;
  uint64_t hash = 0;
};
struct UfcAsyncState {
  bool started = false;
  const ui::vulkan::VulkanDevice* device = nullptr;
  std::unique_ptr<DeferredCommandBuffer> exec;
  std::thread thread;
  std::mutex mutex;
  std::condition_variable cv, done_cv;
  std::deque<UfcAsyncJob> queue;
  size_t pending = 0;
  bool stop = false;
  std::atomic<bool> failed{false};
  std::vector<std::vector<uintmax_t>> free_streams;
  std::atomic<uint64_t> verified{0}, mismatches{0};
  uint64_t jobs = 0, syncs = 0, flushes = 0;
  std::chrono::steady_clock::time_point last_log{};
} ufc_async;

bool UfcAsyncVerify() {
  static const bool on = [] { const char* v = std::getenv("UFC_ASYNC_VERIFY"); return v && v[0] == '1'; }();
  return on;
}
uint64_t UfcStreamHash(const std::vector<uintmax_t>& v) {
  const uint64_t h = XXH3_64bits(v.data(), v.size() * sizeof(uintmax_t));
  return h ? h : 1;
}

void UfcAsyncWorker() {
  auto& a = ufc_async;
  const auto stream_member = Get(ufc_async_access::StreamTag{});
  for (;;) {
    UfcAsyncJob job;
    {
      std::unique_lock<std::mutex> lock(a.mutex);
      a.cv.wait(lock, [&] { return a.stop || !a.queue.empty(); });
      if (a.queue.empty()) return;
      job = std::move(a.queue.front());
      a.queue.pop_front();
    }
    if (!a.failed.load()) {
      const auto& dfn = a.device->functions();
      const VkDevice device = a.device->device();
      bool ok = dfn.vkResetCommandPool(device, job.pool, 0) == VK_SUCCESS;
      if (ok) {
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = dfn.vkBeginCommandBuffer(job.buffer, &begin_info) == VK_SUCCESS;
      }
      if (ok) {
        (a.exec.get()->*stream_member).swap(job.stream);
        a.exec->Execute(job.buffer);
        (a.exec.get()->*stream_member).swap(job.stream);
        if (job.hash) {
          ++a.verified;
          if (UfcStreamHash(job.stream) != job.hash) ++a.mismatches;
        }
        ok = dfn.vkEndCommandBuffer(job.buffer) == VK_SUCCESS;
      }
      if (ok) {
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if (!job.wait_semaphores.empty()) {
          submit_info.waitSemaphoreCount = uint32_t(job.wait_semaphores.size());
          submit_info.pWaitSemaphores = job.wait_semaphores.data();
          submit_info.pWaitDstStageMask = job.wait_stages.data();
        }
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &job.buffer;
        ui::vulkan::VulkanDevice::Queue::Acquisition queue_acquisition =
            a.device->AcquireQueue(a.device->queue_family_graphics_compute(), 0);
        ok = dfn.vkQueueSubmit(queue_acquisition.queue(), 1, &submit_info, job.fence) == VK_SUCCESS;
      }
      if (!ok) {
        REXGPU_ERROR("[ufc-async-submit] worker submission failed; treating as device loss");
        a.failed = true;
      }
    }
    {
      std::lock_guard<std::mutex> lock(a.mutex);
      job.stream.clear();
      a.free_streams.push_back(std::move(job.stream));
      --a.pending;
    }
    a.done_cv.notify_all();
  }
}

void UfcAsyncFlush() {
  auto& a = ufc_async;
  if (!a.started) return;
  std::unique_lock<std::mutex> lock(a.mutex);
  if (a.pending) ++a.flushes;
  a.done_cv.wait(lock, [&] { return a.pending == 0; });
}

size_t UfcAsyncPending() {
  auto& a = ufc_async;
  if (!a.started) return 0;
  std::lock_guard<std::mutex> lock(a.mutex);
  return a.pending;
}

void UfcAsyncStop() {
  auto& a = ufc_async;
  if (!a.started) return;
  {
    std::lock_guard<std::mutex> lock(a.mutex);
    a.stop = true;
  }
  a.cv.notify_all();
  if (a.thread.joinable()) a.thread.join();
  a.exec.reset();
  a.started = false;
  a.stop = false;
}

bool UfcAsyncWanted(bool is_swap) {
  if (is_swap || ufc_async.failed.load() || !REXCVAR_GET(ufc_async_submit)) return false;
  if (ufc_gpu_timing::Enabled() || ufc_resource_timing::Enabled()) return false;
  // V70 / lossless readback submit to the queue themselves: keep everything synchronous then.
  return rex::cvar::GetFlagByName("readback_resolve") == "none";
}
}  // namespace

VulkanCommandProcessor::~VulkanCommandProcessor() { UfcAsyncStop(); }
''')

# Shutdown: drain everything (the await below drains the worker) and stop it.
shutdown_await = '  ([&]() {ufc_wait_diag::Context reason("vulkan_cp:1914");ufc_wait_diag::Scope elapsed(ufc_wait_diag::AwaitAll);return AwaitAllQueueOperationsCompletion();}());\n'
rep(shutdown_await, shutdown_await + '  UfcAsyncStop();\n')

# Fence checks: drain before blocking waits; only poll fences of submitted jobs.
rep('  size_t fences_total = submissions_in_flight_fences_.size();\n  size_t fences_awaited = 0;\n',
    '''  size_t ufc_unsubmitted = 0;
  if (ufc_async.started) {
    if (await_submission > submission_completed_) {
      UfcAsyncFlush();
    }
    ufc_unsubmitted = UfcAsyncPending();
    if (ufc_async.failed.load()) {
      device_lost_ = true;
      if (graphics_system_) {
        graphics_system_->OnHostGpuLossFromAnyThread(true);
      }
      return;
    }
  }
  size_t fences_total = submissions_in_flight_fences_.size() - ufc_unsubmitted;
  size_t fences_awaited = 0;
''')

# Sparse binds go straight to the queue: keep queue order.
rep('    if (!sparse_memory_binds_.empty()) {\n      sparse_buffer_bind_infos_temp_.clear();\n',
    '    if (!sparse_memory_binds_.empty()) {\n      UfcAsyncFlush();\n      sparse_buffer_bind_infos_temp_.clear();\n')

# EndSubmission: hand mid-frame submissions to the worker; the original block stays as the
# synchronous path.
start = '''    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    if (dfn.vkResetCommandPool(device, command_buffer.pool, 0) != VK_SUCCESS) {'''
end = '''    uint64_t submission_current = GetCurrentSubmission();
    ufc_ondemand::Submitted(submission_current);'''
assert s.count(start) == 1 and s.count(end) == 1
a = s.index(start); b = s.index(end)
block = s[a:b]
fence_decl = '    assert_false(fences_free_.empty());\n    VkFence fence = fences_free_.back();\n'
assert block.count(fence_decl) == 1
sync_body = block.replace(fence_decl, '')
sync_body = sync_body.replace('    assert_false(command_buffers_writable_.empty());\n    CommandBuffer command_buffer = command_buffers_writable_.back();\n', '')
sync_body = '\n'.join(('  ' + l if l else l) for l in sync_body.split('\n'))
new_block = '''    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    assert_false(fences_free_.empty());
    VkFence fence = fences_free_.back();
    if (UfcAsyncWanted(is_swap)) {
      auto& ua = ufc_async;
      if (!ua.started) {
        ua.device = vulkan_device;
        ua.exec = std::make_unique<DeferredCommandBuffer>(*this);
        ua.stop = false;
        ua.thread = std::thread(UfcAsyncWorker);
        ua.started = true;
        REXGPU_INFO("[ufc-async-submit] worker started");
      }
      if (dfn.vkResetFences(device, 1, &fence) != VK_SUCCESS) {
        REXGPU_ERROR("Failed to reset a Vulkan submission fence");
        return false;
      }
      UfcAsyncJob job;
      job.pool = command_buffer.pool;
      job.buffer = command_buffer.buffer;
      job.fence = fence;
      job.wait_semaphores = current_submission_wait_semaphores_;
      job.wait_stages = current_submission_wait_stage_masks_;
      {
        std::lock_guard<std::mutex> lock(ua.mutex);
        if (!ua.free_streams.empty()) {
          job.stream = std::move(ua.free_streams.back());
          ua.free_streams.pop_back();
        }
      }
      job.stream.clear();
      (deferred_command_buffer_.*Get(ufc_async_access::StreamTag{})).swap(job.stream);
      if (UfcAsyncVerify()) job.hash = UfcStreamHash(job.stream);
      {
        std::lock_guard<std::mutex> lock(ua.mutex);
        ua.queue.push_back(std::move(job));
        ++ua.pending;
      }
      ua.cv.notify_one();
      ++ua.jobs;
    } else {
      if (ufc_async.started) {
        UfcAsyncFlush();
        ++ufc_async.syncs;
        if (ufc_async.failed.load()) {
          return false;
        }
      }
''' + sync_body.rstrip() + '''
    }
    if (ufc_async.started) {
      const auto now = std::chrono::steady_clock::now();
      if (now - ufc_async.last_log > std::chrono::seconds(10)) {
        ufc_async.last_log = now;
        REXGPU_INFO("[ufc-async-submit] async={} sync={} flushes={} verified={} mismatches={}",
                    ufc_async.jobs, ufc_async.syncs, ufc_async.flushes, ufc_async.verified.load(),
                    ufc_async.mismatches.load());
      }
    }
'''
s = s[:a] + new_block + s[b:]
p.write_text(s)
print('patched ufc async submit')
