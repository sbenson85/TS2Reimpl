#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// cTSTreeStackElem (1.18.0.187): one frame of the SimAntics call stack, the object the interpreter (cTSTreeSim,
// TreeSim.h) runs a BHAV tree on. Implements cITSTreeStackElem (iid 0x7a53b3dd; vtable 0xdd771c, 43 slots). Blocks
// are 0x3c bytes from the pool cRZFastAllocator<cTSTreeStackElem> (global allocator 0x1016994; the stack's NewFrame
// takes the block and initialises it, the deleting destructor gives it back).
//
//   +0x00 vtable
//   +0x04 reference count (the stack holds one reference per pushed frame)
//   +0x08 u16 tree id (BHAV id of the tree this frame runs; 0xffff for none, e.g. the bottom frame CallTree pushes).
//         Bit 15 is the pause bit (slots 22/23); the id proper is the low 15 bits.
//   +0x0a u16 stack object id (the id of the object the tree runs on; inherited by pushed frames)
//   +0x0c u16 node index (the node the frame is on; 0xfffd and 0xfffe mean "return true / false", 0xfffc is the
//         "no target" marker whose use throws 0x44c)
//   +0x0e   two bytes the creator never writes
//   +0x10 u32 resume state (0 when the frame has no pending callee; slot 19)
//   +0x14 u8 local count   +0x15 u8 argument count (lengths of the vectors at +0x30 and +0x24)
//   +0x16 u8 byte16        (never written by the creator; PushTree copies the caller's)
//   +0x18, +0x1a, +0x1c u16 words copied from the calling frame into every pushed frame (PushTree arguments 5..7);
//         the first two are what an object id lookup of the error path (CallTree) uses
//   +0x1e u8 byte1e
//   +0x20 owner: the scope object that resolves tree ids for the frame (TreeOwner, slot 23 GetTree)
//   +0x24 vector<u16> arguments    +0x30 vector<u16> locals    (zero-filled by SetArgCount / SetLocalCount)
//
// Slots below are written as members named by what they do; the vtable slot and RVA are in the comments. The slots
// that share a body with unrelated classes (the plain getters/setters of the +0x18/+0x1a/+0x1c words, the stack
// object id, the node index, resume state and owner accessors) stay with the original.
// The saved-game format version a stream says it carries (the frame and stack Read functions branch on it).
uint32_t TreeStreamVersion(void *stream);

class cTSTreeStackElem {
public:
    static constexpr uint32_t kVTable = 0xdd771c;
    static constexpr uint32_t kPool = 0x1016994; // the allocator object (+4 is its current page)
    static constexpr uint32_t kSize = 0x3c;
    static constexpr uint32_t kIidUnknown = 1;
    static constexpr uint32_t kIidFrame = 0x7a53b3dd;

    // Takes a block from the pool and initialises it the way the stack's NewFrame does (reference count 0).
    // Fields the original leaves alone (+0xe, +0xf, +0x16, +0x17, +0x1f) keep whatever the pool block held.
    static cTSTreeStackElem *NewBlock();

    bool QueryInterface(uint32_t iid, void **out);           // slot 0, 0x577b50
    int32_t GetTreeId();                                     // slot 9, 0x577400: -1 for 0xffff, else the low 15 bits
    void SetTreeId(uint32_t id);                             // slot 10, 0x578420
    void SetFlag16(uint32_t value);                          // slot 17, 0x578290
    bool IsPaused();                                         // slot 22, 0x576e50
    void SetPaused(uint8_t paused);                          // slot 23, 0x578260
    void SetByte1e(uint32_t value);                          // slot 25, 0x5782a0
    void SetArgCount(uint8_t count);                         // slot 28, 0x578350
    void SetLocalCount(uint8_t count);                       // slot 29, 0x5782e0
    uint32_t GetArg(int32_t index);                          // slot 30, 0x5773c0
    uint32_t GetLocal(int32_t index);                        // slot 31, 0x5772c0
    void SetArg(int32_t index, uint32_t value);              // slot 32, 0x5783c0
    void SetArgs(int32_t count, const uint16_t *values);     // slot 33, 0x5783e0
    void SetLocal(uint32_t index, uint32_t value);           // slot 34, 0x5782b0
    uint16_t *GetArgAddress(uint32_t index);                 // slot 35, 0x5779f0
    uint16_t *GetLocalAddress(uint32_t index);               // slot 36, 0x5779c0
    void GetTreeName(void *name);                            // slot 37, 0x577420: owner->GetTreeName(id, name), or "No behavior"
    bool Write(void *stream, void *ownerWriter);             // slot 38, 0x578520
    bool Read(void *stream, void *ownerReader, uint32_t defaultWord); // slot 39, 0x577cf0
    cTSTreeStackElem *Destroy(uint32_t flags);               // slot 42, 0x575d70 (deleting destructor)

    const void *mVTable;          // +0x00
    uint32_t mRefCount;           // +0x04
    uint16_t mTreeWord;           // +0x08 tree id | pause bit
    uint16_t mStackObjectId;      // +0x0a
    uint16_t mNodeIndex;          // +0x0c
    uint8_t mPad0e[2];            // +0x0e
    uint32_t mResumeState;        // +0x10
    uint8_t mLocalCount;          // +0x14
    uint8_t mArgCount;            // +0x15
    uint8_t mByte16;              // +0x16
    uint8_t mPad17;               // +0x17
    uint16_t mWord18;             // +0x18
    uint16_t mWord1a;             // +0x1a
    uint16_t mWord1c;             // +0x1c
    uint8_t mByte1e;              // +0x1e
    uint8_t mPad1f;               // +0x1f
    void *mOwner;                 // +0x20
    std::vector<uint16_t> mArgs;   // +0x24
    std::vector<uint16_t> mLocals; // +0x30
};

static_assert(sizeof(cTSTreeStackElem) == 0x3c, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mTreeWord) == 8, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mNodeIndex) == 0xc, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mResumeState) == 0x10, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mLocalCount) == 0x14, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mWord18) == 0x18, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mOwner) == 0x20, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mArgs) == 0x24, "cTSTreeStackElem layout");
static_assert(offsetof(cTSTreeStackElem, mLocals) == 0x30, "cTSTreeStackElem layout");
