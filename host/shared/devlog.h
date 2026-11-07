#pragma once
// The device call log's recorder (TS2HOST_DEVLOG; host/control_devlog.cpp, host/shared/d3dlog.h; docs/ui-testing.md).
// A diagnostic: it records, never changes, what the game asks of Direct3D. Built into the host and the difftest runner
// (tests/host_devlog.cpp).
//
// Calls arrive as canonical text records (no pointers, no machine-dependent values) and are grouped by the frame
// (the host's framework tick) current when they are made. Each record is one of two kinds:
//   stream: the ordered part: state, draws, dynamic buffer uploads, Present;
//   async:  resource creation and static uploads, plus every call made off the main thread: compared as a set per
//           frame where two reference runs show them reordered (tools/devlog.py).
// When a frame ends (a record of a later frame arrives, or Finish) its digest line is written; for chosen frames the
// records themselves follow it. File format (text, one record per line; tools/devlog.py reads it):
//   devlog <format> <settings>                          header (written by the host; format 2: host/shared/d3dlog.h)
//   frame <n> stream <count> <hash> async <count> <sethash> <seqhash> draws <d> presents <p>
//   s <n> <i> <text>                                    stream record i of frame n (full frames only)
//   a <n> <i> <text>                                    async record i of frame n (full frames only)
//   full <label> <first> <count>                        frames first.. first+count-1 are written in full
//   mark <n> <label>                                    a checkpoint
//   clk <n> <fields> | <fields>                         the frame clock's tick n (host/control_frameclock.cpp): what
//                                                       the clocks returned and how threads were scheduled; the part
//                                                       before " |" is deterministic under lockstep
// <hash> chains the stream records' hashes in order; <sethash> is the sum of the async records' hashes (order-free),
// <seqhash> chains them in order. All hashes are 16 hex digits.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devlog {

// 64-bit hash of a byte range (MurmurHash64A's construction; not cryptographic).
uint64_t Hash(const void *data, size_t size, uint64_t seed = 0x5d3c0de9);
// The same, but a fault while reading (a lock pointer gone bad) gives `fallback` instead of crashing the game.
uint64_t HashGuarded(const void *data, size_t size, uint64_t fallback);
inline uint64_t Mix(uint64_t a, uint64_t b)
{
    const uint64_t v[2] = {a, b};
    return Hash(v, sizeof(v), 0x9e3779b97f4a7c15ull);
}

enum class Kind : uint8_t { Stream, Async };

// Frame ranges "900,4000-4002" (inclusive).
class FrameSet {
public:
    bool Parse(std::string_view spec); // false on a malformed spec (nothing added)
    void Add(uint32_t first, uint32_t last) { mRanges.emplace_back(first, last); }
    bool Contains(uint32_t frame) const;
    bool Empty() const { return mRanges.empty(); }

private:
    std::vector<std::pair<uint32_t, uint32_t>> mRanges;
};

class Recorder {
public:
    using FrameSource = std::function<uint32_t()>;
    using Sink = std::function<void(std::string_view)>; // receives whole lines, each ending in '\n'

    Recorder(FrameSource frame, Sink sink) : mFrame(std::move(frame)), mSink(std::move(sink)) {}

    // Frames written in full (TS2HOST_DEVLOG's frame list).
    void SetFullFrames(const FrameSet &frames) { mFull = frames; }
    // The "devlog <count> <label>" control command: the current frame and the count-1 after it in full.
    void RequestFull(uint32_t count, std::string_view label);
    void Mark(std::string_view label);
    // A line of its own, written at once (the frame clock's "clk <frame> ..." lines); it must not start with a record
    // or digest keyword.
    void Note(std::string_view line);
    // Calls made on other threads are recorded as async whatever their kind (0: no main thread yet; every call counts
    // as the main thread's).
    void SetMainThread(uint32_t threadId) { mMainThread = threadId; }
    uint32_t MainThread() const { return mMainThread; }

    // draw/present: counted in the frame's digest line.
    void Record(Kind kind, std::string_view text, bool draw = false, bool present = false);
    void Finish(); // ends the current frame: writes its lines
    uint32_t CurrentFrame() const { return mCurrent; }

private:
    void Advance(uint32_t frame); // under mLock
    void WriteFrame();            // under mLock

    FrameSource mFrame;
    Sink mSink;
    FrameSet mFull;
    uint32_t mMainThread = 0;
    std::mutex mLock;
    bool mOpen = false;    // a frame has records
    uint32_t mCurrent = 0; // its number
    uint32_t mStreamCount = 0, mAsyncCount = 0, mDraws = 0, mPresents = 0;
    uint64_t mStreamHash = 0, mAsyncSum = 0, mAsyncSeq = 0;
    bool mFullFrame = false;
    std::string mLines; // the frame's records, when written in full
};

} // namespace devlog
