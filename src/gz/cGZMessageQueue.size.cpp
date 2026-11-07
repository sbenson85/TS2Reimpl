// cGZMessageQueue in the standalone shape (lane LR). Built for size, as the game's (0xc74c32..0xc759c1).
#include "gz/cGZMessageQueue.h"

#include "game/vtable.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZThreadSignal.h"

#include <new>

namespace {

// Every slot but the Wait* ones runs under the queue's lock, taken and released through its vtable.
class Locked {
public:
    explicit Locked(cRZCriticalSection *lock) : mLock(As<ifaces::cIGZCriticalSection>(lock)) { mLock->Lock(); }
    ~Locked() { mLock->Unlock(); }
    Locked(const Locked &) = delete;
    Locked &operator=(const Locked &) = delete;

private:
    ifaces::cIGZCriticalSection *mLock;
};

// The four words compared in order (the game's 0x2144da).
bool SameMessage(const cGZMessage &a, const cGZMessage &b)
{
    return a.mId == b.mId && a.mWord4 == b.mWord4 && a.mObject == b.mObject && a.mWordC == b.mWordC;
}

} // namespace

cGZMessageQueue::cGZMessageQueue() : mRefCount(0) {}

cGZMessageQueue::~cGZMessageQueue() {}

bool cGZMessageQueue::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZMessageQueue::AddRef() { return ++mRefCount; }

uint32_t cGZMessageQueue::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

void cGZMessageQueue::Insert(std::list<cGZMessage>::iterator where, const cGZMessage &message)
{
    Locked lock(&mLock);
    const size_t before = mMessages.size();
    mMessages.insert(where, message);
    if (before == 0) As<ifaces::cIGZThreadSignal>(&mSignal)->Signal();
}

void cGZMessageQueue::PushBack(const cGZMessage *message) { Insert(mMessages.end(), *message); }

void cGZMessageQueue::PushFront(const cGZMessage *message) { Insert(mMessages.begin(), *message); }

bool cGZMessageQueue::RemoveFirstWithId(uint32_t id)
{
    Locked lock(&mLock);
    for (auto it = mMessages.begin(); it != mMessages.end(); ++it) {
        if (it->mId == id) {
            mMessages.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueue::RemoveFirst(const cGZMessage *message)
{
    Locked lock(&mLock);
    for (auto it = mMessages.begin(); it != mMessages.end(); ++it) {
        if (SameMessage(*it, *message)) {
            mMessages.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueue::Unsupported7(uint32_t)
{
    Locked lock(&mLock);
    return false;
}

bool cGZMessageQueue::Unsupported8(uint32_t value) { return cGZMessageQueue::Unsupported7(value); }

uint32_t cGZMessageQueue::RemoveAllWithId(uint32_t id)
{
    Locked lock(&mLock);
    uint32_t removed = 0;
    for (auto it = mMessages.begin(); it != mMessages.end();) {
        if (it->mId == id) {
            ++removed;
            it = mMessages.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

uint32_t cGZMessageQueue::RemoveAll(const cGZMessage *message)
{
    Locked lock(&mLock);
    uint32_t removed = 0;
    for (auto it = mMessages.begin(); it != mMessages.end();) {
        if (SameMessage(*it, *message)) {
            ++removed;
            it = mMessages.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

uint32_t cGZMessageQueue::Clear()
{
    Locked lock(&mLock);
    const uint32_t count = static_cast<uint32_t>(mMessages.size());
    mMessages.clear();
    return count;
}

uint32_t cGZMessageQueue::FrontId()
{
    Locked lock(&mLock);
    return mMessages.empty() ? 0 : mMessages.front().mId;
}

uint32_t cGZMessageQueue::BackId()
{
    Locked lock(&mLock);
    return mMessages.empty() ? 0 : mMessages.back().mId;
}

bool cGZMessageQueue::ContainsId(uint32_t id)
{
    Locked lock(&mLock);
    for (const cGZMessage &queued : mMessages)
        if (queued.mId == id) return true;
    return false;
}

cGZMessage *cGZMessageQueue::Front()
{
    Locked lock(&mLock);
    return mMessages.empty() ? nullptr : &mMessages.front();
}

cGZMessage *cGZMessageQueue::Back()
{
    Locked lock(&mLock);
    return mMessages.empty() ? nullptr : &mMessages.back();
}

bool cGZMessageQueue::Contains(const cGZMessage *message)
{
    Locked lock(&mLock);
    for (const cGZMessage &queued : mMessages)
        if (SameMessage(queued, *message)) return true;
    return false;
}

uint32_t cGZMessageQueue::PopFrontId()
{
    Locked lock(&mLock);
    if (mMessages.empty()) return 0;
    const uint32_t id = mMessages.front().mId;
    mMessages.pop_front();
    return id;
}

uint32_t cGZMessageQueue::PopBackId()
{
    Locked lock(&mLock);
    if (mMessages.empty()) return 0;
    const uint32_t id = mMessages.back().mId;
    mMessages.pop_back();
    return id;
}

bool cGZMessageQueue::RemoveFirstWithIdAgain(uint32_t id) { return RemoveFirstWithId(id); }

void cGZMessageQueue::TakeOut(std::list<cGZMessage>::iterator node, cGZMessage *out)
{
    *out = *node;
    mMessages.erase(node);
}

bool cGZMessageQueue::PopFront(cGZMessage *out)
{
    Locked lock(&mLock);
    const bool any = !mMessages.empty();
    if (any) TakeOut(mMessages.begin(), out);
    return any;
}

bool cGZMessageQueue::PopBack(cGZMessage *out)
{
    Locked lock(&mLock);
    const bool any = !mMessages.empty();
    if (any) TakeOut(std::prev(mMessages.end()), out);
    return any;
}

bool cGZMessageQueue::RemoveFirstAgain(const cGZMessage *message) { return cGZMessageQueue::RemoveFirst(message); }

// The Wait* slots try their non-waiting slot through the vtable until it yields a message, waiting on the signal
// between tries (no lock of their own). An id of 0 counts as no message.
uint32_t cGZMessageQueue::WaitPopFrontId()
{
    uint32_t id;
    while ((id = PopFrontId()) == 0) As<ifaces::cIGZThreadSignal>(&mSignal)->Wait(gzqueue::kWaitMicroseconds);
    return id;
}

uint32_t cGZMessageQueue::WaitPopBackId()
{
    uint32_t id;
    while ((id = PopBackId()) == 0) As<ifaces::cIGZThreadSignal>(&mSignal)->Wait(gzqueue::kWaitMicroseconds);
    return id;
}

bool cGZMessageQueue::WaitPopFront(cGZMessage *out)
{
    while (!PopFront(out)) As<ifaces::cIGZThreadSignal>(&mSignal)->Wait(gzqueue::kWaitMicroseconds);
    return true;
}

bool cGZMessageQueue::WaitPopBack(cGZMessage *out)
{
    while (!PopBack(out)) As<ifaces::cIGZThreadSignal>(&mSignal)->Wait(gzqueue::kWaitMicroseconds);
    return true;
}

uint32_t cGZMessageQueue::Size()
{
    Locked lock(&mLock);
    return static_cast<uint32_t>(mMessages.size());
}

// ---- adapters --------------------------------------------------------------------------------------------------------

cGZMessageQueue *__fastcall cGZMessageQueueAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZMessageQueue;
}

void __fastcall cGZMessageQueueAdapters::Destruct(cGZMessageQueue *self, void *)
{
    self->cGZMessageQueue::~cGZMessageQueue();
}
