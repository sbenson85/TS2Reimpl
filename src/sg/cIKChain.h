#pragma once
#include <cstdint>

// nGZSceneDBAnim::cIKChainConstProxy: a cIIKChain-derived proxy at object +0 (primary vtable 0xe685a4, 51 slots).
// Slot N is at byte offset 4N; RVAs are 1.18.0.187. `this` is the proxy itself.
//
// Layout as these functions touch it (the object is larger; its size is not known yet):
//   +0x00  primary vtable
//   +0x04  pointer to a holder; the holder's dword at +0x48 is the base of EntryAddress
//   +0x08  word: the count EntryAddress scales by 28
//   +0x2e  dword compared by ContainsByOther and FindTagged (tag 0xfb5b3383)
//   +0x33  dword: a flag, nonzero to skip the shortcut in ContainsByOther and ContainsValue
//   +0x3d  dword compared by FindTagged (tag 0xe9c7f1ad)
//   +0x42  dword base of ElementAddress; elements are 43 (0x2b) bytes apart
//
// The element and count calls go through this object's vtable (slot 47 is the element count, slot 23 the element
// address) and the element's own vtable (slots 15, 28 and 29), in the same order as the original.
//
// The cIKChain secondary subobject (vtable 0xe6376c, 51 slots, at object +0x16c) has thunks. Each thunk computes
//   real = this - [this - 4]          (the vtordisp stored before the subobject)
// and runs its body with `real` as `this`; the body's fields are at real - K. The object is at real - 0x16c.
namespace nGZSceneDBAnim {

class cIKChainConstProxy {
public:
    static constexpr uint32_t kVTable = 0xe685a4; // primary vtable, 51 slots

    bool QueryInterface(uint32_t riid, void **ppv);     // slot 0, 0xa331a0: ret 8, al only
    uint32_t ElementAddress(uint32_t index);            // slot 23, 0xa33110: ret 4, index * 43 + [this + 0x42]
    uint32_t EntryAddress();                            // slot 25, 0xa33030: ret 0, [[this + 4] + 0x48] + 28 * word
    int32_t IndexOf(uint32_t value);                    // slot 46, 0xa33140: ret 4, index or -1
    bool FindTagged(uint32_t value, uint32_t tag);      // slot 48, 0xa332e0: ret 8
    bool AnyElementAccepts(uint32_t value);             // slot 49, 0xa33350: ret 4
    bool ContainsValue(uint32_t value);                 // slot 50, 0xa33090: ret 4
};

// nGZSceneDBAnim::cIKChain secondary subobject (vtable 0xe6376c). Each function is a thunk with the vtordisp
// adjustment described above; `this` is the subobject.
class cIKChainSubobject {
public:
    static constexpr uint32_t kVTable = 0xe6376c; // 51 slots

    uint32_t IncrementWord();                           // slot 1, thunk 0x9e7cb0: ret 0, real - 0x164 += 1, returns it
    uint32_t GetWord0C();                               // slot 25, thunk 0x9e7cce: ret 0, returns the dword at real - 0x160
};

} // namespace nGZSceneDBAnim
