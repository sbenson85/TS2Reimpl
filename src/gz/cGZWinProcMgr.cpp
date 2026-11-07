// cGZWinProcMgr (lane KM): the whole class in the standalone shape (src/gz/cGZWinProcMgr.h). Built for speed in the game.
#include "gz/cGZWinProcMgr.h"

#include "game/vtable.h"
#include "gz/cGZWinCtrlMgr.h" // cIGZWinCtrlMgr
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject

namespace {

// The window's slot 7 (0x5386d516): gives it a window procedure (null clears it).
bool WinSetProc(void *window, void *proc) { return RawSlot<7, bool>(window, proc); }

} // namespace

cGZWinProcMgr::cGZWinProcMgr() : rz::cRZSystemService(kServiceId, kPriority), mDefaultWindowId(kDefaultWindowId) {}

cGZWinProcMgr::~cGZWinProcMgr() = default; // the map, then the service part

cIGZWinProcMgr *__cdecl cGZWinProcMgr::Create() { return new cGZWinProcMgr; }

bool cGZWinProcMgr::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == cIGZWinProcMgr::kIid) {
        *out = static_cast<cIGZWinProcMgr *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

bool cGZWinProcMgr::Shutdown()
{
    // Slot 4 (through the cIGZWinProcMgr face) with the first key until the map is empty.
    while (mProcs.size() != 0) static_cast<cIGZWinProcMgr *>(this)->Slot4(mProcs.begin()->first);
    return true;
}

bool cGZWinProcMgr::Slot3(uint32_t) { return true; }
bool cGZWinProcMgr::Slot4(uint32_t) { return true; }

uint32_t cGZWinProcMgr::GetClassObject(uint32_t classId, uint32_t iid, void **out)
{
    return static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, iid, out);
}

bool cGZWinProcMgr::FindWindow(uint32_t windowId, WinProcRef &window)
{
    ScopedRef winMgr{nullptr};
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kWinMgrServiceId, kWinMgrIid, &winMgr.mObject);
    bool found = false;
    // The control manager's CreateControl (slot 26): the creators first, then its default control; with iid 1 and the
    // default type 0x88963e39 it creates a generic window (it searches nothing).
    if (winMgr.mObject) found = As<cIGZWinCtrlMgr>(winMgr.mObject)->CreateControl(windowId, 1, window.AsPPVoid());
    winMgr.ReleaseObject();
    return found;
}

bool cGZWinProcMgr::AttachProc(cIGZUnknown *window, cIGZUnknown *proc)
{
    if (!window || !proc) return false;
    WinProcRef win, winProc; // the queries' results are not tested, the pointers are
    window->QueryInterface(kWinIid, win.AsPPVoid());
    if (!win) return false;
    proc->QueryInterface(kWinProcIid, winProc.AsPPVoid());
    if (!winProc) return false;
    if (WinSetProc(win.Get(), winProc.Get())) return true;
    WinSetProc(win.Get(), nullptr);
    return false;
}

bool cGZWinProcMgr::CreateProcForDefaultWindow(uint32_t classId, uint32_t iid, void **out)
{
    WinProcRef window, proc;
    FindWindow(mDefaultWindowId, window);
    GetClassObject(classId, 1, proc.AsPPVoid());
    if (!AttachProc(window.Get(), proc.Get())) return false;
    return window->QueryInterface(iid, out);
}

bool cGZWinProcMgr::CreateProcForWindow(uint32_t classId, uint32_t windowId, uint32_t iid, void **out)
{
    WinProcRef window, proc;
    FindWindow(windowId, window);
    GetClassObject(classId, 1, proc.AsPPVoid());
    if (!AttachProc(window.Get(), proc.Get())) return false;
    return window->QueryInterface(iid, out);
}

bool cGZWinProcMgr::AttachProcToDefaultWindow(cIGZUnknown *proc, uint32_t iid, void **out)
{
    WinProcRef window;
    FindWindow(mDefaultWindowId, window);
    if (!AttachProc(window.Get(), proc)) return false;
    return window->QueryInterface(iid, out);
}

bool cGZWinProcMgr::AttachProcToWindow(cIGZUnknown *proc, uint32_t windowId, uint32_t iid, void **out)
{
    WinProcRef window;
    FindWindow(windowId, window);
    if (!AttachProc(window.Get(), proc)) return false;
    return window->QueryInterface(iid, out);
}

