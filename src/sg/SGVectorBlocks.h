#pragma once
// The element blocks of the shader's vectors (its passes, the descriptors cShaderInit builds), allocated and given back
// the way the game's std::allocator does: below 4 KB plain operator new and sized delete; from 4 KB on, padded by 0x23
// bytes and aligned to 32, the real pointer in the dword before the block (checked on the way back). One copy for
// cShader.cpp and cShaderInit.cpp (lane VH).
#include <corecrt.h>
#include <cstddef>
#include <cstdint>
#include <new>
#include <xutility>

namespace nGZSceneGraph::vectorblocks {

constexpr uint32_t kBigBlock = 0x1000; // from here on, the aligned layout
constexpr uint32_t kBigPad = 0x23;     // 32 bytes of alignment slack plus the dword holding the real pointer

// Nothing for no bytes; operator new below 4 KB; else padded and 32-byte aligned with the real pointer before it.
template <class T> T *Allocate(uint32_t bytes)
{
    if (bytes == 0) return nullptr;
    if (bytes < kBigBlock) return static_cast<T *>(::operator new(bytes));
    if (bytes + kBigPad <= bytes) std::_Xbad_alloc();
    void *const raw = ::operator new(bytes + kBigPad);
    if (!raw) _invalid_parameter_noinfo_noreturn(); // operator new does not return null; the library's check stays
    void **const block = reinterpret_cast<void **>((reinterpret_cast<uintptr_t>(raw) + kBigPad) & ~static_cast<uintptr_t>(0x1f));
    block[-1] = raw;
    return reinterpret_cast<T *>(block);
}

// The block from `first` with room up to `cap` given back: sized delete; a big block's real pointer read from the dword
// before it, and the library's check that it lies within the slack.
template <class T> void Free(T *first, T *cap)
{
    const int32_t count = int32_t((reinterpret_cast<intptr_t>(cap) - reinterpret_cast<intptr_t>(first)) / int32_t(sizeof(T)));
    size_t bytes = size_t(count) * sizeof(T);
    void *block = first;
    if (bytes >= kBigBlock) {
        bytes += kBigPad;
        block = reinterpret_cast<void **>(first)[-1];
        if (reinterpret_cast<uintptr_t>(first) - reinterpret_cast<uintptr_t>(block) - 4 > 0x1f) _invalid_parameter_noinfo_noreturn();
    }
    ::operator delete(block, bytes);
}

} // namespace nGZSceneGraph::vectorblocks
