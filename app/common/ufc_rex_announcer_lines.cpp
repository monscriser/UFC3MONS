// Ring announcer (Bruce Buffer / PRIDE announcer) intro no longer waits for Start after a line
// it did not notice (--ufc_rex_announcer_lines, default off; false = the original).
//
// Guest code (identical in Original v48 and Reborn r9):
//  sub_82F18A78(event, args, n)  resolves a dialogue event to an audio node (0 = no recording).
//  sub_82F1A4D8(seq, node, ., event, delay)  queues a resolved line on a dynamic sequence.
//    Announcer callers: sub_827D7010 (resolve LR 0x827D7044, queue LR 0x827D70A4, sequence
//    this+44; this = r29) and sub_82C36530 (PRIDE; resolve LR 0x82C3659C, queue LR 0x82C366B4,
//    sequence this or this+28; this = r29). A line resolving to 0 is never queued.
//  sub_82F1AE28(seq)             the event playing on the sequence now (0 = none).
//  sub_827D6E68(sound, debounced)  polls sound+44; with r4 != 0 it returns a line only once it
//    has stayed current for 5 ticks (a new line sets the counter at 0x833DFE98 to 1, i.e. bit 0
//    = armed; the line is returned once the counter, advanced by 2 per tick elsewhere, reaches
//    10; last line at 0x833DFE9C), otherwise 0. The gate [[0x833DFA84]+72]+2152 must be set. Only caller: the UFC intro director sub_82BAC5D8.
//  sub_82C36368(obj, event)      PRIDE director check: is event playing on obj or obj+28.
// The directors move to the next stage when they see the line they wait for. A line that is
// shorter than the 5-tick debounce (a fighter without his own recording gets a short filler
// node) or that ended between two polls is never seen, and a line with no recording is never
// queued at all, so the intro stalls until the player presses Start.
//
// Fix: every line the announcer callers queue, or fail to resolve, is remembered per sequence
// in order. When sub_82F1AE28 shows a new line, the remembered lines queued before it that were
// never reported are owed; while the sequence is idle, lines without a recording are owed at
// once and, after kIdle without anything playing or queued, the rest too. Owed lines are
// reported by sub_82F1AE28 one after another for kHold each, before the real value. With the
// option on, the debounced sub_827D6E68 returns that value at once (the game's counters still
// run). Default off: it changes the intro timing and needs an in-game check.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(ufc_rex_announcer_lines, true, "UFC",
                    "Ring intro: let the announcer directors see every line, also lines too short to notice or "
                    "without a recording, so the intro does not wait for Start");

extern "C" void __imp__sub_82F18A78(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82F1A4D8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82F1AE28(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_827D6E68(PPCContext& ctx, uint8_t* base);

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kHold = std::chrono::milliseconds(100);  // ~3 ticks at 30 fps for each owed line
constexpr auto kIdle = std::chrono::milliseconds(250);  // gap between lines is shorter than this
constexpr size_t kMaxRemembered = 32;

constexpr uint32_t kUfcResolveLr = 0x827D7044, kUfcQueueLr = 0x827D70A4, kUfcSeqOffset = 44;
constexpr uint32_t kPrideResolveLr = 0x82C3659C, kPrideQueueLr = 0x82C366B4;

struct Line {
  uint32_t event;
  bool silent;  // no recording: never plays
};
struct Sequence {
  std::deque<Line> ahead;      // remembered, not seen playing yet
  std::deque<uint32_t> owed;   // missed, still to report
  uint32_t playing = 0;        // last real value
  bool reporting = false;
  Clock::time_point owed_since{}, idle_since{}, last_added{};
};
std::mutex g_mutex;
std::unordered_map<uint32_t, Sequence> g_seq;

struct LastPoll {
  bool polled = false;
  uint32_t value = 0;
};
thread_local LastPoll t_poll;

bool On() { return REXCVAR_GET(ufc_rex_announcer_lines); }

void Remember(uint32_t seq, uint32_t event, bool silent) {
  std::lock_guard<std::mutex> lock(g_mutex);
  Sequence& s = g_seq[seq];
  if (s.ahead.size() >= kMaxRemembered) s.ahead.pop_front();
  s.ahead.push_back({event, silent});
  s.last_added = Clock::now();
}

uint32_t Report(uint32_t seq, uint32_t real) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto found = g_seq.find(seq);
  if (found == g_seq.end()) return real;
  Sequence& s = found->second;
  const auto now = Clock::now();
  const size_t owed_before = s.owed.size();
  if (real != s.playing) {
    if (real != 0) {
      size_t at = 0;
      while (at < s.ahead.size() && (s.ahead[at].silent || s.ahead[at].event != real)) ++at;
      if (at < s.ahead.size()) {
        for (size_t k = 0; k < at; ++k)
          if (s.ahead[k].silent || s.ahead[k].event != s.playing) s.owed.push_back(s.ahead[k].event);
        s.ahead.erase(s.ahead.begin(), s.ahead.begin() + static_cast<std::ptrdiff_t>(at + 1));
      }
    } else {
      s.idle_since = now;
    }
    s.playing = real;
  } else if (real == 0) {
    const bool idle = now - (s.idle_since > s.last_added ? s.idle_since : s.last_added) >= kIdle;
    while (!s.ahead.empty() && (s.ahead.front().silent || idle)) {
      s.owed.push_back(s.ahead.front().event);
      s.ahead.pop_front();
    }
  }
  if (s.owed.size() > owed_before)
    REXLOG_INFO("[ufc-rex-announcer] seq 0x{:08X}: {} unseen line(s), next 0x{:08X}", seq,
                s.owed.size() - owed_before, s.owed.front());
  if (s.owed.empty()) {
    if (real == 0 && s.ahead.empty() && now - s.last_added >= kIdle) g_seq.erase(found);
    return real;
  }
  if (!s.reporting) {
    s.reporting = true;
    s.owed_since = now;
  } else if (now - s.owed_since >= kHold) {
    s.owed.pop_front();
    s.owed_since = now;
    if (s.owed.empty()) {
      s.reporting = false;
      return real;
    }
  }
  return s.owed.front();
}
}  // namespace

REX_HOOK_RAW(sub_82F18A78) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr), event = ctx.r3.u32, owner = ctx.r29.u32;
  __imp__sub_82F18A78(ctx, base);
  if (ctx.r3.u32 != 0 || !On()) return;
  if (lr == kUfcResolveLr) Remember(owner + kUfcSeqOffset, event, true);
  else if (lr == kPrideResolveLr) Remember(owner, event, true);  // the director checks obj first
}

REX_HOOK_RAW(sub_82F1A4D8) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr), seq = ctx.r3.u32, event = ctx.r6.u32;
  __imp__sub_82F1A4D8(ctx, base);
  if (On() && (lr == kUfcQueueLr || lr == kPrideQueueLr)) Remember(seq, event, false);
}

REX_HOOK_RAW(sub_82F1AE28) {
  const uint32_t seq = ctx.r3.u32;
  __imp__sub_82F1AE28(ctx, base);
  if (!On()) return;
  ctx.r3.u64 = Report(seq, ctx.r3.u32);
  t_poll.polled = true;
  t_poll.value = ctx.r3.u32;
}

REX_HOOK_RAW(sub_827D6E68) {
  const bool debounced = ctx.r4.u32 != 0;
  t_poll.polled = false;
  __imp__sub_827D6E68(ctx, base);
  if (On() && debounced && t_poll.polled) ctx.r3.u64 = t_poll.value;
}
