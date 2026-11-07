#pragma once
#include "gz/cGZClipboardManagerClipboard.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <map>

// The clipboard manager service: a map from clipboard id to clipboard object, behind a recursive
// lock. Most operations lock, then forward to one clipboard (looked up by id through the manager's
// own slot 6) or to all of them (id -1) or to the first one that accepts (id -2), calling the
// clipboard's virtual slots. Original vtables (1.18.0.187): 0xe9edfc (cIGZClipboardManager, 21
// slots), 0xe9ee54 (cIGZSystemService part at +4, 13 slots), 0xe9ee8c (cRZUnknownMT part at +8,
// 6 slots). Service id 0x27ebfffd, priority 0x131334, interface id 0x7ec0010.
//
// The object is 0x38 bytes: the primary vtable, a cRZSystemService at +4 (0x1c bytes), the
// initialised flag at +0x20, its own reference count at +0x24, the map at +0x28 (head pointer, count)
// and the lock (a cRZCriticalSection) at +0x30. The game's vtables are referenced only by the
// original constructor and destructor, which nothing reaches any more (the constructor is hooked).

using cGZClipboardMap = std::map<uint32_t, cIGZClipboard *>;
static_assert(sizeof(cGZClipboardMap) == 8, "the map is a head pointer and a size");

// The manager's interface (game vtable 0xe9ed2c, the one the constructor stores first; RTTI cIGZClipboardManager, id
// 0x7ec0010). Init and Shutdown are declared here as well as on cIGZSystemService: the class's one Init / Shutdown fills
// both, and the +4 face's slots 8 / 9 are the compiler's thunks (0xc6db78, 0xc6e681). Slots 11..20 forward a clipboard
// slot of unknown meaning (the number is the manager's slot; the clipboard's is in the comment) to the clipboard with the
// given id, to all of them (kAll: the result is false) or to the first that returns true (kAny).
class cIGZClipboardManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x7ec0010;

    virtual bool Init() = 0;                                                        // slot 3
    virtual bool Shutdown() = 0;                                                    // slot 4
    virtual uint32_t GetCount() = 0;                                                // slot 5
    virtual cIGZClipboard *GetClipboard(uint32_t id) = 0;            // slot 6 (ret 4)
    // Copies up to *count clipboards (each AddRef'd) into `out`; with a null `out` stores the number of clipboards in
    // *count. Returns the number.
    virtual uint32_t GetClipboards(cIGZClipboard **out, uint32_t *count) = 0; // slot 7 (ret 8)
    virtual bool AddClipboard(cIGZClipboard *clipboard) = 0;         // slot 8 (ret 4)
    virtual bool RemoveClipboard(uint32_t id) = 0;                                  // slot 9 (ret 4; kAll: every one)
    virtual bool DetachClipboard(cIGZClipboard *clipboard) = 0;      // slot 10 (ret 4)
    virtual bool Slot11(uint32_t a1, uint32_t id) = 0;                              // -> clip 10 (kAny)
    virtual uint32_t Slot12(void *array, uint32_t *count, uint32_t id) = 0;         // -> clip 11 (kAll)
    virtual uint32_t Slot13(uint32_t a1) = 0;                                       // -> clip 10, returns the id
    virtual bool Slot14(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t id) = 0;    // -> clip 12 (kAny)
    virtual bool Slot15(uint32_t a1, uint32_t a2, uint32_t id) = 0;                 // -> clip 14 (kAll)
    virtual bool Slot16(uint32_t a1, uint32_t id) = 0;                              // -> clip 15 (kAll)
    virtual bool Slot17(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t id) = 0;    // -> clip 16 (kAny)
    virtual bool Slot18(uint32_t a1, uint32_t id) = 0;                              // -> clip 17 (kAny)
    virtual bool Slot19(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t unused, uint32_t id) = 0; // -> clip 18, fourth argument 1 (kAll)
    virtual bool Slot20(uint32_t a1, uint32_t a2, uint32_t id) = 0;                 // -> clip 19 (kAll)
};

// The manager in the standalone shape (docs/standalone-shape.md): the compiler emits the three vtables and the adjustor
// thunks; the game's constructor 0xc6d222 is hooked to cGZClipboardManagerAdapters::Construct, and our cTSAppFW builds
// the class directly. Its own reference count (+0x24, under the lock) replaces the service part's.
class cGZClipboardManager : public cIGZClipboardManager, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xe9edfc; // the game's, for tests
    static constexpr uint32_t kSystemServiceVTable = 0xe9ee54;
    static constexpr uint32_t kUnknownVTable = 0xe9ee8c;
    static constexpr uint32_t kSize = 0x38;
    static constexpr uint32_t kServiceId = 0x27ebfffd;
    static constexpr int32_t kPriority = 0x131334;
    static constexpr uint32_t kInterfaceId = cIGZClipboardManager::kIid;
    static constexpr uint32_t kAll = 0xffffffff;  // id: every clipboard
    static constexpr uint32_t kAny = 0xfffffffe;  // id: the first clipboard that accepts
    static constexpr uint32_t kNone = 0xfffffffd; // returned by Slot13 when none accepts

    cGZClipboardManager();            // 0xc6d222
    ~cGZClipboardManager() override;  // 0xc6d38e; deleting 0xc6d418 (+8 slot 3, thunk 0xc6d410)

    cGZClipboardManager(const cGZClipboardManager &) = delete;
    cGZClipboardManager &operator=(const cGZClipboardManager &) = delete;

    // cIGZUnknown (all three faces; the +4 / +8 ones through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **ppv) override; // 0xc6dcda (thunks 0xc6dd25, 0xc6dd2d)
    uint32_t AddRef() override;                              // 0xc6d4da (thunks 0xc6d4fd, 0xc6d505)
    uint32_t Release() override;                             // 0xc6e150 (thunks 0xc6e190, 0xc6e198)

    // cIGZClipboardManager (Init / Shutdown also cIGZSystemService's)
    bool Init() override;                                    // 0xc6db04
    bool Shutdown() override;                                // 0xc6e654
    uint32_t GetCount() override;                            // 0xc6d797
    cIGZClipboard *GetClipboard(uint32_t id) override; // 0xc6d762
    uint32_t GetClipboards(cIGZClipboard **out, uint32_t *count) override; // 0xc6da71
    bool AddClipboard(cIGZClipboard *clipboard) override;    // 0xc6d475
    bool RemoveClipboard(uint32_t id) override;                             // 0xc6e1df
    bool DetachClipboard(cIGZClipboard *clipboard) override; // 0xc6e271
    bool Slot11(uint32_t a1, uint32_t id) override;                                         // 0xc6dbf0
    uint32_t Slot12(void *array, uint32_t *count, uint32_t id) override;                    // 0xc6d646
    uint32_t Slot13(uint32_t a1) override;                                                  // 0xc6da1e
    bool Slot14(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t id) override;               // 0xc6d7b4
    bool Slot15(uint32_t a1, uint32_t a2, uint32_t id) override;                            // 0xc6e329
    bool Slot16(uint32_t a1, uint32_t id) override;                                         // 0xc6d5dc
    bool Slot17(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t id) override;               // 0xc6d91e
    bool Slot18(uint32_t a1, uint32_t id) override;                                         // 0xc6d8ac
    bool Slot19(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t unused, uint32_t id) override; // 0xc6e51c
    bool Slot20(uint32_t a1, uint32_t a2, uint32_t id) override;                            // 0xc6e4ac

    bool mInitialised;                 // 0x20 (Init creates clipboard 0 once; Shutdown removes every clipboard)
    volatile uint32_t mLockedRefCount; // 0x24 the manager's own count, changed under the lock
    cGZClipboardMap mClipboards;       // 0x28 id -> clipboard (head node pointer, size)
    cRZCriticalSection mLock;          // 0x30
};

static_assert(sizeof(cGZClipboardManager) == cGZClipboardManager::kSize, "cGZClipboardManager size");
static_assert(offsetof(cGZClipboardManager, mServiceId) == 0x10, "cGZClipboardManager layout: the service part at +4");
static_assert(offsetof(cGZClipboardManager, mInitialised) == 0x20 && offsetof(cGZClipboardManager, mLockedRefCount) == 0x24 &&
                  offsetof(cGZClipboardManager, mClipboards) == 0x28 && offsetof(cGZClipboardManager, mLock) == 0x30,
              "cGZClipboardManager layout");

// The constructor hook's entry point (docs/standalone-shape.md): `this` in ecx, the dummy edx. Nothing in the image calls
// the original constructor (only cTSAppFW::AddApplicationServicesHere, ours, builds the class).
struct cGZClipboardManagerAdapters {
    static cGZClipboardManager *__fastcall Construct(void *memory, void *edx); // 0xc6d222
};
