#pragma once
// cGZMessageServer2 (lane KU): the framework's second message server (system service 0x4fa845b, priority 5000000,
// interface cIGZMessageServer2 0x652294c7). It delivers counted cIGZMessage2 messages (gz/cIGZMessage2Standard.h) to
// cIGZMessageTarget2 targets (gz/cIGZMessageTarget2.h), three ways, as the first server (gz/cGZMessageServer.h) does:
// - MessageSend: at once, to every target registered for the message's type, then to every target registered for type 0
//   (the catch-all);
// - MessagePost: through a queue the tick empties;
// - GeneralMessagePostToTarget: to one target on a later tick (the general posts, cancelled per target).
// Unlike the first server it counts everything it holds (CountedRef): the registrations, the pending changes (both
// kinds), the general posts' targets and messages, the queued messages, and a send's collected targets.
//
// Faces (RTTI order): +0 cIGZMessageServer2 (game vtable 0xe9e670, 14 slots), +4 cRZSystemService / cIGZSystemService
// (0xe9e6ac, 13), +8 cRZUnknownMT (0xe9e6e4, 6). 0xa8 bytes, built for size.
//
// Creation (standalone shape, docs/standalone-shape.md): the cRZMessageCOMDirector (0xc6eb80) registers the creator
// 0xc673ce (new 0xa8, the constructor 0xc66b80, the +4 face back); nothing else calls the constructor, and the vtables are
// referenced only by it and the destructor 0xc66ce6. With the creator hooked to Create, every second message server is
// ours. The queue (+0x38) is still the game's cGZMessageQueue2, built and destroyed by its original constructor and
// destructor.
#include "gz/cIGZMessage2Standard.h" // cIGZMessage2
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <unordered_map>
#include <vector>

class cIGZSystemService;

// A counted interface pointer, the server's cRZAutoRefCount<T> (inlined in the game: its assignment is 0x142080, its copy
// constructor e.g. in 0xc66aeb): AddRef when a pointer is taken in, Release when it is dropped. Copies count (there is no
// move), so the containers' copies, reallocations and erasures count as the game's do.
template <class T> class CountedRef {
public:
    CountedRef() = default;
    explicit CountedRef(T *object) : mObject(object)
    {
        if (mObject) mObject->AddRef();
    }
    CountedRef(const CountedRef &other) : CountedRef(other.mObject) {}
    CountedRef &operator=(const CountedRef &other)
    {
        if (this != &other) Assign(other.mObject);
        return *this;
    }
    ~CountedRef()
    {
        if (mObject) mObject->Release();
    }

    // Counts the new object, stores it, then releases the old one; nothing for the same pointer (0x142080).
    void Assign(T *object)
    {
        T *const old = mObject;
        if (object == old) return;
        if (object) object->AddRef();
        mObject = object;
        if (old) old->Release();
    }
    // Lets go of the object without releasing it (the caller takes the reference over).
    T *Detach()
    {
        T *const object = mObject;
        mObject = nullptr;
        return object;
    }
    T *Get() const { return mObject; }
    T *operator->() const { return mObject; }

private:
    T *mObject = nullptr;
};

// cIGZMessageQueue2: a locked FIFO of counted cIGZMessage2 pointers with an event raised when the first message arrives
// (the Wait* slots block on it). Slot meanings from cGZMessageQueue2's bodies (vtable 0xe9e588, 25 slots). A message's
// type is its slot 4 (GetType), equality its slot 6 (IsEqual). The out slots hand out a counted message (null and false
// when there is none).
class cIGZMessageQueue2 : public cIGZUnknown {
public:
    virtual void PushBack(cIGZMessage2 *message) = 0;                      // 3 0xc679e6 (ret 4): counted
    virtual void PushFront(cIGZMessage2 *message) = 0;                     // 4 0xc67a3e (ret 4)
    virtual bool RemoveFirstWithType(uint32_t type) = 0;                   // 5 0xc67cb7 (ret 4)
    virtual bool RemoveFirst(cIGZMessage2 *message) = 0;                   // 6 0xc67c63 (ret 4): the first equal one
    virtual bool RemoveLastWithType(uint32_t type) = 0;                    // 7 0xc67d5e (ret 4)
    virtual bool RemoveLast(cIGZMessage2 *message) = 0;                    // 8 0xc67d09 (ret 4)
    virtual uint32_t RemoveAllWithType(uint32_t type) = 0;                 // 9 0xc67bd3 (ret 4): the count removed
    virtual uint32_t RemoveAll(cIGZMessage2 *message) = 0;                 // 10 0xc67b72 (ret 4): the count removed
    virtual uint32_t Clear() = 0;                                          // 11 0xc67b4b: the count before
    virtual uint32_t FrontType() = 0;                                      // 12 0xc679b4: 0 when empty
    virtual uint32_t BackType() = 0;                                       // 13 0xc6793f: 0 when empty
    virtual bool ContainsType(uint32_t type) = 0;                          // 14 0xc678ba (ret 4)
    virtual bool Front(cIGZMessage2 **out) = 0;                            // 15 0xc67972 (ret 4)
    virtual bool Back(cIGZMessage2 **out) = 0;                             // 16 0xc678fc (ret 4)
    virtual bool Find(cIGZMessage2 *message, cIGZMessage2 **out) = 0;      // 17 0xc67862 (ret 8): the first equal one
    virtual bool PopFront(cIGZMessage2 **out) = 0;                         // 18 0xc6753f (ret 4)
    virtual bool PopBack(cIGZMessage2 **out) = 0;                          // 19 0xc674b9 (ret 4)
    virtual bool WaitPopFront(cIGZMessage2 **out) = 0;                     // 20 0xc6758e (ret 4)
    virtual bool WaitPopBack(cIGZMessage2 **out) = 0;                      // 21 0xc6750a (ret 4)
    virtual uint32_t Size() = 0;                                           // 22 shared 0xc675c3
};

// cIGZMessageServer2 (iid 0x652294c7, 14 slots).
class cIGZMessageServer2 : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x652294c7;
    virtual bool MessageSend(cIGZMessage2 *message) = 0;                                     // 3 (ret 4): true
    virtual bool MessagePost(cIGZMessage2 *message, bool urgent) = 0;                        // 4 (ret 8): true
    virtual bool AddNotification(cIGZMessageTarget2 *target, uint32_t type) = 0;            // 5 (ret 8): true
    virtual bool RemoveNotification(cIGZMessageTarget2 *target, uint32_t type) = 0;         // 6 (ret 8): true
    virtual int32_t GeneralMessagePostToTarget(cIGZMessage2 *message, cIGZMessageTarget2 *target) = 0; // 7 (ret 8)
    virtual int32_t CancelGeneralMessagePostsToTarget(cIGZMessageTarget2 *target) = 0;      // 8 (ret 4)
    virtual bool OnTick(uint32_t counter) = 0;                                               // 9 (ret 4): the service's
    virtual uint32_t GetMessageQueueSize() = 0;                                              // 10
    virtual void SetAlwaysClearQueueOnTick(bool clear) = 0;                                  // 11 (ret 4)
    virtual uint32_t RefCount() = 0;                                                         // 12: the service's count
    virtual bool CreateMessage(uint32_t clsid, uint32_t iid, void **out) = 0;               // 13 (ret 0xc)
};

class cGZMessageServer2 : public cIGZMessageServer2, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0xa8;
    static constexpr uint32_t kServiceId = 0x4fa845b;
    static constexpr int32_t kPriority = 5000000;
    // CreateMessage's one class: cRZMessage2Standard (0x2c bytes, RZMemory; constructor 0x1c033d).
    static constexpr uint32_t kStandardMessageClsid = 0x46df45e7;

    cGZMessageServer2();                    // 0xc66b80
    ~cGZMessageServer2() noexcept override; // 0xc66ce6; deleting 0xc66e97 (+8 face slot 3, thunk 0xc66e2b)

    cGZMessageServer2(const cGZMessageServer2 &) = delete;
    cGZMessageServer2 &operator=(const cGZMessageServer2 &) = delete;

    // 0xc673ce (the creator the director registers): new, construct, the +4 face back.
    static cIGZSystemService *__cdecl Create();

    // cIGZUnknown (every face; the +4 / +8 ones through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **out) override; // 0xc67b02 (thunks 0xc67b24, 0xc67b2c)
    uint32_t AddRef() override;                              // the service's (shared thunks 0x145240 ...)
    uint32_t Release() override;                             // the service's (shared thunks 0x1a85b0 ...)
    uint32_t RefCount() override;                            // slot 12 (shared 0x47c6e0) and the +8 face's slot 5

    // cIGZSystemService (on the +4 face)
    bool Init() override;                   // 0xc6765a: added to the framework's tick (no check for a framework)
    bool Shutdown() override;               // 0xc68051: removed from the tick (no check)
    bool OnTick(uint32_t counter) override; // 0xc67792 (the +4 face's slot 10: thunk 0xc6785a)

    // cIGZMessageServer2
    bool MessageSend(cIGZMessage2 *message) override;                                     // 0xc676a8
    bool MessagePost(cIGZMessage2 *message, bool urgent) override;                        // 0xc67674
    bool AddNotification(cIGZMessageTarget2 *target, uint32_t type) override;             // 0xc66eb9
    bool RemoveNotification(cIGZMessageTarget2 *target, uint32_t type) override;          // 0xc67db1
    int32_t GeneralMessagePostToTarget(cIGZMessage2 *message, cIGZMessageTarget2 *target) override; // 0xc673fe
    int32_t CancelGeneralMessagePostsToTarget(cIGZMessageTarget2 *target) override;       // 0xc6712f
    uint32_t GetMessageQueueSize() override;                                              // 0xc675e0
    void SetAlwaysClearQueueOnTick(bool clear) override;                                  // shared 0x4e7ed0
    bool CreateMessage(uint32_t clsid, uint32_t iid, void **out) override;                // 0xc671ae

    // 0xc67274: every pending change applied in order, then the pending list emptied (MessageSend, under the lock).
    void ApplyPendingChanges();
    // 0xc67204: the pending changes replayed through AddNotification / RemoveNotification once no send collects (mDepth
    // 0) and no replay runs (sReplayingPending).
    void ProcessPending();
    // 0xc67f61: the general posts delivered, at most as many as were waiting when the round began.
    void DispatchGeneralPosts();

    using TargetRef = CountedRef<cIGZMessageTarget2>;
    using MessageRef = CountedRef<cIGZMessage2>;

    // A registration (the type and the counted target), as a pending change holds it.
    struct Registration {
        TargetRef target; // +0
        uint32_t type;    // +4
    };
    // A registration change made while a send collects its targets; both kinds hold the target.
    struct PendingChange {
        bool add;                  // +0 (else a removal)
        Registration registration; // +4
    };
    static_assert(sizeof(PendingChange) == 0xc, "PendingChange: flag, target, type");

    // A general post: the target and the message, both counted.
    struct GeneralPost {
        TargetRef target;   // +0
        MessageRef message; // +4
    };
    static_assert(sizeof(GeneralPost) == 8, "GeneralPost: target, message");

    using TargetList = std::list<TargetRef>;
    using TargetMap = std::unordered_map<uint32_t, TargetList *>;

    // Our cGZMessageQueue2 in place (gz/cGZMessageQueue.h includes this header, so the member is raw storage): built and
    // destroyed by the queue's adapters (0xc66b0d / 0xc66c91), its slots through cIGZMessageQueue2.
    struct GameMessageQueue {
        GameMessageQueue();
        ~GameMessageQueue();
        GameMessageQueue(const GameMessageQueue &) = delete;
        GameMessageQueue &operator=(const GameMessageQueue &) = delete;
        cIGZMessageQueue2 *operator->() { return reinterpret_cast<cIGZMessageQueue2 *>(raw); }
        alignas(4) unsigned char raw[0x20];
    };

    // The critical section as a member: built (0x1af64e) in member order, destroyed (0x1af68a) in reverse.
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };

    OwnedLock mTargetsLock;            // +0x20 the registrations, the pending list and mDepth
    OwnedLock mQueueLock;              // +0x28 the queue and mProcessed
    OwnedLock mGeneralLock;            // +0x30 the general posts
    GameMessageQueue mQueue;           // +0x38
    uint64_t mProcessed;               // +0x58 queued messages taken off by OnTick, ever
    bool mAlwaysClearQueue;            // +0x60 OnTick empties the queue, also of what its sends post (from the start)
    int32_t mDepth;                    // +0x64 nonzero while a send collects (and while a registration is made)
    std::list<PendingChange> mPending; // +0x68
    TargetMap mTargets;                // +0x70 message type -> its targets (in registration order)
    int32_t mGeneralRemaining;         // +0x90 the general posts the current round may still deliver
    std::deque<GeneralPost> mGeneralPosts; // +0x94

    // The game's static 0x1103050: set while ProcessPending replays (every second server; there is one).
    static bool sReplayingPending;
};
static_assert(sizeof(cGZMessageServer2) == cGZMessageServer2::kSize, "cGZMessageServer2 size");
static_assert(offsetof(cGZMessageServer2, mTargetsLock) == 0x20 && offsetof(cGZMessageServer2, mQueue) == 0x38 &&
                  offsetof(cGZMessageServer2, mProcessed) == 0x58 && offsetof(cGZMessageServer2, mAlwaysClearQueue) == 0x60 &&
                  offsetof(cGZMessageServer2, mDepth) == 0x64 && offsetof(cGZMessageServer2, mPending) == 0x68 &&
                  offsetof(cGZMessageServer2, mTargets) == 0x70 && offsetof(cGZMessageServer2, mGeneralRemaining) == 0x90 &&
                  offsetof(cGZMessageServer2, mGeneralPosts) == 0x94,
              "cGZMessageServer2 layout: the service at +4");
