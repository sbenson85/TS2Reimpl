#include "gz/cGZMessageServer.h"

#include "gz/cGZMessageQueue.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <algorithm>

bool cGZMessageServer::sReplayingPending = false;

cGZMessageServer::GameMessageQueue::GameMessageQueue()
{
    cGZMessageQueueAdapters::Construct(raw, nullptr); // our queue (lane LR), built in the member's storage
}

cGZMessageServer::GameMessageQueue::~GameMessageQueue()
{
    cGZMessageQueueAdapters::Destruct(reinterpret_cast<cGZMessageQueue *>(raw), nullptr);
}

cGZMessageServer::cGZMessageServer()
    : rz::cRZSystemService(kServiceId, kPriority), mProcessed(0), mDepth(0), mAlwaysClearQueue(true),
      mGeneralRemaining(0)
{
}

cGZMessageServer::~cGZMessageServer() noexcept
{
    // Every registration released, id by id in the map's order, each list from its front.
    mTargetsLock.Lock();
    mDepth = 0;
    while (!mTargets.empty()) {
        const TargetMap::iterator first = mTargets.begin();
        TargetList *const targets = first->second;
        mTargets.erase(first);
        if (!targets) continue;
        while (!targets->empty()) {
            cIGZMessageTarget *const target = targets->front();
            targets->pop_front();
            try {
                target->Release();
            } catch (...) {
                // a target that throws from Release is dropped all the same
            }
        }
        delete targets;
    }
    mTargetsLock.Unlock();
}

cIGZSystemService *__cdecl cGZMessageServer::Create()
{
    return new cGZMessageServer;
}

bool cGZMessageServer::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cIGZMessageServer *>(this); // no check on out
    AddRef();
    return true;
}

uint32_t cGZMessageServer::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZMessageServer::Release() { return rz::cRZSystemService::Release(); }

bool cGZMessageServer::Init()
{
    void *const framework = GetServiceObject();
    if (!framework) return false;
    return As<ifaces::cIGZFrameWork>(framework)->AddToTick(
        As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
}

bool cGZMessageServer::Shutdown()
{
    void *const framework = GetServiceObject();
    if (!framework) return true;
    return As<ifaces::cIGZFrameWork>(framework)->RemoveFromTick(
        As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
}

bool cGZMessageServer::OnTick(uint32_t)
{
    // The queue: the messages waiting when the tick began (more when mAlwaysClearQueue: until it is empty, also of what
    // the sends post), each sent in turn.
    cGZMessage message = {};
    mQueueLock.Lock();
    const uint64_t stopAt = mProcessed + mQueue->Size();
    mQueueLock.Unlock();
    for (;;) {
        mQueueLock.Lock();
        const bool popped = mQueue->PopFront(&message);
        if (popped) ++mProcessed;
        mQueueLock.Unlock();
        if (!popped) break;
        MessageSend(&message);
        if (!mAlwaysClearQueue && mProcessed >= stopAt) break;
    }

    mGeneralLock.Lock();
    const bool generalPosts = !mGeneralPosts.empty();
    mGeneralLock.Unlock();
    if (generalPosts) DispatchGeneralPosts();
    return true;
}

bool cGZMessageServer::MessageSend(cGZMessage *message)
{
    // The targets are collected (and counted) under the lock, then called without it: a handler may register,
    // unregister, send or post.
    cIGZMessageTarget *targets[kMaxSendTargets + 1];
    size_t count = 0;
    mTargetsLock.Lock();
    ApplyPendingChanges();
    ++mDepth;
    const uint32_t id = message->mId;
    if (id != 0) {
        const TargetMap::iterator found = mTargets.find(id);
        if (found != mTargets.end()) {
            for (cIGZMessageTarget *const target : *found->second) {
                // ORIGINAL-BUG: the original's array has no bound; a 384th target overruns its stack frame. Ours
                // delivers to the first 383.
                if (count == kMaxSendTargets) break;
                targets[count++] = target;
                target->AddRef();
            }
        }
    }
    targets[count] = nullptr;
    --mDepth;
    mTargetsLock.Unlock();

    for (size_t i = 0; targets[i]; ++i) {
        targets[i]->DoMessage(message);
        targets[i]->Release();
    }
    return true;
}

bool cGZMessageServer::MessagePost(cGZMessage *message, bool urgent)
{
    mQueueLock.Lock();
    if (urgent)
        mQueue->PushFront(message);
    else
        mQueue->PushBack(message);
    mQueueLock.Unlock();
    return true;
}

bool cGZMessageServer::AppendTarget(uint32_t id, cIGZMessageTarget *target)
{
    const TargetMap::iterator found = mTargets.find(id);
    if (found == mTargets.end()) {
        TargetList *const targets = new TargetList;
        mTargets.emplace(id, targets);
        targets->push_back(target);
        return true;
    }
    TargetList *const targets = found->second;
    if (std::find(targets->begin(), targets->end(), target) != targets->end()) return false;
    targets->push_back(target);
    return true;
}

bool cGZMessageServer::DropTarget(uint32_t id, cIGZMessageTarget *target)
{
    const TargetMap::iterator found = mTargets.find(id);
    if (found == mTargets.end()) return false;
    TargetList *const targets = found->second;
    if (std::find(targets->begin(), targets->end(), target) == targets->end()) return false;
    targets->remove(target);
    target->Release();
    if (targets->empty()) {
        delete targets;
        mTargets.erase(id);
    }
    return true;
}

bool cGZMessageServer::AddNotification(cIGZMessageTarget *target, uint32_t id)
{
    mTargetsLock.Lock();
    if (mDepth != 0) {
        // A send is collecting: the change waits, holding its own reference.
        mPending.push_back({true, target, id});
        target->AddRef();
    } else {
        mDepth = 1;
        if (AppendTarget(id, target)) target->AddRef();
        --mDepth;
        ProcessPending();
    }
    mTargetsLock.Unlock();
    return true;
}

bool cGZMessageServer::RemoveNotification(cIGZMessageTarget *target, uint32_t id)
{
    mTargetsLock.Lock();
    if (mDepth != 0) {
        mPending.push_back({false, target, id});
    } else {
        mDepth = 1;
        DropTarget(id, target);
        --mDepth;
        ProcessPending();
    }
    mTargetsLock.Unlock();
    return true;
}

void cGZMessageServer::ApplyPendingChanges()
{
    if (mPending.empty()) return;
    for (const PendingChange &change : mPending) {
        if (change.add)
            // ORIGINAL-BUG: an add whose target is registered already keeps the reference the pending add took.
            AppendTarget(change.id, change.target);
        else
            DropTarget(change.id, change.target);
    }
    mPending.clear();
}

void cGZMessageServer::ProcessPending()
{
    mTargetsLock.Lock();
    if (!sReplayingPending) {
        sReplayingPending = true;
        if (mDepth == 0) {
            while (!mPending.empty()) {
                const PendingChange &change = mPending.front();
                if (change.add) {
                    AddNotification(change.target, change.id);
                    change.target->Release(); // the pending add's own reference
                } else {
                    RemoveNotification(change.target, change.id);
                }
                mPending.pop_front();
            }
        }
        sReplayingPending = false;
    }
    mTargetsLock.Unlock();
}

int32_t cGZMessageServer::GeneralMessagePostToTarget(cGZMessage *message, cIGZMessageTarget *target)
{
    mGeneralLock.Lock();
    mGeneralPosts.push_back({target, *message});
    return mGeneralLock.Unlock();
}

int32_t cGZMessageServer::CancelGeneralMessagePostsToTarget(cIGZMessageTarget *target)
{
    mGeneralLock.Lock();
    // The original walks the deque by absolute index, one step after every element. Erasing shifts the shorter side
    // (std::deque::erase): the elements before it move back (the front advances; the next element keeps its index), or
    // those after it move forward (the next element takes the erased one's index).
    for (size_t i = 0; i < mGeneralPosts.size();) {
        if (mGeneralPosts[i].target != target) {
            ++i;
            continue;
        }
        const bool frontMoves = i < mGeneralPosts.size() - i - 1;
        mGeneralPosts.erase(mGeneralPosts.begin() + static_cast<ptrdiff_t>(i));
        // ORIGINAL-BUG: when the elements after it moved forward, the next post is skipped (it stays queued); and when
        // the erased post was the last, the original walks past the end (reading stale blocks, or crashing). Ours stops
        // at the end.
        if (!frontMoves) ++i;
    }
    return mGeneralLock.Unlock();
}

uint32_t cGZMessageServer::GetMessageQueueSize()
{
    mQueueLock.Lock();
    const uint32_t size = mQueue->Size();
    mQueueLock.Unlock();
    return size;
}

void cGZMessageServer::SetAlwaysClearQueueOnTick(bool clear)
{
    mAlwaysClearQueue = clear;
}

void cGZMessageServer::DispatchGeneralPosts()
{
    // A round delivers at most the posts waiting when it began (posts made by the handlers wait for the next round). A
    // nested round (a handler ticking the server) continues the outer round's count.
    GeneralPost post = {};
    mGeneralLock.Lock();
    if (mGeneralRemaining <= 0) mGeneralRemaining = static_cast<int32_t>(mGeneralPosts.size());
    while (mGeneralRemaining > 0) {
        --mGeneralRemaining;
        if (mGeneralPosts.empty()) continue; // cancelled meanwhile
        post = mGeneralPosts.front();
        mGeneralPosts.pop_front();
        mGeneralLock.Unlock();
        post.target->DoMessage(&post.message);
        mGeneralLock.Lock();
    }
    --mGeneralRemaining;
    mGeneralLock.Unlock();
}
