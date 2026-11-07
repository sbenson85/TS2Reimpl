#pragma once
#include <cstddef>
#include <cstdint>

// Animation channel classes of nGZSceneDBAnim (RVAs are 1.18.0.187). Each class's primary vtable is at object +0.
// Slot N is at byte offset 4N.
//
// The classes are plain structs with the game's layout; the vtable word is written by Destruct (and, later, by the
// constructors), and virtual calls go through the object's own vtable (VirtualSlot) so game subclasses still work.
// Sizes come from the sized deletes: cChannelUber 0xc4, cChannelUberXYZ 0xd4, cChannelUberAttribute 0xdc,
// cChannelUberQ 0x164.
//
// Object kinds the channels point at (their classes are not identified yet):
//   - "V" reference: a pointer to an object with a virtual base. The object has a vfptr at +0 and a vbptr at +4;
//     the interface that AddRef/Release are called on is at (object + 4) + vbtable[1]. Released with slot 2.
//   - "D" reference: a pointer to an object whose own vtable (at +0) has Release in slot 2.
//   - raw: a pointer the channel does not own (not released by the destructor).
namespace nGZSceneDBAnim {

// Base channel. Vtable 0xe67c44 (9 slots; slots 4-8 are pure virtual).
class cChannelUber {
public:
    static constexpr uint32_t kVTable = 0xe67c44;
    static constexpr uint32_t kSize = 0xc4;

    // Slots.
    bool QueryInterface(uint32_t riid, void **ppv);     // slot 0, 0xa266d0: ret 8, al only
    uint32_t AddRef();                                  // slot 1: the shared body of 0x148fb0 / 0x146a50
    uint32_t Release();                                 // slot 2, 0x2386b0: decrements; at 0 calls slot 3 with 1
    cChannelUber *DeletingDestructor(uint32_t flags);   // slot 3, 0xa266a0: ret 4
    // Slot 4-8 are pure virtual (_purecall); the derived classes override 5/6/4 with trivial bodies and 7, 8 with the
    // evaluators.

    // Constructor 0xa26250 is not reimplemented (it builds a collaborator through unidentified pool allocators).
    void Destruct(); // 0xa264a0: the base destructor body

    // 0xa267b0 (ret 4): the shared weight evaluator the slots 7 and 8 start with. Asks the target (+0x18) for its
    // context and reads a float from it, then (modes 4-6) blends it with the curve at +0x28. *blend, when not null,
    // receives the complementary weight. Returns the weight as a float in st0.
    float Evaluate(float *blend);

    const void *vtable_;   // +0x00
    uint32_t refCount_;    // +0x04
    void *ref8_;           // +0x08  V reference
    uint8_t flagC_;        // +0x0c  1 after construction
    uint8_t padD_[3];
    int32_t mode_;         // +0x10  evaluators switch on mode - 1 and test mode == 2
    void *ref14_;          // +0x14  V reference
    void *target_;         // +0x18  raw; slot 12 returns the context, slot 0 of its vbase 1 is QueryInterface
    void *raw1c_;          // +0x1c  raw
    void *ref20_;          // +0x20  D reference
    int32_t axis24_;       // +0x24  3 after construction
    void *ref28_;          // +0x28  V reference; slot 6 (int) returns a float curve value
    void *ref2c_;          // +0x2c  D reference; slot 18 (float) is a setter
    uint8_t flag30_;       // +0x30
    uint8_t pad31_[3];
    uint32_t key34_[4];    // +0x34
    uint32_t key44_;       // +0x44  (&key44_ is passed to the context's slot 62)
    void *ref48_;          // +0x48  D reference
    void *ref4c_;          // +0x4c  D reference
    uint32_t key50_[4];    // +0x50
    uint32_t key60_;       // +0x60
    void *ref64_;          // +0x64  D reference
    void *ref68_;          // +0x68  D reference
    uint32_t block6c_[6];  // +0x6c
    float f84_;            // +0x84  1.0 after construction
    void *raw88_;          // +0x88  raw
    uint8_t flag8c_;       // +0x8c  negate component index90_ of the result
    uint8_t pad8d_[3];
    int32_t index90_;      // +0x90
    int32_t i94_;          // +0x94  1 after construction
    int32_t i98_;          // +0x98  3 after construction
    float f9c_;            // +0x9c  1.0
    float fa0_;            // +0xa0  -1.0
    float fa4_;            // +0xa4  1.0
    void *rawA8_;          // +0xa8  raw
    uint8_t flagAC_;       // +0xac
    uint8_t flagAD_;       // +0xad
    uint8_t padAE_[2];
    float quatB0_[4];      // +0xb0  0, 0, 0, 1 after construction
    void *refC0_;          // +0xc0  D reference
};
static_assert(sizeof(void *) == 4, "the game is 32-bit");
static_assert(sizeof(cChannelUber) == cChannelUber::kSize, "cChannelUber layout");
static_assert(offsetof(cChannelUber, mode_) == 0x10 && offsetof(cChannelUber, target_) == 0x18, "cChannelUber layout");
static_assert(offsetof(cChannelUber, ref28_) == 0x28 && offsetof(cChannelUber, flag30_) == 0x30, "cChannelUber layout");
static_assert(offsetof(cChannelUber, key44_) == 0x44 && offsetof(cChannelUber, ref4c_) == 0x4c, "cChannelUber layout");
static_assert(offsetof(cChannelUber, ref68_) == 0x68 && offsetof(cChannelUber, f84_) == 0x84, "cChannelUber layout");
static_assert(offsetof(cChannelUber, flag8c_) == 0x8c && offsetof(cChannelUber, rawA8_) == 0xa8, "cChannelUber layout");
static_assert(offsetof(cChannelUber, quatB0_) == 0xb0 && offsetof(cChannelUber, refC0_) == 0xc0, "cChannelUber layout");

// cChannelUber with a Q-type evaluator. Vtable 0xe6431c (9 slots).
class cChannelUberQ : public cChannelUber {
public:
    static constexpr uint32_t kVTable = 0xe6431c;
    static constexpr uint32_t kSize = 0x164;
    static constexpr uint32_t kPoolHeadRva = 0x1064bf4; // free list of dead objects

    bool QueryInterface(uint32_t riid, void **ppv);     // slot 0, 0x9efd10: riid 1, 0xac704952, 0xec70498e
    cChannelUberQ *DeletingDestructor(uint32_t flags);  // slot 3, 0x9ef0d0
    void Destruct();                                    // 0x9eef80
    static void __cdecl PoolFree(void *object);         // 0x9ef0b0: the class's operator delete (free-list push)
    void *ReturnThis();                                 // slot 4 here, slot 5 of XYZ and slot 6 of Attribute: 0x1413e0
    // Slots 7 / 8 (lane IC): the weight, Evaluate(blend) unless mode 2 (then 1.0); when it is positive or the mode is 2,
    // the rotation sampler 0x9ef110 (original) fills out + 0xc for , and a failed sample makes the weight 0. Slot 8 passes no blend.
    float EvaluateBlended(uint32_t time, uint8_t *out, float *blend); // slot 7, 0x9efc40 (ret 0xc)
    float EvaluateUnblended(uint32_t time, uint8_t *out);             // slot 8, 0x9efbe0 (ret 8)

    void *refC4_;           // +0x0c4  V reference
    float quatC8_[4];       // +0x0c8  0, 0, 0, 1
    float angles_[3];       // +0x0d8  cached angles
    float trig_[6];         // +0x0e4  sin/cos of the three angles
    float quatFC_[4];       // +0x0fc
    float block10c_[17];    // +0x10c  (0x11c, 0x124, 0x12c, 0x13c and 0x14c are 1.0 after construction)
    void *ref150_;          // +0x150  D reference
    int32_t i154_;          // +0x154
    float newAngles_[3];    // +0x158
};
static_assert(sizeof(cChannelUberQ) == cChannelUberQ::kSize, "cChannelUberQ layout");
static_assert(offsetof(cChannelUberQ, refC4_) == 0xc4 && offsetof(cChannelUberQ, trig_) == 0xe4, "cChannelUberQ layout");
static_assert(offsetof(cChannelUberQ, block10c_) == 0x10c && offsetof(cChannelUberQ, ref150_) == 0x150, "cChannelUberQ layout");
static_assert(offsetof(cChannelUberQ, newAngles_) == 0x158, "cChannelUberQ layout");

// cChannelUber with an XYZ evaluator. Vtable 0xe64354 (9 slots).
class cChannelUberXYZ : public cChannelUber {
public:
    static constexpr uint32_t kVTable = 0xe64354;
    static constexpr uint32_t kSize = 0xd4;
    static constexpr uint32_t kPoolHeadRva = 0x1064ee4;

    bool QueryInterface(uint32_t riid, void **ppv);     // slot 0, 0x9f0840: riid 1, 0xcc70496e, 0xec70498e
    cChannelUberXYZ *DeletingDestructor(uint32_t flags); // slot 3, 0x9eff80
    void Destruct();                                    // 0x9efe60
    static void __cdecl PoolFree(void *object);         // 0x9eff60
    // Slots 7 / 8 (lane IC): the weight, Evaluate(blend) unless mode 2 (then 1.0); when it is positive or the mode is 2,
    // the position sampler 0x9effc0 (original) fills out + 0x1c for , and a failed sample makes the weight 0. Slot 8 passes no blend.
    float EvaluateBlended(uint32_t time, uint8_t *out, float *blend); // slot 7, 0x9f0770 (ret 0xc)
    float EvaluateUnblended(uint32_t time, uint8_t *out);             // slot 8, 0x9f0710 (ret 8)

    void *refC4_;           // +0xc4  V reference
    float position_[3];     // +0xc8
};
static_assert(sizeof(cChannelUberXYZ) == cChannelUberXYZ::kSize, "cChannelUberXYZ layout");

// cChannelUber with an Attribute evaluator. Vtable 0xe6438c (9 slots).
class cChannelUberAttribute : public cChannelUber {
public:
    static constexpr uint32_t kVTable = 0xe6438c;
    static constexpr uint32_t kSize = 0xdc;
    static constexpr uint32_t kPoolHeadRva = 0x10651d4;

    bool QueryInterface(uint32_t riid, void **ppv);          // slot 0, 0x9f0c70: riid 1, 0x6ce2e072, 0xec70498e
    cChannelUberAttribute *DeletingDestructor(uint32_t flags); // slot 3, 0x9f0a10
    float Slot7(float time, uint8_t *record, float *blend);  // slot 7, 0x9f0bb0: ret 0xc
    float Slot8(float time, uint8_t *record);                // slot 8, 0x9f0b60: ret 8
    void Destruct();                                         // 0x9f08f0
    static void __cdecl PoolFree(void *object);              // 0x9f09f0

    // 0x9f0a50 (ret 8): copies the attribute's values (count_ floats) into `dest`. The first parameter is unused.
    // Modes 1 and 5 copy the values stored in the channel (+0xcc), mode 9 does too but needs ref14_, mode 4 asks the
    // context for a key and the data object (ref_) for the values, and every other mode fails.
    bool Fetch(float time, float *dest);

    int32_t count_;         // +0xc4  number of floats
    void *ref_;             // +0xc8  V reference
    float values_[4];       // +0xcc
};
static_assert(sizeof(cChannelUberAttribute) == cChannelUberAttribute::kSize, "cChannelUberAttribute layout");
static_assert(offsetof(cChannelUberAttribute, count_) == 0xc4 && offsetof(cChannelUberAttribute, values_) == 0xcc,
              "cChannelUberAttribute layout");

// nGZSceneDBAnim::cEventKeyConst (primary vtable 0xe67f00, 15 slots). The object's size is not known; the fields the
// reimplemented slots read are named.
class cEventKeyConst {
public:
    static constexpr uint32_t kVTable = 0xe67f00;

    // Result of slot 4: a new cRZString holding the class name, a type id and a count.
    struct ClassNameResult {
        void *name;
        uint32_t typeId;
        uint32_t count;
    };

    bool QueryInterface(uint32_t riid, void **ppv);   // slot 0, 0xa29200: ret 8, al only
    void Nop4(uint32_t);                              // slots 3, 5, 7, 8 and 9: 0x300a0 (ret 4)
    ClassNameResult *GetTypeName(ClassNameResult *out); // slot 4, 0xa29270: ret 4
    void Nop8(uint32_t, uint32_t);                    // slot 10, 0x1b9a50 (ret 8)
    float GetFloat11();                               // slot 11, 0xa29240: the unsigned word at +4 as a float
    int32_t GetSigned12();                            // slot 12, 0xa29260: the signed word at +6
    uint32_t GetDword13();                            // slot 13, 0xa29230: the dword at +0xa (unaligned)
    cEventKeyConst *Construct();                      // 0xa29140: sets the vtable, returns this
    void Destruct();                                  // 0xa29150: sets the vtable

    const void *vtable_;    // +0x00
    uint16_t word4_;        // +0x04
    int16_t word6_;         // +0x06
    uint8_t unknown8_[2];
    uint8_t dwordA_[4];     // +0x0a  (unaligned dword, read as bytes)
};
static_assert(offsetof(cEventKeyConst, word4_) == 4 && offsetof(cEventKeyConst, word6_) == 6, "cEventKeyConst layout");
static_assert(offsetof(cEventKeyConst, dwordA_) == 0xa, "cEventKeyConst layout");

} // namespace nGZSceneDBAnim
