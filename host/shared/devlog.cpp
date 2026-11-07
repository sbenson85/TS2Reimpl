// The device call log's recorder (host/shared/devlog.h).
#include "devlog.h"

#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace devlog {

uint64_t Hash(const void *data, size_t size, uint64_t seed)
{
    constexpr uint64_t m = 0xc6a4a7935bd1e995ull;
    constexpr int r = 47;
    uint64_t h = seed ^ (static_cast<uint64_t>(size) * m);
    const auto *p = static_cast<const uint8_t *>(data);
    const uint8_t *end = p + (size & ~static_cast<size_t>(7));
    for (; p != end; p += 8) {
        uint64_t k;
        std::memcpy(&k, p, 8);
        k *= m;
        k ^= k >> r;
        k *= m;
        h ^= k;
        h *= m;
    }
    switch (size & 7) {
    case 7: h ^= static_cast<uint64_t>(p[6]) << 48; [[fallthrough]];
    case 6: h ^= static_cast<uint64_t>(p[5]) << 40; [[fallthrough]];
    case 5: h ^= static_cast<uint64_t>(p[4]) << 32; [[fallthrough]];
    case 4: h ^= static_cast<uint64_t>(p[3]) << 24; [[fallthrough]];
    case 3: h ^= static_cast<uint64_t>(p[2]) << 16; [[fallthrough]];
    case 2: h ^= static_cast<uint64_t>(p[1]) << 8; [[fallthrough]];
    case 1: h ^= static_cast<uint64_t>(p[0]); h *= m;
    }
    h ^= h >> r;
    h *= m;
    h ^= h >> r;
    return h;
}

uint64_t HashGuarded(const void *data, size_t size, uint64_t fallback)
{
    __try {
        return Hash(data, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

bool FrameSet::Parse(std::string_view spec)
{
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::string text(spec);
    const char *p = text.c_str();
    while (*p) {
        char *end = nullptr;
        unsigned long first = std::strtoul(p, &end, 10);
        if (end == p) return false;
        unsigned long last = first;
        p = end;
        if (*p == '-') {
            last = std::strtoul(p + 1, &end, 10);
            if (end == p + 1 || last < first) return false;
            p = end;
        }
        ranges.emplace_back(static_cast<uint32_t>(first), static_cast<uint32_t>(last));
        if (*p == ',') ++p;
        else if (*p) return false;
    }
    mRanges.insert(mRanges.end(), ranges.begin(), ranges.end());
    return true;
}

bool FrameSet::Contains(uint32_t frame) const
{
    for (const auto &r : mRanges)
        if (frame >= r.first && frame <= r.second) return true;
    return false;
}

namespace {

void AppendHex(std::string &out, uint64_t v)
{
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    out += buf;
}

// Labels are single words in the file.
std::string Word(std::string_view label)
{
    std::string w(label.empty() ? std::string_view("-") : label);
    for (char &c : w)
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = '_';
    return w;
}

} // namespace

void Recorder::RequestFull(uint32_t count, std::string_view label)
{
    std::lock_guard<std::mutex> guard(mLock);
    uint32_t first = mFrame();
    // A frame that has records already and isn't being kept can't be written in full any more: start at the next.
    if (mOpen && mCurrent >= first && !mFullFrame) first = mCurrent + 1;
    if (!count) count = 1;
    mFull.Add(first, first + count - 1);
    char buf[64];
    std::snprintf(buf, sizeof(buf), " %u %u\n", first, count);
    mSink("full " + Word(label) + buf);
}

void Recorder::Mark(std::string_view label)
{
    std::lock_guard<std::mutex> guard(mLock);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "mark %u ", mFrame());
    mSink(buf + Word(label) + "\n");
}

void Recorder::Note(std::string_view line)
{
    std::lock_guard<std::mutex> guard(mLock);
    std::string text(line);
    for (char &c : text)
        if (c == '\r' || c == '\n') c = ' ';
    mSink(text + "\n");
}

void Recorder::Advance(uint32_t frame)
{
    if (mOpen && frame <= mCurrent) return; // a late record from another thread joins the open frame
    if (mOpen) WriteFrame();
    mOpen = true;
    mCurrent = frame;
    mStreamCount = mAsyncCount = mDraws = mPresents = 0;
    mStreamHash = mAsyncSum = mAsyncSeq = 0;
    mFullFrame = mFull.Contains(frame);
    mLines.clear();
}

void Recorder::Record(Kind kind, std::string_view text, bool draw, bool present)
{
    const uint32_t thread = GetCurrentThreadId();
    std::lock_guard<std::mutex> guard(mLock);
    Advance(mFrame());
    std::string worker;
    if (mMainThread && thread != mMainThread) { // another thread's call: async, and marked
        kind = Kind::Async;
        worker = "w ";
        worker += text;
        text = worker;
    }
    const uint64_t h = Hash(text.data(), text.size());
    uint32_t index;
    if (kind == Kind::Stream) {
        index = mStreamCount++;
        mStreamHash = Mix(mStreamHash, h);
    } else {
        index = mAsyncCount++;
        mAsyncSum += h;
        mAsyncSeq = Mix(mAsyncSeq, h);
    }
    if (draw) ++mDraws;
    if (present) ++mPresents;
    if (mFullFrame) {
        char head[40];
        std::snprintf(head, sizeof(head), "%c %u %u ", kind == Kind::Stream ? 's' : 'a', mCurrent, index);
        mLines += head;
        mLines.append(text.data(), text.size());
        mLines += '\n';
    }
}

void Recorder::WriteFrame()
{
    std::string line;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "frame %u stream %u ", mCurrent, mStreamCount);
    line += buf;
    AppendHex(line, mStreamHash);
    std::snprintf(buf, sizeof(buf), " async %u ", mAsyncCount);
    line += buf;
    AppendHex(line, mAsyncSum);
    line += ' ';
    AppendHex(line, mAsyncSeq);
    std::snprintf(buf, sizeof(buf), " draws %u presents %u\n", mDraws, mPresents);
    line += buf;
    mSink(line);
    if (!mLines.empty()) mSink(mLines);
    mLines.clear();
    mOpen = false;
}

void Recorder::Finish()
{
    std::lock_guard<std::mutex> guard(mLock);
    if (mOpen) WriteFrame();
}

} // namespace devlog
