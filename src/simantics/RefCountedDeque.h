#pragma once
#include <cstdint>
#include <deque>

// The game's std::deque of reference-counted object pointers (a person's interaction queues): each element is a
// pointer to a cIGZUnknown-style object (slot 1 AddRef, slot 2 Release) and the deque owns one reference per element.
// The layout is the toolchain's std::deque<void *> (the same MSVC 14.40 template: iterator proxy +0, block map +4, map
// size +8, offset of the first element +0xc, element count +0x10; four elements per 16-byte block), so the storage
// inside a game object can be used through this class directly. The class is never constructed over game memory: the
// game's code builds the queues, so only the operations the game calls on them are members here.
//
// Each member replaces one instantiation of the game's deque<RefPtr> code:
class RefCountedDeque {
public:
    // The element count (the game reads the dword at +0x10).
    uint32_t Size() const { return static_cast<uint32_t>(mItems.size()); }

    // 0x1d49e0 (__thiscall, ret 4): push_back of a copy of the holder `*itemRef`: grows the block map and allocates the
    // block when needed, stores the pointer, adds a reference to it (null is stored as it is) and counts it.
    // (The original adds the reference before it counts the element; nothing can see the difference.)
    void PushBack(void *const *itemRef);

    // 0x525380 (__thiscall): pop_front. Releases the front element (null is skipped) and then removes it; the offset
    // restarts at 0 when the deque becomes empty. Must not be called on an empty deque.
    void PopFront() noexcept;

    // 0x1d48f0 (__thiscall): releases every element from the back to the front, then frees all blocks and the block
    // map (map and map size end up 0, the deque is empty and reusable).
    void Clear() noexcept;

    // The address of element `index` (std::deque::at: the game's 0x524900 is the toolchain's instantiation, so it is not
    // hooked); an index at or past the end raises the deque's range error.
    void **At(uint32_t index);

    // The first element, without a range check (an empty deque reads the block the offset points at).
    void *Front() const { return mItems.front(); }

private:
    std::deque<void *> mItems;
};

// 0x5248f0 (__cdecl, never returns): the range error of the game's deque<T>::at, "invalid deque<T> subscript" (std::out_of_range).
[[noreturn]] void __cdecl ThrowDequeRangeError();

static_assert(sizeof(RefCountedDeque) == 0x14, "RefCountedDeque layout (iterator proxy, map, map size, offset, size)");
