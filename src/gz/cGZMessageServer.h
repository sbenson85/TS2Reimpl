#pragma once
// cGZMessageServer (lane KU): the framework's first message server (system service 0x64063387, priority 5000000,
// interface cIGZMessageServer 0xc1085722). It delivers cGZMessage messages (gz/cIGZMessageTarget.h) three ways:
// - MessageSend: at once, to every target registered for the message's id (AddNotification / RemoveNotification);
// - MessagePost: through a queue the tick empties (each queued message is sent as above);
// - GeneralMessagePostToTarget: to one target on a later tick (the general posts, cancelled per target).
// Registrations made while the server collects a send's targets wait in a pending list (mDepth); the targets are
// AddRef'd while registered and around each delivery, so a handler may register, unregister, send or post.
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (game vtable 0xea0450, 13 slots), +4 cRZUnknownMT (0xea0488,
// 6), +0x1c cIGZMessageServer (0xea04a4, 12). 0xa8 bytes, built for size.
//
// Creation (standalone shape, docs/standalone-shape.md): the cTSAppFWCOMDirector constructor (0xc2bba4) registers the
// creator 0xc751d0 (new 0xa8, the constructor 0xc74c5d); nothing else calls the constructor, and the vtables are
// referenced only by it and the destructor 0xc74d0c. With the creator hooked to Create, every message server is ours.
// The queue (+0x38) is still the game's cGZMessageQueue, built and destroyed by its original constructor and destructor.
#include "gz/cIGZMessageTarget.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <unordered_map>

class cIGZSystemService;

// cIGZMessageQueue (iid 0x298b2f5e): a locked FIFO of cGZMessage copies with an event raised when the first message
// arrives (the Wait* slots block on it). Slot meanings from cGZMessageQueue's bodies (vtable 0xea03d4, 30 slots).
class cIGZMessageQueue : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x298b2f5e;
    virtual void PushBack(const cGZMessage *message) = 0;           // 3 0xc75792 (ret 4)
    virtual void PushFront(const cGZMessage *message) = 0;          // 4 0xc757cb (ret 4)
    virtual bool RemoveFirstWithId(uint32_t id) = 0;                // 5 0xc7595d (ret 4)
    virtual bool RemoveFirst(const cGZMessage *message) = 0;        // 6 0xc7523c (ret 4): the first equal message
    virtual bool Unsupported7(uint32_t value) = 0;                  // 7 0xc759a9 (ret 4): false
    virtual bool Unsupported8(uint32_t value) = 0;                  // 8 0xc759a9 (ret 4): false
    virtual uint32_t RemoveAllWithId(uint32_t id) = 0;              // 9 0xc75909 (ret 4): the count removed
    virtual uint32_t RemoveAll(const cGZMessage *message) = 0;      // 10 0xc758a2 (ret 4): the count removed
    virtual uint32_t Clear() = 0;                                   // 11 0xc7587b: the count before
    virtual uint32_t FrontId() = 0;                                 // 12 0xc75767: 0 when empty
    virtual uint32_t BackId() = 0;                                  // 13 0xc75710: 0 when empty
    virtual bool ContainsId(uint32_t id) = 0;                       // 14 0xc756a8 (ret 4)
    virtual cGZMessage *Front() = 0;                                // 15 0xc7573c: null when empty
    virtual cGZMessage *Back() = 0;                                 // 16 0xc756e4: null when empty
    virtual bool Contains(const cGZMessage *message) = 0;           // 17 0xc75663 (ret 4)
    virtual uint32_t PopFrontId() = 0;                              // 18 0xc753fd: 0 when empty
    virtual uint32_t PopBackId() = 0;                               // 19 0xc75319: 0 when empty
    virtual bool RemoveFirstWithIdAgain(uint32_t id) = 0;           // 20 shared 0x2b5b00: slot 5
    virtual bool PopFront(cGZMessage *out) = 0;                     // 21 0xc75376 (ret 4): false when empty
    virtual bool PopBack(cGZMessage *out) = 0;                      // 22 0xc75290 (ret 4): false when empty
    virtual bool RemoveFirstAgain(const cGZMessage *message) = 0;   // 23 0xc7523c, as slot 6
    virtual uint32_t WaitPopFrontId() = 0;                          // 24 0xc75430
    virtual uint32_t WaitPopBackId() = 0;                           // 25 0xc7534d
    virtual bool WaitPopFront(cGZMessage *out) = 0;                 // 26 0xc753c8 (ret 4)
    virtual bool WaitPopBack(cGZMessage *out) = 0;                  // 27 0xc752e4 (ret 4)
    virtual uint32_t Size() = 0;                                    // 28 shared 0xc675c3
};

// cIGZMessageServer (iid 0xc1085722, 12 slots).
class cIGZMessageServer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xc1085722;
    virtual bool MessageSend(cGZMessage *message) = 0;                                  // 3 (ret 4): true
    virtual bool MessagePost(cGZMessage *message, bool urgent) = 0;                     // 4 (ret 8): true
    virtual bool AddNotification(cIGZMessageTarget *target, uint32_t id) = 0;           // 5 (ret 8): true
    virtual bool RemoveNotification(cIGZMessageTarget *target, uint32_t id) = 0;        // 6 (ret 8): true
    virtual int32_t GeneralMessagePostToTarget(cGZMessage *message, cIGZMessageTarget *target) = 0; // 7 (ret 8)
    virtual int32_t CancelGeneralMessagePostsToTarget(cIGZMessageTarget *target) = 0;  // 8 (ret 4)
    virtual bool OnTick(uint32_t counter) = 0;                                          // 9 (ret 4): the service's
    virtual uint32_t GetMessageQueueSize() = 0;                                         // 10
    virtual void SetAlwaysClearQueueOnTick(bool clear) = 0;                             // 11 (ret 4)
};

class cGZMessageServer : public rz::cRZSystemService, public cIGZMessageServer {
public:
    static constexpr uint32_t kSize = 0xa8;
    static constexpr uint32_t kServiceId = 0x64063387;
    static constexpr int32_t kPriority = 5000000;
    // MessageSend collects a send's targets on the stack: 384 slots, the last for the terminating null.
    static constexpr size_t kMaxSendTargets = 383;

    cGZMessageServer();                    // 0xc74c5d
    ~cGZMessageServer() noexcept override; // 0xc74d0c; deleting 0xc74e61 (+4 face slot 3, thunk 0xc74e3a)

    cGZMessageServer(const cGZMessageServer &) = delete;
    cGZMessageServer &operator=(const cGZMessageServer &) = delete;

    // 0xc751d0 (the creator the director registers): new, construct (the +0 face back).
    static cIGZSystemService *__cdecl Create();

    // cIGZUnknown (every face; the +4 / +0x1c ones through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **out) override; // 0xc7582a (thunks 0xc75854, 0xc7585c)
    uint32_t AddRef() override;                              // the service's (folded 0x1cc320 and its thunks)
    uint32_t Release() override;                             // the service's (folded 0x1cd7a0 and its thunks)

    // cIGZSystemService
    bool Init() override;                 // 0xc7547d: added to the framework's tick
    bool Shutdown() override;             // 0xc75b61: removed from the tick
    bool OnTick(uint32_t counter) override; // 0xc7558d (the message face's slot 9: thunk 0xc7565b)

    // cIGZMessageServer
    bool MessageSend(cGZMessage *message) override;                                      // 0xc754cb
    bool MessagePost(cGZMessage *message, bool urgent) override;                         // 0xc75497
    bool AddNotification(cIGZMessageTarget *target, uint32_t id) override;               // 0xc74e83
    bool RemoveNotification(cIGZMessageTarget *target, uint32_t id) override;            // 0xc759c1
    int32_t GeneralMessagePostToTarget(cGZMessage *message, cIGZMessageTarget *target) override; // 0xc751ef
    int32_t CancelGeneralMessagePostsToTarget(cIGZMessageTarget *target) override;      // 0xc74f80
    uint32_t GetMessageQueueSize() override;                                             // 0xc75459
    void SetAlwaysClearQueueOnTick(bool clear) override;                                 // folded 0x9afde0

    // 0xc7508a: every pending change applied in order, then the pending list emptied (MessageSend, under the lock).
    void ApplyPendingChanges();
    // 0xc75005: the pending changes replayed through AddNotification / RemoveNotification once no send collects
    // (mDepth 0) and no replay runs (sReplayingPending).
    void ProcessPending();
    // 0xc75a9b: the general posts delivered, at most as many as were waiting when the round began.
    void DispatchGeneralPosts();

    // A registration change made while a send collects its targets. An add holds a reference to the target.
    struct PendingChange {
        bool add;                  // +0 (else a removal)
        cIGZMessageTarget *target; // +4
        uint32_t id;               // +8
    };
    static_assert(sizeof(PendingChange) == 0xc, "PendingChange: flag, target, id");

    // A general post: the target (not counted) and a copy of the message.
    struct GeneralPost {
        cIGZMessageTarget *target; // +0
        cGZMessage message;        // +4
    };
    static_assert(sizeof(GeneralPost) == 0x14, "GeneralPost: target, message");

    using TargetList = std::list<cIGZMessageTarget *>;
    using TargetMap = std::unordered_map<uint32_t, TargetList *>;

    // Our cGZMessageQueue in place (gz/cGZMessageQueue.h includes this header, so the member is raw storage): built and
    // destroyed by the queue's adapters (0xc74c32 / 0xc74cea), its slots through cIGZMessageQueue.
    struct GameMessageQueue {
        GameMessageQueue();
        ~GameMessageQueue();
        GameMessageQueue(const GameMessageQueue &) = delete;
        GameMessageQueue &operator=(const GameMessageQueue &) = delete;
        cIGZMessageQueue *operator->() { return reinterpret_cast<cIGZMessageQueue *>(raw); }
        alignas(4) unsigned char raw[0x20];
    };

    // The critical section as a member: built (0x1af64e) in member order, destroyed (0x1af68a) in reverse.
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };

    OwnedLock mTargetsLock;           // +0x20 the registrations, the pending list and mDepth
    OwnedLock mQueueLock;             // +0x28 the queue and mProcessed
    OwnedLock mGeneralLock;           // +0x30 the general posts
    GameMessageQueue mQueue;          // +0x38
    uint64_t mProcessed;              // +0x58 queued messages taken off by OnTick, ever
    int32_t mDepth;                   // +0x60 nonzero while a send collects (and while a registration is made)
    bool mAlwaysClearQueue;           // +0x64 OnTick empties the queue, also of what its sends post (from the start)
    std::list<PendingChange> mPending; // +0x68
    TargetMap mTargets;               // +0x70 message id -> its targets (in registration order; each counted)
    int32_t mGeneralRemaining;        // +0x90 the general posts the current round may still deliver
    std::deque<GeneralPost> mGeneralPosts; // +0x94

    // The game's static 0x1103248: set while ProcessPending replays (every server; there is one).
    static bool sReplayingPending;

private:
    // id's list gains target unless it holds it already (a new list for a new id); true when it was appended.
    bool AppendTarget(uint32_t id, cIGZMessageTarget *target);
    // target leaves id's list (every copy) and is released; an emptied list goes, with its id. True when it was there.
    bool DropTarget(uint32_t id, cIGZMessageTarget *target);
};
static_assert(sizeof(cGZMessageServer) == cGZMessageServer::kSize, "cGZMessageServer size");
static_assert(offsetof(cGZMessageServer, mTargetsLock) == 0x20 && offsetof(cGZMessageServer, mQueue) == 0x38 &&
                  offsetof(cGZMessageServer, mProcessed) == 0x58 && offsetof(cGZMessageServer, mDepth) == 0x60 &&
                  offsetof(cGZMessageServer, mAlwaysClearQueue) == 0x64 && offsetof(cGZMessageServer, mPending) == 0x68 &&
                  offsetof(cGZMessageServer, mTargets) == 0x70 && offsetof(cGZMessageServer, mGeneralRemaining) == 0x90 &&
                  offsetof(cGZMessageServer, mGeneralPosts) == 0x94,
              "cGZMessageServer layout: the message face at +0x1c");
