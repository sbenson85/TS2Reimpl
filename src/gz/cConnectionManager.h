#pragma once
// nGZEvent::cConnectionManager and its iterators (lane LR, foundations F1), in the standalone shape
// (docs/standalone-shape.md). RVAs are 1.18.0.187.
//
// The event system's connection table. Connect(source, sourceEvent, target, targetEvent) records the connection twice:
// under the source in the "from" map (sourceEvent -> {target, targetEvent}) and under the target in the "to" map
// (targetEvent -> {source, sourceEvent}). Both are unordered maps keyed by the behaviour pointer (identity hash) holding
// multimaps keyed by event, so connections with one key keep their insertion order. Every object handed in is first
// asked for its behaviour face (cIAbstractBehaviorType, QueryInterface 0xfb859324; the object itself when it refuses,
// and the reference the answer adds is never released, as the original). Connecting and disconnecting tell both ends
// through their behaviour slot 3 with a cConnectionChangedEvent on the stack.
//
// The one instance is the event system service's function-local static (0x10cadd8, 0x44 bytes), built by the
// constructor 0x1aa630 and destroyed by 0x1aa69a at exit; both are hooked, and the game vtable 0xda233c is stored by
// nothing else, so the static carries our vtable. The iterators' game vtables (0xda2378, 0xda23bc) are stored only by
// the manager's slots 10 / 11 and the iterators' own members, all reached through objects our code made.
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>

namespace nGZEvent {

// The behaviour face every connected object is reduced to (nGZEvent::cIAbstractBehaviorType). Only slot 3 is called here.
class cIAbstractBehaviorType : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xfb859324;
    // Slot 3 (ret 0x10): an event delivered to the behaviour. The connection manager passes (kConnectionChanged, 0,
    // the event, 0) and ignores the result.
    virtual void OnEvent(uint32_t type, uint32_t arg1, void *event, uint32_t arg3) = 0;
};

// One end of a connection, as the iterators hand it out (by value: Get's hidden out pointer).
struct cConnectionInfo {
    cIAbstractBehaviorType *mObject;
    uint32_t mEvent;
};
static_assert(sizeof(cConnectionInfo) == 8, "cConnectionInfo size");

// The event both ends receive (built on the stack by slots 3 and 4; 0x18 bytes). Its class is the game's
// (cConnectionChangedEvent, vtable 0xda239c, 7 slots): receivers may call it, our code only fills it.
struct cConnectionChangedEvent {
    static constexpr uint32_t kVTable = 0xda239c;
    static constexpr uint32_t kType = 0x5b51e806; // the type passed to OnEvent
    const void *mVTable;
    uint32_t mDisconnected; // 0 connected, 1 disconnected
    cIAbstractBehaviorType *mSource;
    cIAbstractBehaviorType *mTarget;
    uint32_t mSourceEvent;
    uint32_t mTargetEvent;
};
static_assert(sizeof(cConnectionChangedEvent) == 0x18, "cConnectionChangedEvent size");

// cIGZIterator / cIGZForwardIterator<cConnectionInfo, cGZElementByValue<cConnectionInfo>>: slots 3-6.
class cIConnectionIterator : public cIGZUnknown {
public:
    virtual cIConnectionIterator *Clone() = 0;                    // slot 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cIConnectionIterator *other) = 0;  // slot 4
    virtual cConnectionInfo Get() = 0;                            // slot 5
    virtual void Next() = 0;                                      // slot 6
};

// The bidirectional and random-access additions (slots 7-9), as the null iterator implements them.
class cIConnectionRandomAccessIterator : public cIConnectionIterator {
public:
    virtual void Previous() = 0;               // slot 7
    virtual void Advance(int32_t count) = 0;   // slot 8
    virtual cConnectionInfo At(int32_t index) = 0; // slot 9 (ret 8: the out pointer and the index)
};

// What slots 10 and 11 return (by hidden pointer): a cursor (a clone of begin) and the range's two ends, each counted.
struct cConnectionRange {
    cIConnectionIterator *mCurrent;
    cIConnectionIterator *mBegin;
    cIConnectionIterator *mEnd;
};
static_assert(sizeof(cConnectionRange) == 0xc, "cConnectionRange size");

// nGZEvent::cIConnectionManager (slots 3-12).
class cIConnectionManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xbb254040;

    virtual void Connect(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target, uint32_t targetEvent) = 0;
    virtual void Disconnect(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target, uint32_t targetEvent) = 0;
    virtual void DisconnectAll(cIGZUnknown *object) = 0;
    virtual bool HasConnectionsFrom(cIGZUnknown *source) = 0;
    virtual bool HasConnectionsTo(cIGZUnknown *target) = 0;
    virtual uint32_t CountFrom(cIGZUnknown *source, uint32_t sourceEvent) = 0;
    virtual uint32_t CountTo(cIGZUnknown *target, uint32_t targetEvent) = 0;
    virtual cConnectionRange ConnectionsFrom(cIGZUnknown *source, uint32_t sourceEvent) = 0;
    virtual cConnectionRange ConnectionsTo(cIGZUnknown *target, uint32_t targetEvent) = 0;
    virtual bool IsConnected(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target, uint32_t targetEvent) = 0;
};

// The maps' hash: the pointer itself (the original hands the key to its bucket lookup unchanged).
struct BehaviorHash {
    size_t operator()(const cIAbstractBehaviorType *object) const { return reinterpret_cast<size_t>(object); }
};

using Connections = std::multimap<uint32_t, cConnectionInfo>;
using ConnectionTable = std::unordered_map<cIAbstractBehaviorType *, Connections, BehaviorHash>;

struct cConnectionManager : cIConnectionManager {
    static constexpr uint32_t kSize = 0x44;

    cConnectionManager() = default;        // 0x1aa630: both maps empty
    virtual ~cConnectionManager();         // 0x1aa69a; the deleting destructor 0x1aa788 is slot 13

    cConnectionManager(const cConnectionManager &) = delete;
    cConnectionManager &operator=(const cConnectionManager &) = delete;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1aaf72: 1 and kIid, false for a null out
    uint32_t AddRef() override;                              // shared 0x886f4: 1 (the instance is a static)
    uint32_t Release() override;                             // shared 0x886f4: 1

    // cIConnectionManager
    void Connect(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target, uint32_t targetEvent) override; // 0x1aaabd
    void Disconnect(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target,
                    uint32_t targetEvent) override;                                          // 0x1aa84e
    void DisconnectAll(cIGZUnknown *object) override;                                        // 0x1aa9ca
    bool HasConnectionsFrom(cIGZUnknown *source) override;                                   // 0x1aac7d
    bool HasConnectionsTo(cIGZUnknown *target) override;                                     // 0x1aac40
    uint32_t CountFrom(cIGZUnknown *source, uint32_t sourceEvent) override;                  // 0x1aae49
    uint32_t CountTo(cIGZUnknown *target, uint32_t targetEvent) override;                    // 0x1aadfe
    cConnectionRange ConnectionsFrom(cIGZUnknown *source, uint32_t sourceEvent) override;    // 0x1aae94
    cConnectionRange ConnectionsTo(cIGZUnknown *target, uint32_t targetEvent) override;      // 0x1aacba
    bool IsConnected(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target,
                     uint32_t targetEvent) override;                                         // 0x1aa7a7

    // 0x1aaa98: the object's behaviour face (QueryInterface kIid into a slot holding the object itself).
    static cIAbstractBehaviorType *AsBehavior(cIGZUnknown *object);

    ConnectionTable mFrom; // +0x04 source -> (sourceEvent -> {target, targetEvent})
    ConnectionTable mTo;   // +0x24 target -> (targetEvent -> {source, sourceEvent})
};
static_assert(sizeof(cConnectionManager) == cConnectionManager::kSize, "cConnectionManager size");
static_assert(offsetof(cConnectionManager, mFrom) == 4 && offsetof(cConnectionManager, mTo) == 0x24,
              "cConnectionManager layout");

// nGZEvent::cConnectionIterator (game vtable 0xda2378, 0xc bytes): a position in one key's run of a multimap.
class cConnectionIterator : public cIConnectionIterator {
public:
    explicit cConnectionIterator(Connections::const_iterator node) : mNode(node) {} // 0x1aa618 (count 0)
    cConnectionIterator(const cConnectionIterator &other) : mNode(other.mNode) {}    // 0x1aa5ff (count 0)
    virtual ~cConnectionIterator() = default; // 0x1aa693; the deleting destructor 0x1aa769 is slot 7
    cConnectionIterator &operator=(const cConnectionIterator &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;     // shared 0x1a849d: riid 1 only, false for a null out
    uint32_t AddRef() override;                                  // shared 0x148fb0
    uint32_t Release() override;                                 // shared 0x1a84c0: deletes at 0
    cIConnectionIterator *Clone() override;                      // 0x1aaa7b
    bool IsEqual(const cIConnectionIterator *other) override;    // shared 0x17b680: the words at +8 compared
    cConnectionInfo Get() override;                              // 0x1aac0d
    void Next() override;                                        // 0x1aadf6: the tree successor

    uint32_t mRefCount = 0;            // +4
    Connections::const_iterator mNode; // +8
};
static_assert(sizeof(cConnectionIterator) == 0xc, "cConnectionIterator size");
static_assert(offsetof(cConnectionIterator, mNode) == 8, "cConnectionIterator layout");

// cRZNullRandomAccessIterator<cConnectionInfo, cGZElementByValue<cConnectionInfo>> (game vtable 0xda23bc, 8 bytes):
// both ends of an empty range. Every instance equals every iterator; it reads as {null, 0} and does not move.
class cNullConnectionIterator : public cIConnectionRandomAccessIterator {
public:
    cNullConnectionIterator() = default;
    virtual ~cNullConnectionIterator() = default; // the deleting destructor 0x1aa706 is slot 10

    bool QueryInterface(uint32_t riid, void **out) override;     // shared 0x1a849d
    uint32_t AddRef() override;                                  // shared 0x148fb0
    uint32_t Release() override;                                 // shared 0x1aafb0: deletes at 0
    cIConnectionIterator *Clone() override;                      // 0x1aaa61
    bool IsEqual(const cIConnectionIterator *other) override;    // shared 0x148260: true
    cConnectionInfo Get() override;                              // 0x1aaba9: {null, 0}
    void Next() override;                                        // shared 0x140fe0: nothing
    void Previous() override;                                    // shared 0x140fe0: nothing
    void Advance(int32_t count) override;                        // shared 0x300a0: nothing
    cConnectionInfo At(int32_t index) override;                  // shared 0x1aac30: slot 5, the index ignored

    uint32_t mRefCount = 0; // +4
};
static_assert(sizeof(cNullConnectionIterator) == 8, "cNullConnectionIterator size");

// The constructor and destructor hooks' entry points (`this` in ecx, the dummy edx).
struct cConnectionManagerAdapters {
    static cConnectionManager *__fastcall Construct(void *memory, void *edx); // 0x1aa630
    static void __fastcall Destruct(cConnectionManager *self, void *edx);     // 0x1aa69a
};

} // namespace nGZEvent
