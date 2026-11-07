#pragma once
// Stand-ins patched over code for the scope of a test (docs/lane/test.md, "Stand-ins over code").
//
// CodePatch: overwrites a function's first five bytes with `jmp stand` and restores them when it goes out of scope.
// StandIn: the same stand-in over a callee on both sides at once: the image's function at an RVA and our replacement of it
// (a function, or one of our members through shape::MemberCode). When a callee of the function under test becomes ours,
// the caller's replacement calls ours instead of the image's, so a test that stood in for the image's alone would let ours
// run for real on one side; a StandIn keeps both sides isolated alike. The stand-in is called with the callee's own
// arguments (thiscall as __fastcall with a dummy edx, as usual).
//
//   shape::StandIn tutorial(0x4a27a0, &cTSNeighborhoodManager::AddTutorial, &StandTutorial);
//   shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);   // a static member: a plain function pointer
//
// Patches are applied in the constructor's order and removed in reverse.
#include "shape.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <windows.h>

namespace shape {

struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, const void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xe9;
        std::memcpy(site + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    // Over the image's function at `rva`.
    CodePatch(uint32_t rva, const void *stand) : CodePatch(GameAddress<void>(rva), stand) {}
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch()
    {
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
};

struct StandIn {
    CodePatch image, ours;
    // `replacement`: our function or non-virtual member that replaces the image's at `rva`, or a code address (a virtual
    // member's body from shape::VirtualMemberCode); `stand`: a function pointer.
    template <class Ours, class Stand>
    StandIn(uint32_t rva, Ours replacement, Stand stand) : image(rva, CodeOf(stand)), ours(CodeOf(replacement), CodeOf(stand))
    {
    }
    StandIn(const StandIn &) = delete;
    StandIn &operator=(const StandIn &) = delete;
};

// DeleteRecorder: operator delete patched to one recorder on both sides, for deleting-destructor tests (lane FT; it
// replaces the per-test RecordDelete stand-ins over 0x824624, which saw the original's free but left ours to an
// AllocationCounter). While one is alive, the image's operator delete (0x82540f) and sized delete (0x824624) and our own
// ::operator delete (both forms, which our deleting destructors call) jump to it. Between Begin() and End(), on the
// thread that made it, each non-null delete is recorded (the block, the size passed: 0 for the unsized form, and
// whether it was the image's delete or ours) and the block is kept, not freed, so the test can compare both sides'
// records and still read the block; Release() frees the kept blocks (std::free: blocks from operator new, the test's or
// ours) and clears the record. Outside the window, and on other threads, a delete frees as usual. Keep the window
// around the call alone: the harness's own deletes inside it are recorded too. Never allocates; one at a time.
//
//   shape::DeleteRecorder deletes;
//   deletes.Begin(); original(block, 1); deletes.End();
//   if (!deletes.Is(0, block, 0x14)) ...                  // one delete, of this block, sized 0x14
//   deletes.Release();
struct DeleteRecorder {
    struct Call {
        void *block;
        uint32_t size; // as passed to the sized form; 0 for the unsized one
        bool image;    // the image's delete (0x824624 / 0x82540f), not ours
    };
    static constexpr int kMax = 32;

    DeleteRecorder()
        : imageSized(0x824624u, reinterpret_cast<const void *>(&ImageSized)),
          imageUnsized(0x82540fu, reinterpret_cast<const void *>(&ImageUnsized)),
          oursSized(reinterpret_cast<void *>(static_cast<void (*)(void *, size_t)>(&::operator delete)),
                    reinterpret_cast<const void *>(&OursSized)),
          oursUnsized(reinterpret_cast<void *>(static_cast<void (*)(void *)>(&::operator delete)),
                      reinterpret_cast<const void *>(&OursUnsized))
    {
        State &s = St();
        if (s.alive) std::abort(); // one at a time
        s = State{};
        s.alive = true;
        s.thread = GetCurrentThreadId();
    }
    ~DeleteRecorder()
    {
        St().recording = false;
        Release();
        St().alive = false;
    }
    DeleteRecorder(const DeleteRecorder &) = delete;
    DeleteRecorder &operator=(const DeleteRecorder &) = delete;

    void Begin() { St().recording = true; }
    void End() { St().recording = false; }
    int Count() const { return St().count; }     // deletes recorded (more than kMax: Overflowed)
    bool Overflowed() const { return St().overflow; }
    const Call &At(int i) const { return St().calls[i]; }
    // The i-th delete was of `block`, sized `size` (0: the unsized form), whichever side's delete it was.
    bool Is(int i, const void *block, uint32_t size) const
    {
        return i < Count() && At(i).block == block && At(i).size == size;
    }
    bool Deleted(const void *block) const
    {
        for (int i = 0; i < Count(); ++i)
            if (At(i).block == block) return true;
        return false;
    }
    int FromImage() const // how many went through the image's delete
    {
        int n = 0;
        for (int i = 0; i < Count(); ++i) n += At(i).image;
        return n;
    }
    // Frees the kept blocks (each once) and clears the record.
    void Release()
    {
        State &s = St();
        for (int i = 0; i < s.count; ++i) {
            bool seen = false;
            for (int j = 0; j < i; ++j) seen |= s.calls[j].block == s.calls[i].block;
            if (!seen) std::free(s.calls[i].block);
        }
        s.count = 0;
        s.overflow = false;
    }

private:
    struct State {
        bool alive = false, recording = false, overflow = false;
        DWORD thread = 0;
        int count = 0;
        Call calls[kMax] = {};
    };
    static State &St()
    {
        static State s;
        return s;
    }
    static void Take(void *block, size_t size, bool image)
    {
        if (!block) return;
        State &s = St();
        if (s.recording && GetCurrentThreadId() == s.thread) {
            if (s.count < kMax) {
                s.calls[s.count++] = Call{block, uint32_t(size), image};
                return; // kept until Release
            }
            s.overflow = true;
        }
        std::free(block);
    }
    static void __cdecl ImageSized(void *block, size_t size) { Take(block, size, true); }
    static void __cdecl ImageUnsized(void *block) { Take(block, 0, true); }
    static void __cdecl OursSized(void *block, size_t size) { Take(block, size, false); }
    static void __cdecl OursUnsized(void *block) { Take(block, 0, false); }

    CodePatch imageSized, imageUnsized, oursSized, oursUnsized;
};

} // namespace shape
