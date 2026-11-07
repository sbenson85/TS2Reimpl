#pragma once
// nGZEvent::cGZEventReflector and its behaviour base cAbstractBehaviorTypeT<cIAbstractBehaviorType, cRefSingleThreaded>
// (lane LT, foundations F1 part 3), in the standalone shape (docs/standalone-shape.md). RVAs are 1.18.0.187; the code is
// the size profile (/O1).
//
// The behaviour base is the event system's participant: a count at +4 whose top bit says "someone listens to me" (kept
// in step by OnEvent from the connection manager's cConnectionChangedEvent), Notify / Ask that walk this object's
// listeners through the connection manager (cached at 0x10cadc8), Connect / Disconnect that forward to it, and the
// process-wide registry of event attributes (the map returned by 0x1480d0: event id -> six words).
//
// The reflector (class 0xad6c43e8, made by the event COM director's factory 0x1a9e0b) turns events into messages: every
// event it was asked to reflect (Reflect(source, event): the source's attribute for the event is recorded and the
// reflector connected to the source) reaches its target (a cIGZMessageTarget2) as a cRZMessage2Standard of type
// 0xed6c1159 carrying the event id, the first argument and the data pointer.
//
// Faces (RTTI order): +0 the behaviour base (vtable 0xda22d4; the base's own 0xda2290 while the base is built), +8
// cIGZEventReflector (0xda2318). The factory and the constructor 0x1a999a (called only by the factory) are hooked; the
// vtables are stored by nothing else (the base's also by its destructor 0x1a99c1), so every reflector carries ours and
// the slots are ours in effect through those callers.
#include "gz/cConnectionManager.h" // cIAbstractBehaviorType, cIConnectionManager
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZAutoRef.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace nGZEvent {

// One event's attribute record (24 bytes): what the behaviour types' slot 14 hands out by value. The registry's
// default record is {0, the empty text 0xd9b9d0, 0, 0, 0, 0}; a missing id reads as the static record 0xff2374.
struct cEventAttribute {
    uint32_t mId;          // +0x00 the event id (0: no such event)
    const char *mName;     // +0x04 a text pointer (the default: the shared empty text)
    uint32_t mWord08;      // +0x08
    uint32_t mWord0C;      // +0x0c
    uint32_t mWord10;      // +0x10
    uint32_t mWord14;      // +0x14 the reflector stores 4 in the records it adds
};
static_assert(sizeof(cEventAttribute) == 0x18, "cEventAttribute size");

// The registry 0x1480d0 returns (std::map<unsigned long, attribute>): a function-local static built on first use.
using cEventAttributeMap = std::map<uint32_t, cEventAttribute>;

// cIGZIterator / cIGZForwardIterator<unsigned long, cGZElementByValue<unsigned long>> (slots 3-6): the event ids.
class cIAttributeIDIterator : public cIGZUnknown {
public:
    virtual cIAttributeIDIterator *Clone() = 0;                    // slot 3: a new iterator at the same place
    virtual bool IsEqual(const cIAttributeIDIterator *other) = 0;  // slot 4
    virtual uint32_t Get() = 0;                                    // slot 5
    virtual void Next() = 0;                                       // slot 6
};

// What slot 13 returns (by hidden pointer; 0x1a98e0 fills it): a cursor (a clone of begin) and the two ends, counted.
struct cAttributeIDRange {
    cIAttributeIDIterator *mCurrent;
    cIAttributeIDIterator *mBegin;
    cIAttributeIDIterator *mEnd;
};
static_assert(sizeof(cAttributeIDRange) == 0xc, "cAttributeIDRange size");

// 0x1a98e0 (thiscall on the range, ret 8; also used by the original's connection manager and every behaviour type's
// slot 13): the cursor a counted clone of begin (begin is assumed), then begin and end counted.
cAttributeIDRange MakeIteratorRange(cIAttributeIDIterator *begin, cIAttributeIDIterator *end);

// 0x146670 (thiscall on the range; about 25 callers, the connection ranges of every behaviour type among them): the end,
// the begin, then the cursor released, each when there is one. Its frame is noexcept (a throwing Release terminates).
void ReleaseIteratorRange(cIGZUnknown *const range[3]) noexcept;

// The whole behaviour interface (nGZEvent::cIAbstractBehaviorType, slots 3-14). "Notify" events go to listeners'
// slot 3, "queries" to their slot 4 (true when handled).
class cIAbstractBehavior : public cIAbstractBehaviorType {
public:
    virtual bool OnQuery(uint32_t type, uint32_t arg1, void *data) = 0;           // 4
    virtual void NotifyListeners(uint32_t type, uint32_t arg1, void *data) = 0;   // 5
    virtual bool AskListeners(uint32_t type, uint32_t arg1, void *data) = 0;      // 6: every listener said yes
    virtual void SetAttribute(uint32_t, uint32_t, uint32_t) = 0;                         // 7 (nothing here)
    virtual void Connect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) = 0;    // 8
    virtual void Disconnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) = 0; // 9
    virtual void GetAttribute(uint32_t, uint32_t, uint32_t) = 0;                        // 10 (nothing here)
    virtual uint32_t IsActive() = 0;                                              // 11: the count's top bit
    virtual uint32_t GetEventCount() = 0;                                         // 12
    virtual cAttributeIDRange GetEventIds() = 0;                                  // 13
    virtual cEventAttribute GetEventAttribute(uint32_t event) = 0;                // 14
};

// cAbstractBehaviorTypeT<cIAbstractBehaviorType, cRefSingleThreaded> (game vtable 0xda2290, 16 slots; constructor
// 0x1a98d0, destructor 0x1a99c1, deleting destructor 0x1a99e7 = slot 15). Only the reflector derives from it.
class cAbstractBehaviorType : public cIAbstractBehavior {
public:
    static constexpr uint32_t kVTable = 0xda2290;
    static constexpr uint32_t kActiveBit = 0x80000000;
    static constexpr uint32_t kCountMask = 0x7fffffff;
    static constexpr uint32_t kBehaviorIid2 = 0x3b253dc7; // answered as the behaviour itself, counted

    cAbstractBehaviorType() = default;
    virtual ~cAbstractBehaviorType() = default;
    cAbstractBehaviorType(const cAbstractBehaviorType &) = delete;
    cAbstractBehaviorType &operator=(const cAbstractBehaviorType &) = delete;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // shared_vf_1aa007
    uint32_t AddRef() override;                              // 0x1a9a34 (jmp to shared 0x1a9a25)
    uint32_t Release() override;                             // 0x1aa071

    // cIAbstractBehavior
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override;  // 0x1a9e32
    bool OnQuery(uint32_t type, uint32_t arg1, void *data) override;                 // shared_vf_1a9f00: slot 6
    void NotifyListeners(uint32_t type, uint32_t arg1, void *data) override;         // 0x1aa0c4
    bool AskListeners(uint32_t type, uint32_t arg1, void *data) override;            // 0x1a9d56
    void SetAttribute(uint32_t, uint32_t, uint32_t) override {}                             // shared 0x377e0
    void Connect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) override;    // 0x1a9ce1
    void Disconnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) override; // 0x1a9d36
    void GetAttribute(uint32_t, uint32_t, uint32_t) override {}                            // shared 0x377e0
    uint32_t IsActive() override;                                                    // shared 0x147cf0
    uint32_t GetEventCount() override;                                               // shared_vf_1a9ff1
    cAttributeIDRange GetEventIds() override;                                        // 0x1a9a41
    cEventAttribute GetEventAttribute(uint32_t event) override;                      // 0x1a9bd9

    // 0x1a9d01 (thiscall; `this` unused): the connection manager, cached at 0x10cadc8 once the event system service
    // gave one.
    static cIConnectionManager *GetEventManager();
    // 0x1480d0 (cdecl, the original's: about 25 callers): the event attribute registry.
    static cEventAttributeMap *Registry();
    // 0x1485f0 (cdecl, ~10 callers): the registry itself, built on first use (the two base events in it, as
    // RegisterBaseEvents leaves it); a non-zero `id` stores *attribute under it. Not inlined: tests stand in for it.
    __declspec(noinline) static cEventAttributeMap *__cdecl RegistryFor(uint32_t id, const cEventAttribute *attribute);
    // 0x1464f0 (thiscall, `this` unused and returned): stores the connection-changed and attribute-infos-updated events
    // in the registry. The plain form is for our own callers.
    static void RegisterBaseEvents();
    static void *__fastcall RegisterBaseEventsAdapter(void *self, void *edx);
    // 0x1465d0 and 0x146580 (thiscall, `this` unused and returned): each one of the two events (ConnectionChanged,
    // AttributeInfosUpdated); other registries' constructors call them.
    static void *__fastcall RegisterConnectionChangedAdapter(void *self, void *edx);
    static void *__fastcall RegisterAttributeInfosUpdatedAdapter(void *self, void *edx);
    // For tests: forget the registry (the next use builds it afresh).
    static void ResetRegistryForTest();

    uint32_t mRefCount = 0; // +4 the count (low 31 bits) and the active bit
};
static_assert(sizeof(cAbstractBehaviorType) == 8, "cAbstractBehaviorType size");

// 0x1aa148 (cdecl): flips the active bit of *count when it differs from `active` (the base OnEvent of every
// cAbstractBehaviorTypeT instance calls it).
void __cdecl SyncActiveBit(uint32_t *count, bool active);

// The event system service held for a scope (0x1a991d builds it: the framework's GetSystemService(0x5b1e896f,
// 0x1b1e895a); the holder is released through 0x1466e0).
struct cEventServiceRef {
    RZAutoRef mRef;
    cEventServiceRef() { Acquire(); }
    ~cEventServiceRef() { mRef.ReleaseObject(); }
    cEventServiceRef(const cEventServiceRef &) = delete;
    cEventServiceRef &operator=(const cEventServiceRef &) = delete;
    // 0x1a991d (thiscall, returns this): clears the holder, then asks the framework (when there is one) for the
    // service into it.
    cEventServiceRef *Acquire();
    cIConnectionManager *GetConnectionManager() const;
};
static_assert(sizeof(cEventServiceRef) == 4, "cEventServiceRef size");

// cIGZEventReflector (iid 0xad6c43f1, slots 3-6).
class cIGZEventReflector : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xad6c43f1;
    virtual void ReflectRegistered() = 0;                              // 3: every registered record appended
    virtual void Reset() = 0;                                          // 4: disconnected, emptied, no target
    virtual void *SetTarget(cIGZMessageTarget2 *target) = 0;           // 5: counted; returns the target field
    virtual void Reflect(cIAbstractBehavior *source, uint32_t event) = 0; // 6
};

class cGZEventReflector : public cAbstractBehaviorType, public cIGZEventReflector {
public:
    static constexpr uint32_t kVTable = 0xda22d4;
    static constexpr uint32_t kReflectorVTable = 0xda2318;
    static constexpr uint32_t kSize = 0x1c;
    static constexpr uint32_t kClassId = 0xad6c43e8;       // the COM class (0x1a9e2c returns it)
    static constexpr uint32_t kMessageType = 0xed6c1159;   // the messages sent to the target
    static constexpr uint32_t kReflectedEvent = 0xe9dd42cf; // notified to listeners when an event is first reflected
    static constexpr uint32_t kAddedWord14 = 4;            // mWord14 of the records Reflect adds
    static constexpr uint32_t kNullEventVTable = 0xd9ca78; // cNullEvent's (game) vtable: the event of kReflectedEvent

    cGZEventReflector() = default; // 0x1a999a
    ~cGZEventReflector() override; // 0x1a99c8; slot 15 the deleting destructor 0x1a9a06
    cGZEventReflector(const cGZEventReflector &) = delete;
    cGZEventReflector &operator=(const cGZEventReflector &) = delete;

    // 0x1a9e0b (cdecl, the COM director's factory): a new reflector's cIGZEventReflector face (null when new failed),
    // not counted.
    static cIGZEventReflector *__cdecl Create();
    // 0x1a9e2c (cdecl): the COM class id (the event COM director's table, rz/COMDirectorData.cpp, holds it directly).
    static uint32_t __cdecl ClassId() { return kClassId; }

    // cIGZUnknown (+8 through the thunks: QI 0x1aa069, AddRef shared_vf_1a9a39, Release 0x1aa0bc)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1aa03e
    uint32_t AddRef() override { return cAbstractBehaviorType::AddRef(); }
    uint32_t Release() override;                             // 0x1aa0b7: the base's

    // cIAbstractBehavior
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0x1a9e5f
    bool OnQuery(uint32_t type, uint32_t arg1, void *data) override;                // 0x1a9f05
    uint32_t GetEventCount() override;                                              // 0x1a9ffa
    cAttributeIDRange GetEventIds() override;                                       // 0x1a9b15
    cEventAttribute GetEventAttribute(uint32_t event) override;                     // 0x1a9c11

    // cIGZEventReflector
    void ReflectRegistered() override;                                  // 0x1a9fb2
    void Reset() override;                                              // 0x1aa166
    void *SetTarget(cIGZMessageTarget2 *target) override;               // shared_vf_1aa15e
    void Reflect(cIAbstractBehavior *source, uint32_t event) override;  // 0x1a9c4a

    // 0x1a98af (cdecl, a std::find_if instance returning through a hidden pointer; called only by the slots above):
    // the first record of `event` in [first, last), else last.
    static const cEventAttribute *FindRecord(const cEventAttribute *first, const cEventAttribute *last, uint32_t event);
    const cEventAttribute *FindReflected(uint32_t event) const;

    // The target, counted (released by the destructor after the records are freed, as members in reverse order).
    struct TargetRef {
        cIGZMessageTarget2 *mObject = nullptr;
        TargetRef() = default;
        TargetRef(const TargetRef &) = delete;
        TargetRef &operator=(const TargetRef &) = delete;
        ~TargetRef()
        {
            if (mObject) mObject->Release();
        }
    };
    TargetRef mTarget;                       // +0x0c
    std::vector<cEventAttribute> mReflected; // +0x10 the reflected events' records

private:
    // A new message of kMessageType for (event, arg1, data), counted (null when the allocation failed).
    static cIGZUnknown *NewMessage(uint32_t event, uint32_t arg1, void *data);
};
static_assert(sizeof(cGZEventReflector) == cGZEventReflector::kSize, "cGZEventReflector size");
static_assert(offsetof(cGZEventReflector, mRefCount) == 4, "cGZEventReflector layout");
static_assert(offsetof(cGZEventReflector, mTarget) == 0xc, "cGZEventReflector layout");
static_assert(offsetof(cGZEventReflector, mReflected) == 0x10, "cGZEventReflector layout");

// The hooks' entry points.
struct cGZEventReflectorAdapters {
    static cGZEventReflector *__fastcall Construct(void *memory, void *edx);           // 0x1a999a
    static cEventServiceRef *__fastcall AcquireEventService(cEventServiceRef *self, void *edx); // 0x1a991d
    static cAttributeIDRange *__fastcall MakeIteratorRange(cAttributeIDRange *out, void *edx, cIAttributeIDIterator *begin,
                                                           cIAttributeIDIterator *end); // 0x1a98e0
    static void __fastcall ReleaseIteratorRange(cIGZUnknown *const *range, void *edx);  // 0x146670
};

} // namespace nGZEvent
