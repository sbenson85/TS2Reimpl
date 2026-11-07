// Owned XC entries for the cIGZAny statics (lane GZ; src/app/StaticInitAny.h). Hosted, the objects stay in the
// image's storage, where game code reads them (the light-map manager, the effects manager, the pattern code, the
// common system's tick); each entry runs in its own slot (the host's skip list) and the destructors go to
// BoundAtExit() in that slot.
#include "app/StaticInitAny.h"

#include "app/StaticInit.h"
#include "game/game.h"
#include "gz/cGZAny.h"

#include <array>
#include <cstring>
#include <iterator>
#include <new>
#include <utility>

namespace staticinit {

namespace {

constexpr AnyKind B = AnyKind::Boolean, U = AnyKind::Uint32, F = AnyKind::Float32;

// Slot order. Entry, object and thunk from db/initializers.jsonl (the cIGZAny class-inline entries), the class from
// the vtable .data holds at the object, the value from the word after it; the tests check each against the image.
constexpr AnyStatic kAnys[] = {
    {171, 0x1392c0, 0xffd8e4, 0xd91b10, B, 1},          // the light-map manager's
    {174, 0x139360, 0xffde30, 0xd91bf0, B, 1},          // 174-184: 12-byte rows {an .rdata pointer, AnyBoolean}
    {175, 0x139370, 0xffde3c, 0xd91c00, B, 1},
    {176, 0x139310, 0xffde48, 0xd91ba0, B, 1},
    {177, 0x139320, 0xffde54, 0xd91bb0, B, 1},
    {178, 0x139350, 0xffde60, 0xd91be0, B, 0},
    {179, 0x139380, 0xffde6c, 0xd91c10, B, 0},
    {180, 0x139330, 0xffde78, 0xd91bc0, B, 0},
    {181, 0x139390, 0xffde84, 0xd91c20, B, 0},
    {182, 0x139300, 0xffde90, 0xd91b90, B, 0},
    {183, 0x1392f0, 0xffde9c, 0xd91b80, B, 0},
    {184, 0x139340, 0xffdea8, 0xd91bd0, B, 0},
    {190, 0x139560, 0xfffd40, 0xd91db0, B, 1},
    {326, 0x13caed, 0x101d7c4, 0xd94742, U, 1},
    {327, 0x13cae1, 0x101d7cc, 0xd94737, U, 2},
    {328, 0x13cad5, 0x101d7d4, 0xd9472c, U, 3},
    {337, 0x13ccd5, 0x102186c, 0xd94790, B, 1},
    {338, 0x13cc99, 0x1021874, 0xd94759, B, 0},
    {339, 0x13ccbd, 0x102187c, 0xd9477a, U, 1},
    {340, 0x13ccc9, 0x1021884, 0xd94785, U, 2},
    {341, 0x13cca5, 0x102188c, 0xd94764, U, 3},
    {342, 0x13ccb1, 0x1021894, 0xd9476f, U, 4},
    {687, 0x140100, 0x107d500, 0xd977f0, F, 0x3f800000}, // 687-697: 12-byte rows {pointer, value} the effects manager reads
    {688, 0x1400d0, 0x107d50c, 0xd977c0, F, 0x3f800000},
    {689, 0x1400f0, 0x107d518, 0xd977e0, F, 0x3f800000},
    {690, 0x1400a0, 0x107d524, 0xd97790, U, 10000},
    {691, 0x1400c0, 0x107d530, 0xd977b0, F, 0x3f666666}, // 0.9f
    {692, 0x1400e0, 0x107d53c, 0xd977d0, U, 0},
    {693, 0x140110, 0x107d548, 0xd97800, U, 3},
    {694, 0x140090, 0x107d554, 0xd97780, B, 1},
    {695, 0x1400b0, 0x107d560, 0xd977a0, F, 0},
    {696, 0x140120, 0x107d56c, 0xd97810, B, 1},
    {697, 0x140080, 0x107d578, 0xd97770, B, 1},
    {770, 0x1409d0, 0x1085ef0, 0xd98a90, U, 2},          // the pattern code's
    {780, 0x140aa0, 0x1087514, 0xd98df0, B, 1},          // the common system's
    {781, 0x140ab0, 0x108751c, 0xd98e00, B, 1},
};

// What the image's thunk does: the object's destructor (cIGZAny's vtable stored), here ours.
void DestroyAny(const AnyStatic &e)
{
    switch (e.kind) {
    case AnyKind::Boolean: GameAddress<gz::AnyBoolean>(e.storage)->gz::AnyBoolean::~AnyBoolean(); break;
    case AnyKind::Uint32: GameAddress<gz::AnyUint32>(e.storage)->gz::AnyUint32::~AnyUint32(); break;
    case AnyKind::Float32: GameAddress<gz::AnyFloat32>(e.storage)->gz::AnyFloat32::~AnyFloat32(); break;
    }
}

// atexit takes no argument: one thunk per entry, by its index in the table.
template <size_t I> void __cdecl AnyThunk() { DestroyAny(kAnys[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> AnyThunks(std::index_sequence<I...>) { return {&AnyThunk<I>...}; }
constexpr auto kAnyThunks = AnyThunks(std::make_index_sequence<std::size(kAnys)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kAnys)> out{};
    size_t n = 0;
    for (const auto &e : kAnys) out[n++] = {e.slot, e.imageRva, &InitAny, &e};
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < std::size(kAnys); ++i) {
        if (kAnys[i - 1].slot >= kAnys[i].slot) return false;
    }
    return true;
}
static_assert(SortedBySlot(), "cIGZAny XC entries: one per slot, in slot order");

} // namespace

void __cdecl InitAny(const void *anyStatic)
{
    const auto &e = *static_cast<const AnyStatic *>(anyStatic);
    void *storage = GameAddress<void>(e.storage);
    switch (e.kind) {
    case AnyKind::Boolean: ::new (storage) gz::AnyBoolean(static_cast<uint8_t>(e.bits)); break;
    case AnyKind::Uint32: ::new (storage) gz::AnyUint32(e.bits); break;
    case AnyKind::Float32: {
        float value;
        std::memcpy(&value, &e.bits, 4);
        ::new (storage) gz::AnyFloat32(value);
        break;
    }
    }
    BoundAtExit()(kAnyThunks[&e - kAnys]);
}

const AnyStatic *AnyStatics(size_t *count)
{
    *count = std::size(kAnys);
    return kAnys;
}

const winmainstart::OwnedInitialiser *OwnedAnys(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
