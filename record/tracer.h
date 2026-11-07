#pragma once
// Instruction tracer for record and replay (docs/record-replay.md). Arms a hardware breakpoint on
// one function of the mapped image; each call that hits it (up to a limit) is single-stepped to its
// return, decoding every instruction's memory operands with Zydis, and recorded as a Capture: entry
// registers, the bytes the call read from outside its own stack frame (their values before the call
// first wrote them), the bytes it wrote, and the calls that left the image (stepped over, not traced).
// Single-threaded: the thread that arms the tracer is the only one traced.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace record {

// Capture data lives on a private heap: the tracer allocates while the traced code runs, and blocks
// from the process heap would reuse ones the original just freed (and fool leak checks).
void *PrivateAllocate(size_t bytes);
void PrivateFree(void *p);
template <typename T> struct PrivateAllocator {
    using value_type = T;
    PrivateAllocator() = default;
    template <typename U> PrivateAllocator(const PrivateAllocator<U> &) {}
    T *allocate(size_t n) { return static_cast<T *>(PrivateAllocate(n * sizeof(T))); }
    void deallocate(T *p, size_t) { PrivateFree(p); }
    template <typename U> bool operator==(const PrivateAllocator<U> &) const { return true; }
};
template <typename T> using Vector = std::vector<T, PrivateAllocator<T>>;

template <typename K, typename V> using Map = std::map<K, V, std::less<K>, PrivateAllocator<std::pair<const K, V>>>;

struct BoundaryCall {
    uint32_t target = 0;     // address called outside the image
    uint32_t returnAddr = 0; // in the image
    uint32_t esp = 0;        // at the call target (points at the return address)
    uint32_t eax = 0, ecx = 0, edx = 0; // on return
    uint32_t retEsp = 0;     // on return (above `esp` by 4 plus the arguments a callee-cleanup call pops)
    uint8_t fpu = 0;         // x87 stack on return: 0 unchanged, 1 one value pushed (st0 below), 2 other
    uint8_t st0[10] = {};
    // Bytes the call had seen (read or written, its own frame included) that the boundary changed,
    // with their values on return. Bytes the call first reads after the boundary are plain reads.
    Map<uint32_t, uint8_t> writes;
};

struct Capture {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0, ebp = 0, esp = 0, eflags = 0;
    uint16_t fpuControl = 0;
    uint32_t mxcsr = 0;
    uint32_t retEax = 0, retEdx = 0, retEsp = 0;
    Map<uint32_t, uint8_t> reads;  // first value of each byte read before the call wrote it
    Map<uint32_t, uint8_t> before; // value of each written byte before the call
    Map<uint32_t, uint8_t> writes; // final value of each written byte
    Map<uint32_t, uint8_t> tebWrites; // fs-relative writes (SEH chain), by TEB offset, final values
    Map<uint32_t, uint8_t> tebReads;  // fs-relative reads (TLS array pointer...), by TEB offset, first values
    // Image code bytes that differ from the image as first seen by SetImage (a test's patch, such as
    // a jump to a stub): instruction fetches aren't recorded as reads, so replay needs these.
    Map<uint32_t, uint8_t> code;
    Vector<BoundaryCall> boundaries;
    uint64_t steps = 0;
};

struct TraceStats {
    uint64_t captured = 0;   // calls traced to their return
    uint64_t abandoned = 0;  // traces that never returned normally (exception unwound past them)
    uint64_t steps = 0;
    uint64_t decodeErrors = 0;
    uint64_t tooLong = 0;    // traces stopped at the step limit
    Map<std::string, uint64_t> unhandled; // instructions whose memory access we can't size
};

// Image bounds: code outside [lo, hi) is a boundary (stepped over). The first call with these bounds
// also keeps a copy of the image's executable sections, the baseline for Capture::code.
void SetImage(uintptr_t lo, uintptr_t hi);
// Arms the tracer on `entry` for the calling thread: the next `limit` calls are traced. A call
// longer than `maxSteps` instructions is abandoned (counted in tooLong).
void Arm(uintptr_t entry, uint32_t limit, uint64_t maxSteps = 500000);
// Disarms; returns the captures made since Arm (and clears them).
Vector<Capture> Disarm();
const TraceStats &Stats();
void ResetStats();

} // namespace record
