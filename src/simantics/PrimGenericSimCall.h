#pragma once
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>

namespace primX {
struct Text;
}

// SimAntics opcode 0x01, GenericSimCall (0x58bc50; ~52 KB, __thiscall, ret 8): `handler(this = the object, frame,
// param)`. A switch on the param word +4 (0..0x54) through the jump table at 0x5986f8 (85 entries, 79 distinct targets;
// the map is in docs/subsystems/simantics.md). Each case is its own member of GenericSimCall below.
//
// Every case value of the jump table (0..0x54) is ported; 7, 8, 9, 15, 16 and everything above 0x54 throw 0x2a as the original's default does.

// The object as the case bodies see it (the same shape as the other lanes' PrimXObject): two vtables, the context holder
// at +0x40 and the pointer at +0x2c8.
struct PrimGscObject {
    void **mVTable;       // +0x00 primary vtable (cTSObject, 0xdd2f0c): slot 19 reads argument word i (i < 8), 20 writes it
    void **mSecondary;    // +0x04 vtable of the cTSTreeSim subobject
    uint8_t mPad[0x38];   // +0x08..+0x3f
    void *mContext;       // +0x40 the context holder (slots 3, 6, 9, 10, 23, 24, 26, ... return the sub-objects/lists)
    uint8_t mPad2[4];     // +0x44..+0x47
    void *mField48;       // +0x48 the owner's log object (slot 28 takes a text)
    uint8_t mPad3[0x1f4]; // +0x4c..+0x23f
    void *mField240;      // +0x240 the outfit owner object (slot 65 says whether a change is allowed)
    uint8_t mPad4[0x84];  // +0x244..+0x2c7
    void *mField2c8;      // +0x2c8 object whose slot 15 returns an attribute word and slot 41 takes six words

    // 0x58bc50 (opcode 0x01). Returns the branch code (1 true, 0 false, 2, ...); bad operands throw a 16-bit code.
    uint32_t GenericSimCall(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimGscObject, mContext) == 0x40, "PrimGscObject layout");
static_assert(offsetof(PrimGscObject, mField48) == 0x48, "PrimGscObject layout");
static_assert(offsetof(PrimGscObject, mField240) == 0x240, "PrimGscObject layout");
static_assert(offsetof(PrimGscObject, mField2c8) == 0x2c8, "PrimGscObject layout");

// A four-dword block 0x57c700 fills from its arguments in order (a resource-key shaped value).
struct Key16 {
    uint32_t w[4];
};

// The state of one call: the object, the frame, the param, and the four sub-objects the function fetches from the
// context holder before it switches (prologue order: slots 9, 10, 26, 24).
class GenericSimCall {
public:
    GenericSimCall(PrimGscObject *self, void *frame, const PrimParamView *param);

    // The prologue's four calls, then the switch. Returns the value the original leaves in eax.
    uint32_t Run();

    // The switch value, as the original reads it: the word at param +4, sign-extended; anything outside 0..0x54 (as an
    // unsigned number) goes to the default.
    uint32_t Dispatch(int32_t which);

private:
    // ---- shared helpers -------------------------------------------------------------------------------------------
    int16_t Arg(int32_t i);                  // slot 19 of the object: argument word i (0..7, else 0)
    void SetArg(int32_t i, uint32_t value);  // slot 20: stores the low word of value into argument i
    void *Context();                         // the pointer at this+0x40, read afresh each time
    void *ContextSlot(int slot);             // slot `slot` of the context holder, called now
    void *StackObject();                     // the object the frame's id word names: throws 0xa when there is none
    [[noreturn]] void Throw(int16_t code);
    bool CheckAttribute(int32_t a, int32_t b); // 0x57ee10 (original call): does the attribute word match
    void AttributeTail(void *holder, int32_t id, bool refetchAttribute); // the shared end of cases 19 and 20
    void RequirePerson(int32_t id);            // the person-list object for id must exist, else throws 0x37
    void *KeyedEntry(Key16 &key);
    bool LoadObjectText(primX::Text &text, uint32_t table, uint32_t entry, void *object); // the shared start of cases 46 and 53
    void LookupByKeys(const uint32_t *firstWords, int keyCount, uint32_t lookupId, int slot); // cases 48, 74, 75              // the shared start of cases 82 and 83

    // ---- cases (the number is the param word +4) -----------------------------------------------------------------
    uint32_t Case00();
    uint32_t Case41();
    uint32_t Case42();
    uint32_t Case76();
    uint32_t Case49();
    uint32_t Case62();
    uint32_t Case66();
    uint32_t Case68();
    uint32_t Case34();
    uint32_t Case45();
    uint32_t Case24();
    uint32_t Case30();
    uint32_t Case47();
    uint32_t Case48();
    uint32_t Case67();
    uint32_t Case74();
    uint32_t Case75();
    uint32_t Case84();
    uint32_t Case28();
    uint32_t Case37();
    uint32_t Case40();
    uint32_t Case44();
    uint32_t Case51();
    uint32_t Case72();
    uint32_t Case78();
    uint32_t Case79();
    uint32_t Case01_03();     // 1, 3
    uint32_t Case02();
    uint32_t Case05();
    uint32_t Case10();
    uint32_t Case11();
    uint32_t Case12();
    uint32_t Case13();
    uint32_t Case14();
    uint32_t Case17();
    uint32_t Case18();
    uint32_t Case19();
    uint32_t Case20();
    uint32_t Case21();
    uint32_t Case22();
    uint32_t Case23();
    uint32_t Case25();
    uint32_t Case27();
    uint32_t Case26_80();     // 26, 80
    uint32_t Case33();
    uint32_t Case35();
    uint32_t Case36();
    uint32_t Case38();
    uint32_t Case54();
    uint32_t Case55();
    uint32_t Case56();
    uint32_t Case57();
    uint32_t Case58();
    uint32_t Case82();
    uint32_t Case83();
    uint32_t Case63();
    uint32_t Case64();
    uint32_t Case65();
    uint32_t Case69();
    uint32_t Case71();
    uint32_t Case73();
    uint32_t Case77();
    uint32_t Case04();
    uint32_t Case06();
    uint32_t Case43();
    uint32_t Case52();
    uint32_t Case59();
    uint32_t Case60();
    uint32_t Case70();
    uint32_t Case81();
    void NotifyAppearanceChange(void *iface, int32_t mode); // the end of cases 59 and 60
    uint32_t Case29();
    uint32_t Case31();
    uint32_t Case32();
    uint32_t Case39();
    uint32_t Case46();
    uint32_t Case50();
    uint32_t Case53();
    uint32_t Case61();

    PrimGscObject *mSelf;
    void *mFrame;
    const PrimParamView *mParam;
    void *mL2c = nullptr; // context slot 9 at entry
    void *mL24 = nullptr; // context slot 10 at entry
    void *mL1c = nullptr; // context slot 26 at entry: the object table (slot 29 finds an object by id)
    void *mL18c = nullptr; // context slot 24 at entry
};
