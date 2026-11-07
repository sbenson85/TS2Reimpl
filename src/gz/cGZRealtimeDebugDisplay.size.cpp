// cGZRealtimeDebugDisplay and its entries (lane LA). Layout, faces and creation: src/gz/cGZRealtimeDebugDisplay.h.
#include "gz/cGZRealtimeDebugDisplay.h"

#include "game/vtable.h"
#include "gz/cIGZMessageTarget.h" // cGZMessage
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessageServer.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <new>

namespace {

constexpr uint32_t kMessageServerId = 0x64063387;
constexpr uint32_t kMessageServerIid = 0xc1085722; // cIGZMessageServer

} // namespace

namespace gz {

// ---- cGZRTDEntry ----

// 0x970399
cGZRTDEntry::cGZRTDEntry(uint32_t key) : mKind(0), mRefs(0), mKey(key) { mName.Construct(); }

// 0x9704f9: the name's destructor.
cGZRTDEntry::~cGZRTDEntry() { mName.Destruct(); }

// 0x97077e
uint32_t cGZRTDEntry::AddRef()
{
    ++mRefs;
    return mRefs;
}

// 0x9709dd: the last reference (a count of 0 or 1) deletes through the destructor slot.
uint32_t cGZRTDEntry::Release()
{
    if (mRefs > 1) {
        --mRefs;
        return mRefs;
    }
    delete this;
    return 0;
}

// ---- cGZRTDValueEntry ----

// 0x9703bd
cGZRTDValueEntry::cGZRTDValueEntry(uint32_t key, uint32_t value)
    : cGZRTDEntry(0), mValueKey(key), mValue(value), mTexts(nullptr), mTextCount(0)
{
}

// 0x9703fc
cGZRTDValueEntry::cGZRTDValueEntry(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count)
    : cGZRTDEntry(0), mValueKey(key), mValue(value), mTexts(texts), mTextCount(count)
{
    for (uint32_t i = 0; i < count; ++i) {
        texts[i]->AddRef(); // ORIGINAL-BUG: never released
        cRZString *copy = static_cast<cRZString *>(::operator new(sizeof(cRZString), std::nothrow));
        // ORIGINAL-BUG: a failed allocation is used as the copy (AddRef on null).
        if (copy) copy = copy->Construct(texts[i]->ToChar());
        copy->AsInterface()->AddRef();
        mCopies.push_back(copy);
    }
}

// 0x97050e: the first mTextCount copies released (the vectors' storage freed by their destructors).
cGZRTDValueEntry::~cGZRTDValueEntry()
{
    for (uint32_t i = 0; i < mTextCount; ++i) mCopies[i]->AsInterface()->Release();
}

} // namespace gz

using gz::cGZRTDEntry;
using gz::cGZRTDValueEntry;

// ---- cGZRealtimeDebugDisplay ----

// 0x9704bb: the base part (id 0x8414a2c2, priority 4,000,000), no message server, hidden, no entries.
cGZRealtimeDebugDisplay::cGZRealtimeDebugDisplay()
    : rz::cRZSystemService(kServiceId, kPriority), mVisible(false), mServer(nullptr), mUnused30(0)
{
}

// 0x97054b: Shutdown (called directly), the list freed, the base part's destructor.
cGZRealtimeDebugDisplay::~cGZRealtimeDebugDisplay() noexcept { cGZRealtimeDebugDisplay::Shutdown(); }

// 0x92f5ed
cIGZRealtimeDebugDisplay *__cdecl cGZRealtimeDebugDisplay::Create() { return new (std::nothrow) cGZRealtimeDebugDisplay; }

cGZRealtimeDebugDisplay *__fastcall cGZRealtimeDebugDisplayAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZRealtimeDebugDisplay;
}

// ---- helpers ----

// The holder 0x762db4 (cleared, then the framework's GetSystemService into it; untouched without a framework) is copied
// into mServer and counted there, then released.
ifaces::cIGZMessageServer *cGZRealtimeDebugDisplay::Server()
{
    if (!mServer) {
        void *held = nullptr;
        if (void *framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kMessageServerId, kMessageServerIid, &held);
        mServer = static_cast<cIGZUnknown *>(held);
        // ORIGINAL-BUG: no null test (a missing message server crashes here).
        mServer->AddRef();
        if (held) static_cast<cIGZUnknown *>(held)->Release();
    }
    return As<ifaces::cIGZMessageServer>(mServer);
}

void cGZRealtimeDebugDisplay::Post(uint32_t id)
{
    cGZMessage message{id, 0, nullptr, 0};
    Server()->MessagePost(&message, 0);
}

bool cGZRealtimeDebugDisplay::Add(cGZRTDEntry *entry)
{
    mEntries.push_back(entry);
    Post(kChangedMessage);
    return true;
}

bool cGZRealtimeDebugDisplay::Remove(cGZRTDEntry *found)
{
    mEntries.remove(found);
    found->Release();
    cGZMessage message{kChangedMessage, 0, nullptr, 0};
    Server()->MessageSend(&message);
    return true;
}

// ---- cIGZUnknown ----

// 0x9709a3: cIGZRealtimeDebugDisplay -> +0x1c (counted through the primary face's AddRef), else the service's.
bool cGZRealtimeDebugDisplay::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cIGZRealtimeDebugDisplay *>(this);
    AddRef();
    return true;
}

uint32_t cGZRealtimeDebugDisplay::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZRealtimeDebugDisplay::Release() { return rz::cRZSystemService::Release(); }

// ---- cIGZSystemService ----

// 0x9708f7: the message server fetched (when not held); true. (The base's Init is not called.)
bool cGZRealtimeDebugDisplay::Init()
{
    Server();
    return true;
}

// 0x970c63: the message server released and dropped; every entry of kind 0-2 released (all of them: no other kind is
// made) and its node's pointer cleared; the list emptied. True. (The base's Shutdown is not called.)
bool cGZRealtimeDebugDisplay::Shutdown()
{
    if (mServer) {
        mServer->Release();
        mServer = nullptr;
    }
    for (cGZRTDEntry *&entry : mEntries) {
        const uint8_t kind = entry->mKind;
        if (kind == 0 || kind == 1 || kind == 2) entry->Release();
        entry = nullptr;
    }
    mEntries.clear();
    return true;
}

// ---- cIGZRealtimeDebugDisplay ----

// 0x970c12
uint8_t cGZRealtimeDebugDisplay::Show()
{
    cGZMessage message{kShownMessage, 0, nullptr, 0};
    mVisible = true;
    return Server()->MessagePost(&message, 0);
}

// 0x9708a7
uint8_t cGZRealtimeDebugDisplay::Hide()
{
    cGZMessage message{kHiddenMessage, 0, nullptr, 0};
    mVisible = false;
    return Server()->MessagePost(&message, 0);
}

// folded 0x1458b0
bool cGZRealtimeDebugDisplay::IsVisible() { return mVisible; }

// 0x9706fc: a kind 0 entry with `key`. ORIGINAL-BUG (every add slot): a failed allocation is used as the entry.
bool cGZRealtimeDebugDisplay::AddEntry0(uint32_t key)
{
    cGZRTDEntry *entry = new (std::nothrow) cGZRTDEntry(key);
    entry->AddRef();
    return Add(entry);
}

// 0x970aae: the first kind 0 entry with `key` (at +8).
bool cGZRealtimeDebugDisplay::RemoveEntry0(uint32_t key)
{
    cGZRTDEntry *found = nullptr;
    for (cGZRTDEntry *entry : mEntries) {
        if (found) break;
        if (entry->mKind == 0 && entry->mKey == key) found = entry;
    }
    if (!found) return false;
    return Remove(found);
}

// 0x970669: a kind 1 entry with copies of the texts.
bool cGZRealtimeDebugDisplay::AddTextEntry1(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count)
{
    cGZRTDValueEntry *entry = new (std::nothrow) cGZRTDValueEntry(key, value, texts, count);
    entry->AddRef();
    entry->mKind = 1;
    return Add(entry);
}

// 0x9705dc
bool cGZRealtimeDebugDisplay::AddEntry1(uint32_t key, uint32_t value)
{
    cGZRTDValueEntry *entry = new (std::nothrow) cGZRTDValueEntry(key, value);
    entry->AddRef();
    entry->mKind = 1;
    return Add(entry);
}

// 0x9709fc: the first kind 1 entry with `key` (at +0x2c).
bool cGZRealtimeDebugDisplay::RemoveEntry1(uint32_t key)
{
    cGZRTDEntry *found = nullptr;
    for (cGZRTDEntry *entry : mEntries) {
        if (found) break;
        if (entry->mKind == 1 && static_cast<cGZRTDValueEntry *>(entry)->mValueKey == key) found = entry;
    }
    if (!found) return false;
    return Remove(found);
}

// 0x970818: as AddTextEntry1, but the kind is left 0 (so only RemoveEntry0 with key 0 finds it).
bool cGZRealtimeDebugDisplay::AddTextEntry0(uint32_t key, uint32_t value, cIGZString *const *texts, uint32_t count)
{
    cGZRTDValueEntry *entry = new (std::nothrow) cGZRTDValueEntry(key, value, texts, count);
    entry->AddRef();
    return Add(entry);
}

// 0x97078b
bool cGZRealtimeDebugDisplay::AddEntry2(uint32_t key, uint32_t value)
{
    cGZRTDValueEntry *entry = new (std::nothrow) cGZRTDValueEntry(key, value);
    entry->AddRef();
    entry->mKind = 2;
    return Add(entry);
}

// 0x970b60: the first kind 2 entry with `key` (at +0x2c).
bool cGZRealtimeDebugDisplay::RemoveEntry2(uint32_t key)
{
    cGZRTDEntry *found = nullptr;
    for (cGZRTDEntry *entry : mEntries) {
        if (found) break;
        if (entry->mKind == 2 && static_cast<cGZRTDValueEntry *>(entry)->mValueKey == key) found = entry;
    }
    if (!found) return false;
    return Remove(found);
}
