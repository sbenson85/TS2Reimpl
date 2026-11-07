// Owned XC entries for the fixed-size pool statics (lane HE; src/app/StaticInitAllocators.h). Hosted, the objects stay
// in the image's storage, where the pooled classes' operator new / delete reach them; each entry runs in its own slot
// (the host's skip list) and the destructors go to BoundAtExit() in that slot.
#include "app/StaticInitAllocators.h"

#include "app/StaticInit.h"
#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"

#include <array>
#include <iterator>
#include <new>
#include <utility>

namespace staticinit {

namespace {

template <class X> void __cdecl BuildAt(void *storage)
{
    ::new (storage) X;
}

template <class X> void __cdecl DestroyAt(void *storage)
{
    static_cast<X *>(storage)->~X();
}

// A row's class: block size, the game's vtable of the class, our constructor and destructor.
template <uint32_t N, uint32_t VTable> constexpr AllocatorStatic Fixed(uint32_t slot, uint32_t entry, uint32_t storage, uint32_t thunk)
{
    using X = rz::cRZFastFixedAllocator2<N>;
    return {slot, entry, storage, thunk, N, VTable, &BuildAt<X>, &DestroyAt<X>};
}
template <class T, uint32_t VTable> constexpr AllocatorStatic Pooled(uint32_t slot, uint32_t entry, uint32_t storage, uint32_t thunk)
{
    using X = rz::cRZFastAllocator<T>;
    return {slot, entry, storage, thunk, rz::FastAllocatorBlock<T>::kSize, VTable, &BuildAt<X>, &DestroyAt<X>};
}

// The game's vtables of cRZFastFixedAllocator2<size>.
constexpr uint32_t k0c = 0xd9f9bc, k10 = 0xd9f520, k14 = 0xd9d05c, k18 = 0xd9d578, k1c = 0xd9d564, k20 = 0xd9c2d4,
                   k28 = 0xd9ded4, k3c = 0xdd3fec, k54 = 0xe4f1fc, k58 = 0xd9e99c, k5c = 0xd9db38, k74 = 0xda75ec,
                   k98 = 0xe7588c, k9c = 0xe687ac, k19c = 0xd9dd48;
namespace pooled = rz::pooled;

// Slot order. Entry, object and thunk from db/initializers.jsonl (the allocator entries: class-inline and the
// `object` ones whose constructor is an allocator's), the class from the vtable the entry, its constructor or .data
// gives the object; the tests check each row against the image.
constexpr AllocatorStatic kAllocators[] = {
    Fixed<0x20, k20>(17, 0x136e90, 0xfeb8e0, 0xd8e8b0),
    Fixed<0x14, k14>(18, 0x136ef0, 0xfec088, 0xd8e970),
    Fixed<0x1c, k1c>(76, 0x137ee0, 0x10ca058, 0xd8ff90),
    Fixed<0x18, k18>(77, 0x137ec0, 0x10ca064, 0xd8ff30),
    Fixed<0x18, k18>(78, 0x137f20, 0x10ca070, 0xd90050),
    Fixed<0x1c, k1c>(79, 0x137f00, 0x10ca07c, 0xd8fff0),
    Fixed<0x5c, k5c>(80, 0x137fa0, 0xfec630, 0xd90110),
    Fixed<0x20, k20>(81, 0x137f40, 0xfec63c, 0xd900b0),
    Fixed<0x19c, k19c>(82, 0x138000, 0xfec898, 0xd90170),
    Fixed<0x1c, k1c>(83, 0x1380a0, 0x10ca160, 0xd90290),
    Fixed<0x28, k28>(84, 0x138060, 0x10ca16c, 0xd901d0),
    Fixed<0x1c, k1c>(85, 0x1380c0, 0x10ca178, 0xd902f0),
    Fixed<0x28, k28>(86, 0x138080, 0x10ca184, 0xd90230),
    Fixed<0x58, k58>(87, 0x1380e0, 0xfed220, 0xd903e0),
    Fixed<0x28, k28>(88, 0x138160, 0x10ca2fc, 0xd904b0),
    Fixed<0x28, k28>(89, 0x138140, 0x10ca308, 0xd90450),
    Fixed<0x10, k10>(90, 0x138180, 0xfed8f8, 0xd90510),
    Fixed<0x0c, k0c>(91, 0x1381e0, 0xfedb58, 0xd90570),
    Fixed<0x18, k18>(92, 0x138250, 0xfee35c, 0xd905d0),
    Fixed<0x74, k74>(112, 0x1384e0, 0x10cc1e8, 0xd90cc0),
    Pooled<pooled::cTSAudioEvent, 0xda815c>(113, 0x13850c, 0x10cc3e8, 0xd90cde),
    Pooled<pooled::cTSAudioSoundInstance, 0xdab4a0>(125, 0x138734, 0x10cc5dc, 0xd90d08),
    Pooled<pooled::cTSBehaviorNode, 0xdc8fa0>(219, 0x13a9a0, 0x1012908, 0xd92a20),
    Pooled<pooled::cTSWantInstance, 0xdd4000>(285, 0x13c440, 0x1015d58, 0xd944e0),
    Pooled<pooled::cTSTreeStackElem, 0xdd7674>(288, 0x13c4f0, 0x1016994, 0xd94610),
    Fixed<0x58, k58>(504, 0x13e100, 0x105f820, 0xd95140),
    Fixed<0x54, k54>(505, 0x13e160, 0x105f82c, 0xd95170),
    Fixed<0x9c, k9c>(560, 0x13e810, 0x10f5f54, 0xd95ba0),
    Fixed<0x9c, k9c>(561, 0x13e850, 0x10f5f60, 0xd95bc0),
    Fixed<0x9c, k9c>(562, 0x13e7f0, 0x10f5f6c, 0xd95b90),
    Fixed<0x9c, k9c>(563, 0x13e830, 0x10f5f78, 0xd95bb0),
    Fixed<0x10, k10>(565, 0x13e880, 0x10f6020, 0xd95cf0),
    Fixed<0x0c, k0c>(566, 0x13e8a0, 0x10f602c, 0xd95d00),
    Fixed<0x0c, k0c>(568, 0x13e8d0, 0x10f617c, 0xd95d90),
    Fixed<0x3c, k3c>(569, 0x13e8f0, 0x10f620c, 0xd95da0),
    Fixed<0x10, k10>(570, 0x13e910, 0x10f6218, 0xd95db0),
    Fixed<0x10, k10>(571, 0x13e930, 0x10f625c, 0xd95ef0),
    Fixed<0x0c, k0c>(572, 0x13e950, 0x10f62f0, 0xd95f70),
    Fixed<0x14, k14>(573, 0x13e970, 0x10f6340, 0xd95f90),
    Fixed<0x14, k14>(574, 0x13e990, 0x10f639c, 0xd95fb0),
    Fixed<0x98, k98>(575, 0x13e9b0, 0x10f6654, 0xd95fd0),
    Fixed<0x58, k58>(576, 0x13e9d0, 0x10f6704, 0xd95fe0),
    Fixed<0x10, k10>(578, 0x13ea00, 0x10f6818, 0xd96080),
    Fixed<0x0c, k0c>(579, 0x13ea20, 0x10f6824, 0xd96090),
    Fixed<0x0c, k0c>(580, 0x13ea40, 0x10f6830, 0xd960a0),
    Fixed<0x10, k10>(621, 0x13ef50, 0x10f8bd4, 0xd963a0),
    Fixed<0x10, k10>(622, 0x13ef70, 0x10f8c60, 0xd963c0),
    Pooled<pooled::cGZSndEventStd, 0xe813d8>(641, 0x13f648, 0x10fad94, 0xd969d9),
    Pooled<pooled::cGenericDataTableContainer, 0xe88294>(681, 0x140020, 0x10fc590, 0xd975b0),
    Pooled<pooled::XPrimParam, 0xe91974>(732, 0x14050c, 0x10fd4c4, 0xd987ec),
};

// atexit takes no argument: one thunk per entry, by its index in the table.
template <size_t I> void __cdecl AllocatorThunk()
{
    kAllocators[I].destroy(GameAddress<void>(kAllocators[I].storage));
}

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> AllocatorThunks(std::index_sequence<I...>)
{
    return {&AllocatorThunk<I>...};
}
constexpr auto kAllocatorThunks = AllocatorThunks(std::make_index_sequence<std::size(kAllocators)>());

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kAllocators)> out{};
    size_t n = 0;
    for (const auto &e : kAllocators) out[n++] = {e.slot, e.imageRva, &InitAllocator, &e};
    return out;
}();

constexpr bool SortedBySlot()
{
    for (size_t i = 1; i < std::size(kAllocators); ++i) {
        if (kAllocators[i - 1].slot >= kAllocators[i].slot) return false;
    }
    return true;
}
static_assert(SortedBySlot(), "allocator XC entries: one per slot, in slot order");

} // namespace

void __cdecl InitAllocator(const void *allocatorStatic)
{
    const auto &e = *static_cast<const AllocatorStatic *>(allocatorStatic);
    e.build(GameAddress<void>(e.storage));
    BoundAtExit()(kAllocatorThunks[&e - kAllocators]);
}

const AllocatorStatic *AllocatorStatics(size_t *count)
{
    *count = std::size(kAllocators);
    return kAllocators;
}

const winmainstart::OwnedInitialiser *OwnedAllocators(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
