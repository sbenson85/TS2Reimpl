#pragma once
// A fake cIGZFile for the Gimex codec tests (tests/gz_gimex_bmp.cpp, gz_gimex_tga.cpp): slots 7 Length, 12 SeekToPosition,
// 15 ReadWithCount and 17 WriteWithCount over a byte vector; any other slot is a stray call (counted). One per side, the same
// bytes, then compared (the bytes written, the position, the strays). Also the little helpers the generators share.
#include "gz/Gimex.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace gimexstream {

using Bytes = std::vector<uint8_t>;

struct FakeFile {
    void **vtable;
    Bytes data;
    uint32_t pos = 0;
    Bytes written;
    int64_t writeLimit = -1; // the bytes it accepts; a write that would pass it takes nothing (count 0)
    int stray = 0;
};

constexpr int kSlots = 64;
inline void *gVTable[kSlots];

inline uint32_t __fastcall FLength(FakeFile *f, void *)
{
    return static_cast<uint32_t>(f->data.size());
}
inline uint32_t __fastcall FSeek(FakeFile *f, void *, uint32_t position)
{
    if (position > f->data.size()) return 0xffffffffu;
    f->pos = position;
    return position;
}
inline uint8_t __fastcall FRead(FakeFile *f, void *, void *dst, uint32_t *length)
{
    const uint32_t left = static_cast<uint32_t>(f->data.size()) - f->pos;
    const uint32_t n = *length < left ? *length : left;
    if (n != 0) std::memcpy(dst, f->data.data() + f->pos, n);
    f->pos += n;
    *length = n;
    return n != 0;
}
inline uint8_t __fastcall FWrite(FakeFile *f, void *, const void *src, uint32_t *length)
{
    if (f->writeLimit >= 0 && static_cast<int64_t>(f->written.size()) + *length > f->writeLimit) {
        *length = 0;
        return 0;
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(src);
    f->written.insert(f->written.end(), bytes, bytes + *length);
    return 1;
}
inline uint32_t __fastcall FStray(FakeFile *f, void *)
{
    ++f->stray;
    return 0;
}

inline void InitVTable()
{
    for (void *&slot : gVTable) slot = reinterpret_cast<void *>(&FStray);
    gVTable[7] = reinterpret_cast<void *>(&FLength);
    gVTable[12] = reinterpret_cast<void *>(&FSeek);
    gVTable[15] = reinterpret_cast<void *>(&FRead);
    gVTable[17] = reinterpret_cast<void *>(&FWrite);
}

struct Stream {
    FakeFile file;
    explicit Stream(const Bytes &data)
    {
        InitVTable();
        file.vtable = gVTable;
        file.data = data;
    }
    ifaces::cIGZFile *Get() { return reinterpret_cast<ifaces::cIGZFile *>(&file); }
};

// A codec instance on a fake stream.
struct Side {
    Stream stream;
    gimex::Instance *instance = nullptr;
    explicit Side(const Bytes &data) : stream(data) {}
};

inline uint32_t Roll(std::mt19937 &r, uint32_t n)
{
    return r() % n;
}

inline void Put16(Bytes &b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
inline void Put32(Bytes &b, uint32_t v)
{
    Put16(b, v);
    Put16(b, v >> 16);
}
inline void Set32(Bytes &b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint32_t Le32(const Bytes &b, size_t at)
{
    return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<uint32_t>(b[at + 3]) << 24;
}
inline void RandomBytes(std::mt19937 &r, Bytes &b, size_t n)
{
    for (size_t i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(r()));
}
inline bool SameBytes(const void *a, const void *b, size_t n)
{
    return std::memcmp(a, b, n) == 0;
}

} // namespace gimexstream
