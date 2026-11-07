// The application's property manager, cGZPropertyManagerT<string traits, cGZPropertySetString> (lane PU). The original is /O1
// code but for AddNotification (0xc272c0, /O2): a .size.cpp, as most of it.
#include "gz/cGZPropertyManager.h"

#include "gz/cIGZMessage2Standard.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace {

// A standard message (RZMemory, 0x2c bytes), built and counted once, as the original builds its notification messages.
cIGZMessage2Standard *NewMessage()
{
    void *memory = RZMemory::Malloc(cRZMessage2Standard::kSize);
    auto *message = reinterpret_cast<cIGZMessage2Standard *>(memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr);
    if (message) message->AddRef();
    return message;
}

} // namespace

bool gzpm::KeyLess::operator()(const GZServiceString &a, const GZServiceString &b) const
{
    return _stricmp(a.c_str(), b.c_str()) < 0;
}

// ---- cIGZUnknown -----------------------------------------------------------------------------------------------------

bool cGZPropertyManagerString::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZPropertyManagerString::kIid) {
        *out = static_cast<cIGZPropertyManagerString *>(this);
        AddRef(); // through the vtable, as the original
        return true;
    }
    if (cGZPropertySetString::QueryInterface(riid, out)) return true;
    return rz::cRZSystemService::QueryInterface(riid, out);
}

uint32_t cGZPropertyManagerString::AddRef() { return cGZPropertySetString::AddRef(); }

uint32_t cGZPropertyManagerString::Release() { return cGZPropertySetString::Release(); }

// ---- the set -------------------------------------------------------------------------------------------------------

bool cGZPropertyManagerString::SetProperty(const GZServiceString *key, void *value)
{
    const bool set = cGZPropertySetString::SetProperty(key, value);
    if (set) NotifyWatchers(key);
    return set;
}

void cGZPropertyManagerString::SetDefault(const GZServiceString *key, void *value)
{
    const uint32_t offset = InternKey(key);
    AddEntryIfAbsent(&offset, value);
}

void cGZPropertyManagerString::AddEntryIfAbsent(const uint32_t *keyOffset, void *value)
{
    const uint32_t key = *keyOffset;
    auto at = std::lower_bound(mEntries.begin(), mEntries.end(), key,
                               [](const cGZPropertyEntry &entry, uint32_t k) { return entry.mKey < k; });
    if (at != mEntries.end() && at->mKey == key) return; // a value already there is kept
    cGZPropertyEntry entry;
    entry.Construct(key, value);
    mEntries.emplace(at, entry);
}

// ---- notifications ----------------------------------------------------------------------------------------------------

void cGZPropertyManagerString::NotifyWatchers(const GZServiceString *key)
{
    auto found = mWatchers.find(*key);
    if (found == mWatchers.end()) return;
    cIGZMessage2Standard *message = NewMessage();
    message->SetVoid1(const_cast<GZServiceString *>(&found->first));
    message->SetVoid2(cGZPropertySetString::GetProperty(key));
    for (auto watcher = found->second.begin(); watcher != found->second.end(); ++watcher) {
        message->SetType(watcher->mMessageType);
        watcher->mTarget->DoMessage(message);
    }
    if (message) message->Release();
}

cIGZPropertySetString *cGZPropertyManagerString::GetPropertySet() { return this; }

bool cGZPropertyManagerString::AddNotification(cIGZMessageTarget2 *target, uint32_t messageType, const GZServiceString *key,
                                              void *defaultValue)
{
    auto found = mWatchers.find(*key);
    if (found == mWatchers.end()) {
        // The key's entry is built on the stack first and copied into the node (the original's order of allocations).
        const gzpm::WatchMap::value_type entry(*key, gzpm::WatcherSet());
        auto inserted = mWatchers.insert(entry);
        if (inserted.second) found = inserted.first;
    }
    if (found == mWatchers.end()) return false;

    bool added;
    {
        const gzpm::Watcher probe(target, messageType);
        added = found->second.insert(probe).second;
    }
    if (!added) return false;

    // (The original also builds and destroys an unused cRZString here.)
    if (HasProperty(key)) {
        if (defaultValue) SetDefault(key, defaultValue);
        // The new watcher is told the current value at once.
        cIGZMessage2Standard *message = NewMessage();
        message->SetVoid1(const_cast<GZServiceString *>(&found->first));
        message->SetVoid2(cGZPropertySetString::GetProperty(key));
        message->SetType(messageType);
        target->DoMessage(message);
        if (message) message->Release();
    } else if (defaultValue) {
        SetDefault(key, defaultValue);
        NotifyWatchers(key);
    }
    return true;
}

bool cGZPropertyManagerString::RemoveNotification(cIGZMessageTarget2 *target, uint32_t messageType, const GZServiceString *key)
{
    auto found = mWatchers.find(*key);
    if (found == mWatchers.end()) return false;
    {
        const gzpm::Watcher probe(target, messageType);
        found->second.erase(probe);
    }
    if (found->second.empty()) mWatchers.erase(found);
    return true;
}

bool cGZPropertyManagerString::RemoveAllNotifications(cIGZMessageTarget2 *target)
{
    bool removed = false;
    for (auto key = mWatchers.begin(); key != mWatchers.end();) {
        gzpm::WatcherSet &watchers = key->second;
        for (auto watcher = watchers.begin(); watcher != watchers.end();) {
            auto current = watcher++;
            if (current->mTarget == target) {
                watchers.erase(current);
                removed = true;
            }
        }
        auto current = key++;
        if (current->second.empty()) mWatchers.erase(current);
    }
    return removed;
}

// ---- cIGZSystemService ---------------------------------------------------------------------------------------------

bool cGZPropertyManagerString::Shutdown()
{
    mWatchers.clear();
    Clear(); // through the vtable, as the original
    return true;
}

// ---- adapters ---------------------------------------------------------------------------------------------------------

cGZPropertyManagerString *__fastcall cGZPropertyManagerStringAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZPropertyManagerString;
}
