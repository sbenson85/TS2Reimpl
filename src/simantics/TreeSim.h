#pragma once
#include "rz/RZFrameworkHelpers.h"
#include "simantics/TreeFrame.h"
#include "simantics/TreeReplay.h"
#include "simantics/TreeStack.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// cTSTreeSim (1.18.0.187): the SimAntics interpreter state of an object, the base of cEdithObject / cTSObject /
// cTSPerson (the object embeds it as a subobject at +4, so `this` here is the object + 4). It implements cITSTreeSim
// (iid 0x3ac12b12; vtable 0xdd7814, 38 slots) plus a cRZUnknown subobject at +4 (vtable 0xdd78b0, reference count at
// +8). The object is 0x70 bytes. The slots 6, 7 and 8 are pure virtual here; cTSObject supplies RunNode (6: executes
// one primitive node), ReadOperand (7: the operand reader, SimOperandSource::Read) and HandleError (8).
//
//   +0x00 vtable            +0x04 cRZUnknown {vtable, reference count}
//   +0x0c u32 step count    nodes executed since the last CallTree started (limit: global 0x1016914)
//   +0x10 u32 step base     the count saved when the limit is raised (see DoNodeAction)
//   +0x14 u32 never written (0)
//   +0x18 the call stack (cTSTreeStack, 0x10 bytes)
//   +0x28 u32 the last jump target taken (0xfffc at construction; 0xfffd means "returned true")
//   +0x2c u8  the last branch: 1 after a true target, 0 after a false one
//   +0x30 u16* default argument block for PushTree (slot 16 sets it)
//   +0x34 u16 error code (a thrown short is stored here by CallTree / RunToCompletion; nonzero blocks CallTree)
//   +0x36 u16 result (CallTree stores its result argument)
//   +0x38 u16 call depth (CallTree adds 1 while it runs)
//   +0x3c service context (GetCachedSingleton 0x20ddfb; the object's +0x40)
//   +0x40 debugger service (0x20ddda, may be null)
//   +0x44 service C (GetCachedSingletonC)
//   +0x48 the context's slot-3 object ("gate"): its slots 22/23 hold the run state CallTree and RunToCompletion set
//   +0x4c the record/replay table (TreeReplay.h: an MSVC hash_map of recorded results, 8-byte keys, values vectors of
//         0x1c-byte rows)
//   +0x6c u8  record/replay switch (0 from the constructor)
// The operand block of a node (an XPrimParam, SimAnticsParams.h) seen by the interpreter: tree-call nodes keep their
// arguments as operand descriptors (kind byte + 16-bit id) from +4, and 0xff bytes there mean "no arguments".
struct TreeNodeParam {
    const void *mVTable;  // +0
    uint32_t mWord4;      // +4
    uint32_t mWord8;      // +8
    bool HasNoOperands() const;  // 0x577990: bytes +4..+0xb are all 0xff
};

class cTSTreeSim {
public:
    static constexpr uint32_t kVTable = 0xdd7814;
    static constexpr uint32_t kUnknownVTable = 0xdd78b0;
    static constexpr uint32_t kSize = 0x70;
    static constexpr uint32_t kIidTreeSim = 0x3ac12b12;
    static constexpr uint32_t kIidFrame = cTSTreeStackElem::kIidFrame;

    // Globals the interpreter shares: the step limit (100000, or 300000 when option 0x33 of the context's gate is
    // set), the largest stack depth (0x1016918) and the "running" byte CallTree clears (0x1016910).
    static constexpr uint32_t kStepLimitGlobal = 0x1016914;
    static constexpr uint32_t kMaxDepthGlobal = 0x1016918;
    static constexpr uint32_t kRunFlagGlobal = 0x1016910;
    static constexpr int32_t kStepLimitNormal = 100000;
    static constexpr int32_t kStepLimitRaised = 300000;
    static constexpr uint32_t kGateOptionSlowLimit = 0x33;

    // Error codes thrown (as 16-bit values) and stored.
    static constexpr int16_t kErrorNoTreeEnd = 0x3e8;    // PushTree failed although the tree exists
    static constexpr int16_t kErrorStackTooSmall = 0x3e9;
    static constexpr int16_t kErrorNoTree = 0x11;        // the called tree id is unknown
    static constexpr int16_t kErrorStepLimit = 0x44d;
    static constexpr int16_t kErrorNoOwner = 0x44e;
    static constexpr int16_t kErrorNoNode = 0x44f;
    static constexpr int16_t kErrorBadTarget = 0x44c;
    static constexpr int16_t kErrorNoCalledTree = 0x450;
    static constexpr int16_t kErrorYielded = 0x451;
    static constexpr int16_t kErrorStackDepth = 0x452;

    // Special jump targets.
    static constexpr uint32_t kTargetNone = 0xfffc;
    static constexpr uint32_t kTargetTrue = 0xfffd;
    static constexpr uint32_t kTargetFalse = 0xfffe;

    // DoNodeAction results.
    static constexpr uint32_t kStepContinue = 0;
    static constexpr uint32_t kStepStackEmpty = 1;
    static constexpr uint32_t kStepYield = 2;

    cTSTreeSim *Construct();                        // 0x5755e0
    void Destruct();                                // 0x5759f0 (the body derived destructors call)
    cTSTreeSim *DeletingDestructor(uint32_t flags); // 0x575c30 (reached through the subobject thunk 0x575c28)
    bool QueryInterface(uint32_t iid, void **out);  // slot 0, 0x577af0 (and the subobject thunk 0x577b17)
    uint8_t NewFrame(uint32_t iid, void **out);     // slot 5, 0x5761f0
    void GetPosition(uint16_t *treeId, uint16_t *nodeIndex);  // slot 10, 0x577060
    uint32_t StartTree(void *owner, uint32_t treeId, uint32_t value);  // slot 11, 0x577e10
    uint8_t PushTree(void *owner, uint16_t *args, uint32_t treeId, uint32_t flag16, uint32_t word18, uint32_t word1a,
                     uint32_t word1c);  // slot 12, 0x577460
    uint32_t DoNodeAction(void *frame, uint32_t unused);  // slot 13, 0x5762d0
    uint32_t ResumeFrame(void *frame, void *node);     // slot 14, 0x577810
    uint8_t CallTree(void *owner, uint32_t word1a, uint32_t treeId, uint32_t word18, uint32_t word1c, uint16_t *args,
                     uint32_t result, uint32_t flag16);  // slot 15, 0x577e80
    void PrepareStack(uint32_t reserve, uint16_t *defaultArgs);  // slot 16, 0x5779d0
    void ClearErrorCode();                          // slot 18, 0x5761d0
    void *FindOwnerBaseFrame();                     // slot 19, 0x577220
    void *GetTopFrame();                            // slot 20, 0x577040
    void *FindLastRealFrame();                      // slot 21, 0x5772e0
    void *GetFrameAt(int32_t index);                // slot 22, 0x577370
    uint32_t GetCurrentOpcode();                    // slot 24, 0x5770e0
    uint32_t GetResult();                           // slot 26, 0x5773f0
    void ClearResult();                             // slot 27, 0x577e70
    uint8_t ReturnedTrue();                         // slot 28, 0x5772b0
    int32_t GetStepLimit();                         // slot 30, 0x577360
    void SetStepLimit(int32_t limit);               // slot 31, 0x5782d0
    void ClearReplayCache(uint32_t unused);         // slot 32, 0x5761e0
    uint8_t GetRunFlag();                           // slot 33, 0x5779b0
    uint8_t RunToCompletion(uint32_t, uint32_t, uint32_t);  // slot 36, 0x577890
    void Detach();                                  // slot 37, 0x578450

    // The record/replay of tree results (trees flagged 8; only with the +0x6c switch on), TreeSimReplay.cpp.
    // Records the branch (and the object's argument words, per the cache mask) the tree that just returned took; `frame`
    // is that tree's frame. 0x575ed0.
    void RecordResult(uint32_t treeFlags, uint32_t cacheMask, uint32_t opcode, void *frame);
    // Looks for a recorded call of tree `opcode` with these arguments (the calling frame is the top of the stack): on a
    // hit stores the recorded branch in *out, restores the object's argument words and returns true. 0x576e70.
    bool ReplayResult(uint32_t treeFlags, uint32_t cacheMask, uint32_t opcode, uint32_t argCount, const uint16_t *args,
                      uint32_t *out, void *unused1, void *unused2);


    // Fields (see the layout above).
    const void *mVTable;          // +0x00
    RZUnknownLayout mUnknown;     // +0x04
    uint32_t mStepCount;          // +0x0c
    uint32_t mStepBase;           // +0x10
    uint32_t mField14;            // +0x14
    cTSTreeStack mStack;          // +0x18
    uint32_t mNextNode;           // +0x28
    uint8_t mLastBranch;          // +0x2c
    uint8_t mPad2d[3];            // +0x2d
    uint16_t *mDefaultArgs;       // +0x30
    uint16_t mErrorCode;          // +0x34
    uint16_t mResult;             // +0x36
    uint16_t mCallDepth;          // +0x38
    uint8_t mPad3a[2];            // +0x3a
    void *mContext;               // +0x3c
    void *mDebugService;          // +0x40
    void *mServiceC;              // +0x44
    void *mGate;                  // +0x48
    TreeReplayMap mReplayMap;     // +0x4c
    uint8_t mRecording;           // +0x6c
    uint8_t mPad6d[3];            // +0x6d
};

static_assert(sizeof(cTSTreeSim) == 0x70, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mStepCount) == 0xc, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mStack) == 0x18, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mNextNode) == 0x28, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mDefaultArgs) == 0x30, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mErrorCode) == 0x34, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mContext) == 0x3c, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mReplayMap) == 0x4c, "cTSTreeSim layout");
static_assert(offsetof(cTSTreeSim, mRecording) == 0x6c, "cTSTreeSim layout");
