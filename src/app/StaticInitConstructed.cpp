// Owned XC entries of the "object" kind (lane HD; src/app/StaticInitConstructed.h). Hosted, the objects stay in the
// image's storage, where game code reads them; each entry runs in its own slot (the host's skip list), builds the
// object with our code and registers our destructor thunk with BoundAtExit(), so the exit list keeps the original's
// LIFO order.
#include "app/StaticInitConstructed.h"

#include "app/ReportText.h"
#include "app/StaticInit.h"
#include "game/game.h"
#include "misc/StartupObjects.h"
#include "rz/RZMemory.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZFileName.h"
#include "rz/cRZStopwatch.h"
#include "std/StdInstances.h"

#include <array>
#include <iterator>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace staticinit {

namespace {

constexpr ConstructedKind NV = ConstructedKind::NameValues, F = ConstructedKind::Floats, D = ConstructedKind::Date,
                          FN = ConstructedKind::FileName, B = ConstructedKind::Flag, SW = ConstructedKind::Stopwatch,
                          CT = ConstructedKind::CrcTables, V20 = ConstructedKind::Vector20,
                          P = ConstructedKind::FixedPool, LL = ConstructedKind::ListLock;

// Slot order. Entry, object and thunk from db/initializers.jsonl (the "object" entries); the arguments, entry arrays
// and float values from each entry's code (pushes, stores, the 16 bytes at a movaps source); the tests check each
// against the image's entry and thunk.
constexpr ConstructedStatic kConstructed[] = {
    {96, 0x1382f7, 0x10cafdc, 0, D, 0},            // cRZDate(0, 0): the epoch (read by cRZTime)
    {97, 0x1382e5, 0x10cafe4, 0, D, 0xc22d},
    {110, 0x1384a5, 0x10cb7e4, 0xd90787, LL, 0},   // network-web
    {132, 0x138850, 0x10cc848, 0xd90da0, SW, 4},
    {197, 0x139790, 0x10d9e00, 0xd91f50, FN, 0},
    {208, 0x13a7a0, 0x10d9ff8, 0xd92430, NV, 0xdbd4c0}, // the light type names (6); thunk inlined
    {212, 0x13a7e0, 0x10da238, 0xd92580, V20, 0},
    // 289-316: vectors of three or four floats (axes, colours), slot 297 is atexit-only
    {289, 0x13c910, 0x10dbdb4, 0, F, 3, {0.0f, 0.0f, 0.0f}},
    {290, 0x13c820, 0x10dbdc0, 0, F, 3, {1.0f, 0.0f, 0.0f}},
    {291, 0x13c870, 0x10dbdcc, 0, F, 3, {0.0f, 1.0f, 0.0f}},
    {292, 0x13c8c0, 0x10dbdd8, 0, F, 3, {0.0f, 0.0f, 1.0f}},
    {293, 0x13c850, 0x10dbde4, 0, F, 4, {1.0f, 0.0f, 0.0f, 0.0f}}, // from .rdata 0xda1ec0
    {294, 0x13c8a0, 0x10dbdf4, 0, F, 4, {0.0f, 1.0f, 0.0f, 0.0f}}, // 0xdbb710
    {295, 0x13c8f0, 0x10dbe04, 0, F, 4, {0.0f, 0.0f, 1.0f, 0.0f}}, // 0xdba2c0
    {296, 0x13c7b0, 0x10dbe14, 0, F, 4, {0.0f, 0.0f, 0.0f, 1.0f}}, // 0xda8660
    {298, 0x13c560, 0x10dbe24, 0, F, 4, {0.0f, 0.0f, 0.0f, 0.0f}}, // xorps
    {299, 0x13c580, 0x10dbe34, 0, F, 4, {1.0f, 0.0f, 0.0f, 0.0f}}, // 0xda1ec0
    {300, 0x13c5a0, 0x10dbe44, 0, F, 4, {0.0f, 1.0f, 1.0f, 1.0f}}, // 0xdba2d0
    {301, 0x13c5c0, 0x10dbe54, 0, F, 4, {1.0f, 1.0f, 1.0f, 1.0f}}, // 0xdb09d0
    {302, 0x13c5e0, 0x10dbe64, 0, F, 3, {0.0f, 0.0f, 0.0f}},
    {303, 0x13c730, 0x10dbe70, 0, F, 3, {1.0f, 1.0f, 1.0f}},
    {304, 0x13c700, 0x10dbe7c, 0, F, 3, {1.0f, 0.0f, 0.0f}},
    {305, 0x13c670, 0x10dbe88, 0, F, 3, {0.0f, 1.0f, 0.0f}},
    {306, 0x13c610, 0x10dbe94, 0, F, 3, {0.0f, 0.0f, 1.0f}},
    {307, 0x13c640, 0x10dbea0, 0, F, 3, {0.0f, 1.0f, 1.0f}},
    {308, 0x13c6d0, 0x10dbeac, 0, F, 3, {1.0f, 0.0f, 1.0f}},
    {309, 0x13c760, 0x10dbeb8, 0, F, 3, {1.0f, 1.0f, 0.0f}},
    {310, 0x13c6a0, 0x10dbec4, 0, F, 3, {1.0f, 0.5f, 0.0f}},
    {311, 0x13ca00, 0x10dbed8, 0, F, 3, {0.0f, 1.0f, 0.0f}},
    {312, 0x13ca30, 0x10dbee4, 0, F, 3, {1.0f, 0.0f, 0.0f}},
    {313, 0x13c9a0, 0x10dbef0, 0, F, 3, {0.0f, 1.0f, 0.0f}},
    {314, 0x13c9d0, 0x10dbefc, 0, F, 3, {0.0f, 0.0f, 1.0f}},
    {315, 0x13c940, 0x10dbf08, 0, F, 3, {0.0f, -1.0f, 0.0f}},
    {316, 0x13c970, 0x10dbf14, 0, F, 3, {0.0f, 0.0f, 1.0f}},
    {497, 0x13dfd0, 0x10df4e0, 0xd94ee0, NV, 0xe04450}, // 17 names; thunk inlined
    {507, 0x13e1d9, 0x10ecd01, 0xd951b1, B, 0},
    {508, 0x13e1ef, 0x10f4d10, 0xd951bb, B, 0},
    {509, 0x13e21d, 0x106220c, 0xd951e3, P, 0x24}, // 509-511, 637-638: the windowing code's pools
    {510, 0x13e235, 0x106221c, 0xd951ed, P, 0x28},
    {511, 0x13e205, 0x106222c, 0xd951d9, P, 0x1c},
    {533, 0x13e46b, 0x10f59d1, 0, CT, 0},
    // 582-620: the command parser's name tables (thunk `jmp 0x3c6bf0`)
    {582, 0x13ed20, 0x10f69b4, 0xd96210, NV, 0xe78d88},
    {583, 0x13ed00, 0x10f69c0, 0xd96200, NV, 0xe78e08},
    {584, 0x13ed60, 0x10f69cc, 0xd96230, NV, 0xe78e40},
    {585, 0x13ec00, 0x10f69d8, 0xd96180, NV, 0xe78e60},
    {586, 0x13ec20, 0x10f69e4, 0xd96190, NV, 0xe78ed0},
    {587, 0x13ed40, 0x10f69f0, 0xd96220, NV, 0xe78ef0},
    {588, 0x13ea80, 0x10f69fc, 0xd960c0, NV, 0xe78620},
    {589, 0x13ece0, 0x10f6a08, 0xd961f0, NV, 0xe78640},
    {590, 0x13eca0, 0x10f6a14, 0xd961d0, NV, 0xe78660},
    {591, 0x13ef00, 0x10f6a20, 0xd96300, NV, 0xe786a0},
    {592, 0x13ef20, 0x10f6a2c, 0xd96310, NV, 0xe786d0},
    {593, 0x13eae0, 0x10f6a38, 0xd960f0, NV, 0xe786e8},
    {594, 0x13eb80, 0x10f6a44, 0xd96140, NV, 0xe78708},
    {595, 0x13ec40, 0x10f6a50, 0xd961a0, NV, 0xe78720},
    {596, 0x13eda0, 0x10f6a5c, 0xd96250, NV, 0xe78738},
    {597, 0x13ed80, 0x10f6a68, 0xd96240, NV, 0xe78750},
    {598, 0x13edc0, 0x10f6a74, 0xd96260, NV, 0xe787b0},
    {599, 0x13ea60, 0x10f6a80, 0xd960b0, NV, 0xe787d8},
    {600, 0x13eb20, 0x10f6a8c, 0xd96110, NV, 0xe78820},
    {601, 0x13eb00, 0x10f6a98, 0xd96100, NV, 0xe78868},
    {602, 0x13ec80, 0x10f6aa4, 0xd961c0, NV, 0xe78880},
    {603, 0x13ec60, 0x10f6ab0, 0xd961b0, NV, 0xe788c8},
    {604, 0x13ebc0, 0x10f6abc, 0xd96160, NV, 0xe78910},
    {605, 0x13ee20, 0x10f6ac8, 0xd96290, NV, 0xe78938},
    {606, 0x13eea0, 0x10f6ad4, 0xd962d0, NV, 0xe78960},
    {607, 0x13eec0, 0x10f6ae0, 0xd962e0, NV, 0xe78988},
    {608, 0x13eee0, 0x10f6aec, 0xd962f0, NV, 0xe789a8},
    {609, 0x13ee40, 0x10f6af8, 0xd962a0, NV, 0xe789c8},
    {610, 0x13ee60, 0x10f6b04, 0xd962b0, NV, 0xe78a00},
    {611, 0x13ee80, 0x10f6b10, 0xd962c0, NV, 0xe78a20},
    {612, 0x13eaa0, 0x10f6b1c, 0xd960d0, NV, 0xe78af0},
    {613, 0x13eac0, 0x10f6b28, 0xd960e0, NV, 0xe78c50},
    {614, 0x13ee00, 0x10f6b34, 0xd96280, NV, 0xe78c80},
    {615, 0x13ede0, 0x10f6b40, 0xd96270, NV, 0xe78ca8},
    {616, 0x13ecc0, 0x10f6b4c, 0xd961e0, NV, 0xe78cd0},
    {617, 0x13eba0, 0x10f6b58, 0xd96150, NV, 0xe78ce8},
    {618, 0x13eb60, 0x10f6b64, 0xd96130, NV, 0xe78d00},
    {619, 0x13eb40, 0x10f6b70, 0xd96120, NV, 0xe78d38},
    {620, 0x13ebe0, 0x10f6b7c, 0xd96170, NV, 0xe78d60},
    {630, 0x13f530, 0x10f9a30, 0xd96650, NV, 0xe7da90}, // thunk inlined
    {637, 0x13f60b, 0x1074dec, 0xd969a6, P, 0x8000},
    {638, 0x13f5f0, 0x1074dfc, 0xd9699c, P, 0x2000},
    {652, 0x13f760, 0x10fb00c, 0xd96bf0, NV, 0xe840d0},
    {653, 0x13f780, 0x10fb018, 0xd96c00, NV, 0xe84110},
    {680, 0x13ffd0, 0x10fc488, 0xd97540, FN, 0},
    {699, 0x140140, 0x10fc874, 0xd97880, NV, 0x107d588}, // thunk inlined; the array in .data
    {706, 0x1401f0, 0x10fd180, 0xd97be0, NV, 0x107dce0},
    {707, 0x1401d0, 0x10fd18c, 0xd97bd0, NV, 0xe8ea10},
    {708, 0x140210, 0x10fd198, 0xd97bf0, NV, 0xe8ea68},
    {710, 0x140240, 0x10fd1e8, 0xd97c60, NV, 0xe8f8a0},  // 710-721: thunks inlined
    {712, 0x140270, 0x10fd204, 0xd97db0, NV, 0x107e1e8},
    {717, 0x1402e0, 0x10fd268, 0xd98020, NV, 0x107e4f0},
    {719, 0x140310, 0x10fd284, 0xd98170, NV, 0x107e5d8},
    {721, 0x140340, 0x10fd2a0, 0xd982c0, NV, 0xe90aa8},
    {777, 0x140a60, 0x1107668, 0xd98d20, NV, 0xeb78d8},
    {778, 0x140a40, 0x1107674, 0xd98d10, NV, 0xeb7910},
};

// Slot 212's object, the fixed-size pools (509-511, 637, 638) and slot 110's object: src/misc/StartupObjects.h (their
// constructors 0xc0a664, 0x945bd6, 0x1ce3f3 are lane HQ's, hooked).
using startupobj::FixedPool;
using startupobj::ListAndLock;
using startupobj::PoolChunk;
using startupobj::ZeroedVector20;

// What the image's thunk does: the object's destructor, here ours.
void DestroyConstructed(const ConstructedStatic &e)
{
    switch (e.kind) {
    case ConstructedKind::NameValues: std::destroy_at(GameAddress<NameValueTable>(e.storage)); break;
    case ConstructedKind::FileName: GameAddress<cRZFileName>(e.storage)->Destruct(); break;
    case ConstructedKind::Vector20: std::destroy_at(&GameAddress<ZeroedVector20>(e.storage)->items); break;
    case ConstructedKind::FixedPool: {
        PoolChunk *&chunks = GameAddress<FixedPool>(e.storage)->chunks;
        while (chunks) {
            PoolChunk *chunk = chunks;
            chunks = chunk->next;
            RZMemory::Free(chunk);
        }
        break;
    }
    case ConstructedKind::ListLock: {
        ListAndLock *object = GameAddress<ListAndLock>(e.storage);
        object->lock.Destruct();
        std::destroy_at(&object->items);
        break;
    }
    default: break; // Flag, Stopwatch: an empty destructor (0x140fe0)
    }
}

// atexit takes no argument: one thunk per entry, by its index in the table.
template <size_t I> void __cdecl ConstructedThunk() { DestroyConstructed(kConstructed[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> ConstructedThunks(std::index_sequence<I...>) { return {&ConstructedThunk<I>...}; }
constexpr auto kConstructedThunks = ConstructedThunks(std::make_index_sequence<std::size(kConstructed)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kConstructed)> out{};
    size_t n = 0;
    for (const auto &e : kConstructed) out[n++] = {e.slot, e.imageRva, &InitConstructed, &e};
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < std::size(kConstructed); ++i) {
        if (kConstructed[i - 1].slot >= kConstructed[i].slot) return false;
    }
    return true;
}
static_assert(SortedBySlot(), "object XC entries: one per slot, in slot order");
static_assert(std::size(kConstructed) == 97);

} // namespace

size_t ConstructedObjectSize(const ConstructedStatic &e)
{
    switch (e.kind) {
    case ConstructedKind::NameValues: return sizeof(NameValueTable);
    case ConstructedKind::Floats: return 4 * e.arg;
    case ConstructedKind::Date: return sizeof(appbase::cRZDate);
    case ConstructedKind::FileName: return sizeof(cRZFileName);
    case ConstructedKind::Flag: return 1;
    case ConstructedKind::Stopwatch: return sizeof(cRZStopwatch);
    case ConstructedKind::CrcTables: return 1; // the object is empty; the tables are globals (the tests cover them)
    case ConstructedKind::Vector20: return sizeof(ZeroedVector20);
    case ConstructedKind::FixedPool: return sizeof(FixedPool);
    case ConstructedKind::ListLock: return sizeof(ListAndLock);
    }
    return 0;
}

void __cdecl InitConstructed(const void *constructedStatic)
{
    const auto &e = *static_cast<const ConstructedStatic *>(constructedStatic);
    switch (e.kind) {
    case ConstructedKind::NameValues:
        GameAddress<NameValueTable>(e.storage)->Construct(GameAddress<const NameValueEntry>(e.arg));
        break;
    case ConstructedKind::Floats: {
        GameAddress<StaticObjectStub>(e.storage)->Construct();
        float *values = GameAddress<float>(e.storage);
        for (uint32_t i = 0; i < e.arg; ++i) values[i] = e.value[i];
        break;
    }
    case ConstructedKind::Date: GameAddress<appbase::cRZDate>(e.storage)->ConstructDays(e.arg, 0); break;
    case ConstructedKind::FileName: GameAddress<cRZFileName>(e.storage)->ConstructEmpty(); break;
    case ConstructedKind::Flag: *GameAddress<uint8_t>(e.storage) = 1; break;
    case ConstructedKind::Stopwatch: GameAddress<cRZStopwatch>(e.storage)->Construct(static_cast<int32_t>(e.arg)); break;
    case ConstructedKind::CrcTables: GameAddress<StringKeyParser::CrcTables>(e.storage)->Initialize(); break;
    case ConstructedKind::Vector20: GameAddress<ZeroedVector20>(e.storage)->Construct(); break;
    case ConstructedKind::FixedPool: GameAddress<FixedPool>(e.storage)->Construct(e.arg); break;
    case ConstructedKind::ListLock: GameAddress<ListAndLock>(e.storage)->Construct(); break;
    }
    if (e.thunk) BoundAtExit()(kConstructedThunks[&e - kConstructed]);
}

const ConstructedStatic *ConstructedStatics(size_t *count)
{
    *count = std::size(kConstructed);
    return kConstructed;
}

const winmainstart::OwnedInitialiser *OwnedConstructed(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
