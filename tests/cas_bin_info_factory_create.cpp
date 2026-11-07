// Differential tests for nTSCASCore::cTSBinInfoFactory::CreateInstance2 0xb89520 (src/cas/cTSBinInfoFactory.cpp). Lane DY.
// (The uint32 property-set serializer's constructor / destructor test moved to tests/gz_property_set_uint32.cpp, lane EQ.)
//
// CreateInstance2 builds real CAS resources on both sides (their original constructors 0xb922d0 / 0xb86e30 /
// 0xb86ea0). For each record type (and unknown ones) and several iids the result, whether out was set, the face
// handed out (its first 0x40 bytes by their kinds, heap pointers as their blocks) and its reference count are compared; the face
// is then released, which destroys the resource on both sides.
#include "difftest.h"
#include "heap_poison.h"
#include "ts_services_fixture.h"

#include "cas/cTSBinInfoFactory.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPropertySetSerializer.h"

#include <cstring>
#include <windows.h>

namespace {

using svcfx::Blob;
using Factory = nTSCASCore::cTSBinInfoFactory;

template <typename R, typename... A> R Slot(void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(object, slot)(object, args...);
}

// The resources' first 0x40 bytes as logged (svcfx::DumpWords), by record type: 'p' pointer, 'd' data, 'h' the low half
// only (cTSBinInfo's constructor 0xb86e30 writes only the low halves of +0x1c and +0x24, word stores at 0xb86e3b /
// 0xb86e41: the high halves are whatever operator new returned).
constexpr char kOutfitKinds[] = "pppp-p-d d";
constexpr char kBinInfoKinds[] = "pd-----h -h-d";
constexpr char kFragmentKinds[] = "pd-dd";

} // namespace

DIFF_TEST(BinInfoFactory_CreateInstance2, 0xb89520)
{
    svcfx::AllocatorScope allocator;
    const uint32_t types[] = {Factory::kTypeOutfitData, Factory::kTypeBinInfo, Factory::kTypeSimFragment, 0, 0x12345678,
                              0xebfee342, Factory::kTypeSimFragment + 1};
    const uint32_t iids[] = {Factory::kIidOutfitData, Factory::kIidBinInfo, Factory::kIidSimFragment, 1, 0x456b8f1d,
                             0x12345678, 0};
    uint32_t created = 0;
    // The resources' constructors leave some bytes unwritten (e.g. the top byte of +0xc): under the poison scope they
    // hold the same pattern on both sides instead of whatever earlier tests left on the heap (the test failed in the
    // full suite only).
    heappoison::Scope poison(ctx, 0xcd);
    for (uint32_t type : types) {
        for (uint32_t iid : iids) {
            Blob result[2];
            for (int s = 0; s < 2; ++s) {
                svcfx::ResetAll(); // the side's heap blocks are registered from here
                alignas(8) uint8_t memory[sizeof(Factory)] = {};
                auto *factory = reinterpret_cast<Factory *>(memory);
                factory->mVTable = GameAddress<const void>(Factory::kVTable);
                factory->mUnknownVTable = GameAddress<const void>(Factory::kUnknownVTable);
                void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                const uint32_t r = s == 0
                    ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t)>(0xb89520)(
                          factory, type, iid, &out, 7, 8)
                    : factory->CreateInstance2(type, iid, &out, 7, 8);
                result[s] = {r & 0xff, svcfx::NormOut(svcfx::P(out), 0x1234)};
                if (r & 0xff) {
                    if (type == Factory::kTypeOutfitData) svcfx::DumpWords(result[s], out, 0x40, kOutfitKinds);
                    else if (type == Factory::kTypeBinInfo) svcfx::DumpWords(result[s], out, 0x40, kBinInfoKinds);
                    else svcfx::DumpWords(result[s], out, 0x40, kFragmentKinds);
                    result[s].push_back(Slot<uint32_t>(out, 1)); // AddRef: the count after the factory's own release
                    Slot<uint32_t>(out, 2);
                    Slot<uint32_t>(out, 2); // the last reference: the resource goes
                    if (s) ++created;
                }
            }
            svcfx::Same(ctx, result[0], result[1], "CreateInstance2", type ^ iid);
        }
    }
    if (created < 3) ctx.Fail("CreateInstance2: fewer resources made than record types");
}
