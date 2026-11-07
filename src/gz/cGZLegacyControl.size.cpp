// cGZLegacyControl (lane MC): src/gz/cGZLegacyControl.h. Built for size, as the game's.
#include "gz/cGZLegacyControl.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZWinMgrW32.h" // cIGZWinMgr
#include "gz/cGZLegacyControlRegistrations.h"
#include "gz/cGZLegacyScriptWriter.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZWin.h"
#include "misc/UtilitiesC.h" // GetCachedServiceObjectD
#include "rz/StaticGetters.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

namespace {

constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kUint32Variant = 6;

ifaces::cIGZWin *Win(cIGZUnknown *window) { return As<ifaces::cIGZWin>(window); }
cIGZCOM *GZCOM() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }
cIGZScriptVocab *&SharedVocab() { return *GameAddress<cIGZScriptVocab *>(cGZLegacyControl::kVocabCell); }
LegacyTypeMap &Types() { return *GameAddress<LegacyTypeMap>(cGZLegacyControl::kTypesCell); }
LegacyHandlerMap &Handlers() { return *GameAddress<LegacyHandlerMap>(cGZLegacyControl::kHandlersCell); }

// GetClassObject for a window: the original tests the low byte of the result.
bool MakeWindow(uint32_t classId, cIGZUnknown **out)
{
    return static_cast<uint8_t>(GZCOM()->GetClassObject(classId, cGZLegacyControl::kWinIid, reinterpret_cast<void **>(out))) != 0;
}

// The window classes' registrations (foundations F6, lane ME), in the constructor's order.
void RunRegistrations()
{
    RegisterCommonNames();
    RegisterFlatRect();
    RegisterText();
    RegisterTextEdit();
    RegisterListBox();
    RegisterCombo();
    RegisterButton();
    RegisterBitmap();
    RegisterSlider();
    RegisterScrollbar();
    RegisterLineInput();
    RegisterOptionGroup();
    RegisterSpinner();
    RegisterOutline();
    RegisterTextTicker();
    RegisterFileBrowser();
    RegisterTreeView();
    RegisterFolders();
    RegisterGrid();
    RegisterCustom();
    RegisterScrollbar2();
    RegisterGen();
}

} // namespace

bool __cdecl FindUint32Attribute(const cGZScriptAttributes *attributes, uint32_t id, uint32_t *out)
{
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != id || !attribute.mValue) continue;
        // The values are variants (the terp's cRZVariant, or a vocabulary's object): slot 3 is the type.
        cIGZVariant *const value = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (value->GetType() == kUint32Variant) {
            *out = *value->AsUint32Ref();
            return true;
        }
    }
    return false;
}

cGZLegacyControl::cGZLegacyControl()
{
    cIGZScriptVocab *&vocab = SharedVocab();
    if (vocab) {
        vocab->AddRef();
        return;
    }
    GZCOM()->GetClassObject(kVocabClass, cIGZScriptVocab::kIID, reinterpret_cast<void **>(&vocab));
    if (vocab) RunRegistrations();
}

cGZLegacyControl::~cGZLegacyControl()
{
    if (mWindow) {
        if (mOwnsWindow) {
            if (auto *parent = static_cast<ifaces::cIGZWin *>(Win(mWindow)->GetParent())) parent->RemoveChild(mWindow);
            Win(mWindow)->Teardown();
        }
        mWindow->Release();
        mWindow = nullptr;
    }
    if (mParentWindow) {
        mParentWindow->Release();
        mParentWindow = nullptr;
    }
    cIGZScriptVocab *&vocab = SharedVocab();
    if (vocab && vocab->Release() == 0) vocab = nullptr;
    if (mWinProc) {
        mWinProc->Release();
        mWinProc = nullptr;
    }
}

cIGZScriptableControl *__cdecl cGZLegacyControl::Create() { return new cGZLegacyControl; }

bool cGZLegacyControl::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kUnknownIid || riid == cIGZScriptableControl::kIID)
        *out = static_cast<cIGZScriptableControl *>(this);
    else if (riid == cIGZRenderControl::kIID)
        *out = static_cast<cIGZRenderControl *>(this);
    else if (riid == cIGZLegacyControlWindow::kIID)
        *out = static_cast<cIGZLegacyControlWindow *>(this);
    else
        return false;
    AddRef();
    return true;
}

uint32_t cGZLegacyControl::AddRef() { return ++mRefCount; }

uint32_t cGZLegacyControl::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// The class id (attribute -1, required and not 0) names the window's GZCOM class; the type id is attribute -2, else
// the types table's entry for the class (0 without one). When the class makes no window, the first class the types
// table gives that type id is tried, then the default class 0x478d1e6f; with no window, false. The window gets the two
// ids as properties and is owned; then, applying (true): the type's handler (if any), ApplyCommonProperties, the pending
// window procedure; Activate; under the parent window (released) or the window manager's main window; the anchors if
// any anchor flag is set; BringToFront; then the same three calls not applying (false), the procedure's first.
bool cGZLegacyControl::Create(cGZScriptAttributes *attributes)
{
    uint32_t classId = 0;
    if (!FindUint32Attribute(attributes, kClassIdAttribute, &classId) || classId == 0) return false;
    uint32_t typeId = 0;
    if (!FindUint32Attribute(attributes, kTypeIdAttribute, &typeId)) {
        const LegacyTypeMap &types = Types();
        const auto it = types.find(classId);
        if (it != types.end()) typeId = it->second;
    }

    mWindow = nullptr; // ORIGINAL-BUG: a window from an earlier Create is dropped without its Release
    if (!MakeWindow(classId, &mWindow)) {
        bool made = false;
        for (const auto &entry : Types()) {
            if (entry.second != typeId) continue;
            made = MakeWindow(entry.first, &mWindow);
            break;
        }
        if (!made && !MakeWindow(kDefaultWindowClass, &mWindow)) return false;
    }

    Win(mWindow)->SetProperty(kClassIdProperty, new cRZVariant(classId));
    if (typeId) Win(mWindow)->SetProperty(kTypeIdProperty, new cRZVariant(typeId));
    mOwnsWindow = true;

    LegacyHandler handler = nullptr;
    const LegacyHandlerMap &handlers = Handlers();
    const auto found = handlers.find(typeId);
    if (found != handlers.end()) handler = found->second;

    if (handler) handler(Win(mWindow), attributes, true);
    ApplyCommonProperties(Win(mWindow), attributes, true);
    HandWinProc(mWindow, true);
    Win(mWindow)->Activate();
    if (mParentWindow) {
        Win(mParentWindow)->InsertChild(mWindow);
        mParentWindow->Release();
        mParentWindow = nullptr;
    } else {
        cIGZWinMgr *const manager = static_cast<cIGZWinMgr *>(GetCachedServiceObjectD());
        Win(manager->GetMainWindow())->InsertChild(mWindow);
    }
    if (Win(mWindow)->IsFlagSet(kAnchorFlags)) Win(mWindow)->UpdateAnchors();
    Win(mWindow)->BringToFront();
    HandWinProc(mWindow, false);
    if (handler) handler(Win(mWindow), attributes, false);
    ApplyCommonProperties(Win(mWindow), attributes, false);
    return false;
}

cIGZUnknown *cGZLegacyControl::GetVocab() { return SharedVocab(); }

uint32_t cGZLegacyControl::GetId() { return mWindow ? Win(mWindow)->GetID() : 0; }

void cGZLegacyControl::SetId(uint32_t id)
{
    if (mWindow) Win(mWindow)->SetID(id);
}

void cGZLegacyControl::SetOwnsWindow(bool owns) { mOwnsWindow = owns; }

bool cGZLegacyControl::Bind(cIGZScriptableControlSet *, uint32_t, uint32_t, uint32_t) { return true; }

bool cGZLegacyControl::SetParent(cIGZUnknown *parent)
{
    if (mParentWindow) {
        mParentWindow->Release();
        mParentWindow = nullptr;
    }
    cIGZLegacyControlWindow *legacy = nullptr;
    if (parent && parent->QueryInterface(cIGZLegacyControlWindow::kIID, reinterpret_cast<void **>(&legacy))) {
        mParentWindow = legacy->GetWindow();
        if (mParentWindow) mParentWindow->AddRef();
        legacy->Release();
    }
    return true;
}

bool cGZLegacyControl::Render(void *) { return true; }

bool cGZLegacyControl::SetRect(const cGZRect *rect)
{
    if (!mWindow) return false;
    Win(mWindow)->SetAreaRect(const_cast<int32_t *>(&rect->left));
    return true;
}

bool cGZLegacyControl::GetRect(cGZRect *rect)
{
    if (!mWindow) return false;
    Win(mWindow)->GetArea(&rect->left);
    return true;
}

bool cGZLegacyControl::HitTest(const cGZPoint *, void *) { return true; }

cIGZUnknown *cGZLegacyControl::GetWindow() { return mWindow; }

void cGZLegacyControl::SetWindow(cIGZUnknown *window)
{
    if (window == mWindow) return;
    if (window) window->AddRef();
    if (mWindow) mWindow->Release();
    mWindow = window;
}

void cGZLegacyControl::SetWinProc(cIGZUnknown *proc)
{
    if (proc == mWinProc) return;
    if (proc) proc->AddRef();
    if (mWinProc) mWinProc->Release();
    mWinProc = proc;
}

bool cGZLegacyControl::GetScript(cIGZString **out)
{
    cIGZUnknown *const window = GetWindow();
    if (!window) return false;
    cRZString *const made = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    cIGZString *const text = made ? made->Construct()->AsInterface() : nullptr;
    text->AddRef(); // as the original: no test for a failed new
    if (WriteLegacyScript(reinterpret_cast<ifaces::cIGZWin *>(window), made)) {
        *out = text;
        return true;
    }
    text->Release();
    return false;
}

void cGZLegacyControl::HandWinProc(cIGZUnknown *window, bool apply)
{
    if (!apply) return;
    cIGZWinProcHost *host = nullptr;
    if (window->QueryInterface(cIGZWinProcHost::kIID, reinterpret_cast<void **>(&host)) && mWinProc) {
        host->SetWinProc(mWinProc);
        mWinProc->Release();
        mWinProc = nullptr;
    }
    if (host) host->Release();
}
