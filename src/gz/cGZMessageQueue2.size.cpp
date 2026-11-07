// cGZMessageQueue2 in the standalone shape (lane LR). Built for size, as the game's (0xc66b0d..0xc67db1).
#include "gz/cGZMessageQueue.h"

#include "game/vtable.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZThreadSignal.h"

#include <new>

namespace {

class Locked {
public:
    explicit Locked(cRZCriticalSection *lock) : mLock(As<ifaces::cIGZCriticalSection>(lock)) { mLock->Lock(); }
    ~Locked() { mLock->Unlock(); }
    Locked(const Locked &) = delete;
    Locked &operator=(const Locked &) = delete;

private:
    ifaces::cIGZCriticalSection *mLock;
};

cIGZMessage2 *MessageOf(const FwRef &ref) { return static_cast<cIGZMessage2 *>(ref.mObject); }

} // namespace

cGZMessageQueue2::cGZMessageQueue2() : mRefCount(0) {}

cGZMessageQueue2::~cGZMessageQueue2() {}

bool cGZMessageQueue2::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZMessageQueue2::AddRef() { return ++mRefCount; }

uint32_t cGZMessageQueue2::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// The message is held by a temporary reference while a copy of it (a second reference) goes into the list.
void cGZMessageQueue2::Insert(std::list<FwRef>::iterator where, cIGZMessage2 *message)
{
    Locked lock(&mLock);
    const size_t before = mMessages.size();
    {
        const FwRef held(message);
        mMessages.insert(where, held);
    }
    if (before == 0) As<ifaces::cIGZThreadSignal>(&mSignal)->Signal();
}

void cGZMessageQueue2::PushBack(cIGZMessage2 *message) { Insert(mMessages.end(), message); }

void cGZMessageQueue2::PushFront(cIGZMessage2 *message) { Insert(mMessages.begin(), message); }

bool cGZMessageQueue2::RemoveFirstWithType(uint32_t type)
{
    Locked lock(&mLock);
    for (auto it = mMessages.begin(); it != mMessages.end(); ++it) {
        if (MessageOf(*it)->GetType() == type) {
            mMessages.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueue2::RemoveFirst(cIGZMessage2 *message)
{
    Locked lock(&mLock);
    for (auto it = mMessages.begin(); it != mMessages.end(); ++it) {
        if (MessageOf(*it)->IsEqual(message)) {
            mMessages.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueue2::RemoveLastWithType(uint32_t type)
{
    Locked lock(&mLock);
    for (auto it = mMessages.end(); it != mMessages.begin();) {
        --it;
        if (MessageOf(*it)->GetType() == type) {
            mMessages.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZMessageQueue2::RemoveLast(cIGZMessage2 *message)
{
    Locked lock(&mLock);
    for (auto it = mMessages.end(); it != mMessages.begin();) {
        --it;
        if (MessageOf(*it)->IsEqual(message)) {
            mMessages.erase(it);
            return true;
        }
    }
    return false;
}

uint32_t cGZMessageQueue2::RemoveAllWithType(uint32_t type)
{
    Locked lock(&mLock);
    uint32_t removed = 0;
    for (auto it = mMessages.begin(); it != mMessages.end();) {
        if (MessageOf(*it)->GetType() == type) {
            ++removed;
            it = mMessages.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

uint32_t cGZMessageQueue2::RemoveAll(cIGZMessage2 *message)
{
    Locked lock(&mLock);
    uint32_t removed = 0;
    for (auto it = mMessages.begin(); it != mMessages.end();) {
        if (MessageOf(*it)->IsEqual(message)) {
            ++removed;
            it = mMessages.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

uint32_t cGZMessageQueue2::Clear()
{
    Locked lock(&mLock);
    const uint32_t count = static_cast<uint32_t>(mMessages.size());
    mMessages.clear();
    return count;
}

uint32_t cGZMessageQueue2::FrontType()
{
    Locked lock(&mLock);
    return mMessages.empty() ? 0 : MessageOf(mMessages.front())->GetType();
}

uint32_t cGZMessageQueue2::BackType()
{
    Locked lock(&mLock);
    return mMessages.empty() ? 0 : MessageOf(mMessages.back())->GetType();
}

bool cGZMessageQueue2::ContainsType(uint32_t type)
{
    Locked lock(&mLock);
    for (const FwRef &queued : mMessages)
        if (MessageOf(queued)->GetType() == type) return true;
    return false;
}

// A message handed out through an out argument carries a reference for the caller.
static void HandOut(cIGZMessage2 *message, cIGZMessage2 **out)
{
    *out = message;
    message->AddRef();
}

bool cGZMessageQueue2::Front(cIGZMessage2 **out)
{
    Locked lock(&mLock);
    if (mMessages.empty()) {
        *out = nullptr;
        return false;
    }
    HandOut(MessageOf(mMessages.front()), out);
    return true;
}

bool cGZMessageQueue2::Back(cIGZMessage2 **out)
{
    Locked lock(&mLock);
    if (mMessages.empty()) {
        *out = nullptr;
        return false;
    }
    HandOut(MessageOf(mMessages.back()), out);
    return true;
}

bool cGZMessageQueue2::Find(cIGZMessage2 *message, cIGZMessage2 **out)
{
    Locked lock(&mLock);
    for (const FwRef &queued : mMessages) {
        if (MessageOf(queued)->IsEqual(message)) {
            HandOut(MessageOf(queued), out);
            return true;
        }
    }
    *out = nullptr;
    return false;
}

void cGZMessageQueue2::TakeOut(std::list<FwRef>::iterator node, cIGZMessage2 **out)
{
    HandOut(MessageOf(*node), out);
    mMessages.erase(node);
}

bool cGZMessageQueue2::PopFront(cIGZMessage2 **out)
{
    Locked lock(&mLock);
    if (mMessages.empty()) {
        *out = nullptr;
        return false;
    }
    TakeOut(mMessages.begin(), out);
    return true;
}

bool cGZMessageQueue2::PopBack(cIGZMessage2 **out)
{
    Locked lock(&mLock);
    if (mMessages.empty()) {
        *out = nullptr;
        return false;
    }
    TakeOut(std::prev(mMessages.end()), out);
    return true;
}

// As cGZMessageQueue's: the non-waiting slot through the vtable until it yields, a 1000 ms wait between tries.
bool cGZMessageQueue2::WaitPopFront(cIGZMessage2 **out)
{
    while (!PopFront(out)) As<ifaces::cIGZThreadSignal>(&mSignal)->Wait(gzqueue::kWaitMicroseconds);
    return true;
}

bool cGZMessageQueue2::WaitPopBack(cIGZMessage2 **out)
{
    while (!PopBack(out)) As<ifaces::cIGZThreadSignal>(&mSignal)->Wait(gzqueue::kWaitMicroseconds);
    return true;
}

uint32_t cGZMessageQueue2::Size()
{
    Locked lock(&mLock);
    return static_cast<uint32_t>(mMessages.size());
}

bool cGZMessageQueue2::TakeFirst(cIGZMessage2 *message, cIGZMessage2 **out)
{
    Locked lock(&mLock);
    for (auto it = mMessages.begin(); it != mMessages.end(); ++it) {
        if (MessageOf(*it)->IsEqual(message)) {
            TakeOut(it, out);
            return true;
        }
    }
    *out = nullptr;
    return false;
}

// ---- adapters --------------------------------------------------------------------------------------------------------

cGZMessageQueue2 *__fastcall cGZMessageQueueAdapters::Construct2(void *memory, void *)
{
    return ::new (memory) cGZMessageQueue2;
}

void __fastcall cGZMessageQueueAdapters::Destruct2(cGZMessageQueue2 *self, void *)
{
    self->cGZMessageQueue2::~cGZMessageQueue2();
}
