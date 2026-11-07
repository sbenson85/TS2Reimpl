// rz::cRZCOMDllDirector's slots and non-virtual members (src/rz/RZCOMDllDirectorShape.h). Speed-built: matchcheck finds
// AddCls / AddClsDirect speed-built and the other bodies size-built; neither profile matches them yet (lane EO tried both).
#include "rz/COMDirectors.h"
#include "rz/RZCOMDllDirectorShape.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"

#include <vcruntime.h>

extern "C" int __cdecl _purecall();

namespace rz {

// ---------------------------------------------------------------------------------------------
// The class map and the sub-director list

void cRZCOMDllDirector::AddCls(uint32_t clsid, void *factory)
{
    // 0x1ae080 -> 0x1adf65: find the id first; only a new id gets a node (the first registration wins).
    mClasses.try_emplace(clsid, comdir::ClassEntry{factory, comdir::kObjectFactory});
}

void cRZCOMDllDirector::AddClsDirect(uint32_t clsid, void *factory)
{
    // 0x1ae0ad: the same body with kind 2.
    mClasses.try_emplace(clsid, comdir::ClassEntry{factory, comdir::kCreateFunction});
}

void cRZCOMDllDirector::AddDirector(cRZCOMDllDirector *sub)
{
    // 0x1ae0da. The original pushes the path's address before calling GZCOM() (slot 12, no stack arguments), so the
    // word stays on the stack as the second argument of the InitializeCOM call that follows.
    sub->InitializeCOM(GZCOM(), *mLibraryPath.value.AsInterface());
    // The end of sub's list is read again on every iteration, as the original does.
    for (auto it = sub->mChildren.begin(); it != sub->mChildren.end(); ++it) AddDirector(*it);
    mChildren.push_back(sub);
}

// ---------------------------------------------------------------------------------------------
// cIGZUnknown

bool cRZCOMDllDirector::QueryInterface(uint32_t riid, void **out)
{
    // 0x1ae298. cIGZUnknown and the director interface (0xa21ee941) answer with this; the framework hooks interface
    // (0x03fa40bf) answers with the +4 face. Each AddRef's this through slot 1.
    if (riid == 1 || riid == 0xa21ee941) {
        *out = static_cast<cIGZCOMDirector *>(this);
    } else if (riid == 0x03fa40bf) {
        *out = static_cast<cIGZFrameWorkHooks *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cRZCOMDllDirector::AddRef()
{
    // 0x145260 (shared with other classes whose count is at +8).
    return ++mRefCount;
}

uint32_t cRZCOMDllDirector::Release()
{
    // 0x1ae2f0: tail jump to slot 10 of the object's own vtable.
    return RemoveRef();
}

// ---------------------------------------------------------------------------------------------
// cIGZCOMDirector

bool cRZCOMDllDirector::InitializeCOM(cIGZCOM *com, const cIGZString &libraryPath)
{
    // 0x1ae24d.
    if (com == nullptr) return false;
    mCOM = com;
    mFramework = com->FrameWork();
    *mLibraryPath.value.AsInterface() = libraryPath; // slot 16 of the string, through its vtable
    for (auto it = mChildren.begin(); it != mChildren.end(); ++it) (*it)->InitializeCOM(com, libraryPath);
    return true;
}

bool cRZCOMDllDirector::OnStart(cIGZCOM *)
{
    // 0x148260 (ret 4).
    return true;
}

void cRZCOMDllDirector::EnumClassObjects(void *callback, void *context)
{
    // 0x1ae148: the sub-directors first (slot 5), then this director's own ids in list order.
    for (auto it = mChildren.begin(); it != mChildren.end(); ++it) (*it)->EnumClassObjects(callback, context);
    auto fn = reinterpret_cast<comdir::EnumCallback>(callback);
    for (auto it = mClasses.begin(); it != mClasses.end(); ++it) fn(it->first, 0, context);
}

bool cRZCOMDllDirector::GetClassObject(uint32_t clsid, uint32_t iid, void **ppv)
{
    // 0x1ae199.
    for (auto it = mChildren.begin(); it != mChildren.end(); ++it)
        if ((*it)->GetClassObject(clsid, iid, ppv)) return true;

    auto it = mClasses.find(clsid);
    if (it == mClasses.end()) return false;
    void *factory = it->second.factory;
    switch (it->second.kind) {
    case comdir::kObjectFactory: {
        // The factory returns a new object (a null return would crash here, as it does in the original); it is asked
        // for the interface, and destroyed (AddRef, Release) if it has none.
        cIGZUnknown *object = reinterpret_cast<cIGZUnknown *(__cdecl *)()>(factory)();
        if (object->QueryInterface(iid, ppv)) return true;
        object->AddRef();
        object->Release();
        return false;
    }
    case comdir::kCreateFunction:
        // The result is whatever the creating function returns in al.
        return reinterpret_cast<bool(__cdecl *)(uint32_t, void **)>(factory)(iid, ppv);
    default:
        return false;
    }
}

bool cRZCOMDllDirector::CanUnloadNow()
{
    // 0x1ae11d: only asks the sub-directors while the reference count is zero.
    // ORIGINAL-BUG: a director that is still referenced (count != 0) answers true ("can unload"), the opposite of
    // DllCanUnloadNow's contract; nothing in the game calls the slot.
    if (mRefCount == 0)
        for (auto it = mChildren.begin(); it != mChildren.end(); ++it)
            if (!(*it)->CanUnloadNow()) return false;
    return true;
}

bool cRZCOMDllDirector::OnUnload()
{
    // 0x144910.
    return true;
}

uint32_t cRZCOMDllDirector::RefCount()
{
    // 0x46390.
    return mRefCount;
}

uint32_t cRZCOMDllDirector::RemoveRef()
{
    // 0x1ae2f5: decrements a non-zero count and returns the new value (0 stays 0).
    uint32_t count = mRefCount;
    if (count != 0) mRefCount = --count;
    return count;
}

cIGZFrameWork *cRZCOMDllDirector::FrameWork()
{
    // 0x147800.
    return mFramework;
}

cIGZCOM *cRZCOMDllDirector::GZCOM()
{
    // 0x1457d0.
    return mCOM;
}

uint32_t cRZCOMDllDirector::GetDirectorID()
{
    // The original's base vtable holds _purecall here (the base is never used on its own).
    return static_cast<uint32_t>(_purecall());
}

bool cRZCOMDllDirector::GetLibraryPath(cIGZString &out)
{
    // 0x1ae22c: hands the path's text and length to slot 3 of `out`.
    const std::string &text = mLibraryPath.value.mString;
    out.FromChar(text.c_str(), static_cast<uint32_t>(text.size()));
    return true;
}

uint32_t cRZCOMDllDirector::GetHeapAllocatedSize()
{
    // 0x1ae227 jumps to 0x6c6b0, which returns 0.
    return 0;
}

// cIGZFrameWorkHooks: the shared `mov al, 1; ret` 0x144910.
bool cRZCOMDllDirector::PreFrameWorkInit() { return true; }
bool cRZCOMDllDirector::PreAppInit() { return true; }
bool cRZCOMDllDirector::PostAppInit() { return true; }
bool cRZCOMDllDirector::PreAppShutdown() { return true; }
bool cRZCOMDllDirector::PostAppShutdown() { return true; }
bool cRZCOMDllDirector::PostSystemServiceShutdown() { return true; }
bool cRZCOMDllDirector::AbortiveQuit() { return true; }
bool cRZCOMDllDirector::OnInstall() { return true; }

// ---------------------------------------------------------------------------------------------
// The constructors' registration steps

void cRZCOMDllDirector::RunSteps(unsigned index)
{
    const comdir::DirectorSpec &spec = comdir::kDirectorSpecs[index];
    for (unsigned i = 0; i < spec.stepCount; ++i) {
        const comdir::Step &step = comdir::kSteps[spec.firstStep + i];
        switch (step.op) {
        case comdir::kAddCls:
            AddCls(step.a, GameAddress<void>(step.b));
            break;
        case comdir::kAddClsDirect:
            AddClsDirect(step.a, GameAddress<void>(step.b));
            break;
        case comdir::kAddDirector:
            AddDirector(comdir::GetDirector(step.a));
            break;
        case comdir::kAddDirectorIfPresent:
            // The original calls the getter once to test it and again to use it.
            if (comdir::GetDirector(step.a) != nullptr) AddDirector(comdir::GetDirector(step.a));
            break;
        case comdir::kAddClsIfSimProbe:
            if (comdir::SimProbe()) AddCls(step.a, GameAddress<void>(step.b));
            break;
        case comdir::kAddTSSGHelper:
            comdir::AddTSSGHelper(this);
            break;
        case comdir::kAddEngineHelper:
            comdir::AddEngineHelper(this);
            break;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// cGZCOMDirector's own slots

uint32_t cGZCOMDirector::GetDirectorID()
{
    // 0x1448f1.
    return kId;
}

bool cGZCOMDirector::StoreFrameWork()
{
    // 0x1448f7.
    auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject());
    mFramework = framework;
    return framework != nullptr;
}

// ---------------------------------------------------------------------------------------------
// Adapters (the old entry points of the slots and members)

bool __fastcall cRZCOMDllDirectorAdapters::QueryInterface(cIGZCOMDirector *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::QueryInterface(riid, out);
}
bool __fastcall cRZCOMDllDirectorAdapters::InitializeCOM(cIGZCOMDirector *face, void *, cIGZCOM *com, const cIGZString &path)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::InitializeCOM(com, path);
}
void __fastcall cRZCOMDllDirectorAdapters::EnumClassObjects(cIGZCOMDirector *face, void *, void *callback, void *context)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::EnumClassObjects(callback, context);
}
bool __fastcall cRZCOMDllDirectorAdapters::GetClassObject(cIGZCOMDirector *face, void *, uint32_t clsid, uint32_t iid, void **ppv)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::GetClassObject(clsid, iid, ppv);
}
bool __fastcall cRZCOMDllDirectorAdapters::CanUnloadNow(cIGZCOMDirector *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::CanUnloadNow();
}
uint32_t __fastcall cRZCOMDllDirectorAdapters::RemoveRef(cIGZCOMDirector *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::RemoveRef();
}
bool __fastcall cRZCOMDllDirectorAdapters::GetLibraryPath(cIGZCOMDirector *face, void *, cIGZString &out)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::GetLibraryPath(out);
}
bool __fastcall cRZCOMDllDirectorAdapters::HooksQueryInterface(cIGZFrameWorkHooks *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::QueryInterface(riid, out);
}
uint32_t __fastcall cRZCOMDllDirectorAdapters::HooksRelease(cIGZFrameWorkHooks *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::Release();
}
uint32_t __fastcall cRZCOMDllDirectorAdapters::HooksAddRef(cIGZFrameWorkHooks *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cRZCOMDllDirector *>(face)->cRZCOMDllDirector::AddRef();
}
void __fastcall cRZCOMDllDirectorAdapters::AddCls(cRZCOMDllDirector *self, void *, uint32_t clsid, void *factory)
{
    [[msvc::forceinline_calls]] return self->cRZCOMDllDirector::AddCls(clsid, factory);
}
void __fastcall cRZCOMDllDirectorAdapters::AddClsDirect(cRZCOMDllDirector *self, void *, uint32_t clsid, void *factory)
{
    [[msvc::forceinline_calls]] return self->cRZCOMDllDirector::AddClsDirect(clsid, factory);
}
void __fastcall cRZCOMDllDirectorAdapters::AddDirector(cRZCOMDllDirector *self, void *, cRZCOMDllDirector *sub)
{
    [[msvc::forceinline_calls]] return self->cRZCOMDllDirector::AddDirector(sub);
}

uint32_t __fastcall cGZCOMDirectorAdapters::GetDirectorID(cIGZCOMDirector *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOMDirector *>(face)->cGZCOMDirector::GetDirectorID();
}
bool __fastcall cGZCOMDirectorAdapters::StoreFrameWork(cIGZCOMDirector *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cGZCOMDirector *>(face)->cGZCOMDirector::StoreFrameWork();
}

} // namespace rz
