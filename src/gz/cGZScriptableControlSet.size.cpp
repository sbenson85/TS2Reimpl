// cGZScriptableControlSet (lane MC): src/gz/cGZScriptableControlSet.h. Built for size, as the game's (0x85bf91..0x85c51a
// are packed).
#include "gz/cGZScriptableControlSet.h"

namespace {

constexpr uint32_t kUnknownIid = 1;

} // namespace

bool cGZRect::Contains(const cGZPoint &point) const
{
    return point.x >= left && point.y >= top && point.x <= right && point.y <= bottom;
}

cGZScriptableControlSet::~cGZScriptableControlSet()
{
    const uint32_t count = mControls.Size();
    for (uint32_t i = 0; i < count; ++i) mControls.mBegin[i]->Release();
    mControls.Erase(mControls.mBegin, mControls.mEnd);
}

cIGZScriptableControlSet *__cdecl cGZScriptableControlSet::Create() { return new cGZScriptableControlSet; }

bool cGZScriptableControlSet::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kUnknownIid || riid == cIGZScriptableControlSet::kIID) {
        *out = static_cast<cIGZScriptableControlSet *>(this);
    } else if (riid == cIGZRenderControlSet::kIID) {
        *out = static_cast<cIGZRenderControlSet *>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

uint32_t cGZScriptableControlSet::AddRef() { return ++mRefCount; }

uint32_t cGZScriptableControlSet::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

cGZScriptableControlSet::Controls *cGZScriptableControlSet::GetControls() { return &mControls; }

bool cGZScriptableControlSet::Add(cIGZScriptableControl *control)
{
    if (!control) return false;
    control->AddRef();
    mControls.PushBack(control);
    return true;
}

cIGZScriptableControl *cGZScriptableControlSet::FindById(uint32_t id)
{
    const uint32_t count = mControls.Size();
    for (uint32_t i = 0; i < count; ++i) {
        cIGZScriptableControl *const control = mControls.mBegin[i];
        if (control->GetId() == id) return control;
    }
    return nullptr;
}

cIGZScriptableControl *cGZScriptableControlSet::GetAt(uint32_t index)
{
    return index < mControls.Size() ? mControls.mBegin[index] : nullptr;
}

namespace {

// The control's window (its cIGZLegacyControl face's slot 3), not counted; null without that face.
cIGZUnknown *WindowOf(cIGZScriptableControl *control)
{
    cIGZUnknown *window = nullptr;
    if (control) {
        cIGZLegacyControlWindow *legacy = nullptr;
        if (control->QueryInterface(cIGZLegacyControlWindow::kIID, reinterpret_cast<void **>(&legacy))) {
            window = legacy->GetWindow();
            legacy->Release();
        }
    }
    return window;
}

} // namespace

cIGZUnknown *cGZScriptableControlSet::FindWindowById(uint32_t id) { return WindowOf(FindById(id)); }

cIGZUnknown *cGZScriptableControlSet::GetWindowAt(uint32_t index) { return WindowOf(GetAt(index)); }

void cGZScriptableControlSet::SetOwnsWindows(bool owns)
{
    const uint32_t count = mControls.Size();
    for (uint32_t i = 0; i < count; ++i) mControls.mBegin[i]->SetOwnsWindow(owns);
}

uint32_t cGZScriptableControlSet::GetCount() { return mControls.Size(); }

bool cGZScriptableControlSet::BindAll(uint32_t iid, uint32_t arg)
{
    bool ok = true;
    for (uint32_t i = 0; i < mControls.Size(); ++i) {
        cIGZScriptableControl *const control = mControls.mBegin[i];
        void *face = nullptr;
        if (control && control->QueryInterface(iid, &face)) {
            ok = ok && control->Bind(this, i, iid, arg);
            control->Release();
        }
    }
    return ok;
}

bool cGZScriptableControlSet::Render(void *context)
{
    const uint32_t count = mControls.Size();
    for (uint32_t i = 0; i < count; ++i) {
        cIGZScriptableControl *const control = mControls.mBegin[i];
        cIGZRenderControl *render = nullptr;
        if (control && control->QueryInterface(cIGZRenderControl::kIID, reinterpret_cast<void **>(&render))) {
            render->Render(context);
            render->Release();
        }
    }
    return true;
}

bool cGZScriptableControlSet::HitTest(const cGZPoint *point, void *arg)
{
    bool handled = false;
    for (uint32_t i = mControls.Size(); i-- > 0 && !handled;) {
        cIGZScriptableControl *const control = mControls.mBegin[i];
        cIGZRenderControl *render = nullptr;
        if (control && control->QueryInterface(cIGZRenderControl::kIID, reinterpret_cast<void **>(&render))) {
            cGZRect rect;
            if (render->GetRect(&rect) && rect.Contains(*point)) handled = render->HitTest(point, arg);
            render->Release();
        }
    }
    return false;
}
