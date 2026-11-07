#pragma once
// The message servers' queues (lane LR, foundations F1), in the standalone shape (docs/standalone-shape.md). Both are
// 0x20 bytes: +4 a std::list of the queued messages, +0xc a cRZCriticalSection guarding it, +0x14 an auto-reset
// cRZThreadSignal pulsed when a message arrives in an empty queue (the Wait* slots wait on it 1000 ms at a time and try
// again), +0x1c the reference count. RVAs are 1.18.0.187.
//
// cGZMessageQueue (cIGZMessageQueue, game vtable 0xea03d4, 30 slots) holds cGZMessage copies; cGZMessageQueue2
// (cIGZMessageQueue2, 0xe9e588, 25 slots) holds counted cIGZMessage2 pointers. The interfaces (slot meanings) are the
// servers' (gz/cGZMessageServer.h, gz/cGZMessageServer2.h).
//
// Creation: cGZMessageQueue's constructor 0xc74c32 is called only by the cGZMessageServer constructor (its queue member
// at +0x38); cGZMessageQueue2's 0xc66b0d by the cGZMessageServer2 constructor, cGZSndSys::Init and the game's
// cGZMessageQueueManager2 (slot 5, 0xc66ff9). The game vtables are stored only by those constructors and the
// destructors 0xc74cea / 0xc66c91, so with the constructor hooks every queue carries our vtable.
#include "gz/cGZMessageServer.h"
#include "gz/cGZMessageServer2.h"
#include "gz/Framework.h" // FwRef
#include "rz/cRZCriticalSection.h"
#include "rz/cRZThreadSignal.h"

#include <cstddef>
#include <cstdint>
#include <list>

namespace gzqueue {

// The queue's lock as a member: built (0x1af64e) in member order, destroyed (0x1af68a) in reverse.
struct Lock : cRZCriticalSection {
    Lock() { Construct(); }
    ~Lock() { Destruct(); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
};

// The arrival signal: an auto-reset event (0x9ccaa9 with argument 0), destroyed by 0x9ccb13.
struct Signal : cRZThreadSignal {
    Signal() { Construct(0); }
    ~Signal() { Destruct(); }
    Signal(const Signal &) = delete;
    Signal &operator=(const Signal &) = delete;
};

// What the Wait* slots pass to the signal's Wait (microseconds): one second per try.
constexpr uint32_t kWaitMicroseconds = 1000000;

} // namespace gzqueue

class cGZMessageQueue : public cIGZMessageQueue {
public:
    static constexpr uint32_t kSize = 0x20;

    cGZMessageQueue();             // 0xc74c32
    virtual ~cGZMessageQueue();    // 0xc74cea; the deleting destructor 0xc74e42 is slot 29

    cGZMessageQueue(const cGZMessageQueue &) = delete;
    cGZMessageQueue &operator=(const cGZMessageQueue &) = delete;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override;      // 0xc75806: 1 and kIid, no check on out
    uint32_t AddRef() override;                                    // folded 0x18e960
    uint32_t Release() override;                                   // 0xc75864: deletes at a count of 1 or less

    // cIGZMessageQueue
    void PushBack(const cGZMessage *message) override;             // 0xc75792
    void PushFront(const cGZMessage *message) override;            // 0xc757cb
    bool RemoveFirstWithId(uint32_t id) override;                  // 0xc7595d
    bool RemoveFirst(const cGZMessage *message) override;          // 0xc7523c (also slot 23)
    bool Unsupported7(uint32_t value) override;                    // 0xc759a9 (also slot 8): false, under the lock
    bool Unsupported8(uint32_t value) override;                    // 0xc759a9
    uint32_t RemoveAllWithId(uint32_t id) override;                // 0xc75909
    uint32_t RemoveAll(const cGZMessage *message) override;        // 0xc758a2
    uint32_t Clear() override;                                     // 0xc7587b
    uint32_t FrontId() override;                                   // 0xc75767
    uint32_t BackId() override;                                    // 0xc75710
    bool ContainsId(uint32_t id) override;                         // 0xc756a8
    cGZMessage *Front() override;                                  // 0xc7573c
    cGZMessage *Back() override;                                   // 0xc756e4
    bool Contains(const cGZMessage *message) override;             // 0xc75663
    uint32_t PopFrontId() override;                                // 0xc753fd
    uint32_t PopBackId() override;                                 // 0xc75319
    bool RemoveFirstWithIdAgain(uint32_t id) override;             // shared 0x2b5b00: slot 5 through the vtable
    bool PopFront(cGZMessage *out) override;                       // 0xc75376
    bool PopBack(cGZMessage *out) override;                        // 0xc75290
    bool RemoveFirstAgain(const cGZMessage *message) override;     // 0xc7523c, as slot 6
    uint32_t WaitPopFrontId() override;                            // 0xc75430
    uint32_t WaitPopBackId() override;                             // 0xc7534d
    bool WaitPopFront(cGZMessage *out) override;                   // 0xc753c8
    bool WaitPopBack(cGZMessage *out) override;                    // 0xc752e4
    uint32_t Size() override;                                      // shared 0xc675c3 (the two queues' only)

    std::list<cGZMessage> mMessages; // +0x04
    gzqueue::Lock mLock;             // +0x0c
    gzqueue::Signal mSignal;         // +0x14 pulsed when a message arrives in an empty queue
    uint32_t mRefCount;              // +0x1c

private:
    // Inserts a copy before `where` under the lock; pulses the signal when the queue was empty.
    void Insert(std::list<cGZMessage>::iterator where, const cGZMessage &message);
    // Takes `node`'s message out (the lock is held).
    void TakeOut(std::list<cGZMessage>::iterator node, cGZMessage *out);
};
static_assert(sizeof(cGZMessageQueue) == cGZMessageQueue::kSize, "cGZMessageQueue size");
static_assert(offsetof(cGZMessageQueue, mMessages) == 4 && offsetof(cGZMessageQueue, mLock) == 0xc &&
                  offsetof(cGZMessageQueue, mSignal) == 0x14 && offsetof(cGZMessageQueue, mRefCount) == 0x1c,
              "cGZMessageQueue layout");

class cGZMessageQueue2 : public cIGZMessageQueue2 {
public:
    static constexpr uint32_t kSize = 0x20;
    static constexpr uint32_t kIid = 0xa52294cd;  // what QueryInterface accepts besides 1

    cGZMessageQueue2();            // 0xc66b0d
    virtual ~cGZMessageQueue2();   // 0xc66c91; the deleting destructor 0xc66e59 is slot 23

    cGZMessageQueue2(const cGZMessageQueue2 &) = delete;
    cGZMessageQueue2 &operator=(const cGZMessageQueue2 &) = delete;

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override;       // 0xc67a98: 1 and kIid, no check on out
    uint32_t AddRef() override;                                     // folded 0x18e960
    uint32_t Release() override;                                    // 0xc67b34

    // cIGZMessageQueue2
    void PushBack(cIGZMessage2 *message) override;                  // 0xc679e6
    void PushFront(cIGZMessage2 *message) override;                 // 0xc67a3e
    bool RemoveFirstWithType(uint32_t type) override;               // 0xc67cb7
    bool RemoveFirst(cIGZMessage2 *message) override;               // 0xc67c63
    bool RemoveLastWithType(uint32_t type) override;                // 0xc67d5e
    bool RemoveLast(cIGZMessage2 *message) override;                // 0xc67d09
    uint32_t RemoveAllWithType(uint32_t type) override;             // 0xc67bd3
    uint32_t RemoveAll(cIGZMessage2 *message) override;            // 0xc67b72
    uint32_t Clear() override;                                      // 0xc67b4b
    uint32_t FrontType() override;                                  // 0xc679b4
    uint32_t BackType() override;                                   // 0xc6793f
    bool ContainsType(uint32_t type) override;                      // 0xc678ba
    bool Front(cIGZMessage2 **out) override;                        // 0xc67972
    bool Back(cIGZMessage2 **out) override;                         // 0xc678fc
    bool Find(cIGZMessage2 *message, cIGZMessage2 **out) override;  // 0xc67862
    bool PopFront(cIGZMessage2 **out) override;                     // 0xc6753f
    bool PopBack(cIGZMessage2 **out) override;                      // 0xc674b9
    bool WaitPopFront(cIGZMessage2 **out) override;                 // 0xc6758e
    bool WaitPopBack(cIGZMessage2 **out) override;                  // 0xc6750a
    uint32_t Size() override;                                       // shared 0xc675c3

    // Slot 24, 0xc67453 (after the destructor): the first message equal to `message` (its slot 6) taken out of the
    // queue into *out (counted); null and false when there is none.
    virtual bool TakeFirst(cIGZMessage2 *message, cIGZMessage2 **out);

    std::list<FwRef> mMessages; // +0x04 each counted
    gzqueue::Lock mLock;        // +0x0c
    gzqueue::Signal mSignal;    // +0x14
    uint32_t mRefCount;         // +0x1c

private:
    void Insert(std::list<FwRef>::iterator where, cIGZMessage2 *message);
    // Hands `node`'s message out (AddRef), then erases the node (its reference released).
    void TakeOut(std::list<FwRef>::iterator node, cIGZMessage2 **out);
};
static_assert(sizeof(cGZMessageQueue2) == cGZMessageQueue2::kSize, "cGZMessageQueue2 size");
static_assert(offsetof(cGZMessageQueue2, mMessages) == 4 && offsetof(cGZMessageQueue2, mLock) == 0xc &&
                  offsetof(cGZMessageQueue2, mSignal) == 0x14 && offsetof(cGZMessageQueue2, mRefCount) == 0x1c,
              "cGZMessageQueue2 layout");

// The constructor and destructor hooks' entry points (`this` in ecx, the dummy edx). The servers call the original
// constructors and destructors on their queue members (OriginalFunction), so the destructors are hooked too.
struct cGZMessageQueueAdapters {
    static cGZMessageQueue *__fastcall Construct(void *memory, void *edx);   // 0xc74c32
    static void __fastcall Destruct(cGZMessageQueue *self, void *edx);       // 0xc74cea
    static cGZMessageQueue2 *__fastcall Construct2(void *memory, void *edx); // 0xc66b0d
    static void __fastcall Destruct2(cGZMessageQueue2 *self, void *edx);     // 0xc66c91
};
