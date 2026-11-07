#include "gz/cGZClipboardManager.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZCriticalSection.h"

#include <new>

namespace {

using Clip = cIGZClipboard;

// Holds the manager's lock through its cIGZCriticalSection vtable, as the original does.
struct Locked {
    explicit Locked(cRZCriticalSection *lock) : mLock(As<ifaces::cIGZCriticalSection>(lock)) { mLock->Lock(); }
    ~Locked() { mLock->Unlock(); }
    ifaces::cIGZCriticalSection *mLock;
};

// The clipboard's reference count (its cIGZUnknown slots 1 and 2).
void AddRefClip(Clip *clip) { As<cIGZUnknown>(clip)->AddRef(); }
void ReleaseClip(Clip *clip) { As<cIGZUnknown>(clip)->Release(); }

// A call to one of the clipboard's other virtual slots, with the caller's argument words passed on unchanged (the
// slots' names and types: cIGZClipboard, gz/cGZClipboardManagerClipboard.h).
template <typename R, typename... Args> R CallSlot(Clip *clip, int slot, Args... args)
{
    return VirtualSlot<R(__thiscall *)(void *, Args...)>(clip, slot)(clip, args...);
}

} // namespace

// ---- cGZClipboardManager ---------------------------------------------------------------------------------------------

cGZClipboardManager::cGZClipboardManager()
    : rz::cRZSystemService(kServiceId, kPriority), mInitialised(false), mLockedRefCount(0)
{
    mLock.Construct();
}

cGZClipboardManager::~cGZClipboardManager()
{
    // The lock first, then the map (the member's destructor), then the service part.
    mLock.Destruct();
}

bool cGZClipboardManager::QueryInterface(uint32_t riid, void **ppv)
{
    Locked lock(&mLock);
    if (riid == kInterfaceId) {
        *ppv = static_cast<cIGZClipboardManager *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, ppv);
}

uint32_t cGZClipboardManager::AddRef()
{
    Locked lock(&mLock);
    uint32_t count = mLockedRefCount + 1;
    mLockedRefCount = count;
    return count;
}

uint32_t cGZClipboardManager::Release()
{
    {
        Locked lock(&mLock);
        uint32_t count = mLockedRefCount;
        if (count > 1) {
            --count;
            mLockedRefCount = count;
            return count; // the lock is released on the way out
        }
    }
    // Last reference: shut down, then delete (the deleting destructor in the +8 face's slot 3).
    Shutdown();
    delete this;
    return 0;
}

bool cGZClipboardManager::Init()
{
    Locked lock(&mLock);
    if (!mInitialised) {
        mInitialised = true;
        Clip *const clip = new cGZClipboardManagerClipboard;
        clip->AddRef();
        clip->SetId(0);
        clip->Init();
        mClipboards.insert(cGZClipboardMap::value_type(0, clip));
    }
    return true;
}

bool cGZClipboardManager::Shutdown()
{
    Locked lock(&mLock);
    if (mInitialised) {
        mInitialised = false;
        RemoveClipboard(kAll);
    }
    return true;
}

// ---- slots 5..20 -----------------------------------------------------------------------------------------------------

uint32_t cGZClipboardManager::GetCount()
{
    Locked lock(&mLock);
    return static_cast<uint32_t>(mClipboards.size());
}

Clip *cGZClipboardManager::GetClipboard(uint32_t id)
{
    Locked lock(&mLock);
    auto it = mClipboards.find(id);
    return it == mClipboards.end() ? nullptr : it->second;
}

uint32_t cGZClipboardManager::GetClipboards(Clip **out, uint32_t *count)
{
    Locked lock(&mLock);
    uint32_t size = static_cast<uint32_t>(mClipboards.size());
    if (!out) {
        *count = size;
        return size;
    }
    if (*count < size) size = *count;
    *count = size;
    uint32_t copied = 0;
    if (size) {
        auto it = mClipboards.begin();
        do {
            Clip *clip = it->second;
            AddRefClip(clip);
            out[copied++] = clip;
            ++it;
        } while (copied < *count);
    }
    return *count;
}

bool cGZClipboardManager::AddClipboard(Clip *clip)
{
    Locked lock(&mLock);
    uint32_t id = CallSlot<uint32_t>(clip, 5);
    RemoveClipboard(id); // replace an existing one
    AddRefClip(clip);
    CallSlot<void>(clip, 3);
    mClipboards.insert(cGZClipboardMap::value_type(id, clip));
    return true;
}

bool cGZClipboardManager::RemoveClipboard(uint32_t id)
{
    Locked lock(&mLock);
    cGZClipboardMap &map = mClipboards;
    if (id == kAll) {
        for (auto it = map.begin(); it != map.end(); ++it) {
            Clip *clip = it->second;
            CallSlot<void>(clip, 4);
            ReleaseClip(clip);
        }
        map.clear();
        return true;
    }
    auto it = map.find(id);
    if (it == map.end()) return false;
    Clip *clip = it->second;
    CallSlot<void>(clip, 4);
    ReleaseClip(clip);
    map.erase(it);
    return true;
}

bool cGZClipboardManager::DetachClipboard(Clip *clipboard)
{
    Locked lock(&mLock);
    cGZClipboardMap &map = mClipboards;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it->second != clipboard) continue;
        CallSlot<void>(clipboard, 4);
        ReleaseClip(clipboard);
        map.erase(it);
        return true;
    }
    return false;
}

// 11..20 -----------------------------------------------------------------------------------------

bool cGZClipboardManager::Slot11(uint32_t a1, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAny) {
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) {
            if (CallSlot<bool>(it->second, 10, a1)) return true;
        }
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    return CallSlot<bool>(clip, 10, a1);
}

uint32_t cGZClipboardManager::Slot12(void *array, uint32_t *count, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAll) {
        uint32_t total = 0;
        if (*count != 0) {
            auto it = mClipboards.begin();
            for (;;) {
                if (it == mClipboards.end()) break;
                uint32_t remaining = *count - total;
                total += CallSlot<uint32_t>(it->second, 11, static_cast<void *>(static_cast<uint32_t *>(array) + total), &remaining);
                ++it;
                if (total >= *count) break;
            }
        }
        *count = total;
        return *count;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return *count;
    return CallSlot<uint32_t>(clip, 11, array, count);
}

uint32_t cGZClipboardManager::Slot13(uint32_t a1)
{
    Locked lock(&mLock);
    for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) {
        if (CallSlot<bool>(it->second, 10, a1)) return it->first;
    }
    return kNone;
}

bool cGZClipboardManager::Slot14(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAny) {
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) {
            if (CallSlot<bool>(it->second, 12, a1, a2, a3)) return true;
        }
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    return CallSlot<bool>(clip, 12, a1, a2, a3);
}

bool cGZClipboardManager::Slot15(uint32_t a1, uint32_t a2, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAll) {
        // The results are ignored and the answer is false.
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) CallSlot<bool>(it->second, 14, a1, a2);
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    return CallSlot<bool>(clip, 14, a1, a2);
}

bool cGZClipboardManager::Slot16(uint32_t a1, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAll) {
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) CallSlot<void>(it->second, 15, a1);
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    CallSlot<void>(clip, 15, a1);
    return true;
}

bool cGZClipboardManager::Slot17(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAny) {
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) {
            if (CallSlot<bool>(it->second, 16, a1, a2, a3)) return true;
        }
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    return CallSlot<bool>(clip, 16, a1, a2, a3);
}

bool cGZClipboardManager::Slot18(uint32_t a1, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAny) {
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) {
            if (CallSlot<bool>(it->second, 17, a1)) return true;
        }
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    return CallSlot<bool>(clip, 17, a1);
}

bool cGZClipboardManager::Slot19(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t /*unused*/, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAll) {
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) CallSlot<bool>(it->second, 18, a1, a2, a3, 1u);
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    return CallSlot<bool>(clip, 18, a1, a2, a3, 1u);
}

bool cGZClipboardManager::Slot20(uint32_t a1, uint32_t a2, uint32_t id)
{
    Locked lock(&mLock);
    if (id == kAll) {
        for (auto it = mClipboards.begin(); it != mClipboards.end(); ++it) CallSlot<bool>(it->second, 19, a1, a2);
        return false;
    }
    Clip *clip = GetClipboard(id);
    if (!clip) return false;
    return CallSlot<bool>(clip, 19, a1, a2);
}

// ---- adapters --------------------------------------------------------------------------------------------------------

cGZClipboardManager *__fastcall cGZClipboardManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZClipboardManager;
}
