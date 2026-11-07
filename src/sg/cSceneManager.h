// nTSSG::cSceneManager (lane IR): the scene graph's scene manager (cTSSGSystem's manager set). One interface,
// cISceneManager (vtable 0xdb5344, 34 slots). 0x528 bytes (the deleting destructor 0x2bde20 frees that much); constructor
// 0x2bd480 and destructor 0x2bd900 are still the original's.
//
// It keeps a window of per-frame records (a std::deque of 8-byte entries at +0x10, numbered from mFirstFrame), ten
// named counters (cRZString + value), a vector of samples, two stopwatches and process-memory statistics.
#pragma once

#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nTSSG {

// One frame record (8 bytes; the deque at +0x10 keeps two per block).
struct cSceneFrame {
    uint32_t mValue; // slot 21
    uint8_t mFlag;   // slot 22
    uint8_t mPad[3];
};

// A named counter (0x20 bytes: the string's own reference-count word holds the value).
class cSceneManager {
public:
    // 0x2bfa90 slot 0 (ret 8): ids 1, 0x69512c64 and 0x69512c6d give the object (AddRef, slot 1); false otherwise or for
    // a null `out`.
    bool QueryInterface(uint32_t iid, void **out);
    // 0x2bfac0 slot 2: the count drops by one; at zero the object deletes itself (slot 28 (1)) and 0 is returned.
    uint32_t Release();
    // 0x2bff90 slot 16 (ret 4): mRef520 = `object` (AddRef the new one, Release the old; nothing when the same).
    void SetRef520(void *object);
    // 0x2be740 slot 20: the number of the last frame record (mFirstFrame + count - 1).
    uint32_t LastFrame();
    // 0x2be7f0 slot 21 (ret 4): frame `frame`'s value (no range check).
    uint32_t FrameValue(uint32_t frame);
    // 0x2be820 slot 22 (ret 4): frame `frame`'s flag (no range check).
    uint8_t FrameFlag(uint32_t frame);
    // 0x2be6f0 slot 23: every 30th call (a global counter, 0x10d92dc, at 0 first) mRef51C's slot 18 with mRef8; returns
    // mRef51C.
    void *Tick();
    // 0x2be6e0 slot 24: the second counter's name (its string's slot 5, ToChar).
    const char *Name1();
    // 0x2be6d0 slot 25: the eleventh counter's name (its string's slot 5, ToChar).
    const char *Name10();
    // 0x2c00c0 slot 26: mSamplesReset set, mSamples emptied (resize to 0; the buffer stays), mSampleSums cleared.
    void ResetSamples();
    // 0x2bde20 slot 28 (ret 4): the destructor (0x2bd900, the original's), then the memory freed when `flags` bit 0 is
    // set. Returns the object.
    cSceneManager *DeletingDestructor(uint32_t flags);

    const void *mVTable;          // 0x000 0xdb5344
    uint32_t mRefCount;           // 0x004
    void *mRef8;                  // 0x008 reference (slot 9 calls its slot 46; slot 23 hands it to mRef51C)
    void *mRefC;                  // 0x00c reference
    void *mFramesProxy;           // 0x010 std::deque<cSceneFrame>: container proxy
    cSceneFrame **mFramesMap;     // 0x014 block map (two records per block)
    uint32_t mFramesMapSize;      // 0x018 a power of two
    uint32_t mFramesOffset;       // 0x01c
    uint32_t mFramesCount;        // 0x020
    uint32_t mFirstFrame;         // 0x024 the number of the first record
    uint8_t mFlag28;              // 0x028 (slot 14, a body folded with other classes, sets it)
    uint8_t mPad29[7];
    cRZStopwatch mWatch30;        // 0x030 mode 3
    cRZString mCounters[11];      // 0x048 a name each; the value in the string's last word
    uint8_t mSamplesReset;        // 0x1a8 slot 26 sets it
    uint8_t mPad1A9[3];
    std::vector<double> mSamples; // 0x1ac
    uint32_t mSampleSums[2];      // 0x1b8 slot 26 clears them
    uint8_t mFields1C0[0x4e0 - 0x1c0];
    uint32_t mField4E0;           // 0x4e0
    uint32_t mField4E4;           // 0x4e4
    cRZStopwatch mWatch4E8;       // 0x4e8 mode 5, started at construction
    uint8_t mMemoryInfo[0x518 - 0x500]; // 0x500 process memory statistics (0x1baa57 builds them)
    uint32_t mField518;           // 0x518
    void *mRef51C;                // 0x51c reference (slot 23 calls its slot 18)
    void *mRef520;                // 0x520 reference (slot 16)
    uint32_t mField524;           // 0x524
};
static_assert(offsetof(cSceneManager, mFirstFrame) == 0x24);
static_assert(offsetof(cSceneManager, mCounters) == 0x48);
static_assert(offsetof(cSceneManager, mSamples) == 0x1ac);
static_assert(offsetof(cSceneManager, mWatch4E8) == 0x4e8);
static_assert(offsetof(cSceneManager, mRef51C) == 0x51c);
static_assert(sizeof(cSceneManager) == 0x528);

} // namespace nTSSG
