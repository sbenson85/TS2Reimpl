#pragma once
#include <cstddef>
#include <cstdint>

// Non-inlined STL helpers of MSVC 14.40 as compiled into the game: std::basic_string<char>
// members, std::vector<uint32_t> and a vector of 28-byte elements that hold a string, and the
// memory-copy and fill helpers they use. Layouts come from the disassembly:
//   std::string (0x18 bytes): the 16-byte small buffer, or a pointer to the heap block once the
//     capacity is above 15, at +0; size at +0x10; capacity at +0x14. The heap block is
//     capacity + 1 bytes (NUL terminated).
//   std::vector (0xc bytes): first element at +0, end of the elements at +4, end of the storage at
//     +8. Storage comes from std::allocator, so blocks of 4 KB and over carry the MSVC alignment
//     header.

struct StdSeqString {
    union {
        char buf[16];
        char *ptr;
    } data;          // +0
    size_t size;     // +0x10
    size_t capacity; // +0x14

    // 0x1603f0: basic_string::append(const char *s, size_t count), __thiscall. Returns this (eax).
    StdSeqString *Append(const char *s, size_t count);

    const char *Data() const { return capacity > 15 ? data.ptr : data.buf; }
};
static_assert(sizeof(StdSeqString) == 0x18, "std::string layout");

// 0x146090: copies [first, last) to dest with memcpy and returns dest + the byte count.
// __cdecl, three stack arguments.
uint8_t *StdSeqCopyBytes(const uint8_t *first, const uint8_t *last, uint8_t *dest);

// 0x15b5e0 and 0x1d88e0 (same contract, a plain loop in the second): fills the uint32_t range
// [first, last) with *value (read through the pointer on every store). __cdecl, three stack arguments.
void StdSeqFillU32(uint32_t *first, uint32_t *last, const uint32_t *value);

struct StdSeqVectorU32 {
    uint32_t *first;  // +0
    uint32_t *last;   // +4
    uint32_t *capEnd; // +8

    // 0x15fdc0: vector<uint32_t>::assign(count, value), __thiscall, two stack arguments (ret 8).
    void Assign(uint32_t count, uint32_t value);
};
static_assert(sizeof(StdSeqVectorU32) == 0xc, "vector layout");

