// Owned XC entries of the "atexit-only" kind (lane HC; src/app/StaticInitAtExit.h). Hosted, the objects stay in the
// image's storage, constant-initialised by the image's file, where game code reads them; each entry runs in its own
// slot (the host's skip list) and only registers our destructor thunk with BoundAtExit(), so the exit list keeps the
// original's LIFO order.
#include "app/StaticInitAtExit.h"

#include "app/StaticInit.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZMemory.h"

#include <windows.h>
#include <oleauto.h>

#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "oleaut32.lib")

namespace staticinit {

namespace {

constexpr AtExitKind S = AtExitKind::String, V4 = AtExitKind::Vector4, V8 = AtExitKind::Vector8,
                     P = AtExitKind::PoolChunks, R = AtExitKind::Release, RV = AtExitKind::ReleaseVirtualBase,
                     D = AtExitKind::Delete, C = AtExitKind::Variant;

// Slot order. Entry, object and thunk from db/initializers.jsonl (the atexit-only entries); the kind from the thunk's
// code (what it reads and calls); the tests check each against the image's entry and thunk.
constexpr AtExitStatic kAtExits[] = {
    {14, 0x136e60, 0xfeb668, 0xd8e740, P},
    {15, 0x136e80, 0xfeb678, 0xd8e820, P},
    {16, 0x136e70, 0xfeb688, 0xd8e7b0, P},
    {131, 0x138840, 0xff7f30, 0xd90d40, S},
    {133, 0x138870, 0xff8b8c, 0xd90de0, S},
    {138, 0x1388e0, 0xff9b1c, 0xd90e80, S},
    {148, 0x138c30, 0xffac18, 0xd91240, S},
    {165, 0x139270, 0xffb2d4, 0xd91930, S},
    {167, 0x139280, 0xffba1c, 0xd91990, S},
    {168, 0x139290, 0xffc7d8, 0xd91a40, P},
    {170, 0x1392d0, 0xffd6d0, 0xd91b20, S},
    {172, 0x1394a0, 0xffdbd0, 0xd91c30, S},
    {186, 0x1394b0, 0xffe3d8, 0xd91c90, S},
    {189, 0x139570, 0xfffbf0, 0xd91dc0, S},
    {192, 0x1395a0, 0x1002aa0, 0xd91e80, S},
    {194, 0x139780, 0x1008b30, 0xd91ef0, S},
    {195, 0x1397c0, 0x100a0f0, 0xd920e0, S},
    {205, 0x13a780, 0x100b2e0, 0xd92360, S},
    {206, 0x13a790, 0x100b4bc, 0xd923c0, S},
    {213, 0x13a910, 0x100e440, 0xd92720, S},
    {214, 0x13a920, 0x100eb08, 0xd92780, S},
    {216, 0x13a950, 0x1011b14, 0xd92880, S},
    {217, 0x13a960, 0x1012038, 0xd928e0, S},
    {225, 0x13ab10, 0x10dab28, 0xd92ce0, V4}, // the neighbourhood code's
    {227, 0x13ab50, 0x10139dc, 0xd92da0, S},
    {234, 0x13b5b0, 0x1014ddc, 0xd930f0, S},
    {287, 0x13c4e0, 0x10162bc, 0xd945b0, S},
    {297, 0x13c810, 0x1016a60, 0xd94690, S},
    {366, 0x13d0b4, 0x10dce84, 0xd9490d, R},  // the terrain paint tool's
    {367, 0x13d0c0, 0x10dcebc, 0xd9493b, R},
    {383, 0x13d2fb, 0x10de488, 0xd94c7b, R},
    {496, 0x13dff0, 0x104edcc, 0xd94fd0, S},
    {537, 0x13e4c0, 0x10f5b60, 0xd953e0, RV}, // 537-542: the animation code's
    {538, 0x13e4d0, 0x10f5b64, 0xd95430, RV},
    {539, 0x13e4e0, 0x10f5b68, 0xd95480, RV},
    {540, 0x13e4f0, 0x10f5b6c, 0xd954d0, RV},
    {541, 0x13e500, 0x10f5b74, 0xd95520, RV},
    {542, 0x13e510, 0x10f5b88, 0xd95570, RV},
    {543, 0x13e520, 0x10632b0, 0xd955c0, P},  // 543-559: the animation code's pools
    {544, 0x13e530, 0x10632c0, 0xd95630, P},
    {545, 0x13e540, 0x106431c, 0xd956a0, P},
    {546, 0x13e550, 0x106460c, 0xd95710, P},
    {547, 0x13e560, 0x10648fc, 0xd95780, P},
    {548, 0x13e570, 0x1064bf0, 0xd957f0, P},
    {549, 0x13e580, 0x1064ee0, 0xd95860, P},
    {550, 0x13e590, 0x10651d0, 0xd958d0, P},
    {555, 0x13e7a0, 0x106551c, 0xd95940, P},
    {556, 0x13e7b0, 0x10656f8, 0xd959b0, P},
    {557, 0x13e7c0, 0x1065ed8, 0xd95a20, P},
    {558, 0x13e7d0, 0x1067bd4, 0xd95a90, P},
    {559, 0x13e7e0, 0x1068268, 0xd95b00, P},
    {567, 0x13e8c0, 0x106b100, 0xd95d30, S},
    {577, 0x13e9f0, 0x106f174, 0xd96000, S},
    {581, 0x13ef40, 0x106ffa8, 0xd96320, S},
    {625, 0x13f500, 0x107479c, 0xd964d0, S},
    {626, 0x13f510, 0x107482c, 0xd96530, S},
    {627, 0x13f520, 0x10748bc, 0xd96590, S},
    {628, 0x13f560, 0x1074964, 0xd967a0, S},
    {629, 0x13f550, 0x107497c, 0xd96740, S},
    {631, 0x13f570, 0x1074a1c, 0xd96800, S},
    {633, 0x13f5a0, 0x1074b24, 0xd968e0, S},
    {635, 0x13f5ce, 0x10fac48, 0xd96978, R},  // 635-636: cGZSndEa's
    {636, 0x13f5c2, 0x10fac4c, 0xd96968, R},
    {647, 0x13f720, 0x1078bc4, 0xd96a70, S},
    {648, 0x13f730, 0x1078cec, 0xd96ad0, S},
    {649, 0x13f740, 0x1078d94, 0xd96b30, S},
    {650, 0x13f750, 0x1078fe4, 0xd96b90, S},
    {651, 0x13f7a0, 0x1079190, 0xd96c10, S},
    {674, 0x13ff70, 0x10796b8, 0xd973f0, S},
    {676, 0x13ffa0, 0x1079b04, 0xd974b0, S},
    {678, 0x140010, 0x107b69c, 0xd97550, S},
    {683, 0x140050, 0x107bc9c, 0xd97630, S},
    {684, 0x140060, 0x107c09c, 0xd97690, S},
    {685, 0x140070, 0x107cfd0, 0xd97700, P},
    {686, 0x140130, 0x107d3ac, 0xd97820, S},
    {700, 0x140180, 0x107d990, 0xd97a30, S},
    {701, 0x1401a0, 0x107d9b0, 0xd97b00, P},
    {702, 0x140190, 0x107d9c0, 0xd97a90, P},
    {704, 0x1401c0, 0x107db44, 0xd97b70, S},
    {705, 0x140230, 0x107ddb0, 0xd97c00, S},
    {709, 0x140260, 0x107e19c, 0xd97d50, S},
    {711, 0x140290, 0x107e294, 0xd97ea0, S},
    {714, 0x1402c0, 0x107e434, 0xd97f60, S},
    {715, 0x1402d0, 0x107e4cc, 0xd97fc0, S},
    {716, 0x140300, 0x107e57c, 0xd98110, S},
    {718, 0x140330, 0x107e684, 0xd98260, S},
    {720, 0x140360, 0x107e724, 0xd983b0, S},
    {723, 0x140390, 0x107e84c, 0xd98470, S},
    {724, 0x1403a0, 0x107e954, 0xd984d0, S},
    {725, 0x1403b0, 0x107e9e4, 0xd98530, S},
    {726, 0x1403c0, 0x107ea74, 0xd98590, S},
    {727, 0x1403d0, 0x107eb04, 0xd985f0, S},
    {728, 0x1403e0, 0x107eef8, 0xd98650, P},
    {730, 0x1404f0, 0x10fd408, 0xd98730, V8}, // the scenegraph's
    {731, 0x140500, 0x10fd414, 0xd98790, V8},
    {762, 0x140887, 0x10fdb3c, 0xd98904, D},  // the crash reporter's annotations (StartCrashReporter)
    {764, 0x1408c0, 0x1083278, 0xd989f0, C},
    {771, 0x1409e0, 0x1085fbc, 0xd98b60, S},
    {772, 0x140a10, 0x108604c, 0xd98bd0, S},
    {774, 0x140a20, 0x1086364, 0xd98c50, S},
    {775, 0x140a30, 0x1086c20, 0xd98cb0, S},
    {776, 0x140a80, 0x1086cac, 0xd98d30, S},
    {779, 0x140a90, 0x10874f8, 0xd98d90, S},
    {782, 0x140ac0, 0x1087958, 0xd98e10, S},
    {784, 0x140af0, 0x1087ba8, 0xd98ed0, S},
    {785, 0x140b00, 0x1088ff0, 0xd98f60, S},
    {793, 0x140e40, 0x10897ec, 0xd992c0, S},
    {795, 0x140e50, 0x108a1a0, 0xd99330, S},
    {796, 0x140e60, 0x108a684, 0xd99390, S},
};

// A chunk of a fixed-size pool: RZMemory blocks linked through their first word.
struct PoolChunk {
    PoolChunk *next;
};

// What a ReleaseVirtualBase reference points at: an object whose cIGZUnknown is a virtual base (its first word the
// vbtable pointer). Never constructed; the conversion to cIGZUnknown * is the compiler's vbtable adjustment.
struct VirtualUnknown : virtual cIGZUnknown {};

// What the image's thunk does: the object's destructor, here ours.
void DestroyAtExit(const AtExitStatic &e)
{
    switch (e.kind) {
    case AtExitKind::String: std::destroy_at(GameAddress<std::string>(e.storage)); break;
    case AtExitKind::Vector4: std::destroy_at(GameAddress<std::vector<uint32_t>>(e.storage)); break;
    case AtExitKind::Vector8: std::destroy_at(GameAddress<std::vector<uint64_t>>(e.storage)); break;
    case AtExitKind::PoolChunks: {
        PoolChunk *&chunks = *GameAddress<PoolChunk *>(e.storage);
        while (chunks) {
            PoolChunk *chunk = chunks;
            chunks = chunk->next;
            RZMemory::Free(chunk);
        }
        break;
    }
    case AtExitKind::Release:
        if (cIGZUnknown *object = *GameAddress<cIGZUnknown *>(e.storage)) object->Release();
        break;
    case AtExitKind::ReleaseVirtualBase:
        if (VirtualUnknown *object = *GameAddress<VirtualUnknown *>(e.storage)) static_cast<cIGZUnknown *>(object)->Release();
        break;
    case AtExitKind::Delete:
        if (void *object = *GameAddress<void *>(e.storage)) VirtualCall<void *(__thiscall *)(void *, uint32_t), 0>(object, 1u);
        break;
    case AtExitKind::Variant: VariantClear(GameAddress<VARIANT>(e.storage)); break;
    }
}

// atexit takes no argument: one thunk per entry, by its index in the table.
template <size_t I> void __cdecl AtExitThunk() { DestroyAtExit(kAtExits[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> AtExitThunks(std::index_sequence<I...>) { return {&AtExitThunk<I>...}; }
constexpr auto kAtExitThunks = AtExitThunks(std::make_index_sequence<std::size(kAtExits)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kAtExits)> out{};
    size_t n = 0;
    for (const auto &e : kAtExits) out[n++] = {e.slot, e.imageRva, &InitAtExit, &e};
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < std::size(kAtExits); ++i) {
        if (kAtExits[i - 1].slot >= kAtExits[i].slot) return false;
    }
    return true;
}
static_assert(SortedBySlot(), "atexit-only XC entries: one per slot, in slot order");
static_assert(std::size(kAtExits) == 109);
static_assert(sizeof(std::string) == 24 && sizeof(std::vector<uint32_t>) == 12 && sizeof(VARIANT) == 16);

} // namespace

size_t AtExitObjectSize(AtExitKind kind)
{
    switch (kind) {
    case AtExitKind::String: return sizeof(std::string);
    case AtExitKind::Vector4:
    case AtExitKind::Vector8: return sizeof(std::vector<uint32_t>);
    case AtExitKind::Variant: return sizeof(VARIANT);
    default: return 4;
    }
}

// The image's entry writes nothing: the object is the file's. Ours only registers our destructor in the slot.
void __cdecl InitAtExit(const void *atExitStatic)
{
    const auto &e = *static_cast<const AtExitStatic *>(atExitStatic);
    BoundAtExit()(kAtExitThunks[&e - kAtExits]);
}

const AtExitStatic *AtExitStatics(size_t *count)
{
    *count = std::size(kAtExits);
    return kAtExits;
}

const winmainstart::OwnedInitialiser *OwnedAtExits(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
