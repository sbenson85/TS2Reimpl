#include "simantics/TreeSim.h"

#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZRefCount.h"
#include "simantics/TreeSimInternal.h"

#include <new>

using namespace treesim;

// Construction and destruction of the interpreter state (cTSTreeSim: the constructor 0x5755e0, the destructor body
// 0x5759f0 and the deleting destructor 0x575c30).

namespace {

constexpr uint32_t kDebugServiceGlobal = 0x10cc294;

using RefFn = uint32_t(__thiscall *)(void *);

void AddRef(void *object)
{
    VirtualCall<RefFn, 1>(object);
}

void Release(void *object)
{
    VirtualCall<RefFn, 2>(object);
}

// Replaces a held service pointer: the new object gets its reference before the field changes and the old one is
// released afterwards (and nothing happens when they are the same).
void Assign(void *&field, void *object)
{
    void *old = field;
    if (object == old) return;
    if (object) AddRef(object);
    field = object;
    if (old) Release(old);
}

// Takes the reference a held service pointer carries away: the field is cleared first, then the object released.
void Drop(void *&field)
{
    void *object = field;
    if (!object) return;
    field = nullptr;
    Release(object);
}

} // namespace

cTSTreeSim *cTSTreeSim::Construct()
{
    mVTable = GameAddress<void>(kVTable);
    mUnknown.Construct(); // sets the cRZUnknown vtable (0xda2fd0) and a zero count
    mUnknown.mVTable = GameAddress<void>(kUnknownVTable);
    mStepCount = 0;
    mStepBase = 0;
    mField14 = 0;
    mStack.mVTable = GameAddress<void>(cTSTreeStack::kVTable);
    new (&mStack.mFrames) std::vector<TreeFrameRef>();
    mNextNode = kTargetNone;
    mLastBranch = 0;
    mDefaultArgs = nullptr;
    mErrorCode = 0;
    mResult = 0;
    mCallDepth = 0;
    mContext = nullptr;
    mDebugService = nullptr;
    mServiceC = nullptr;
    mGate = nullptr;

    mReplayMap.Construct();
    mRecording = 0;

    // The services: the context, the service C, the context's gate, and the debugger service when there is one.
    Assign(mContext, GetCachedSingleton());
    Assign(mServiceC, GetCachedSingletonC());
    Assign(mGate, As<Context>(mContext)->GetGate());
    void *debug = *GameAddress<void *>(kDebugServiceGlobal);
    if (debug && debug != mDebugService) Assign(mDebugService, debug);
    return this;
}

void cTSTreeSim::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
    mUnknown.mVTable = GameAddress<void>(kUnknownVTable);
    Drop(mDebugService);
    Drop(mGate);
    Drop(mServiceC);
    Drop(mContext);
    mReplayMap.Destroy();
    // The members' own destructors find the four service fields empty.
    mStack.mVTable = GameAddress<void>(cTSTreeStack::kVTable);
    std::vector<TreeFrameRef>().swap(mStack.mFrames);
    reinterpret_cast<cRZUnknown *>(&mUnknown)->Destruct();
}

cTSTreeSim *cTSTreeSim::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}
