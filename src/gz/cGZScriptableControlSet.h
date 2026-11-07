#pragma once
// cGZScriptableControlSet (lane MC, foundations F5): the UI script service's result object (COM class 0x5a354595, made
// by the creator 0x85b2c9 the UIScript director registers; the service's Attach asks for it, iid 0x1a35608a): the
// list of the script controls a script run made (the cGZLegacyControl objects, one per window of the script), counted,
// in the order they were added. Its render face hands render and hit-test calls to every control's render face.
// 0x24 bytes, built for size (/O1).
//
// Faces (RTTI order): +0 cIGZScriptableControlSet (game vtable 0xe481a0, 13 slots), +4 cIGZRenderControlSet (0xe481d8,
// 5), then the control array (cGZArray of 4-byte elements, +8) and the count (+0x20). The constructor 0x85bfe2 (only
// reached from the creator's tail jump) and the destructor 0x85c00f are the only code storing the vtables (the
// constructor stores the bases' 0xe48154 / 0xe48188 first), so with the creator hooked every object carries ours.
#include "gz/cGZScriptTerp.h" // cGZArray
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

class cIGZScriptableControlSet;
class cIGZString;

// A point and a rectangle in window coordinates (the render face's hit test).
struct cGZPoint {
    int32_t x;
    int32_t y;
};
struct cGZRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    // 0x85c340 (thiscall, ret 4): the point is inside, the edges included.
    bool Contains(const cGZPoint &point) const;
};
static_assert(sizeof(cGZRect) == 0x10, "cGZRect size");

// cIGZScriptableControl (iid 0x1a356083): one control of a script (cGZLegacyControl's +0 face). The slots the set
// calls; the others are the control's.
class cIGZScriptableControl : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x1a356083;
    virtual bool Create(cGZScriptAttributes *attributes) = 0;           // 3 (cGZLegacyControl 0x86dfdc, ret 4)
    virtual cIGZUnknown *GetVocab() = 0;                                // 4
    virtual uint32_t GetId() = 0;                                       // 5: the window's id (0 without one)
    virtual void SetId(uint32_t id) = 0;                                // 6
    virtual void SetOwnsWindow(bool owns) = 0;                          // 7
    // 8 (ret 0x10): told it is element `index` of `set`, for interface `iid` (cGZLegacyControl: true).
    virtual bool Bind(cIGZScriptableControlSet *set, uint32_t index, uint32_t iid, uint32_t arg) = 0;
    virtual bool SetParent(cIGZUnknown *parent) = 0;                    // 9
};

// cIGZLegacyControl (iid 0x9a4527cb): the control's window (cGZLegacyControl +8).
class cIGZLegacyControlWindow : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x9a4527cb;
    virtual cIGZUnknown *GetWindow() = 0;              // 3: not counted
    virtual void SetOwnsWindow(bool owns) = 0;         // 4
    virtual void SetWindow(cIGZUnknown *window) = 0;   // 5: counted (the old one released)
    virtual void SetWinProc(cIGZUnknown *proc) = 0;    // 6: counted, handed to the window by Create
    virtual bool GetScript(cIGZString **out) = 0;      // 7: the window written as script text (a counted string)
};

// cIGZRenderControl (iid 0x5a356159): a control's render face (cGZLegacyControl +4).
class cIGZRenderControl : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x5a356159;
    virtual bool Render(void *context) = 0;                     // 3
    virtual bool SetRect(const cGZRect *rect) = 0;              // 4
    virtual bool GetRect(cGZRect *rect) = 0;                    // 5
    virtual bool HitTest(const cGZPoint *point, void *arg) = 0; // 6
};

// cIGZScriptableControlSet (iid 0x1a35608a).
class cIGZScriptableControlSet : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x1a35608a;
    using Controls = cGZArray<cIGZScriptableControl *>;
    virtual Controls *GetControls() = 0;                           // 3 (shared 0x3a4f40): the array (null for null)
    virtual bool Add(cIGZScriptableControl *control) = 0;           // 4: counted and appended; false for null
    virtual cIGZScriptableControl *FindById(uint32_t id) = 0;       // 5: the first whose GetId is `id`, or null
    virtual cIGZScriptableControl *GetAt(uint32_t index) = 0;       // 6 (shared 0x7232e8): null past the end
    virtual cIGZUnknown *FindWindowById(uint32_t id) = 0;           // 7: slot 5's control's window, or null
    virtual cIGZUnknown *GetWindowAt(uint32_t index) = 0;           // 8: slot 6's control's window, or null
    virtual void SetOwnsWindows(bool owns) = 0;                     // 9: every control's SetOwnsWindow
    virtual uint32_t GetCount() = 0;                                // 10 (shared 0x153ad0)
    // 11 (ret 8): every control that has interface `iid` bound (slot 8: this, its index, iid, arg) while they all
    // agree; true when none refused.
    virtual bool BindAll(uint32_t iid, uint32_t arg) = 0;
};

// cIGZRenderControlSet (iid 0xfa35615f).
class cIGZRenderControlSet : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xfa35615f;
    virtual bool Render(void *context) = 0;                     // 3: every control's render face; true
    // 4 (ret 8): the controls from the last on until one whose rectangle has the point handles it; false.
    virtual bool HitTest(const cGZPoint *point, void *arg) = 0;
};

class cGZScriptableControlSet : public cIGZScriptableControlSet, public cIGZRenderControlSet {
public:
    static constexpr uint32_t kVTable = 0xe481a0; // the game's, for tests
    static constexpr uint32_t kRenderVTable = 0xe481d8;
    static constexpr uint32_t kClassId = 0x5a354595;
    static constexpr uint32_t kSize = 0x24;

    cGZScriptableControlSet() = default; // 0x85bfe2
    // 0x85c00f: every control released. Slot 12 the deleting destructor 0x85c055.
    virtual ~cGZScriptableControlSet();
    cGZScriptableControlSet(const cGZScriptableControlSet &) = delete;
    cGZScriptableControlSet &operator=(const cGZScriptableControlSet &) = delete;

    // 0x85b2c9 (cdecl, the UIScript director's factory): a new set (null when new failed), not counted.
    static cIGZScriptableControlSet *__cdecl Create();

    // cIGZUnknown (+4 through the thunks 0x85c3d4, 0x85c07c, 0x85c3f3)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x85c388
    uint32_t AddRef() override;                              // shared_vf_85c074
    uint32_t Release() override;                             // 0x85c3dc

    // cIGZScriptableControlSet
    Controls *GetControls() override;                                // shared_vf_3a4f40
    bool Add(cIGZScriptableControl *control) override;               // 0x85c211
    cIGZScriptableControl *FindById(uint32_t id) override;           // 0x85c155
    cIGZScriptableControl *GetAt(uint32_t index) override;           // shared_vf_7232e8
    cIGZUnknown *FindWindowById(uint32_t id) override;               // 0x85c18f
    cIGZUnknown *GetWindowAt(uint32_t index) override;               // 0x85c1d0
    void SetOwnsWindows(bool owns) override;                         // 0x85c453
    uint32_t GetCount() override;                                    // shared_vf_153ad0
    bool BindAll(uint32_t iid, uint32_t arg) override;               // 0x85c481

    // cIGZRenderControlSet
    bool Render(void *context) override;                             // 0x85c3fb
    bool HitTest(const cGZPoint *point, void *arg) override;         // 0x85c084

    Controls mControls;     // +0x08
    uint32_t mRefCount = 0; // +0x20
};
static_assert(sizeof(cGZScriptableControlSet) == cGZScriptableControlSet::kSize, "cGZScriptableControlSet size");
static_assert(offsetof(cGZScriptableControlSet, mControls) == 8, "cGZScriptableControlSet layout");
static_assert(offsetof(cGZScriptableControlSet, mRefCount) == 0x20, "cGZScriptableControlSet layout");
