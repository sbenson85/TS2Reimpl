#pragma once
#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

class cIGZString;

// cTSInteraction: one interaction a Sim has queued, is running or is choosing between (the entries of the person's
// queues, its current and selected interaction, FindBestAction's pending list): who does it (the actor's object id), on
// what (the stack object and the index of the interaction in its table), the tree arguments, priority, the values copied
// from the table entry when it is set up (action tree, attenuation, sub-queue flags, display type, name), a link id that
// chains the interactions of a joint action, a serial number and a flag word. RTTI: cITSInteraction : cIGZSerializable :
// cIGZUnknown, all at +0 (one vtable, 0xdc8324, 81 slots). 0x6c bytes. Class id 0xfc5ecc3d. RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md, lane GV): a real C++ class; the compiler emits its vtable. The game creates
// interactions only through the creator 0x450a70 (no constructor function: the constructor is inline in it), which the
// cTSSimCOMDirector registers by address for class id 0xfc5ecc3d; the game's vtable is referenced only by that creator and
// the deleting destructor 0x44f640. The hook on the creator is the class's switch. Every slot function is referenced only
// by the game's vtable: the byte-matched ones keep an adapter (cTSInteractionAdapters), the others have no hook and are
// trapped under Hooks.cTSInteraction_Create=1 ("in_effect": "callers").
//
// Other objects are reached through their vtables (slot numbers below): a person (cITSPerson face; 8 its object, 11 sets a
// person data field, 30 a byte for the attenuation, 123 removes a queued interaction by serial, 126/127 the pending and
// action queue counts, 129/130 their elements, 132 the current and 133 the selected interaction); an object (cTSObject: 47
// its id, 87 its interaction table); the table (13: the entry at an index); the table entry (cITSTreeTableEntry, see
// cTSTreeTableEntry.h: 19, 27, 31, 37, 39, 41, 46, 48, 50, 76, 84, 90).

// The interaction's name: a cRZString (hook-shaped, built and destroyed by hand) as a member.
struct cTSInteractionName {
    union { cRZString mString; };
    cTSInteractionName() { mString.Construct(); }
    cTSInteractionName(const cTSInteractionName &) = delete;
    cTSInteractionName &operator=(const cTSInteractionName &) = delete;
    ~cTSInteractionName() { mString.Destruct(); }
};
static_assert(sizeof(cTSInteractionName) == 0x20, "name layout");

// The interaction's interface (the original's name, global as its RTTI): slots 6..80 after cIGZSerializable's. Flag
// getters and setters take and return the bit of the flag word at +0x68 named in the comment; a setter reads only the low
// byte of its argument.
class cITSInteraction : public cIGZSerializable {
public:
    virtual bool IsDefault() = 0;                                   // 6: nothing set up (the creator's values)
    virtual bool SetUpBetween(void *actor, void *source) = 0;       // 7: two persons: the source's current interaction joined
    virtual bool SetUp(void *actor, void *stackObject, int32_t index, uint32_t priority, void *extraObject) = 0; // 8
    virtual bool CopyFrom(cITSInteraction *other) = 0;              // 9
    virtual void ClearObjectIds() = 0;                              // 10: the four object ids to 0
    virtual uint32_t GetSourceKind() = 0;                           // 11: 1 when set up by slot 7, 0 by slot 8
    virtual float GetAttenuation() = 0;                             // 12: the entry's attenuation value for the actor
    virtual int16_t GetActionTreeId() = 0;                          // 13
    virtual void SetActionTreeId(int16_t id) = 0;                   // 14
    virtual int16_t *GetArguments() = 0;                            // 15: the four tree arguments
    virtual void *GetStackObject() = 0;                             // 16: the object with id +0xe (object table)
    virtual void *GetActor() = 0;                                   // 17: the actor's cITSPerson (id +0xc), not held
    virtual int32_t GetInteractionIndex() = 0;                      // 18: in the stack object's table; -1 none
    virtual uint16_t GetAllowSubQueue() = 0;                        // 19: the entry's flag, copied by slot 8
    virtual uint16_t GetTypeSubQueue() = 0;                         // 20: likewise
    virtual int16_t GetUIDisplayType() = 0;                         // 21
    virtual uint32_t GetPriority() = 0;                             // 22
    virtual void Ignore(uint32_t unused) = 0;                       // 23: does nothing (a folded `ret 4`)
    virtual void *GetTableEntry() = 0;                              // 24: the stack object's table entry at the index
    virtual cIGZString *GetName() = 0;                              // 25: the name, built from the entry when empty
    virtual void SetName(const cIGZString *name) = 0;               // 26: copies it, sets bit 7
    virtual void SetArguments(const int16_t *arguments) = 0;        // 27: four words
    virtual void AssignSerial() = 0;                                // 28: the next number of the global counter
    virtual bool HasSerial() = 0;                                   // 29
    virtual uint32_t GetSerial() = 0;                               // 30
    virtual bool GetAutoFirstSelect() = 0;                          // 31: bit 0 (the entry's AutoFirstSelect)
    virtual void SetAutoFirstSelect(bool on) = 0;                   // 32
    virtual bool GetFlag1() = 0;                                    // 33: bit 1
    virtual void SetFlag1(bool on) = 0;                             // 34
    virtual bool GetFlag14() = 0;                                   // 35: bit 14
    virtual void SetFlag14(bool on) = 0;                            // 36
    virtual bool GetCheckPending() = 0;                             // 37: bit 2
    virtual void SetCheckPending(bool on) = 0;                      // 38
    virtual bool HasPassedCheck() = 0;                              // 39: bit 3
    virtual void SetCheckPassed(bool on) = 0;                       // 40
    virtual bool GetFlag4() = 0;                                    // 41: bit 4
    virtual void SetFlag4(bool on) = 0;                             // 42
    virtual bool GetBranchFlag() = 0;                               // 43: bit 5
    virtual void SetBranchFlag(bool on) = 0;                        // 44
    virtual bool GetFlag6() = 0;                                    // 45: bit 6
    virtual void SetFlag6(bool on) = 0;                             // 46
    virtual bool GetFlag10() = 0;                                   // 47: bit 10
    virtual void SetFlag10(bool on) = 0;                            // 48
    virtual bool GetFlag11() = 0;                                   // 49: bit 11
    virtual void SetFlag11(bool on) = 0;                            // 50
    virtual bool GetNameSet() = 0;                                  // 51: bit 7 (slot 26 sets it, slot 25 clears it)
    virtual void SetNameSet(bool on) = 0;                           // 52
    virtual bool GetFlag8() = 0;                                    // 53: bit 8
    virtual void SetFlag8(bool on) = 0;                             // 54
    virtual bool IsVetoed() = 0;                                    // 55: bit 9
    virtual void SetVetoed(bool on) = 0;                            // 56
    virtual bool GetFlag12() = 0;                                   // 57: bit 12
    virtual void SetFlag12(bool on) = 0;                            // 58
    virtual bool GetFlag13() = 0;                                   // 59: bit 13
    virtual void SetFlag13(bool on) = 0;                            // 60
    virtual void *GetExtraObject() = 0;                             // 61: the object with id +0x12
    virtual int16_t GetExtraObjectId() = 0;                         // 62
    virtual void *GetGroupObject() = 0;                             // 63: the object with id +0x10
    virtual int16_t GetGroupObjectId() = 0;                         // 64
    virtual void SetGroupObject(void *object) = 0;                  // 65: its id, -1 for none
    virtual void AssignLinkId() = 0;                                // 66: a new id from the singleton's slot 3 object
    virtual void CancelLinked(void *exceptPerson) = 0;              // 67: ends the chain of linked interactions
    virtual uint16_t GetLinkId() = 0;                               // 68
    virtual uint16_t GetLinkNext() = 0;                             // 69
    virtual void SetLinkNext(uint16_t id) = 0;                      // 70
    virtual cITSInteraction *FindByLinkId(int16_t id) = 0;          // 71: among every person's interactions
    virtual void SetParameters(uint32_t a, uint32_t b) = 0;         // 72
    virtual void GetParameters(uint32_t *a, uint32_t *b) = 0;       // 73
    virtual int16_t GetWord32() = 0;                                // 74
    virtual void SetWord32(int16_t value) = 0;                      // 75
    virtual int16_t GetWord34() = 0;                                // 76
    virtual void SetWord34(int16_t value) = 0;                      // 77
    virtual bool HasGroup() = 0;                                    // 78: a group object, or parameter A not 0 / -1
    virtual ~cITSInteraction() = default;                           // 79: the deleting destructor
    virtual void SetExtraObject(void *object) = 0;                  // 80: its id, -1 for none
};

class cTSInteraction : public cITSInteraction {
public:
    static constexpr uint32_t kOriginalVTable = 0xdc8324;
    static constexpr uint32_t kVTableSlots = 81;
    static constexpr uint32_t kClassId = 0xfc5ecc3d;
    static constexpr uint32_t kInterfaceId = 0x5aca6692;   // cITSInteraction
    static constexpr uint32_t kSerializableId = 0xe4fda3d4;
    static constexpr uint32_t kSerialCounter = 0x10da95c;  // int32: the last serial handed out (slot 28, Read)
    static constexpr size_t kSize = 0x6c;

    cTSInteraction();
    cTSInteraction(const cTSInteraction &) = delete;
    cTSInteraction &operator=(const cTSInteraction &) = delete;
    ~cTSInteraction() override;                           // slot 79: 0x44f640

    // 0x450a70 (__cdecl, no arguments): the creator the COM director registers; reference count 0.
    static cTSInteraction *Create();

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x451560
    uint32_t AddRef() override;                              // folded 0x148fb0
    uint32_t Release() override;                             // 0x451990
    // cIGZSerializable
    bool Write(ifaces::cIGZOStream *stream) override;        // 0x452540
    bool Read(ifaces::cIGZIStream *stream) override;         // 0x4515d0
    uint32_t GetGZCLSID() override;                          // 0x450b50

    bool IsDefault() override;
    bool SetUpBetween(void *actor, void *source) override;
    bool SetUp(void *actor, void *stackObject, int32_t index, uint32_t priority, void *extraObject) override;
    bool CopyFrom(cITSInteraction *other) override;
    void ClearObjectIds() override;
    uint32_t GetSourceKind() override;
    float GetAttenuation() override;
    int16_t GetActionTreeId() override;
    void SetActionTreeId(int16_t id) override;
    int16_t *GetArguments() override;
    void *GetStackObject() override;
    void *GetActor() override;
    int32_t GetInteractionIndex() override;
    uint16_t GetAllowSubQueue() override;
    uint16_t GetTypeSubQueue() override;
    int16_t GetUIDisplayType() override;
    uint32_t GetPriority() override;
    void Ignore(uint32_t unused) override;
    void *GetTableEntry() override;
    cIGZString *GetName() override;
    void SetName(const cIGZString *name) override;
    void SetArguments(const int16_t *arguments) override;
    void AssignSerial() override;
    bool HasSerial() override;
    uint32_t GetSerial() override;
    bool GetAutoFirstSelect() override;
    void SetAutoFirstSelect(bool on) override;
    bool GetFlag1() override;
    void SetFlag1(bool on) override;
    bool GetFlag14() override;
    void SetFlag14(bool on) override;
    bool GetCheckPending() override;
    void SetCheckPending(bool on) override;
    bool HasPassedCheck() override;
    void SetCheckPassed(bool on) override;
    bool GetFlag4() override;
    void SetFlag4(bool on) override;
    bool GetBranchFlag() override;
    void SetBranchFlag(bool on) override;
    bool GetFlag6() override;
    void SetFlag6(bool on) override;
    bool GetFlag10() override;
    void SetFlag10(bool on) override;
    bool GetFlag11() override;
    void SetFlag11(bool on) override;
    bool GetNameSet() override;
    void SetNameSet(bool on) override;
    bool GetFlag8() override;
    void SetFlag8(bool on) override;
    bool IsVetoed() override;
    void SetVetoed(bool on) override;
    bool GetFlag12() override;
    void SetFlag12(bool on) override;
    bool GetFlag13() override;
    void SetFlag13(bool on) override;
    void *GetExtraObject() override;
    int16_t GetExtraObjectId() override;
    void *GetGroupObject() override;
    int16_t GetGroupObjectId() override;
    void SetGroupObject(void *object) override;
    void AssignLinkId() override;
    void CancelLinked(void *exceptPerson) override;
    uint16_t GetLinkId() override;
    uint16_t GetLinkNext() override;
    void SetLinkNext(uint16_t id) override;
    cITSInteraction *FindByLinkId(int16_t id) override;
    void SetParameters(uint32_t a, uint32_t b) override;
    void GetParameters(uint32_t *a, uint32_t *b) override;
    int16_t GetWord32() override;
    void SetWord32(int16_t value) override;
    int16_t GetWord34() override;
    void SetWord34(int16_t value) override;
    bool HasGroup() override;
    void SetExtraObject(void *object) override;

    // +0x00 vtable
    uint32_t mRefCount;          // +0x04
    uint32_t mSourceKind;        // +0x08
    int16_t mActorId;            // +0x0c: the actor's object id
    int16_t mStackObjectId;      // +0x0e
    int16_t mGroupObjectId;      // +0x10
    int16_t mExtraObjectId;      // +0x12
    int32_t mInteractionIndex;   // +0x14
    int16_t mArguments[4];       // +0x18
    uint32_t mPriority;          // +0x20
    int16_t mActionTreeId;       // +0x24
    float mAttenuation;          // +0x28
    uint16_t mAllowSubQueue;     // +0x2c
    uint16_t mTypeSubQueue;      // +0x2e
    int16_t mUIDisplayType;      // +0x30
    int16_t mWord32;             // +0x32
    int16_t mWord34;             // +0x34 (+0x36 padding: never written)
    uint32_t mParameterA;        // +0x38: the entry's slot 48 when its bit-22 flag is set
    uint32_t mParameterB;        // +0x3c: the entry's slot 46 likewise
    uint16_t mLinkId;            // +0x40
    uint16_t mLinkNext;          // +0x42: the next interaction of the chain (its link id), 0 for none
    cTSInteractionName mName;    // +0x44
    uint32_t mSerial;            // +0x64
    uint32_t mFlags;             // +0x68
};
static_assert(sizeof(cTSInteraction) == cTSInteraction::kSize, "cTSInteraction layout");
static_assert(offsetof(cTSInteraction, mRefCount) == 0x04);
static_assert(offsetof(cTSInteraction, mActorId) == 0x0c);
static_assert(offsetof(cTSInteraction, mInteractionIndex) == 0x14);
static_assert(offsetof(cTSInteraction, mArguments) == 0x18);
static_assert(offsetof(cTSInteraction, mPriority) == 0x20);
static_assert(offsetof(cTSInteraction, mAttenuation) == 0x28);
static_assert(offsetof(cTSInteraction, mWord34) == 0x34);
static_assert(offsetof(cTSInteraction, mParameterA) == 0x38);
static_assert(offsetof(cTSInteraction, mLinkId) == 0x40);
static_assert(offsetof(cTSInteraction, mName) == 0x44);
static_assert(offsetof(cTSInteraction, mSerial) == 0x64);
static_assert(offsetof(cTSInteraction, mFlags) == 0x68);

// The RVA hooks' entry points (src/hook/tables/cTSInteraction.cpp) for the byte-matched slots, which keep an adapter
// (docs/standalone-shape.md, "Dropping adapters"): the original calling convention (this in ecx), our function called
// without virtual dispatch, its body inlined.
struct cTSInteractionAdapters {
    using Self = cTSInteraction;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static bool __fastcall IsDefault(Self *self, void *);
    static void __fastcall ClearObjectIds(Self *self, void *);
    static void *__fastcall GetStackObject(Self *self, void *);
    static uint16_t __fastcall GetTypeSubQueue(Self *self, void *);
    static void __fastcall SetName(Self *self, void *, const cIGZString *name);
    static void __fastcall SetArguments(Self *self, void *, const int16_t *arguments);
    static void __fastcall AssignSerial(Self *self, void *);
    static bool __fastcall GetAutoFirstSelect(Self *self, void *);
    static void __fastcall SetAutoFirstSelect(Self *self, void *, bool on);
    static bool __fastcall GetFlag1(Self *self, void *);
    static void __fastcall SetFlag1(Self *self, void *, bool on);
    static bool __fastcall GetFlag14(Self *self, void *);
    static void __fastcall SetFlag14(Self *self, void *, bool on);
    static bool __fastcall GetCheckPending(Self *self, void *);
    static void __fastcall SetCheckPending(Self *self, void *, bool on);
    static bool __fastcall HasPassedCheck(Self *self, void *);
    static void __fastcall SetCheckPassed(Self *self, void *, bool on);
    static bool __fastcall GetFlag4(Self *self, void *);
    static void __fastcall SetFlag4(Self *self, void *, bool on);
    static bool __fastcall GetBranchFlag(Self *self, void *);
    static void __fastcall SetBranchFlag(Self *self, void *, bool on);
    static bool __fastcall GetFlag6(Self *self, void *);
    static void __fastcall SetFlag6(Self *self, void *, bool on);
    static bool __fastcall GetFlag10(Self *self, void *);
    static void __fastcall SetFlag10(Self *self, void *, bool on);
    static bool __fastcall GetFlag11(Self *self, void *);
    static void __fastcall SetFlag11(Self *self, void *, bool on);
    static bool __fastcall GetNameSet(Self *self, void *);
    static void __fastcall SetNameSet(Self *self, void *, bool on);
    static bool __fastcall GetFlag8(Self *self, void *);
    static void __fastcall SetFlag8(Self *self, void *, bool on);
    static bool __fastcall IsVetoed(Self *self, void *);
    static void __fastcall SetVetoed(Self *self, void *, bool on);
    static bool __fastcall GetFlag12(Self *self, void *);
    static void __fastcall SetFlag12(Self *self, void *, bool on);
    static bool __fastcall GetFlag13(Self *self, void *);
    static void __fastcall SetFlag13(Self *self, void *, bool on);
    static void *__fastcall GetExtraObject(Self *self, void *);
    static void *__fastcall GetGroupObject(Self *self, void *);
    static void __fastcall SetGroupObject(Self *self, void *, void *object);
    static void __fastcall AssignLinkId(Self *self, void *);
    static uint16_t __fastcall GetLinkId(Self *self, void *);
    static uint16_t __fastcall GetLinkNext(Self *self, void *);
    static void __fastcall SetLinkNext(Self *self, void *, uint16_t id);
    static void __fastcall SetParameters(Self *self, void *, uint32_t a, uint32_t b);
    static void __fastcall GetParameters(Self *self, void *, uint32_t *a, uint32_t *b);
    static void __fastcall SetWord32(Self *self, void *, int16_t value);
    static void __fastcall SetWord34(Self *self, void *, int16_t value);
    static bool __fastcall HasGroup(Self *self, void *);
    static void __fastcall SetExtraObject(Self *self, void *, void *object);
};
