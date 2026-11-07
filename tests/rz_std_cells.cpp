// cRZStd's owned table and cells (lane FA; src/rz/cRZStd.h, src/rz/cRZStdTable.cpp). In a hooked build every owned
// symbol must be the image's own cell, so the word cRZStd::Construct (0x1bf920) stores is the image's table on both sides
// and the slots read the image's debug level, lock, log group and import slots. The image-free program's own table must
// hold our three slots in the image's order.
#include "difftest.h"
#include "game/import_slots.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZStd.h"

#include <cstring>
#include <random>

namespace {

using CtorCRZStd = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

template <typename T> const void *Member(T pmf)
{
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

} // namespace

DIFF_TEST(cRZStd_OwnedTable, 0x1bf920)
{
    struct Cell {
        const void *ours;
        uint32_t rva;
        const char *what;
    };
    const Cell cells[] = {
        {&ts2_rva_da3e6c, 0xda3e6c, "vtable"},
        {&ts2_rva_10cb5d4, 0x10cb5d4, "debug level"},
        {&ts2_rva_10cb5cc, 0x10cb5cc, "message-box lock"},
        {ts2_rva_da3f88, 0xda3f88, "log group"},
        {&ts2_rva_d9a110, 0xd9a110, "OutputDebugStringA slot"},
        {&ts2_rva_d9a164, 0xd9a164, "GetEnvironmentVariableA slot"},
        {&ts2_rva_d9a16c, 0xd9a16c, "GetModuleFileNameA slot"},
        {&ts2_rva_d9a1bc, 0xd9a1bc, "GetCurrentProcessId slot"},
        {&ts2_rva_d9a1ec, 0xd9a1ec, "GetFullPathNameA slot"},
        {&ts2_rva_d9a568, 0xd9a568, "MessageBoxA slot"},
        {&ts2_rva_d9a5c0, 0xd9a5c0, "GetWindowThreadProcessId slot"},
        {&ts2_rva_d9a5c4, 0xd9a5c4, "IsWindow slot"},
    };
    for (const Cell &c : cells) {
        ++ctx.cases;
        if (c.ours != ctx.image.At(c.rva)) ctx.Fail("%s: ours at %p, the image's 0x%x at %p", c.what, c.ours, c.rva, ctx.image.At(c.rva));
    }
    ++ctx.cases;
    if (std::strcmp(ts2_rva_da3f88, "ASSERT") != 0) ctx.Fail("log group \"%s\"", ts2_rva_da3f88);

    // The word each side's constructor stores: the image's table, on both sides.
    auto orig = ctx.Original<CtorCRZStd>(0x1bf920);
    std::mt19937 gen(0x3e6c);
    for (int i = 0; i < 64; ++i) {
        uint32_t mode = gen(), first = gen(), second = gen();
        cRZStd a{}, b{};
        a.mVTable = b.mVTable = reinterpret_cast<void *>(static_cast<uintptr_t>(gen()));
        orig(&a, mode, first, second);
        b.Construct(mode, first, second);
        ++ctx.cases;
        if (a.mVTable != ctx.image.At(0xda3e6c) || b.mVTable != a.mVTable)
            ctx.Fail("mode 0x%x: original stores %p, ours %p (image table %p)", mode, a.mVTable, b.mVTable, ctx.image.At(0xda3e6c));
    }

    // Our table for image-free programs: the image's slot order, each slot our replacement.
    const void *expected[] = {reinterpret_cast<const void *>(&cRZStd::Printf), Member(&cRZStd::Assert),
                              reinterpret_cast<const void *>(&cRZStd::Report)};
    for (int i = 0; i < 3; ++i) {
        ++ctx.cases;
        if (ts2_own_vt_da3e6c[i] != expected[i]) ctx.Fail("own table slot %d: %p, expected %p", i, ts2_own_vt_da3e6c[i], expected[i]);
    }
}
