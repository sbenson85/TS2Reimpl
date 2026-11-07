#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct ServiceRefVector;

// cEdithObjectTestSim (1.18.0.187, vtable 0xdc8490, 11 slots, 0x2c bytes): the object that runs an interaction's check tree
// (the tree that decides whether a Sim may use an interaction) for the person primitives. The primitives keep one on the stack:
// construct it, Begin() it with the person, the stack object and two flags, RunCheckTree() it once per interaction, destroy it.
// Destroying restores the tree arguments of the actor's object that RunCheckTree saved.
//
// Only the members the primitives call are here (the other vtable slots are the game's): constructor, destructor, Begin
// (slot 3) and RunCheckTree (slot 9).
struct cEdithObjectTestSim {
    static constexpr uint32_t kVTable = 0xdc8490;
    static constexpr int kArgumentCount = 8; // the tree arguments of the actor's object

    void **mVTable;                          // +0x00
    uint32_t mRefCount;                      // +0x04 (slot 2 releases it)
    void *mPerson;                           // +0x08 the cITSPerson subobject Begin was given
    uint32_t mObjectKey;                     // +0x0c cleared by Begin, then set through slot 5 from the stack object's slot 95. That
                                             // slot returns the object itself, so this is the stack object (a cTSObject address)
    uint8_t mFlagA;                          // +0x10 Begin's third argument; set: the first tree argument is 1
    uint8_t mFlagB;                          // +0x11 Begin's fourth argument; set: a run's verdict is forced to 1
    uint8_t mPad12[2];                       // +0x12 (the constructor clears the word at +0x10 only)
    // +0x14 the entry indexes of the stack object's table that AppendAutonomousInteractions walks: the vector inside the
    // interaction group the gather loop is on (set by 0x20daf0, a one-store setter shared with other classes)
    const std::vector<uint32_t> *mEntryIndexes;
    uint16_t mSavedArguments[kArgumentCount]; // +0x18 the actor object's tree arguments before the first run
    void *mSavedFor;                         // +0x28 the cITSPerson whose arguments were saved (null: nothing to restore)

    // 0x44f510 (__thiscall): vtable, +4 / +8 / +0xc / word +0x10 / +0x28 cleared; the rest is left as it was. Returns this.
    cEdithObjectTestSim *Construct();

    // 0x44f540 (__thiscall): sets the vtable back and, when arguments were saved, writes the eight saved words back to the
    // actor object (cITSPerson slot 8, then cTSObject slot 20) and forgets them.
    void Destruct() noexcept;

    // 0x451060 (slot 3, ret 0x10): stores the person (`personInterface`, a cITSPerson subobject address) and the two flags,
    // clears +0xc and +0x28, then gives slot 5 the stack object's slot 95. Returns 1 (the rest of eax is the stack object's slot-95 value).
    uint8_t Begin(void *personInterface, void *stackObject, uint32_t flagA, uint32_t flagB);

    // 0x4510b0 (slot 4, ret 0xc): Begin without the stack object: stores the person and the two flags (low bytes) and clears
    // +0xc and +0x28. Returns 1 in al (the upper bits are the person pointer's, unused). Lane AF: the FindBestAction search
    // calls this and SetObjectKey separately.
    uint8_t SetPerson(void *personInterface, uint32_t flagA, uint32_t flagB);

    // Slot 5 (0x149c60, shared with other classes; folded, so not hooked): sets the object key (+0xc) through the vtable.
    void SetObjectKey(uint32_t key);

    // 0x451f00 (slot 9, ret 0x18): decides whether `interaction` (a cTSInteraction) passes its check and records the
    // verdict in it.
    //  - Does nothing for an interaction without an id, or without actor / stack object / table entry.
    //  - The verdict starts as "age masks of the actor and the table entry intersect", then the vetoes: the table entry's
    //    own check (when the actor asks for it), the stack object's slots 135 and 190, the tuning value 0x23 with the
    //    entry's restriction, and the entry's debug-only flag (a debug-flagged stack object with the debug setting on is the one case that wants it).
    //  - A passing interaction with a check tree (the table entry's check tree id) and `runTree` set runs that tree on the
    //    actor's tree sim (CallTree with the stack object's owner, ids, `callMode`, tree arguments {flagA, source kind is 1,
    //    `extraArgument`, 0}); the actor's tree arguments are saved first (once per run object). Otherwise the verdict is 1.
    //  - With a `recorder` the table entry is registered, and a verdict whose actor and stack object owners differ may
    //    reset or negate the recorder's counter record.
    //  - The interaction's check flags are then set from the actor's attributes 0x32, 6, 0x65 and 0x68.
    //  `bypassVeto` skips the autonomy veto only together with the other conditions listed above.
    void RunCheckTree(void *interaction, uint32_t callMode, void *recorder, uint32_t runTree, uint32_t extraArgument, uint32_t bypassVeto);

    // ---- the gather side (lane AH; src/simantics/EdithObjectTestSimGather.cpp) -----------------------------------------------
    // The test sim collects the interactions a Sim may start on a stack object into `list` (counted pointers to cTSInteraction,
    // allocator-service storage).

    // 0x44fb90 (slot 6, ret 0xc): flag A set (the autonomy search) -> AppendAutonomousInteractions(list, functionMask), else
    // AppendInteractionsForMenu(list, stopAtFirst); the argument that does not apply is ignored.
    void AppendInteractions(ServiceRefVector *list, uint32_t functionMask, uint32_t stopAtFirst);

    // 0x44fbc0 (ret 8): the autonomy gather. For every entry index of mEntryIndexes (last to first) of the stack object's
    // interaction table that the Sim's age mask allows, that is not flagged, and whose threshold the Sim's Person Data 0x24
    // reaches, makes an interaction (SetUp with the entry index and source kind 0), flags it (slot 48) and lists it when its
    // menu string id is positive. Then, for a stack object that is a Sim (object kind 2), the interaction that Sim's current
    // interaction's table entry implies (SetUpBetween, branch flag, flag 400) when that entry is Sim-only and provides one of the
    // functions in `functionMask` (all of them when the mask is empty) and is not flagged.
    // ORIGINAL-BUG: an entry index the table does not have (a null entry) is only reported through the table's name and then
    // used, which crashes.
    void AppendAutonomousInteractions(ServiceRefVector *list, uint32_t functionMask);

    // 0x44ff80 (ret 8): the menu gather. One interaction per entry of the stack object's table (source kind 0x32), run through
    // AppendIfPasses when the Sim may take it (an interrupt only when the current interaction allows it and few actions are
    // queued; others when something pending is below priority 0x32 or fewer than 7 pending interactions have a group); stops after
    // the first listed one when `stopAtFirst` is set. With the debug switch on, a stack object that allows it also gets the
    // "Dump Happy Log" (only for object kind 2) and "Force Error" cheat interactions. Interactions between this Sim and a Sim
    // stack object follow (SetUpBetween, both directions) when the current interaction's table entry is Sim-only. Publishes
    // itself and the list in the globals 0x10da960 / 0x10da964 while it runs (0x4526a0 reads them); clears the first at the end.
    void AppendInteractionsForMenu(ServiceRefVector *list, uint32_t stopAtFirst);

    // 0x4519c0 (ret 0x10): runs the check tree of `interaction` (RunCheckTree with call mode 2, the given recorder, run tree 1,
    // `extraArgument`, no bypass) with the interaction published as the UI manager (0x10da968, cleared afterwards), and lists it
    // (counted) when the tree did not list anything itself, the interaction is not flag 0x10 and it passed.
    void AppendIfPasses(ServiceRefVector *list, void *interaction, void *recorder, uint32_t extraArgument);
};

// 0x44f760 (__cdecl, seven stack arguments): makes a cheat interaction (SetUp with the entry index and source kind, no extra), gives
// it the name and the menu string id, sets its check-passed flag and lists it.
void __cdecl AddCheatInteraction(ServiceRefVector *list, void *person, void *stackObject, int16_t entryIndex, uint32_t sourceKind,
                                 const char *name, uint32_t menuStringId);

static_assert(offsetof(cEdithObjectTestSim, mPerson) == 0x08, "cEdithObjectTestSim layout");
static_assert(offsetof(cEdithObjectTestSim, mFlagA) == 0x10, "cEdithObjectTestSim layout");
static_assert(offsetof(cEdithObjectTestSim, mEntryIndexes) == 0x14, "cEdithObjectTestSim layout");
static_assert(offsetof(cEdithObjectTestSim, mSavedArguments) == 0x18, "cEdithObjectTestSim layout");
static_assert(offsetof(cEdithObjectTestSim, mSavedFor) == 0x28, "cEdithObjectTestSim layout");
static_assert(sizeof(cEdithObjectTestSim) == 0x2c, "cEdithObjectTestSim layout");

// The test sim as the primitives keep it in their frames: constructed where the frame declares it and destroyed on every way
// out of the frame (also while an error unwinds), unless Destroy() ended its life earlier, which the frame's code writes out
// on some paths.
class TestSimScope {
public:
    TestSimScope() { mSim.Construct(); }
    TestSimScope(const TestSimScope &) = delete;
    TestSimScope &operator=(const TestSimScope &) = delete;
    ~TestSimScope()
    {
        if (mAlive) mSim.Destruct();
    }
    void Destroy()
    {
        mSim.Destruct();
        mAlive = false;
    }
    cEdithObjectTestSim *operator->() { return &mSim; }

private:
    cEdithObjectTestSim mSim;
    bool mAlive = true;
};
