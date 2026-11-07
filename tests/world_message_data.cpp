// Differential test for the world message data classes' QueryInterface slots (src/world/WorldMessageData.h).
// Each side gets an 8-byte object whose vtable is a copy with a recording AddRef in slot 1; the result (al), the
// out-parameter (untouched / this / anything else), the AddRef calls and the object's bytes are compared.
#include "difftest.h"
#include "world/WorldMessageData.h"

#include <cstring>
#include <random>

namespace {

int gAddRefs = 0;
void *gAddRefSelf = nullptr;

uint32_t __fastcall RecordingAddRef(void *self, void * /*edx*/)
{
    ++gAddRefs;
    gAddRefSelf = self;
    return 0x77;
}

void *gVTable[3] = {nullptr, reinterpret_cast<void *>(&RecordingAddRef), nullptr};

using OursFn = bool (*)(void *, uint32_t, void **);
template <class C> bool CallOurs(void *self, uint32_t riid, void **ppv)
{
    return static_cast<C *>(self)->QueryInterface(riid, ppv);
}

struct Entry {
    const char *name;
    uint32_t rva;
    uint32_t iid;
    OursFn ours;
};

#define ENTRY(C) {#C, nTSWorld::C::kQueryInterfaceRva, nTSWorld::C::kIid, &CallOurs<nTSWorld::C>}
const Entry kEntries[] = {
    ENTRY(cSupportChanged),       ENTRY(cRoofAddSpec),         ENTRY(cRoofRemoveSpec),
    ENTRY(cRoofVisibilityChange), ENTRY(cPoolSpec),            ENTRY(cFencePostAddSpec),
    ENTRY(cWallAddSpec),          ENTRY(cFloorElevationChange), ENTRY(cFloorPatternChange),
    ENTRY(cFloorVisibilityChange), ENTRY(cTerrainElevationChange), ENTRY(cTerrainRectElevationChange),
    ENTRY(cAwningUpdateData),     ENTRY(cWallPatternChange),   ENTRY(cWallRemoveSpec),
    ENTRY(cWaterElevationChange),
};
#undef ENTRY

constexpr uintptr_t kUntouched = 0x5a5a5a5a;

struct Outcome {
    uint8_t result;
    uint32_t out; // 0 untouched, 1 this, 2 other (raw value in raw)
    uintptr_t raw;
    int addRefs;
    bool addRefSelf;
    uint32_t object[2];
};

Outcome Run(difftest::Context &ctx, const Entry &e, bool original, uint32_t riid, bool nullPpv, uint32_t count)
{
    alignas(8) uint32_t object[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVTable)), count};
    void *out = reinterpret_cast<void *>(kUntouched);
    void **ppv = nullPpv ? nullptr : &out;
    gAddRefs = 0;
    gAddRefSelf = nullptr;
    uint8_t result;
    if (original) {
        // Garbage above al: the original returns a bool in al only.
        result = static_cast<uint8_t>(ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(e.rva)(object, riid, ppv));
    } else {
        result = e.ours(object, riid, ppv);
    }
    Outcome o{};
    o.result = result;
    o.raw = reinterpret_cast<uintptr_t>(out);
    o.out = o.raw == kUntouched ? 0 : (out == object ? 1 : 2);
    o.addRefs = gAddRefs;
    o.addRefSelf = gAddRefSelf == object;
    o.object[0] = object[0] == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVTable)) ? 0 : object[0];
    o.object[1] = object[1];
    return o;
}

} // namespace

DIFF_TEST(WorldMessageData_QueryInterface, 0xd11260)
{
    std::mt19937 rng(0x4847);
    for (const Entry &e : kEntries) {
        // Ids that reach every branch: the class's own, cIGZUnknown (1), 0, another class's id, near misses, random.
        for (int i = 0; i < 400; ++i) {
            uint32_t riid;
            switch (i % 8) {
            case 0: riid = e.iid; break;
            case 1: riid = 1; break;
            case 2: riid = 0; break;
            case 3: riid = kEntries[rng() % (sizeof(kEntries) / sizeof(kEntries[0]))].iid; break;
            case 4: riid = e.iid + 1; break;
            case 5: riid = e.iid - 1; break;
            case 6: riid = 2; break;
            default: riid = rng(); break;
            }
            bool nullPpv = (i / 8) % 2 == 1;
            // The class's own id with a null ppv writes through null on both sides (the original does not check).
            if (nullPpv && riid == e.iid) continue;
            uint32_t count = rng() % 4;
            Outcome a = Run(ctx, e, true, riid, nullPpv, count);
            Outcome b = Run(ctx, e, false, riid, nullPpv, count);
            ++ctx.cases;
            bool same = a.result == b.result && a.out == b.out && (a.out != 2 || a.raw == b.raw) &&
                        a.addRefs == b.addRefs && a.addRefSelf == b.addRefSelf && a.object[0] == b.object[0] &&
                        a.object[1] == b.object[1];
            if (!same)
                ctx.Fail("%s riid 0x%08x ppv %s: original al %u out %u (0x%x) addrefs %d self %d; ours al %u out %u (0x%x) addrefs %d self %d",
                         e.name, riid, nullPpv ? "null" : "set", a.result, a.out, static_cast<unsigned>(a.raw), a.addRefs,
                         a.addRefSelf, b.result, b.out, static_cast<unsigned>(b.raw), b.addRefs, b.addRefSelf);
        }
    }
}
