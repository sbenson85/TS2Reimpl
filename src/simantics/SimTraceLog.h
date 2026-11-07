#pragma once
#include <cstdint>

// The SimAntics execution trace log (1.18.0.187): the global object at 0x10149e8 the interpreter (cTSTreeSim)
// writes to when it is switched on in the game's debug settings. Only the members the interpreter calls are here; the
// class has more (0x525ad0..0x5275c0 hold its other members). Output goes through the text sink object at +0x2c
// (slot 14 writes a string, slot 7 a 16-bit number, slot 9 an integer); indentation is one tab per nesting level
// (+4), raised by every traced tree call and lowered by every traced return.
//
//   +0x00 u8 enabled        +0x01 u8 active (both must be set: IsActive)
//   +0x02 u8 show node text (LogNode asks the debugger service for the node's text and prints it)
//   +0x04 i32 nesting depth
//   +0x2c text sink (null: nothing is written)
struct SimTraceLog {
    static constexpr uint32_t kGlobal = 0x10149e8;

    static SimTraceLog *Get();

    bool IsActive() const;                                              // 0x526fb0
    void LogCall(int32_t treeId, int32_t stackObjectId, const char *name);  // 0x526e20 (ret 0xc): "Gosub tree id ..."
    void LogErrored();                                                  // 0x526be0: "Tree Errored Out."
    void LogNode(void *frame, uint32_t isTrue);                         // 0x5270a0 (ret 8): the node the frame left
    void WriteIndent();                                                 // 0x527540: one tab per nesting level
    // 0x526fc0 (ret 0x10): the heading of an interaction check tree ("New Interaction checkTree." with the interaction
    // index, the stack object's id and name); the first argument is not used. Resets the nesting depth to -1.
    void LogInteractionCheckStart(int32_t unused, int32_t stackObjectId, const char *stackObjectName, int32_t interactionIndex);
    void LogInteractionCheckComplete();                                 // 0x526b60: "Interaction Check Tree Complete."
    // 0x526b40: closes the trace file (the sink's deleting destructor, slot 21 with 1), forgets the sink and clears the
    // active byte. Lane AF: the end of a FindBestAction search's check-tree log.
    void CloseSink();

    // 0x5273b0 (ret 0xc): opens the check-tree log of a FindBestAction search: the sink (a text file named
    // "ChecktreeLogging_<tick>_<name>.xls", created by the game's file class 0x1b4dd2 when there is none yet) gets a header row
    // ("Me Object", the name, "Id", the person's object id, "On Tick", the tick), and the log becomes active.
    void LogSearchStart(const char *name, int32_t tick, int32_t objectId);

private:
    void WriteNode(void *frame, uint32_t isTrue); // the body of LogNode (inside its catch (...))

public:

    uint8_t mEnabled;      // +0x00
    uint8_t mActive;       // +0x01
    uint8_t mShowNodeText; // +0x02
    uint8_t mPad03;        // +0x03
    int32_t mDepth;        // +0x04
    uint8_t mUnknown08[0x24]; // +0x08
    void *mSink;           // +0x2c
};

static_assert(sizeof(SimTraceLog) == 0x30, "SimTraceLog layout (known part)");
