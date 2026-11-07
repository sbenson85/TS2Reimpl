#include "gz/cGZMessageServer2.h"

#include "gz/cGZMessageQueue.h"
#include "game/original.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <algorithm>

bool cGZMessageServer2::sReplayingPending = false;

cGZMessageServer2::GameMessageQueue::GameMessageQueue()
{
    cGZMessageQueueAdapters::Construct2(raw, nullptr); // our queue (lane LR), built in the member's storage
}

cGZMessageServer2::GameMessageQueue::~GameMessageQueue()
{
    cGZMessageQueueAdapters::Destruct2(reinterpret_cast<cGZMessageQueue2 *>(raw), nullptr);
}

cGZMessageServer2::cGZMessageServer2()
    : rz::cRZSystemService(kServiceId, kPriority), mProcessed(0), mAlwaysClearQueue(true), mDepth(0),
      mGeneralRemaining(0)
{
}

cGZMessageServer2::~cGZMessageServer2() noexcept
{
    // Every registration released, type by type in the map's order, each list from its front (the node let go of its
    // target before the target is released).
    mTargetsLock.Lock();
    mDepth = 0;
    while (!mTargets.empty()) {
        const TargetMap::iterator first = mTargets.begin();
        TargetList *const targets = first->second;
        mTargets.erase(first);
        if (!targets) continue;
        while (!targets->empty()) {
            cIGZMessageTarget2 *const target = targets->front().Detach();
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

cIGZSystemService *__cdecl cGZMessageServer2::Create()
{
    return new cGZMessageServer2; // the +4 face
}

bool cGZMessageServer2::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cIGZMessageServer2 *>(this); // no check on out
    static_cast<cIGZMessageServer2 *>(this)->AddRef();
    return true;
}

uint32_t cGZMessageServer2::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZMessageServer2::Release() { return rz::cRZSystemService::Release(); }
uint32_t cGZMessageServer2::RefCount() { return rz::cRZUnknownMT::RefCount(); }

bool cGZMessageServer2::Init()
{
    return As<ifaces::cIGZFrameWork>(GetServiceObject())
        ->AddToTick(As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
}

bool cGZMessageServer2::Shutdown()
{
    return As<ifaces::cIGZFrameWork>(GetServiceObject())
        ->RemoveFromTick(As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
}

bool cGZMessageServer2::OnTick(uint32_t)
{
    // The queue: the messages waiting when the tick began (more when mAlwaysClearQueue: until it is empty, also of what
    // the sends post), each sent in turn and then let go of.
    cIGZMessage2 *message = nullptr;
    mQueueLock.Lock();
    const uint64_t stopAt = mProcessed + mQueue->Size();
    mQueueLock.Unlock();
    for (;;) {
        mQueueLock.Lock();
        const bool popped = mQueue->PopFront(&message);
        if (popped) ++mProcessed;
        mQueueLock.Unlock();
        if (!popped) break;
        MessageSend(message);
        message->Release();
        if (!mAlwaysClearQueue && mProcessed >= stopAt) break;
    }

    mGeneralLock.Lock();
    const bool generalPosts = !mGeneralPosts.empty();
    mGeneralLock.Unlock();
    if (generalPosts) DispatchGeneralPosts();
    return true;
}

bool cGZMessageServer2::MessageSend(cIGZMessage2 *message)
{
    // The targets of the message's type, then those of type 0, are collected (counted) under the lock, then called
    // without it: a handler may register, unregister, send or post.
    std::vector<TargetRef> targets;
    mTargetsLock.Lock();
    ApplyPendingChanges();
    ++mDepth;
    for (uint32_t type = message->GetType();; type = 0) {
        const TargetMap::iterator found = mTargets.find(type);
        if (found != mTargets.end()) {
            for (const TargetRef &target : *found->second) {
                targets.emplace_back();
                targets.back().Assign(target.Get());
            }
        }
        if (type == 0) break;
    }
    --mDepth;
    mTargetsLock.Unlock();

    for (const TargetRef &target : targets) target->DoMessage(message);
    return true;
}

bool cGZMessageServer2::MessagePost(cIGZMessage2 *message, bool urgent)
{
    mQueueLock.Lock();
    if (urgent)
        mQueue->PushFront(message);
    else
        mQueue->PushBack(message);
    mQueueLock.Unlock();
    return true;
}

bool cGZMessageServer2::AddNotification(cIGZMessageTarget2 *target, uint32_t type)
{
    mTargetsLock.Lock();
    if (mDepth != 0) {
        // A send is collecting: the change waits, holding the target.
        const Registration registration = {TargetRef(target), type};
        mPending.push_back(PendingChange{true, registration});
    } else {
        mDepth = 1;
        const TargetMap::iterator found = mTargets.find(type);
        TargetList *targets;
        if (found == mTargets.end()) {
            targets = new TargetList;
            mTargets.emplace(type, targets);
        } else {
            targets = found->second;
        }
        const bool present = std::any_of(targets->begin(), targets->end(),
                                         [target](const TargetRef &held) { return held.Get() == target; });
        if (!present) {
            targets->emplace_back();
            targets->back().Assign(target);
        }
        --mDepth;
        ProcessPending();
    }
    mTargetsLock.Unlock();
    return true;
}

bool cGZMessageServer2::RemoveNotification(cIGZMessageTarget2 *target, uint32_t type)
{
    mTargetsLock.Lock();
    if (mDepth != 0) {
        const Registration registration = {TargetRef(target), type};
        mPending.push_back(PendingChange{false, registration});
    } else {
        mDepth = 1;
        const TargetMap::iterator found = mTargets.find(type);
        if (found != mTargets.end()) {
            TargetList *const targets = found->second;
            const TargetList::iterator held = std::find_if(
                targets->begin(), targets->end(), [target](const TargetRef &t) { return t.Get() == target; });
            if (held != targets->end()) {
                targets->erase(held); // the first copy only
                if (targets->empty()) {
                    delete targets;
                    mTargets.erase(type);
                }
            }
        }
        --mDepth;
        ProcessPending();
    }
    mTargetsLock.Unlock();
    return true;
}

void cGZMessageServer2::ApplyPendingChanges()
{
    if (mPending.empty()) return;
    for (const PendingChange &change : mPending) {
        const Registration &r = change.registration;
        const auto same = [&r](const TargetRef &held) { return held.Get() == r.target.Get(); };
        const TargetMap::iterator found = mTargets.find(r.type);
        if (change.add) {
            // ORIGINAL-BUG: this runs at depth 0, and the copy's AddRef runs before its list node is linked, so a target
            // that re-registers itself from AddRef is not seen by AddNotification's presence check: the list then holds
            // it twice (two deliveries per send; RemoveNotification takes one copy, a pending remove both).
            if (found == mTargets.end()) {
                TargetList *const targets = new TargetList;
                mTargets.emplace(r.type, targets);
                targets->push_back(r.target);
            } else if (std::none_of(found->second->begin(), found->second->end(), same)) {
                found->second->push_back(r.target);
            }
        } else if (found != mTargets.end()) {
            TargetList *const targets = found->second;
            if (std::any_of(targets->begin(), targets->end(), same)) {
                targets->remove_if(same); // every copy
                if (targets->empty()) {
                    delete targets;
                    mTargets.erase(r.type);
                }
            }
        }
    }
    mPending.clear();
}

void cGZMessageServer2::ProcessPending()
{
    mTargetsLock.Lock();
    if (!sReplayingPending) {
        sReplayingPending = true;
        if (mDepth == 0) {
            while (!mPending.empty()) {
                const PendingChange &change = mPending.front();
                if (change.add)
                    AddNotification(change.registration.target.Get(), change.registration.type);
                else
                    RemoveNotification(change.registration.target.Get(), change.registration.type);
                mPending.pop_front(); // the change's hold on the target goes with it
            }
        }
        sReplayingPending = false;
    }
    mTargetsLock.Unlock();
}

int32_t cGZMessageServer2::GeneralMessagePostToTarget(cIGZMessage2 *message, cIGZMessageTarget2 *target)
{
    mGeneralLock.Lock();
    {
        const GeneralPost post = {TargetRef(target), MessageRef(message)};
        mGeneralPosts.push_back(post);
    }
    return mGeneralLock.Unlock();
}

int32_t cGZMessageServer2::CancelGeneralMessagePostsToTarget(cIGZMessageTarget2 *target)
{
    mGeneralLock.Lock();
    // As the first server's (gz/cGZMessageServer.size.cpp): an absolute-index walk, one step after every element.
    for (size_t i = 0; i < mGeneralPosts.size();) {
        if (mGeneralPosts[i].target.Get() != target) {
            ++i;
            continue;
        }
        const bool frontMoves = i < mGeneralPosts.size() - i - 1;
        mGeneralPosts.erase(mGeneralPosts.begin() + static_cast<ptrdiff_t>(i));
        // ORIGINAL-BUG: when the posts after it moved forward, the next post is skipped (it stays queued); and when the
        // erased post was the last, the original walks past the end (reading stale blocks, or crashing). Ours stops at
        // the end.
        if (!frontMoves) ++i;
    }
    return mGeneralLock.Unlock();
}

uint32_t cGZMessageServer2::GetMessageQueueSize()
{
    mQueueLock.Lock();
    const uint32_t size = mQueue->Size();
    mQueueLock.Unlock();
    return size;
}

void cGZMessageServer2::SetAlwaysClearQueueOnTick(bool clear)
{
    mAlwaysClearQueue = clear;
}

bool cGZMessageServer2::CreateMessage(uint32_t clsid, uint32_t iid, void **out)
{
    if (clsid != kStandardMessageClsid) return false;
    void *const memory = RZMemory::Malloc(0x2c);
    cIGZUnknown *const message =
        memory ? reinterpret_cast<cIGZUnknown *>(static_cast<cRZMessage2Standard *>(memory)->Construct()) : nullptr;
    if (message) message->AddRef();
    const bool found = message->QueryInterface(iid, out); // no check, as the original
    if (message) message->Release();
    return found;
}

void cGZMessageServer2::DispatchGeneralPosts()
{
    // A round delivers at most the posts waiting when it began (posts made by the handlers wait for the next round). A
    // nested round (a handler ticking the server) continues the outer round's count.
    GeneralPost post;
    mGeneralLock.Lock();
    if (mGeneralRemaining <= 0) mGeneralRemaining = static_cast<int32_t>(mGeneralPosts.size());
    while (mGeneralRemaining > 0) {
        --mGeneralRemaining;
        if (mGeneralPosts.empty()) continue; // cancelled meanwhile
        post = mGeneralPosts.front();
        mGeneralPosts.pop_front();
        mGeneralLock.Unlock();
        post.target->DoMessage(post.message.Get());
        mGeneralLock.Lock();
    }
    --mGeneralRemaining;
    mGeneralLock.Unlock();
}
