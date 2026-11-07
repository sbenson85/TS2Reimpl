#pragma once
#include <cstdint>

// nTSCASCore::cTSSimOutfitData: a Sim's outfit data in Create-a-Sim. Primary vtable 0xe8b25c (116 slots) at
// object +0; the cITSBinableResource subobject is at +4 and the cIGZSelfDescribing one at +8. Slot N is at byte
// offset 4N; RVAs are 1.18.0.187. `this` is the primary subobject.
//
// Layout as these functions touch it (the object is larger; its size is not known yet):
//   +0x00  primary vtable
//   +0x50  dword; SetDword50Notify stores it and calls slot 53 when it changes, Dword50OrOne reads it (0 -> 1)
//   +0x58  dword (float bits, written by StoreFloatBits58)
//   +0x6c  16-byte block (StoreBlock6C)
//   +0x7c  16-byte block (StoreBlock7C)
//   +0x8c  16-byte block (StoreBlock8C)
//   +0xe4  start of a sub-structure (Address0E4)
//   +0x134 dword (StoreDword134)
//   +0x138 start of a sub-structure (Address138)
//   +0x14c byte (StoreByte14C)
//   +0x150 dword bit set; FlagsContain, SetMaskBits150
//   +0x154 14 dwords, one per bit: table indexed by the lowest set bit (SlotForLowestBit), filled by FillSlots154
//
// LowWordIsZero calls slot 16 through this object's vtable (ax of its result), SetDword50Notify calls slot 53.
namespace nTSCASCore {

class cTSSimOutfitData {
public:
    static constexpr uint32_t kVTable = 0xe8b25c; // primary vtable, 116 slots

    void StoreBlock6C(const void *src);                  // slot 56, 0xba6be0: ret 4, copies 16 bytes to +0x6c
    void StoreBlock7C(const void *src);                  // slot 57, 0xb81c80: ret 4, copies 16 bytes to +0x7c
    void StoreBlock8C(const void *src);                  // slot 58, 0xb81970: ret 4, copies 16 bytes to +0x8c
    void StoreByte14C(uint8_t value);                    // slot 15, 0xba6f60: ret 4, byte to +0x14c
    void StoreDword134(uint32_t value);                  // slot 109, 0xba6f00: ret 4, dword to +0x134
    void StoreFloatBits58(uint32_t bits);                // slot 54, 0xba6f30: ret 4, the argument's 4 bytes to +0x58
    void *Address138();                                  // slot 17, 0xb94ce0: ret 0, this + 0x138
    void *Address0E4();                                  // slot 37, 0xb9be90: ret 0, this + 0xe4
    uint32_t Dword50OrOne();                             // slot 24, 0xba9f80: ret 0, [+0x50], or 1 when it is 0
    uint32_t SlotForLowestBit(uint32_t mask);            // slot 46, 0xb99410: ret 4, [+0x154 + 4i] or 0
    bool FlagsContain(uint32_t mask);                    // slot 47, 0xbaa250: ret 4, al = ([+0x150] & mask) == mask
    void SetMaskBits150(uint32_t bits, uint32_t set);    // slot 111, 0xba6a20: ret 8
    void FillSlots154(uint32_t mask, uint32_t value);    // slot 110, 0xba6b50: ret 8
    void SetDword50Notify(uint32_t value);               // slot 52, 0xba6f10: ret 4, calls slot 53 on change
    bool LowWordIsZero();                                // slot 18, 0xb9aa80: ret 0, al = (low word of slot 16 == 0)

    // slot 104, 0xb9c110: ret 8. Asks the resource manager to preload the part resources listed for one age and one
    // part bit. age -1 means 1. part 0 or -1 recurses (slot 104) over a fixed list of part bits (which list depends on
    // the +0x50 bits 1..3, the optional bits come from slot 25); a value with several bits recurses once per set bit.
    // A single bit looks up key (age << 16 | part) in the map at +0x114 (uint32 -> vector of 16-byte resource keys);
    // for each key, the resource (type 0xebcf3e20) whose age mask (slot 7) admits `age` is preloaded, and so is each
    // of its items, matched by name against the resource listed under key (0x800000 | part) (for parts 4, 8 and
    // 0x10 also 0x800004, 0x800010, 0x800008 in that order), and each item's key is passed to 0xb9bec0.
    void PreloadParts(uint32_t age, uint32_t part);
};

// The map at +0x114: an MSVC std::map<uint32_t, vector of 16-byte resource keys> node (the map's +0 is the head).
struct OutfitPartNode {
    OutfitPartNode *mLeft;   // +0x00
    OutfitPartNode *mParent; // +0x04
    OutfitPartNode *mRight;  // +0x08
    uint8_t mColor;          // +0x0c
    uint8_t mIsNil;          // +0x0d (the head only)
    uint32_t mKey;           // +0x10
    uint8_t (*mFirst)[16];   // +0x14 vector of resource keys
    uint8_t (*mLast)[16];    // +0x18
    uint8_t (*mEnd)[16];     // +0x1c
};
static_assert(sizeof(OutfitPartNode) == 0x20, "OutfitPartNode layout");

} // namespace nTSCASCore
