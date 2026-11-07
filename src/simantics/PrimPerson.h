#pragma once
#include "simantics/PrimExpression.h"
#include "simantics/PrimHandlers.h"
#include "simantics/FindBestAction.h"
#include "simantics/RefCountedDeque.h"
#include "simantics/ServiceRefVector.h"
#include "simantics/cTSBehaviorTree.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The person's own SimAntics primitive handlers (1.18.0.187), lane Y: the functions cTSPerson::vf332 (0x51e4c0,
// src/simantics/PrimDispatch.cpp) calls for opcodes 0x03, 0x11, 0x1d, 0x1e (selector 0) and 0x25,
// `handler(this = the person, frame, param)` (__thiscall, ret 8). Bad operands are reported by throwing a 16-bit code
// (`throw (short)code`). The person is a cTSPerson: its primary vtable is 0xdced48, the cTSTreeSim subobject sits at
// +4 (slot 7 reads an operand, slot 32 is the "post state" call), the cITSPerson subobject at +0x2f0 (the object
// inside whose vtable slot calls `this` is this + 0x2f0), the environment at +0x40 (slot 10 and 26 give list
// objects) and a second holder at +0x48.
//
// Only the fields the handlers touch are named; the rest is padding. The object is never constructed from this.
// 0x4529c0 (__cdecl, lane AD): stores the UI manager in game global 0x10da968 (the person primitives clear it with 0).
void __cdecl SetUiManager(void *manager);

// cTSPerson::cMotiveInc (vtable 0xdced28): the 16-byte element of the person's motive-increment vector (+0x5ec).
// Its copy constructor writes the vtable, the id word and the two floats, never the word at +6.
struct MotiveInc {
    const void *mVTable;
    uint16_t mMotive;
    uint16_t mPad; // not written by the constructor
    float mRate;
    float mTarget;
};
static_assert(sizeof(MotiveInc) == 0x10, "MotiveInc layout");

// The person's std::vector<cMotiveInc> (begin, end, limit).
struct MotiveIncVector {
    MotiveInc *mFirst;
    MotiveInc *mLast;
    MotiveInc *mEnd;
};

// 0x50da10 (233 bytes, __thiscall ret 8): std::vector<cMotiveInc>::_Emplace_reallocate. Inserts a copy of *value
// before `where` into a new block grown by the library's rule (1.5 times the capacity, at least one more element),
// copies the old elements around it, frees the old block and returns the new element.
MotiveInc *__fastcall MotiveEmplaceReallocate(MotiveIncVector *self, void *, MotiveInc *where, const MotiveInc *value);
// 0x50f4d0 (132 bytes, __thiscall ret 4): std::vector<cMotiveInc>::_Reallocate_exactly (reserve's worker): moves the
// elements into a block of exactly *newCapacity elements and frees the old one.
void __fastcall MotiveReallocateExactly(MotiveIncVector *self, void *, const uint32_t *newCapacity);
// 0x524d30 (649 bytes, __thiscall ret 0x10): std::vector<cMotiveInc>::insert(where, count, value); stores the iterator
// to the first inserted element in *out and returns out. cTSPerson::Read 0x51e9d0 sizes the loaded vector with it.
MotiveInc **__fastcall MotiveInsertN(MotiveIncVector *self, void *, MotiveInc **out, MotiveInc *where, uint32_t count, const MotiveInc *value);

struct PrimPersonObject {
    void **mVTable;                    // +0x000 primary vtable (0xdced48)
    void **mTreeSimVTable;             // +0x004 vtable of the cTSTreeSim subobject
    uint8_t mPad08[0x18 - 0x08];
    uint32_t mSearchFlag18;            // +0x18 cleared when a FindBestAction search starts (meaning not established)
    uint8_t mPad1c[0x40 - 0x1c];
    void *mEnvironment;                // +0x040 slot 10 / 26: list objects (slot 29 of those: object by id)
    uint8_t mPad44[4];
    void *mHolder48;                   // +0x048 slot 67 gives an object the handler 0x03 toggles
    uint8_t mPad4c[0x70 - 0x4c];
    uint8_t mBusy;                     // +0x070 byte flag (Prim03 sets it while it runs)
    uint8_t mPad71[0x1c4 - 0x71];
    ObjectRegistry *mObjectRegistry;   // +0x1c4 the table of all objects by number (1-based)
    uint8_t mPad1c8[0x2c8 - 0x1c8];
    void *mClock;                      // +0x2c8 the world clock (slot 18: the current tick)
    uint8_t mPad2cc[0x2f0 - 0x2cc];
    void **mPersonVTable;              // +0x2f0 vtable of the cITSPerson subobject (this + 0x2f0 is its `this`)
    uint8_t mPad2f4[4];
    // ---- the FindBestAction search state (lane AF; see FindBestAction.h) ----
    std::vector<ActionRecord> mCandidates;      // +0x2f8 the checked interactions of the last search (empty: none found)
    int32_t mSearchTick;                        // +0x304 the sim tick the search ran in (the tick source's slot 18)
    float mSearchTime;                          // +0x308 the +0x60c object's slot 6 at that moment
    uint8_t mHasChoice;                         // +0x30c 1 once an action was chosen
    uint8_t mPad30d[3];
    int32_t mChosenInput;                       // +0x310 the input value (+0x83c) when the choice was made
    std::vector<ScoreRecord> mCandidateScores;  // +0x314 one per candidate, sorted with them
    SearchSnapshot mSnapshotA;                  // +0x320 assigned from the object at +0x814
    SearchSnapshot mSnapshotB;                  // +0x3ac filled by the +0x60c object's slot 9
    int32_t mLifeStageAtSearch;                 // +0x438 the cITSPerson owner's slot 36, sign-extended
    std::vector<ActionRecord> mRejected;        // +0x43c rejected candidates, kept while score recording is on
    uint8_t mPad448[0x458 - 0x448];
    std::vector<ScoreRecord> mRejectedScores;   // +0x458 one per rejected candidate
    uint8_t mPad464[0x580 - 0x464];
    RecentActionVector mRecentActions;   // +0x580 the memory of recently started actions (newest expiry first)
    RefPtrVector *mRefsAInUse;                  // +0x58c Prim03's reference vectors A and B while a search runs; cleared at its end
    RefPtrVector *mRefsBInUse;                  // +0x590
    // +0x594 and +0x5a8: two std::deques of reference-counted cTSInteraction pointers (RefCountedDeque: proxy +0, map +4,
    // map size +8, offset +0xc, size +0x10). The Person Data fields 0x1a (queue size) and 0x4d/0x4e (the head's id and
    // stack object id) mirror the one at +0x5a8, the Sim's visible action queue; the one at +0x594 holds the entries the
    // idle poll takes next (Person Data 0x2d, 0x48, 0x4f mirror its head).
    uint8_t mQueueA[0x14];             // +0x594..+0x5a7 pending queue (size dword at +0x5a4)
    uint8_t mQueueB[0x14];             // +0x5a8..+0x5bb action queue (map +0x5ac, map size +0x5b0, offset +0x5b4, size +0x5b8)
    void *mCurrentInteraction;         // +0x5bc the interaction being performed (cITSPerson slot 132); a reference holder
    void *mPreviousInteraction;        // +0x5c0 the interaction it replaced; a reference holder
    void *mSelectedInteraction;        // +0x5c4 the interaction chosen from the queue (cITSPerson slot 133); a reference holder
    uint8_t mHistory[0x14];            // +0x5c8..+0x5db RefCountedDeque: finished interactions (IdleForInput appends the current one)
    uint8_t mPad5dc[4];
    uint8_t *mReactionBegin;           // +0x5e0 vector of 12-byte elements (begin / end; the count / 12 goes to +0x618 / +0x61a)
    uint8_t *mReactionEnd;             // +0x5e4
    uint8_t mPad5e8[4];
    uint8_t *mMotiveBegin;             // +0x5ec the vector of cMotiveInc (16 bytes each: vtable, id word, two floats)
    uint8_t *mMotiveEnd;               // +0x5f0
    uint8_t *mMotiveLimit;             // +0x5f4
    uint8_t mPad5f8[0x60c - 0x5f8];
    void **mObject60cVTable;           // +0x60c an embedded object (its vtable); slot 8 reads the next input
    uint8_t mPad610[0x618 - 0x610];
    uint16_t mReactionCountSelected;   // +0x618 (mReactionEnd - mReactionBegin) / 12 after an interaction was selected; cleared with the action queue
    uint16_t mReactionCountCurrent;    // +0x61a the same after an interaction was started
    uint8_t mPad61c[0x814 - 0x61c];
    SearchSnapshot *mSnapshotSource;   // +0x814 the object 0x4365d0 copies into mSnapshotA when a search starts
    void *mFrame;                      // +0x818 Prim03's context: the frame ...
    uint8_t mFlag81c;                  // +0x81c bits 1..5 of param byte +7
    uint8_t mFlag81d;                  // +0x81d
    uint8_t mRespectCurrentInteraction;                  // +0x81e
    uint8_t mSameObjectAsCurrent;                  // +0x81f
    uint32_t mInput820;                // +0x820 the param word +4 (the input to wait for; 0: poll)
    uint8_t mUseNodeObject;                  // +0x824 bit 5 of param byte +7
    uint8_t mPad825[3];
    void *mScoringObject;                   // +0x828 this, or the object slot 29 gives
    uint8_t mLoopFlag;                 // +0x82c 1 while Prim03's loop runs
    uint8_t mPad82d[3];
    uint32_t mResult830;               // +0x830 the last input value
    int32_t mCheckedCount;             // +0x834 interactions whose check tree ran in this wait (Prim03 zeroes it)
    uint32_t mAcceptedCount;            // +0x838 interactions accepted for scoring (Prim03 zeroes it)
    uint32_t mCheckTreeRuns;            // +0x83c check-tree runs of the search (cleared when a search starts)
    ServiceRefVector mPending;         // +0x840 the interactions gathered for the search (cTSInteraction, counted)
    std::vector<ActionRecord> mChoices; // +0x84c the scored actions in the order they are tried (sorted by score)
    uint8_t mPad858[0x868 - 0x858];
    ActionRecord *mChosen;             // +0x868 the record chosen (an element of mChoices), null if none
    RefPtrVector mRefsA;               // +0x86c reference-counted pointers (the actions to flush)
    RefPtrVector mRefsB;               // +0x878 a parallel vector
    uint8_t mPad884[0x8b8 - 0x884];
    EmbeddedString mActionName;        // +0x8b8 the chosen action's name; passed by address to the new interaction's slot 26
    uint32_t mActionParameterA;        // +0x8d8 the chosen action's parameters (ActionRecord::mParameterA..C)
    uint32_t mActionParameterB;        // +0x8dc
    int32_t mActionParameterC;         // +0x8e0
    uint16_t mActionArguments[8];      // +0x8e4 the chosen action's eight tree arguments; passed by address (slot 248's second argument, the new interaction's slot 27)

    // 0x521860 (801 bytes), opcode 3. Remembers the node's operands in the context block (+0x818..+0x838), then runs
    // the "wait for input" loop: while Helper 0x515940 allows it, it reads the next input through the +0x60c object (slot 8)
    // until the input handler or the list state ends it, flushing the pending reactions (0x5155b0, 0x516c40 and the
    // pairs of vector A and B through 0x516160, then 0x5150d0). Returns the value 0x515270 gives, or 0 when 0x515940
    // refused. Always clears +0x58c/+0x590, both reference vectors and the global 0x10da968.
    uint32_t Prim03(void *frame, const PrimParamView *param);

    // 0x522110 (418 bytes), vtable slot 361, opcode 0x1d. Bit 0 of param byte +7 clears the cMotiveInc vector
    // (+0x5ec). Otherwise the motive id for param byte +6 (0, 1, 5..9, 14, 15; anything else throws 0x1a) goes to
    // the +0x2f0 object's slot 11 when bit 1 is set, and a cMotiveInc (vtable 0xdced28, id = byte +6, two floats:
    // the first operand / 60 / 30 and the second operand as float) is appended to the vector. Returns 1.
    uint32_t Prim1D(void *frame, const PrimParamView *param);

    // 0x522300 (49 bytes), vtable slot 362, opcode 0x25. The +0x2f0 object's slot 137 is asked about the frame's
    // slot 12 word; returns 1 when it says yes, else 0.
    uint32_t Prim25(void *frame, const PrimParamView *param);

    // 0x51da50 (226 bytes), vtable slot 198 (cTSObject's 0x5342d0 is the base; meaning tentative: (re)initialisation).
    // Runs the base with the argument, reserves 512 candidates (+0x2f8) and 2 motive increments (+0x5ec), clears Prim03's
    // reference-vector pointers, attaches the person to the +0x60c motive object, prepares the +0x48 object's helper,
    // adds the person to the +0x40 object's list and applies the pet scale. See PersonInit.cpp.
    void Initialise198(uint32_t argument);

    // 0x525520 (901 bytes), opcode 0x1e with selector 0 (the dispatch has checked it). Throws 0x15 without a stack
    // object, looks up the stack object's person (0 when none, or without a slot-87 list), creates an interaction
    // object through the director (class 0xfc5ecc3d, interface 0x5aca6692), fills it from the person's fields
    // (+0x8b8..+0x8e4), may bump a counter on the owner's table, and always returns 3 after releasing it.
    // Errors thrown: 0x15, 0x91, 0x16, 0x44f.
    uint32_t Prim1E0(void *frame, const PrimParamView *param);

    // 0x521b90 (1,404 bytes), opcode 0x11 IdleForInput. See PrimPersonIdle.cpp.
    uint32_t IdleForInput(void *frame, const PrimParamView *param);

    // ---- the person's interaction bookkeeping (lane AD; src/simantics/PersonInteractions.cpp) -----------------------------
    // `interaction` is a cTSInteraction; Person Data is written through the cITSPerson subobject (+0x2f0).

    RefCountedDeque &QueueA() { return *reinterpret_cast<RefCountedDeque *>(mQueueA); }
    RefCountedDeque &QueueB() { return *reinterpret_cast<RefCountedDeque *>(mQueueB); }
    RefCountedDeque &History() { return *reinterpret_cast<RefCountedDeque *>(mHistory); }

    // 0x512c30: tells every interaction in the action queue (+0x5a8) that no person owns it any more (slot 67 with this
    // person's cITSPerson, rereading the size after each call because an interaction may remove itself), empties the
    // queue, and resets the Person Data that mirrors it (queue size, head ids, selected interaction, +0x618).
    void ClearActionQueue();

    // 0x5120c0 (ret 4): hands `interaction` this person (its slot 67) and tells its stack object (slot 238) that event 4
    // happened to this person's object id.
    void RejectInteraction(void *interaction);

    // 0x520e40: Person Data 0x1a / 0x4d / 0x4e from the action queue's size and head, 0x4b / 0x4c from the current
    // interaction (cITSPerson slot 132) and 0x66 / 0x67 from the selected one (slot 133; the current one when there is no
    // selection; zero when neither exists).
    void PublishActionQueue();

    // 0x5208a0: Person Data 0x2d / 0x48 / 0x4f from the head of the pending queue (+0x594; zero when it is empty) and 0x6d
    // from the current interaction.
    void PublishPendingQueue();

    // 0x5205c0 (ret 4): makes `interaction` the selected one (+0x5c4, with the reference counts), publishes it in Person
    // Data 0x9d / 0x27 / 0x49 / 0x47 / 0x6e / 0x6d, empties the motive vector when its table entry is initialised, then
    // PublishActionQueue and the element count at +0x618.
    void SelectInteraction(void *interaction);

    // 0x520360 (ret 4): makes `interaction` the current one (+0x5bc; the old one becomes the previous one, +0x5c0, and gets
    // the tree sim's last branch), publishes it in Person Data 0x15 / 0x50 / 0xa4 / 0x27 / 0x21 / 0x66 / 0x67 / 0xa6 / 0x49 /
    // 0x47 / 0x6e, empties the motive vector, then ClearActionQueue, PublishPendingQueue and the element count at +0x61a.
    void StartInteraction(void *interaction);

    // ---- FindBestAction (lane AF; src/simantics/FindBestAction.cpp) ----------------------------------------------------------

    // 0x5150d0: ranks the scored actions (mChoices, best first; with score recording also mCandidates and mCandidateScores),
    // picks the rank to take (0 for a player-controlled choice, else a random one among the best ten) and walks down the list
    // checking the interactions that have not been checked yet. The last accepted action of that walk becomes mChosen,
    // mHasChoice is set and mChosenInput remembers the check count. With score recording on, the walk goes on to the end and
    // the trace log's file is closed. Returns nothing (the original's eax is leftover).
    void ChooseAction();

    // 0x515940: begins a FindBestAction search. Clears the person's attribute 0x14 and Person Data 0x35, and refuses (0) unless
    // the autonomy conditions hold, the input is not blocked and this tick has no search yet. Otherwise forgets the last search
    // (candidates, pending list, choices, reference vectors, the chosen record), stores the tick and time, snapshots the state when score
    // recording is on (and starts the check-tree log), expires and trims the recent-action memory (at most tuning 0x1014524
    // entries, newest expiry first), tells the person it is searching and returns 1.
    bool BeginSearch();

    // 0x5155b0: fills the pending list (mPending) with the interactions the Sim may start on the objects that offer the function
    // in mResult830 (+0x830) to its life stage: the object module's interaction groups, each stack object looked up again by
    // number and kept when it is available, not hidden, not busy and its state attribute (0x38) has neither bit 0 nor bit 2.
    // Unless mFlag81c is set (then only the object the frame's first argument names is taken, plus the ones an unavailable
    // check lets through with mFlag81d), the stack object's owner id must also be in the set of ids the Sim's cITSPerson gives
    // (when it gives one). Each kept object's interactions are appended by the test sim (the autonomy gather, flag A).
    void GatherInteractions();

    // 0x516160 (ret 0x14): turns `interaction` (a cTSInteraction, with its table entry and the kind's preference table, a
    // SimRowTable) into an ActionRecord with a score, appended to mCandidates and mChoices, unless the score is below the
    // threshold (0.2, or 0.05 for a person that takes the best only) or 1e-5; those go to mRejected when score recording is
    // on. With recording on a ScoreRecord of how the score came about is kept too (mCandidateScores / mRejectedScores).
    // `gathered`: the interaction came from the gathered list rather than the person's own reaction list; `checked`: its
    // check tree has run already (stored in the record). The score is the motive score minus the time, times the recent-action
    // discount, the proximity, 1 + the object bonus, the interaction factor, the skill bonus multiplier (for a Sim stack
    // object whose kind row has the bonus flag), the stack object's score factor and the person factor, one at a time.
    void ScoreInteraction(void *interaction, void *tableEntry, void *preferences, uint8_t gathered, uint8_t checked);

    // 0x516c40: walks the gathered interactions (mPending), drops the ones the person may not take now (its own entry does not
    // allow it on itself, the current interaction does not allow the interruption, the entry was used lately), notes the use,
    // and scores the rest (checking first those whose tree flags ask for it).
    void ScorePending();

    // 0x515c80 (ret 4): runs the check tree of `interaction` (a cTSInteraction) for this person with a test sim and tells
    // whether the interaction is acceptable: it passed its check, is not vetoed afterwards, and no action string was made
    // by the tree. Counts the run (+0x83c) and the check (+0x834). With score recording on, a rejected interaction leaves an
    // ActionRecord and a ScoreRecord in mRejected / mRejectedScores (so the log can say why it was not taken). Also points
    // the UI manager (0x10da968) at the interaction and clears the action-string flag before the run.
    bool CheckInteraction(void *interaction);

    // 0x515270: starts the chosen action (mChosen) unless a minimum-score mode refuses it: the person's attribute 0x14 takes its
    // entry index, the tree frame its stack object, Person Data 0x35 the stack object id, the +0x8b8..+0x8f3 block its name,
    // parameters and eight arguments (all zero when the fourth argument word is 0xffff), and the recent-action memory a new
    // entry that expires after tuning 0x1014528 ticks. Returns 1 when started, 0 when there is no choice or it is refused.
    uint32_t StartChosenAction();

    // 0x523da0 (ret 4): when `interaction` is on a non-Sim stack object whose attribute table has flag 0x19 set, bumps the
    // use counter (capped at 100) this Sim keeps for it in the stack object's skill list.
    void CountInteractionUse(void *interaction);
};
static_assert(offsetof(PrimPersonObject, mEnvironment) == 0x40, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mHolder48) == 0x48, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mBusy) == 0x70, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mPersonVTable) == 0x2f0, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mCandidates) == 0x2f8, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mSearchTick) == 0x304, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mCandidateScores) == 0x314, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mSnapshotA) == 0x320, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mSnapshotB) == 0x3ac, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mLifeStageAtSearch) == 0x438, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mRejected) == 0x43c, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mRejectedScores) == 0x458, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mRecentActions) == 0x580, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mRefsAInUse) == 0x58c, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mPending) == 0x840, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mChoices) == 0x84c, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mChosen) == 0x868, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mSnapshotSource) == 0x814, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mQueueA) == 0x594, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mQueueB) == 0x5a8, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mCurrentInteraction) == 0x5bc, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mHistory) == 0x5c8, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mReactionBegin) == 0x5e0, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mReactionCountSelected) == 0x618, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mReactionCountCurrent) == 0x61a, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mMotiveBegin) == 0x5ec, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mObject60cVTable) == 0x60c, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mFrame) == 0x818, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mInput820) == 0x820, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mResult830) == 0x830, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mRefsA) == 0x86c, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mRefsB) == 0x878, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mActionName) == 0x8b8, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mActionParameterA) == 0x8d8, "PrimPersonObject layout");
static_assert(offsetof(PrimPersonObject, mActionArguments) == 0x8e4, "PrimPersonObject layout");
