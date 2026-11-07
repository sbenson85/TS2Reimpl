#pragma once
// Recording fakes for the Gimex codecs' entry points (src/gz/Gimex.h), shared by the Gimex tests. Every distinct entry
// point of rows 1..6 but `about` is patched (shape::CodePatch) to a fake that logs its RVA and arguments and answers
// from a seeded script, so the original's table (0x105b990) and ours reach the same fakes. Open and wopen write the
// instance they answer to *out (when out is not null), as the codecs do.
#include "code_patch.h"
#include "gz/GimexFormats.h"
#include "shape.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace gimexfx {

struct Event {
    uint32_t entry; // the entry point's RVA
    uint32_t words[4];
    bool operator==(const Event &o) const { return entry == o.entry && std::memcmp(words, o.words, sizeof(words)) == 0; }
};
inline std::vector<Event> gEvents;
inline std::mt19937 gAnswers;
inline uint32_t gInfoBlock[8]; // what the fake info entries answer (a pointer the tests compare)

inline uint32_t Answer()
{
    // Values with high bits set and some with a zero low byte, so a caller's truth test is seen.
    switch (gAnswers() % 5) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 0x100;
    case 3: return 0xffffffff;
    default: return gAnswers() & 0xffff;
    }
}

template <uint32_t Rva> int32_t __stdcall Fake1(uint32_t a)
{
    gEvents.push_back({Rva, {a, 0, 0, 0}});
    return static_cast<int32_t>(Answer());
}
template <uint32_t Rva> uint32_t __stdcall Fake2(uint32_t a, uint32_t b)
{
    gEvents.push_back({Rva, {a, b, 0, 0}});
    return gAnswers() % 3 ? shape::P(gInfoBlock) : 0;
}
template <uint32_t Rva> int32_t __stdcall Fake4(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({Rva, {a, b, c, d}});
    return static_cast<int32_t>(Answer());
}
// open / wopen: the instance (a scripted word, sometimes null) to *out, then the answer.
template <uint32_t Rva> int32_t __stdcall FakeOpen(uint32_t out, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({Rva, {out, b, c, d}});
    const uint32_t instance = gAnswers() % 4 ? 0x7000 + (gAnswers() & 0xff0) : 0;
    if (out != 0) *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(out)) = instance;
    return static_cast<int32_t>(Answer());
}

template <class F> const void *Fn(F f)
{
    return reinterpret_cast<const void *>(f);
}

template <class F> void *Addr(F f)
{
    return reinterpret_cast<void *>(f);
}

// 0x87fe5d, 0x87ed09 and 0x87f3e4 are shared by several rows.
struct CodecFakes {
    shape::CodePatch p[20] = {
        {0x87ea4e, Fn(&Fake1<0x87ea4e>)},    // FSH is
        {0x87ea9f, Fn(&FakeOpen<0x87ea9f>)}, // FSH open
        {0x87dc98, Fn(&Fake2<0x87dc98>)},    // FSH info
        {0x87eb11, Fn(&Fake4<0x87eb11>)},    // FSH read
        {0x87dc79, Fn(&Fake1<0x87dc79>)},    // FSH close
        {0x87eb94, Fn(&FakeOpen<0x87eb94>)}, // FSH wopen
        {0x87ebf5, Fn(&Fake4<0x87ebf5>)},    // FSH write
        {0x87eb4b, Fn(&Fake1<0x87eb4b>)},    // FSH wclose
        {0x87ef8c, Fn(&Fake1<0x87ef8c>)},    // JPEG is
        {0x87efdb, Fn(&FakeOpen<0x87efdb>)}, // JPEG open
        {0x87ed18, Fn(&Fake2<0x87ed18>)},    // JPEG info
        {0x87f013, Fn(&Fake4<0x87f013>)},    // JPEG read
        {0x87ed09, Fn(&Fake1<0x87ed09>)},    // JPEG / PNG close and wclose
        {0x87f3e4, Fn(&FakeOpen<0x87f3e4>)}, // wopen of rows 2..6
        {0x87f413, Fn(&Fake4<0x87f413>)},    // JPEG write
        {0x87fb72, Fn(&Fake1<0x87fb72>)},    // IJL is
        {0x87fbcb, Fn(&FakeOpen<0x87fbcb>)}, // IJL open
        {0x87f9e0, Fn(&Fake2<0x87f9e0>)},    // IJL info
        {0x87fc52, Fn(&Fake4<0x87fc52>)},    // IJL read
        {0x87f9b2, Fn(&Fake1<0x87f9b2>)},    // IJL close
    };
    shape::CodePatch q[18] = {
        {0x87fe74, Fn(&Fake4<0x87fe74>)},    // IJL write
        {0x87fe5d, Fn(&Fake1<0x87fe5d>)},    // IJL wclose, BMP / TGA close and wclose
        {0x8803e9, Fn(&Fake1<0x8803e9>)},    // PNG is
        {0x880435, Fn(&FakeOpen<0x880435>)}, // PNG open
        {0x88019a, Fn(&Fake2<0x88019a>)},    // PNG info
        {0x880536, Fn(&Fake4<0x880536>)},    // PNG read
        {0x880168, Fn(&Fake1<0x880168>)},    // PNG close
        {0x8807bb, Fn(&Fake4<0x8807bb>)},    // PNG write
        {0x881730, Fn(&Fake1<0x881730>)},    // BMP is
        {0x88176a, Fn(&FakeOpen<0x88176a>)}, // BMP open
        {0x881316, Fn(&Fake2<0x881316>)},    // BMP info
        {0x8817a2, Fn(&Fake4<0x8817a2>)},    // BMP read
        {0x881d40, Fn(&Fake4<0x881d40>)},    // BMP write
        {0x882db5, Fn(&Fake1<0x882db5>)},    // TGA is
        {0x882e91, Fn(&FakeOpen<0x882e91>)}, // TGA open
        {0x882958, Fn(&Fake2<0x882958>)},    // TGA info
        {0x882ec9, Fn(&Fake4<0x882ec9>)},    // TGA read
        {0x8833ac, Fn(&Fake4<0x8833ac>)},    // TGA write
    };
    // The entry points that are ours (lanes MN, MO, MP): the table of ours reaches these, so they get the same fakes as
    // the image's code at the same RVAs.
    shape::CodePatch oursFsh[8] = {
        {Addr(&gimex::fsh::Probe), Fn(&Fake1<0x87ea4e>)},        // FSH is
        {Addr(&gimex::fsh::Open), Fn(&FakeOpen<0x87ea9f>)},      // FSH open
        {Addr(&gimex::fsh::GetInfo), Fn(&Fake2<0x87dc98>)},      // FSH info
        {Addr(&gimex::fsh::ReadFrame), Fn(&Fake4<0x87eb11>)},    // FSH read
        {Addr(&gimex::fsh::Close), Fn(&Fake1<0x87dc79>)},        // FSH close
        {Addr(&gimex::fsh::OpenWriter), Fn(&FakeOpen<0x87eb94>)}, // FSH wopen
        {Addr(&gimex::fsh::WriteFrame), Fn(&Fake4<0x87ebf5>)},   // FSH write
        {Addr(&gimex::fsh::CloseWriter), Fn(&Fake1<0x87eb4b>)},  // FSH wclose
    };
    shape::CodePatch ours[30] = {
        {Addr(&gimex::png::Probe), Fn(&Fake1<0x8803e9>)},     // PNG is
        {Addr(&gimex::png::Open), Fn(&FakeOpen<0x880435>)},   // PNG open
        {Addr(&gimex::png::GetInfo), Fn(&Fake2<0x88019a>)},   // PNG info
        {Addr(&gimex::png::ReadFrame), Fn(&Fake4<0x880536>)}, // PNG read
        {Addr(&gimex::png::Close), Fn(&Fake1<0x880168>)},     // PNG close
        {Addr(&gimex::png::WriteFrame), Fn(&Fake4<0x8807bb>)}, // PNG write
        {Addr(&gimex::jpeg::Probe), Fn(&Fake1<0x87ef8c>)},    // JPEG is
        {Addr(&gimex::jpeg::Open), Fn(&FakeOpen<0x87efdb>)},  // JPEG open
        {Addr(&gimex::jpeg::GetInfo), Fn(&Fake2<0x87ed18>)},  // JPEG info
        {Addr(&gimex::jpeg::ReadFrame), Fn(&Fake4<0x87f013>)}, // JPEG read
        {Addr(&gimex::jpeg::Close), Fn(&Fake1<0x87ed09>)},    // JPEG close and wclose, PNG wclose
        {Addr(&gimex::jpeg::WriteFrame), Fn(&Fake4<0x87f413>)}, // JPEG write
        {Addr(&gimex::OpenWriter), Fn(&FakeOpen<0x87f3e4>)}, // wopen of rows 2..6
        {Addr(&gimex::FreeInstance), Fn(&Fake1<0x87fe5d>)},  // BMP / TGA close and wclose, IJL wclose
        {Addr(&gimex::bmp::Probe), Fn(&Fake1<0x881730>)},    // BMP is
        {Addr(&gimex::bmp::Open), Fn(&FakeOpen<0x88176a>)},  // BMP open
        {Addr(&gimex::bmp::GetInfo), Fn(&Fake2<0x881316>)},  // BMP info
        {Addr(&gimex::bmp::ReadFrame), Fn(&Fake4<0x8817a2>)}, // BMP read
        {Addr(&gimex::bmp::WriteFrame), Fn(&Fake4<0x881d40>)}, // BMP write
        {Addr(&gimex::tga::Probe), Fn(&Fake1<0x882db5>)},     // TGA is
        {Addr(&gimex::tga::Open), Fn(&FakeOpen<0x882e91>)},   // TGA open
        {Addr(&gimex::tga::GetInfo), Fn(&Fake2<0x882958>)},   // TGA info
        {Addr(&gimex::tga::ReadFrame), Fn(&Fake4<0x882ec9>)}, // TGA read
        {Addr(&gimex::tga::WriteFrame), Fn(&Fake4<0x8833ac>)}, // TGA write
        {Addr(&gimex::ijl::Probe), Fn(&Fake1<0x87fb72>)},     // IJL is
        {Addr(&gimex::ijl::Open), Fn(&FakeOpen<0x87fbcb>)},   // IJL open
        {Addr(&gimex::ijl::GetInfo), Fn(&Fake2<0x87f9e0>)},   // IJL info
        {Addr(&gimex::ijl::Close), Fn(&Fake1<0x87f9b2>)},     // IJL close
        {Addr(&gimex::ijl::ReadFrame), Fn(&Fake4<0x87fc52>)}, // IJL read
        {Addr(&gimex::ijl::WriteFrame), Fn(&Fake4<0x87fe74>)}, // IJL write
    };
};

} // namespace gimexfx
