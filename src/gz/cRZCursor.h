#pragma once
// The cursors of the GZ framework (standalone shape): cRZCursor (game vtable 0xe61c64, 15 slots, 0x3c bytes), the
// system cursor cRZSystemCursorW95 (vtable 0xe61cf0) and cRZCursorManager (vtable 0xe61ca4, 18 slots), plus the
// graphics-system helpers they use.
//
// Construction: the cursor constructor with a path 0x9d00ad (called by cTSUIServiceManager's cursor loading 0x73fbc4 on
// 0x3c bytes and by the manager's AddCursorFromFile) and the manager constructor 0x9d0158 (called by
// cGZWinMgrBase::Init on 0x2c bytes) are hooked to placement adapters, and the plain cursor's COM factory 0x9b3460
// (registered by the resource COM director) to ours. The system cursor is made only by the manager's Init, and the plain
// constructor 0x9d0109 otherwise only by the system cursor's, so both are ours through those callers; every slot is
// ours through our vtables.
//
// The Win32 calls go through the game's import slots (game/import.h).
#include "gz/cIGZBuffer.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>

#include "game/owned.h"

namespace ifaces {
class cIGZGraphicSystem;
}

// ---- the graphics system helpers ----

// The graphics system service (id 0xc416025c, face 0x73283c).
constexpr uint32_t kGraphicSystemServiceId = 0xc416025c;
constexpr uint32_t kGraphicSystemIid = 0x73283c;
// The cached graphics system of 0x9c5a05 (uncounted).
constexpr uint32_t kGraphicSystemCell = 0x10f51a4;
TS2_OWNED_STATIC(ifaces::cIGZGraphicSystem *, 10f51a4);

// 0x7ae7f4 (__thiscall on a ScopedRef): null, then the framework's GetSystemService for the graphics system into it
// (nothing without a framework). Returns ref.
ScopedRef *AcquireGraphicSystem(ScopedRef *ref);
// 0x9c5a05 (__cdecl): the cached graphics system; when the cell is empty, acquired into a local reference, stored and
// the local released, so the cell holds no reference of its own. Re-reads the cell.
ifaces::cIGZGraphicSystem *GetGraphicSystem();
// 0x5ba211 (__stdcall, ret 4; the caller's ecx is ignored): the bits per pixel of a colour format, as cGZColorType's.
uint32_t __stdcall ColorFormatBits(uint32_t format);

// ---- cursors ----

class cIGZCursor : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x834c471e;
    virtual bool Slot3() = 0;                                         // slot 3: answers true (folded 0x144910)
    virtual bool Slot4() = 0;                                         // slot 4: answers true (folded 0x144910)
    virtual void SetCursor(bool set) = 0;                             // slot 5
    virtual void ShowCursor(bool show) = 0;                           // slot 6
    virtual void GetCursorHotSpot(int32_t *x, int32_t *y) = 0;        // slot 7
    virtual void SetCursorHotSpot(int32_t x, int32_t y) = 0;          // slot 8
    virtual bool LoadCursor(const cIGZString &path) = 0;              // slot 9
    virtual void SetCursorHandle(void *handle) = 0;                   // slot 10
    virtual void SetCursorBuffer(cIGZBuffer *buffer) = 0;             // slot 11
    virtual bool UsesSystemCursor() = 0;                              // slot 12 (folded 0x1480a0)
    virtual cIGZBuffer *GetCursorBuffer() = 0;                        // slot 13 (folded 0x1457d0)
};

class cRZCursor : public cIGZCursor {
public:
    static constexpr uint32_t kSize = 0x3c;

    cRZCursor();                               // 0x9d0109: no image
    explicit cRZCursor(const cIGZString &path); // 0x9d00ad: then LoadCursor(path) (not through the vtable)
    virtual ~cRZCursor();                       // 0x9d0251; slot 14 the deleting destructor 0x9d02a3
    cRZCursor(const cRZCursor &) = delete;
    cRZCursor &operator=(const cRZCursor &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override; // 0x9d09a9
    uint32_t AddRef() override;                              // folded 0x148fb0
    uint32_t Release() override;                             // folded 0x7acd61
    bool Slot3() override;
    bool Slot4() override;
    void SetCursor(bool set) override;                       // 0x9d0a62
    void ShowCursor(bool show) override;                     // 0x9d0b70
    void GetCursorHotSpot(int32_t *x, int32_t *y) override;  // 0x9d03bf
    void SetCursorHotSpot(int32_t x, int32_t y) override;    // 0x9d0b5b
    bool LoadCursor(const cIGZString &path) override;        // 0x9d04f2
    void SetCursorHandle(void *handle) override;             // 0x9d0b39
    void SetCursorBuffer(cIGZBuffer *buffer) override;       // 0x9d0b0d
    bool UsesSystemCursor() override;
    cIGZBuffer *GetCursorBuffer() override;

protected:
    // The image of a .cur / .ico file through LoadImageA and GetDIBits into a 32-bit buffer (mBuffer); the part of
    // LoadCursor after the path's conversion.
    void LoadImageIntoBuffer(const char *path);
    // The non-virtual SetCursorHandle the destructor calls.
    void ReplaceHandle(void *handle);

    uint32_t mRefCount;                    // +4
    bool mUseSystemCursor;                 // +8 the graphics system's UseSystemCursor at construction
    ifaces::cIGZGraphicSystem *mGraphics;  // +0xc GetGraphicSystem() at construction (uncounted)
    union {
        cRZString mPath; // +0x10 the last path loaded (built and destroyed by hand: cRZString::Construct / Destruct)
    };
    cIGZBuffer *mBuffer;                   // +0x30 the image (counted)
    int16_t mHotX;                         // +0x34
    int16_t mHotY;                         // +0x36
    void *mHandle;                         // +0x38 HCURSOR

    friend struct cRZCursorAdapters;
    friend struct cRZCursorLayout;
};

// The Win32 system cursors by the manager's kinds.
class cRZSystemCursorW95 : public cRZCursor {
public:
    explicit cRZSystemCursorW95(uint32_t kind); // 0x9d0181: LoadCursorA(0, the kind's IDC_*); no handle for others
    ~cRZSystemCursorW95() override;             // slot 14 0x9d02e1: forgets the handle (shared, not destroyed)

    bool LoadCursor(const cIGZString &path) override; // slot 9 folded 0x18a7a0: false
    void SetCursorHandle(void *handle) override;      // slot 10 folded 0x220010: stores only
};

// ---- the cursor manager ----

class cIGZCursorManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xa34c498a;
    virtual bool Init() = 0;                                              // slot 3
    virtual bool Shutdown() = 0;                                          // slot 4
    virtual bool AddCursorFromFile(const cIGZString &path, uint32_t id) = 0; // slot 5
    virtual bool AddCursor(cIGZCursor *cursor, uint32_t id) = 0;          // slot 6
    virtual bool RemoveCursor(uint32_t id) = 0;                           // slot 7
    virtual cIGZCursor *GetCurrentCursor() = 0;                           // slot 8 (folded 0x15f390)
    virtual cIGZCursor *GetCursor(uint32_t id) = 0;                       // slot 9
    virtual bool SetCurrentCursor(cIGZCursor *cursor) = 0;                // slot 10
    virtual bool SetCurrentCursorById(uint32_t id) = 0;                   // slot 11
    virtual bool PushAutoCursor(uint32_t id) = 0;                         // slot 12
    virtual bool PopAutoCursor() = 0;                                     // slot 13
    virtual void ShowCursor() = 0;                                        // slot 14
    virtual void HideCursor() = 0;                                        // slot 15
    virtual bool IsCursorVisible() = 0;                                   // slot 16
};

class cRZCursorManager : public cIGZCursorManager {
public:
    // The manager's system cursors (Init), also their ids.
    static constexpr uint32_t kArrow = 1;
    static constexpr uint32_t kIBeam = 3;
    static constexpr uint32_t kWait = 0xf;
    static constexpr uint32_t kHelp = 0x10;
    static constexpr uint32_t kHand = 0x11;
    // What the original allocates (cGZWinMgrBase::Init); ours fits in it.
    static constexpr uint32_t kGameSize = 0x2c;

    cRZCursorManager();           // 0x9d0158
    virtual ~cRZCursorManager();  // 0x9d0287; slot 17 the deleting destructor 0x9d02c2
    cRZCursorManager(const cRZCursorManager &) = delete;
    cRZCursorManager &operator=(const cRZCursorManager &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;                    // 0x9d09e1
    uint32_t AddRef() override;                                                 // folded 0x145260
    uint32_t Release() override;                                                // 0x9d0a19
    bool Init() override;                                                       // 0x9d03fa
    bool Shutdown() override;                                                   // 0x9d0ba7
    bool AddCursorFromFile(const cIGZString &path, uint32_t id) override;      // 0x9d030a
    bool AddCursor(cIGZCursor *cursor, uint32_t id) override;                  // 0x9d033a
    bool RemoveCursor(uint32_t id) override;                                    // 0x9d0a30
    cIGZCursor *GetCurrentCursor() override;
    cIGZCursor *GetCursor(uint32_t id) override;                                // 0x9d03a0
    bool SetCurrentCursor(cIGZCursor *cursor) override;                         // 0x9d0af3
    bool SetCurrentCursorById(uint32_t id) override;                            // 0x9d0ac4
    bool PushAutoCursor(uint32_t id) override;                                  // 0x9d0967
    bool PopAutoCursor() override;                                              // 0x9d0918
    void ShowCursor() override;                                                 // 0x9d0b86
    void HideCursor() override;                                                 // 0x9d03d6
    bool IsCursorVisible() override;                                            // 0x9d04d2

private:
    // The current cursor's counted assignment (0x9d0c32's: nothing when the same; AddRef the new, Release the old).
    void AssignCurrent(cIGZCursor *cursor);
    // 0x9d0bfe: Release every listed cursor, then empty the list.
    void ReleaseAll();

    bool mInitialized;                        // +4
    uint32_t mRefCount;                       // +8
    std::map<uint32_t, cIGZCursor *> mCursors; // +0xc counted
    std::deque<cIGZCursor *> mAutoStack;      // +0x14 the cursors PushAutoCursor replaced (counted)
    cIGZCursor *mCurrent;                     // +0x28 counted

    friend struct cRZCursorLayout;
};

struct cRZCursorLayout {
    static_assert(sizeof(cRZCursor) == cRZCursor::kSize, "cRZCursor size");
    static_assert(sizeof(cRZSystemCursorW95) == cRZCursor::kSize, "cRZSystemCursorW95 size");
    static_assert(offsetof(cRZCursor, mRefCount) == 4, "cRZCursor layout");
    static_assert(offsetof(cRZCursor, mUseSystemCursor) == 8, "cRZCursor layout");
    static_assert(offsetof(cRZCursor, mGraphics) == 0xc, "cRZCursor layout");
    static_assert(offsetof(cRZCursor, mPath) == 0x10, "cRZCursor layout");
    static_assert(offsetof(cRZCursor, mBuffer) == 0x30, "cRZCursor layout");
    static_assert(offsetof(cRZCursor, mHotX) == 0x34, "cRZCursor layout");
    static_assert(offsetof(cRZCursor, mHotY) == 0x36, "cRZCursor layout");
    static_assert(offsetof(cRZCursor, mHandle) == 0x38, "cRZCursor layout");
    static_assert(sizeof(cRZCursorManager) == cRZCursorManager::kGameSize, "cRZCursorManager size");
    static_assert(offsetof(cRZCursorManager, mInitialized) == 4, "cRZCursorManager layout");
    static_assert(offsetof(cRZCursorManager, mRefCount) == 8, "cRZCursorManager layout");
    static_assert(offsetof(cRZCursorManager, mCursors) == 0xc, "cRZCursorManager layout");
    static_assert(offsetof(cRZCursorManager, mAutoStack) == 0x14, "cRZCursorManager layout");
    static_assert(offsetof(cRZCursorManager, mCurrent) == 0x28, "cRZCursorManager layout");
};

// The hooks' entry points.
struct cRZCursorAdapters {
    static cRZCursor *__cdecl Create();                                                      // 0x9b3460 (COM factory)
    static cRZCursor *__fastcall Construct(void *memory, void *edx, const cIGZString *path); // 0x9d00ad
    static cRZCursorManager *__fastcall ConstructManager(void *memory, void *edx);         // 0x9d0158
    static ScopedRef *__fastcall AcquireGraphicSystem(ScopedRef *ref, void *edx);           // 0x7ae7f4
};
