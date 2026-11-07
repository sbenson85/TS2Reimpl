#pragma once
// The pixel format exercise, written once and run against two implementations of the same objects:
//   * pixelformat_check.exe runs it on our manager, built from our own formats and vtables, with no
//     original image anywhere in the process;
//   * the difftest (tests/pixel_format_report.cpp) runs it on the original manager and its formats,
//     through their own vtables, and requires the result to equal the committed reference
//     (tests/fixtures/pixelformat_report.txt) that the standalone check compares against.
// It touches the objects only through their vtable slots (src/game/vtable.h VirtualSlot), the way the
// game does, so it needs neither our classes nor the image: just a manager pointer.
//
// The report is plain text, one fact per field, with no pointers or addresses in it (formats are named by
// their position in the id list). Bump kReportVersion when its layout changes; the reference file
// carries the version in its first line.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "game/vtable.h"

namespace pfreport {

constexpr int kReportVersion = 1;

// The format ids the manager registers, in the order of its table (D3DFORMAT values and FOURCCs).
inline constexpr uint32_t kIds[56] = {
    0x14,       0x15,       0x16,       0x17,       0x18,       0x19,       0x1a,       0x1b,       0x1c,       0x1d,       0x1e,       0x1f,       0x20,       0x21,
    0x22,       0x23,       0x24,       0x28,       0x29,       0x32,       0x33,       0x34,       0x3c,       0x3d,       0x3e,       0x3f,       0x40,       0x43,
    0x59565955, 0x47424752, 0x32595559, 0x42475247, 0x31545844, 0x32545844, 0x33545844, 0x34545844, 0x35545844, 0x46,       0x47,       0x49,       0x4b,       0x4d,
    0x4f,       0x50,       0x52,       0x53,       0x51,       0x6e,       0x3154454d, 0x6f,       0x70,       0x71,       0x72,       0x73,       0x74,       0x75,
};
constexpr int kIdCount = 56;

// Ids that are not registered: the manager answers its unknown format.
inline constexpr uint32_t kProbeIds[] = {0, 1, 0x13, 0x100, 0x7fffffff, 0xdeadbeef, 0xffffffff};

// Interface ids the formats and the manager are asked for.
inline constexpr uint32_t kFormatIids[] = {1,          2,          0xbac3896f, 0x4c96818d, 0x2b3f3457, 0x9ac38991, 0x2cb4c095,
                                           0xbac3f24f, 0x5ac389b6, 0x5b672736, 0x9ac3899e, 0xdeadbeef, 0};
inline constexpr uint32_t kManagerIids[] = {1, 2, 0xdac38a6f, 0xbac3896f, 0};

// Widths for GetRowBytes.
inline constexpr uint32_t kWidths[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 100, 255, 256, 1000, 1023, 1024, 4096, 65536};

// ARGB pack/unpack samples.
inline constexpr uint8_t kColors[][4] = {{0, 0, 0, 0},       {255, 255, 255, 255}, {1, 2, 3, 4},    {255, 0, 0, 255},     {0, 255, 0, 128},
                                          {0, 0, 255, 0},     {127, 128, 129, 130}, {64, 32, 16, 8}, {200, 100, 50, 25}};
inline constexpr uint32_t kPixels[] = {0, 1, 0xff, 0x1234, 0x12345678, 0x80000000, 0xffffffff, 0xf81f, 0x7e0};

// Interface ids of the format classes (slot 5, GetClassId).
constexpr uint32_t kClassBase = 0xbac3896f;
constexpr uint32_t kClassABGRFloat = 0x2b3f3457;
constexpr uint32_t kClassARGB = 0x9ac38991;
constexpr uint32_t kClassAlphaLuminance = 0x2cb4c095;
constexpr uint32_t kClassDXT = 0xbac3f24f;
constexpr uint32_t kClassDepthStencil = 0x5ac389b6;
constexpr uint32_t kClassUnknown = 0x5b672736;
constexpr uint32_t kClassYUV = 0x9ac3899e;

namespace detail {

using U0 = uint32_t(__thiscall *)(void *);
using U1 = uint32_t(__thiscall *)(void *, uint32_t);
using QiFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
using LessFn = uint32_t(__thiscall *)(void *, const void *, uint32_t);
using NameFn = const char *(__thiscall *)(void *);
using UnpackFn = uint8_t *(__thiscall *)(void *, uint8_t *, uint32_t);

inline uint32_t S0(void *o, int slot) { return VirtualSlot<U0>(o, slot)(o); }
inline uint32_t S1(void *o, int slot, uint32_t a) { return VirtualSlot<U1>(o, slot)(o, a); }

struct Writer {
    std::string text;
    template <typename... A> void Line(const char *fmt, A... args)
    {
        char buf[1024];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        text += buf;
        text += '\n';
    }
    template <typename... A> void Part(const char *fmt, A... args)
    {
        char buf[1024];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        text += buf;
    }
    void End() { text += '\n'; }
};

// 'S' success with the object itself, 'X' success with another pointer, '-' failure.
inline char QiResult(void *obj, uint32_t iid)
{
    void *out = reinterpret_cast<void *>(0x1234abcd);
    uint32_t ok = VirtualSlot<QiFn>(obj, 0)(obj, iid, &out) & 0xff;
    if (!ok) return out == reinterpret_cast<void *>(0x1234abcd) ? '-' : '?';
    return out == obj ? 'S' : 'X';
}

// What a lookup returned relative to the format itself.
inline const char *Relation(const void *found, const void *format)
{
    if (!found) return "null";
    return found == format ? "same" : "other";
}

} // namespace detail

// Runs the whole exercise on a manager (any object with the cIPixelFormatsManager vtable).
inline std::string BuildReport(void *manager)
{
    using namespace detail;
    Writer w;
    w.Line("pixelformat-report %d", kReportVersion);
    w.Line("# Facts about the nGZGraphic4 pixel formats, produced through the objects' vtables. Generated from");
    w.Line("# the original image by tests/pixel_format_report.cpp; checked by pixelformat_check.exe. Do not edit.");

    // The manager
    w.Part("manager qi:");
    for (uint32_t iid : kManagerIids) w.Part(" %08x=%c", iid, QiResult(manager, iid));
    w.End();
    w.Line("manager addref=%u release=%u", S0(manager, 1), S0(manager, 2));

    void *formats[kIdCount];
    void *unknown = reinterpret_cast<void *>(static_cast<uintptr_t>(S0(manager, 5)));
    for (int i = 0; i < kIdCount; ++i) formats[i] = reinterpret_cast<void *>(static_cast<uintptr_t>(S1(manager, 4, kIds[i])));
    auto indexOf = [&](const void *p) -> int {
        if (!p) return -1;
        if (p == unknown) return kIdCount;
        for (int i = 0; i < kIdCount; ++i) {
            if (formats[i] == p) return i;
        }
        return -2;
    };

    w.Part("probe ids:");
    for (uint32_t id : kProbeIds) {
        void *f = reinterpret_cast<void *>(static_cast<uintptr_t>(S1(manager, 4, id)));
        int idx = indexOf(f);
        if (idx == kIdCount) w.Part(" %08x=unknown", id);
        else w.Part(" %08x=#%d", id, idx);
    }
    w.End();

    auto describe = [&](const char *label, void *f) {
        uint32_t cls = S0(f, 5);
        w.Line("%s id=%08x name=%s class=%08x addref=%u release=%u", label, S0(f, 3), VirtualSlot<NameFn>(f, 4)(f), cls, S0(f, 1), S0(f, 2));
        w.Line("  bpp=%u slot7=%u slot8=%u", S0(f, 6), S0(f, 7), S0(f, 8));
        w.Part("  rowbytes:");
        for (uint32_t width : kWidths) w.Part(" %u=%u", width, S1(f, 9, width));
        w.End();
        w.Part("  qi:");
        for (uint32_t iid : kFormatIids) w.Part(" %08x=%c", iid, QiResult(f, iid));
        w.End();
        switch (cls) {
        case kClassABGRFloat:
            w.Line("  alpha=%u red=%u green=%u blue=%u or=%u", S0(f, 11), S0(f, 12), S0(f, 13), S0(f, 14), S0(f, 15));
            break;
        case kClassARGB: {
            w.Line("  alpha=%u red=%u green=%u blue=%u unused=%u", S0(f, 11), S0(f, 12), S0(f, 13), S0(f, 14), S0(f, 15));
            w.Part("  pack(b,g,r,a):");
            for (const auto &c : kColors) {
                uint8_t bgra[4] = {c[0], c[1], c[2], c[3]};
                w.Part(" %02x%02x%02x%02x=%08x", c[0], c[1], c[2], c[3], S1(f, 16, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bgra))));
            }
            w.End();
            w.Part("  unpack(b,g,r,a):");
            for (uint32_t pixel : kPixels) {
                uint8_t bgra[4] = {0xcd, 0xcd, 0xcd, 0xcd};
                VirtualSlot<UnpackFn>(f, 17)(f, bgra, pixel);
                w.Part(" %08x=%02x%02x%02x%02x", pixel, bgra[0], bgra[1], bgra[2], bgra[3]);
            }
            w.End();
            break;
        }
        case kClassAlphaLuminance:
            w.Line("  alpha=%u luminance=%u,%u,%u,%u", S0(f, 11), S0(f, 12), S0(f, 13), S0(f, 14), S0(f, 15));
            break;
        case kClassDXT:
            w.Line("  alpha=%u color=%u,%u,%u version=%u blockbytes=%u", S0(f, 11), S0(f, 12), S0(f, 13), S0(f, 14), S0(f, 15), S0(f, 16));
            break;
        case kClassDepthStencil:
            w.Line("  depth=%u float=%u stencil=%u", S0(f, 11), S0(f, 12) & 0xff, S0(f, 13));
            break;
        case kClassYUV:
            w.Line("  fourcc=%08x", S0(f, 11));
            break;
        default:
            break;
        }
    };

    for (int i = 0; i < kIdCount; ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "format #%d", i);
        describe(label, formats[i]);
        // The manager's lookup by parameters finds this very format (or says it can't).
        void *f = formats[i];
        uint32_t cls = S0(f, 5);
        void *found = nullptr;
        const char *kind = "none";
        using L5 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        using L4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
        using L3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
        using LP = uint32_t(__thiscall *)(void *, const uint8_t *);
        auto ptr = [](uint32_t v) { return reinterpret_cast<void *>(static_cast<uintptr_t>(v)); };
        switch (cls) {
        case kClassARGB:
            kind = "argb";
            found = ptr(VirtualSlot<L5>(manager, 6)(manager, S0(f, 6), S0(f, 11), S0(f, 12), S0(f, 13), S0(f, 14)));
            break;
        case kClassAlphaLuminance:
            kind = "alphaluminance";
            found = ptr(VirtualSlot<L3>(manager, 7)(manager, S0(f, 6), S0(f, 11), S0(f, 12)));
            break;
        case kClassABGRFloat:
            kind = "abgrfloat";
            found = ptr(VirtualSlot<L4>(manager, 8)(manager, S0(f, 11), S0(f, 14), S0(f, 13), S0(f, 12)));
            break;
        case kClassYUV: {
            kind = "yuv";
            uint32_t fourcc = S0(f, 11);
            uint8_t chars[4] = {uint8_t(fourcc), uint8_t(fourcc >> 8), uint8_t(fourcc >> 16), uint8_t(fourcc >> 24)};
            found = ptr(VirtualSlot<LP>(manager, 9)(manager, chars));
            break;
        }
        case kClassDXT:
            kind = "dxt";
            found = ptr(VirtualSlot<U1>(manager, 10)(manager, S0(f, 15)));
            break;
        case kClassDepthStencil:
            kind = "depthstencil";
            found = ptr(VirtualSlot<L4>(manager, 11)(manager, S0(f, 6), S0(f, 11), S0(f, 13), S0(f, 12) & 0xff));
            break;
        default:
            break;
        }
        w.Line("  lookup %s: %s", kind, std::string(kind) == "none" ? "-" : Relation(found, f));
    }
    describe("unknown format", unknown);

    // Ordering: less(i, j) for every pair, non-strict then strict.
    for (int strict = 0; strict < 2; ++strict) {
        w.Line("less %s (row i, column j: format #i < format #j)", strict ? "strict" : "non-strict");
        for (int i = 0; i < kIdCount; ++i) {
            std::string row;
            for (int j = 0; j < kIdCount; ++j) {
                uint32_t r = VirtualSlot<LessFn>(formats[i], 10)(formats[i], formats[j], strict ? 1 : 0) & 0xff;
                row += r ? '1' : '0';
            }
            w.Line("  %2d %s", i, row.c_str());
        }
    }
    w.Part("less vs unknown (non-strict, strict; unknown<#i then #i<unknown):");
    for (int i = 0; i < kIdCount; ++i) {
        uint32_t a = VirtualSlot<LessFn>(unknown, 10)(unknown, formats[i], 0) & 0xff;
        uint32_t b = VirtualSlot<LessFn>(formats[i], 10)(formats[i], unknown, 0) & 0xff;
        uint32_t c = VirtualSlot<LessFn>(unknown, 10)(unknown, formats[i], 1) & 0xff;
        uint32_t d = VirtualSlot<LessFn>(formats[i], 10)(formats[i], unknown, 1) & 0xff;
        w.Part(" %u%u%u%u", a ? 1 : 0, b ? 1 : 0, c ? 1 : 0, d ? 1 : 0);
    }
    w.End();
    w.Line("end");
    return w.text;
}

} // namespace pfreport
