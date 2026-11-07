#pragma once
#include "game/vtable.h"
#include "simantics/TreeFrame.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// A reference to a frame as the stack's vector holds it: the game's smart-pointer element (copying adds a reference,
// destroying gives one back), so the vector's reallocation copies every element and releases the old ones exactly like
// the original's, call for call. Deliberately has no move constructor.
class TreeFrameRef {
public:
    TreeFrameRef() = default;
    explicit TreeFrameRef(cTSTreeStackElem *frame) : mFrame(frame) { Add(); }
    TreeFrameRef(const TreeFrameRef &other) : mFrame(other.mFrame) { Add(); }
    TreeFrameRef &operator=(const TreeFrameRef &other)
    {
        if (mFrame != other.mFrame) {
            cTSTreeStackElem *old = mFrame;
            mFrame = other.mFrame;
            Add();
            if (old) Give(old);
        }
        return *this;
    }
    ~TreeFrameRef()
    {
        if (mFrame) Give(mFrame);
    }
    cTSTreeStackElem *get() const { return mFrame; }

private:
    using RefFn = uint32_t(__thiscall *)(void *);
    void Add()
    {
        if (mFrame) VirtualCall<RefFn, 1>(mFrame);
    }
    static void Give(cTSTreeStackElem *frame) { VirtualCall<RefFn, 2>(frame); }

    cTSTreeStackElem *mFrame = nullptr;
};

static_assert(sizeof(TreeFrameRef) == 4, "TreeFrameRef is a pointer");

// cTSTreeStack (1.18.0.187): the call stack of a cTSTreeSim, a vector of frames (cTSTreeStackElem) in an embedded
// object at TreeSim+0x18. Implements cITSTreeStack (iid 0x7abea0d5; vtable 0xdd77cc, 17 slots). The object is
// 0x10 bytes, a vtable and the vector (MSVC std::vector layout: first, last, end of storage); it is not reference
// counted (AddRef/Release are the shared `return 1`). The stack holds one reference per pushed frame.
//
// The game throws the 32-bit enum below when it looks for the top frame of an empty stack (ThrowInfo 0xf803fc);
// the type name matters (handlers catch it by type), which is why the enum is unnamed inside this class.
class cTSTreeStack {
public:
    enum { kStackErrorBase = 0x3e8, kStackErrorNoFrame = 0x3ea };

    static constexpr uint32_t kVTable = 0xdd77cc;
    static constexpr uint32_t kSize = 0x10;
    static constexpr uint32_t kIidUnknown = 1;
    static constexpr uint32_t kIidStack = 0x7abea0d5;
    // The error code Pop throws (as a 16-bit value) when there is no frame to remove.
    static constexpr int16_t kErrorEmpty = 0x3e9;

    bool QueryInterface(uint32_t iid, void **out);   // slot 0, 0x577b20
    void Reset(uint32_t reserve);                    // slot 3, 0x577850: Clear, then room for `reserve` frames
    uint8_t NewFrame(uint32_t iid, void **out);      // slot 4, 0x576200
    void Push(cTSTreeStackElem *frame, uint32_t unused1, void *unused2); // slot 5, 0x577a70
    void Pop(uint32_t unused1, void *unused2);       // slot 6, 0x577a00
    void Clear();                                    // slot 7, 0x577e60
    cTSTreeStackElem *GetAt(int32_t index);          // slots 8 and 9, 0x577390
    uint8_t Write(void *stream, void *argument);     // slot 11, 0x5784c0
    uint8_t Read(void *stream, void *ownerReader, uint32_t defaultWord); // slot 12, 0x577b80
    cTSTreeStack *Destroy(uint32_t flags);           // slot 16, 0x575d40 (deleting destructor)

    // Shared with unrelated classes and left to the original: Count (slot 10, 0x16db20).
    uint32_t Size() const { return static_cast<uint32_t>(mFrames.size()); }

    const void *mVTable;            // +0x00
    std::vector<TreeFrameRef> mFrames; // +0x04 (each element holds a reference to its frame)
};

static_assert(sizeof(cTSTreeStack) == 0x10, "cTSTreeStack layout");
static_assert(offsetof(cTSTreeStack, mFrames) == 4, "cTSTreeStack layout");
