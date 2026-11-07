#pragma once
#include <cstdint>

// nGZSceneDBAnim::cSceneDBAnimSystem: the animation system service. The object is a cISceneDBAnimSystem
// (primary vtable 0xe62544, 46 slots, at object +0) with a cRZSystemService / cIGZSystemService subobject at
// +4 (vtable 0xe62600, 13 slots) and a cRZUnknownMT subobject at +8 (vtable 0xe62638, 6 slots). Every function
// here is a slot of the primary vtable, so `this` is the object itself. Slot N is at byte offset 4N; RVAs are
// 1.18.0.187.
//
// Layout as these functions touch it (the object is larger; its size is not known yet):
//   +0x00  primary vtable
//   +0x04  cRZSystemService subobject (QueryInterface falls through to it with this = object + 4)
//   +0x20  list A: begin, +0x24 end (count = (end - begin) / 4)
//   +0x2c  list B: begin, +0x30 end (count = (end - begin) / 4)
//   +0x38  cRZRandom (mIntegerSeed, mRealSeed, mUnknown)
//   +0x54  counter, drained by DrainWhileCounter54 (argument from +0x60)
//   +0x64  value (SetCounter64AndDrain stores its argument here)
//   +0x68  counter, drained by DrainWhileCounter68 and SetCounter64AndDrain (argument from +0x74)
//   +0xb8, +0xbc  two words reset by ResetWords (0xb8, 0xbc)
namespace nGZSceneDBAnim {

class cSceneDBAnimSystem {
public:
    static constexpr uint32_t kVTable = 0xe62544;     // primary vtable, 46 slots
    static constexpr uint32_t kSubobjectOffset = 4;   // cRZSystemService subobject

    bool QueryInterface(uint32_t riid, void **ppv);       // slot 0, 0x9db320: ret 8
    uint32_t ListAElementAt(uint32_t index);              // slots 3 and 4, 0x9dac40: ret 4
    uint32_t ListBElementAt(uint32_t index);              // slots 7 and 8, 0x9dabe0: ret 4
    uint32_t FindVirtualByValue(uint32_t value);          // slot 9, 0x9dabf0: ret 4
    void SetCounter64AndDrain(uint32_t value);            // slot 20, 0x9dba10: ret 4
    void DrainWhileCounter68();                           // slot 21, 0x9dada0
    void DrainWhileCounter54();                           // slot 22, 0x9dadd0
    void ResetWords();                                    // slot 26, 0x9db870
    int32_t FindInListA(uint32_t value);                  // slot 29, 0x9daef0: ret 4 (index or -1)
    int32_t FindInListB(uint32_t value);                  // slot 31, 0x9daec0: ret 4 (index or -1)
    int32_t ListBCount();                                 // slot 32, 0x9db270: (end - begin) >> 2
    uint32_t RandomUint32();                              // slot 44, 0x9db390: tail call to cRZRandom
    float RandomFloat();                                  // slot 45, 0x9db370: (float) cRZRandom double
};

} // namespace nGZSceneDBAnim
